#define YM2610B_WARNING

/* YM2608 rhythm data is PCM ,not an ADPCM */
#define YM2608_RHYTHM_PCM

/*
**
** File: fm.c -- software implementation of Yamaha FM sound generator
**
** Copyright (C) 1998 Tatsuyuki Satoh , MultiArcadeMachineEmulator development
**
** Version 0.37e
**
*/

/*
** History:
**
** 12-08-2001 Jarek Burczynski:
**  - corrected sin_tab and tl_tab data	(verified on real chip)
**  - corrected feedback calculations (verified on real chip)
**  - corrected phase generator calculations (verified on real chip)
**  - corrected envelope generator calculations (verified on real chip)
**  - corrected FM volume level (YM2610 and YM2610B).
**  - changed YMxxxUpdateOne() functions (YM2203, YM2608, YM2610, YM2610B,
* YM2612) :
**    this was needed to calculate YM2610 FM channels output correctly.
**    (Each FM channel is calculated as in other chips, but the output of the
* channel
**    gets shifted right by one *before* sending to accumulator. That was
* impossible to do
**    with previous implementation).
**
** 23-07-2001 Jarek Burczynski, Nicola Salmoria:
**  - corrected YM2610 ADPCM type A algorithm and tables (verified on real chip)
**
** 11-06-2001 Jarek Burczynski:
**  - corrected end of sample bug in OPNB_ADPCM_CALC_CHA.
**    Real YM2610 checks for equality between current and end addresses (only 20
* LSB bits).
**
** 08-12-98 hiro-shi:
** rename ADPCMA -> ADPCMB, ADPCMB -> ADPCMA
** move ROM limit check.(CALC_CH? -> 2610Write1/2)
** test program (ADPCMB_TEST)
** move ADPCM A/B end check.
** ADPCMB repeat flag(no check)
** change ADPCM volume rate (8->16) (32->48).
**
** 09-12-98 hiro-shi:
** change ADPCM volume. (8->16, 48->64)
** replace ym2610 ch0/3 (YM-2610B)
** init cur_chip (restart bug fix)
** change ADPCM_SHIFT (10->8) missing bank change 0x4000-0xffff.
** add ADPCM_SHIFT_MASK
** change ADPCMA_DECODE_MIN/MAX.
*/


/*
  TO DO:
!!!!!!!	CORRECT FIRST MISSING CREDIT SOUND IN GIGANDES (DELTA-T module, when
DELTAN register = 0) !!!!!!
    - add SSG envelope generator support (darkseal)
    - use real sample rate and let mixer.c do the sample rate convertion

  no check:
    YM2608 rhythm sound
    OPN SSG type envelope (SEG)
    YM2151 CSM speech mode

  no support:
    YM2608 status mask (register :0x110)
    YM2608 RYTHM sound
    YM2608 PCM memory data access , DELTA-T-ADPCM with PCM port
    YM2151 CSM speech mode with internal timer

  preliminary :
    key scale level rate (?)
    YM2151 noise mode (CH7.OP4)
    LFO contoller (YM2612/YM2610/YM2608/YM2151)

  note:
                        OPN                           OPM
    fnum          fM * 2^20 / (fM/(12*n))
    TimerOverA    ( 12*n)*(1024-NA)/fM        64*(1024-Na)/fM
    TimerOverB    (192*n)*(256-NB)/fM       1024*(256-Nb)/fM
    output bits   10bit<<3bit               16bit * 2ch (YM3012=10bit<<3bit)
    sampling rate fFM / (12*prescaler)      fM / 64
    lfo freq                                ( fM*2^(LFRQ/16) ) / (4295*10^6)
*/

/************************************************************************/
/*    comment of hiro-shi(Hiromitsu Shioya)                             */
/*    YM2610(B) = OPN-B                                                 */
/*    YM2610  : PSG:3ch FM:4ch ADPCM(18.5KHz):6ch DeltaT ADPCM:1ch      */
/*    YM2610B : PSG:3ch FM:6ch ADPCM(18.5KHz):6ch DeltaT ADPCM:1ch      */
/************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include <new>

/* Generator - legacy headers plus the extracted testable units, which the
 * Catch2 tests include directly. */
#include "support.h"
#include "fm.h"
#include "genstate.h"
#include "fm_eg.hpp" /* FM_SLOT struct + calc_eg/CALC_FCSLOT (extracted, tested) */
#include "fm_lfo.hpp" /* opn_lfo_step (extracted, tested) */
#include "ym2612.hpp"

#define _STATE_H

#ifndef PI
#define PI 3.14159265358979323846
#endif


/***** shared function building option ****/
#define BUILD_OPN                                                   \
  (BUILD_YM2203 || BUILD_YM2608 || BUILD_YM2610 || BUILD_YM2610B || \
   BUILD_YM2612)
#define BUILD_OPNB (BUILD_YM2610 || BUILD_YM2610B)
#define BUILD_OPN_PRESCALER (BUILD_YM2203 || BUILD_YM2608)
#define BUILD_ADPCMA (BUILD_YM2608 || BUILD_YM2610 || BUILD_YM2610B)
#define BUILD_ADPCMB (BUILD_YM2608 || BUILD_YM2610 || BUILD_YM2610B)


#if BUILD_ADPCMB
/* include external DELTA-T ADPCM unit */
#include "ymdeltat.h" /* DELTA-T ADPCM UNIT */
#endif

/* -------------------- sound quality define selection --------------------- */
#define FREQ_SH 16  /* 16.16 fixed point (frequency calculations) */
#define ENV_SH 16   /* 16.16 fixed point (envelope calculations)  */
#define LFO_SH 23   /*  9.23 fixed point (LFO calculations)       */
#define TIMER_SH 16 /* 16.16 fixed point (timers calculations)    */

#define FREQ_MASK ((1 << FREQ_SH) - 1)
#define ENV_MASK ((1 << ENV_SH) - 1)

/* envelope output entries */
#define ENV_BITS 10
#define ENV_LEN (1 << ENV_BITS)
#define ENV_STEP (128.0 / ENV_LEN)
#define ENV_QUIET ((int)(0x68 / (ENV_STEP)))

#define MAX_ATT_INDEX ((ENV_LEN << ENV_SH) - 1) /* 1023.ffff */
#define MIN_ATT_INDEX ((1 << ENV_SH) - 1)       /*    0.ffff */

/* sinwave entries */
#define SIN_BITS 10
#define SIN_LEN (1 << SIN_BITS)
#define SIN_MASK (SIN_LEN - 1)

#define TL_RES_LEN (256) /* 8 bits addressing (real chip) */


/* LFO table entries */
#define LFO_ENT 512
#define LFO_RATE 0x10000
#define PMS_RATE 0x400
/* LFO runtime work */
#if BUILD_YM2610B || BUILD_YM2612 /* jp 2001-09-30 */
#endif
/* OPN LFO waveform table */
static INT32 OPN_LFO_wave[LFO_ENT];

/* -------------------- tables --------------------- */

/* sustain level table (3db per step) */
/* bit0, bit1, bit2, bit3, bit4, bit5, bit6 */
/* 1,    2,    4,    8,    16,   32,   64   (value)*/
/* 0.75, 1.5,  3,    6,    12,   24,   48   (dB)*/

/* 0 - 15: 0, 3, 6, 9,12,15,18,21,24,27,30,33,36,39,42,93 (dB)*/
#define SC(db) (UINT32)(db * (4.0 / ENV_STEP) * (1 << ENV_SH))
static const UINT32 SL_TABLE[16] = {
    SC(0), SC(1), SC(2),  SC(3),  SC(4),  SC(5),  SC(6),  SC(7),
    SC(8), SC(9), SC(10), SC(11), SC(12), SC(13), SC(14), SC(31)};
#undef SC

/*	TL_TAB_LEN is calculated as:
 *	13 - sinus amplitude bits     (Y axis)
 *	2  - sinus sign bit           (Y axis)
 *	TL_RES_LEN - sinus resolution (X axis)
 */
#define TL_TAB_LEN (13 * 2 * TL_RES_LEN)
static signed int tl_tab[TL_TAB_LEN];

/* sin waveform table in 'decibel' scale */
static unsigned int sin_tab[SIN_LEN];


#define OPM_DTTABLE OPN_DTTABLE
static UINT8 OPN_DTTABLE[4 * 32] = {
    /* this is YM2151 and YM2612 phase increment data (in 10.10 fixed point
       format)*/
    /* FD=0 */
    0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    /* FD=1 */
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5,
    6, 6, 7, 8, 8, 8, 8,
    /* FD=2 */
    1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11,
    12, 13, 14, 16, 16, 16, 16,
    /* FD=3 */
    2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14,
    16, 17, 19, 20, 22, 22, 22, 22};


/* output final shift */
#if (FM_SAMPLE_BITS == 16)
#define FINAL_SH (0)
#define MAXOUT (+32767)
#define MINOUT (-32768)
#else
#define FINAL_SH (8)
#define MAXOUT (+127)
#define MINOUT (-128)
#endif

/* -------------------- local defines , macros --------------------- */
/* register number to channel number , slot offset */
#define OPN_CHAN(N) (N & 3)
#define OPN_SLOT(N) ((N >> 2) & 3)
#define OPM_CHAN(N) (N & 7)
#define OPM_SLOT(N) ((N >> 3) & 3)
/* slot number */
#define SLOT1 0
#define SLOT2 2
#define SLOT3 1
#define SLOT4 3

/* bit0 = Right enable , bit1 = Left enable */
#define OUTD_RIGHT 1
#define OUTD_LEFT 2
#define OUTD_CENTER 3

/* FM timer model */
#define FM_TIMER_SINGLE (0)
#define FM_TIMER_INTERVAL (1)

/* ---------- debug section ------------------- */
/* save output as raw 16-bit sample */
/* #define SAVE_SAMPLE */

#ifdef SAVE_SAMPLE
static FILE *sample[1];
#if 0 /*save to MONO file */
#define SAVE_ALL_CHANNELS                                \
  {                                                      \
    signed int pom = rt;                                 \
    fputc((unsigned short)pom & 0xff, sample[0]);        \
    fputc(((unsigned short)pom >> 8) & 0xff, sample[0]); \
  }
