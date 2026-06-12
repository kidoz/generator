/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Audiophile-quality sound system with oversampling, float processing, and dithering */

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>

extern "C" {
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
}

/*** variables externed ***/

int sound_debug = 0;
int sound_feedback = 0;
unsigned int sound_minfields = 5;
unsigned int sound_maxfields = 10;
unsigned int sound_speed = SOUND_SAMPLERATE;
unsigned int sound_internal_rate = SOUND_INTERNAL_RATE;
unsigned int sound_oversampling = SOUND_OVERSAMPLING;
unsigned int sound_sampsperfield;
unsigned int sound_threshold;
uint8 sound_regs1[256];
uint8 sound_regs2[256];
uint8 sound_address1 = 0;
uint8 sound_address2 = 0;
uint8 sound_keys[8];
int sound_logsample = 0;
unsigned int sound_on = 1;
unsigned int sound_psg = 1;
unsigned int sound_fm = 1;
unsigned int sound_filter = 50;
unsigned int sound_hq_filter = SOUND_HQ_FILTER;
sound_dither_t sound_dither_mode = SOUND_DITHER_TRIANGULAR;

/* Audio buffers */
uint16 sound_soundbuf[2][SOUND_BUFFER_SAMPLES];
float sound_floatbuf[2][SOUND_BUFFER_SAMPLES * 4];

/*** forward references ***/

static void sound_process(void);
static void sound_writetolog(unsigned char c);

/*** file scoped variables ***/

static int sound_active = 0;
static uint8 *sound_logdata;
static unsigned int sound_logdata_size;
static unsigned int sound_logdata_p;
static unsigned int sound_fieldhassamples;

/* Internal oversampled buffers (for chip emulation at higher rate) */
static int16_t *sound_oversample_buf[2];
static size_t sound_oversample_buf_size;

/* Dithering state (for TPDF) */
static float dither_state_l = 0.0f;
static float dither_state_r = 0.0f;

#ifdef JFM
static t_jfm_ctx *sound_ctx;
#endif

/*
 * High-Quality Biquad Filter (float-based)
 *
 * Implements a cascaded biquad filter for audiophile-grade filtering.
 * Uses double precision for coefficient calculation, float for processing.
 */
typedef struct {
  float b0, b1, b2;  /* feedforward coefficients */
  float a1, a2;      /* feedback coefficients (a0 normalized to 1) */
  float z1, z2;      /* delay elements */
} biquad_float_t;

/* Initialize biquad for Butterworth low-pass */
static void biquad_init_lowpass(biquad_float_t *f, double sample_rate, double cutoff_hz)
{
  double omega = 2.0 * M_PI * cutoff_hz / sample_rate;
  double sin_omega = sin(omega);
  double cos_omega = cos(omega);
  double alpha = sin_omega / (2.0 * 0.7071067811865476); /* Q = 1/sqrt(2) for Butterworth */

  double a0 = 1.0 + alpha;
  f->b0 = (float)((1.0 - cos_omega) / 2.0 / a0);
  f->b1 = (float)((1.0 - cos_omega) / a0);
  f->b2 = (float)((1.0 - cos_omega) / 2.0 / a0);
  f->a1 = (float)((-2.0 * cos_omega) / a0);
  f->a2 = (float)((1.0 - alpha) / a0);
  f->z1 = 0.0f;
  f->z2 = 0.0f;
}

/* Initialize biquad for high-pass (DC blocking) */
static void biquad_init_highpass(biquad_float_t *f, double sample_rate, double cutoff_hz)
{
  double omega = 2.0 * M_PI * cutoff_hz / sample_rate;
  double sin_omega = sin(omega);
  double cos_omega = cos(omega);
  double alpha = sin_omega / (2.0 * 0.7071067811865476);

  double a0 = 1.0 + alpha;
  f->b0 = (float)((1.0 + cos_omega) / 2.0 / a0);
  f->b1 = (float)(-(1.0 + cos_omega) / a0);
  f->b2 = (float)((1.0 + cos_omega) / 2.0 / a0);
  f->a1 = (float)((-2.0 * cos_omega) / a0);
  f->a2 = (float)((1.0 - alpha) / a0);
  f->z1 = 0.0f;
  f->z2 = 0.0f;
}

