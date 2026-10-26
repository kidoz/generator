/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Media and hardware selection shared by every UI backend. */

#pragma once

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

struct MediaLoadOptions {
  std::optional<SystemKind> system;
  bool attach_32x = false;
};

struct MediaInfo {
  SystemKind system = SystemKind::MegaDrive;
  MediaKind kind = MediaKind::Cartridge;
};

const char *system_name(SystemKind system);

}  // namespace generator
