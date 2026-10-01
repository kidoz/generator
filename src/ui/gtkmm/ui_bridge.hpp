/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "emulator_core.hpp"

#include <glibmm/bytes.h>

#include <memory>

// Global emulator core instance
extern std::unique_ptr<generator::EmulatorCore> g_emulator_core;

/* Adopt the newest completed field as a private snapshot for the UI
 * thread. The data is copied under the frame lock, so the result stays
 * valid no matter how far the emulation thread runs afterwards; *width
 * and *height are zeroed and an empty ref returned when no field has
 * arrived yet. Rows sit at stride HMAXSIZE * 4 bytes (see
 * screen_geometry.hpp). */
Glib::RefPtr<Glib::Bytes> ui_take_frame(int *width, int *height);
