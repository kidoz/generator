/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Action names shared by the menus, shortcuts and the app controller */

#pragma once

namespace generator::nkui::commands {

/* Every command surface - the widget menu bar, the native app menu and the
 * keyboard shortcuts - routes through one handler keyed on these names, so
 * behaviour cannot drift between them. */

inline constexpr const char *kOpenRom = "file.open-rom";
inline constexpr const char *kClearRecent = "file.clear-recent";
inline constexpr const char *kQuit = "file.quit";

/* Recent-ROM entries carry their list index: "file.open-recent.3". */
inline constexpr const char *kOpenRecentPrefix = "file.open-recent.";

inline constexpr const char *kPause = "emulation.pause";
inline constexpr const char *kReset = "emulation.reset";
inline constexpr const char *kSoftReset = "emulation.soft-reset";
inline constexpr const char *kSaveState = "emulation.save-state";
inline constexpr const char *kLoadState = "emulation.load-state";

/* Slot entries carry their slot number: "emulation.state-slot.4". */
inline constexpr const char *kStateSlotPrefix = "emulation.state-slot.";

inline constexpr const char *kFullscreen = "view.fullscreen";
inline constexpr const char *kScaleNearest = "view.scale-nearest";
inline constexpr const char *kScaleSmooth = "view.scale-smooth";

inline constexpr const char *kPreferences = "app.preferences";
inline constexpr const char *kAbout = "help.about";

}  // namespace generator::nkui::commands