/* Process sample through biquad (Direct Form II Transposed) */
static inline float biquad_process_float(biquad_float_t *f, float in)
{
  float out = f->b0 * in + f->z1;
  f->z1 = f->b1 * in - f->a1 * out + f->z2;
  f->z2 = f->b2 * in - f->a2 * out;
  return out;
}

/* Filter state for HQ filtering (cascaded stages) */
static biquad_float_t hpf_l, hpf_r;           /* DC blocking high-pass */
static biquad_float_t lpf1_l, lpf1_r;         /* Anti-aliasing low-pass stage 1 */
static biquad_float_t lpf2_l, lpf2_r;         /* Anti-aliasing low-pass stage 2 */
static biquad_float_t downsample_lpf_l, downsample_lpf_r; /* Decimation filter */
static int filters_initialized = 0;

/* Initialize all filters for current sample rate */
static void init_filters(void)
{
  double output_rate = (double)sound_speed;
  double internal_rate = (double)sound_internal_rate;

  /* DC blocking high-pass at 10 Hz (removes DC offset) */
  biquad_init_highpass(&hpf_l, internal_rate, 10.0);
  biquad_init_highpass(&hpf_r, internal_rate, 10.0);

  /* Nyquist frequency for output */
  double nyquist = output_rate / 2.0;

  /* Anti-aliasing low-pass: cutoff at 95% of Nyquist for gentle rolloff */
  double cutoff = nyquist * 0.95;

  /* Two cascaded 2nd-order Butterworth = 4th order (24 dB/octave) */
  biquad_init_lowpass(&lpf1_l, internal_rate, cutoff);
  biquad_init_lowpass(&lpf1_r, internal_rate, cutoff);
  biquad_init_lowpass(&lpf2_l, internal_rate, cutoff * 0.9);
  biquad_init_lowpass(&lpf2_r, internal_rate, cutoff * 0.9);

  /* Decimation filter: steep cutoff just below Nyquist */
  if (sound_oversampling > 1) {
    biquad_init_lowpass(&downsample_lpf_l, internal_rate, nyquist * 0.85);
    biquad_init_lowpass(&downsample_lpf_r, internal_rate, nyquist * 0.85);
  }

  filters_initialized = 1;

  LOG_VERBOSE("Audio filters initialized: output=%u Hz, internal=%u Hz, %ux oversampling",
               sound_speed, sound_internal_rate, sound_oversampling);
}

/* Simple PRNG for dithering (fast, good quality) */
static uint32_t dither_seed = 0x12345678;

static inline float dither_random(void)
{
  /* Xorshift32 */
  dither_seed ^= dither_seed << 13;
  dither_seed ^= dither_seed >> 17;
  dither_seed ^= dither_seed << 5;
  /* Convert to float in range [-1, 1] */
  return (float)((int32_t)dither_seed) / (float)0x7FFFFFFF;
}

/* Apply TPDF dithering (triangular probability density function) */
static inline int16_t apply_dither(float sample, float *state)
{
  float dithered;

  switch (sound_dither_mode) {
  case SOUND_DITHER_NONE:
    /* Simple truncation */
    dithered = sample;
    break;

  case SOUND_DITHER_RECTANGULAR:
    /* RPDF: single random value, +-0.5 LSB */
    dithered = sample + dither_random() * 0.5f;
    break;

  case SOUND_DITHER_TRIANGULAR:
  default:
    /* TPDF: sum of two random values for triangular distribution, +-1 LSB */
    {
      float r1 = dither_random();
      float r2 = *state;
      *state = dither_random();
      dithered = sample + (r1 + r2) * 0.5f;
    }
    break;
  }

  /* Clamp and convert to int16 */
  if (dithered > 32767.0f) dithered = 32767.0f;
  if (dithered < -32768.0f) dithered = -32768.0f;

  return (int16_t)dithered;
}

/*** sound_init - initialise this sub-unit ***/

