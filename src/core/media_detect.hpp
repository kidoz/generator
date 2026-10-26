/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Media type detection and bounded file input, independent of emulation. */

#pragma once

#include "media.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace generator {

std::expected<MediaInfo, std::string>
detect_media(std::string_view filename, std::span<const uint8_t> data,
             const MediaLoadOptions &options = {});

std::expected<std::vector<uint8_t>, std::string>
read_media_file(std::string_view filename);

}  // namespace generator
