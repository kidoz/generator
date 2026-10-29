/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "media.hpp"

#include <expected>
#include <string>

namespace generator::gtkui {

struct FirmwareSettings {
  std::string sega_cd_bios;
  SegaCdRegion sega_cd_region = SegaCdRegion::Japan;

  static FirmwareSettings load();
  bool save() const;
  std::expected<void, std::string> validate() const;
  MediaLoadOptions media_options() const;
  bool operator==(const FirmwareSettings &) const = default;
};

}  // namespace generator::gtkui
