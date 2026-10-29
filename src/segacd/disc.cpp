/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "disc.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <limits>
#include <optional>

namespace generator {

namespace {

constexpr std::uintmax_t kMaxCueBytes = 1024u * 1024u;
constexpr uint64_t kNoIndex = std::numeric_limits<uint64_t>::max();

struct FileEntry {
  std::filesystem::path path;
  uint64_t sectors = 0;
};

struct PendingTrack {
  unsigned int number = 0;
  SegaCdTrackType type = SegaCdTrackType::Mode1;
  size_t file = 0;
  uint64_t index0 = kNoIndex;
  uint64_t index1 = kNoIndex;
  uint64_t pregap = 0;
  bool preemphasis = false;
  size_t line = 0;
};

std::string cue_error(size_t line, std::string_view message)
{
  return "CUE line " + std::to_string(line) + ": " + std::string(message);
}

std::string upper(std::string_view value)
{
  std::string result{value};
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return (char)std::toupper(c); });
  return result;
}

bool tokenize(std::string_view line, std::vector<std::string> &tokens)
{
  size_t at = 0;
  while (at < line.size()) {
    while (at < line.size() &&
           (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) {
      at++;
    }
    if (at == line.size()) {
      break;
    }
    if (line[at] == '"') {
      at++;
      const size_t begin = at;
      while (at < line.size() && line[at] != '"') {
        at++;
      }
      if (at == line.size()) {
        return false;
      }
      tokens.emplace_back(line.substr(begin, at - begin));
      at++;
      if (at < line.size() && line[at] != ' ' && line[at] != '\t' &&
          line[at] != '\r') {
        return false;
      }
    } else {
      const size_t begin = at;
      while (at < line.size() && line[at] != ' ' && line[at] != '\t' &&
             line[at] != '\r') {
        at++;
      }
      tokens.emplace_back(line.substr(begin, at - begin));
    }
  }
  return true;
}

template <typename T> bool parse_unsigned(std::string_view text, T &value)
{
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value, 10);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_msf(std::string_view text, uint64_t &frames)
{
  const size_t first = text.find(':');
  const size_t second =
      text.find(':', first == std::string_view::npos ? 0 : first + 1);
  if (first == std::string_view::npos || second == std::string_view::npos ||
      text.find(':', second + 1) != std::string_view::npos) {
    return false;
  }
  uint64_t minutes;
  uint64_t seconds;
  if (!parse_unsigned(text.substr(0, first), minutes) ||
      !parse_unsigned(text.substr(first + 1, second - first - 1), seconds) ||
      !parse_unsigned(text.substr(second + 1), frames) || seconds >= 60 ||
      frames >= 75 ||
      minutes > (std::numeric_limits<uint64_t>::max() / 75 - seconds) / 60) {
    return false;
  }
  frames += (minutes * 60 + seconds) * 75;
  return true;
}

class Fnv1a64 {
public:
  void update(std::span<const uint8_t> bytes)
  {
    for (const uint8_t byte : bytes) {
      m_hash ^= byte;
      m_hash *= 0x100000001b3ULL;
    }
  }
  void u64(uint64_t value)
  {
    std::array<uint8_t, 8> bytes{};
    for (size_t i = 0; i < bytes.size(); i++) {
      bytes[i] = (uint8_t)(value >> (i * 8));
    }
    update(bytes);
  }
  uint64_t value() const
  {
    return m_hash;
  }

private:
  uint64_t m_hash = 0xcbf29ce484222325ULL;
};

std::expected<void, std::string> hash_file(Fnv1a64 &hash, const FileEntry &file)
{
  std::ifstream in{file.path, std::ios::binary};
  if (!in) {
    return std::unexpected("cannot open disc image: " + file.path.string());
  }
  std::vector<uint8_t> buffer(1024u * 1024u);
  while (in) {
    in.read(reinterpret_cast<char *>(buffer.data()),
            (std::streamsize)buffer.size());
    const auto count = in.gcount();
    if (count > 0) {
      hash.update(std::span<const uint8_t>{buffer.data(), (size_t)count});
    }
  }
  if (!in.eof()) {
    return std::unexpected("cannot read disc image: " + file.path.string());
  }
  return {};
}

std::expected<std::string, std::string>
read_cue(const std::filesystem::path &path)
{
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    return std::unexpected("cannot open CUE sheet: " + path.string());
  }
  if (size > kMaxCueBytes) {
    return std::unexpected("CUE sheet is larger than 1 MiB");
  }
  std::ifstream in{path, std::ios::binary};
  if (!in) {
    return std::unexpected("cannot open CUE sheet: " + path.string());
  }
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  if (text.size() > kMaxCueBytes) {
    return std::unexpected("CUE sheet is larger than 1 MiB");
  }
  return text;
}

