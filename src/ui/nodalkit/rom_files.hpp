/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ROM filename filters shared by the open dialog and drag-and-drop */

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace generator::nkui {

/* gen_loadimage() reads raw images and Richard Bannister's interleaved SMD
 * format; there is no archive support, so no *.zip here. */
inline const std::vector<std::string> &rom_filters()
{
  static const std::vector<std::string> filters = {"*.bin", "*.smd", "*.gen",
                                                   "*.md", "*.rom"};
  return filters;
}

/* Whether a chosen or dropped file matches the filters above. */
inline bool looks_like_rom(const std::filesystem::path &path)
{
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext == ".bin" || ext == ".smd" || ext == ".gen" || ext == ".md" ||
         ext == ".rom";
}

}  // namespace generator::nkui
