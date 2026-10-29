/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "emulator_core.hpp"
#include "media_detect.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

EmulatorCore make_core()
{
  return EmulatorCore(std::make_unique<NullAudio>(),
                      std::make_unique<NullVideo>(),
                      std::make_shared<NullLogger>());
}

}  // namespace

TEST_CASE("media detection distinguishes each requested hardware family",
          "[media]")
{
  CHECK(detect_media("game.sms", {})->system == SystemKind::MasterSystem);
  CHECK(detect_media("game.32x", {})->system == SystemKind::X32);
  CHECK(detect_media("game.cue", {})->system == SystemKind::SegaCd);
  CHECK(detect_media("game.CHD", {})->system == SystemKind::SegaCd);
  CHECK(detect_media("game.chd", {}, {.attach_32x = true})->system ==
        SystemKind::SegaCdX32);
  CHECK(detect_media("game.cue", {}, {.attach_32x = true})->system ==
        SystemKind::SegaCdX32);

  auto rom = make_genesis_rom();
  CHECK(detect_media("game.bin", rom)->system == SystemKind::MegaDrive);

  std::copy_n("SEGA 32X", 8, rom.begin() + 0x100);
  CHECK(detect_media("game.bin", rom)->system == SystemKind::X32);

  std::fill_n(rom.begin() + 0x100, 8, 0);
  std::copy_n("SEGA", 4, rom.begin() + 0x100);
  std::copy_n("TMR SEGA", 8, rom.begin() + 0x1FF0);
  CHECK(detect_media("game.bin", rom)->system == SystemKind::MasterSystem);

  const MediaLoadOptions override{.system = SystemKind::MasterSystem};
  CHECK(detect_media("game.gen", rom, override)->system ==
        SystemKind::MasterSystem);
}

TEST_CASE("unsupported systems do not replace the running cartridge", "[media]")
{
  auto core = make_core();
  auto rom = make_genesis_rom();
  REQUIRE(core.load_rom_mem(rom));
  REQUIRE(core.rom_loaded());
  CHECK(core.media_info().system == SystemKind::MegaDrive);

  std::copy_n("SEGA 32X", 8, rom.begin() + 0x100);
  const auto result = core.load_rom_mem(rom);
  REQUIRE_FALSE(result);
  CHECK(result.error().find("32X") != std::string::npos);
  CHECK(core.rom_loaded());
  CHECK(core.media_info().system == SystemKind::MegaDrive);

  const auto sms = core.load_media("missing.sms");
  REQUIRE_FALSE(sms);
  CHECK(sms.error().find("cannot open") != std::string::npos);

  const auto cd32x = core.load_media("missing.cue", {.attach_32x = true});
  REQUIRE_FALSE(cd32x);
  CHECK(cd32x.error().find("Sega CD 32X") != std::string::npos);
}

TEST_CASE("load_media loads a Genesis file through the public core", "[media]")
{
  const char *path = "test_media_rom.gen";
  const auto rom = make_genesis_rom();
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out.write((const char *)rom.data(), (std::streamsize)rom.size());
  }

  auto core = make_core();
  const auto result = core.load_media(path);
  REQUIRE(result);
  CHECK(core.rom_loaded());
  CHECK(core.media_info().system == SystemKind::MegaDrive);
  CHECK(core.media_info().kind == MediaKind::Cartridge);
  CHECK(std::string(core.rom_name()) == path);

  core.unload_rom();
  CHECK_FALSE(core.rom_loaded());
  std::remove(path);
}