#else /*save to STEREO file */
#define SAVE_ALL_CHANNELS                                \
  {                                                      \
    signed int pom = lt;                                 \
    fputc((unsigned short)pom & 0xff, sample[0]);        \
    fputc(((unsigned short)pom >> 8) & 0xff, sample[0]); \
    pom = rt;                                            \
    fputc((unsigned short)pom & 0xff, sample[0]);        \
    fputc(((unsigned short)pom >> 8) & 0xff, sample[0]); \
  }
#endif
#endif


/* ---------- OPN / OPM one channel  ---------- */
/* FM_SLOT is defined in fm_eg.hpp (shared with the extracted envelope core
 * and unit tests). */

typedef struct fm_chan {
  FM_SLOT SLOT[4];
  UINT8 ALGO;       /* Algorithm						*/
  UINT8 FB;         /* feedback shift					*/
  INT32 op1_out[2]; /* op1 output for feedback			*/
  /* Algorithm (connection) */
  INT32 *connect1; /* pointer of SLOT1 output			*/
  INT32 *connect2; /* pointer of SLOT2 output			*/
  INT32 *connect3; /* pointer of SLOT3 output			*/
  INT32 *connect4; /* pointer of SLOT4 output			*/
  /* LFO */
  INT32 pms;  /* PMS depth channel level			*/
  UINT32 ams; /* AMS depth channel level			*/
  /* Phase Generator */
  UINT32 fc;   /* fnum,blk:adjusted to sample rate	*/
  UINT8 kcode; /* key code:						*/
} FM_CH;

/* OPN/OPM common state */
typedef struct fm_state {
  UINT8 index;      /* chip index (number of chip) */
  int clock;        /* master clock  (Hz)  */
  int rate;         /* sampling rate (Hz)  */
  double freqbase;  /* frequency base      */
  double TimerBase; /* Timer base time     */
#if FM_BUSY_FLAG_SUPPORT
  INT32 BusyCount;  /* Busy flag sample counter (decrements each sample) */
#endif
  UINT8 address;       /* address register		*/
  UINT8 irq;           /* interrupt level		*/
  UINT8 irqmask;       /* irq mask				*/
  UINT8 status;        /* status flag			*/
  UINT32 mode;         /* mode  CSM / 3SLOT	*/
  UINT8 prescaler_sel; /* prescaler slelector	*/
  UINT8 fn_h;          /* freq latch			*/
  int TA;              /* timer a				*/
  int TAC;             /* timer a counter		*/
  UINT8 TB;            /* timer b				*/
  int TBC;             /* timer b counter		*/
  /* local time tables */
  INT32 DT_TABLE[8][32]; /* DeTune table		*/
  UINT32 eg_tab[32 + 64 +
                32]; /* Envelope Generator rates (32 + 64 rates + 32 RKS) */
  /* Extention Timer and IRQ handler */
  FM_TIMERHANDLER Timer_Handler;
  FM_IRQHANDLER IRQ_Handler;
  /* timer model single / interval */
  UINT8 timermodel;
} FM_ST;


/* -------------------- state --------------------- */

/* some globals */
#define TYPE_SSG 0x01                             /* SSG support          */
#define TYPE_OPN 0x02 /* OPN device           */  // this one is not used ????
#define TYPE_LFOPAN 0x04                          /* OPN type LFO and PAN */
#define TYPE_6CH 0x08                             /* FM 6CH / 3CH         */
#define TYPE_DAC 0x10                             /* YM2612's DAC device  */
#define TYPE_ADPCM 0x20                           /* two ADPCM units      */

#define TYPE_YM2203 (TYPE_SSG)
#define TYPE_YM2608 (TYPE_SSG | TYPE_LFOPAN | TYPE_6CH | TYPE_ADPCM)
#define TYPE_YM2610 (TYPE_SSG | TYPE_LFOPAN | TYPE_6CH | TYPE_ADPCM)
#define TYPE_YM2612 (TYPE_DAC | TYPE_LFOPAN | TYPE_6CH)

/* Per-chip runtime work area. This chip's FM_CH connect* pointers point
 * into these buffers (see setup_connection). Formerly file-scope statics
 * (out_fm/pg_in*) that the cur_chip cache swapped per chip; now embedded in
 * FM_OPN so every OPN-family user carries its own. */
typedef struct fm_work {
  INT32 out_fm[8];              /* channel outputs */
  INT32 pg_in2, pg_in3, pg_in4; /* PG modulation sums */
} FM_WORK;

/* -------------------- log output  -------------------- */
/* log output level */
#define LOG_ERR 3 /* ERROR       */
#define LOG_WAR 2 /* WARNING     */
#define LOG_INF 1 /* INFORMATION */
#define LOG_LEVEL LOG_INF

#ifndef __RAINE__
#define LOG(n, x)       \
  if ((n) >= LOG_LEVEL) \
  logerror x
#endif

/* ----- limitter ----- */
#define Limit(val, max, min) \
  {                          \
    if (val > max)           \
      val = max;             \
    else if (val < min)      \
      val = min;             \
  }

/* ----- YM2612 DAC ladder effect ----- */
/* The YM2612 has a 9-bit DAC with inherent non-linearity due to resistor
 * ladder mismatches. This creates crossover distortion around zero crossing,
 * giving the characteristic "crunchy" sound heard in Streets of Rage, etc.
 *
 * Based on analysis from Nuked-OPN2:
 * - The DAC has asymmetric behavior around zero
 * - Positive samples get a small positive bias
 * - There's a "dead zone" around zero that pushes samples away
 * - The effect is more pronounced at low amplitudes
 *
 * This simplified model captures the essential character of the distortion.
 */
INLINE INT32 ym2612_dac_ladder(INT32 sample)
{
  /* The ladder effect creates a "dead zone" around zero.
   * Samples near zero get pushed away, creating crossover distortion.
   * The threshold is approximately 1/256 of full scale (9-bit DAC behavior).
   *
   * For a 14-bit internal sample (after FINAL_SH), the dead zone is ~64 units.
   * We model this as: if |sample| < threshold, push it away from zero.
   */
  const INT32 LADDER_THRESHOLD = 64;  /* Dead zone threshold */
  const INT32 LADDER_BIAS = 32;       /* Bias to add in dead zone */

  if (sample > 0) {
    if (sample < LADDER_THRESHOLD) {
      /* In dead zone: push away from zero */
      sample += LADDER_BIAS;
    }
    /* Small positive bias for all positive samples (DAC +1 behavior) */
    sample += (sample >> 9) + 1;
  } else if (sample < 0) {
    if (sample > -LADDER_THRESHOLD) {
      /* In dead zone: push away from zero */
      sample -= LADDER_BIAS;
    }
    /* Slight asymmetry for negative samples */
    sample -= ((-sample) >> 9);
  }
  /* sample == 0 stays at 0 */

  return sample;
}

/* ----- buffering one of data(STEREO chip) ----- */
#if FM_STEREO_MIX
/* stereo mixing */
#define FM_BUFFERING_STEREO                     \
  {                                             \
    /* get left & right output with clipping */ \
    out_ch[OUTD_LEFT] += out_ch[OUTD_CENTER];   \
    Limit(out_ch[OUTD_LEFT], MAXOUT, MINOUT);   \
    out_ch[OUTD_RIGHT] += out_ch[OUTD_CENTER];  \
    Limit(out_ch[OUTD_RIGHT], MAXOUT, MINOUT);  \
    /* buffering */                             \
    *bufL++ = out_ch[OUTD_LEFT] >> FINAL_SH;    \
    *bufL++ = out_ch[OUTD_RIGHT] >> FINAL_SH;   \
  }
#else
/* stereo separate */
#define FM_BUFFERING_STEREO                     \
  {                                             \
    /* get left & right output with clipping */ \
    out_ch[OUTD_LEFT] += out_ch[OUTD_CENTER];   \
    Limit(out_ch[OUTD_LEFT], MAXOUT, MINOUT);   \
    out_ch[OUTD_RIGHT] += out_ch[OUTD_CENTER];  \
    Limit(out_ch[OUTD_RIGHT], MAXOUT, MINOUT);  \
    /* buffering */                             \
    bufL[i] = out_ch[OUTD_LEFT] >> FINAL_SH;    \
    bufR[i] = out_ch[OUTD_RIGHT] >> FINAL_SH;   \
  }
#endif

#if FM_INTERNAL_TIMER
/* ----- internal timer mode , update timer */
/* ---------- calculate timer A ---------- */
#define INTERNAL_TIMER_A(ST, CSM_CH)                      \
  {                                                       \
    if (ST->TAC && (ST->Timer_Handler == 0))              \
      if ((ST->TAC -= (int)(ST->freqbase * 4096)) <= 0) { \
        TimerAOver(ST);                                   \
        /* CSM mode total level latch and auto key on */  \
        if (ST->mode & 0x80)                              \
          CSMKeyControll(CSM_CH);                         \
      }                                                   \
  }
/* ---------- calculate timer B ---------- */
#define INTERNAL_TIMER_B(ST, step)                             \
  {                                                            \
    if (ST->TBC && (ST->Timer_Handler == 0))                   \
      if ((ST->TBC -= (int)(ST->freqbase * 4096 * step)) <= 0) \
        TimerBOver(ST);                                        \
  }
#else /* FM_INTERNAL_TIMER */
/* external timer mode */
#define INTERNAL_TIMER_A(ST, CSM_CH)
#define INTERNAL_TIMER_B(ST, step)
#endif /* FM_INTERNAL_TIMER */

/* --------------------- subroutines  --------------------- */
/* status set and IRQ handling */
INLINE void FM_STATUS_SET(FM_ST *ST, int flag)
{
  /* set status flag */
  ST->status |= flag;
  if (!(ST->irq) && (ST->status & ST->irqmask)) {
    ST->irq = 1;
    /* callback user interrupt handler (IRQ is OFF to ON) */
    if (ST->IRQ_Handler)
      (ST->IRQ_Handler)(ST->index, 1);
  }
}

/* status reset and IRQ handling */
INLINE void FM_STATUS_RESET(FM_ST *ST, int flag)
{
  /* reset status flag */
  ST->status &= ~flag;
  if ((ST->irq) && !(ST->status & ST->irqmask)) {
    ST->irq = 0;
    /* callback user interrupt handler (IRQ is ON to OFF) */
    if (ST->IRQ_Handler)
      (ST->IRQ_Handler)(ST->index, 0);
  }
}

