/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "emulator_core.hpp"
#include "core/machine.hpp"
#include "core/state_v3.hpp"
#include "x32/x32.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
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

void put16(std::vector<uint8_t> &bytes, uint32_t address, uint16_t value)
{
  bytes[address] = value >> 8;
  bytes[address + 1] = value;
}

void put32(std::vector<uint8_t> &bytes, uint32_t address, uint32_t value)
{
  bytes[address] = value >> 24;
  bytes[address + 1] = value >> 16;
  bytes[address + 2] = value >> 8;
  bytes[address + 3] = value;
}

void cpu_write32(X32 &device, std::vector<uint8_t> &boot, uint32_t address,
                 uint32_t value)
{
  device.master().set_r(1, address);
  device.master().set_r(2, value);
  device.master().step();
}

template <typename Predicate>
bool advance_until(X32 &device, Predicate predicate, uint64_t limit = 5000)
{
  for (uint64_t ticks = 0; ticks < limit; ticks++) {
    device.advance_mclk(1);
    if (predicate())
      return true;
  }
  return false;
}

void write_file(const char *path, const std::vector<uint8_t> &bytes)
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  REQUIRE(out.good());
  out.write((const char *)bytes.data(), (std::streamsize)bytes.size());
}

}  // namespace

TEST_CASE("X32 decodes adapter buses and boots one SH-2", "[x32]")
{
  std::vector<uint8_t> cartridge(0x1000, 0);
  put32(cartridge, 0, 0x11223344);
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  put16(master, 0x100, 0xE101);
  put16(master, 0x102, 0xE202);
  put16(master, 0x104, 0x312C);
  put16(master, 0x106, 0x0009);
  put32(master, 0x108, 0x200);
  X32 device(cartridge, master, slave);
  device.power_on();
  device.advance_mclk(4);
  CHECK(device.master().pc() == 0x100);
  uint16_t control = 0;
  REQUIRE(device.main_bus().write(0xA15100, 0x83, true, true) == 0);
  REQUIRE(device.main_bus().read(0xA15100, true, true, &control) == 0);
  CHECK(control == 0x83);
  device.master().set_sr(0);
  device.master_bus().write16(0x20004000, 2);
  REQUIRE(advance_until(device, [&] { return device.master().r(1) == 3; }));
  INFO((int)device.master().fault());
  INFO(device.master().pc());
  CHECK(device.master().r(1) == 3);
  CHECK(device.master().r(2) == 2);

  device.master_bus().write32(0x06000000, 0x89ABCDEF);
  CHECK(device.master_bus().read32(0x26000000) == 0x89ABCDEF);
  CHECK(device.master_bus().read32(0x02000000) == 0x11223344);
  REQUIRE(device.main_bus().read(0x200000, true, true, &control) == 0);
  CHECK(control == 0x1122);

  device.main_bus().write(0xA15102, 1, true, true);
  REQUIRE(advance_until(device, [&] { return device.master().pc() == 0x200; }));
  CHECK(device.master().pc() == 0x200);
  device.master_bus().write16(0x2000401A, 0);
  REQUIRE(advance_until(device, [&] { return device.master().pc() == 0x202; }));
  CHECK(device.master().pc() == 0x202);

  device.main_bus().write(0xA15120, 0x4D4F, true, true);
  CHECK(device.communication()[0] == 0x4D4F);
  device.main_bus().write(0xA15104, 2, true, true);
  REQUIRE(device.main_bus().read(0xA15104, true, true, &control) == 0);
  CHECK(control == 2);
  REQUIRE(device.main_bus().read(0xA130EC, true, true, &control) == 0);
  CHECK(control == 0x4D41);
  REQUIRE(device.main_bus().read(0xA130EE, true, true, &control) == 0);
  CHECK(control == 0x5253);
}

TEST_CASE("EmulatorCore loads X32 media with synthetic boot ROMs", "[x32]")
{
  const char *game = "test_x32_game.32x";
  const char *master_path = "test_x32_master.boot";
  const char *slave_path = "test_x32_slave.boot";
  std::vector<uint8_t> cartridge(0x1000, 0);
  std::copy_n("SEGA 32X", 8, cartridge.begin() + 0x100);
  cartridge[0x200] = 0x60;
  cartridge[0x201] = 0xFE;
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  put16(master, 0x100, 0x0009);
  write_file(game, cartridge);
  write_file(master_path, master);
  write_file(slave_path, slave);

  EmulatorCore core(std::make_unique<NullAudio>(),
                    std::make_unique<NullVideo>(),
                    std::make_shared<NullLogger>());
  const auto missing = core.load_media(game);
  REQUIRE_FALSE(missing);
  CHECK(missing.error().find("32X") != std::string::npos);
  CHECK_FALSE(core.rom_loaded());

  MediaLoadOptions options;
  options.x32_firmware = X32Firmware{master_path, slave_path};
  const auto loaded = core.load_media(game, options);
  if (!loaded) {
    INFO(loaded.error());
  }
  REQUIRE(loaded);
  CHECK(core.rom_loaded());
  CHECK(core.media_info().system == SystemKind::X32);
  const auto state = core.capture_state();
  REQUIRE(state);
  CHECK_FALSE(state->empty());
  core.unload_rom();
  CHECK_FALSE(core.rom_loaded());

  std::remove(game);
  std::remove(master_path);
  std::remove(slave_path);
}


