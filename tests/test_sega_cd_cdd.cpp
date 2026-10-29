/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "segacd/cdd.hpp"
#include "segacd/sega_cd.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace generator;

namespace {

class CddFixture {
public:
  CddFixture()
  {
    std::filesystem::create_directories(root);
    std::vector<uint8_t> bin(6 * kSegaCdSectorBytes);
    for (size_t sector = 0; sector < 6; sector++) {
      std::fill_n(bin.begin() + (std::ptrdiff_t)(sector * kSegaCdSectorBytes),
                  kSegaCdSectorBytes, (uint8_t)(sector + 1));
      bin[sector * kSegaCdSectorBytes + 16] = (uint8_t)(0xA0 + sector);
    }
    std::ofstream out{root / "disc.bin", std::ios::binary | std::ios::trunc};
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char *>(bin.data()),
              (std::streamsize)bin.size());
    out.close();
    std::ofstream cue{root / "game.cue", std::ios::binary | std::ios::trunc};
    REQUIRE(cue.good());
    cue << "FILE \"disc.bin\" BINARY\n"
           "TRACK 01 MODE1/2352\n"
           "INDEX 01 00:00:00\n"
           "TRACK 02 AUDIO\n"
           "INDEX 00 00:00:02\n"
           "INDEX 01 00:00:04\n";
    cue.close();
    auto loaded = SegaCdDisc::load_cue(root / "game.cue");
    REQUIRE(loaded);
    disc = std::move(*loaded);
  }
  ~CddFixture()
  {
    std::filesystem::remove_all(root);
  }

  std::filesystem::path root{"test_sega_cd_cdd_files"};
  SegaCdDisc disc;
};

}  // namespace

TEST_CASE("Sega CD CDD reports TOC and executes seek/read commands",
          "[sega_cd][cdd]")
{
  CddFixture fixture;
  SegaCdCdd cdd(fixture.disc, 7500);
  cdd.reset();
  auto first = cdd.toc_entry(1);
  auto second = cdd.toc_entry(2);
  REQUIRE(first);
  REQUIRE(second);
  CHECK(first->start_lba == 0);
  CHECK(first->sector_count == 2);
  CHECK(first->type == SegaCdTrackType::Mode1);
  CHECK(second->start_lba == 4);
  CHECK(second->sector_count == 2);
  CHECK(second->type == SegaCdTrackType::Audio);
  CHECK_FALSE(cdd.toc_entry(3));

  REQUIRE(cdd.execute(SegaCdCddCommand::Seek, 1));
  CHECK(cdd.lba() == 1);
  CHECK(cdd.status() == SegaCdCddStatus::Paused);
  REQUIRE(cdd.execute(SegaCdCddCommand::ReadSector));
  CHECK(cdd.lba() == 2);
  CHECK(cdd.sector_pending());
  CHECK(cdd.irq_pending());
  CHECK_FALSE(cdd.execute(SegaCdCddCommand::ReadSector));

  auto sector = cdd.take_sector();
  REQUIRE(sector);
  CHECK((*sector)[0] == 2);
  CHECK_FALSE(cdd.sector_pending());
  CHECK_FALSE(cdd.take_sector());
  cdd.acknowledge_interrupt();
  CHECK_FALSE(cdd.irq_pending());
}

TEST_CASE("Sega CD CDD delivers sectors at 75 Hz with pause and backpressure",
          "[sega_cd][cdd]")
{
  CddFixture fixture;
  SegaCdCdd cdd(fixture.disc, 7500);
  cdd.reset();
  REQUIRE(cdd.execute(SegaCdCddCommand::Play));
  cdd.advance_mclk(99);
  CHECK_FALSE(cdd.sector_pending());
  cdd.advance_mclk(1);
  REQUIRE(cdd.sector_pending());
  CHECK((*cdd.take_sector())[0] == 1);
  cdd.acknowledge_interrupt();

  cdd.advance_mclk(100);
  REQUIRE(cdd.sector_pending());
  CHECK((*cdd.take_sector())[0] == 2);
  cdd.acknowledge_interrupt();
  REQUIRE(cdd.execute(SegaCdCddCommand::Pause));
  cdd.advance_mclk(1000);
  CHECK_FALSE(cdd.sector_pending());
  REQUIRE(cdd.execute(SegaCdCddCommand::Resume));
  cdd.advance_mclk(100);
  REQUIRE(cdd.sector_pending());
  CHECK((*cdd.take_sector())[0] == 3);

  REQUIRE(cdd.execute(SegaCdCddCommand::Stop));
  CHECK(cdd.status() == SegaCdCddStatus::Ready);
  CHECK(cdd.lba() == 0);
  CHECK_FALSE(cdd.irq_pending());
}

TEST_CASE("Sega CD gate registers drive CDD and acknowledge sub-CPU IRQ4",
          "[sega_cd][cdd]")
{
  CddFixture fixture;
  SegaCd cd(fixture.disc, 7500);
  cd.power_on();
  auto &main = cd.main_bus();
  auto &sub = cd.sub_bus_port();
  uint16_t status = 0;

  main.write(0xA12006, 1, true, true);
  main.write(0xA12004, (uint8_t)SegaCdCddCommand::Seek, true, true);
  main.write(0xA12004, (uint8_t)SegaCdCddCommand::ReadSector, true, true);
  REQUIRE(main.read(0xA12004, true, true, &status) == 0);
  CHECK((status & 0x30) == 0x30);
  REQUIRE(sub.read(0xFF0004, true, true, &status) == 0);
  CHECK((status & 0x30) == 0x30);
  REQUIRE(cd.cdd().take_sector());

  main.write(0xA12004, (uint8_t)SegaCdCddCommand::Play, true, true);
  cd.advance_mclk(100);
  CHECK(cd.cdd().irq_pending());
  cd.sub_cpu().bus().interrupt_ack(4);
  CHECK_FALSE(cd.cdd().irq_pending());
  CHECK(cd.sub_cpu().bus().acknowledged_level() == 4);
}

TEST_CASE("Sega CD CDD sectors feed CDC DMA into program RAM", "[sega_cd][cdc]")
{
  CddFixture fixture;
  SegaCd cd(fixture.disc, 7500);
  cd.power_on();
  auto &main = cd.main_bus();
  main.write(0xA12004, (uint8_t)SegaCdCddCommand::Play, true, true);
  cd.advance_mclk(100);
  CHECK(cd.cdc().available() == kSegaCdSectorBytes);
  CHECK_FALSE(cd.cdd().sector_pending());

  main.write(0xA12006, 0x200, true, true);
  main.write(0xA1200C, 0x1004, true, true);
  cd.advance_mclk(4);
  const auto ram = cd.sub_cpu().bus().program_ram();
  CHECK(ram[0x200] == 1);
  CHECK(ram[0x201] == 1);
  CHECK(ram[0x202] == 1);
  CHECK(ram[0x203] == 1);
  CHECK(cd.cdc().available() == kSegaCdSectorBytes - 4);
  CHECK(cd.cdc().irq_pending());
  cd.sub_cpu().bus().interrupt_ack(4);
  CHECK_FALSE(cd.cdc().irq_pending());

  uint16_t value = 0;
  main.read(0xA1200A, true, true, &value);
  CHECK(value == 1);
}
