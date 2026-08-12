/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fm_lfo.hpp - YM2612/OPN LFO step, extracted for testing.
 *
 * The per-sample LFO advance (AM/PM depth computation) was open-coded and
 * duplicated across four OPN-family update paths in fm.c (YM2203/YM2608/
 * YM2610/YM2612), which made the PM-clock behavior untestable. This header
 * exposes a single pure helper that fm.c's update loops call and that unit
 * tests (tests/test_fm_lfo.cpp) drive directly.
 *
 * The waveform table is passed as a parameter (not read as a global) so the
 * helper is pure and a test can supply a synthetic waveform.
 */

#ifndef FM_LFO_HPP
#define FM_LFO_HPP

#include "fm.h" /* INT32, UINT32 typedefs */

#ifdef __cplusplus
extern "C" {
#endif

/* LFO fixed-point scale (canonical copy for standalone use; fm.c defines the
 * same macros itself, so each is guarded to avoid redefinition). */
#ifndef LFO_SH
#define LFO_SH 23 /* 9.23 fixed point (LFO calculations) */
#endif
#ifndef LFO_RATE
#define LFO_RATE 0x10000
#endif

/* Advance the OPN LFO by one sample step and write the AM/PM depth for the
 * new phase. Caller MUST guard on lfo_incr != 0 (matching the original
 * inline sites, which are inside `if (LFOIncr) { ... }`).
 *
 *   lfo_cnt   - current LFO phase accumulator
 *   lfo_incr  - per-sample phase increment (must be != 0)
 *   lfo_wave  - the OPN_LFO_wave[] table (LFO_ENT entries)
 *   invert_am - 1 for the YM2612 inverted-AM direction, 0 otherwise
 *   out_amd   - receives the AM depth (tremolo)
 *   out_pmd   - receives the PM depth (vibrato), centered around 0
 *
 * Returns the updated lfo_cnt.
 */
UINT32 opn_lfo_step(UINT32 lfo_cnt, UINT32 lfo_incr, const INT32 *lfo_wave,
                    int invert_am, UINT32 *out_amd, INT32 *out_pmd);

#ifdef __cplusplus
}
#endif

#endif /* FM_LFO_HPP */
