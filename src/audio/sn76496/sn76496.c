/* This code comes from MAME 0.53 and according to changes log was written
   by Nicola Salmoria.  The MAME license says:

   VI. Reuse of Source Code
   --------------------------
   This chapter might not apply to specific portions of MAME (e.g. CPU
   emulators) which bear different copyright notices.
   The source code cannot be used in a commercial product without the written
   authorization of the authors. Use in non-commercial products is allowed, and
   indeed encouraged.  If you use portions of the MAME source code in your
   program, however, you must make the full source code freely available as
   well.
   Usage of the _information_ contained in the source code is free for any use.
   However, given the amount of time and energy it took to collect this
   information, if you find new information we would appreciate if you made it
   freely available as well.

*/

/***************************************************************************

  sn76496.c

  Routines to emulate the Texas Instruments SN76489 / SN76496 programmable
  tone /noise generator. Also known as (or at least compatible with) TMS9919.

  Noise emulation is not accurate due to lack of documentation. The noise
  generator uses a shift register with a XOR-feedback network, but the exact
  layout is unknown. It can be set for either period or white noise; again,
  the details are unknown.

***************************************************************************/

/* get uint16 definition */
#include "generator.h"

#include "sn76496.h"

#define MAX_OUTPUT 0x7fff
#define STEP 0x10000

/* Formulas for noise generator */
/* The SN76489/SN76496 uses a 16-bit Linear Feedback Shift Register (LFSR).
 * Output is bit 0 of the shift register.
 *
 * For the Sega Genesis/Megadrive (SN76489AN variant):
 * - White noise: tapped feedback from bits 0 and 3 (XOR)
 * - Periodic noise: single tap at bit 0 (creates 15-stage cycling pattern)
 *
 * The feedback is calculated, then the register shifts right, and the
 * feedback bit enters at bit 15.
 */

/* White noise feedback taps: bits 0 and 3 (Sega Genesis verified) */
#define FB_WNOISE_TAPS 0x0009

/* Periodic noise: single tap creates rotating pattern */
#define FB_PNOISE_TAPS 0x0001

/* Noise generator initial state - bit 15 set (standard for SN76489) */
#define NG_PRESET 0x8000

struct SN76496 sn[MAX_76496];

/* Helper: calculate parity of masked bits (for LFSR feedback) */
static inline int parity(unsigned int val)
{
  val ^= val >> 8;
  val ^= val >> 4;
  val ^= val >> 2;
  val ^= val >> 1;
  return val & 1;
}

void SN76496Write(int chip, int data)
{
  struct SN76496 *R = &sn[chip];

  /* should update buffer before getting here */

  if (data & 0x80) {
    int r = (data & 0x70) >> 4;
    int c = r / 2;

    R->LastRegister = r;
    R->Register[r] = (R->Register[r] & 0x3f0) | (data & 0x0f);
    switch (r) {
    case 0: /* tone 0 : frequency */
    case 2: /* tone 1 : frequency */
    case 4: /* tone 2 : frequency */
      /* Period 0 and 1 produce DC output (channel held high).
       * We use Period=0 to signal this special case in SN76496Update. */
      if (R->Register[r] <= 1) {
        R->Period[c] = 0; /* DC output */
        R->Output[c] = 1; /* held high */
      } else {
        R->Period[c] = R->UpdateStep * R->Register[r];
      }
      if (r == 4) {
        /* update noise shift frequency */
        if ((R->Register[6] & 0x03) == 0x03)
          R->Period[3] = R->Period[2] ? 2 * R->Period[2] : 0;
      }
      break;
    case 1: /* tone 0 : volume */
    case 3: /* tone 1 : volume */
    case 5: /* tone 2 : volume */
    case 7: /* noise  : volume */
      R->Volume[c] = R->VolTable[data & 0x0f];
      break;
    case 6: /* noise  : frequency, mode */
    {
      int n = R->Register[6];
      /* Store tap mask for feedback calculation */
      R->NoiseFB = (n & 4) ? FB_WNOISE_TAPS : FB_PNOISE_TAPS;
      n &= 3;
      /* N/512,N/1024,N/2048,Tone #3 output */
      R->Period[3] = (n == 3) ? (R->Period[2] ? 2 * R->Period[2] : 0)
                              : (R->UpdateStep << (5 + n));

      /* reset noise shifter */
      R->RNG = NG_PRESET;
      R->Output[3] = R->RNG & 1;
    } break;
    }
  } else {
    int r = R->LastRegister;
    int c = r / 2;

    switch (r) {
    case 0: /* tone 0 : frequency */
    case 2: /* tone 1 : frequency */
    case 4: /* tone 2 : frequency */
      R->Register[r] = (R->Register[r] & 0x0f) | ((data & 0x3f) << 4);
      /* Period 0 and 1 produce DC output (channel held high) */
      if (R->Register[r] <= 1) {
        R->Period[c] = 0; /* DC output */
        R->Output[c] = 1; /* held high */
      } else {
        R->Period[c] = R->UpdateStep * R->Register[r];
      }
      if (r == 4) {
        /* update noise shift frequency */
        if ((R->Register[6] & 0x03) == 0x03)
          R->Period[3] = R->Period[2] ? 2 * R->Period[2] : 0;
      }
      break;
    }
  }
}