int sound_init(void)
{
  int ret;

  /* Calculate timing parameters - guard against division by zero */
  unsigned int framerate = vdp_framerate ? vdp_framerate : 60;
  sound_sampsperfield = sound_speed / framerate;
  sound_threshold = sound_sampsperfield * sound_minfields;

  /* Allocate oversampling buffers if needed */
  if (sound_oversampling > 1) {
    size_t needed = (size_t)(sound_sampsperfield * sound_oversampling + 16);
    if (needed > sound_oversample_buf_size) {
      free(sound_oversample_buf[0]);
      free(sound_oversample_buf[1]);
      sound_oversample_buf[0] = (int16_t *)malloc(needed * sizeof(int16_t));
      sound_oversample_buf[1] = (int16_t *)malloc(needed * sizeof(int16_t));
      if (!sound_oversample_buf[0] || !sound_oversample_buf[1]) {
        LOG_CRITICAL("Failed to allocate oversampling buffers");
        return 1;
      }
      sound_oversample_buf_size = needed;
    }
  }

  ret = sound_start();
  if (ret)
    return ret;

  /* Initialize sound chips at internal (oversampled) rate */
#ifdef JFM
  if ((sound_ctx = jfm_init(0, 2612, vdp_clock / 7, sound_internal_rate, nullptr,
                            nullptr)) == nullptr) {
#else
  if (YM2612Init(1, vdp_clock / 7, sound_internal_rate, nullptr, nullptr)) {
#endif
    LOG_VERBOSE("YM2612 failed init");
    sound_stop();
    return 1;
  }
  if (SN76496Init(0, vdp_clock / 15, 0, sound_internal_rate)) {
    LOG_VERBOSE("SN76496 failed init");
    sound_stop();
#ifdef JFM
    jfm_final(sound_ctx);
#else
    YM2612Shutdown();
#endif
    return 1;
  }

  /* Initialize filters */
  init_filters();

  if (sound_logdata)
    free(sound_logdata);
  sound_logdata_size = 8192;
  sound_logdata = (uint8 *)malloc(sound_logdata_size);
  if (!sound_logdata)
    ui_err("out of memory");

  LOG_VERBOSE("Sound initialized: output=%u Hz, internal=%u Hz, %ux oversampling, HQ filter=%s",
               sound_speed, sound_internal_rate, sound_oversampling,
               sound_hq_filter ? "enabled" : "disabled");
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

  free(sound_oversample_buf[0]);
  free(sound_oversample_buf[1]);
  sound_oversample_buf[0] = nullptr;
  sound_oversample_buf[1] = nullptr;
  sound_oversample_buf_size = 0;
}

/*** sound_start - start sound ***/

int sound_start(void)
{
  int result;

  if (sound_active) {
    LOG_VERBOSE("Restarting sound (full reset)...");
    result = soundp_reset();
  } else {
    LOG_VERBOSE("Starting sound...");
    result = soundp_start();
  }

  if (result != 0) {
    LOG_VERBOSE("Failed to start sound hardware");
    return -1;
  }
  sound_active = 1;
  LOG_VERBOSE("Started sound.");
  return 0;
}

/*** sound_stop - stop sound ***/

void sound_stop(void)
{
  if (!sound_active)
    return;
  LOG_VERBOSE("Stopping sound...");
  soundp_stop();
  sound_active = 0;
  LOG_VERBOSE("Stopped sound.");
}

/*** sound_reset - reset sound sub-unit ***/

int sound_reset(void)
{
  LOG_VERBOSE("Resetting sound (full subsystem restart)...");

  if (sound_active) {
    soundp_stop();
    sound_active = 0;
  }
#ifdef JFM
  jfm_final(sound_ctx);
#else
  YM2612Shutdown();
#endif

  /* Calculate timing parameters - guard against division by zero */
  unsigned int framerate = vdp_framerate ? vdp_framerate : 60;
  sound_sampsperfield = sound_speed / framerate;
  sound_threshold = sound_sampsperfield * sound_minfields;

  if (soundp_reset() != 0) {
    LOG_VERBOSE("Failed to reset sound hardware");
    return 1;
  }
  sound_active = 1;

#ifdef JFM
  if ((sound_ctx = jfm_init(0, 2612, vdp_clock / 7, sound_internal_rate, nullptr,
                            nullptr)) == nullptr) {
#else
  if (YM2612Init(1, vdp_clock / 7, sound_internal_rate, nullptr, nullptr)) {
#endif
    LOG_VERBOSE("YM2612 failed init during reset");
    soundp_stop();
    sound_active = 0;
    return 1;
  }
  if (SN76496Init(0, vdp_clock / 15, 0, sound_internal_rate)) {
    LOG_VERBOSE("SN76496 failed init during reset");
    soundp_stop();
    sound_active = 0;
#ifdef JFM
    jfm_final(sound_ctx);
#else
    YM2612Shutdown();
#endif
    return 1;
  }

  /* Re-initialize filters */
  init_filters();

  LOG_VERBOSE("Sound reset complete.");
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
      sound_writetolog(0);
    } else {
      if (!sound_fieldhassamples) {
        o = sound_logdata + 3;
        for (p = sound_logdata + 3; p < (sound_logdata + sound_logdata_p); p++) {
          if ((*p & 0xF0) != 0x00 || *p == 4)
            ui_err("assertion of no samples failed");
          switch (*p) {
          case 0:
            ui_err("field marker in middle of field data");
          case 1:
          case 2:
            *o++ = *p++;
            *o++ = *p++;
            *o++ = *p;
            break;
          case 3:
            *o++ = *p++;
            *o++ = *p;
            break;
          case 5:
            break;
          default:
            ui_err("invalid data in sound log buffer");
          }
        }
        sound_logdata_p = o - sound_logdata;
        sound_logdata[1] = 0;
        sound_logdata[2] = 0;
      }
    }
    GEN_UI_CALL(g_ctx, musiclog, sound_logdata, sound_logdata_p);
  }

  if (!sound_on) {
    sound_feedback = 0;
    return;
  }

  if ((pending = soundp_samplesbuffered()) == -1)
    ui_err("Failed to read pending bytes in output sound buffer");
  if ((unsigned int)pending < sound_threshold)
    sound_feedback = -1;
  else
    sound_feedback = 0;

  if (sound_debug) {
    LOG_VERBOSE("End of field - %d samples buffered, threshold %d, feedback %d",
                 pending, sound_threshold, sound_feedback);
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
      sound_keys[7] = 0;
      sound_logsample = data;
    } else {
      if (gen_musiclog != musiclog_off) {
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
    if (sound_logsample == -1) {
      sound_writetolog(5);
    } else {
      if ((sound_logsample & 0xF0) == 0) {
        sound_writetolog(4);
      }
      sound_writetolog(sound_logsample);
      sound_logsample = -1;
      sound_fieldhassamples = 1;
    }
  }
  sound_process();
}

/*** sound_process - process sound with oversampling and HQ filtering ***/

static void sound_process(void)
{
  /* Calculate output sample range for this scanline */
  int s1 = (sound_sampsperfield * vdp_line) / vdp_totlines;
  int s2 = (sound_sampsperfield * (vdp_line + 1)) / vdp_totlines;
  unsigned int output_samples = s2 - s1;

  if (output_samples == 0 || s2 <= s1)
    return;

  /* Initialize filters if not done */
  if (!filters_initialized)
    init_filters();

  /* Calculate internal (oversampled) sample count */
  unsigned int internal_samples = output_samples * sound_oversampling;

  /* Temporary buffers for chip output */
  static int16_t fm_buf_l[SOUND_BUFFER_SAMPLES * 4];
  static int16_t fm_buf_r[SOUND_BUFFER_SAMPLES * 4];
  static uint16 psg_buf[SOUND_BUFFER_SAMPLES * 4];
  int16_t *fm_bufs[2] = { fm_buf_l, fm_buf_r };

  /* Generate samples at internal rate */
  if (sound_fm) {
#ifdef JFM
    jfm_update(sound_ctx, (void **)fm_bufs, internal_samples);
#else
    YM2612UpdateOne(0, fm_bufs, internal_samples);
#endif
  } else {
    memset(fm_buf_l, 0, internal_samples * sizeof(int16_t));
    memset(fm_buf_r, 0, internal_samples * sizeof(int16_t));
  }

  if (sound_psg) {
    SN76496Update(0, psg_buf, internal_samples);
  }

  /* Process through float pipeline */
  float *out_l = sound_floatbuf[0];
  float *out_r = sound_floatbuf[1];

  for (unsigned int i = 0; i < internal_samples; i++) {
    /* Convert to float and mix
     * FM: already signed 16-bit
     * PSG: unsigned 16-bit, convert to signed and scale
     * Mix ratio: FM * 0.875 + PSG * 0.375 (leaves headroom) */
    float fm_l = (float)fm_buf_l[i];
    float fm_r = (float)fm_buf_r[i];
    float psg = sound_psg ? ((float)psg_buf[i] - 16384.0f) : 0.0f;

    float mix_l = fm_l * 0.875f + psg * 0.375f;
    float mix_r = fm_r * 0.875f + psg * 0.375f;

    /* Apply HQ filtering if enabled */
    if (sound_hq_filter) {
      /* DC blocking high-pass */
      mix_l = biquad_process_float(&hpf_l, mix_l);
      mix_r = biquad_process_float(&hpf_r, mix_r);

      /* Anti-aliasing low-pass (cascaded for steeper rolloff) */
      mix_l = biquad_process_float(&lpf1_l, mix_l);
      mix_r = biquad_process_float(&lpf1_r, mix_r);
      mix_l = biquad_process_float(&lpf2_l, mix_l);
      mix_r = biquad_process_float(&lpf2_r, mix_r);
    }

    out_l[i] = mix_l;
    out_r[i] = mix_r;
  }

  /* Downsample if oversampling is active */
  uint16 *dest_l = sound_soundbuf[0] + s1;
  uint16 *dest_r = sound_soundbuf[1] + s1;

  if (sound_oversampling > 1) {
    /* Apply decimation filter and downsample */
    for (unsigned int i = 0; i < output_samples; i++) {
      float sum_l = 0.0f, sum_r = 0.0f;

      /* Average oversampled values with filtering */
      for (unsigned int j = 0; j < sound_oversampling; j++) {
        unsigned int idx = i * sound_oversampling + j;
        float sample_l = out_l[idx];
        float sample_r = out_r[idx];

        /* Apply decimation filter */
        if (sound_hq_filter) {
          sample_l = biquad_process_float(&downsample_lpf_l, sample_l);
          sample_r = biquad_process_float(&downsample_lpf_r, sample_r);
        }

        sum_l += sample_l;
        sum_r += sample_r;
      }

      /* Average and convert to int16 with dithering */
      float final_l = sum_l / (float)sound_oversampling;
      float final_r = sum_r / (float)sound_oversampling;

      dest_l[i] = (uint16)apply_dither(final_l, &dither_state_l);
      dest_r[i] = (uint16)apply_dither(final_r, &dither_state_r);
    }
  } else {
    /* No oversampling - direct conversion with dithering */
    for (unsigned int i = 0; i < output_samples; i++) {
      dest_l[i] = (uint16)apply_dither(out_l[i], &dither_state_l);
      dest_r[i] = (uint16)apply_dither(out_r[i], &dither_state_r);
    }
  }
}

/*** Audio quality control API ***/

int sound_set_quality(sound_quality_t quality)
{
  switch (quality) {
  case SOUND_QUALITY_LOW:
    sound_speed = 44100;
    sound_oversampling = 1;
    sound_hq_filter = 0;
    break;
  case SOUND_QUALITY_MEDIUM:
    sound_speed = 48000;
    sound_oversampling = 2;
    sound_hq_filter = 1;
    break;
  case SOUND_QUALITY_HIGH:
    sound_speed = 96000;
    sound_oversampling = 4;
    sound_hq_filter = 1;
    break;
  case SOUND_QUALITY_CUSTOM:
    /* Keep current settings */
    break;
  }

  sound_internal_rate = sound_speed * sound_oversampling;
  filters_initialized = 0; /* Force filter recalculation */

  return sound_reset();
}

int sound_set_sample_rate(unsigned int rate)
{
  if (rate != 44100 && rate != 48000 && rate != 96000) {
    LOG_VERBOSE("Invalid sample rate %u, must be 44100, 48000, or 96000", rate);
    return -1;
  }

  sound_speed = rate;
  sound_internal_rate = sound_speed * sound_oversampling;
  filters_initialized = 0;

  return sound_reset();
}

int sound_set_oversampling(unsigned int factor)
{
  if (factor != 1 && factor != 2 && factor != 4) {
    LOG_VERBOSE("Invalid oversampling factor %u, must be 1, 2, or 4", factor);
    return -1;
  }

  sound_oversampling = factor;
  sound_internal_rate = sound_speed * sound_oversampling;
  filters_initialized = 0;

  return sound_reset();
}

void sound_set_dither_mode(sound_dither_t mode)
{
  sound_dither_mode = mode;
}

/*** sound_writetolog - write to music log buffer ***/

static void sound_writetolog(unsigned char c)
{
  sound_logdata[sound_logdata_p++] = c;
  if (sound_logdata_p >= sound_logdata_size) {
    LOG_VERBOSE("sound log buffer limit increased");
    sound_logdata_size += 8192;
    sound_logdata = (uint8 *)realloc(sound_logdata, sound_logdata_size);
    if (!sound_logdata)
      ui_err("out of memory");
  }
}
