/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Save-state result codes shared by the core, the backend adapters and the
 * UI layers. */

#pragma once

#include <string_view>

namespace generator {

enum class StateError {
  NotLoaded,          /* no ROM is loaded */
  NoSlot,             /* the requested slot has no state file */
  InvalidSlot,        /* slot outside the supported range */
  Io,                 /* open/read/write/rename failed */
  Corrupt,            /* framing, bounds or checksum failure */
  WrongRom,           /* the state belongs to a different ROM */
  UnsupportedVersion, /* state format newer (or older) than this build reads */
};

inline constexpr std::string_view state_error_message(StateError error)
{
  switch (error) {
  case StateError::NotLoaded:
    return "No ROM is loaded";
  case StateError::NoSlot:
    return "No save state in this slot";
  case StateError::InvalidSlot:
    return "Invalid save-state slot";
  case StateError::Io:
    return "Could not read or write the save state";
  case StateError::Corrupt:
    return "Save state is damaged or truncated";
  case StateError::WrongRom:
    return "Save state belongs to a different ROM";
  case StateError::UnsupportedVersion:
    return "Save state version is not supported";
  }
  return "Unknown save-state error";
}

}  // namespace generator
