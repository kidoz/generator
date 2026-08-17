/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared state between the NodalKit UI backend and the emulator core */

#pragma once

#include "frame_buffer.hpp"

#include <memory>
#include <string>

namespace generator {

class EmulatorCore;

namespace nkui {

/* The emulator core, constructed in ui_loop() and torn down in ui_final().
 * Null before the UI starts and after it shuts down; every user is expected
 * to check. */
extern std::unique_ptr<EmulatorCore> g_emulator_core;

/* The completed fields the emulation thread hands to the UI thread. */
extern FrameBuffer g_frames;

/* ROM named on the command line, empty when none was given. Parsed in
 * ui_init() because the verbosity flag beside it has to take effect before
 * the core initialises and starts logging. */
extern std::string g_startup_rom;

}  // namespace nkui

}  // namespace generator