std::expected<uint64_t, std::string> calculate_identity(
    const std::vector<SegaCdTrack> &tracks, const std::vector<FileEntry> &files,
    const std::vector<PendingTrack> &pending, uint64_t total_sectors)
{
  Fnv1a64 hash;
  hash.u64(tracks.size());
  hash.u64(total_sectors);
  for (const SegaCdTrack &track : tracks) {
    hash.u64(track.number);
    hash.u64((uint8_t)track.type);
    hash.u64(track.start_lba);
    hash.u64(track.sector_count);
    hash.u64(track.pregap_sectors);
    hash.u64(track.stored_pregap_sectors);
    hash.u64(track.preemphasis ? 1 : 0);
  }

  std::vector<bool> hashed(files.size(), false);
  for (const PendingTrack &track : pending) {
    if (hashed[track.file]) {
      continue;
    }
    hashed[track.file] = true;
    const FileEntry &file = files[track.file];
    hash.u64(file.sectors);
    auto result = hash_file(hash, file);
    if (!result) {
      return std::unexpected(result.error());
    }
  }
  return hash.value();
}

}  // namespace

std::expected<SegaCdDisc, std::string>
SegaCdDisc::load(const std::filesystem::path &filename)
{
  if (upper(filename.extension().string()) == ".CHD") {
    return load_chd(filename);
  }
  std::array<char, 8> signature{};
  std::ifstream in{filename, std::ios::binary};
  in.read(signature.data(), signature.size());
  if (std::string_view{signature.data(), signature.size()} == "MComprHD") {
    return load_chd(filename);
  }
  return load_cue(filename);
}

std::expected<uint64_t, std::string>
SegaCdDisc::chd_identity(const std::filesystem::path &filename,
                         uint64_t stored_frames) const
{
  // Hash the actual file, including metadata, rather than trusting its SHA1
  // header. Renaming an image preserves identity; recompressing may change it.
  return calculate_identity(m_tracks, {{filename, stored_frames}},
                            {PendingTrack{}}, m_sector_count);
}

