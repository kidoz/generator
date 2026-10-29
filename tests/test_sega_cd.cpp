/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "emulator_core.hpp"
#include "segacd/disc.hpp"
#include "segacd/firmware.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace generator;

namespace {

class DiscFixture {
public:
  DiscFixture()
  {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::vector<uint8_t> data(6 * kSegaCdSectorBytes);
    std::vector<uint8_t> audio(2 * kSegaCdSectorBytes);
    for (size_t sector = 0; sector < 6; sector++) {
      std::fill_n(data.begin() + (std::ptrdiff_t)(sector * kSegaCdSectorBytes),
                  kSegaCdSectorBytes, (uint8_t)(sector + 1));
    }
    for (size_t sector = 0; sector < 2; sector++) {
      data[sector * kSegaCdSectorBytes + 16] = (uint8_t)(0xA0 + sector);
      std::fill_n(audio.begin() + (std::ptrdiff_t)(sector * kSegaCdSectorBytes),
                  kSegaCdSectorBytes, (uint8_t)(0x40 + sector));
    }
    write_binary(root / "data.bin", data);
    write_binary(root / "audio.bin", audio);
    write_cue(root / "game.cue");
  }
  ~DiscFixture()
  {
    std::filesystem::remove_all(root);
  }

  static void write_binary(const std::filesystem::path &path,
                           const std::vector<uint8_t> &bytes)
  {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char *>(bytes.data()),
              (std::streamsize)bytes.size());
    REQUIRE(out.good());
  }

  static void write_text(const std::filesystem::path &path,
                         const std::string &text)
  {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    REQUIRE(out.good());
    out << text;
    REQUIRE(out.good());
  }

  static void write_cue(const std::filesystem::path &path)
  {
    write_text(path, "FILE \"data.bin\" BINARY\n"
                     "  TRACK 01 MODE1/2352\n"
                     "    INDEX 01 00:00:00\n"
                     "  TRACK 02 AUDIO\n"
                     "    FLAGS PRE\n"
                     "    PREGAP 00:00:02\n"
                     "    INDEX 00 00:00:02\n"
                     "    INDEX 01 00:00:04\n"
                     "FILE \"audio.bin\" BINARY\n"
                     "  TRACK 03 AUDIO\n"
                     "    PREGAP 00:00:01\n"
                     "    INDEX 01 00:00:00\n");
  }

  std::filesystem::path root{"test_sega_cd_media_files"};
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

EmulatorCore make_core()
{
  return EmulatorCore(std::make_unique<NullAudio>(),
                      std::make_unique<NullVideo>(),
                      std::make_shared<NullLogger>());
}

}  // namespace

TEST_CASE("Sega CD CUE layout handles files, indices, and pregaps",
          "[sega_cd][media]")
{
  DiscFixture fixture;
  auto disc = SegaCdDisc::load_cue(fixture.root / "game.cue");
  INFO((disc.has_value() ? "loaded" : disc.error()));
  REQUIRE(disc);
  REQUIRE(disc->tracks().size() == 3);
  CHECK(disc->sector_count() == 11);

  const auto &tracks = disc->tracks();
  CHECK(tracks[0].type == SegaCdTrackType::Mode1);
  CHECK(tracks[0].start_lba == 0);
  CHECK(tracks[0].sector_count == 2);
  CHECK(tracks[1].start_lba == 6);
  CHECK(tracks[1].pregap_sectors == 4);
  CHECK(tracks[1].stored_pregap_sectors == 2);
  CHECK(tracks[1].sector_count == 2);
  CHECK(tracks[1].preemphasis);
  CHECK(tracks[2].start_lba == 9);
  CHECK(tracks[2].pregap_sectors == 1);

  auto virtual_gap = disc->read_sector(2);
  REQUIRE(virtual_gap);
  CHECK(std::all_of(virtual_gap->begin(), virtual_gap->end(),
                    [](uint8_t byte) { return byte == 0; }));
  auto stored_gap = disc->read_sector(4);
  REQUIRE(stored_gap);
  CHECK((*stored_gap)[0] == 3);
  auto audio = disc->read_sector(9);
  REQUIRE(audio);
  CHECK((*audio)[0] == 0x40);

  auto data = disc->read_data_sector(0);
  REQUIRE(data);
  CHECK((*data)[0] == 0xA0);
  CHECK_FALSE(disc->read_data_sector(6));
  CHECK_FALSE(disc->read_sector(11));
}

TEST_CASE("Sega CD identity covers content and layout rather than paths",
          "[sega_cd][media]")
{
  DiscFixture fixture;
  auto original = SegaCdDisc::load_cue(fixture.root / "game.cue");
  REQUIRE(original);

  const auto moved = fixture.root / "moved";
  std::filesystem::create_directories(moved);
  std::filesystem::copy_file(fixture.root / "data.bin", moved / "other.bin");
  std::filesystem::copy_file(fixture.root / "audio.bin", moved / "sound.bin");
  DiscFixture::write_text(moved / "different.cue",
                          "REM layout and paths do not define identity\n"
                          "FILE \"other.bin\" BINARY\n"
                          "TRACK 01 MODE1/2352\n"
                          "INDEX 01 00:00:00\n"
                          "TRACK 02 AUDIO\n"
                          "FLAGS PRE\n"
                          "PREGAP 00:00:02\n"
                          "INDEX 00 00:00:02\n"
                          "INDEX 01 00:00:04\n"
                          "FILE \"sound.bin\" BINARY\n"
                          "TRACK 03 AUDIO\n"
                          "PREGAP 00:00:01\n"
                          "INDEX 01 00:00:00\n");
  auto copy = SegaCdDisc::load_cue(moved / "different.cue");
  REQUIRE(copy);
  CHECK(copy->identity() == original->identity());

  auto changed = std::ifstream{moved / "sound.bin", std::ios::binary};
  REQUIRE(changed.good());
  changed.close();
  std::fstream modify{moved / "sound.bin",
                      std::ios::binary | std::ios::in | std::ios::out};
  REQUIRE(modify.good());
  modify.seekp(0);
  modify.put(0x7F);
  modify.close();
  auto mutated = SegaCdDisc::load_cue(moved / "different.cue");
  REQUIRE(mutated);
  CHECK(mutated->identity() != original->identity());
}

