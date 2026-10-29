/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Media and hardware selection shared by every UI backend. */

#pragma once

#include <filesystem>
#include <optional>

namespace generator {

enum class SystemKind {
  MegaDrive,
  MasterSystem,
  SegaCd,
  X32,
  SegaCdX32,
};

enum class MediaKind {
  Cartridge,
  Disc,
};

enum class SegaCdRegion {
  Japan,
  NorthAmerica,
  Europe,
};

struct SegaCdFirmware {
  std::filesystem::path path;
  SegaCdRegion region = SegaCdRegion::Japan;
};

struct MediaLoadOptions {
  std::optional<SystemKind> system;
  bool attach_32x = false;
  std::optional<SegaCdFirmware> sega_cd_firmware;
};

struct MediaInfo {
  SystemKind system = SystemKind::MegaDrive;
  MediaKind kind = MediaKind::Cartridge;
};

const char *system_name(SystemKind system);

}  // namespace generator