/* IRQ mask set */
static void FM_IRQMASK_SET(FM_ST *ST, int flag)
{
  ST->irqmask = flag;
  /* IRQ handling check */
  FM_STATUS_SET(ST, 0);
  FM_STATUS_RESET(ST, 0);
}

#if FM_BUSY_FLAG_SUPPORT
/* Busy flag implementation using sample counter.
 * The YM2612 sets the busy flag for approximately 32 FM clock cycles after a write.
 * At 7.67 MHz FM clock and 44.1 kHz sample rate, this is less than 1 sample,
 * but we use a minimum of 1 sample to ensure the flag is visible to polling code.
 *
 * The busy flag is important for games that poll the status register to wait
 * for the chip to be ready before writing the next value. */
INLINE UINT8 FM_STATUS_FLAG(FM_ST *ST)
{
  if (ST->BusyCount > 0)
    return ST->status | 0x80; /* with busy */
  return ST->status;
}
INLINE void FM_BUSY_SET(FM_ST *ST, int busyclock)
{
  /* Set busy for at least 1 sample. The busyclock parameter indicates
   * the number of FM clocks the operation takes. We convert to samples:
   * samples = busyclock * sample_rate / fm_clock
   * For typical values (busyclock=32, rate=44100, clock=7670454):
   * samples = 32 * 44100 / 7670454 ≈ 0.18, so we use minimum of 1 */
  (void)busyclock; /* Currently unused - using fixed 1 sample */
  ST->BusyCount = 1;
}
/* Called from Ym2612::update_one to decrement busy counter each sample */
INLINE void FM_BUSY_UPDATE(FM_ST *ST)
{
  if (ST->BusyCount > 0)
    ST->BusyCount--;
}
#define FM_BUSY_CLEAR(ST) ((ST)->BusyCount = 0)
#else
#define FM_STATUS_FLAG(ST) ((ST)->status)
#define FM_BUSY_SET(ST, bclock) \
  {                             \
  }
#define FM_BUSY_UPDATE(ST) \
  {                        \
  }
#define FM_BUSY_CLEAR(ST) \
  {                       \
  }
#endif

/* ---------- event handler of Phase Generator ---------- */

/* phase of the envelope generator */
#define EG_ATT 4
#define EG_DEC 3
#define EG_SUS 2
#define EG_REL 1
#define EG_OFF 0

/* SSG-EG bit definitions (from SEG register, bits 0-3) */
#define SSG_ENABLE   0x08  /* bit 3: SSG-EG enable */
#define SSG_ATTACK   0x04  /* bit 2: attack direction (0=down, 1=up) */
#define SSG_ALTERNATE 0x02 /* bit 1: alternate direction each cycle */
#define SSG_HOLD     0x01  /* bit 0: hold at end of cycle */

#if 0
/* This will be removed as soon as SSG support will be added */
#if FM_SEG_SUPPORT

/* SEG down side end  */
static void FM_EG_SSG_sr( FM_SLOT *SLOT )
{
	if( SLOT->SEG&2){
		/* reverse */
		SLOT->state = FM_EG_SSG_SR;
		SLOT->volume = SLOT->SL + (EG_UST - EG_DST);
		SLOT->eve = EG_UED;
		SLOT->evs = SLOT->delta_sr;
	}else{
		/* again */
		SLOT->volume = EG_DST;
	}
	/* hold */
	if( SLOT->SEG&1) SLOT->evs = 0;
}

/* SEG upside side end */
static void FM_EG_SSG_sr( FM_SLOT *SLOT )
{
	if( SLOT->SEG&2){
		/* reverse  */
		SLOT->state = FM_EG_SSG_DR;
		SLOT->volume = EG_DST;
		SLOT->eve = EG_DED;
		SLOT->evs = SLOT->delta_dr;
	}else{
		/* again */
		SLOT->volume = SLOT->SL + (EG_UST - EG_DST);
	}
	/* hold check */
	if( SLOT->SEG&1) SLOT->evs = 0;
}

/* SEG Attack end */
static void FM_EG_SSG_ar( FM_SLOT *SLOT )
{
	if( SLOT->SEG&4){	/* start direction */
		/* next SSG-SR (upside start ) */
		SLOT->state = FM_EG_SSG_SR;
		SLOT->volume = SLOT->SL + (EG_UST - EG_DST);
		SLOT->eve = EG_UED;
		SLOT->evs = SLOT->delta_sr;
	}else{
		/* next SSG-DR (downside start ) */
		SLOT->state = FM_EG_SSG_DR;
		SLOT->volume = EG_DST;
		SLOT->eve = EG_DED;
		SLOT->evs = SLOT->delta_dr;
	}
}
#endif /* FM_SEG_SUPPORT */
#endif


/* ----- key on of SLOT ----- */
INLINE void FM_KEYON(FM_CH *CH, int s)
{
  FM_SLOT *SLOT = &CH->SLOT[s];
  if (!SLOT->key) {
    SLOT->key = 1;
    /* restart Phase Generator */
    SLOT->Cnt = 0;
#if FM_SEG_SUPPORT
    /* SSG-EG: initialize inversion state based on attack direction (bit 2) */
    if (SLOT->SEG & SSG_ENABLE) {
      /* SSG_ATTACK (bit 2) determines initial direction:
       * 0 = start with downward envelope (normal)
       * 1 = start with upward envelope (inverted output) */
      SLOT->ssg_inv = (SLOT->SEG & SSG_ATTACK) ? 1 : 0;
    } else {
      SLOT->ssg_inv = 0;
    }
#endif
    /* phase -> Attack */
    SLOT->state = EG_ATT;
  }
}
/* ----- key off of SLOT ----- */
INLINE void FM_KEYOFF(FM_CH *CH, int s)
{
  /* Behavior is implemented in fm_slot_keyoff() (fm_eg.cpp, extracted for
   * unit testing). */
  fm_slot_keyoff(&CH->SLOT[s]);
}

/* setup Algorithm connection */
static void setup_connection(FM_WORK *W, FM_CH *CH, int ch)
{
  INT32 *carrier = &W->out_fm[ch];

  switch (CH->ALGO) {
  case 0:
    /*  PG---S1---S2---S3---S4---OUT */
    CH->connect1 = &W->pg_in2;
    CH->connect2 = &W->pg_in3;
    CH->connect3 = &W->pg_in4;
    break;
  case 1:
    /*  PG---S1-+-S3---S4---OUT */
    /*  PG---S2-+               */
    CH->connect1 = &W->pg_in3;
    CH->connect2 = &W->pg_in3;
    CH->connect3 = &W->pg_in4;
    break;
  case 2:
    /* PG---S1------+-S4---OUT */
    /* PG---S2---S3-+          */
    CH->connect1 = &W->pg_in4;
    CH->connect2 = &W->pg_in3;
    CH->connect3 = &W->pg_in4;
    break;
  case 3:
    /* PG---S1---S2-+-S4---OUT */
    /* PG---S3------+          */
    CH->connect1 = &W->pg_in2;
    CH->connect2 = &W->pg_in4;
    CH->connect3 = &W->pg_in4;
    break;
  case 4:
    /* PG---S1---S2-+--OUT */
    /* PG---S3---S4-+      */
    CH->connect1 = &W->pg_in2;
    CH->connect2 = carrier;
    CH->connect3 = &W->pg_in4;
    break;
  case 5:
    /*         +-S2-+     */
    /* PG---S1-+-S3-+-OUT */
    /*         +-S4-+     */
    CH->connect1 = 0; /* special case */
    CH->connect2 = carrier;
    CH->connect3 = carrier;
    break;
  case 6:
    /* PG---S1---S2-+     */
    /* PG--------S3-+-OUT */
    /* PG--------S4-+     */
    CH->connect1 = &W->pg_in2;
    CH->connect2 = carrier;
    CH->connect3 = carrier;
    break;
  case 7:
    /* PG---S1-+     */
    /* PG---S2-+-OUT */
    /* PG---S3-+     */
    /* PG---S4-+     */
    CH->connect1 = carrier;
    CH->connect2 = carrier;
    CH->connect3 = carrier;
  }
  CH->connect4 = carrier;
}

/* set detune & multiple */
INLINE void set_det_mul(FM_ST *ST, FM_CH *CH, FM_SLOT *SLOT, int v)
{
  SLOT->mul = (v & 0x0f) ? (v & 0x0f) * 2 : 1;
  SLOT->DT = ST->DT_TABLE[(v >> 4) & 7];
  CH->SLOT[SLOT1].Incr = -1;
}

/* set total level */
INLINE void set_tl(FM_CH *CH, FM_SLOT *SLOT, int v, int csmflag)
{
  SLOT->TL = (v & 0x7f) << (ENV_BITS - 7); /*7bit TL*/
  /* if it is not a CSM channel , latch the total level */
  if (!csmflag)
    SLOT->TLL = SLOT->TL;
}

/* set attack rate & key scale  */
INLINE void set_ar_ksr(FM_CH *CH, FM_SLOT *SLOT, int v, UINT32 *eg_tab)
{
  SLOT->KSR = 3 - (v >> 6);
  SLOT->ARval = (v & 0x1f) ? 32 + ((v & 0x1f) << 1) : 0;
  SLOT->AR = &eg_tab[SLOT->ARval];

  if ((SLOT->ARval + SLOT->ksr) < 32 + 62)
    SLOT->delta_ar = SLOT->AR[SLOT->ksr];
  else
    SLOT->delta_ar = MAX_ATT_INDEX + 1;

  CH->SLOT[SLOT1].Incr =
      -1; /* Optimize: only set this, if new SLOT->KSR is different */
}

/* set decay rate */
INLINE void set_dr(FM_SLOT *SLOT, int v, UINT32 *eg_tab)
{
  SLOT->DR = (v & 0x1f) ? &eg_tab[32 + ((v & 0x1f) << 1)] : &eg_tab[0];
  SLOT->delta_dr = SLOT->DR[SLOT->ksr];
}

