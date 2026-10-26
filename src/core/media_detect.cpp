/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "media_detect.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace generator {

namespace {

constexpr std::uintmax_t kMaxCartridgeBytes = 32u * 1024u * 1024u;

std::string extension_of(std::string_view filename)
{
  std::string extension =
      std::filesystem::path{std::string(filename)}.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return extension;
}

bool has_signature(std::span<const uint8_t> data, size_t offset,
                   std::string_view signature)
{
  return offset + signature.size() <= data.size() &&
         std::memcmp(data.data() + offset, signature.data(),
                     signature.size()) == 0;
}

bool has_sms_header(std::span<const uint8_t> data)
{
  constexpr size_t offsets[] = {0x1FF0, 0x3FF0, 0x7FF0};
  for (const size_t offset : offsets) {
    if (has_signature(data, offset, "TMR SEGA") ||
        has_signature(data, offset + 512, "TMR SEGA")) {
      return true;
    }
  }
  return false;
}

MediaInfo cartridge(SystemKind system)
{
  return MediaInfo{system, MediaKind::Cartridge};
}

MediaInfo disc(bool attach_32x)
{
  return MediaInfo{attach_32x ? SystemKind::SegaCdX32 : SystemKind::SegaCd,
                   MediaKind::Disc};
}

}  // namespace

const char *system_name(SystemKind system)
{
  switch (system) {
  case SystemKind::MegaDrive:
    return "Mega Drive / Genesis";
  case SystemKind::MasterSystem:
    return "Master System";
  case SystemKind::SegaCd:
    return "Sega CD";
  case SystemKind::X32:
    return "32X";
  case SystemKind::SegaCdX32:
    return "Sega CD 32X";
  }
  return "unknown";
}

std::expected<MediaInfo, std::string>
detect_media(std::string_view filename, std::span<const uint8_t> data,
             const MediaLoadOptions &options)
{
  if (options.system) {
    const SystemKind system = *options.system;
    if (system == SystemKind::SegaCd || system == SystemKind::SegaCdX32) {
      return disc(system == SystemKind::SegaCdX32);
    }
    return cartridge(system);
  }

  const std::string extension = extension_of(filename);
  if (extension == ".cue" || extension == ".chd") {
    return disc(options.attach_32x);
  }
  if (extension == ".sms") {
    return cartridge(SystemKind::MasterSystem);
  }
  if (extension == ".32x") {
    return cartridge(SystemKind::X32);
  }

  if (has_signature(data, 0x100, "SEGA 32X") ||
      has_signature(data, 0x300, "SEGA 32X")) {
    return cartridge(SystemKind::X32);
  }
  if (has_sms_header(data)) {
    return cartridge(SystemKind::MasterSystem);
  }
  return cartridge(SystemKind::MegaDrive);
}

std::expected<std::vector<uint8_t>, std::string>
read_media_file(std::string_view filename)
{
  const std::filesystem::path path{std::string(filename)};
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    return std::unexpected("cannot open media file: " + std::string(filename));
  }
  if (file_size > kMaxCartridgeBytes) {
    return std::unexpected("cartridge image is too large");
  }

  std::ifstream in{path, std::ios::binary};
  if (!in) {
    return std::unexpected("cannot open media file: " + std::string(filename));
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  if (data.size() > kMaxCartridgeBytes) {
    return std::unexpected("cartridge image is too large");
  }
  return data;
}

}  // namespace generator
