/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core/machine.hpp"
#include "segacd/firmware.hpp"
#include "segacd/sega_cd.hpp"
#include "x32/x32.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
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
  write16(ram, 0x106, 0x8020);
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

  main.write(0x020200, 0x1234, true, true);
  CHECK(cd.sub_cpu().bus().program_ram()[0x200] == 0x12);
  CHECK(cd.sub_cpu().bus().program_ram()[0x201] == 0x34);

  main.write(0x200100, 0xABCD, true, true);
  CHECK(sub.read(0x080100, true, true, &value) == 2);
  main.write(0xA12002, 2, true, true);
  CHECK(sub.read(0x080100, true, true, &value) == 1);
  CHECK(value == 0xABCD);
  sub.write(0x080102, 0x5678, true, true);
  CHECK(cd.word_ram()[0x102] == 0x56);
  CHECK(cd.word_ram()[0x103] == 0x78);

  main.write(0xA12010, 0xCAFE, true, true);
  sub.write(0xFF8020, 0xBEEF, true, true);
  CHECK(cd.main_communication()[0] == 0xCA);
  CHECK(cd.main_communication()[1] == 0xFE);
  CHECK(cd.sub_communication()[0] == 0xBE);
  CHECK(cd.sub_communication()[1] == 0xEF);

  main.write(0xA12000, 1, true, true);
  CHECK(cd.sub_cpu().running());
  main.write(0xA12000, 3, true, true);
  CHECK_FALSE(cd.sub_cpu().running());
}

TEST_CASE("Sega CD program RAM banks cover all 512 KiB through the boot window",
          "[sega_cd][machine]")
{
  SegaCd cd;
  cd.power_on();
  auto &main = cd.main_bus();
  auto ram = cd.sub_cpu().bus().program_ram();
  for (unsigned bank = 0; bank < 4; bank++) {
    main.write(0xA12002, (uint16_t)(bank << 6), false, true);
    main.write(0x020000, (uint16_t)(0x1230 + bank), true, true);
    main.write(0x03FFFE, (uint16_t)(0xAB00 + bank), true, true);
    CHECK(ram[bank * 0x20000] == 0x12);
    CHECK(ram[bank * 0x20000 + 1] == 0x30 + bank);
    CHECK(ram[bank * 0x20000 + 0x1FFFF] == bank);
  }
  for (unsigned bank = 0; bank < 4; bank++) {
    uint16_t value = 0;
    main.write(0xA12002, (uint16_t)(bank << 6), false, true);
    main.read(0x020000, true, true, &value);
    CHECK(value == 0x1230 + bank);
    main.write(0x020000, 0xEF00, true, false);
    main.write(0x020000, 0x00CD, false, true);
    main.read(0x020000, true, true, &value);
    CHECK(value == 0xEFCD);
  }

  main.write(0xA12000, 1, false, true);
  CHECK(main.write(0x020000, 0, true, true) == 2);
  CHECK(ram[0x60000] == 0xEF);
  main.write(0xA12000, 3, false, true);
  main.write(0x020000, 0x5678, true, true);
  CHECK(ram[0x60000] == 0x56);
}

TEST_CASE(
    "Sega CD reset release fetches uploaded vectors and bus grant resumes",
    "[sega_cd][machine]")
{
  SegaCd cd;
  cd.power_on();
  load_sub_program(cd.sub_cpu().bus());
  auto &main = cd.main_bus();
  main.write(0xA12000, 3, false, true);
  CHECK_FALSE(cd.sub_cpu().running());
  main.write(0xA12000, 1, false, true);
  CHECK(cd.sub_cpu().cpu().pc() == 0x100);
  CHECK(cd.sub_cpu().cpu().a(7) == 0x70000);
  CHECK(cd.sub_cpu().clock_credit() < 0);
  cd.advance_mclk(10000);
  REQUIRE(cd.sub_cpu().cpu().pc() == 0x108);
  const auto ticks = cd.sub_cpu().master_ticks();
  main.write(0xA12000, 3, false, true);
  main.write(0xA12000, 1, false, true);
  CHECK(cd.sub_cpu().cpu().pc() == 0x108);
  CHECK(cd.sub_cpu().master_ticks() == ticks);

  // Upper-byte interrupt writes must leave the low-byte reset/bus lines alone.
  main.write(0xA12000, 0, true, false);
  CHECK(cd.sub_cpu().running());
  main.write(0xA12000, 2, false, true);
  write32(cd.sub_cpu().bus().program_ram(), 4, 0x108);
  main.write(0xA12000, 3, false, true);
  CHECK_FALSE(cd.sub_cpu().running());
  main.write(0xA12000, 1, false, true);
  CHECK(cd.sub_cpu().cpu().pc() == 0x108);
  CHECK(cd.sub_cpu().master_ticks() == ticks);
}