/* set sustain rate */
INLINE void set_sr(FM_SLOT *SLOT, int v, UINT32 *eg_tab)
{
  SLOT->SR = (v & 0x1f) ? &eg_tab[32 + ((v & 0x1f) << 1)] : &eg_tab[0];
  SLOT->delta_sr = SLOT->SR[SLOT->ksr];
}

/* set release rate */
INLINE void set_sl_rr(FM_SLOT *SLOT, int v, UINT32 *eg_tab)
{
  SLOT->sl = SL_TABLE[v >> 4];
  SLOT->RR = &eg_tab[34 + ((v & 0x0f) << 2)];
  SLOT->delta_rr = SLOT->RR[SLOT->ksr];
}


INLINE signed int op_calc(UINT32 phase, unsigned int env, signed int pm)
{
  UINT32 p;

  p = (env << 3) +
      sin_tab[(((signed int)((phase & ~FREQ_MASK) + (pm << 15))) >> FREQ_SH) &
              SIN_MASK];

  if (p >= TL_TAB_LEN)
    return 0;
  return tl_tab[p];
}

INLINE signed int op_calc1(UINT32 phase, unsigned int env, signed int pm)
{
  UINT32 p;
  INT32 i;

  i = (phase & ~FREQ_MASK) + pm;

  /*logerror("i=%08x (i>>16)&511=%8i phase=%i [pm=%08x] ",i, (i>>16)&511,
   * phase>>FREQ_SH, pm);*/

  p = (env << 3) + sin_tab[(i >> FREQ_SH) & SIN_MASK];

  /*logerror("(p&255=%i p>>8=%i) out= %i\n", p&255,p>>8, tl_tab[p&255]>>(p>>8)
   * );*/

  if (p >= TL_TAB_LEN)
    return 0;
  return tl_tab[p];
}


/*
 * SSG-EG (SSG Envelope Generator) Implementation
 *
 * SSG-EG is controlled by register bits:
 *   Bit 3: Enable SSG-EG mode
 *   Bit 2: Attack direction (0 = attack towards min, 1 = attack towards max)
 *   Bit 1: Alternate (toggle direction after each cycle)
 *   Bit 0: Hold (hold at end instead of looping)
 *
 * The 8 SSG-EG types produce these envelope shapes:
 *   1000: \\\\\\\ (repeat decay)
 *   1001: \_____ (decay then hold low)
 *   1010: \/\/\/\ (triangle wave)
 *   1011: \^^^^^ (decay then hold high)
 *   1100: /////// (repeat attack)
 *   1101: /^^^^^ (attack then hold high)
 *   1110: /\/\/\/ (inverted triangle)
 *   1111: /_____ (attack then hold low)
 */

/* calc_eg() is implemented in fm_eg.cpp (extracted for unit testing).
 * It takes lfo_amd as an explicit parameter; callers below pass the file-scope
 * global of the same name. */


/* ---------- calculate one of channel ---------- */
INLINE void FM_CALC_CH(FM_WORK *W, FM_CH *CH, UINT32 lfo_amd, INT32 lfo_pmd)
{
  unsigned int eg_out1, eg_out2, eg_out3, eg_out4; /*envelope output*/

  /* Phase Generator */
  W->pg_in2 = W->pg_in3 = W->pg_in4 = 0;

  /* Envelope Generator */
  eg_out1 = calc_eg(&CH->SLOT[SLOT1], lfo_amd);
  eg_out2 = calc_eg(&CH->SLOT[SLOT2], lfo_amd);
  eg_out3 = calc_eg(&CH->SLOT[SLOT3], lfo_amd);
  eg_out4 = calc_eg(&CH->SLOT[SLOT4], lfo_amd);

  /* Connection */
  {
    INT32 out = CH->op1_out[0] + CH->op1_out[1];
    CH->op1_out[0] = CH->op1_out[1];

    if (!CH->connect1) {
      /* algorithm 5  */
      W->pg_in2 = W->pg_in3 = W->pg_in4 = CH->op1_out[0];
    } else {
      /* other algorithms */
      *CH->connect1 += CH->op1_out[0];
    }

    CH->op1_out[1] = 0;
    if (eg_out1 < ENV_QUIET) /* SLOT 1 */
      CH->op1_out[1] = op_calc1(CH->SLOT[SLOT1].Cnt, eg_out1, (out << CH->FB));
  }

  if (eg_out2 < ENV_QUIET) /* SLOT 2 */
    *CH->connect2 += op_calc(CH->SLOT[SLOT2].Cnt, eg_out2, W->pg_in2);

  if (eg_out3 < ENV_QUIET) /* SLOT 3 */
    *CH->connect3 += op_calc(CH->SLOT[SLOT3].Cnt, eg_out3, W->pg_in3);

  if (eg_out4 < ENV_QUIET) /* SLOT 4 */
    *CH->connect4 += op_calc(CH->SLOT[SLOT4].Cnt, eg_out4, W->pg_in4);


  /* update phase counters AFTER output calculations */
  {
    INT32 pms = lfo_pmd * CH->pms / LFO_RATE;
    if (pms) {
      CH->SLOT[SLOT1].Cnt +=
          CH->SLOT[SLOT1].Incr + (INT32)(pms * CH->SLOT[SLOT1].Incr) / PMS_RATE;
      CH->SLOT[SLOT2].Cnt +=
          CH->SLOT[SLOT2].Incr + (INT32)(pms * CH->SLOT[SLOT2].Incr) / PMS_RATE;
      CH->SLOT[SLOT3].Cnt +=
          CH->SLOT[SLOT3].Incr + (INT32)(pms * CH->SLOT[SLOT3].Incr) / PMS_RATE;
      CH->SLOT[SLOT4].Cnt +=
          CH->SLOT[SLOT4].Incr + (INT32)(pms * CH->SLOT[SLOT4].Incr) / PMS_RATE;
    } else {
      CH->SLOT[SLOT1].Cnt += CH->SLOT[SLOT1].Incr;
      CH->SLOT[SLOT2].Cnt += CH->SLOT[SLOT2].Incr;
      CH->SLOT[SLOT3].Cnt += CH->SLOT[SLOT3].Incr;
      CH->SLOT[SLOT4].Cnt += CH->SLOT[SLOT4].Incr;
    }
  }
}

/* ---------- update phase increment counter of operator ---------- */
/* CALC_FCSLOT() is implemented in fm_eg.cpp (extracted for unit testing). */

/* ---------- update phase increments counters  ---------- */
INLINE void OPN_CALC_FCOUNT(FM_CH *CH)
{
  if (CH->SLOT[SLOT1].Incr == -1) {
    int fc = CH->fc;
    int kc = CH->kcode;
    CALC_FCSLOT(&CH->SLOT[SLOT1], fc, kc);
    CALC_FCSLOT(&CH->SLOT[SLOT2], fc, kc);
    CALC_FCSLOT(&CH->SLOT[SLOT3], fc, kc);
    CALC_FCSLOT(&CH->SLOT[SLOT4], fc, kc);
  }
}

/* ----------- initialize time tables ----------- */
static void init_timetables(FM_ST *ST, UINT8 *DTTABLE)
{
  int i, d;
  double rate;

#if 0
	logerror("FM.C: samplerate=%8i chip clock=%8i  freqbase=%f  \n",
			 ST->rate, ST->clock, ST->freqbase );
#endif

  /* DeTune table */
  for (d = 0; d <= 3; d++) {
    for (i = 0; i <= 31; i++) {
      rate = ((double)DTTABLE[d * 32 + i]) * SIN_LEN * ST->freqbase *
             (1 << FREQ_SH) / ((double)(1 << 20));
      ST->DT_TABLE[d][i] = (INT32)rate;
      ST->DT_TABLE[d + 4][i] = (INT32)-rate;
#if 0
			logerror("FM.C: DT [%2i %2i] = %8x  \n", d, i, ST->DT_TABLE[d][i] );
#endif
    }
  }

  /* calculate Envelope Generator rate table */
  for (i = 0; i < 34; i++)
    ST->eg_tab[i] = 0; /* infinity */

  /* Envelope timing ratio correction:
   * The original MAME core assumed a 1:3 ratio between frequency counter
   * and envelope generator updates. Hardware tests show the actual ratio
   * is approximately 1:2.4375 (39/16).
   *
   * To correct this, we scale the envelope rate divisor:
   * Original: 12.0 * 1024.0 (assumes 1:3 ratio)
   * Corrected: 12.0 * (48.0/39.0) * 1024.0 (corrects to 1:2.4375 ratio)
   *
   * This makes envelopes progress at the correct rate relative to pitch. */
  const double EG_RATE_DIVISOR = 12.0 * (48.0 / 39.0) * 1024.0;

  for (i = 2; i < 64; i++) {
    rate = ST->freqbase; /* frequency rate */
    if (i < 60)
      rate *= 1.0 + (i & 3) * 0.25; /* b0-1 : x1 , x1.25 , x1.5 , x1.75 */
    rate *= 1 << (i >> 2);          /* b2-5 : shift bit */
    rate /= EG_RATE_DIVISOR;
    rate *= (double)(1 << ENV_SH);
    ST->eg_tab[32 + i] = rate;
#if 0
		logerror("FM.C: Rate %2i %1i  Decay [real %11.4f ms][emul %11.4f ms][d=%08x]\n",i>>2, i&3,
			( ((double)(ENV_LEN<<ENV_SH)) / rate )                     * (1000.0 / (double)ST->rate),
			( ((double)(ENV_LEN<<ENV_SH)) / (double)ST->eg_tab[32+i] ) * (1000.0 / (double)ST->rate), ST->eg_tab[32+i] );
#endif
  }

  for (i = 0; i < 32; i++) {
    ST->eg_tab[32 + 64 + i] = ST->eg_tab[32 + 63];
  }
}

/* ---------- reset one of channel  ---------- */
static void reset_channel(FM_ST *ST, FM_CH *CH, int chan)
{
  int c, s;

  ST->mode = 0; /* normal mode */
  FM_STATUS_RESET(ST, 0xff);
  ST->TA = 0;
  ST->TAC = 0;
  ST->TB = 0;
  ST->TBC = 0;

  for (c = 0; c < chan; c++) {
    CH[c].fc = 0;
    for (s = 0; s < 4; s++) {
      CH[c].SLOT[s].SEG = 0;
      CH[c].SLOT[s].ssg_inv = 0;
      CH[c].SLOT[s].state = EG_OFF;
      CH[c].SLOT[s].volume = MAX_ATT_INDEX;
    }
  }
}

