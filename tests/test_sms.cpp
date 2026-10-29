/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "emulator_core.hpp"
#include "sms/sms_machine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
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

class RecordingAudio final : public IAudioBackend {
public:
  std::vector<int16_t> samples;

  void output_samples(std::span<const uint16_t> left,
                      std::span<const uint16_t> right) override
  {
    const size_t count = std::min(left.size(), right.size());
    for (size_t i = 0; i < count; i++) {
      samples.push_back((int16_t)left[i]);
    }
  }
};

class RecordingVideo final : public IVideoBackend {
public:
  std::array<std::array<uint32_t, 256>, 240> lines{};
  int line_count = 0;
  int fields = 0;

  void render_line(int, std::span<const uint8_t>) override
  {
  }
  void render_line_rgba(int line, std::span<const uint32_t> pixels) override
  {
    if (line < 0 || line >= 240)
      return;
    std::copy_n(pixels.begin(), std::min<size_t>(pixels.size(), 256),
                lines[(size_t)line].begin());
    line_count = std::max(line_count, line + 1);
  }
  void present_field() override
  {
    fields++;
  }
};

class NullLogger final : public ILogger {
public:
  void log(LogLevel, std::string_view) override
  {
  }
};

std::vector<uint8_t> make_boot_rom()
{
  std::vector<uint8_t> rom(0x8000, 0);
  constexpr uint8_t program[] = {
      0x3E, 0x10,                   /* LD A,$10: CRAM address 16 */
      0xD3, 0xBF,                   /* OUT ($BF),A */
      0x3E, 0xC0,                   /* LD A,$C0: select CRAM */
      0xD3, 0xBF, 0x3E, 0x3F,       /* LD A,$3F: white */
      0xD3, 0xBE,                   /* OUT ($BE),A */
      0x3E, 0x04,                   /* LD A,$04: Mode 4 */
      0xD3, 0xBF, 0x3E, 0x80,       /* register 0 */
      0xD3, 0xBF, 0x3E, 0x40,       /* display enable */
      0xD3, 0xBF, 0x3E, 0x81,       /* register 1 */
      0xD3, 0xBF, 0xC3, 0x1B, 0x00, /* JP $001B */
  };
  std::copy(std::begin(program), std::end(program), rom.begin());
  std::copy_n("TMR SEGA", 8, rom.begin() + 0x7FF0);
  return rom;
}

std::vector<uint8_t> make_audio_rom()
{
  auto rom = make_boot_rom();
  constexpr uint8_t audio[] = {
      0x3E, 0x80, 0xD3, 0x7E, /* tone 0, low period */
      0x3E, 0x01, 0xD3, 0x7E, /* tone 0, high period */
      0x3E, 0x90, 0xD3, 0x7E, /* tone 0, full volume */
      0xC3, 0x29, 0x00,       /* JP $0029 */
  };
  std::copy(std::begin(audio), std::end(audio), rom.begin() + 0x1A);
  return rom;
}

std::vector<uint8_t> make_genesis_rom()
{
  std::vector<uint8_t> rom(0x4000, 0);
  rom[0] = 0x00;
  rom[1] = 0xFF;
  rom[4] = 0x02;
  std::copy_n("SEGA", 4, rom.begin() + 0x100);
  rom[0x200] = 0x60;
  rom[0x201] = 0xFE;
  return rom;
}

std::vector<uint8_t> make_banked_rom(size_t banks)
{
  std::vector<uint8_t> rom(banks * 0x4000);
  for (size_t bank = 0; bank < banks; bank++) {
    std::fill_n(rom.begin() + (std::ptrdiff_t)(bank * 0x4000), 0x4000,
                (uint8_t)bank);
  }
  return rom;
}

MasterSystemMachine make_machine()
{
  return MasterSystemMachine(std::make_unique<NullAudio>(),
                             std::make_unique<RecordingVideo>(),
                             std::make_shared<NullLogger>());
}

struct TempData {
  TempData()
  {
    path = std::filesystem::temp_directory_path() /
           ("generator-sms-test-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
    old = std::getenv("XDG_DATA_HOME");
    setenv("XDG_DATA_HOME", path.string().c_str(), 1);
  }
  ~TempData()
  {
    if (old) {
      setenv("XDG_DATA_HOME", old, 1);
    } else {
      unsetenv("XDG_DATA_HOME");
    }
    std::filesystem::remove_all(path);
  }
  std::filesystem::path path;
  const char *old = nullptr;
};

}  // namespace