TEST_CASE(
    "Sega CD memory control preserves byte lanes and exchanges 2M ownership",
    "[sega_cd][machine]")
{
  SegaCd cd;
  cd.power_on();
  auto &main = cd.main_bus();
  auto &sub = cd.sub_bus_port();
  uint16_t value = 0;
  main.read(0xA12002, true, true, &value);
  CHECK(value == 1);
  main.write(0xA12002, 0xC4, false, true);
  main.read(0xA12002, true, true, &value);
  CHECK(value == 0xC1);  // MODE and RET are read-only to the main CPU.
  main.write(0xA12002, 0x0100, true, false);
  main.read(0xA12002, true, true, &value);
  CHECK(value == 0x01C1);
  main.write(0xA12002, 0xC2, false, true);
  sub.read(0xFF8002, true, true, &value);
  CHECK(value == 0x0102);  // Bank bits are main-side only.
  main.write(0xA12002, 0xC1, false, true);
  CHECK_FALSE(cd.base_state().word_ram_main_owner);
  sub.write(0xFF8002, 0x0100, true, false);
  CHECK_FALSE(cd.base_state().word_ram_main_owner);
  sub.write(0xFF8002, 1, false, true);
  main.read(0xA12002, true, true, &value);
  CHECK(value == 0x01C1);

  // Protection applies to sub-CPU writes; the main CPU can still upload BIOS.
  main.write(0x020000, 0xABCD, true, true);
  cd.sub_cpu().bus().write(0x60000, 0, true, true);
  cd.sub_cpu().bus().write(0, 0, true, true);
  CHECK(cd.sub_cpu().bus().program_ram()[0x60000] == 0);
  main.write(0xA12002, 0, false, true);
  main.write(0x020000, 0xABCD, true, true);
  cd.sub_cpu().bus().write(0, 0, true, true);
  CHECK(cd.sub_cpu().bus().program_ram()[0] == 0xAB);
  cd.sub_cpu().bus().write(0x200, 0x1234, true, true);
  CHECK(cd.sub_cpu().bus().program_ram()[0x200] == 0x12);
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

  sub.write(0xFF8002, 4, true, true);
  REQUIRE(main.read(0x200000, true, true, &value) == 1);
  CHECK(value == 0xAAAA);
  REQUIRE(sub.read(0x0C0000, true, true, &value) == 1);
  CHECK(value == 0xBBBB);

  main.write(0xA12002, 0, false, true);
  main.read(0xA12002, true, true, &value);
  CHECK(value ==
        6);  // A swap request keeps the banks until the sub-CPU replies.
  main.read(0x200000, true, true, &value);
  CHECK(value == 0xAAAA);
  sub.write(0xFF8002, 5, true, true);
  REQUIRE(main.read(0x200000, true, true, &value) == 1);
  CHECK(value == 0xBBBB);
  REQUIRE(sub.read(0x0C0000, true, true, &value) == 1);
  CHECK(value == 0xAAAA);
  REQUIRE(main.read(0xA12002, true, true, &value) == 0);
  CHECK(value == 5);

  SegaCdSector sector{};
  sector.fill(0x5A);
  REQUIRE(cd.cdc().push_sector(sector));
  REQUIRE(cd.cdc().start_dma(SegaCdCdcDmaTarget::WordRamSub, 0, 2));
  cd.advance_mclk(108);
  CHECK(cd.word_ram()[0] == 0x5A);
  CHECK(cd.word_ram()[1] == 0x5A);
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

TEST_CASE(
    "Machine BIOS uploads sub-CPU vectors and completes the RAM handshake",
    "[sega_cd][machine]")
{
  auto machine = make_machine();
  machine.attach_sega_cd(std::make_unique<SegaCd>());
  auto bios = make_main_bios();
  write32(bios, 4, 0x200);
  uint32_t pos = 0x200;
  const auto emit = [&](uint16_t word) {
    write16(bios, pos, word);
    pos += 2;
  };
  const auto store = [&](uint32_t addr, uint16_t word) {
    emit(0x33FC);  // MOVE.W #word, absolute.L
    emit(word);
    emit((uint16_t)(addr >> 16));
    emit((uint16_t)addr);
  };
  store(0xA12000, 2);  // Hold reset and request the program bus.
  store(0xA12002, 0);  // Select program RAM bank 0.
  const std::array<uint16_t, 4> vectors{0x0007, 0, 0, 0x0100};
  for (size_t i = 0; i < vectors.size(); i++)
    store(0x020000 + (uint32_t)i * 2, vectors[i]);
  const std::array<uint16_t, 6> sub_program{0x702A, 0x33FC, 1,
                                            0x00FF, 0x8002, 0x60FE};
  for (size_t i = 0; i < sub_program.size(); i++)
    store(0x020100 + (uint32_t)i * 2, sub_program[i]);
  store(0xA12002, 2);  // Give 2M Word RAM to the sub-CPU.
  store(0xA12000, 1);  // Release reset and the program bus.
  emit(0x0839);        // BTST #0, $A12003; BEQ back to BTST
  emit(0);
  emit(0x00A1);
  emit(0x2003);
  emit(0x67F6);
  emit(0x704D);  // MOVEQ #77,D0 proves that the handshake completed.
  const uint32_t done = pos;
  emit(0x60FE);
  REQUIRE(machine.load_rom_mem(bios, "upload_bios.bin"));

  machine.run_frame();
  CHECK(machine.cpu_debug().pc() == done);
  CHECK(machine.cpu_debug().d(0) == 77);
  CHECK(machine.sega_cd_debug()->sub_cpu().cpu().d(0) == 42);
  CHECK(machine.sega_cd_debug()->base_state().word_ram_main_owner);
}

TEST_CASE("Machine CD 32X routes both expansions and saves their state",
          "[sega_cd][machine][x32]")
{
  auto machine = make_machine();
  machine.attach_sega_cd(std::make_unique<SegaCd>());
  std::vector<uint8_t> cartridge(0x1000, 0);
  write16(cartridge, 0, 0x1122);
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  for (auto *boot : {&master, &slave}) {
    write32(*boot, 0, 0x8000);
    write32(*boot, 4, 0x100);
    for (uint32_t address = 0x100; address < boot->size(); address += 2)
      write16(*boot, address, 0x0009);
  }
  machine.attach_x32(std::make_unique<X32>(cartridge, master, slave));

  auto bios = make_main_bios();
  uint32_t pos = 0x108;
  const auto emit = [&](uint16_t word) {
    write16(bios, pos, word);
    pos += 2;
  };
  const auto store = [&](uint32_t addr, uint16_t word) {
    emit(0x33FC);
    emit(word);
    emit((uint16_t)(addr >> 16));
    emit((uint16_t)addr);
  };
  store(0x200100, 0xABCD);
  store(0xA15120, 0x1234);
  store(0xA15100, 0x83);
  emit(0x3039);
  emit(0x0088);
  emit(0);
  emit(0x3239);
  emit(0x0020);
  emit(0x0100);
  emit(0x60FE);
  REQUIRE(machine.load_rom_mem(bios, "cd32x_bios.bin"));
  machine.run_frame();

  CHECK(machine.system_kind() == SystemKind::SegaCdX32);
  CHECK(machine.cpu_debug().d(0) == 0x1122);
  CHECK(machine.cpu_debug().d(1) == 0xABCD);
  CHECK(machine.sega_cd_debug()->word_ram()[0x100] == 0xAB);
  CHECK(machine.sega_cd_debug()->word_ram()[0x101] == 0xCD);
  CHECK(machine.sega_cd_debug()->sub_cpu().master_ticks() != 0);
  CHECK(machine.x32_debug()->communication()[0] == 0x1234);
  CHECK(machine.x32_debug()->master().pc() != 0x100);
  CHECK(machine.x32_debug()->slave().pc() != 0x100);

  const char *path = "test_cd32x_state.gst";
  const auto saved = machine.capture_state();
  REQUIRE(saved);
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out.write((const char *)saved->data(), (std::streamsize)saved->size());
  }
  machine.run_frame();
  REQUIRE(machine.load_state(path));
  const auto restored = machine.capture_state();
  REQUIRE(restored);
  CHECK(*restored == *saved);
  std::remove(path);
}