/* ---------- initialize generic tables ---------- */

static void init_tables(void)
{
  signed int i, x;
  signed int n;
  double o, m;

  for (x = 0; x < TL_RES_LEN; x++) {
    m = (1 << 16) / pow(2, (x + 1) * (ENV_STEP / 4.0) / 8.0);
    m = floor(m);

    /* we never reach (1<<16) here due to the (x+1) */
    /* result fits within 16 bits at maximum */

    n = (int)m; /* 16 bits here */
    n >>= 4;    /* 12 bits here */
    if (n & 1)  /* round to nearest */
      n = (n >> 1) + 1;
    else
      n = n >> 1;
    /* 11 bits here (rounded) */
    n <<= 2; /* 13 bits here (as in real chip) */
    tl_tab[x * 2 + 0] = n;
    tl_tab[x * 2 + 1] = -tl_tab[x * 2 + 0];

    for (i = 1; i < 13; i++) {
      tl_tab[x * 2 + 0 + i * 2 * TL_RES_LEN] = tl_tab[x * 2 + 0] >> i;
      tl_tab[x * 2 + 1 + i * 2 * TL_RES_LEN] =
          -tl_tab[x * 2 + 0 + i * 2 * TL_RES_LEN];
    }
#if 0
			logerror("tl %04i", x);
			for (i=0; i<13; i++)
				logerror(", [%02i] %4x", i*2, tl_tab[ x*2 /*+1*/ + i*2*TL_RES_LEN ]);
			logerror("\n");
		}
#endif
  }
  /*logerror("FM.C: TL_TAB_LEN = %i elements (%i bytes)\n",TL_TAB_LEN,
   * (int)sizeof(tl_tab));*/


  for (i = 0; i < SIN_LEN; i++) {
    /* non-standard sinus */
    m = sin(((i * 2) + 1) * PI / SIN_LEN); /* checked against the real chip */

    /* we never reach zero here due to ((i*2)+1) */

    if (m > 0.0)
      o = 8 * log(1.0 / m) / log(2); /* convert to 'decibels' */
    else
      o = 8 * log(-1.0 / m) / log(2); /* convert to 'decibels' */

    o = o / (ENV_STEP / 4);

    n = (int)(2.0 * o);
    if (n & 1) /* round to nearest */
      n = (n >> 1) + 1;
    else
      n = n >> 1;

    sin_tab[i] = n * 2 + (m >= 0.0 ? 0 : 1);
    /*logerror("FM.C: sin [%4i]= %4i (tl_tab value=%5i)\n", i,
     * sin_tab[i],tl_tab[sin_tab[i]]);*/
  }

  /*logerror("FM.C: ENV_QUIET= %08x\n",ENV_QUIET );*/

#ifdef SAVE_SAMPLE
  sample[0] = fopen("sampsum.pcm", "ab");
#endif
}

static int FMInitTable(void)
{
  return 1;
}


static void FMCloseTable(void)
{
#if 0
	if( tl_tab ) free( tl_tab );
	tl_tab = 0;
#endif
#ifdef SAVE_SAMPLE
  fclose(sample[0]);
#endif
  return;
}

/* OPN/OPM Mode  Register Write */
INLINE void FMSetMode(FM_ST *ST, int n, int v)
{
  /* b7 = CSM MODE */
  /* b6 = 3 slot mode */
  /* b5 = reset b */
  /* b4 = reset a */
  /* b3 = timer enable b */
  /* b2 = timer enable a */
  /* b1 = load b */
  /* b0 = load a */
  ST->mode = v;

  /* reset Timer b flag */
  if (v & 0x20)
    FM_STATUS_RESET(ST, 0x02);
  /* reset Timer a flag */
  if (v & 0x10)
    FM_STATUS_RESET(ST, 0x01);
  /* load b */
  if (v & 0x02) {
    if (ST->TBC == 0) {
      /* James Ponder 2001-09-30: Timer is not correct, adjusted by 12 */
      ST->TBC = (256 - ST->TB) << (4 + 12);
      /* External timer handler */
      if (ST->Timer_Handler)
        (ST->Timer_Handler)(n, 1, ST->TBC, ST->TimerBase);
    }
  } else if (ST->timermodel == FM_TIMER_INTERVAL) { /* stop interbval timer */
    if (ST->TBC != 0) {
      ST->TBC = 0;
      if (ST->Timer_Handler)
        (ST->Timer_Handler)(n, 1, 0, ST->TimerBase);
    }
  }
  /* load a */
  if (v & 0x01) {
    if (ST->TAC == 0) {
      /* James Ponder 2001-09-30: Timer is not correct, adjusted by 12 */
      ST->TAC = (1024 - ST->TA) << 12;
      /* External timer handler */
      if (ST->Timer_Handler)
        (ST->Timer_Handler)(n, 0, ST->TAC, ST->TimerBase);
    }
  } else if (ST->timermodel == FM_TIMER_INTERVAL) { /* stop interbval timer */
    if (ST->TAC != 0) {
      ST->TAC = 0;
      if (ST->Timer_Handler)
        (ST->Timer_Handler)(n, 0, 0, ST->TimerBase);
    }
  }
}

/* Timer A Overflow */
static void TimerAOver(FM_ST *ST)
{
  /* set status (if enabled) */
  if (ST->mode & 0x04)
    FM_STATUS_SET(ST, 0x01);
  /* clear or reload the counter */
  if (ST->timermodel == FM_TIMER_INTERVAL) {
    /* James Ponder 2001-09-30: Timer is not correct, adjusted by 12 */
    ST->TAC = (1024 - ST->TA) << 12;
    if (ST->Timer_Handler)
      (ST->Timer_Handler)(ST->index, 0, ST->TAC, ST->TimerBase);
  } else
    ST->TAC = 0;
}
/* Timer B Overflow */
static void TimerBOver(FM_ST *ST)
{
  /* set status (if enabled) */
  if (ST->mode & 0x08)
    FM_STATUS_SET(ST, 0x02);
  /* clear or reload the counter */
  if (ST->timermodel == FM_TIMER_INTERVAL) {
    /* James Ponder 2001-09-30: Timer is not correct, adjusted by 12 */
    ST->TBC = (256 - ST->TB) << (4 + 12);
    if (ST->Timer_Handler)
      (ST->Timer_Handler)(ST->index, 1, ST->TBC, ST->TimerBase);
  } else
    ST->TBC = 0;
}
/* CSM Key Controll */
static void CSMKeyControll(FM_CH *CH)
{
  /* all key off */
  /* FM_KEYOFF(CH,SLOT1); */
  /* FM_KEYOFF(CH,SLOT2); */
  /* FM_KEYOFF(CH,SLOT3); */
  /* FM_KEYOFF(CH,SLOT4); */
  /* total level latch */
  CH->SLOT[SLOT1].TLL = CH->SLOT[SLOT1].TL;
  CH->SLOT[SLOT2].TLL = CH->SLOT[SLOT2].TL;
  CH->SLOT[SLOT3].TLL = CH->SLOT[SLOT3].TL;
  CH->SLOT[SLOT4].TLL = CH->SLOT[SLOT4].TL;
  /* all key on */
  FM_KEYON(CH, SLOT1);
  FM_KEYON(CH, SLOT2);
  FM_KEYON(CH, SLOT3);
  FM_KEYON(CH, SLOT4);
}

#ifdef _STATE_H
#if 0
static void FM_channel_postload(FM_CH *CH,int num_ch)
{
	int slot , ch;

	for(ch=0;ch<num_ch;ch++,CH++)
	{
		/* slots */
		for(slot=0;slot<4;slot++)
		{
		}
	}
}
#endif
/* FM channel save , internal state only */
static void FMsave_state_channel(const char *name, int num, FM_CH *CH,
                                 int num_ch)
{
  int slot, ch;
  char state_name[20];
  const char slot_array[4] = {1, 3, 2, 4};

  for (ch = 0; ch < num_ch; ch++, CH++) {
    /* channel */
    sprintf(state_name, "%s.CH%d", name, ch);
    state_save_register_INT32(state_name, num, "feedback", CH->op1_out, 2);
    state_save_register_UINT32(state_name, num, "phasestep", &CH->fc, 1);
    /* slots */
    for (slot = 0; slot < 4; slot++) {
      FM_SLOT *SLOT = &CH->SLOT[slot];

      sprintf(state_name, "%s.CH%d.SLOT%d", name, ch, slot_array[slot]);
      state_save_register_UINT32(state_name, num, "phasecount", &SLOT->Cnt, 1);
      state_save_register_UINT8(state_name, num, "state", &SLOT->state, 1);
      state_save_register_INT32(state_name, num, "volume", &SLOT->volume, 1);
      state_save_register_UINT32(state_name, num, "totallevel", &SLOT->TLL, 1);
    }
  }
}

static void FMsave_state_st(const char *state_name, int num, FM_ST *ST)
{
#if FM_BUSY_FLAG_SUPPORT
  state_save_register_INT32(state_name, num, "BusyCount", &ST->BusyCount, 1);
#endif
  state_save_register_UINT8(state_name, num, "address", &ST->address, 1);
  state_save_register_UINT8(state_name, num, "IRQ", &ST->irq, 1);
  state_save_register_UINT8(state_name, num, "IRQ MASK", &ST->irqmask, 1);
  state_save_register_UINT8(state_name, num, "status", &ST->status, 1);
  state_save_register_UINT32(state_name, num, "mode", &ST->mode, 1);
  state_save_register_UINT8(state_name, num, "prescaler", &ST->prescaler_sel,
                            1);
  state_save_register_UINT8(state_name, num, "freq latch", &ST->fn_h, 1);
  state_save_register_int(state_name, num, "TIMER A", &ST->TA);
  state_save_register_int(state_name, num, "TIMER Acnt", &ST->TAC);
  state_save_register_UINT8(state_name, num, "TIMER B", &ST->TB, 1);
  state_save_register_int(state_name, num, "TIMER Bcnt", &ST->TBC);
}
#endif /* _STATE_H */

#if BUILD_OPN
/***********************************************************/
/* OPN unit                                                */
/***********************************************************/

