/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fm_eg.cpp - YM2612 envelope + phase-increment core implementation.
 *
 * Extracted verbatim from fm.c (calc_eg at fm.c:896, CALC_FCSLOT at fm.c:1079)
 * so the envelope math is unit-testable. The only change from the original
 * INLINE code is that calc_eg() receives the current LFO AM depth as a
 * parameter instead of reading fm.c's static lfo_amd global; every caller in
 * fm.c passes that global, so production output is unchanged.
 *
 * NOTE: FM_SEG_SUPPORT is defined to 1 by fm.h (YM2612 path). The OPM
 * override in fm.c (FM_SEG_SUPPORT=0) is local to fm.c and does not apply
 * here, which is correct: this core is the OPN2/YM2612 envelope math only.
 */

#include "fm_eg.hpp"

unsigned int calc_eg(FM_SLOT *SLOT, UINT32 lfo_amd)
{
  unsigned int out;

  switch (SLOT->state) {
  case EG_ATT: /* attack phase */
  {
    INT32 step = SLOT->volume;

    SLOT->volume -= SLOT->delta_ar;
    step = (step >> ENV_SH) -
           (((UINT32)SLOT->volume) >>
            ENV_SH); /* number of levels passed since last time */
    if (step > 0) {
      INT32 tmp_volume =
          SLOT->volume + (step << ENV_SH); /* adjust by number of levels */
      do {
        tmp_volume =
            tmp_volume - (1 << ENV_SH) - ((tmp_volume >> 4) & ~ENV_MASK);
        if (tmp_volume <= MIN_ATT_INDEX)
          break;
        step--;
      } while (step);
      SLOT->volume = tmp_volume;
    }

    if (SLOT->volume <= MIN_ATT_INDEX) {
      if (SLOT->volume < 0)
        SLOT->volume = 0;
      SLOT->state = EG_DEC;
    }
  } break;

  case EG_DEC: /* decay phase */
    if ((SLOT->volume += SLOT->delta_dr) >= SLOT->sl) {
      SLOT->volume = SLOT->sl;
      SLOT->state = EG_SUS;
    }
    break;

  case EG_SUS: /* sustain phase */
#if FM_SEG_SUPPORT
    /* SSG-EG: check for envelope completion during sustain */
    if (SLOT->SEG & SSG_ENABLE) {
      if ((SLOT->volume += SLOT->delta_sr) >= MAX_ATT_INDEX) {
        /* Envelope reached minimum output (max attenuation) */
        if (SLOT->SEG & SSG_HOLD) {
          /* Hold mode */
          if (SLOT->SEG & SSG_ALTERNATE) {
            /* Hold with alternate: hold at opposite polarity */
            SLOT->ssg_inv ^= 1;
          }
          /* Hold at current level */
          SLOT->volume = MAX_ATT_INDEX;
          SLOT->state = EG_OFF;
        } else {
          /* Loop mode */
          if (SLOT->SEG & SSG_ALTERNATE) {
            /* Alternate: invert and continue from max */
            SLOT->ssg_inv ^= 1;
          }
          /* Restart from max volume (min attenuation) */
          SLOT->volume = MIN_ATT_INDEX;
          SLOT->state = EG_ATT;
        }
      }
    } else
#endif
    {
      /* Normal sustain behavior */
      if ((SLOT->volume += SLOT->delta_sr) > MAX_ATT_INDEX) {
        SLOT->volume = MAX_ATT_INDEX;
        SLOT->state = EG_OFF;
      }
    }
    break;

  case EG_REL: /* release phase */
#if FM_SEG_SUPPORT
    /* SSG-EG during release: handle specially */
    if (SLOT->SEG & SSG_ENABLE) {
      /* When SSG-EG is active, release may need special handling.
       * If envelope was inverted, we need to consider the inversion
       * when transitioning to release. The release phase itself
       * runs normally but starts from the current (possibly inverted) position.
       */
      if ((SLOT->volume += SLOT->delta_rr) > MAX_ATT_INDEX) {
        SLOT->volume = MAX_ATT_INDEX;
        SLOT->state = EG_OFF;
      }
    } else
#endif
    {
      if ((SLOT->volume += SLOT->delta_rr) > MAX_ATT_INDEX) {
        SLOT->volume = MAX_ATT_INDEX;
        SLOT->state = EG_OFF;
      }
    }
    break;
  }

  /* Calculate output with SSG-EG inversion */
#if FM_SEG_SUPPORT
  if ((SLOT->SEG & SSG_ENABLE) && SLOT->ssg_inv && (SLOT->state != EG_OFF)) {
    /* SSG-EG inversion: output = MAX - volume
     * This creates the "upward" envelope shapes */
    out = SLOT->TLL + ((MAX_ATT_INDEX - (unsigned int)SLOT->volume) >> ENV_SH);
  } else
#endif
  {
    out = SLOT->TLL + (((unsigned int)SLOT->volume) >> ENV_SH);
  }

  if (SLOT->ams)
    out += (SLOT->ams * lfo_amd / LFO_RATE);
  return out;
}

/* ---------- update phase increment counter of operator ---------- */
void CALC_FCSLOT(FM_SLOT *SLOT, int fc, int kc)
{
  int ksr;

  /* (frequency) phase increment counter.
   * The detune addition can overflow the 17-bit FNUM range; mask it back to
   * 17 bits before the multiply, matching the real YM2612/Genesis Plus GX
   * behavior. Fixes incorrect sound in GEMS-driver games (Comix Zone,
   * Flashback, etc.) — TECH_DEBT TD-019. */
  fc += SLOT->DT[kc];
  fc &= 0x1ffff; /* 17-bit phase overflow */
  SLOT->Incr = (fc * SLOT->mul) >> 1;

  ksr = kc >> SLOT->KSR;
  if (SLOT->ksr != ksr) {
    SLOT->ksr = ksr;
    /* calculate envelope generator rates */
    if ((SLOT->ARval + ksr) < 32 + 62)
      SLOT->delta_ar = SLOT->AR[ksr];
    else
      SLOT->delta_ar = MAX_ATT_INDEX + 1;
    SLOT->delta_dr = SLOT->DR[ksr];
    SLOT->delta_sr = SLOT->SR[ksr];
    SLOT->delta_rr = SLOT->RR[ksr];
  }
}