std::expected<SegaCdDisc, std::string>
SegaCdDisc::load_cue(const std::filesystem::path &filename)
{
  const std::filesystem::path cue_path{std::string(filename)};
  auto text = read_cue(cue_path);
  if (!text) {
    return std::unexpected(text.error());
  }
  if (text->starts_with("\xEF\xBB\xBF")) {
    text->erase(0, 3);
  }

  std::vector<FileEntry> files;
  std::vector<PendingTrack> pending;
  std::optional<size_t> current_file;
  PendingTrack *track = nullptr;
  size_t line_number = 0;
  size_t begin = 0;
  while (begin <= text->size()) {
    const size_t end = text->find('\n', begin);
    const bool last = end == std::string::npos;
    const std::string_view line{text->data() + begin,
                                (last ? text->size() : end) - begin};
    line_number++;
    std::vector<std::string> tokens;
    if (line.find('\0') != std::string_view::npos || !tokenize(line, tokens)) {
      return std::unexpected(cue_error(line_number, "invalid syntax"));
    }
    if (!tokens.empty()) {
      const std::string command = upper(tokens.front());
      if (command == "FILE") {
        if (tokens.size() != 3 || upper(tokens[2]) != "BINARY" ||
            tokens[1].empty()) {
          return std::unexpected(
              cue_error(line_number, "expected FILE \"path\" BINARY"));
        }
        std::filesystem::path path = cue_path.parent_path() / tokens[1];
        path = path.lexically_normal();
        std::error_code ec;
        const auto bytes = std::filesystem::file_size(path, ec);
        if (ec) {
          return std::unexpected(cue_error(
              line_number, "cannot open disc image: " + path.string()));
        }
        if (bytes == 0 || bytes % kSegaCdSectorBytes != 0) {
          return std::unexpected(cue_error(
              line_number, "disc image size is not a whole number of sectors"));
        }
        current_file = files.size();
        files.push_back(FileEntry{std::move(path), bytes / kSegaCdSectorBytes});
        track = nullptr;
      } else if (command == "TRACK") {
        if (!current_file || tokens.size() != 3) {
          return std::unexpected(
              cue_error(line_number, "TRACK requires a FILE and mode"));
        }
        PendingTrack next;
        next.file = *current_file;
        next.line = line_number;
        if (!parse_unsigned(tokens[1], next.number) || next.number == 0 ||
            next.number > 99) {
          return std::unexpected(
              cue_error(line_number, "invalid track number"));
        }
        const std::string mode = upper(tokens[2]);
        if (mode == "MODE1/2352") {
          next.type = SegaCdTrackType::Mode1;
        } else if (mode == "MODE2/2352") {
          next.type = SegaCdTrackType::Mode2;
        } else if (mode == "AUDIO") {
          next.type = SegaCdTrackType::Audio;
        } else {
          return std::unexpected(
              cue_error(line_number, "unsupported track mode: " + tokens[2]));
        }
        pending.push_back(next);
        track = &pending.back();
      } else if (command == "INDEX") {
        unsigned int index;
        uint64_t frames;
        if (!track || tokens.size() != 3 || !parse_unsigned(tokens[1], index) ||
            !parse_msf(tokens[2], frames) || index > 1) {
          return std::unexpected(
              cue_error(line_number, "expected INDEX 00 or 01 with MM:SS:FF"));
        }
        uint64_t &target = index == 0 ? track->index0 : track->index1;
        if (target != kNoIndex) {
          return std::unexpected(cue_error(line_number, "duplicate INDEX"));
        }
        target = frames;
      } else if (command == "PREGAP") {
        if (!track || tokens.size() != 2 || track->pregap != 0 ||
            !parse_msf(tokens[1], track->pregap)) {
          return std::unexpected(cue_error(line_number, "invalid PREGAP"));
        }
      } else if (command == "FLAGS") {
        if (!track || tokens.size() < 2) {
          return std::unexpected(cue_error(line_number, "invalid FLAGS"));
        }
        for (size_t i = 1; i < tokens.size(); i++) {
          track->preemphasis = track->preemphasis || upper(tokens[i]) == "PRE";
        }
      } else if (command == "POSTGAP") {
        return std::unexpected(
            cue_error(line_number, "POSTGAP is not supported yet"));
      } else if (command != "REM" && command != "CATALOG" &&
                 command != "CDTEXTFILE" && command != "TITLE" &&
                 command != "PERFORMER" && command != "SONGWRITER" &&
                 command != "ISRC") {
        return std::unexpected(
            cue_error(line_number, "unsupported command: " + tokens.front()));
      }
    }
    if (last) {
      break;
    }
    begin = end + 1;
  }

  if (pending.empty()) {
    return std::unexpected("CUE sheet contains no tracks");
  }
  for (size_t i = 0; i < pending.size(); i++) {
    const PendingTrack &item = pending[i];
    if (item.number != i + 1 || item.index1 == kNoIndex ||
        (item.index0 != kNoIndex && item.index0 > item.index1) ||
        item.index1 > files[item.file].sectors) {
      return std::unexpected(
          cue_error(item.line, "invalid or missing track INDEX 01"));
    }
  }

  SegaCdDisc disc;
  uint64_t next_lba = 0;
  for (size_t i = 0; i < pending.size(); i++) {
    const PendingTrack &item = pending[i];
    uint64_t end = files[item.file].sectors;
    if (i + 1 < pending.size() && pending[i + 1].file == item.file) {
      const PendingTrack &next = pending[i + 1];
      end = next.index0 == kNoIndex ? next.index1 : next.index0;
    }
    if (end < item.index1 || end > files[item.file].sectors) {
      return std::unexpected(cue_error(item.line, "invalid track length"));
    }

    SegaCdTrack out;
    out.number = item.number;
    out.type = item.type;
    out.file = files[item.file].path;
    out.stored_pregap_sectors =
        item.index0 == kNoIndex ? 0 : item.index1 - item.index0;
    out.pregap_sectors = out.stored_pregap_sectors + item.pregap;
    out.sector_count = end - item.index1;
    out.start_lba = next_lba + out.pregap_sectors;
    out.file_offset = item.index1 * kSegaCdSectorBytes;
    out.stored_pregap_offset =
        item.index0 == kNoIndex ? 0 : item.index0 * kSegaCdSectorBytes;
    out.preemphasis = item.preemphasis;
    next_lba = out.start_lba + out.sector_count;
    disc.m_tracks.push_back(std::move(out));
  }
  disc.m_sector_count = next_lba;
  auto identity =
      calculate_identity(disc.m_tracks, files, pending, disc.m_sector_count);
  if (!identity) {
    return std::unexpected(identity.error());
  }
  disc.m_identity = *identity;
  return disc;
}