void SN76496Update(int chip, uint16 *buffer, int length)
{
  int i;
  struct SN76496 *R = &sn[chip];

  /* If the volume is 0, increase the counter */
  for (i = 0; i < 4; i++) {
    if (R->Volume[i] == 0) {
      /* note that I do count += length, NOT count = length + 1. You might
         think it's the same since the volume is 0, but doing the latter
         could cause interferencies when the program is rapidly modulating
         the volume. */
      if (R->Count[i] <= length * STEP)
        R->Count[i] += length * STEP;
    }
  }

  while (length > 0) {
    int vol[4];
    unsigned int out;
    int left;

    /* vol[] keeps track of how long each square wave stays */
    /* in the 1 position during the sample period. */
    vol[0] = vol[1] = vol[2] = vol[3] = 0;

    for (i = 0; i < 3; i++) {
      /* Period 0 means DC output - channel held high */
      if (R->Period[i] == 0) {
        vol[i] = STEP; /* full high for entire sample */
        continue;
      }

      if (R->Output[i])
        vol[i] += R->Count[i];
      R->Count[i] -= STEP;
      /* Period[i] is the half period of the square wave. Here, in each */
      /* loop I add Period[i] twice, so that at the end of the loop the */
      /* square wave is in the same status (0 or 1) it was at the start. */
      /* vol[i] is also incremented by Period[i], since the wave has been 1 */
      /* exactly half of the time, regardless of the initial position. */
      /* If we exit the loop in the middle, Output[i] has to be inverted */
      /* and vol[i] incremented only if the exit status of the square */
      /* wave is 1. */
      while (R->Count[i] <= 0) {
        R->Count[i] += R->Period[i];
        if (R->Count[i] > 0) {
          R->Output[i] ^= 1;
          if (R->Output[i])
            vol[i] += R->Period[i];
          break;
        }
        R->Count[i] += R->Period[i];
        vol[i] += R->Period[i];
      }
      if (R->Output[i])
        vol[i] -= R->Count[i];
    }

    /* Noise channel processing */
    left = STEP;

    /* Period 0 for noise means DC output (when using tone 2 freq with period 0) */
    if (R->Period[3] == 0) {
      vol[3] = R->Output[3] ? STEP : 0;
    } else {
      do {
        int nextevent;

        if (R->Count[3] < left)
          nextevent = R->Count[3];
        else
          nextevent = left;
        if (R->Output[3])
          vol[3] += R->Count[3];
        R->Count[3] -= nextevent;
        if (R->Count[3] <= 0) {
          /* Correct LFSR shift with parity-based feedback.
           * Feedback bit = parity of (RNG AND tap_mask)
           * Then shift right and insert feedback at bit 15. */
          int feedback = parity(R->RNG & R->NoiseFB);
          R->RNG = (R->RNG >> 1) | (feedback << 15);
          R->Output[3] = R->RNG & 1;
          R->Count[3] += R->Period[3];
          if (R->Output[3])
            vol[3] += R->Period[3];
        }
        if (R->Output[3])
          vol[3] -= R->Count[3];
        left -= nextevent;
      } while (left > 0);
    }

    out = vol[0] * R->Volume[0] + vol[1] * R->Volume[1] +
          vol[2] * R->Volume[2] + vol[3] * R->Volume[3];
    if (out > MAX_OUTPUT * STEP)
      out = MAX_OUTPUT * STEP;
    *(buffer++) = out / STEP;
    length--;
  }
}

