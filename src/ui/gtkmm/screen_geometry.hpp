/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Geometry of the gtkmm backend's screen buffers */

#pragma once

/* This backend renders into fixed-stride buffers big enough for the largest
 * scaled field plus borders, and hands GDK a sub-rectangle of one. The
 * stride therefore has to agree between the code that fills the buffer
 * (ui_bridge.cpp) and the code that uploads it (emulator_view.cpp); both
 * used to carry their own copy of these macros, so changing one silently
 * corrupted the other. */

#define MAX_SCALE_FACTOR 4
#define HBORDER_MAX 32
#define VBORDER_MAX 32
#define HMAXSIZE ((320 * MAX_SCALE_FACTOR) + 2 * HBORDER_MAX)
#define VMAXSIZE ((240 * MAX_SCALE_FACTOR) + 2 * VBORDER_MAX)
