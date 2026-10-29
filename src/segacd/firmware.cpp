/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "firmware.hpp"

#include <fstream>

namespace generator {

std::expected<SegaCdFirmwareImage, std::string>
SegaCdFirmwareImage::load(const SegaCdFirmware &firmware)
{
  std::error_code ec;
  const auto size = std::filesystem::file_size(firmware.path, ec);
  if (ec) {
    return std::unexpected("cannot open Sega CD BIOS: " +
                           firmware.path.string());
  }
  if (size != kSize) {
    return std::unexpected("Sega CD BIOS must be exactly 128 KiB: " +
                           firmware.path.string());
  }

  std::ifstream in{firmware.path, std::ios::binary};
  if (!in) {
    return std::unexpected("cannot open Sega CD BIOS: " +
                           firmware.path.string());
  }
  SegaCdFirmwareImage image;
  image.m_region = firmware.region;
  image.m_bytes.resize(kSize);
  in.read(reinterpret_cast<char *>(image.m_bytes.data()),
          (std::streamsize)image.m_bytes.size());
  if (!in) {
    return std::unexpected("cannot read Sega CD BIOS: " +
                           firmware.path.string());
  }

  uint64_t hash = 0xcbf29ce484222325ULL;
  for (const uint8_t byte : image.m_bytes) {
    hash ^= byte;
    hash *= 0x100000001b3ULL;
  }
  image.m_identity = hash;
  return image;
}

}  // namespace generator