const SegaCdTrack *SegaCdDisc::track_at(uint64_t lba) const
{
  for (const SegaCdTrack &track : m_tracks) {
    const uint64_t begin = track.start_lba - track.pregap_sectors;
    if (lba >= begin &&
        lba < track.start_lba + track.sector_count + track.postgap_sectors) {
      return &track;
    }
  }
  return nullptr;
}

std::expected<SegaCdSector, std::string>
SegaCdDisc::read_stored(const SegaCdTrack &track, uint64_t offset,
                        uint64_t sector) const
{
  if (m_chd) {
    return read_chd(track, offset, sector);
  }
  SegaCdSector bytes{};
  std::ifstream in{track.file, std::ios::binary};
  if (!in) {
    return std::unexpected("cannot open disc image: " + track.file.string());
  }
  const uint64_t position = offset + sector * kSegaCdSectorBytes;
  if (position > std::numeric_limits<uint64_t>::max() - kSegaCdSectorBytes) {
    return std::unexpected("disc sector offset overflows");
  }
  in.seekg((std::streamoff)position);
  in.read(reinterpret_cast<char *>(bytes.data()),
          (std::streamsize)bytes.size());
  if (!in) {
    return std::unexpected("cannot read disc sector " + std::to_string(sector) +
                           " from " + track.file.string());
  }
  return bytes;
}

std::expected<SegaCdSector, std::string>
SegaCdDisc::read_sector(uint64_t lba) const
{
  const SegaCdTrack *track = track_at(lba);
  if (!track) {
    return std::unexpected("disc LBA is outside the track layout");
  }
  if (lba >= track->start_lba + track->sector_count) {
    return SegaCdSector{};
  }
  if (lba >= track->start_lba) {
    return read_stored(*track, track->file_offset, lba - track->start_lba);
  }

  const uint64_t pregap_lba = track->start_lba - track->pregap_sectors;
  const uint64_t relative = lba - pregap_lba;
  const uint64_t virtual_sectors =
      track->pregap_sectors - track->stored_pregap_sectors;
  if (relative < virtual_sectors) {
    return SegaCdSector{};
  }
  return read_stored(*track, track->stored_pregap_offset,
                     relative - virtual_sectors);
}

std::expected<SegaCdDataSector, std::string>
SegaCdDisc::read_data_sector(uint64_t lba) const
{
  const SegaCdTrack *track = track_at(lba);
  if (!track || track->audio() || lba < track->start_lba ||
      lba >= track->start_lba + track->sector_count) {
    return std::unexpected("LBA is not a data sector");
  }
  auto raw = read_sector(lba);
  if (!raw) {
    return std::unexpected(raw.error());
  }
  SegaCdDataSector data{};
  std::copy_n(raw->begin() + (std::ptrdiff_t)track->data_offset(), data.size(),
              data.begin());
  return data;
}

}  // namespace generator
