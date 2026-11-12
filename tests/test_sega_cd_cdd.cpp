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

namespace {

void command(SegaCd &cd, std::array<uint8_t, 10> packet, bool valid = true)
{
  unsigned sum = 0;
  for (size_t i = 0; i < 9; i++)
    sum += packet[i];
  packet[9] = (uint8_t)((~sum & 15) ^ (valid ? 0 : 1));
  for (size_t i = 0; i < 10; i += 2)
    cd.sub_bus_port().write(
        0xFF8042 + (uint32_t)i,
        (uint16_t)((uint16_t)packet[i] << 8 | packet[i + 1]), true, true);
}

std::array<uint8_t, 10> response(SegaCd &cd)
{
  std::array<uint8_t, 10> result{};
  for (size_t i = 0; i < 10; i += 2) {
    uint16_t word = 0;
    cd.sub_bus_port().read(0xFF8038 + (uint32_t)i, true, true, &word);
    result[i] = word >> 8;
    result[i + 1] = word & 255;
  }
  unsigned sum = 0;
  for (auto nibble : result) {
    CHECK(nibble < 16);
    sum += nibble;
  }
  CHECK((sum & 15) == 15);
  return result;
}

void cdc_write(SegaCd &cd, uint8_t index, uint8_t value)
{
  cd.sub_bus_port().write(0xFF8004, index, false, true);
  cd.sub_bus_port().write(0xFF8006, value, false, true);
}

}  // namespace

TEST_CASE("CDD packets return BCD TOC and use HOCK at 75 Hz", "[sega_cd][cdd]")
{
  CddFixture fixture;
  SegaCd cd(fixture.disc, 7500);
  cd.power_on();
  auto &sub = cd.sub_bus_port();
  sub.write(0xFF8032, 0x10, false, true);
  sub.write(0xFF8036, 4, false, true);
  REQUIRE(cd.cdd().irq_pending());
  const auto initial = response(cd);
  for (size_t i = 0; i < 9; i++)
    CHECK(initial[i] == 0);
  CHECK(initial[9] == 15);
  sub.interrupt_ack(4);
  CHECK_FALSE(cd.cdd().irq_pending());
  cd.advance_mclk(99);
  CHECK_FALSE(cd.cdd().irq_pending());
  cd.advance_mclk(1);
  CHECK(cd.cdd().irq_pending());

  command(cd, {2, 0, 0, 4});
  const auto toc = response(cd);
  CHECK(toc[1] == 4);
  CHECK(toc[2] == 0);
  CHECK(toc[3] == 1);
  CHECK(toc[4] == 0);
  CHECK(toc[5] == 2);
  command(cd, {2, 0, 0, 5, 0, 1});
  const auto track = response(cd);
  CHECK(track[1] == 5);
  CHECK(track[4] == 0);
  CHECK(track[5] == 2);
  CHECK((track[6] & 8) != 0);
  CHECK(track[8] == 1);

  command(cd, {4, 0, 0, 0, 0, 1, 7, 0});  // lead-in position 00:01:70
  CHECK(cd.cdd().save().protocol.lead_in == 8);
  CHECK(response(cd)[0] == 2);
  cd.advance_mclk(100);
  CHECK(response(cd)[0] == 4);
  command(cd, {3, 0, 0, 0, 0, 2, 0, 1}, false);
  CHECK(cd.cdd().lba() == 0);
  CHECK_FALSE(cd.cdd().playing());
  command(cd, {3, 0, 0, 0, 0, 2, 0, 1});
  CHECK(cd.cdd().lba() == 0);
  CHECK(cd.cdd().save().protocol.lead_in == 2);
  CHECK(cd.cdd().playing());
}