TEST_CASE("X32 interleaves master and slave and dispatches INTS", "[x32]")
{
  std::vector<uint8_t> cartridge(0x1000, 0);
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  put32(slave, 0, 0x8000);
  put32(slave, 4, 0x100);
  put16(master, 0x100, 0xE101);
  put16(master, 0x102, 0x0009);
  put16(master, 0x104, 0x0009);
  put16(slave, 0x100, 0xE505);
  put16(slave, 0x102, 0x0009);
  put16(slave, 0x104, 0x0009);
  put32(master, 0x108, 0x200);
  put32(slave, 0x108, 0x200);
  X32 device(cartridge, master, slave);
  device.power_on();
  device.main_bus().write(0xA15100, 0x83, true, true);
  device.master().set_sr(0);
  device.slave().set_sr(0);
  device.master_bus().write16(0x20004000, 2);
  device.slave_bus().write16(0x20004000, 2);
  REQUIRE(advance_until(device, [&] {
    return device.master().pc() == 0x106 && device.slave().pc() == 0x106;
  }));
  CHECK(device.master().r(1) == 1);
  CHECK(device.slave().r(5) == 5);
  CHECK(device.master().pc() == 0x106);
  CHECK(device.slave().pc() == 0x106);

  device.main_bus().write(0xA15102, 2, true, true);
  REQUIRE(advance_until(device, [&] { return device.slave().pc() == 0x200; }));
  REQUIRE(advance_until(device, [&] { return device.master().pc() == 0x108; }));
  CHECK(device.slave().pc() == 0x200);
  CHECK(device.master().pc() == 0x108);
  device.slave_bus().write16(0x2000401A, 0);
  REQUIRE(advance_until(device, [&] { return device.slave().pc() == 0x202; }));
  CHECK(device.slave().pc() == 0x202);
}


