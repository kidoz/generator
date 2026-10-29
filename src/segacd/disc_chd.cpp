/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "disc.hpp"

#if __has_include(<libchdr/chd.h>)
#include <libchdr/chd.h>
#else
// The system pkg-config package may expose include/libchdr directly.
#include <chd.h>
#endif

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>

namespace generator {

namespace {

// CHD CD frames contain 2352 sector bytes and 96 subchannel bytes. Tracks
// are padded to four frames in the file; that padding is absent on the disc.
constexpr uint64_t kChdFrameBytes = kSegaCdSectorBytes + 96;
constexpr uint64_t kMaxCdFrames = 100 * 60 * 75 + 99 * 3;
constexpr uint32_t kMaxHunkBytes = 1024 * 1024;

std::string chd_error_message(std::string_view operation, chd_error error)
{
  return "CHD " + std::string(operation) + ": " + chd_error_string(error);
}

struct ChdTrackMetadata {
  uint64_t number = 0;
  uint64_t frames = 0;
  uint64_t pregap = 0;
  uint64_t postgap = 0;
  std::string type;
  std::string subtype;
  std::string pgtype;
  std::string pgsub;
};

bool field(std::istringstream &in, std::string_view key, std::string &value)
{
  std::string token;
  if (!(in >> token) || !token.starts_with(key)) {
    return false;
  }
  value = token.substr(key.size());
  return !value.empty();
}

bool field(std::istringstream &in, std::string_view key, uint64_t &value)
{
  std::string text;
  if (!field(in, key, text)) {
    return false;
  }
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool parse_metadata(std::string_view text, bool extended,
                    ChdTrackMetadata &track)
{
  std::istringstream in{std::string(text)};
  if (!field(in, "TRACK:", track.number) || !field(in, "TYPE:", track.type) ||
      !field(in, "SUBTYPE:", track.subtype) ||
      !field(in, "FRAMES:", track.frames)) {
    return false;
  }
  if (extended && (!field(in, "PREGAP:", track.pregap) ||
                   !field(in, "PGTYPE:", track.pgtype) ||
                   !field(in, "PGSUB:", track.pgsub) ||
                   !field(in, "POSTGAP:", track.postgap))) {
    return false;
  }
  std::string extra;
  return !(in >> extra);
}

bool valid_subtype(std::string_view subtype)
{
  return subtype == "NONE" || subtype == "RW" || subtype == "RW_RAW";
}

std::expected<void, std::string>
validate_header(const std::filesystem::path &filename)
{
  // libchdr 0.3.0's header-only helper leaves internal file-size state
  // uninitialized. Read only the allocation bounds here, then let chd_open
  // validate the complete header using its initialized file state.
  std::array<uint8_t, 124> bytes{};
  std::ifstream in{filename, std::ios::binary};
  in.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
  if (!in || std::string_view{reinterpret_cast<const char *>(bytes.data()),
                              8} != "MComprHD") {
    return std::unexpected("CHD cannot read header: missing or invalid image");
  }
  const auto be = [&bytes](size_t at, size_t count) {
    uint64_t value = 0;
    for (size_t i = 0; i < count; i++) {
      value = (value << 8) | bytes[at + i];
    }
    return value;
  };
  const uint64_t version = be(12, 4);
  if (version < 3 || version > 5) {
    return std::unexpected("CHD unsupported disc version");
  }
  const uint64_t logical = be(version == 5 ? 32 : 28, 8);
  const uint64_t hunk = be(version == 5 ? 56 : version == 4 ? 44 : 76, 4);
  if (hunk == 0 || hunk > kMaxHunkBytes || hunk % kChdFrameBytes != 0 ||
      logical == 0 || logical % kChdFrameBytes != 0 ||
      logical / kChdFrameBytes > kMaxCdFrames ||
      (version == 5 && be(60, 4) != kChdFrameBytes) ||
      (version < 5 && be(24, 4) != (logical + hunk - 1) / hunk)) {
    return std::unexpected("CHD has invalid or unsupported CD geometry");
  }
  return {};
}

}  // namespace

class SegaCdChd {
public:
  ~SegaCdChd()
  {
    if (file) {
      chd_close(file);
    }
  }