TEST_CASE("Sega CD mailboxes and flag latches have shared CPU-specific writers",
          "[sega_cd][machine]")
{
  SegaCd cd;
  cd.power_on();
  auto &main = cd.main_bus();
  auto &sub = cd.sub_bus_port();
  uint16_t value = 0;
  main.write(0xA12010, 0x1234, true, true);
  sub.read(0xFF8010, true, true, &value);
  CHECK(value == 0x1234);
  sub.write(0xFF8010, 0, true, true);
  main.read(0xA12010, true, true, &value);
  CHECK(value == 0x1234);
  sub.write(0xFF802E, 0x5678, true, true);
  main.read(0xA1202E, true, true, &value);
  CHECK(value == 0x5678);
  main.write(0xA1202E, 0, true, true);
  sub.read(0xFF802E, true, true, &value);
  CHECK(value == 0x5678);
  main.write(0xA1200E, 0xAB00, true, false);
  sub.write(0xFF800E, 0x00CD, false, true);
  main.read(0xA1200E, true, true, &value);
  CHECK(value == 0xABCD);
  // Sub-CPU byte access aliases both lanes onto CFS.
  sub.write(0xFF800E, 0x1200, true, false);
  main.read(0xA1200E, true, true, &value);
  CHECK(value == 0xAB12);
}