/* OPN 3slot struct */
typedef struct opn_3slot {
  UINT32 fc[3];   /* fnum3,blk3  :calculated */
  UINT8 fn_h;     /* freq3 latch            */
  UINT8 kcode[3]; /* key code    :          */
} FM_3SLOT;

/* OPN/A/B common state */
typedef struct opn_f {
  UINT8 type;              /* chip type         */
  FM_ST ST;                /* general state     */
  FM_3SLOT SL3;            /* 3 slot mode state */
  FM_CH *P_CH;             /* pointer of CH     */
  unsigned int PAN[6 * 2]; /* fm channels output masks (0xffffffff = enable) */

  UINT32 FN_TABLE[2048]; /* fnumber -> increment counter */
  /* LFO */
  UINT32 LFOCnt;
  UINT32 LFOIncr;
  UINT32 lfo_amd; /* cached AM/PM modulation outputs (per chip; previously
                    file-scope statics shared across UpdateOne calls) */
  INT32 lfo_pmd;
  FM_WORK work; /* per-chip channel work buffers */
  UINT32 LFO_FREQ[8]; /* LFO FREQ table */
} FM_OPN;

/* OPN key frequency number -> key code follow table */
/* fnum higher 4bit -> keycode lower 2bit */
static const UINT8 OPN_FKTABLE[16] = {0, 0, 0, 0, 0, 0, 0, 1,
                                      2, 3, 3, 3, 3, 3, 3, 3};

// #define LFO_ENT 512
// #define LFO_SH (32-9)
// #define LFO_RATE 0x10000
// #define PMS_RATE 0x400

static int OPNInitTable(void)
{
  int i;

  /* LFO wave table */
  for (i = 0; i < LFO_ENT; i++) {
    OPN_LFO_wave[i] = i < LFO_ENT / 2
                          ? i * LFO_RATE / (LFO_ENT / 2)
                          : (LFO_ENT - i) * LFO_RATE / (LFO_ENT / 2);

    /*logerror("FM.C: OPN_LFO_wave[%4i]= %8x\n",i,OPN_LFO_wave[i]);*/
    /* 0, 0x0100, 0x0200, 0x0300 ... 0xff00, 0x10000, 0xff00..0x0100 */
  }

  init_tables();

  return FMInitTable();
}

/* ---------- prescaler set(and make time tables) ---------- */
static void OPNSetPres(FM_OPN *OPN, int pres, int TimerPres, int SSGpres)
{
  int i;

  /* frequency base */
#if 1
  OPN->ST.freqbase =
      (OPN->ST.rate) ? ((double)OPN->ST.clock / OPN->ST.rate) / pres : 0;
#else
  OPN->ST.rate = (double)OPN->ST.clock / pres;
  OPN->ST.freqbase = 1.0;
#endif

  /* Timer base time */
  OPN->ST.TimerBase = 1.0 / ((double)OPN->ST.clock / (double)TimerPres);
  /* SSG part  prescaler set */
  if (SSGpres)
    SSGClk(OPN->ST.index, OPN->ST.clock * 2 / SSGpres);
  /* make time tables */
  init_timetables(&OPN->ST, OPN_DTTABLE);
  /* calculate fnumber -> increment counter table */
  for (i = 0; i < 2048; i++) {
    /* freq table for octave 7 */
    /* opn phase increment counter = 20bit */
    OPN->FN_TABLE[i] =
        (UINT32)((double)i * 64 * OPN->ST.freqbase *
                 (1 << (FREQ_SH - 10))); /* -10 because chip works with 10.10
                                            fixed point, while we use 16.16 */
#if 0
		logerror("FM.C: FN_TABLE[%4i] = %08x (dec=%8i)\n",
				 i, OPN->FN_TABLE[i]>>6,OPN->FN_TABLE[i]>>6 );
#endif
  }

  /* LFO freq. table */
  {
    /* 3.98Hz,5.56Hz,6.02Hz,6.37Hz,6.88Hz,9.63Hz,48.1Hz,72.2Hz @ 8MHz */
#define FM_LF(Hz) ((double)LFO_ENT * (1 << LFO_SH) * (Hz) / (8000000.0 / 144))
    static const double freq_table[8] = {FM_LF(3.98), FM_LF(5.56), FM_LF(6.02),
                                         FM_LF(6.37), FM_LF(6.88), FM_LF(9.63),
                                         FM_LF(48.1), FM_LF(72.2)};
#undef FM_LF
    for (i = 0; i < 8; i++) {
      OPN->LFO_FREQ[i] = (UINT32)(freq_table[i] * OPN->ST.freqbase);
    }
  }

  /*	LOG(LOG_INF,("OPN %d set prescaler %d\n",OPN->ST.index,pres));*/
}

/* ---------- write a OPN mode register 0x20-0x2f ---------- */
static void OPNWriteMode(FM_OPN *OPN, int r, int v)
{
  UINT8 c;
  FM_CH *CH;

  switch (r) {
  case 0x21: /* Test */
    break;
  case 0x22: /* LFO FREQ (YM2608/YM2612) */
    if (OPN->type & TYPE_LFOPAN) {
      OPN->LFOIncr = (v & 0x08) ? OPN->LFO_FREQ[v & 7] : 0;
        }
    break;
  case 0x24: /* timer A High 8*/
    OPN->ST.TA = (OPN->ST.TA & 0x03) | (((int)v) << 2);
    break;
  case 0x25: /* timer A Low 2*/
    OPN->ST.TA = (OPN->ST.TA & 0x3fc) | (v & 3);
    break;
  case 0x26: /* timer B */
    OPN->ST.TB = v;
    break;
  case 0x27: /* mode , timer controll */
    FMSetMode(&(OPN->ST), OPN->ST.index, v);
    break;
  case 0x28: /* key on / off */
    c = v & 0x03;
    if (c == 3)
      break;
    if ((v & 0x04) && (OPN->type & TYPE_6CH))
      c += 3;
    CH = OPN->P_CH;
    CH = &CH[c];
    /* csm mode */
    /* if( c == 2 && (OPN->ST.mode & 0x80) ) break; */
    if (v & 0x10)
      FM_KEYON(CH, SLOT1);
    else
      FM_KEYOFF(CH, SLOT1);
    if (v & 0x20)
      FM_KEYON(CH, SLOT2);
    else
      FM_KEYOFF(CH, SLOT2);
    if (v & 0x40)
      FM_KEYON(CH, SLOT3);
    else
      FM_KEYOFF(CH, SLOT3);
    if (v & 0x80)
      FM_KEYON(CH, SLOT4);
    else
      FM_KEYOFF(CH, SLOT4);
    /*		LOG(LOG_INF,("OPN %d:%d : KEY %02X\n",n,c,v&0xf0));*/
    break;
  }
}

/* ---------- write a OPN register (0x30-0xff) ---------- */
static void OPNWriteReg(FM_OPN *OPN, int r, int v)
{
  UINT8 c;
  FM_CH *CH;
  FM_SLOT *SLOT;

  /* 0x30 - 0xff */
  if ((c = OPN_CHAN(r)) == 3)
    return; /* 0xX3,0xX7,0xXB,0xXF */
  if ((r >= 0x100) /* && (OPN->type & TYPE_6CH) */)
    c += 3;
  CH = OPN->P_CH;
  CH = &CH[c];

  SLOT = &(CH->SLOT[OPN_SLOT(r)]);
  switch (r & 0xf0) {
  case 0x30: /* DET , MUL */
    set_det_mul(&OPN->ST, CH, SLOT, v);
    break;
  case 0x40: /* TL */
    set_tl(CH, SLOT, v, (c == 2) && (OPN->ST.mode & 0x80));
    break;
  case 0x50: /* KS, AR */
    set_ar_ksr(CH, SLOT, v, OPN->ST.eg_tab);
    break;
  case 0x60: /*     DR */
    /* bit7 = AMS_ON ENABLE(YM2612) */
    set_dr(SLOT, v, OPN->ST.eg_tab);
    if (OPN->type & TYPE_LFOPAN) {
      SLOT->amon = (v & 0x80) ? ~0 : 0;
      SLOT->ams = CH->ams & SLOT->amon;
    }
    break;
  case 0x70: /*     SR */
    set_sr(SLOT, v, OPN->ST.eg_tab);
    break;
  case 0x80: /* SL, RR */
    set_sl_rr(SLOT, v, OPN->ST.eg_tab);
    break;
  case 0x90: /* SSG-EG */
#if !FM_SEG_SUPPORT
    if (v & 0x08)
      LOG(LOG_ERR,
          ("OPN %d,%d,%d :SSG-TYPE envelope selected (not supported )\n",
           OPN->ST.index, c, OPN_SLOT(r)));
#endif
    SLOT->SEG = v & 0x0f;
    break;
  case 0xa0:
    switch (OPN_SLOT(r)) {
    case 0: /* 0xa0-0xa2 : FNUM1 */
    {
      UINT32 fn = (((UINT32)((OPN->ST.fn_h) & 7)) << 8) + v;
      UINT8 blk = OPN->ST.fn_h >> 3;
      /* keyscale code */
      CH->kcode = (blk << 2) | OPN_FKTABLE[(fn >> 7)];
      /* phase increment counter */
      CH->fc = OPN->FN_TABLE[fn] >> (7 - blk);
      CH->SLOT[SLOT1].Incr = -1;
    } break;
    case 1: /* 0xa4-0xa6 : FNUM2,BLK */
      OPN->ST.fn_h = v & 0x3f;
      break;
    case 2: /* 0xa8-0xaa : 3CH FNUM1 */
      if (r < 0x100) {
        UINT32 fn = (((UINT32)(OPN->SL3.fn_h & 7)) << 8) + v;
        UINT8 blk = OPN->SL3.fn_h >> 3;
        /* keyscale code */
        OPN->SL3.kcode[c] = (blk << 2) | OPN_FKTABLE[(fn >> 7)];
        /* phase increment counter */
        OPN->SL3.fc[c] = OPN->FN_TABLE[fn] >> (7 - blk);
        (OPN->P_CH)[2].SLOT[SLOT1].Incr = -1;
      }
      break;
    case 3: /* 0xac-0xae : 3CH FNUM2,BLK */
      if (r < 0x100)
        OPN->SL3.fn_h = v & 0x3f;
      break;
    }
    break;
  case 0xb0:
    switch (OPN_SLOT(r)) {
    case 0: /* 0xb0-0xb2 : FB,ALGO */
    {
      int feedback = (v >> 3) & 7;
      CH->ALGO = v & 7;
      CH->FB = feedback ? feedback + 6 : 0;
      setup_connection(&OPN->work, CH, c);
    } break;
    case 1: /* 0xb4-0xb6 : L , R , AMS , PMS (YM2612/YM2610B/YM2610/YM2608) */
      if (OPN->type & TYPE_LFOPAN) {
        /* b0-2 PMS */
        /* 0,3.4,6.7,10,14,20,40,80(cent) */
        static const double pmd_table[8] = {0, 3.4, 6.7, 10, 14, 20, 40, 80};

        /* b4-5 AMS */
        /* 0, 1.4,     5.9,     11.8           (dB) */
        /* 0, 1.40625, 5.90625, 11.90625 (or 11.8125) */
        /* 0, 15,    , 63	  , 127      (or 126)   in internal representation */

        /* bit0,    bit1,   bit2,  bit3, bit4, bit5, bit6, bit7, bit8, bit9 */
        /* 1,       2,      4,     8,    16,   32,   64,   128,  256,  512
         * (internal representation value)*/
        /* 0.09375, 0.1875, 0.375, 0.75, 1.5,  3,    6,    12,   24,   48 (dB)*/
        static const int amd_table[4] = {
            (int)(((0.0 * 4) / 3) / ENV_STEP),
            (int)(((1.40625 * 4) / 3) / ENV_STEP),
            (int)(((5.90625 * 4) / 3) / ENV_STEP),
            (int)(((11.90625 * 4) / 3) / ENV_STEP)};
        /* amd_table simply becomes = { 0, 15, 63, 127 } */

        CH->pms = (INT32)((1.5 / 1200.0) * pmd_table[v & 7] * PMS_RATE);

        CH->ams = amd_table[(v >> 4) & 0x03];
        CH->SLOT[SLOT1].ams = CH->ams & CH->SLOT[SLOT1].amon;
        CH->SLOT[SLOT2].ams = CH->ams & CH->SLOT[SLOT2].amon;
        CH->SLOT[SLOT3].ams = CH->ams & CH->SLOT[SLOT3].amon;
        CH->SLOT[SLOT4].ams = CH->ams & CH->SLOT[SLOT4].amon;

        /* PAN :  b7 = L, b6 = R */
        OPN->PAN[c * 2] = (v & 0x80) ? ~0 : 0;
        OPN->PAN[c * 2 + 1] = (v & 0x40) ? ~0 : 0;

        /* LOG(LOG_INF,("OPN %d,%d : PAN %x
         * %x\n",n,c,OPN->PAN[c*2],OPN->PAN[c*2+1]));*/
      }
      break;
    }
    break;
  }
}