TEST_CASE("X32 DREQ bridge drains FIFO through the master DMAC", "[x32]")
{
  std::vector<uint8_t> cartridge(0x1000, 0);
  std::vector<uint8_t> master(0x2000, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  for (uint32_t address = 0x100; address <= 0x10A; address += 2)
    put16(master, address, 0x2122);
  X32 device(cartridge, master, slave);
  device.power_on();
  device.main_bus().write(0xA15100, 0x83, true, true);
  device.master().set_sr(0);
  cpu_write32(device, master, 0xFFFFFF88, 4);
  cpu_write32(device, master, 0xFFFFFF80, 0xA15112);
  cpu_write32(device, master, 0xFFFFFF84, 0x06000000);
  cpu_write32(device, master, 0xFFFFFFA0, 0x40);
  cpu_write32(device, master, 0xFFFFFFB0, 1);
  cpu_write32(device, master, 0xFFFFFF8C, 0x4825);
  device.main_bus().write(0xA15108, 0x3344, true, true);
  device.main_bus().write(0xA1510A, 0x1122, true, true);
  device.main_bus().write(0xA1510C, 0, true, true);
  device.main_bus().write(0xA1510E, 0x0600, true, true);
  device.main_bus().write(0xA15110, 8, true, true);
  device.main_bus().write(0xA15106, 4, true, true);
  device.main_bus().write(0xA15112, 0x1122, true, true);
  device.main_bus().write(0xA15112, 0x3344, true, true);
  device.main_bus().write(0xA15112, 0x5566, true, true);
  device.main_bus().write(0xA15112, 0x7788, true, true);
  uint16_t control = 0;
  REQUIRE(device.main_bus().read(0xA15106, true, true, &control) == 0);
  CHECK(control == 0x84);
  REQUIRE(device.main_bus().read(0xA15108, true, true, &control) == 0);
  CHECK(control == 0x3344);
  REQUIRE(device.main_bus().read(0xA1510A, true, true, &control) == 0);
  CHECK(control == 0x1122);
  REQUIRE(device.main_bus().read(0xA1510C, true, true, &control) == 0);
  CHECK(control == 0);
  REQUIRE(device.main_bus().read(0xA1510E, true, true, &control) == 0);
  CHECK(control == 0x0600);

  REQUIRE(advance_until(device, [&] {
    return device.master_bus().read32(0x06000000) == 0x11223344;
  }));
  CHECK(device.master_bus().read32(0x06000000) == 0x11223344);
  REQUIRE(device.main_bus().read(0xA15106, true, true, &control) == 0);
  CHECK(control == 0x04);
  REQUIRE(device.main_bus().read(0xA15110, true, true, &control) == 0);
  CHECK(control == 4);
  device.main_bus().write(0xA15112, 0x999A, true, true);
  device.main_bus().write(0xA15112, 0xBBCC, true, true);
  REQUIRE(advance_until(device, [&] {
    return device.master_bus().read32(0x06000004) == 0x55667788;
  }));
  CHECK(device.master_bus().read32(0x06000004) == 0x55667788);
  device.main_bus().write(0xA15112, 0xDDEE, true, true);
  device.main_bus().write(0xA15112, 0x0011, true, true);
  REQUIRE(advance_until(device, [&] {
    return device.master_bus().read32(0x06000008) == 0x999ABBCC;
  }));
  CHECK(device.master_bus().read32(0x06000008) == 0x999ABBCC);
  REQUIRE(advance_until(device, [&] {
    return device.master_bus().read32(0x0600000C) == 0xDDEE0011;
  }));
  CHECK(device.master_bus().read32(0x0600000C) == 0xDDEE0011);
  REQUIRE(device.main_bus().read(0xA15106, true, true, &control) == 0);
  CHECK(control == 0);
  REQUIRE(device.main_bus().read(0xA15110, true, true, &control) == 0);
  CHECK(control == 0);
}


namespace {

void write_main(X32 &device, uint32_t address, uint16_t value)
{
  REQUIRE(device.main_bus().write(address, value, true, true) == 0);
}

uint16_t read_main(X32 &device, uint32_t address)
{
  uint16_t value = 0;
  REQUIRE(device.main_bus().read(address, true, true, &value) == 0);
  return value;
}

void prepare_display_zero(X32 &device)
{
  device.master_bus().write16(0x20004000, 0);
  write_main(device, 0xA15100, 1);
  write_main(device, 0xA15180, 0);
  device.set_blanking(false, true);
  device.set_blanking(false, false);
  write_main(device, 0xA1518A, 1);
  device.master_bus().write16(0x20004000, 0x8000);
}

void show_display_zero(X32 &device)
{
  device.master_bus().write16(0x20004000, 0);
  write_main(device, 0xA1518A, 0);
}

class CaptureVideo final : public IVideoBackend {
public:
  void render_line(int, std::span<const uint8_t>) override
  {
    paletted_lines++;
  }
  void render_line_rgba(int line, std::span<const uint32_t> pixels) override
  {
    rgba_lines++;
    if (line >= 0 && line < 240)
      std::copy_n(pixels.begin(), std::min(pixels.size(), lines[line].size()),
                  lines[line].begin());
  }
  void present_field() override
  {
    presents++;
  }
  void reset_counts()
  {
    paletted_lines = 0;
    rgba_lines = 0;
    presents = 0;
  }

  std::array<std::array<uint32_t, 320>, 240> lines{};
  int paletted_lines = 0;
  int rgba_lines = 0;
  int presents = 0;
};

std::vector<uint8_t> make_x32_video_rom()
{
  std::vector<uint8_t> rom(0x8000, 0);
  put32(rom, 0, 0x00FF0000);
  put32(rom, 4, 0x200);
  std::copy_n("SEGA", 4, rom.begin() + 0x100);
  size_t at = 0x200;
  auto emit_control = [&](uint16_t word) {
    put16(rom, at, 0x33FC);
    put16(rom, at + 2, word);
    put16(rom, at + 4, 0x00C0);
    put16(rom, at + 6, 0x0004);
    at += 8;
  };
  emit_control(0x8154);
  emit_control(0x8C81);
  put16(rom, at, 0x60FE);
  return rom;
}

}  // namespace

TEST_CASE("X32 shares video registers palette and framebuffers", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master;
  std::vector<uint8_t> slave;
  X32 device(cartridge, master, slave);
  device.power_on();
  CHECK(read_main(device, 0xA15180) == 0);
  write_main(device, 0xA15100, 1);
  CHECK(read_main(device, 0xA15180) == 0x8000);
  write_main(device, 0xA15180, 0x2083);
  CHECK(read_main(device, 0xA15180) == 0xA083);
  write_main(device, 0xA15182, 1);
  write_main(device, 0xA15202, 0xABCD);
  device.master_bus().write16(0x20004000, 0x8000);
  CHECK(device.master_bus().read16(0x20004102) == 1);
  CHECK(device.master_bus().read32(0x20004202) == 0xABCD0000);

  prepare_display_zero(device);
  device.master_bus().write16(0x20004000, 0);
  CHECK((read_main(device, 0xA1518A) & 1) == 1);
  write_main(device, 0x840000, 0x1200);
  CHECK(read_main(device, 0x840000) == 0x1200);
  write_main(device, 0x840000, 0);
  CHECK(read_main(device, 0x840000) == 0x1200);
  write_main(device, 0x860002, 0x0034);
  CHECK(read_main(device, 0x840002) == 0x0034);
  device.master_bus().write16(0x20004000, 0x8000);
  device.master_bus().write16(0x24000004, 0x5678);
  device.master_bus().write16(0x20004000, 0);
  CHECK(read_main(device, 0x840004) == 0x5678);
  write_main(device, 0xA15180, 2);
  device.set_blanking(false, true);
  device.set_blanking(false, false);
  write_main(device, 0xA1518A, 0);
  CHECK((read_main(device, 0xA1518A) & 1) == 1);
  device.set_blanking(true, false);
  CHECK((read_main(device, 0xA1518A) & 1) == 0);
  CHECK((read_main(device, 0xA1518A) & 0xE000) == 0xA000);
}

TEST_CASE("X32 direct color composition honors priority and through", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master;
  std::vector<uint8_t> slave;
  X32 device(cartridge, master, slave);
  device.power_on();
  prepare_display_zero(device);
  device.master_bus().write16(0x24000000, 0x0200);
  device.master_bus().write16(0x24000400, 0x7C00);
  device.master_bus().write16(0x24000402, 0x03E0);
  device.master_bus().write16(0x24000404, 0x001F);
  device.master_bus().write16(0x24000406, 0xFC00);
  show_display_zero(device);
  write_main(device, 0xA15180, 2);
  device.set_blanking(false, true);
  device.set_blanking(false, false);

  std::array<uint8_t, 4> md{1, 1, 0, 1};
  std::array<uint16_t, 64> cram{};
  cram[1] = 0x000E;
  std::array<uint32_t, 4> output{};
  device.composite_line(0, md, cram.data(), output);
  CHECK(output[0] == 0xFFFF0000);
  CHECK(output[1] == 0xFFFF0000);
  CHECK(output[2] == 0xFFFF0000);
  CHECK(output[3] == 0xFF0000FF);

  write_main(device, 0xA15180, 0x2002);
  device.set_blanking(false, true);
  device.set_blanking(false, false);
  device.composite_line(0, md, cram.data(), output);
  CHECK(output[0] == 0xFF0000FF);
  CHECK(output[1] == 0xFF00FF00);
  CHECK(output[2] == 0xFFFF0000);
  CHECK(output[3] == 0xFFFF0000);
}

TEST_CASE("X32 packed and run-length modes use palette pixels", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master;
  std::vector<uint8_t> slave;
  X32 device(cartridge, master, slave);
  device.power_on();
  prepare_display_zero(device);
  device.master_bus().write16(0x20004000, 0);
  write_main(device, 0xA15202, 0x7C00);
  write_main(device, 0xA15204, 0x03E0);
  device.master_bus().write16(0x20004000, 0x8000);
  device.master_bus().write16(0x24000000, 0x0200);
  device.master_bus().write8(0x24000400, 1);
  device.master_bus().write8(0x24000401, 2);
  device.master_bus().write8(0x24000402, 1);
  device.master_bus().write8(0x24000403, 2);
  show_display_zero(device);
  write_main(device, 0xA15180, 1);
  device.set_blanking(false, true);
  device.set_blanking(false, false);

  std::array<uint8_t, 6> md{};
  std::array<uint16_t, 64> cram{};
  std::array<uint32_t, 6> output{};
  device.composite_line(0, md, cram.data(), output);
  CHECK(output[0] == 0xFF0000FF);
  CHECK(output[1] == 0xFF00FF00);
  write_main(device, 0xA15182, 1);
  device.set_blanking(false, true);
  device.set_blanking(false, false);
  device.composite_line(0, md, cram.data(), output);
  CHECK(output[0] == 0xFF00FF00);
  CHECK(output[1] == 0xFF0000FF);

  prepare_display_zero(device);
  device.master_bus().write16(0x24000400, 0x0201);
  device.master_bus().write16(0x24000402, 0x0302);
  show_display_zero(device);
  write_main(device, 0xA15182, 0);
  write_main(device, 0xA15180, 3);
  device.set_blanking(false, true);
  device.set_blanking(false, false);
  device.composite_line(0, md, cram.data(), output);
  CHECK(output[0] == 0xFF0000FF);
  CHECK(output[1] == 0xFF0000FF);
  CHECK(output[2] == 0xFF00FF00);
  CHECK(output[3] == 0xFF00FF00);
  CHECK(output[4] == 0xFF00FF00);
  CHECK(output[5] == 0xFF000000);
}

TEST_CASE("Machine delivers active X32 video as RGBA scanlines", "[x32]")
{
  auto video = std::make_unique<CaptureVideo>();
  CaptureVideo *capture = video.get();
  Machine machine(std::make_unique<NullAudio>(), std::move(video),
                  std::make_shared<NullLogger>());
  REQUIRE(machine.load_rom_mem(make_x32_video_rom()));
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master;
  std::vector<uint8_t> slave;
  machine.attach_x32(std::make_unique<X32>(cartridge, master, slave));
  X32 *device = machine.x32_debug();
  device->power_on();
  machine.run_frame();
  CHECK(capture->paletted_lines == 224);
  CHECK(capture->rgba_lines == 0);
  CHECK(capture->presents == 1);

  capture->reset_counts();
  prepare_display_zero(*device);
  device->master_bus().write16(0x24000000, 0x0200);
  device->master_bus().write16(0x24000400, 0x7C00);
  show_display_zero(*device);
  write_main(*device, 0xA15180, 0x2002);
  machine.run_frame();
  CHECK(capture->paletted_lines == 0);
  CHECK(capture->rgba_lines == 224);
  CHECK(capture->presents == 1);
  CHECK(capture->lines[0][0] == 0xFF0000FF);
  CHECK(capture->lines[0][1] == 0xFF000000);
}


TEST_CASE("X32 autofill times writes blocks access and wraps pages", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master;
  std::vector<uint8_t> slave;
  X32 device(cartridge, master, slave);
  device.power_on();
  prepare_display_zero(device);
  device.master_bus().write16(0x20004000, 0);
  write_main(device, 0xA15184, 2);
  write_main(device, 0xA15186, 0x01FE);
  device.master_bus().write16(0x20004000, 0x8000);
  device.master_bus().write16(0x20004108, 0xBEEF);
  device.master_bus().write16(0x20004000, 0);
  CHECK((read_main(device, 0xA1518A) & 2) == 2);
  device.advance_mclk(7);
  CHECK((read_main(device, 0xA1518A) & 2) == 2);
  device.advance_mclk(3);
  CHECK(read_main(device, 0xA15186) == 0x01FF);
  CHECK((read_main(device, 0xA1518A) & 2) == 2);
  write_main(device, 0x840020, 0x1234);
  device.advance_mclk(6);
  CHECK((read_main(device, 0xA1518A) & 2) == 0);
  CHECK(read_main(device, 0xA15186) == 1);
  CHECK(read_main(device, 0x8403FC) == 0xBEEF);
  CHECK(read_main(device, 0x8403FE) == 0xBEEF);
  CHECK(read_main(device, 0x840000) == 0xBEEF);
  CHECK(read_main(device, 0x840020) == 0);
}

TEST_CASE("X32 VINT uses its vector mask and clear register", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  put32(master, 0x118, 0x200);
  put16(master, 0x100, 0x0009);
  put16(master, 0x200, 0x0009);
  X32 device(cartridge, master, slave);
  device.power_on();
  write_main(device, 0xA15100, 0x83);
  device.master().set_sr(0);
  device.master_bus().write16(0x20004000, 0x8088);
  CHECK(device.master_bus().read16(0x20004000) == 0x8288);
  device.slave_bus().write16(0x20004000, 0x8082);
  CHECK(device.master_bus().read16(0x20004000) == 0x8288);
  CHECK(device.slave_bus().read16(0x20004000) == 0x8282);
  device.master_bus().write16(0x20004000, 8);

  device.set_blanking(false, false);
  device.set_blanking(true, false);
  device.advance_mclk(5);
  CHECK(device.master().pc() == 0x200);
  CHECK(device.master().sr() == 0xC0);
  device.master_bus().write16(0x20004016, 0);
  device.set_blanking(false, false);
  device.master().set_sr(0);
  REQUIRE(advance_until(device, [&] { return device.master().pc() == 0x202; }));
  CHECK(device.master().pc() == 0x202);
}

TEST_CASE("X32 HINT honors the H-count interval", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  put32(master, 0x110, 0x300);
  put16(master, 0x100, 0x0009);
  put16(master, 0x300, 0x0009);
  X32 device(cartridge, master, slave);
  device.power_on();
  write_main(device, 0xA15100, 0x83);
  device.master().set_sr(0);
  device.master_bus().write16(0x20004000, 4);

  device.set_blanking(false, false);
  device.set_blanking(false, true);
  device.master_bus().write16(0x20004004, 1);
  device.advance_mclk(5);
  CHECK(device.master().pc() == 0x300);
  CHECK(device.master().sr() == 0xA0);
  device.master_bus().write16(0x20004018, 0);
  device.set_blanking(false, false);
  device.master().set_sr(0);
  device.set_blanking(false, true);
  REQUIRE(advance_until(device, [&] { return device.master().pc() == 0x302; }));
  CHECK(device.master().pc() == 0x302);
  device.set_blanking(false, false);
  device.set_blanking(false, true);
  REQUIRE(advance_until(device, [&] { return device.master().pc() == 0x300; }));
  CHECK(device.master().pc() == 0x300);
}

TEST_CASE("X32 HEN permits HINT during VBlank", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  put32(master, 0x110, 0x300);
  put16(master, 0x100, 0x0009);
  put16(master, 0x300, 0x0009);
  X32 device(cartridge, master, slave);
  device.power_on();
  write_main(device, 0xA15100, 0x83);
  device.master().set_sr(0);
  device.master_bus().write16(0x20004000, 4);
  device.set_blanking(true, false);
  device.set_blanking(true, true);
  device.advance_mclk(5);
  CHECK(device.master().pc() == 0x102);

  device.master_bus().write16(0x20004000, 0x84);
  device.set_blanking(true, false);
  device.set_blanking(true, true);
  REQUIRE(advance_until(device, [&] { return device.master().pc() == 0x300; }));
  CHECK(device.master().pc() == 0x300);
}


namespace {

class CaptureAudio final : public IAudioBackend {
public:
  void output_samples(std::span<const uint16_t> left,
                      std::span<const uint16_t> right) override
  {
    left_samples.insert(left_samples.end(), left.begin(), left.end());
    right_samples.insert(right_samples.end(), right.begin(), right.end());
  }

  std::vector<uint16_t> left_samples;
  std::vector<uint16_t> right_samples;
};

}  // namespace

TEST_CASE("X32 PWM registers FIFOs and stereo routing", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master;
  std::vector<uint8_t> slave;
  X32 device(cartridge, master, slave);
  device.power_on();
  write_main(device, 0xA15100, 1);
  write_main(device, 0xA15132, 4);
  CHECK((read_main(device, 0xA15134) & 0x4000) == 0x4000);
  write_main(device, 0xA15134, 100);
  write_main(device, 0xA15134, 200);
  write_main(device, 0xA15134, 300);
  CHECK((read_main(device, 0xA15134) & 0x8000) == 0x8000);
  write_main(device, 0xA15134, 400);
  CHECK((read_main(device, 0xA15134) & 0x8000) == 0x8000);

  write_main(device, 0xA15130, 1);
  device.advance_mclk(10);
  CHECK(device.audio_left() == 8192);
  CHECK(device.audio_right() == 0);
  CHECK((read_main(device, 0xA15134) & 0xC000) == 0);
  write_main(device, 0xA15130, 9);
  CHECK(device.audio_left() == 8192);
  CHECK(device.audio_right() == 8192);
  device.master_bus().write16(0x20004030, 0x0385);
  CHECK(read_main(device, 0xA15130) == 0x0385);
  write_main(device, 0xA15130, 0);
  CHECK(device.audio_left() == 0);
  CHECK(device.audio_right() == 0);
}

TEST_CASE("X32 PWM timer interrupts use vector mask and clear", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x180);
  put32(master, 0x100, 0x200);
  put16(master, 0x180, 0x0009);
  put16(master, 0x200, 0x0009);
  X32 device(cartridge, master, slave);
  device.power_on();
  write_main(device, 0xA15100, 0x83);
  device.master().set_sr(0x60);
  device.master_bus().write16(0x20004000, 1);
  write_main(device, 0xA15132, 4);
  write_main(device, 0xA15134, 3);
  device.master_bus().write16(0x20004030, 0x0181);
  device.advance_mclk(20);
  CHECK(device.audio_left() == 4096);
  device.master().set_sr(0);
  device.advance_mclk(5);
  CHECK(device.master().pc() == 0x200);
  CHECK(device.master().sr() == 0x60);
  device.master_bus().write16(0x20004030, 0);
  device.master_bus().write16(0x2000401C, 0);
  device.master().set_sr(0);
  REQUIRE(advance_until(device, [&] { return device.master().pc() == 0x202; }));
  CHECK(device.master().pc() == 0x202);
  device.master().set_sr(0xF0);
  device.master_bus().write16(0x20004030, 0x0181);
  device.advance_mclk(20);
  device.master().set_sr(0);
  REQUIRE(advance_until(device, [&] { return device.master().pc() == 0x200; }));
  CHECK(device.master().pc() == 0x200);
}