TEST_CASE("Sega and Codemasters mappers bank ROM and preserve SRAM", "[sms]")
{
  SmsCartridge cart;
  REQUIRE(cart.load(make_banked_rom(8)));
  CHECK(cart.read(0x0000) == 0);
  CHECK(cart.read(0x4000) == 1);
  CHECK(cart.read(0x8000) == 2);

  cart.write(0xFFFE, 3);
  cart.write(0xFFFF, 4);
  CHECK(cart.read(0x4000) == 3);
  CHECK(cart.read(0x8000) == 4);
  cart.write(0xFFFD, 5);
  CHECK(cart.read(0x0000) == 0);
  CHECK(cart.read(0x0400) == 5);

  cart.write(0xFFFC, 0x08);
  cart.write(0x9000, 0x5A);
  CHECK(cart.read(0x9000) == 0x5A);
  CHECK(cart.sram_dirty());
  cart.write(0xFFFC, 0x0C);
  CHECK(cart.read(0x9000) == 0);
  cart.write(0xFFFC, 0x08);
  CHECK(cart.read(0x9000) == 0x5A);

  cart.set_mapper(SmsMapper::Codemasters);
  cart.write(0x0000, 3);
  cart.write(0x4000, 2);
  cart.write(0x8000, 4);
  CHECK(cart.read(0x0000) == 3);
  CHECK(cart.read(0x4000) == 2);
  CHECK(cart.read(0x8000) == 4);
}

TEST_CASE("an SMS ROM boots through EmulatorCore and emits RGBA video", "[sms]")
{
  const char *path = "test_sms_boot.sms";
  const auto rom = make_boot_rom();
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out.write((const char *)rom.data(), (std::streamsize)rom.size());
  }

  auto video = std::make_unique<RecordingVideo>();
  RecordingVideo *output = video.get();
  EmulatorCore core(std::make_unique<NullAudio>(), std::move(video),
                    std::make_shared<NullLogger>());
  REQUIRE(core.load_media(path));
  CHECK(core.media_info().system == SystemKind::MasterSystem);
  CHECK(core.media_info().kind == MediaKind::Cartridge);
  core.run_frame();

  REQUIRE(output->line_count == 192);
  REQUIRE(output->fields == 1);
  CHECK(output->lines[0][0] == 0xFFFFFFFFU);
  CHECK(output->lines[191][255] == 0xFFFFFFFFU);
  std::remove(path);
}

TEST_CASE("SMS pads use active-low two-button ports", "[sms]")
{
  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_boot_rom()));
  machine.set_input(0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0);
  machine.set_input(1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0);
  CHECK((machine.io_debug_read(0xDC) & 0x11) == 0);
  CHECK((machine.io_debug_read(0xDC) & 0x80) == 0);
  CHECK((machine.io_debug_read(0xDD) & 0x08) == 0);
}

TEST_CASE("SMS state restore is byte-identical and continuous", "[sms]")
{
  const char *path = "test_sms_state.gst";
  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_boot_rom()));
  machine.run_frame();
  const auto original = machine.capture_state();
  REQUIRE(original.has_value());
  REQUIRE(machine.save_state(path).has_value());

  machine.run_frame();
  machine.run_frame();
  REQUIRE(machine.load_state(path).has_value());
  CHECK(machine.capture_state().value() == *original);
  machine.run_frame();
  std::remove(path);
}

TEST_CASE("SMS battery and numbered slots use per-ROM storage", "[sms]")
{
  TempData temp;
  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_boot_rom()));
  machine.cartridge_debug().write(0xFFFC, 0x08);
  machine.cartridge_debug().write(0x9000, 0x5A);
  REQUIRE(machine.save_sram().has_value());
  machine.run_frame();
  REQUIRE(machine.save_state_slot(0).has_value());
  CHECK(machine.state_slot_date(0) != 0);

  auto restored = make_machine();
  REQUIRE(restored.load_rom_mem(make_boot_rom()));
  restored.cartridge_debug().write(0xFFFC, 0x08);
  CHECK(restored.cartridge_debug().read(0x9000) == 0x5A);
  REQUIRE(restored.load_state_slot(0).has_value());
}

TEST_CASE("SMS PSG reaches the machine audio mixer", "[sms]")
{
  auto audio = std::make_unique<RecordingAudio>();
  RecordingAudio *output = audio.get();
  MasterSystemMachine machine(std::move(audio),
                              std::make_unique<RecordingVideo>(),
                              std::make_shared<NullLogger>());
  REQUIRE(machine.load_rom_mem(make_audio_rom()));
  machine.run_frame();
  REQUIRE_FALSE(output->samples.empty());
  CHECK(std::any_of(output->samples.begin(), output->samples.end(),
                    [](int16_t sample) { return sample != 0; }));
}

TEST_CASE("core machine replacement works in both directions", "[sms]")
{
  EmulatorCore core(std::make_unique<NullAudio>(),
                    std::make_unique<RecordingVideo>(),
                    std::make_shared<NullLogger>());
  REQUIRE(core.load_rom_mem(make_boot_rom()));
  CHECK(core.media_info().system == SystemKind::MasterSystem);
  core.run_frame();

  REQUIRE(core.load_rom_mem(make_genesis_rom()));
  CHECK(core.media_info().system == SystemKind::MegaDrive);
  core.run_frame();

  REQUIRE(core.load_rom_mem(make_boot_rom()));
  CHECK(core.media_info().system == SystemKind::MasterSystem);
  core.run_frame();
}