static void SN76496_set_clock(int chip, int clock)
{
  struct SN76496 *R = &sn[chip];

  /* the base clock for the tone generators is the chip clock divided by 16; */
  /* for the noise generator, it is clock / 256. */
  /* Here we calculate the number of steps which happen during one sample */
  /* at the given sample rate. No. of events = sample rate / (clock/16). */
  /* STEP is a multiplier used to turn the fraction into a fixed point */
  /* number. */
  R->UpdateStep = ((double)STEP * R->SampleRate * 16) / clock;
}

static void SN76496_set_gain(int chip, int gain)
{
  struct SN76496 *R = &sn[chip];
  int i;
  double out;

  /*
   * SN76489/SN76496 volume attenuation:
   * - 4-bit value (0-15)
   * - Each step is 2dB attenuation
   * - Volume 0 = full output (0dB attenuation)
   * - Volume 14 = -28dB
   * - Volume 15 = off (effectively infinite attenuation)
   *
   * We divide by 4 to leave headroom for mixing 4 channels.
   * The actual mixing levels are adjusted in gensound.c.
   */

  gain &= 0xff;
  /* Base output level (divided by 4 for 4-channel headroom) */
  out = MAX_OUTPUT / 4.0;

  /* Apply gain adjustment (0.2 dB per step) */
  while (gain-- > 0)
    out *= 1.023292992; /* = 10 ^ (0.2/20) */

  /* Build volume table with 2dB per step attenuation */
  for (i = 0; i < 15; i++) {
    /* Clamp to avoid overflow */
    if (out > MAX_OUTPUT / 4.0)
      R->VolTable[i] = MAX_OUTPUT / 4;
    else
      R->VolTable[i] = (int)(out + 0.5); /* round to nearest */
    out /= 1.258925412; /* = 10 ^ (2/20) = 2dB attenuation */
  }
  R->VolTable[15] = 0; /* Volume 15 = silence */
}

int SN76496Init(int chip, int clock, int gain, int sample_rate)
{
  int i;
  struct SN76496 *R = &sn[chip];

  R->SampleRate = sample_rate;
  SN76496_set_clock(chip, clock);

  /* Initialize all volumes to 0 (will use VolTable[0x0f] = 0 = silence) */
  for (i = 0; i < 4; i++)
    R->Volume[i] = 0;

  R->LastRegister = 0;
  for (i = 0; i < 8; i += 2) {
    R->Register[i] = 0;
    R->Register[i + 1] = 0x0f; /* volume = 0 (silence) */
  }

  /* Initialize tone channels: Register 0 means DC output (held high) */
  for (i = 0; i < 3; i++) {
    R->Output[i] = 1;  /* DC high */
    R->Period[i] = 0;  /* 0 = DC mode */
    R->Count[i] = 0;
  }

  /* Initialize noise channel */
  R->NoiseFB = FB_PNOISE_TAPS; /* Default to periodic noise */
  R->Period[3] = R->UpdateStep << 5; /* Default N/512 rate */
  R->Count[3] = R->Period[3];
  R->RNG = NG_PRESET;
  R->Output[3] = R->RNG & 1;

  SN76496_set_gain(chip, gain & 0xff);

  return 0;
}
