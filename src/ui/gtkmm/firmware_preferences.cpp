/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "firmware_preferences.hpp"
#include "gtkopts.h"

#include <filesystem>
#include <fstream>
#include <string_view>

namespace generator::gtkui {

FirmwareSettings FirmwareSettings::load()
{
  FirmwareSettings settings;
  if (const char *path = gtkopts_getvalue("sega_cd_bios"))
    settings.sega_cd_bios = path;
  if (const char *region = gtkopts_getvalue("sega_cd_region")) {
    if (std::string_view(region) == "north-america")
      settings.sega_cd_region = SegaCdRegion::NorthAmerica;
    else if (std::string_view(region) == "europe")
      settings.sega_cd_region = SegaCdRegion::Europe;
  }
  return settings;
}

bool FirmwareSettings::save() const
{
  // The option file stores one unquoted value per line, up to 1023 bytes.
  if (sega_cd_bios.find_first_of("\r\n") != std::string::npos ||
      sega_cd_bios.find('\0') != std::string::npos ||
      sega_cd_bios.size() > 1000 ||
      (!sega_cd_bios.empty() && sega_cd_bios.back() == ' '))
    return false;
  const char *region = "japan";
  switch (sega_cd_region) {
  case SegaCdRegion::Japan:
    break;
  case SegaCdRegion::NorthAmerica:
    region = "north-america";
    break;
  case SegaCdRegion::Europe:
    region = "europe";
    break;
  default:
    return false;
  }
  return gtkopts_setvalue("sega_cd_bios", sega_cd_bios.c_str()) == 0 &&
         gtkopts_setvalue("sega_cd_region", region) == 0;
}

std::expected<void, std::string> FirmwareSettings::validate() const
{
  if (sega_cd_bios.empty())
    return {};
  std::error_code ec;
  const auto size = std::filesystem::file_size(sega_cd_bios, ec);
  if (ec)
    return std::unexpected("Cannot open the selected Sega CD BIOS file.");
  if (size != 128u * 1024u)
    return std::unexpected("The Sega CD BIOS must be exactly 128 KiB.");
  std::ifstream in{sega_cd_bios, std::ios::binary};
  if (!in || in.peek() == std::ifstream::traits_type::eof())
    return std::unexpected("Cannot read the selected Sega CD BIOS file.");
  return {};
}

MediaLoadOptions FirmwareSettings::media_options() const
{
  MediaLoadOptions options;
  if (!sega_cd_bios.empty())
    options.sega_cd_firmware = SegaCdFirmware{sega_cd_bios, sega_cd_region};
  return options;
}

}  // namespace generator::gtkui
