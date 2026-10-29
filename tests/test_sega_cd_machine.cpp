/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core/machine.hpp"
#include "segacd/firmware.hpp"
#include "segacd/sega_cd.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <span>
#include <vector>

using namespace generator;

namespace {

class NullAudio final : public IAudioBackend {
public:
  void output_samples(std::span<const uint16_t>,
                      std::span<const uint16_t>) override
  {
  }
};

class NullVideo final : public IVideoBackend {
public:
  void render_line(int, std::span<const uint8_t>) override
  {
  }
  void render_line_rgba(int, std::span<const uint32_t>) override
  {
  }
  void present_field() override
  {
  }
};

class NullLogger final : public ILogger {
public:
  void log(LogLevel, std::string_view) override
  {
  }
};

Machine make_machine()
{
  return Machine(std::make_unique<NullAudio>(), std::make_unique<NullVideo>(),
                 std::make_shared<NullLogger>());
}

void write16(std::span<uint8_t> memory, uint32_t addr, uint16_t value)
{
  memory[addr] = (uint8_t)(value >> 8);
  memory[addr + 1] = (uint8_t)value;
}

void write32(std::span<uint8_t> memory, uint32_t addr, uint32_t value)
{
  write16(memory, addr, (uint16_t)(value >> 16));
  write16(memory, addr + 2, (uint16_t)value);
}

std::vector<uint8_t> make_main_bios()
{
  std::vector<uint8_t> bios(SegaCdFirmwareImage::kSize, 0);
  write32(bios, 0, 0x00FF0000);
  write32(bios, 4, 0x00000100);
  std::copy_n("SEGA", 4, bios.begin() + 0x100);
  write16(bios, 0x100, 0x33FC);
  write16(bios, 0x102, 0x0001);
  write16(bios, 0x104, 0x00A1);
  write16(bios, 0x106, 0x2000);
  write16(bios, 0x108, 0x60FE);
  return bios;
}

void load_sub_program(SegaCdSubBus &bus)
{
  const auto ram = bus.program_ram();
  write32(ram, 0, 0x00070000);
  write32(ram, 4, 0x00000100);
  write16(ram, 0x100, 0x702A);
  write16(ram, 0x102, 0x33C0);
  write16(ram, 0x104, 0x00FF);
  write16(ram, 0x106, 0x0030);
  write16(ram, 0x108, 0x60FE);
}

}  // namespace

TEST_CASE("Sega CD RAM windows and communications connect both CPU buses",
          "[sega_cd][machine]")
{
  SegaCd cd;
  cd.power_on();
  auto &main = cd.main_bus();
  auto &sub = cd.sub_bus_port();
  uint16_t value = 0;

  main.write(0x400200, 0x1234, true, true);
  CHECK(cd.sub_cpu().bus().program_ram()[0x200] == 0x12);
  CHECK(cd.sub_cpu().bus().program_ram()[0x201] == 0x34);

  main.write(0x200100, 0xABCD, true, true);
  CHECK(sub.read(0x080100, true, true, &value) == 2);
  main.write(0xA12002, 0, true, true);
  CHECK(sub.read(0x080100, true, true, &value) == 1);
  CHECK(value == 0xABCD);
  sub.write(0x080102, 0x5678, true, true);
  CHECK(cd.word_ram()[0x102] == 0x56);
  CHECK(cd.word_ram()[0x103] == 0x78);

  main.write(0xA12020, 0xCAFE, true, true);
  sub.write(0xFF0030, 0xBEEF, true, true);
  CHECK(cd.main_communication()[0] == 0xCA);
  CHECK(cd.main_communication()[1] == 0xFE);
  CHECK(cd.sub_communication()[0] == 0xBE);
  CHECK(cd.sub_communication()[1] == 0xEF);

  main.write(0xA12000, 1, true, true);
  CHECK(cd.sub_cpu().running());
  main.write(0xA12000, 3, true, true);
  CHECK_FALSE(cd.sub_cpu().running());
}

TEST_CASE("Sega CD 1M word-RAM mode swaps independent main/sub banks",
          "[sega_cd][machine]")
{
  SegaCd cd;
  cd.power_on();
  auto &main = cd.main_bus();
  auto &sub = cd.sub_bus_port();
  uint16_t value = 0;
  main.write(0x200000, 0xAAAA, true, true);
  main.write(0x220000, 0xBBBB, true, true);

  main.write(0xA12002, 3, true, true);
  REQUIRE(main.read(0x200000, true, true, &value) == 1);
  CHECK(value == 0xAAAA);
  REQUIRE(sub.read(0x080000, true, true, &value) == 1);
  CHECK(value == 0xBBBB);

  main.write(0xA12002, 2, true, true);
  REQUIRE(main.read(0x200000, true, true, &value) == 1);
  CHECK(value == 0xBBBB);
  REQUIRE(sub.read(0x080000, true, true, &value) == 1);
  CHECK(value == 0xAAAA);
  REQUIRE(main.read(0xA12002, true, true, &value) == 0);
  CHECK(value == 2);

  SegaCdSector sector{};
  sector.fill(0x5A);
  REQUIRE(cd.cdc().push_sector(sector));
  main.write(0xA12006, 0, true, true);
  main.write(0xA1200C, 0x2002, true, true);
  cd.advance_mclk(108);
  CHECK(cd.word_ram()[0x20000] == 0x5A);
  CHECK(cd.word_ram()[0x20001] == 0x5A);
  main.write(0xA12006, 0, true, true);
  main.write(0xA12008, 2, true, true);
  main.write(0xA1200C, 0x2002, true, true);
  CHECK_FALSE(cd.cdc().dma_active());
}

TEST_CASE(
    "Sega CD sub-CPU writes its communication port through the expansion bus",
    "[sega_cd][machine]")
{
  SegaCd cd;
  load_sub_program(cd.sub_cpu().bus());
  cd.power_on();
  cd.main_bus().write(0xA12000, 1, true, true);
  cd.advance_mclk(10000);
  CHECK(cd.sub_cpu().cpu().d(0) == 42);
  CHECK(cd.sub_cpu().cpu().pc() == 0x108);
  CHECK(cd.sub_communication()[1] == 42);
}

TEST_CASE("Machine boots a synthetic BIOS and schedules the Sega CD sub-CPU",
          "[sega_cd][machine]")
{
  auto machine = make_machine();
  auto cd = std::make_unique<SegaCd>();
  load_sub_program(cd->sub_cpu().bus());
  machine.attach_sega_cd(std::move(cd));
  REQUIRE(machine.load_rom_mem(make_main_bios(), "bios.bin"));
  CHECK(machine.system_kind() == SystemKind::SegaCd);

  machine.run_frame();
  const auto *attached = machine.sega_cd_debug();
  REQUIRE(attached != nullptr);
  CHECK(attached->sub_cpu().running());
  CHECK(attached->sub_cpu().cpu().d(0) == 42);
  CHECK(attached->sub_communication()[0] == 0);
  CHECK(attached->sub_communication()[1] == 42);
  CHECK((machine.io_debug_read(0xA10000) & 0x20) == 0);
  REQUIRE(machine.capture_state());
}