TEST_CASE("Sega CD stopwatch and level-3 timer use 384 sub-CPU clocks",
          "[sega_cd][timer]")
{
  SegaCd cd({}, 12500000);
  cd.power_on();
  auto &sub = cd.sub_bus_port();
  sub.write(0xFF8030, 1, false, true);
  cd.advance_mclk(767);
  CHECK((cd.base_state().gate.pending & 8) == 0);
  uint16_t value = 0;
  sub.read(0xFF800C, true, true, &value);
  CHECK(value == 1);
  cd.advance_mclk(1);
  CHECK((cd.base_state().gate.pending & 8) != 0);
  sub.read(0xFF800C, true, true, &value);
  CHECK(value == 2);
  sub.interrupt_ack(3);
  CHECK((cd.base_state().gate.pending & 8) == 0);
  sub.write(0xFF8030, 0, false, true);
  cd.advance_mclk(768);
  CHECK((cd.base_state().gate.pending & 8) == 0);
  sub.write(0xFF800C, 0, true, true);
  cd.main_bus().read(0xA1200C, true, true, &value);
  CHECK(value == 0);
  cd.advance_mclk(384 * 4096);
  sub.read(0xFF800C, true, true, &value);
  CHECK(value == 0);
}

TEST_CASE("Sega CD backup RAM uses odd bytes across its complete 8 KiB",
          "[sega_cd][backup]")
{
  SegaCd cd;
  cd.power_on();
  auto &sub = cd.sub_bus_port();
  uint16_t value = 0;
  sub.write(0xFE0000, 0xABCD, true, true);
  sub.write(0xFE3FFE, 0x0012, false, true);
  sub.write(0xFE0000, 0, true, false);
  sub.read(0xFE0000, true, true, &value);
  CHECK(value == 0xFFCD);
  sub.read(0xFE3FFE, true, true, &value);
  CHECK(value == 0xFF12);
  cd.power_on();
  sub.read(0xFE0000, true, true, &value);
  CHECK(value == 0xFFCD);
}

TEST_CASE("Sega CD stamp registers render tiles and finish on level 1",
          "[sega_cd][asic]")
{
  SegaCd cd({}, 12500000);
  cd.power_on();
  auto &sub = cd.sub_bus_port();
  auto ram = cd.word_ram();
  std::fill_n(ram.begin() + 0x80, 128, 0xA5);  // stamp 1
  write16(ram, 0x8000, 1);                     // first stamp-map entry
  write16(ram, 0x10000, 0);
  write16(ram, 0x10002, 0);
  write16(ram, 0x10004, 0x800);
  write16(ram, 0x10006, 0);
  sub.write(0xFF8058, 0, true, true);  // 16x16 stamps, 256x256 map
  sub.write(0xFF805A, 0x2000, true, true);
  sub.write(0xFF805C, 0, true, true);
  sub.write(0xFF805E, 0x6000, true, true);
  sub.write(0xFF8060, 0, true, true);
  sub.write(0xFF8062, 8, true, true);
  sub.write(0xFF8064, 1, true, true);
  sub.write(0xFF8066, 0x4000, true, true);
  uint16_t value = 0;
  sub.read(0xFF8058, true, true, &value);
  CHECK(value == 0x8000);
  cd.advance_mclk(39);
  CHECK(cd.asic().stamp_busy());
  cd.advance_mclk(1);
  CHECK_FALSE(cd.asic().stamp_busy());
  for (size_t i = 0; i < 4; i++)
    CHECK(ram[0x18000 + i] == 0xA5);
  CHECK(cd.asic().irq_pending());
  sub.interrupt_ack(4);
  CHECK(cd.asic().irq_pending());
  sub.interrupt_ack(1);
  CHECK_FALSE(cd.asic().irq_pending());
  sub.read(0xFF8064, true, true, &value);
  CHECK(value == 0);
}