TEST_CASE("Sega CD CUE parser rejects malformed and unsupported layouts",
          "[sega_cd][media]")
{
  DiscFixture fixture;
  const std::string missing_index =
      "FILE \"data.bin\" BINARY\nTRACK 01 MODE1/2352\n";
  DiscFixture::write_text(fixture.root / "missing.cue", missing_index);
  auto missing = SegaCdDisc::load_cue(fixture.root / "missing.cue");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().find("INDEX 01") != std::string::npos);

  const std::string bad_time =
      "FILE \"data.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:75\n";
  DiscFixture::write_text(fixture.root / "time.cue", bad_time);
  CHECK_FALSE(SegaCdDisc::load_cue(fixture.root / "time.cue"));

  const std::string unsupported =
      "FILE \"data.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n";
  DiscFixture::write_text(fixture.root / "mode.cue", unsupported);
  auto mode = SegaCdDisc::load_cue(fixture.root / "mode.cue");
  REQUIRE_FALSE(mode);
  CHECK(mode.error().find("unsupported track mode") != std::string::npos);

  const std::string overlapping = "FILE \"data.bin\" BINARY\n"
                                  "TRACK 01 MODE1/2352\nINDEX 01 00:00:04\n"
                                  "TRACK 02 AUDIO\nINDEX 01 00:00:02\n";
  DiscFixture::write_text(fixture.root / "overlap.cue", overlapping);
  CHECK_FALSE(SegaCdDisc::load_cue(fixture.root / "overlap.cue"));
}

TEST_CASE("Sega CD firmware requires a 128 KiB image and hashes its content",
          "[sega_cd][firmware]")
{
  DiscFixture fixture;
  std::vector<uint8_t> bytes(SegaCdFirmwareImage::kSize);
  for (size_t i = 0; i < bytes.size(); i++) {
    bytes[i] = (uint8_t)(i * 17);
  }
  DiscFixture::write_binary(fixture.root / "bios.bin", bytes);
  auto firmware = SegaCdFirmwareImage::load(
      {fixture.root / "bios.bin", SegaCdRegion::NorthAmerica});
  REQUIRE(firmware);
  CHECK(firmware->region() == SegaCdRegion::NorthAmerica);
  CHECK(firmware->bytes() == bytes);

  bytes[0]++;
  DiscFixture::write_binary(fixture.root / "other.bin", bytes);
  auto other = SegaCdFirmwareImage::load(
      {fixture.root / "other.bin", SegaCdRegion::NorthAmerica});
  REQUIRE(other);
  CHECK(other->identity() != firmware->identity());

  DiscFixture::write_binary(fixture.root / "short.bin", {1, 2, 3});
  auto short_bios = SegaCdFirmwareImage::load(
      {fixture.root / "short.bin", SegaCdRegion::Japan});
  REQUIRE_FALSE(short_bios);
  CHECK(short_bios.error().find("128 KiB") != std::string::npos);
}

TEST_CASE(
    "EmulatorCore validates Sega CD media before reporting emulation support",
    "[sega_cd][media]")
{
  DiscFixture fixture;
  auto core = make_core();
  const auto cue = (fixture.root / "game.cue").string();

  auto missing_bios = core.load_media(cue);
  REQUIRE_FALSE(missing_bios);
  CHECK(missing_bios.error().find("BIOS is required") != std::string::npos);
  CHECK_FALSE(core.rom_loaded());

  DiscFixture::write_binary(fixture.root / "short.bin", {1, 2, 3});
  MediaLoadOptions options;
  options.sega_cd_firmware =
      SegaCdFirmware{fixture.root / "short.bin", SegaCdRegion::Japan};
  auto bad_bios = core.load_media(cue, options);
  REQUIRE_FALSE(bad_bios);
  CHECK(bad_bios.error().find("128 KiB") != std::string::npos);

  std::vector<uint8_t> bytes(SegaCdFirmwareImage::kSize, 0x5A);
  DiscFixture::write_binary(fixture.root / "bios.bin", bytes);
  options.sega_cd_firmware->path = fixture.root / "bios.bin";
  auto loaded = core.load_media(cue, options);
  REQUIRE(loaded);
  CHECK(core.rom_loaded());
  CHECK(core.media_info().system == SystemKind::SegaCd);
  CHECK(core.media_info().kind == MediaKind::Disc);

  auto missing_cue =
      core.load_media((fixture.root / "missing.cue").string(), options);
  REQUIRE_FALSE(missing_cue);
  CHECK(missing_cue.error().find("cannot open CUE") != std::string::npos);
}
