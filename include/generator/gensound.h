#ifndef GENERATOR_GENSOUND_H
#define GENERATOR_GENSOUND_H

#ifndef JFM
#include "support.h"
#include "fm.h"
#endif

/*
 * Audio Quality Configuration
 *
 * SOUND_MAXRATE: Maximum supported sample rate (for buffer sizing)
 * SOUND_SAMPLERATE: Default sample rate (can be overridden at runtime)
 * SOUND_OVERSAMPLING: Internal oversampling factor (1, 2, or 4)
 *
 * Higher sample rates (48000/96000 Hz) provide:
 * - Better high-frequency response
 * - Reduced aliasing artifacts
 * - Compatibility with modern audio hardware
 *
 * Oversampling provides:
 * - Anti-aliasing for FM synthesis harmonics
 * - Cleaner sound without audible artifacts
 * - More accurate emulation of analog filtering
 */

/* Maximum supported sample rate (96 kHz for high-resolution audio) */
#define SOUND_MAXRATE 96000

/* Default sample rate - can be changed via build option or runtime */
#ifndef SOUND_SAMPLERATE
#define SOUND_SAMPLERATE 48000
#endif

/* Internal oversampling factor (1x, 2x, or 4x) */
#ifndef SOUND_OVERSAMPLING
#define SOUND_OVERSAMPLING 2
#endif

/* High-quality filter enable */
#ifndef SOUND_HQ_FILTER
#define SOUND_HQ_FILTER 1
#endif

/* Verify audio configuration at compile time */
static_assert(SOUND_SAMPLERATE <= SOUND_MAXRATE,
              "SOUND_SAMPLERATE must not exceed SOUND_MAXRATE");
static_assert(SOUND_SAMPLERATE >= 44100,
              "SOUND_SAMPLERATE must be at least 44100 Hz");
static_assert(SOUND_OVERSAMPLING == 1 || SOUND_OVERSAMPLING == 2 ||
                  SOUND_OVERSAMPLING == 4,
              "SOUND_OVERSAMPLING must be 1, 2, or 4");

/* Internal rate for chip emulation (with oversampling) */
#define SOUND_INTERNAL_RATE (SOUND_SAMPLERATE * SOUND_OVERSAMPLING)

/* Buffer size calculation: max rate / min framerate (50 Hz PAL) */
#define SOUND_BUFFER_SAMPLES (SOUND_MAXRATE / 50)

/* Audio quality settings */
typedef enum {
  SOUND_QUALITY_LOW = 0,    /* 44100 Hz, no oversampling */
  SOUND_QUALITY_MEDIUM = 1, /* 48000 Hz, 2x oversampling */
  SOUND_QUALITY_HIGH = 2,   /* 96000 Hz, 4x oversampling */
  SOUND_QUALITY_CUSTOM = 3  /* User-defined settings */
} sound_quality_t;

/* Dithering modes for 16-bit output */
typedef enum {
  SOUND_DITHER_NONE = 0,        /* No dithering (truncation) */
  SOUND_DITHER_RECTANGULAR = 1, /* RPDF dithering */
  SOUND_DITHER_TRIANGULAR = 2   /* TPDF dithering (recommended) */
} sound_dither_t;

/* External variables */
extern int sound_debug;
extern int sound_feedback;
extern unsigned int sound_minfields;
extern unsigned int sound_maxfields;
extern unsigned int sound_speed;         /* Output sample rate */
extern unsigned int sound_internal_rate; /* Internal processing rate */
extern unsigned int sound_oversampling;  /* Oversampling factor */
extern unsigned int sound_sampsperfield;
extern unsigned int sound_threshold;
extern uint8 sound_regs1[256];
extern uint8 sound_regs2[256];
extern uint8 sound_address1;
extern uint8 sound_address2;
extern uint8 sound_keys[8];
extern unsigned int sound_on;
extern unsigned int sound_psg;
extern unsigned int sound_fm;
extern unsigned int sound_filter;
extern unsigned int sound_hq_filter;     /* High-quality filter enable */
extern sound_dither_t sound_dither_mode; /* Dithering mode */

/* Audio buffers - sized for maximum rate */
extern uint16 sound_soundbuf[2][SOUND_BUFFER_SAMPLES];

/* Float buffer for high-quality internal processing */
extern float
    sound_floatbuf[2][SOUND_BUFFER_SAMPLES * 4]; /* *4 for max oversampling */

/* Core sound API */
int sound_start(void);
void sound_stop(void);
int sound_init(void);
void sound_final(void);
int sound_reset(void);
void sound_startfield(void);
void sound_endfield(void);
void sound_genreset(void);
uint8 sound_ym2612fetch(uint8 addr);
void sound_ym2612store(uint8 addr, uint8 data);
/* Cycle-accurate variant for Z80-originated writes: burstpos is the write's
 * position within the current Z80 sync burst in 1/4096 of the scanline (see
 * cpuz80_getburstpos). The write is queued and applied by the mixer at the
 * matching sample offset. */
void sound_ym2612store_at(uint8 addr, uint8 data, unsigned int burstpos);
void sound_sn76496store(uint8 data);
void sound_line(void);

/* Audio quality control */
int sound_set_quality(sound_quality_t quality);
int sound_set_sample_rate(unsigned int rate);
int sound_set_oversampling(unsigned int factor);
void sound_set_dither_mode(sound_dither_t mode);

#endif /* GENERATOR_GENSOUND_H */
