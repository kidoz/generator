/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "state_files.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace generator {

uint64_t state_fnv1a64(std::span<const uint8_t> data)
{
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (const uint8_t byte : data) {
    hash ^= byte;
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

std::filesystem::path state_user_data_dir()
{
  std::filesystem::path base;
#if defined(_WIN32)
  const char *local = std::getenv("LOCALAPPDATA");
  if (local != nullptr && *local != '\0') {
    base = local;
  }
#endif
  if (base.empty()) {
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0') {
      base = xdg;
    }
  }
  if (base.empty()) {
#if defined(_WIN32)
    const char *home = std::getenv("USERPROFILE");
    if (home != nullptr && *home != '\0') {
      base = home;
    }
#else
    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
      base = std::filesystem::path(home) / ".local" / "share";
    }
#endif
  }
  return base;
}

std::filesystem::path state_media_dir(uint64_t media_id)
{
  const std::filesystem::path base = state_user_data_dir();
  if (base.empty()) {
    return {};
  }
  char id[17];
  std::snprintf(id, sizeof(id), "%016llx", (unsigned long long)media_id);
  return base / "generator" / "states" / id;
}

std::filesystem::path state_slot_file(uint64_t media_id, int slot)
{
  const auto dir = state_media_dir(media_id);
  if (dir.empty()) {
    return {};
  }
  return dir / ("slot" + std::to_string(slot) + ".gst");
}

std::filesystem::path state_battery_file(uint64_t media_id,
                                         std::string_view filename)
{
  const auto dir = state_media_dir(media_id);
  if (dir.empty()) {
    return {};
  }
  return dir / filename;
}

std::expected<std::vector<uint8_t>, StateError>
state_read_bounded(const std::filesystem::path &path, std::uintmax_t max_bytes)
{
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    return std::unexpected(StateError::Io);
  }
  if (file_size > max_bytes) {
    return std::unexpected(StateError::Corrupt);
  }

  std::ifstream in{path, std::ios::binary};
  if (!in) {
    return std::unexpected(StateError::Io);
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  if (data.size() > max_bytes) {
    return std::unexpected(StateError::Corrupt);
  }
  return data;
}

std::expected<void, StateError>
state_write_atomic(const std::filesystem::path &path,
                   std::span<const uint8_t> bytes)
{
  std::error_code ec;
  std::filesystem::path temp = path;
  temp += ".tmp";

  {
    std::ofstream out{temp, std::ios::binary | std::ios::trunc};
    if (!out) {
      return std::unexpected(StateError::Io);
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              (std::streamsize)bytes.size());
    out.flush();
    if (!out) {
      out.close();
      std::filesystem::remove(temp, ec);
      return std::unexpected(StateError::Io);
    }
  }

#if !defined(_WIN32)
  const int fd = ::open(temp.c_str(), O_RDONLY);
  if (fd >= 0) {
    (void)::fsync(fd);
    (void)::close(fd);
  }
  std::filesystem::permissions(temp,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ec);
  ec.clear();
#endif

  std::filesystem::rename(temp, path, ec);
#if defined(_WIN32)
  if (ec) {
    std::filesystem::remove(path, ec);
    std::filesystem::rename(temp, path, ec);
  }
#endif
  if (ec) {
    std::filesystem::remove(temp, ec);
    return std::unexpected(StateError::Io);
  }
  return {};
}

std::vector<uint8_t> state_serialize(std::span<const StateChunk> chunks)
{
  std::vector<StateChunk> with_checksum;
  with_checksum.reserve(chunks.size() + 1);
  with_checksum.insert(with_checksum.end(), chunks.begin(), chunks.end());
  with_checksum.push_back(
      StateChunk{fourcc("CHKS"), std::vector<uint8_t>(4, 0)});

  std::vector<uint8_t> blob = StateV3::serialize(with_checksum);
  const uint32_t checksum = state_crc32(
      std::span<const uint8_t>{blob.data(), blob.size() - sizeof(uint32_t)});
  const size_t checksum_at = blob.size() - sizeof(uint32_t);
  for (size_t i = 0; i < sizeof(uint32_t); i++) {
    blob[checksum_at + i] = (uint8_t)((checksum >> (8 * i)) & 0xFF);
  }
  return blob;
}

}  // namespace generator