  chd_file *file = nullptr;
  std::vector<uint8_t> hunk;
  uint32_t cached_hunk = std::numeric_limits<uint32_t>::max();
  // A copied disc shares the handle and cache. Serialize libchdr operations.
  std::mutex mutex;
};

std::expected<SegaCdDisc, std::string>
SegaCdDisc::load_chd(const std::filesystem::path &filename)
{
  auto validated = validate_header(filename);
  if (!validated) {
    return std::unexpected(validated.error());
  }

  SegaCdDisc disc;
  disc.m_chd = std::make_shared<SegaCdChd>();
  auto error =
      chd_open(filename.c_str(), CHD_OPEN_READ, nullptr, &disc.m_chd->file);
  if (error != CHDERR_NONE) {
    return std::unexpected(chd_error_message("cannot open disc", error));
  }
  const auto &header = *chd_get_header(disc.m_chd->file);
  if (header.unitbytes != kChdFrameBytes) {
    return std::unexpected("CHD does not contain CD frames");
  }
  disc.m_chd->hunk.resize(header.hunkbytes);

  uint64_t stored_frames = 0;
  uint64_t next_lba = 0;
  for (uint32_t index = 0; index <= 99; index++) {
    std::array<char, 512> metadata{};
    uint32_t length = 0;
    bool extended = true;
    error = chd_get_metadata(disc.m_chd->file, CDROM_TRACK_METADATA2_TAG, index,
                             metadata.data(), metadata.size(), &length, nullptr,
                             nullptr);
    if (error == CHDERR_METADATA_NOT_FOUND) {
      extended = false;
      error = chd_get_metadata(disc.m_chd->file, CDROM_TRACK_METADATA_TAG,
                               index, metadata.data(), metadata.size(), &length,
                               nullptr, nullptr);
    }
    if (error == CHDERR_METADATA_NOT_FOUND) {
      break;
    }
    if (error != CHDERR_NONE) {
      return std::unexpected(chd_error_message("cannot read track", error));
    }
    if (index == 99 || length == 0 || length >= metadata.size()) {
      return std::unexpected("CHD has invalid track metadata size or count");
    }
    const auto end =
        std::find(metadata.begin(), metadata.begin() + length, '\0');
    ChdTrackMetadata item;
    if (!parse_metadata({metadata.data(), (size_t)(end - metadata.begin())},
                        extended, item) ||
        item.number != index + 1 || item.frames == 0 ||
        item.frames > kMaxCdFrames || item.pregap > kMaxCdFrames ||
        item.postgap > kMaxCdFrames || !valid_subtype(item.subtype) ||
        (extended && !valid_subtype(item.pgsub))) {
      return std::unexpected("CHD has invalid CD track metadata");
    }

    SegaCdTrack track;
    track.number = (unsigned int)item.number;
    track.file = filename;
    if (item.type == "MODE1_RAW") {
      track.type = SegaCdTrackType::Mode1;
    } else if (item.type == "MODE2_RAW") {
      track.type = SegaCdTrackType::Mode2;
    } else if (item.type == "AUDIO") {
      track.type = SegaCdTrackType::Audio;
    } else {
      return std::unexpected("CHD unsupported track type: " + item.type +
                             "; use MODE1_RAW, MODE2_RAW, or AUDIO");
    }
    track.pregap_sectors = item.pregap;
    // The V prefix means the pregap is stored, not virtual (MAME CHT2).
    if (item.pregap != 0 && item.pgtype.starts_with('V')) {
      if (item.pgtype.substr(1) != item.type || item.pregap >= item.frames) {
        return std::unexpected("CHD has unsupported stored pregap layout");
      }
      track.stored_pregap_sectors = item.pregap;
    }
    track.sector_count = item.frames - track.stored_pregap_sectors;
    track.start_lba = next_lba + track.pregap_sectors;
    track.postgap_sectors = item.postgap;
    // Use the same byte-offset contract as BIN tracks, with CHD padding
    // removed only from the emulated layout, never from the stored offsets.
    track.stored_pregap_offset = stored_frames * kSegaCdSectorBytes;
    track.file_offset =
        (stored_frames + track.stored_pregap_sectors) * kSegaCdSectorBytes;
    next_lba = track.start_lba + track.sector_count + track.postgap_sectors;
    stored_frames += (item.frames + 3) / 4 * 4;
    if (next_lba > kMaxCdFrames ||
        stored_frames > header.logicalbytes / kChdFrameBytes) {
      return std::unexpected("CHD track layout exceeds the CD image");
    }
    disc.m_tracks.push_back(std::move(track));
  }
  if (disc.m_tracks.empty()) {
    return std::unexpected("CHD has no supported CD track metadata");
  }
  if (stored_frames != header.logicalbytes / kChdFrameBytes) {
    return std::unexpected("CHD track layout does not match the CD image");
  }
  disc.m_sector_count = next_lba;
  auto identity = disc.chd_identity(filename, stored_frames);
  if (!identity) {
    return std::unexpected(identity.error());
  }
  disc.m_identity = *identity;
  // Fail before replacing the running machine if its initial sector is bad.
  auto first_sector = disc.read_sector(disc.m_tracks.front().start_lba);
  if (!first_sector) {
    return std::unexpected(first_sector.error());
  }
  return disc;
}

std::expected<SegaCdSector, std::string>
SegaCdDisc::read_chd(const SegaCdTrack &track, uint64_t offset,
                     uint64_t sector) const
{
  const uint64_t frame = offset / kSegaCdSectorBytes + sector;
  const uint64_t frames_per_hunk = m_chd->hunk.size() / kChdFrameBytes;
  const uint32_t hunk = (uint32_t)(frame / frames_per_hunk);
  const size_t position = (size_t)(frame % frames_per_hunk * kChdFrameBytes);
  const std::lock_guard lock{m_chd->mutex};
  if (m_chd->cached_hunk != hunk) {
    // A failed read may have partially overwritten the cache.
    m_chd->cached_hunk = std::numeric_limits<uint32_t>::max();
    const auto error = chd_read(m_chd->file, hunk, m_chd->hunk.data());
    if (error != CHDERR_NONE) {
      return std::unexpected(chd_error_message("cannot decode sector", error));
    }
    m_chd->cached_hunk = hunk;
  }
  SegaCdSector bytes{};
  std::copy_n(m_chd->hunk.begin() + (std::ptrdiff_t)position, bytes.size(),
              bytes.begin());
  if (track.audio()) {
    // CHD CD-DA is big-endian; the BIN reader and mixer use little-endian PCM.
    for (size_t i = 0; i < bytes.size(); i += 2) {
      std::swap(bytes[i], bytes[i + 1]);
    }
  }
  return bytes;
}

}  // namespace generator
