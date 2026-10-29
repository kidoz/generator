/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "media.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace generator {

class SegaCdFirmwareImage {
public:
  static constexpr size_t kSize = 128u * 1024u;

  static std::expected<SegaCdFirmwareImage, std::string>
  load(const SegaCdFirmware &firmware);

  SegaCdRegion region() const
  {
    return m_region;
  }
  uint64_t identity() const
  {
    return m_identity;
  }
  const std::vector<uint8_t> &bytes() const
  {
    return m_bytes;
  }

private:
  SegaCdRegion m_region = SegaCdRegion::Japan;
  uint64_t m_identity = 0;
  std::vector<uint8_t> m_bytes;
};

}  // namespace generator