TEST_CASE("X32 PWM DREQ1 fills the FIFO through channel one", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master(0x2000, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  for (uint32_t address = 0x100; address <= 0x10A; address += 2)
    put16(master, address, 0x2122);
  X32 device(cartridge, master, slave);
  device.power_on();
  write_main(device, 0xA15100, 0x83);
  device.master().set_sr(0);
  cpu_write32(device, master, 0xFFFFFF98, 1);
  cpu_write32(device, master, 0xFFFFFF90, 0x06000000);
  cpu_write32(device, master, 0xFFFFFF94, 0x20004034);
  cpu_write32(device, master, 0xFFFFFFA8, 0x40);
  cpu_write32(device, master, 0xFFFFFFB0, 1);
  cpu_write32(device, master, 0xFFFFFF9C, 0x1425);
  device.master_bus().write16(0x06000000, 3);
  write_main(device, 0xA15132, 0x1000);
  device.master_bus().write16(0x20004030, 0x0081);
  device.advance_mclk(10);
  CHECK(read_main(device, 0xA15134) == 0);

  device.master_bus().write16(0x20004030, 0);
  write_main(device, 0xA15132, 4);
  device.master_bus().write16(0x20004030, 0x0081);
  device.advance_mclk(10);
  CHECK(device.audio_left() == 4096);
}

TEST_CASE("Machine mixes X32 PWM into backend audio", "[x32]")
{
  auto audio = std::make_unique<CaptureAudio>();
  CaptureAudio *capture = audio.get();
  Machine machine(std::move(audio), std::make_unique<NullVideo>(),
                  std::make_shared<NullLogger>());
  REQUIRE(machine.load_rom_mem(make_x32_video_rom()));
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master;
  std::vector<uint8_t> slave;
  machine.attach_x32(std::make_unique<X32>(cartridge, master, slave));
  X32 *device = machine.x32_debug();
  device->power_on();
  machine.run_frame();
  write_main(*device, 0xA15100, 1);
  write_main(*device, 0xA15132, 4);
  write_main(*device, 0xA15134, 3);
  write_main(*device, 0xA15130, 1);
  machine.run_frame();
  CHECK(device->audio_left() == 4096);
  REQUIRE_FALSE(capture->left_samples.empty());
  CHECK(std::any_of(capture->left_samples.begin(), capture->left_samples.end(),
                    [](uint16_t sample) { return (int16_t)sample != 0; }));
  CHECK(std::all_of(capture->right_samples.begin(),
                    capture->right_samples.end(),
                    [](uint16_t sample) { return (int16_t)sample == 0; }));
}


namespace {

void attach_stateful_x32(Machine &machine)
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  put32(slave, 0, 0x8000);
  put32(slave, 4, 0x100);
  put16(master, 0x100, 0x0009);
  put16(slave, 0x100, 0x0009);
  machine.attach_x32(std::make_unique<X32>(cartridge, master, slave));
}

void configure_stateful_x32(Machine &machine)
{
  REQUIRE(machine.load_rom_mem(make_x32_video_rom()));
  attach_stateful_x32(machine);
  machine.run_frame();
  X32 *device = machine.x32_debug();
  write_main(*device, 0xA15100, 0x83);
  device->master().set_sr(0);
  device->slave().set_sr(0);
  write_main(*device, 0xA15132, 4);
  write_main(*device, 0xA15134, 3);
  device->master_bus().write16(0x20004030, 0x0181);
  write_main(*device, 0xA15110, 4);
  write_main(*device, 0xA15106, 4);
  write_main(*device, 0xA15112, 0x1122);
  write_main(*device, 0xA15112, 0x3344);
  write_main(*device, 0xA15184, 3);
  write_main(*device, 0xA15186, 0x0040);
  device->master_bus().write16(0x20004108, 0xBEEF);
  machine.debug_advance_mclk(7);
}

}  // namespace