TEST_CASE("CDD data passes through CDC registers into RAM and the host ports",
          "[sega_cd][cdc]")
{
  CddFixture fixture;
  SegaCd cd(fixture.disc, 7500);
  cd.power_on();
  auto &sub = cd.sub_bus_port();
  sub.write(0xFF8032, 0x20, false, true);
  sub.write(0xFF8004, 0x0500, true, true);  // PRG-RAM DMA
  cdc_write(cd, 1, 0x42);                   // DOUTEN, DTEIEN
  cdc_write(cd, 10, 0x84);                  // decoder and buffer writes
  command(cd, {3, 0, 0, 0, 0, 2, 0, 0});    // absolute 00:02:00 = LBA 0
  for (int i = 0; i < 15; i++) {
    cd.advance_mclk(100);
    if (i == 11) {
      CHECK(cd.cdd().sector_msf() == 147);
      CHECK(cd.cdc().save().registers.header[1] == 1);
      CHECK(cd.cdc().save().registers.header[2] == 0x72);
    }
  }
  uint16_t value = 0;
  sub.write(0xFF8004, 4, false, true);
  sub.read(0xFF8006, false, true, &value);
  CHECK(value == 0);
  sub.read(0xFF8006, false, true, &value);
  CHECK(value == 2);
  sub.read(0xFF8006, false, true, &value);
  CHECK(value == 0);
  sub.read(0xFF8006, false, true, &value);
  CHECK(value == 1);

  const uint16_t source = 4 * 2352 + 4;
  cdc_write(cd, 2, 3);
  cdc_write(cd, 3, 0);
  cdc_write(cd, 4, source & 255);
  cdc_write(cd, 5, source >> 8);
  sub.write(0xFF800A, 0x200 / 8, true, true);
  cdc_write(cd, 6, 0);
  cd.advance_mclk(1);
  const auto ram = cd.sub_cpu().bus().program_ram();
  CHECK(ram[0x200] == 0xA0);
  CHECK(ram[0x201] == 1);
  CHECK(ram[0x202] == 1);
  CHECK(ram[0x203] == 1);
  CHECK(cd.cdc().irq_pending());
  sub.interrupt_ack(5);
  CHECK_FALSE(cd.cdc().irq_pending());  // Gate-array request is acknowledged.
  CHECK((cd.cdc().save().registers.ifstat & 0x40) ==
        0);  // DTE stays set until DTACK.
  cdc_write(cd, 7, 0);
  CHECK_FALSE(cd.cdc().irq_pending());
  sub.read(0xFF8004, true, true, &value);
  CHECK((value & 0xC000) == 0x8000);

  for (bool main : {false, true}) {
    sub.write(0xFF8004, main ? 0x0200 : 0x0300, true, true);
    cdc_write(cd, 2, 3);
    cdc_write(cd, 3, 0);
    cdc_write(cd, 4, source & 255);
    cdc_write(cd, 5, source >> 8);
    cdc_write(cd, 6, 0);
    auto &host = main ? cd.main_bus() : sub;
    host.read(main ? 0xA12008 : 0xFF8008, true, true, &value);
    CHECK(value == 0xA001);
    host.read(main ? 0xA12008 : 0xFF8008, true, true, &value);
    CHECK(value == 0x0101);
  }
}

TEST_CASE("CDD audio commands select CD-DA and program the fader",
          "[sega_cd][audio]")
{
  CddFixture fixture;
  SegaCd cd(fixture.disc, 7500);
  cd.power_on();
  command(cd, {3, 0, 0, 0, 0, 2, 0, 4});
  CHECK(cd.cd_audio().playing());
  CHECK(cd.cd_audio().current_lba() == 4);
  command(cd, {6}, false);
  CHECK(cd.cd_audio().playing());
  command(cd, {6});
  CHECK_FALSE(cd.cd_audio().playing());
  command(cd, {7});
  CHECK(cd.cd_audio().playing());
  cd.sub_bus_port().write(0xFF8034, 0, true, true);
  cd.advance_mclk(1);
  CHECK(cd.cd_audio().save().fader.current < 1024);
  CHECK(cd.cd_audio().save().fader.target == 0);
  command(cd, {1});
  CHECK_FALSE(cd.cd_audio().playing());
}
