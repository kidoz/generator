/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fm_eg.hpp - YM2612 envelope + phase-increment core, extracted for testing.
 *
 * calc_eg() and CALC_FCSLOT() and the FM_SLOT struct they operate on were
 * private (INLINE / file-scope) inside fm.c, which made the envelope math
 * untestable. This header exposes the struct, the envelope/LFO constants the
 * math depends on, and externally-linked declarations of the two functions so
 * they can be driven directly by unit tests (tests/test_fm_eg.cpp).
 *
 * fm.c includes this header instead of defining its own copy, so there is a
 * single source of truth and the production audio path is unchanged.
 *
 * The only behavioral difference from the pre-extraction INLINE code:
 * calc_eg() takes the current LFO AM depth (lfo_amd) as an explicit parameter
 * instead of reading fm.c's static global. Every caller in fm.c passes that
 * global, so the output is byte-for-byte identical.
 */

#ifndef FM_EG_HPP
#define FM_EG_HPP

#include "fm.h" /* INT32, UINT32, UINT8 typedefs */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Envelope / LFO constants (canonical copy for standalone use) ----
 * fm.c defines these same macros itself before including this header, so each
 * is guarded to avoid redefinition; the values are identical. */
#ifndef ENV_SH
#define ENV_SH 16
#endif
#ifndef ENV_LEN
#define ENV_LEN (1 << 10) /* 1024 envelope steps */
#endif
#ifndef ENV_MASK
#define ENV_MASK ((1 << ENV_SH) - 1)
#endif
#ifndef MAX_ATT_INDEX
#define MAX_ATT_INDEX ((ENV_LEN << ENV_SH) - 1) /* 1023.ffff */
#endif
#ifndef MIN_ATT_INDEX
#define MIN_ATT_INDEX ((1 << ENV_SH) - 1) /*    0.ffff */
#endif

#ifndef LFO_RATE
#define LFO_RATE 0x10000
#endif

#ifndef EG_ATT
#define EG_ATT 4
#endif
#ifndef EG_DEC
#define EG_DEC 3
#endif
#ifndef EG_SUS
#define EG_SUS 2
#endif
#ifndef EG_REL
#define EG_REL 1
#endif
#ifndef EG_OFF
#define EG_OFF 0
#endif

#ifndef SSG_ENABLE
#define SSG_ENABLE 0x08 /* bit 3: SSG-EG enable */
#endif
#ifndef SSG_ALTERNATE
#define SSG_ALTERNATE 0x02 /* bit 1: alternate direction each cycle */
#endif
#ifndef SSG_HOLD
#define SSG_HOLD 0x01 /* bit 0: hold at end of cycle */
#endif

/* SSG-EG operates at half the normal envelope resolution: its cycle completes
 * at 512 (0x200) rather than the full 1024-step MAX_ATT_INDEX. This matches
 * Genesis Plus GX (TD-020). Used for the SSG-EG completion comparison and the
 * output-inversion axis in calc_eg(). */
#ifndef SSG_ATT_THRESHOLD
#define SSG_ATT_THRESHOLD (0x200 << ENV_SH) /* 512 in 16.16 fixed point */
#endif

/* ---- OPN/OPM one operator ----
 * Single source of truth: fm.c includes this header instead of defining its
 * own FM_SLOT, so the struct layout is shared between production and tests. */
typedef struct fm_slot {
  INT32 *DT;        /* detune          :DT_TABLE[DT]		*/
  int DT2;          /* multiple,Detune2:(DT2<<4)|ML for OPM	*/
  UINT32 TL;        /* total level     :TL << 3				*/
  UINT8 KSR;        /* key scale rate  :3-KSR				*/
  UINT8 ARval;      /* current AR							*/
  const UINT32 *AR; /* attack rate     :&AR_TABLE[AR<<1]	*/
  const UINT32 *DR; /* decay rate      :&DR_TABLE[DR<<1]	*/
  const UINT32 *SR; /* sustain rate    :&DR_TABLE[SR<<1]	*/
  const UINT32 *RR; /* release rate    :&DR_TABLE[RR<<2+2]	*/
  UINT8 SEG;        /* SSG EG type     :SSGEG				*/
  UINT8 ssg_inv;    /* SSG-EG output inversion flag        */
  UINT8 ksr;        /* key scale rate  :kcode>>(3-KSR)		*/
  UINT32 mul;       /* multiple        :ML_TABLE[ML]		*/

  /* Phase Generator */
  UINT32 Cnt;  /* frequency count :					*/
  UINT32 Incr; /* frequency step  :					*/

  /* Envelope Generator */
  UINT8 state;  /* phase type							*/
  INT32 volume; /* envelope counter						*/
  UINT32 sl;    /* sustain level   :SL_TABLE[SL]		*/

  UINT32 delta_ar; /* envelope step for Attack				*/
  UINT32 delta_dr; /* envelope step for Decay				*/
  UINT32 delta_sr; /* envelope step for Sustain			*/
  UINT32 delta_rr; /* envelope step for Release			*/
  UINT32 TLL;      /* adjusted TotalLevel					*/

  UINT32 key; /* 0=last key was KEY OFF, 1=KEY ON		*/

  /* LFO */
  UINT32 amon; /* AMS enable flag						*/
  UINT32 ams;  /* AMS depth level of this SLOT			*/
} FM_SLOT;

/* ---- Envelope + phase-increment core (externally linked for testing) ---- */

/* Advance one envelope step on SLOT and return the operator output level.
 * lfo_amd is the current LFO AM depth (caller passes fm.c's global). */
unsigned int calc_eg(FM_SLOT *SLOT, UINT32 lfo_amd);

/* Recompute the phase increment (Incr) and, if the key-scale rate changed,
 * the envelope rates for SLOT, given the channel frequency (fc) and key code
 * (kc). Pure function of (SLOT, fc, kc). */
void CALC_FCSLOT(FM_SLOT *SLOT, int fc, int kc);

#ifdef __cplusplus
}
#endif

#endif /* FM_EG_HPP */