#endif /* BUILD_OPN */

#if BUILD_OPN_PRESCALER
/*
  prescaler circuit (best guess to verified chip behaviour)

               +--------------+  +-sel2-+
               |              +--|in20  |
         +---+ |  +-sel1-+       |      |
M-CLK -+-|1/2|-+--|in10  | +---+ |   out|--INT_CLOCK
       | +---+    |   out|-|1/3|-|in21  |
       +----------|in11  | +---+ +------+
                  +------+

reg.2d : sel2 = in21 (select sel2)
reg.2e : sel1 = in11 (select sel1)
reg.2f : sel1 = in10 , sel2 = in20 (clear selector)
reset  : sel1 = in11 , sel2 = in21 (clear both)

*/
void OPNPrescaler_w(FM_OPN *OPN, int addr, int pre_divider)
{
  static const int opn_pres[4] = {2 * 12, 2 * 12, 6 * 12, 3 * 12};
  static const int ssg_pres[4] = {1, 1, 4, 2};
  int sel;

  switch (addr) {
  case 0: /* when reset */
    OPN->ST.prescaler_sel = 2;
    break;
  case 1: /* when postload */
    break;
  case 0x2d: /* divider sel : select 1/1 for 1/3line    */
    OPN->ST.prescaler_sel |= 0x02;
    break;
  case 0x2e: /* divider sel , select 1/3line for output */
    OPN->ST.prescaler_sel |= 0x01;
    break;
  case 0x2f: /* divider sel , clear both selector to 1/2,1/2 */
    OPN->ST.prescaler_sel = 0;
    break;
  }
  sel = OPN->ST.prescaler_sel & 3;
  /* update prescaler */
  OPNSetPres(OPN, opn_pres[sel] * pre_divider, opn_pres[sel] * pre_divider,
             ssg_pres[sel] * pre_divider);
}
#endif /* BUILD_OPN_PRESCALER */



#if BUILD_YM2612
/*******************************************************************************/
/*		YM2612 local section                                                   */
/*******************************************************************************/
/* here's the virtual YM2612 */
typedef struct ym2612_f {
#ifdef _STATE_H
  UINT8 REGS[512]; /* registers         */
#endif
  FM_OPN OPN;   /* OPN state       */
  FM_CH CH[6];  /* channel state */
  int address1; /* address register1 */
  /* dac output (YM2612) */
  int dacen;
  INT32 dacout;
} Ym2612State;

