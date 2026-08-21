/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exact field periods for UI pacing.
 *
 * The video standards do not run at nominal 60/50: a field is a fixed
 * number of master clocks against the crystal, so NTSC refreshes at
 * 262 x 3420 / 53.693175 MHz = 59.9227 Hz and PAL at 313 x 3420 /
 * 53.203424 MHz = 49.7014 Hz. Pacing on nominal 60/50 runs the
 * emulation 0.13%/0.6% fast, which the audio queue then has to shed —
 * and an unpaced loop feeds the sound device everything as fast as the
 * CPU can emulate, so the game plays fast. */

#pragma once

#include <chrono>
#include <cstdint>

namespace generator {

constexpr std::chrono::nanoseconds kFieldNtsc{16695253};
constexpr std::chrono::nanoseconds kFieldPal{20117873};

inline std::chrono::nanoseconds field_duration(bool pal)
{
  return pal ? kFieldPal : kFieldNtsc;
}

}  // namespace generator
