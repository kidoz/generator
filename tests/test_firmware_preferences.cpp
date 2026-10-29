/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "firmware_preferences.hpp"
#include "emulator_core.hpp"
#include "gtkopts.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace generator;
using generator::gtkui::FirmwareSettings;

namespace {

class FirmwareFixture {
public:
  FirmwareFixture() : previous(gtkopts_conf)
  {
    gtkopts_conf = nullptr;
    root = std::filesystem::current_path() /
           ("firmware-options-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
  }
  ~FirmwareFixture()
  {
    clear();
    gtkopts_conf = previous;
    std::filesystem::remove_all(root);
  }
  void clear()
  {
    while (gtkopts_conf) {
      auto *next = gtkopts_conf->next;
      std::free(gtkopts_conf->key);
      std::free(gtkopts_conf->value);
      std::free(gtkopts_conf);
      gtkopts_conf = next;
    }
  }
  void binary(const std::filesystem::path &path, size_t size)
  {
    std::vector<char> bytes(size);
    std::ofstream out{path, std::ios::binary};
    out.write(bytes.data(), (std::streamsize)bytes.size());
    REQUIRE(out.good());
  }
  std::filesystem::path root;

private:
  t_conf *previous;
};

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

}  // namespace

TEST_CASE("firmware paths and regions persist and supply Sega CD load options",
          "[firmware][preferences][sega_cd]")
{
  FirmwareFixture fixture;
  CHECK(FirmwareSettings::load() == FirmwareSettings{});
  CHECK_FALSE(FirmwareSettings::load().media_options().sega_cd_firmware);
  const auto bios = fixture.root / "Sega CD BIOS (Europe) [!].bin";
  fixture.binary(bios, 128 * 1024);
  const FirmwareSettings saved{bios.string(), SegaCdRegion::Europe};
  REQUIRE(saved.validate());
  REQUIRE(saved.save());
  const auto config = fixture.root / "generator.conf";
  REQUIRE(gtkopts_save(config.c_str()) == 0);
  fixture.clear();
  REQUIRE(gtkopts_load(config.c_str()) == 0);
  CHECK(FirmwareSettings::load() == saved);
  auto options = FirmwareSettings::load().media_options();
  REQUIRE(options.sega_cd_firmware);
  CHECK(options.sega_cd_firmware->path == bios);
  CHECK(options.sega_cd_firmware->region == SegaCdRegion::Europe);

  // Use the options read from disk to load a disc through the real public API.
  fixture.binary(fixture.root / "game.bin", 2352);
  {
    std::ofstream cue{fixture.root / "game.cue"};
    cue << "FILE \"game.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n";
  }
  EmulatorCore core{std::make_unique<NullAudio>(),
                    std::make_unique<NullVideo>(),
                    std::make_shared<NullLogger>()};
  REQUIRE(core.load_media((fixture.root / "game.cue").string(), options));
  CHECK(core.media_info().system == SystemKind::SegaCd);
  CHECK(core.video_mode() == 1);
  REQUIRE(FirmwareSettings{}.save());
  CHECK_FALSE(FirmwareSettings::load().media_options().sega_cd_firmware);
}

TEST_CASE(
    "firmware validation reports missing and incorrectly sized BIOS files",
    "[firmware][preferences]")
{
  FirmwareFixture fixture;
  CHECK(FirmwareSettings{}.validate());
  FirmwareSettings settings{(fixture.root / "bios.bin").string()};
  auto missing = settings.validate();
  REQUIRE_FALSE(missing);
  CHECK(missing.error().find("Cannot open") != std::string::npos);
  fixture.binary(settings.sega_cd_bios, 1024);
  auto small = settings.validate();
  REQUIRE_FALSE(small);
  CHECK(small.error().find("128 KiB") != std::string::npos);
  fixture.binary(settings.sega_cd_bios, 256 * 1024);
  CHECK_FALSE(settings.validate());
  fixture.binary(settings.sega_cd_bios, 128 * 1024);
  CHECK(settings.validate());
}

TEST_CASE("firmware settings handle invalid regions and unsafe config values",
          "[firmware][preferences]")
{
  FirmwareFixture fixture;
  for (const auto region : {SegaCdRegion::Japan, SegaCdRegion::NorthAmerica,
                            SegaCdRegion::Europe}) {
    FirmwareSettings settings{"", region};
    REQUIRE(settings.save());
    CHECK(FirmwareSettings::load().sega_cd_region == region);
  }
  REQUIRE(gtkopts_setvalue("sega_cd_region", "unknown") == 0);
  CHECK(FirmwareSettings::load().sega_cd_region == SegaCdRegion::Japan);
  for (const auto &path : {std::string("bios.bin\nsega_cd_region = europe"),
                           std::string(1024, 'a'), std::string("bios.bin ")}) {
    FirmwareSettings settings{path};
    CHECK_FALSE(settings.save());
    CHECK(FirmwareSettings::load().sega_cd_bios.empty());
  }
}
