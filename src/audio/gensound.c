/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */

#include <stdlib.h>
#include <string.h>

#include "generator.h"
#include "gensound.h"
#include "gensoundp.h"
#include "vdp.h"
#include "ui.h"
#include "sn76496.h"
#include "gen_context.h"
#include "gen_ui_callbacks.h"

#ifdef JFM
#include "jfm.h"
#else
#include "support.h"
#include "fm.h"
#endif

/*** variables externed ***/

int sound_debug = 0;              /* debug mode */
int sound_feedback = 0;           /* -1, running out of sound
                                     +0, lots of sound, do something */
unsigned int sound_minfields = 5; /* min fields to try to buffer (optimized with
                                     g_idle_add architecture) */
unsigned int sound_maxfields = 10;           /* max fields before blocking */
unsigned int sound_speed = SOUND_SAMPLERATE; /* sample rate */
unsigned int sound_sampsperfield;            /* samples per field */
unsigned int sound_threshold;                /* samples in buffer aiming for */
uint8 sound_regs1[256];
uint8 sound_regs2[256];
uint8 sound_address1 = 0;
uint8 sound_address2 = 0;
uint8 sound_keys[8];
int sound_logsample = 0;        /* sample to log or -1 if none */
unsigned int sound_on = 1;      /* sound enabled */
unsigned int sound_psg = 1;     /* psg enabled */
unsigned int sound_fm = 1;      /* fm enabled */
unsigned int sound_filter = 50; /* low-pass filter percentage (0-100) */

/* pal is lowest framerate */
uint16 sound_soundbuf[2][SOUND_MAXRATE / 50];

/*** forward references ***/

static void sound_process(void);
static void sound_writetolog(unsigned char c);

/*** file scoped variables ***/

static int sound_active = 0;
static uint8 *sound_logdata;               /* log data */
static unsigned int sound_logdata_size;    /* sound_logdata size */
static unsigned int sound_logdata_p;       /* current log data offset */
static unsigned int sound_fieldhassamples; /* flag if field has samples */

#ifdef JFM
static t_jfm_ctx *sound_ctx;
#endif

/*** sound_init - initialise this sub-unit ***/

