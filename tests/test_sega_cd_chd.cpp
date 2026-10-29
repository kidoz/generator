/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "emulator_core.hpp"
#include "segacd/cd_audio.hpp"
#include "segacd/disc.hpp"
#include "segacd/firmware.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace generator;

namespace {

// An uncompressed v5 CHD needs only a header, a big-endian hunk map, linked
// metadata records, and raw hunks. No chdman binary or game data is required.
class ChdFixture {
public:
  static constexpr size_t kFrameBytes = 2448;
  static constexpr size_t kHunkBytes = 8 * kFrameBytes;

  ChdFixture()
  {
    std::filesystem::create_directories(root);
  }
  ~ChdFixture()
  {
    std::filesystem::remove_all(root);
  }

  static void be(std::vector<uint8_t> &out, size_t at, uint64_t value,
                 size_t count)
  {
    for (size_t i = 0; i < count; i++) {
      out[at + i] = (uint8_t)(value >> ((count - 1 - i) * 8));
    }
  }

  static std::vector<uint8_t> image(const std::vector<std::string> &metadata,
                                    bool extended = true)
  {
    std::vector<uint8_t> out(3 * kHunkBytes);
    std::copy_n("MComprHD", 8, out.begin());
    be(out, 8, 124, 4);
    be(out, 12, 5, 4);
    be(out, 32, 16 * kFrameBytes, 8);
    be(out, 40, 124, 8);
    be(out, 48, metadata.empty() ? 0 : 132, 8);
    be(out, 56, kHunkBytes, 4);
    be(out, 60, kFrameBytes, 4);
    be(out, 124, 1, 4);
    be(out, 128, 2, 4);
    size_t at = 132;
    for (size_t i = 0; i < metadata.size(); i++) {
      const auto &text = metadata[i];
      const size_t next = at + 16 + text.size() + 1;
      REQUIRE(next < kHunkBytes);
      std::copy_n(extended ? "CHT2" : "CHTR", 4, out.begin() + at);
      be(out, at + 4, text.size() + 1, 4);
      be(out, at + 8, i + 1 < metadata.size() ? next : 0, 8);
      std::copy(text.begin(), text.end(), out.begin() + at + 16);
      at = next;
    }
    for (size_t frame = 0; frame < 16; frame++) {
      const size_t start = kHunkBytes + frame * kFrameBytes;
      std::fill_n(out.begin() + start, kSegaCdSectorBytes,
                  (uint8_t)(0x10 + frame));
      std::fill_n(out.begin() + start + kSegaCdSectorBytes, 96, 0xEE);
      if (frame >= 4 && frame < 9) {
        // Signed, asymmetric channels catch both byte and channel swaps.
        out[start] = 0x12;
        out[start + 1] = (uint8_t)(0x30 + frame);
        out[start + 2] = 0xFE;
        out[start + 3] = 0xDC;
      }
    }
    return out;
  }

  static std::vector<std::string> metadata()
  {
    return {
        "TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:3 PREGAP:0 "
        "PGTYPE:MODE1 PGSUB:NONE POSTGAP:0",
        "TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:5 PREGAP:2 "
        "PGTYPE:VAUDIO PGSUB:NONE POSTGAP:1",
        "TRACK:3 TYPE:MODE2_RAW SUBTYPE:RW_RAW FRAMES:2 PREGAP:2 "
        "PGTYPE:MODE2 PGSUB:NONE POSTGAP:0",
    };
  }

  void write(const std::filesystem::path &path,
             const std::vector<uint8_t> &bytes)
  {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char *>(bytes.data()),
              (std::streamsize)bytes.size());
    REQUIRE(out.good());
  }

