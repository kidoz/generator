/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Geometry of the gtkmm backend's screen buffers */

#pragma once

/* Native fields use these legacy fixed-stride banks. The stride must agree
 * between ui_bridge.cpp and the source-frame copy in emulator_view.cpp.
 * Presentation filters allocate separate packed buffers, so xBRZ 5x/6x
 * output is not limited by the banks' historical 4x capacity. */

#define MAX_SCALE_FACTOR 4
#define HBORDER_MAX 32
#define VBORDER_MAX 32
#define HMAXSIZE ((320 * MAX_SCALE_FACTOR) + 2 * HBORDER_MAX)
#define VMAXSIZE ((240 * MAX_SCALE_FACTOR) + 2 * VBORDER_MAX)