namespace generator {

struct Ym2612::Impl {
  int num_chips = 0;
  std::unique_ptr<Ym2612State[]> chips;
};

Ym2612::Ym2612() : m_impl(std::make_unique<Impl>())
{
}

Ym2612::~Ym2612()
{
  shutdown();
}

/* ---------- update one of chip ----------- */
void Ym2612::update_one(int num, INT16 **buffer, int length)
{
  Ym2612State *F2612 = &(m_impl->chips[num]);
  FM_OPN *OPN = &(m_impl->chips[num].OPN);
  int i;
  FMSAMPLE *bufL, *bufR;
  INT32 dacout = F2612->dacout;

  /* set bufer */
  bufL = buffer[0];
  bufR = buffer[1];

  /* Per-chip state is reached directly now: the pointers and scalars the
   * cur_chip cache used to swap are locals below, and the persistent ones
   * (LFOCnt, lfo_amd/lfo_pmd) live in the chip struct. Loading them every
   * call is equivalent to the old cache-hit path (single chip in use);
   * storing them back keeps them persistent exactly as the statics were. */
  FM_ST *State = &OPN->ST;
  FM_WORK *W = &OPN->work;
  FM_CH *cch[6];
  UINT32 lfo_amd = OPN->lfo_amd;
  INT32 lfo_pmd = OPN->lfo_pmd;
  UINT32 LFOCnt = OPN->LFOCnt;
  UINT32 LFOIncr = OPN->LFOIncr;

  cch[0] = &F2612->CH[0];
  cch[1] = &F2612->CH[1];
  cch[2] = &F2612->CH[2];
  cch[3] = &F2612->CH[3];
  cch[4] = &F2612->CH[4];
  cch[5] = &F2612->CH[5];
  /* update frequency counter */
  OPN_CALC_FCOUNT(cch[0]);
  OPN_CALC_FCOUNT(cch[1]);
  if ((State->mode & 0xc0)) {
    /* 3SLOT MODE */
    if (cch[2]->SLOT[SLOT1].Incr == -1) {
      /* 3 slot mode */
      CALC_FCSLOT(&cch[2]->SLOT[SLOT1], OPN->SL3.fc[1], OPN->SL3.kcode[1]);
      CALC_FCSLOT(&cch[2]->SLOT[SLOT2], OPN->SL3.fc[2], OPN->SL3.kcode[2]);
      CALC_FCSLOT(&cch[2]->SLOT[SLOT3], OPN->SL3.fc[0], OPN->SL3.kcode[0]);
      CALC_FCSLOT(&cch[2]->SLOT[SLOT4], cch[2]->fc, cch[2]->kcode);
    }
  } else
    OPN_CALC_FCOUNT(cch[2]);
  OPN_CALC_FCOUNT(cch[3]);
  OPN_CALC_FCOUNT(cch[4]);
  OPN_CALC_FCOUNT(cch[5]);

  /* buffering */
  for (i = 0; i < length; i++) {
    /* clear outputs */
    W->out_fm[0] = 0;
    W->out_fm[1] = 0;
    W->out_fm[2] = 0;
    W->out_fm[3] = 0;
    W->out_fm[4] = 0;
    W->out_fm[5] = 0;

    /* calculate FM */
    FM_CALC_CH(W, cch[0], lfo_amd, lfo_pmd);
    FM_CALC_CH(W, cch[1], lfo_amd, lfo_pmd);
    FM_CALC_CH(W, cch[2], lfo_amd, lfo_pmd);
    FM_CALC_CH(W, cch[3], lfo_amd, lfo_pmd);
    FM_CALC_CH(W, cch[4], lfo_amd, lfo_pmd);
    if (F2612->dacen)
      *cch[5]->connect4 += dacout;
    else
      FM_CALC_CH(W, cch[5], lfo_amd, lfo_pmd);


    /* buffering */
    {
      int lt, rt;

      lt = ((W->out_fm[0] >> 0) & OPN->PAN[0]);
      rt = ((W->out_fm[0] >> 0) & OPN->PAN[1]);
      lt += ((W->out_fm[1] >> 0) & OPN->PAN[2]);
      rt += ((W->out_fm[1] >> 0) & OPN->PAN[3]);
      lt += ((W->out_fm[2] >> 0) & OPN->PAN[4]);
      rt += ((W->out_fm[2] >> 0) & OPN->PAN[5]);
      lt += ((W->out_fm[3] >> 0) & OPN->PAN[6]);
      rt += ((W->out_fm[3] >> 0) & OPN->PAN[7]);
      lt += ((W->out_fm[4] >> 0) & OPN->PAN[8]);
      rt += ((W->out_fm[4] >> 0) & OPN->PAN[9]);
      lt += ((W->out_fm[5] >> 0) & OPN->PAN[10]);
      rt += ((W->out_fm[5] >> 0) & OPN->PAN[11]);


      lt >>= FINAL_SH;
      rt >>= FINAL_SH;

      /* Apply YM2612 DAC ladder effect for authentic sound */
      lt = ym2612_dac_ladder(lt);
      rt = ym2612_dac_ladder(rt);

      Limit(lt, MAXOUT, MINOUT);
      Limit(rt, MAXOUT, MINOUT);

#ifdef SAVE_SAMPLE
      SAVE_ALL_CHANNELS
#endif

      /* buffering */
      bufL[i] = lt;
      bufR[i] = rt;
    }

    /* LFO update AFTER sample output calculation (Genesis Plus GX accuracy fix)
     * The LFO AM waveform is inverted: LFO_RATE - wave gives correct direction
     * This fixes audio bugs in Spider-Man & Venom, California Games, etc. */
    if (LFOIncr) {
      LFOCnt =
          opn_lfo_step(LFOCnt, LFOIncr, OPN_LFO_wave, 1, &lfo_amd, &lfo_pmd);
    }

    /* Update busy flag counter */
    FM_BUSY_UPDATE(State);

    /* timer A controll */
    INTERNAL_TIMER_A(State, cch[2])
  }
  INTERNAL_TIMER_B(State, length)

  OPN->LFOCnt = LFOCnt;
  OPN->lfo_amd = lfo_amd;
  OPN->lfo_pmd = lfo_pmd;
}

#ifdef _STATE_H
void Ym2612::postload()
{
  int num, r;

  /* Ensure the chip array is initialized before accessing it. */
  if (m_impl->chips == nullptr) {
    return;
  }

  for (num = 0; num < m_impl->num_chips; num++) {
    /* DAC data & port */
    /* James Ponder 2001-09-30 level setting of 5 found suitable */
    m_impl->chips[num].dacout = ((int)m_impl->chips[num].REGS[0x2a] - 0x80)
                                << 5; /* level unknown */
    /* James Ponder 2001-10-19 fix from 0x2d to 0x2b */
    m_impl->chips[num].dacen = m_impl->chips[num].REGS[0x2b] & 0x80;
    /* OPN registers */
    /* DT / MULTI , TL , KS / AR , AMON / DR , SR , SL / RR , SSG-EG */
    for (r = 0x30; r < 0x9e; r++)
      if ((r & 3) != 3) {
        OPNWriteReg(&m_impl->chips[num].OPN, r, m_impl->chips[num].REGS[r]);
        OPNWriteReg(&m_impl->chips[num].OPN, r | 0x100,
                    m_impl->chips[num].REGS[r | 0x100]);
      }
    /* FB / CONNECT , L / R / AMS / PMS */
    for (r = 0xb0; r < 0xb6; r++)
      if ((r & 3) != 3) {
        OPNWriteReg(&m_impl->chips[num].OPN, r, m_impl->chips[num].REGS[r]);
        OPNWriteReg(&m_impl->chips[num].OPN, r | 0x100,
                    m_impl->chips[num].REGS[r | 0x100]);
      }
  }
}

/* James Ponder: removed static */
void Ym2612::save_state()
{
  int num;
  const char statename[] = "YM2612";

  /* Ensure the chip array is initialized before accessing it. */
  if (m_impl->chips == nullptr) {
    return;
  }

  for (num = 0; num < m_impl->num_chips; num++) {
    state_save_register_UINT8(statename, num, "regs", m_impl->chips[num].REGS,
                              512);
    FMsave_state_st(statename, num, &m_impl->chips[num].OPN.ST);
    FMsave_state_channel(statename, num, m_impl->chips[num].CH, 6);
    /* 3slots */
    state_save_register_UINT32(statename, num, "slot3fc",
                               m_impl->chips[num].OPN.SL3.fc, 3);
    state_save_register_UINT8(statename, num, "slot3fh",
                              &m_impl->chips[num].OPN.SL3.fn_h, 1);
    state_save_register_UINT8(statename, num, "slot3kc",
                              m_impl->chips[num].OPN.SL3.kcode, 3);
    /* address register1 */
    state_save_register_int(statename, num, "address1",
                            &m_impl->chips[num].address1);
  }
  postload();
}
#endif /* _STATE_H */

/* -------------------------- YM2612 ---------------------------------- */
int Ym2612::init(int num, int clock, int rate, TimerHandler timer_handler,
                 IrqHandler irq_handler)
{
  int i;

  if (m_impl->chips)
    return (-1); /* duplicate init. */

  m_impl->num_chips = num;

  /* allocate extend state space */
  m_impl->chips.reset(new (std::nothrow) Ym2612State[m_impl->num_chips]);
  if (m_impl->chips == nullptr) {
    LOG(LOG_ERR, ("YM2612Init: Failed to allocate %lu bytes for %d chips\n",
                  (unsigned long)(sizeof(Ym2612State) * m_impl->num_chips),
                  m_impl->num_chips));
    return (-1);
  }
  /* clear */
  memset(m_impl->chips.get(), 0, sizeof(Ym2612State) * m_impl->num_chips);
  /* allocate total level table (128kb space) */
  if (!OPNInitTable()) {
    LOG(LOG_ERR, ("YM2612Init: Failed to allocate OPN tables\n"));
    m_impl->chips.reset();
    m_impl->num_chips = 0;
    return (-1);
  }

  for (i = 0; i < m_impl->num_chips; i++) {
    m_impl->chips[i].OPN.ST.index = i;
    m_impl->chips[i].OPN.type = TYPE_YM2612;
    m_impl->chips[i].OPN.P_CH = m_impl->chips[i].CH;
    m_impl->chips[i].OPN.ST.clock = clock;
    m_impl->chips[i].OPN.ST.rate = rate;
    m_impl->chips[i].OPN.ST.timermodel = FM_TIMER_INTERVAL;
    /* Extend handler */
    m_impl->chips[i].OPN.ST.Timer_Handler = timer_handler;
    m_impl->chips[i].OPN.ST.IRQ_Handler = irq_handler;
    reset_chip(i);
  }
  /* James Ponder - removed
#ifdef _STATE_H
  save_state();
#endif
  */
  return 0;
}

/* ---------- shut down emulator ----------- */
void Ym2612::shutdown()
{
  if (!m_impl->chips)
    return;

  FMCloseTable();
  m_impl->chips.reset();
  m_impl->num_chips = 0;
}

/* ---------- reset one of chip ---------- */
void Ym2612::reset_chip(int num)
{
  int i;
  Ym2612State *F2612 = &(m_impl->chips[num]);
  FM_OPN *OPN = &(m_impl->chips[num].OPN);

  OPNSetPres(OPN, 6 * 24, 6 * 24, 0);
  /* status clear */
  FM_IRQMASK_SET(&OPN->ST, 0x03);
  FM_BUSY_CLEAR(&OPN->ST);
  OPNWriteMode(OPN, 0x27, 0x30); /* mode 0 , timer reset */

  reset_channel(&OPN->ST, &F2612->CH[0], 6);
  for (i = 0xb6; i >= 0xb4; i--) {
    OPNWriteReg(OPN, i, 0xc0);
    OPNWriteReg(OPN, i | 0x100, 0xc0);
  }
  for (i = 0xb2; i >= 0x30; i--) {
    OPNWriteReg(OPN, i, 0);
    OPNWriteReg(OPN, i | 0x100, 0);
  }
  for (i = 0x26; i >= 0x20; i--)
    OPNWriteReg(OPN, i, 0);
  /* DAC mode clear */
  F2612->dacen = 0;
}

/* YM2612 write */
/* n = number  */
/* a = address */
/* v = value   */
int Ym2612::write(int n, int a, UINT8 v)
{
  Ym2612State *F2612 = &(m_impl->chips[n]);
  int addr;

  switch (a & 3) {
  case 0: /* address port 0 */
    F2612->OPN.ST.address = v & 0xff;
    break;
  case 1: /* data port 0    */
    addr = F2612->OPN.ST.address;
#ifdef _STATE_H
    F2612->REGS[addr] = v;
#endif
    switch (addr & 0xf0) {
    case 0x20: /* 0x20-0x2f Mode */
      switch (addr) {
      case 0x2a: /* DAC data (YM2612) */
        YM2612UpdateReq(n);
        /* James Ponder 2001-09-30 level setting of 5 found suitable */
        F2612->dacout = ((int)v - 0x80) << 5; /* level unknown */
        break;
      case 0x2b: /* DAC Sel  (YM2612) */
        /* b7 = dac enable */
        F2612->dacen = v & 0x80;
        break;
      default: /* OPN section */
        YM2612UpdateReq(n);
        /* write register */
        OPNWriteMode(&(F2612->OPN), addr, v);
      }
      break;
    default: /* 0x30-0xff OPN section */
      YM2612UpdateReq(n);
      /* write register */
      OPNWriteReg(&(F2612->OPN), addr, v);
    }
    break;
  case 2: /* address port 1 */
    F2612->address1 = v & 0xff;
    break;
  case 3: /* data port 1    */
    addr = F2612->address1 | 0x100;
#ifdef _STATE_H
    F2612->REGS[addr] = v;
#endif
    YM2612UpdateReq(n);
    OPNWriteReg(&(F2612->OPN), addr, v);
    break;
  }
  return F2612->OPN.ST.irq;
}
UINT8 Ym2612::read(int n, int a)
{
  Ym2612State *F2612 = &(m_impl->chips[n]);

  switch (a & 3) {
  case 0: /* status 0 */
    return FM_STATUS_FLAG(&F2612->OPN.ST);
  case 1:
  case 2:
  case 3:
    LOG(LOG_WAR, ("YM2612 #%d:A=%d read unmapped area\n"));
    return FM_STATUS_FLAG(&F2612->OPN.ST);
  }
  return 0;
}

int Ym2612::timer_over(int n, int c)
{
  Ym2612State *F2612 = &(m_impl->chips[n]);

  if (c) { /* Timer B */
    TimerBOver(&(F2612->OPN.ST));
  } else { /* Timer A */
    YM2612UpdateReq(n);
    /* timer update */
    TimerAOver(&(F2612->OPN.ST));
    /* CSM mode key,TL controll */
    if (F2612->OPN.ST.mode &
        0x80) { /* CSM mode total level latch and auto key on */
      CSMKeyControll(&(F2612->CH[2]));
    }
  }
  return F2612->OPN.ST.irq;
}

}  // namespace generator

#endif /* BUILD_YM2612 */