  std::filesystem::path root{"test_sega_cd_chd_files"};
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

TEST_CASE("CHD maps padded tracks and stored and virtual gaps",
          "[sega_cd][chd]")
{
  ChdFixture fixture;
  fixture.write(fixture.root / "game.CHD",
                ChdFixture::image(ChdFixture::metadata()));
  auto disc = SegaCdDisc::load(fixture.root / "game.CHD");
  INFO((disc ? "loaded" : disc.error()));
  REQUIRE(disc);
  REQUIRE(disc->tracks().size() == 3);
  CHECK(disc->sector_count() == 13);
  CHECK(disc->tracks()[0].sector_count == 3);
  CHECK(disc->tracks()[1].start_lba == 5);
  CHECK(disc->tracks()[1].sector_count == 3);
  CHECK(disc->tracks()[1].stored_pregap_sectors == 2);
  CHECK(disc->tracks()[2].start_lba == 11);
  CHECK(disc->tracks()[2].stored_pregap_sectors == 0);

  auto data = disc->read_data_sector(2);
  REQUIRE(data);
  CHECK((*data)[0] == 0x12);
  auto gap = disc->read_sector(3);
  REQUIRE(gap);
  CHECK((*gap)[0] == 0x34);
  CHECK((*gap)[1] == 0x12);
  auto audio = disc->read_sector(5);
  REQUIRE(audio);
  CHECK((*audio)[0] == 0x36);
  CHECK((*audio)[1] == 0x12);
  CHECK((*audio)[2] == 0xDC);
  CHECK((*audio)[3] == 0xFE);
  auto across_hunk = disc->read_sector(7);
  REQUIRE(across_hunk);
  CHECK((*across_hunk)[0] == 0x38);
  for (uint64_t lba : {8, 9, 10}) {
    auto zero = disc->read_sector(lba);
    REQUIRE(zero);
    CHECK(*zero == SegaCdSector{});
    CHECK_FALSE(disc->read_data_sector(lba));
  }
  auto mode2 = disc->read_data_sector(11);
  REQUIRE(mode2);
  CHECK((*mode2)[0] == 0x1C);
  CHECK_FALSE(disc->read_data_sector(5));
  CHECK_FALSE(disc->read_sector(13));

  // Revisit an earlier hunk, including through a copied disc handle.
  auto copy = *disc;
  CHECK(copy.read_data_sector(0)->front() == 0x10);
  CHECK(disc->read_sector(7)->front() == 0x38);
  SegaCdAudio player(*disc, 44100);
  REQUIRE(player.play(2));
  player.advance_mclk(1);
  CHECK(player.sample_left() == 0x1236);
  CHECK(player.sample_right() == (int16_t)0xFEDC);
}

TEST_CASE("CHD supports legacy CHTR metadata and path independent identity",
          "[sega_cd][chd]")
{
  ChdFixture fixture;
  auto image = ChdFixture::image(
      {"TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:16"}, false);
  fixture.write(fixture.root / "first.chd", image);
  fixture.write(fixture.root / "second.cue", image);
  auto first = SegaCdDisc::load(fixture.root / "first.chd");
  auto second = SegaCdDisc::load(fixture.root / "second.cue");
  REQUIRE(first);
  REQUIRE(second);
  CHECK(first->identity() == second->identity());
  image[ChdFixture::kHunkBytes + 16] ^= 1;
  fixture.write(fixture.root / "changed.chd", image);
  auto changed = SegaCdDisc::load(fixture.root / "changed.chd");
  REQUIRE(changed);
  CHECK(changed->identity() != first->identity());
}

TEST_CASE("CHD rejects invalid images and unsupported layouts",
          "[sega_cd][chd]")
{
  ChdFixture fixture;
  auto metadata = ChdFixture::metadata();
  auto bytes = ChdFixture::image(metadata);
  SECTION("truncated header")
  {
    bytes.resize(20);
  }
  SECTION("non CD geometry")
  {
    ChdFixture::be(bytes, 60, 512, 4);
  }
  SECTION("oversized hunk")
  {
    ChdFixture::be(bytes, 56, 0xFFFFFFF0, 4);
  }
  SECTION("missing metadata")
  {
    bytes = ChdFixture::image({});
  }
  SECTION("non sequential tracks")
  {
    metadata[0][6] = '2';
    bytes = ChdFixture::image(metadata);
  }
  SECTION("invalid numeric field")
  {
    metadata[0].replace(metadata[0].find("FRAMES:3"), 8, "FRAMES:-1");
    bytes = ChdFixture::image(metadata);
  }
  SECTION("unsupported cooked track")
  {
    metadata[0].replace(metadata[0].find("MODE1_RAW"), 9, "MODE1");
    bytes = ChdFixture::image(metadata);
  }
  SECTION("stored pregap exceeds frames")
  {
    metadata[1].replace(metadata[1].find("PREGAP:2"), 8, "PREGAP:6");
    bytes = ChdFixture::image(metadata);
  }
  SECTION("incomplete track layout")
  {
    metadata.pop_back();
    bytes = ChdFixture::image(metadata);
  }
  SECTION("track layout exceeds image")
  {
    metadata[2].replace(metadata[2].find("FRAMES:2"), 8, "FRAMES:200");
    bytes = ChdFixture::image(metadata);
  }
  SECTION("missing parent")
  {
    bytes[104] = 1;
  }
  SECTION("unreadable first hunk")
  {
    ChdFixture::be(bytes, 124, 1000, 4);
  }
  fixture.write(fixture.root / "bad.chd", bytes);
  auto loaded = SegaCdDisc::load(fixture.root / "bad.chd");
  REQUIRE_FALSE(loaded);
  CHECK(loaded.error().find("CHD") != std::string::npos);
}

TEST_CASE("CHD read failures invalidate the cached hunk", "[sega_cd][chd]")
{
  ChdFixture fixture;
  auto bytes = ChdFixture::image(ChdFixture::metadata());
  ChdFixture::be(bytes, 128, 1000, 4);
  fixture.write(fixture.root / "later-error.chd", bytes);
  auto disc = SegaCdDisc::load(fixture.root / "later-error.chd");
  REQUIRE(disc);
  CHECK_FALSE(disc->read_sector(7));
  auto earlier = disc->read_data_sector(0);
  REQUIRE(earlier);
  CHECK(earlier->front() == 0x10);
  CHECK_FALSE(disc->read_sector(7));
}

TEST_CASE("public core loads CHD with a BIOS and preserves media on failure",
          "[sega_cd][chd][media]")
{
  ChdFixture fixture;
  fixture.write(fixture.root / "game.chd",
                ChdFixture::image(ChdFixture::metadata()));
  auto core =
      EmulatorCore(std::make_unique<NullAudio>(), std::make_unique<NullVideo>(),
                   std::make_shared<NullLogger>());
  auto missing_bios = core.load_media((fixture.root / "game.chd").string());
  REQUIRE_FALSE(missing_bios);
  CHECK(missing_bios.error().find("BIOS") != std::string::npos);
  std::vector<uint8_t> bios(SegaCdFirmwareImage::kSize);
  fixture.write(fixture.root / "bios.bin", bios);
  MediaLoadOptions options;
  options.sega_cd_firmware =
      SegaCdFirmware{fixture.root / "bios.bin", SegaCdRegion::NorthAmerica};
  REQUIRE(core.load_media((fixture.root / "game.chd").string(), options));
  REQUIRE(core.rom_loaded());
  CHECK(core.media_info().system == SystemKind::SegaCd);
  CHECK(core.media_info().kind == MediaKind::Disc);
  fixture.write(fixture.root / "bad.chd", {0, 1, 2});
  CHECK_FALSE(core.load_media((fixture.root / "bad.chd").string(), options));
  CHECK(core.rom_loaded());
  CHECK(core.media_info().system == SystemKind::SegaCd);
}