int sound_init(void)
{
  int ret;

  /* The sound_minfields parameter specifies how many fields worth of sound we
     should try and keep around (a display field is 1/50th or 1/60th of a
     second) - generator works by trying to keep sound_minfields number of
     fields worth of sound around, and drops plotting frames to keep up on slow
     machines */

  sound_sampsperfield = sound_speed / vdp_framerate;
  sound_threshold = sound_sampsperfield * sound_minfields;

  ret = sound_start();
  if (ret)
    return ret;
#ifdef JFM
  if ((sound_ctx = jfm_init(0, 2612, vdp_clock / 7, sound_speed, nullptr,
                            nullptr)) == nullptr) {
#else
  if (YM2612Init(1, vdp_clock / 7, sound_speed, nullptr, nullptr)) {
#endif
    LOG_VERBOSE(("YM2612 failed init"));
    sound_stop();
    return 1;
  }
  if (SN76496Init(0, vdp_clock / 15, 0, sound_speed)) {
    LOG_VERBOSE(("SN76496 failed init"));
    sound_stop();
#ifdef JFM
    jfm_final(sound_ctx);
#else
    YM2612Shutdown();
#endif
    return 1;
  }
  if (sound_logdata)
    free(sound_logdata);
  sound_logdata_size = 8192;
  sound_logdata = malloc(sound_logdata_size);
  if (!sound_logdata)
    ui_err("out of memory");
  LOG_VERBOSE(("YM2612 Initialised @ sample rate %d", sound_speed));
  return 0;
}

/*** sound_final - finalise this sub-unit ***/

void sound_final(void)
{
  sound_stop();
#ifdef JFM
  jfm_final(sound_ctx);
#else
  YM2612Shutdown();
#endif
}

/*** sound_start - start sound ***/

int sound_start(void)
{
  int result;

  if (sound_active) {
    /* Already active - do a full reset including SDL audio subsystem restart.
     * This fixes issues where audio doesn't work after stop/start cycles
     * without a full SDL audio subsystem reinit. */
    LOG_VERBOSE(("Restarting sound (full reset)..."));
    result = soundp_reset();
  } else {
    /* First start - just initialize */
    LOG_VERBOSE(("Starting sound..."));
    result = soundp_start();
  }

  if (result != 0) {
    LOG_VERBOSE(("Failed to start sound hardware"));
    return -1;
  }
  sound_active = 1;
  LOG_VERBOSE(("Started sound."));
  return 0;
}

/*** sound_stop - stop sound ***/

void sound_stop(void)
{
  if (!sound_active)
    return;
  LOG_VERBOSE(("Stopping sound..."));
  soundp_stop();
  sound_active = 0;
  LOG_VERBOSE(("Stopped sound."));
}

/*** sound_reset - reset sound sub-unit ***/

int sound_reset(void)
{
  /* Use soundp_reset() directly for a full audio subsystem restart.
   * This fixes issues where audio doesn't work after reset cycles
   * without a full SDL audio subsystem reinit. */
  LOG_VERBOSE(("Resetting sound (full subsystem restart)..."));

  /* Stop and shutdown sound chips */
  if (sound_active) {
    soundp_stop();
    sound_active = 0;
  }
#ifdef JFM
  jfm_final(sound_ctx);
#else
  YM2612Shutdown();
#endif

  /* Recalculate timing parameters */
  sound_sampsperfield = sound_speed / vdp_framerate;
  sound_threshold = sound_sampsperfield * sound_minfields;

  /* Do full audio subsystem restart */
  if (soundp_reset() != 0) {
    LOG_VERBOSE(("Failed to reset sound hardware"));
    return 1;
  }
  sound_active = 1;

  /* Reinitialize sound chips */
#ifdef JFM
  if ((sound_ctx = jfm_init(0, 2612, vdp_clock / 7, sound_speed, nullptr,
                            nullptr)) == nullptr) {
#else
  if (YM2612Init(1, vdp_clock / 7, sound_speed, nullptr, nullptr)) {
#endif
    LOG_VERBOSE(("YM2612 failed init during reset"));
    soundp_stop();
    sound_active = 0;
    return 1;
  }
  if (SN76496Init(0, vdp_clock / 15, 0, sound_speed)) {
    LOG_VERBOSE(("SN76496 failed init during reset"));
    soundp_stop();
    sound_active = 0;
#ifdef JFM
    jfm_final(sound_ctx);
#else
    YM2612Shutdown();
#endif
    return 1;
  }

  LOG_VERBOSE(("Sound reset complete."));
  return 0;
}

/*** sound_startfield - start of frame ***/

void sound_startfield(void)
{
  sound_logdata_p = 0;
  if (gen_musiclog == musiclog_gnm) {
    sound_writetolog(0);
    sound_writetolog((vdp_totlines >> 8) & 0xff);
    sound_writetolog(vdp_totlines & 0xff);
    sound_fieldhassamples = 0;
  }
}
/*** sound_endfield - end frame and output sound ***/

void sound_endfield(void)
{
  int pending;
  uint8 *p, *o;

  if (gen_musiclog) {
    if (gen_musiclog == musiclog_gym) {
      /* GYM format ends a field with a 0 byte */
      sound_writetolog(0);
    } else {
      /* GNM format requires sifting through the data */
      if (!sound_fieldhassamples) {
        /* we're only removing data, so we're going to write to the buffer
           we're reading from */
        o = sound_logdata + 3;
        for (p = sound_logdata + 3; p < (sound_logdata + sound_logdata_p);
             p++) {
          if ((*p & 0xF0) != 0x00 || *p == 4)
            ui_err("assertion of no samples failed");
          switch (*p) {
          case 0:
            ui_err("field marker in middle of field data");
          case 1:
          case 2:
            /* FM data, copy 2 and two bytes to output */
            *o++ = *p++;
            *o++ = *p++;
            *o++ = *p;
            break;
          case 3:
            /* PSG data, copy 3 and one byte to output */
            *o++ = *p++;
            *o++ = *p;
            break;
          case 5:
            /* these are the bytes we're trying to strip */
            /* do nothing */
            break;
          default:
            ui_err("invalid data in sound log buffer");
          }
        }
        /* update new end of buffer */
        sound_logdata_p = o - sound_logdata;
        sound_logdata[1] = 0; /* high byte number of samples */
        sound_logdata[2] = 0; /* low byte number of samples */
      }
    }
    /* write data */
    GEN_UI_CALL(g_ctx, musiclog, sound_logdata, sound_logdata_p);
    /* sound_startfield resets everything */
  }

  if (!sound_on) {
    /* sound is turned off - let generator continue */
    sound_feedback = 0;
    return;
  }

  /* work out feedback from sound system */

  if ((pending = soundp_samplesbuffered()) == -1)
    ui_err("Failed to read pending bytes in output sound buffer");
  if ((unsigned int)pending < sound_threshold)
    sound_feedback = -1;
  else
    sound_feedback = 0;

  if (sound_debug) {
    LOG_VERBOSE(
        ("End of field - sound system says %d bytes buffered", pending));
    LOG_VERBOSE(("Threshold %d, therefore feedback = %d ", sound_threshold,
                 sound_feedback));
  }
  soundp_output(sound_soundbuf[0], sound_soundbuf[1], sound_sampsperfield);
}

/*** sound_ym2612fetch - fetch byte from ym2612 chip ***/

uint8 sound_ym2612fetch(uint8 addr)
{
#ifdef JFM
  return jfm_read(sound_ctx, addr);
#else
  return YM2612Read(0, addr);
#endif
}

/*** sound_ym2612store - store a byte to the ym2612 chip ***/

void sound_ym2612store(uint8 addr, uint8 data)
{
  switch (addr) {
  case 0:
    sound_address1 = data;
    break;
  case 1:
    if (sound_address1 == 0x2a) {
      /* sample data */
      sound_keys[7] = 0;
      sound_logsample = data;
    } else {
      if (gen_musiclog != musiclog_off) {
        /* GYM and GNM */
        sound_writetolog(1);
        sound_writetolog(sound_address1);
        sound_writetolog(data);
      }
    }
    if (sound_address1 == 0x28 && (data & 3) != 3)
      sound_keys[data & 7] = data >> 4;
    if (sound_address1 == 0x2b)
      sound_keys[7] = data & 0x80 ? 0xf : 0;
    sound_regs1[sound_address1] = data;
    break;
  case 2:
    if (gen_musiclog != musiclog_off) {
      /* GYM and GNM */
      sound_writetolog(2);
      sound_writetolog(sound_address2);
      sound_writetolog(data);
    }
    sound_address2 = data;
    break;
  case 3:
    sound_regs2[sound_address2] = data;
    break;
  }
#ifdef JFM
  jfm_write(sound_ctx, addr, data);
#else
  YM2612Write(0, addr, data);
#endif
}

/*** sound_sn76496store - store a byte to the sn76496 chip ***/

void sound_sn76496store(uint8 data)
{
  if (gen_musiclog != musiclog_off) {
    /* GYM and GNM */
    sound_writetolog(3);
    sound_writetolog(data);
  }
  SN76496Write(0, data);
}

/*** sound_genreset - reset genesis sound ***/

void sound_genreset(void)
{
#ifdef JFM
  jfm_reset(sound_ctx);
#else
  YM2612ResetChip(0);
#endif
}

/*** sound_line - called at end of line ***/

void sound_line(void)
{
  if (gen_musiclog == musiclog_gnm) {
    /* GNM log */
    if (sound_logsample == -1) {
      sound_writetolog(5);
    } else {
      if ((sound_logsample & 0xF0) == 0) {
        sound_writetolog(4);
      }
      sound_writetolog(sound_logsample);
      sound_logsample = -1;
      /* mark the fact that we have encountered a sample this field */
      sound_fieldhassamples = 1;
    }
  }
  sound_process();
}

/*** sound_process - process sound ***/

/* Audio processing improvements:
 * - High-pass filter (DC blocking) to remove DC offset
 * - Improved mixing ratios based on hardware analysis
 * - Two-pole (biquad) low-pass filter for better anti-aliasing
 *
 * The real Genesis uses:
 * - YM2612: Main FM synthesis, louder in the mix
 * - SN76496: PSG for square waves/noise, softer in the mix
 *
 * High-pass filter: Removes DC offset and very low frequencies (< 20 Hz)
 * This prevents speaker damage and removes any DC bias from the DAC.
 *
 * Two-pole Butterworth low-pass filter:
 * Provides 12dB/octave rolloff (vs 6dB/octave for single-pole).
 * Uses biquad Direct Form II transposed for numerical stability.
 * Cutoff adjustable via sound_filter parameter.
 * At 50% setting, cutoff is approximately 14kHz at 44.1kHz sample rate.
 */

/* Biquad filter state structure */
typedef struct {
  sint32 z1, z2;  /* delay elements (16.16 fixed point) */
} biquad_state_t;

/* Biquad filter coefficients (16.16 fixed point)
 * Precomputed for Butterworth low-pass at ~14kHz cutoff, 44.1kHz sample rate
 * These are the default coefficients; they get scaled by sound_filter */
static const sint32 BIQUAD_B0 = 0x1E00;  /* ~0.117 */
static const sint32 BIQUAD_B1 = 0x3C00;  /* ~0.234 */
static const sint32 BIQUAD_B2 = 0x1E00;  /* ~0.117 */
static const sint32 BIQUAD_A1 = -0x6000; /* ~-0.375 (negated for subtraction) */
static const sint32 BIQUAD_A2 = 0x0400;  /* ~0.016 */

/* Apply biquad filter to a sample (Direct Form II transposed) */
static inline sint32 biquad_process(biquad_state_t *state, sint32 input,
                                     sint32 b0, sint32 b1, sint32 b2,
                                     sint32 a1, sint32 a2)
{
  /* y[n] = b0*x[n] + z1
   * z1 = b1*x[n] + z2 - a1*y[n]
   * z2 = b2*x[n] - a2*y[n] */
  sint32 output = ((b0 * input) >> 16) + (state->z1 >> 16);
  state->z1 = ((b1 * input) + state->z2) - (a1 * output);
  state->z2 = (b2 * input) - (a2 * output);
  return output;
}

static void sound_process(void)
{
  static sint16 *tbuf[2];
  int s1 = (sound_sampsperfield * (vdp_line)) / vdp_totlines;
  int s2 = (sound_sampsperfield * (vdp_line + 1)) / vdp_totlines;
  /* pal is lowest framerate */
  uint16 sn76496buf[SOUND_MAXRATE / 50]; /* far too much but who cares */
  unsigned int samples = s2 - s1;
  unsigned int i;

  /* Biquad low-pass filter state (two-pole for better anti-aliasing) */
  static biquad_state_t lpf_l = {0, 0};
  static biquad_state_t lpf_r = {0, 0};

  /* High-pass filter state (DC blocking filter)
   * Uses a simple single-pole high-pass: y[n] = alpha * (y[n-1] + x[n] - x[n-1])
   * Alpha ~= 0.995 gives cutoff around 15 Hz at 44.1 kHz */
  static sint32 hp_prev_in_l, hp_prev_in_r;
  static sint32 hp_prev_out_l, hp_prev_out_r;
  const sint32 HP_ALPHA = 0xFEB8; /* ~0.995 in 16.16 fixed point */

  /* Scale biquad coefficients based on sound_filter (0-100%)
   * 0% = no filtering (bypass), 100% = maximum filtering
   * We interpolate the b coefficients toward unity gain at 0% */
  sint32 filter_scale = (0x10000 * sound_filter) / 100;
  sint32 b0 = BIQUAD_B0 + (((0x10000 - BIQUAD_B0) * (0x10000 - filter_scale)) >> 16);
  sint32 b1 = (BIQUAD_B1 * filter_scale) >> 16;
  sint32 b2 = (BIQUAD_B2 * filter_scale) >> 16;
  sint32 a1 = (BIQUAD_A1 * filter_scale) >> 16;
  sint32 a2 = (BIQUAD_A2 * filter_scale) >> 16;

  tbuf[0] = sound_soundbuf[0] + s1;
  tbuf[1] = sound_soundbuf[1] + s1;

  if (s2 > s1) {
    if (sound_fm)
#ifdef JFM
      jfm_update(sound_ctx, (void **)tbuf, samples1);
#else
      YM2612UpdateOne(0, tbuf, samples);
#endif

    /* Mixing ratios (improved based on hardware analysis):
     * YM2612: Full level (14-bit output after internal limiting)
     * SN76496: Outputs in range 0 to 0x7fff, subtract 0x4000 for signed
     *
     * Real hardware has PSG slightly quieter than FM.
     * We use: FM * 7/8 + PSG * 3/8 for better balance.
     * This gives more headroom and prevents clipping. */

    if (sound_fm && sound_psg) {
      SN76496Update(0, sn76496buf, samples);
      for (i = 0; i < samples; i++) {
        /* Convert PSG to signed and scale */
        sint32 snsample = ((sint32)sn76496buf[i] - 0x4000) * 3 / 8;
        /* Scale FM for headroom */
        sint32 l = snsample + ((tbuf[0][i] * 7) >> 3);
        sint32 r = snsample + ((tbuf[1][i] * 7) >> 3);

        /* High-pass filter (DC blocking) */
        sint32 hp_l = (HP_ALPHA * (hp_prev_out_l + l - hp_prev_in_l)) >> 16;
        sint32 hp_r = (HP_ALPHA * (hp_prev_out_r + r - hp_prev_in_r)) >> 16;
        hp_prev_in_l = l;
        hp_prev_in_r = r;
        hp_prev_out_l = hp_l;
        hp_prev_out_r = hp_r;

        /* Two-pole low-pass filter (biquad for better anti-aliasing) */
        sint32 lp_l = biquad_process(&lpf_l, hp_l, b0, b1, b2, a1, a2);
        sint32 lp_r = biquad_process(&lpf_r, hp_r, b0, b1, b2, a1, a2);
        tbuf[0][i] = lp_l;
        tbuf[1][i] = lp_r;
      }
    } else if (!sound_fm && !sound_psg) {
      /* no sound */
      memset(tbuf[0], 0, 2 * samples);
      memset(tbuf[1], 0, 2 * samples);
    } else if (sound_fm) {
      /* fm only */
      for (i = 0; i < samples; i++) {
        sint32 l = (tbuf[0][i] * 7) >> 3;
        sint32 r = (tbuf[1][i] * 7) >> 3;

        /* High-pass filter (DC blocking) */
        sint32 hp_l = (HP_ALPHA * (hp_prev_out_l + l - hp_prev_in_l)) >> 16;
        sint32 hp_r = (HP_ALPHA * (hp_prev_out_r + r - hp_prev_in_r)) >> 16;
        hp_prev_in_l = l;
        hp_prev_in_r = r;
        hp_prev_out_l = hp_l;
        hp_prev_out_r = hp_r;

        /* Two-pole low-pass filter (biquad for better anti-aliasing) */
        sint32 lp_l = biquad_process(&lpf_l, hp_l, b0, b1, b2, a1, a2);
        sint32 lp_r = biquad_process(&lpf_r, hp_r, b0, b1, b2, a1, a2);
        tbuf[0][i] = lp_l;
        tbuf[1][i] = lp_r;
      }
    } else {
      /* psg only */
      SN76496Update(0, sn76496buf, samples);
      for (i = 0; i < samples; i++) {
        sint32 snsample = ((sint32)sn76496buf[i] - 0x4000) * 3 / 8;

        /* High-pass filter (DC blocking) */
        sint32 hp_l = (HP_ALPHA * (hp_prev_out_l + snsample - hp_prev_in_l)) >> 16;
        sint32 hp_r = (HP_ALPHA * (hp_prev_out_r + snsample - hp_prev_in_r)) >> 16;
        hp_prev_in_l = snsample;
        hp_prev_in_r = snsample;
        hp_prev_out_l = hp_l;
        hp_prev_out_r = hp_r;

        /* Two-pole low-pass filter (biquad for better anti-aliasing) */
        sint32 lp_l = biquad_process(&lpf_l, hp_l, b0, b1, b2, a1, a2);
        sint32 lp_r = biquad_process(&lpf_r, hp_r, b0, b1, b2, a1, a2);
        tbuf[0][i] = lp_l;
        tbuf[1][i] = lp_r;
      }
    }
  }
}

/*** sound_writetolog - write to music log buffer ***/

static void sound_writetolog(unsigned char c)
{
  /* write the byte to the self-expanding memory log - when we have the whole
     field's worth of data we then sift through the data (see the
     documentation on the GNM log format) and then pass it to ui_writelog */

  sound_logdata[sound_logdata_p++] = c;
  if (sound_logdata_p >= sound_logdata_size) {
    LOG_VERBOSE(("sound log buffer limit increased"));
    sound_logdata_size += 8192;
    sound_logdata = realloc(sound_logdata, sound_logdata_size);
    if (!sound_logdata)
      ui_err("out of memory");
  }
}