TEST_CASE("X32 state round-trips and continues the saved timeline", "[x32]")
{
  const char *path = "test_x32_timeline.gst";
  auto audio = std::make_unique<NullAudio>();
  auto video = std::make_unique<NullVideo>();
  Machine first(std::move(audio), std::move(video),
                std::make_shared<NullLogger>());
  configure_stateful_x32(first);
  const auto saved = first.capture_state();
  REQUIRE(saved);
  write_file(path, *saved);
  first.run_frame();
  REQUIRE(first.load_state(path));
  const auto restored = first.capture_state();
  REQUIRE(restored);
  CHECK(*restored == *saved);

  Machine second(std::make_unique<NullAudio>(), std::make_unique<NullVideo>(),
                 std::make_shared<NullLogger>());
  configure_stateful_x32(second);
  first.run_frame();
  second.run_frame();
  const auto continued = first.capture_state();
  const auto uninterrupted = second.capture_state();
  REQUIRE(continued);
  REQUIRE(uninterrupted);
  CHECK(*continued == *uninterrupted);
  std::remove(path);
}

TEST_CASE("X32 state rejects different boot firmware without applying", "[x32]")
{
  const char *path = "test_x32_wrong_firmware.gst";
  Machine machine(std::make_unique<NullAudio>(), std::make_unique<NullVideo>(),
                  std::make_shared<NullLogger>());
  configure_stateful_x32(machine);
  const auto saved = machine.capture_state();
  REQUIRE(saved);
  auto chunks = StateV3::deserialize(*saved);
  REQUIRE(chunks);
  bool changed = false;
  for (StateChunk &chunk : *chunks) {
    if (chunk.id == fourcc("X320")) {
      REQUIRE(chunk.data.size() > 8);
      chunk.data[0] ^= 0xFF;
      changed = true;
    }
  }
  REQUIRE(changed);
  std::erase_if(*chunks, [](const StateChunk &chunk) {
    return chunk.id == fourcc("CHKS");
  });
  write_file(path, StateV3::serialize(*chunks));
  const auto before = machine.capture_state();
  REQUIRE(before);
  const auto loaded = machine.load_state(path);
  REQUIRE_FALSE(loaded);
  CHECK(loaded.error() == StateError::WrongRom);
  const auto after = machine.capture_state();
  REQUIRE(after);
  CHECK(*after == *before);
  std::remove(path);
}


