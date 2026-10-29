/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace generator {

inline constexpr size_t kSegaCdSectorBytes = 2352;
inline constexpr size_t kSegaCdDataBytes = 2048;
inline constexpr uint64_t kSegaCdSectorsPerSecond = 75;

using SegaCdSector = std::array<uint8_t, kSegaCdSectorBytes>;
using SegaCdDataSector = std::array<uint8_t, kSegaCdDataBytes>;

enum class SegaCdTrackType : uint8_t {
  Mode1,
  Mode2,
  Audio,
};

struct SegaCdTrack {
  unsigned int number = 0;
  SegaCdTrackType type = SegaCdTrackType::Mode1;
  std::filesystem::path file;
  uint64_t start_lba = 0;
  uint64_t sector_count = 0;
  uint64_t pregap_sectors = 0;
  uint64_t stored_pregap_sectors = 0;
  uint64_t file_offset = 0;
  uint64_t stored_pregap_offset = 0;
  bool preemphasis = false;
  uint64_t postgap_sectors = 0;

  bool audio() const
  {
    return type == SegaCdTrackType::Audio;
  }
  size_t data_offset() const
  {
    return audio() ? 0 : 16;
  }
};

class SegaCdChd;

class SegaCdDisc {
public:
  static std::expected<SegaCdDisc, std::string>
  load(const std::filesystem::path &filename);
  static std::expected<SegaCdDisc, std::string>
  load_chd(const std::filesystem::path &filename);
  static std::expected<SegaCdDisc, std::string>
  load_cue(const std::filesystem::path &filename);

  const std::vector<SegaCdTrack> &tracks() const
  {
    return m_tracks;
  }
  uint64_t sector_count() const
  {
    return m_sector_count;
  }
  uint64_t identity() const
  {
    return m_identity;
  }

  std::expected<SegaCdSector, std::string> read_sector(uint64_t lba) const;
  std::expected<SegaCdDataSector, std::string>
  read_data_sector(uint64_t lba) const;

private:
  const SegaCdTrack *track_at(uint64_t lba) const;
  std::expected<SegaCdSector, std::string>
  read_stored(const SegaCdTrack &track, uint64_t offset, uint64_t sector) const;
  std::expected<SegaCdSector, std::string>
  read_chd(const SegaCdTrack &track, uint64_t offset, uint64_t sector) const;
  std::expected<uint64_t, std::string>
  chd_identity(const std::filesystem::path &filename,
               uint64_t stored_frames) const;

  std::shared_ptr<SegaCdChd> m_chd;
  std::vector<SegaCdTrack> m_tracks;
  uint64_t m_sector_count = 0;
  uint64_t m_identity = 0;
};

}  // namespace generator
