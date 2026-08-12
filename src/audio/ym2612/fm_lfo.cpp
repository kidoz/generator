/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fm_lfo.cpp - YM2612/OPN LFO step implementation.
 *
 * Extracted verbatim (in arithmetic) from the four duplicated per-sample LFO
 * blocks in fm.c (YM2203/YM2608/YM2610/YM2612 update paths). Behavior-neutral:
 * PM currently uses the SAME waveform index as AM, which is the known-incorrect
 * GEMS behavior tracked as TECH_DEBT TD-021 (hardware steps PM at 1/4 the AM
 * rate). The fix lands as a separate commit that flips a pinned test.
 *
 * The (UINT32) cast on the waveform read only makes explicit the INT32->UINT32
 * conversion the original inline code did implicitly on assignment to lfo_amd;
 * OPN_LFO_wave holds non-negative values in [0, LFO_RATE], so the bits are
 * identical (verified at fm.c:1328-1331, OPNInitTable).
 */

#include "fm_lfo.hpp"

UINT32 opn_lfo_step(UINT32 lfo_cnt, UINT32 lfo_incr, const INT32 *lfo_wave,
                    int invert_am, UINT32 *out_amd, INT32 *out_pmd)
{
  lfo_cnt += lfo_incr;
  const UINT32 idx = lfo_cnt >> LFO_SH;
  const UINT32 wave_u = (UINT32)lfo_wave[idx];

  *out_amd = invert_am ? (LFO_RATE - wave_u) : wave_u;
  /* TD-021 (open): PM should use a 4x-quantized index. Currently shares the
   * AM index, which is the bug the follow-up commit fixes. */
  *out_pmd = (INT32)lfo_wave[idx] - (LFO_RATE / 2);

  return lfo_cnt;
}