TEST_CASE("X32 FM arbitrates VDP access between 68000 and SH-2", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master;
  std::vector<uint8_t> slave;
  X32 device(cartridge, master, slave);
  device.power_on();
  write_main(device, 0xA15100, 1);
  CHECK(device.master_bus().read16(0x20004100) == 0);
  device.master_bus().write16(0x20004000, 0x8000);
  CHECK(read_main(device, 0xA15180) == 0);
  CHECK(device.master_bus().read16(0x20004100) == 0x8000);
  write_main(device, 0xA15200, 0x1234);
  CHECK(device.master_bus().read16(0x20004200) == 0);
  device.master_bus().write16(0x20004200, 0xABCD);
  CHECK(device.master_bus().read16(0x20004200) == 0xABCD);
  device.master_bus().write16(0x20004000, 0);
  write_main(device, 0xA15200, 0x1234);
  CHECK(read_main(device, 0xA15200) == 0x1234);
  CHECK(device.master_bus().read16(0x20004200) == 0);
}

TEST_CASE("Machine soft reset delivers VRES to both SH-2s", "[x32]")
{
  Machine machine(std::make_unique<NullAudio>(), std::make_unique<NullVideo>(),
                  std::make_shared<NullLogger>());
  REQUIRE(machine.load_rom_mem(make_x32_video_rom()));
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master(0x600, 0);
  std::vector<uint8_t> slave(0x600, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  put32(master, 0x120, 0x400);
  put32(slave, 0, 0x8000);
  put32(slave, 4, 0x100);
  put32(slave, 0x120, 0x500);
  put16(master, 0x100, 0x0009);
  put16(master, 0x400, 0x0009);
  put16(slave, 0x100, 0x0009);
  put16(slave, 0x500, 0x0009);
  machine.attach_x32(std::make_unique<X32>(cartridge, master, slave));
  machine.run_frame();
  machine.soft_reset();
  X32 *device = machine.x32_debug();
  write_main(*device, 0xA15100, 0x83);
  device->master().set_sr(0);
  device->slave().set_sr(0);
  machine.debug_advance_mclk(5);
  CHECK(device->master().pc() == 0x400);
  CHECK(device->slave().pc() == 0x500);
  CHECK(device->master().sr() == 0xE0);
  CHECK(device->slave().sr() == 0xE0);

  device->master_bus().write16(0x20004014, 0);
  device->master().set_sr(0);
  device->slave().set_sr(0);
  machine.debug_advance_mclk(45);
  CHECK(device->master().pc() == 0x402);
  CHECK(device->slave().pc() == 0x500);
  device->slave_bus().write16(0x20004014, 0);
  device->slave().set_sr(0);
  machine.debug_advance_mclk(45);
  CHECK(device->slave().pc() == 0x502);
}


TEST_CASE("X32 bitmap mode takes effect on the next line", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master;
  std::vector<uint8_t> slave;
  X32 device(cartridge, master, slave);
  device.power_on();
  prepare_display_zero(device);
  device.master_bus().write16(0x24000000, 0x0200);
  device.master_bus().write16(0x24000400, 0x03E0);
  show_display_zero(device);
  write_main(device, 0xA15180, 2);
  CHECK(read_main(device, 0xA15180) == 0x8002);

  std::array<uint8_t, 1> md{};
  std::array<uint16_t, 64> cram{};
  std::array<uint32_t, 1> output{};
  device.composite_line(0, md, cram.data(), output);
  CHECK(output[0] == 0xFF000000);
  device.set_blanking(false, true);
  device.set_blanking(false, false);
  device.composite_line(0, md, cram.data(), output);
  CHECK(output[0] == 0xFF00FF00);
}


TEST_CASE("X32 scheduler credits interrupt entry cycles", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  put32(master, 0x108, 0x200);
  put16(master, 0x100, 0x0009);
  put16(master, 0x200, 0x0009);
  X32 device(cartridge, master, slave);
  device.power_on();
  write_main(device, 0xA15100, 0x83);
  device.master().set_sr(0);
  device.master_bus().write16(0x20004000, 2);
  write_main(device, 0xA15102, 1);
  device.advance_mclk(3);
  CHECK(device.master().pc() == 0x200);
  device.advance_mclk(3);
  CHECK(device.master().pc() == 0x200);
  REQUIRE(advance_until(device, [&] { return device.master().pc() == 0x202; }));
}

TEST_CASE("X32 SH-2 bus accesses include per-region wait states", "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master(0x400, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  put16(master, 0x100, 0x2122);
  put16(master, 0x102, 0x2122);
  put16(master, 0x104, 0x2122);
  X32 device(cartridge, master, slave);
  device.power_on();
  write_main(device, 0xA15100, 0x83);
  device.master_bus().write16(0x20004000, 0x8000);
  device.master().set_sr(0);
  device.master().set_r(1, 0x20004100);
  device.master().set_r(2, 2);
  CHECK(device.master().step() == 7);
  device.master().set_r(1, 0x06000000);
  CHECK(device.master().step() == 4);
  device.master().set_r(1, 0x24000000);
  CHECK(device.master().step() == 3);
}

TEST_CASE("X32 DMAC transfers account for source and destination waits",
          "[x32]")
{
  std::vector<uint8_t> cartridge;
  std::vector<uint8_t> master(0x2000, 0);
  std::vector<uint8_t> slave(0x400, 0);
  put32(master, 0, 0x8000);
  put32(master, 4, 0x100);
  for (uint32_t address = 0x100; address <= 0x10A; address += 2)
    put16(master, address, 0x2122);
  X32 device(cartridge, master, slave);
  device.power_on();
  write_main(device, 0xA15100, 0x83);
  device.master().set_sr(0);
  cpu_write32(device, master, 0xFFFFFF88, 1);
  cpu_write32(device, master, 0xFFFFFF80, 0xA15112);
  cpu_write32(device, master, 0xFFFFFF84, 0x06000000);
  cpu_write32(device, master, 0xFFFFFFA0, 0x40);
  cpu_write32(device, master, 0xFFFFFFB0, 1);
  cpu_write32(device, master, 0xFFFFFF8C, 0x4825);
  write_main(device, 0xA15110, 4);
  write_main(device, 0xA15106, 4);
  write_main(device, 0xA15112, 0x1122);
  write_main(device, 0xA15112, 0x3344);
  put16(master, 0x10C, 0x0009);
  put16(master, 0x10E, 0x0009);
  device.master().set_dreq(0, true);
  CHECK(device.master().step() == 5);
  CHECK(device.master().step() == 2);
  CHECK(device.master_bus().read32(0x06000000) == 0x11223344);
}