TEST_CASE(
    "Sega CD PCM registers drive signed-magnitude samples and marker loops",
    "[sega_cd][pcm]")
{
  SegaCd cd({}, 50000000);
  cd.power_on();
  auto &sub = cd.sub_bus_port();
  auto byte = [&](uint32_t address, uint8_t value) {
    sub.write(address, value, false, true);
  };
  byte(0xFF000E, 0xC0);
  byte(0xFF0000, 255);
  byte(0xFF0002, 0x11);
  byte(0xFF0004, 0);
  byte(0xFF0006, 8);
  byte(0xFF0008, 0);
  byte(0xFF000A, 0);
  byte(0xFF000C, 0);
  byte(0xFF000E, 0x80);
  byte(0xFF2000, 0x81);
  byte(0xFF2002, 0x01);
  byte(0xFF2004, 0xFF);
  byte(0xFF0010, 0xFE);
  cd.advance_mclk(1536);
  CHECK(cd.pcm().sample_left() == 7);
  CHECK(cd.pcm().sample_right() == 7);
  cd.advance_mclk(1536);
  CHECK(cd.pcm().sample_left() == -7);
  cd.advance_mclk(1536);
  CHECK(cd.pcm().sample_left() == 7);
  byte(0xFF0010, 0xFF);
  cd.advance_mclk(1536);
  CHECK(cd.pcm().sample_left() == 0);
}

TEST_CASE("Sega CD main gate HINT register replaces the ROM vector word",
          "[sega_cd][machine]")
{
  auto machine = make_machine();
  machine.attach_sega_cd(std::make_unique<SegaCd>());
  auto bios = make_main_bios();
  write16(bios, 0x100, 0x3039);  // MOVE.W $72,D0 before the override
  write32(bios, 0x102, 0x72);
  write16(bios, 0x106, 0x33FC);  // MOVE.W #$BEEF,$A12006
  write16(bios, 0x108, 0xBEEF);
  write32(bios, 0x10A, 0xA12006);
  write16(bios, 0x10E, 0x3239);  // MOVE.W $72,D1 after the override
  write32(bios, 0x110, 0x72);
  write16(bios, 0x114, 0x60FE);
  REQUIRE(machine.load_rom_mem(bios, "hint_bios.bin"));
  machine.run_frame();
  CHECK(machine.cpu_debug().d(0) == 0);
  CHECK(machine.cpu_debug().d(1) == 0xBEEF);
}

TEST_CASE("Machine delays CD Word RAM DMA but reads program RAM immediately",
          "[sega_cd][machine][vdp_dma]")
{
  bool word_ram = true;
  SECTION("Word RAM supplies an initial bus word")
  {
  }
  SECTION("program RAM has no initial bus word")
  {
    word_ram = false;
  }
  auto machine = make_machine();
  machine.attach_sega_cd(std::make_unique<SegaCd>());
  auto bios = make_main_bios();
  size_t pc = 0x100;
  auto store = [&](uint16_t value, uint32_t address) {
    write16(bios, pc, 0x33FC);  // MOVE.W #value,absolute.L
    write16(bios, pc + 2, value);
    write32(bios, pc + 4, address);
    pc += 8;
  };
  const uint32_t source = word_ram ? 0x200000 : 0x020000;
  store(0x1234, source);
  store(0x5678, source + 2);
  store(0x9ABC, source + 4);
  auto port_write = [&](uint16_t value) { store(value, 0xC00004); };
  port_write(0x8110);
  port_write(0x8F02);
  port_write(0x9303);
  port_write(0x9400);
  port_write(0x9500);
  port_write(0x9600);
  port_write(word_ram ? 0x9710 : 0x9701);
  port_write(0x5000);
  port_write(0x0080);
  write16(bios, pc, 0x60FE);
  REQUIRE(machine.load_rom_mem(bios, "dma_bios.bin"));
  machine.run_frame();
  if (word_ram) {
    CHECK(machine.vdp().vram_word(0x1002) == 0x1234);
    CHECK(machine.vdp().vram_word(0x1004) == 0x5678);
  } else {
    CHECK(machine.vdp().vram_word(0x1000) == 0x1234);
    CHECK(machine.vdp().vram_word(0x1002) == 0x5678);
    CHECK(machine.vdp().vram_word(0x1004) == 0x9ABC);
  }
  CHECK(machine.vdp().save().reg[21] == (word_ram ? 2 : 3));
}
