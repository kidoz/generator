/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* SDL2 sound platform implementation */

#include <string.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

#include "generator.h"
#include "gensound.h"
#include "gensoundp.h"
#include "vdp.h"
#include "ui.h"

/*** file scoped variables ***/

static SDL_AudioDeviceID soundp_dev = 0;
static SDL_AudioSpec soundp_spec;

/* Ring buffer for audio samples */
#define RING_BUFFER_SIZE (SOUND_MAXRATE * 2)  /* 2 seconds of buffering */
static int16_t soundp_ring_buffer[RING_BUFFER_SIZE * 2]; /* stereo */
static volatile unsigned int soundp_write_pos = 0;
static volatile unsigned int soundp_read_pos = 0;
static SDL_mutex *soundp_mutex = NULL;

/*** soundp_audio_callback - SDL2 audio callback ***/

static void soundp_audio_callback(void *userdata, Uint8 *stream, int len)
{
  int samples_requested = len / 4; /* 2 bytes per sample, 2 channels */
  int16_t *output = (int16_t *)stream;
  unsigned int samples_available;
  unsigned int i;

  SDL_LockMutex(soundp_mutex);

  /* Calculate available samples in ring buffer */
  if (soundp_write_pos >= soundp_read_pos) {
    samples_available = soundp_write_pos - soundp_read_pos;
  } else {
    samples_available = RING_BUFFER_SIZE - soundp_read_pos + soundp_write_pos;
  }

  /* Copy samples from ring buffer to output */
  if (samples_available >= (unsigned int)samples_requested) {
    for (i = 0; i < (unsigned int)samples_requested; i++) {
      output[i * 2] = soundp_ring_buffer[soundp_read_pos * 2];
      output[i * 2 + 1] = soundp_ring_buffer[soundp_read_pos * 2 + 1];
      soundp_read_pos = (soundp_read_pos + 1) % RING_BUFFER_SIZE;
    }
  } else {
    /* Not enough samples, output what we have and fill rest with silence */
    for (i = 0; i < samples_available; i++) {
      output[i * 2] = soundp_ring_buffer[soundp_read_pos * 2];
      output[i * 2 + 1] = soundp_ring_buffer[soundp_read_pos * 2 + 1];
      soundp_read_pos = (soundp_read_pos + 1) % RING_BUFFER_SIZE;
    }
    /* Fill remaining with silence */
    memset(&output[samples_available * 2], 0,
           (samples_requested - samples_available) * 4);
  }

  SDL_UnlockMutex(soundp_mutex);
}

/*** soundp_start - start sound hardware ***/

int soundp_start(void)
{
  SDL_AudioSpec desired;

  /* Initialize SDL audio if not already done */
  if (!SDL_WasInit(SDL_INIT_AUDIO)) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
      LOG_CRITICAL(("SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError()));
      return 1;
    }
  }

  /* Create mutex for ring buffer access */
  soundp_mutex = SDL_CreateMutex();
  if (!soundp_mutex) {
    LOG_CRITICAL(("Failed to create audio mutex: %s", SDL_GetError()));
    return 1;
  }

  /* Configure audio format */
  memset(&desired, 0, sizeof(desired));
  desired.freq = sound_speed;
  desired.format = AUDIO_S16SYS; /* 16-bit signed, system byte order */
  desired.channels = 2; /* stereo */
  desired.samples = 512; /* buffer size in samples */
  desired.callback = soundp_audio_callback;
  desired.userdata = NULL;

  /* Open audio device */
  soundp_dev = SDL_OpenAudioDevice(NULL, 0, &desired, &soundp_spec,
                                    SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (soundp_dev == 0) {
    LOG_CRITICAL(("SDL_OpenAudioDevice failed: %s", SDL_GetError()));
    SDL_DestroyMutex(soundp_mutex);
    soundp_mutex = NULL;
    return 1;
  }

  /* Check if we got the format we wanted */
  if (soundp_spec.format != AUDIO_S16SYS) {
    LOG_CRITICAL(("SDL audio format not supported (must be 16 bit signed)"));
    SDL_CloseAudioDevice(soundp_dev);
    SDL_DestroyMutex(soundp_mutex);
    soundp_dev = 0;
    soundp_mutex = NULL;
    return 1;
  }

  if (soundp_spec.channels != 2) {
    LOG_CRITICAL(("SDL audio does not support stereo"));
    SDL_CloseAudioDevice(soundp_dev);
    SDL_DestroyMutex(soundp_mutex);
    soundp_dev = 0;
    soundp_mutex = NULL;
    return 1;
  }

  if (abs((int)soundp_spec.freq - (int)sound_speed) > 100) {
    LOG_CRITICAL(("SDL audio does not support sample rate %d (got %d)",
                  sound_speed, soundp_spec.freq));
    SDL_CloseAudioDevice(soundp_dev);
    SDL_DestroyMutex(soundp_mutex);
    soundp_dev = 0;
    soundp_mutex = NULL;
    return 1;
  }

  if (soundp_spec.freq != sound_speed) {
    LOG_NORMAL(("Warning: Sample rate not exactly %d (got %d)",
                sound_speed, soundp_spec.freq));
  }

  /* Initialize ring buffer */
  soundp_write_pos = 0;
  soundp_read_pos = 0;
  memset(soundp_ring_buffer, 0, sizeof(soundp_ring_buffer));

  /* Start audio playback */
  SDL_PauseAudioDevice(soundp_dev, 0);

  LOG_VERBOSE(("SDL2 Audio opened: %d Hz, %d channels, %d samples buffer",
               soundp_spec.freq, soundp_spec.channels, soundp_spec.samples));
  LOG_VERBOSE(("Theshold = %d bytes (%d fields of sound === %dms latency)",
               sound_threshold * 4, sound_minfields,
               (int)(1000 * (float)sound_minfields / (float)vdp_framerate)));

  return 0;
}

/*** soundp_stop - stop sound hardware ***/

void soundp_stop(void)
{
  if (soundp_dev) {
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
  }
  if (soundp_mutex) {
    SDL_DestroyMutex(soundp_mutex);
    soundp_mutex = NULL;
  }
}

/*** soundp_samplesbuffered - how many samples are currently buffered? ***/

int soundp_samplesbuffered(void)
{
  unsigned int samples_buffered;

  if (!soundp_dev)
    return 0;

  SDL_LockMutex(soundp_mutex);

  /* Calculate buffered samples in ring buffer */
  if (soundp_write_pos >= soundp_read_pos) {
    samples_buffered = soundp_write_pos - soundp_read_pos;
  } else {
    samples_buffered = RING_BUFFER_SIZE - soundp_read_pos + soundp_write_pos;
  }

  SDL_UnlockMutex(soundp_mutex);

  return samples_buffered;
}

/*** soundp_output - output samples to SDL2 ***/

void soundp_output(uint16 *left, uint16 *right, unsigned int samples)
{
  unsigned int i;
  unsigned int space_available;

  if (!soundp_dev)
    return;

  SDL_LockMutex(soundp_mutex);

  /* Calculate available space in ring buffer */
  if (soundp_write_pos >= soundp_read_pos) {
    space_available = RING_BUFFER_SIZE - (soundp_write_pos - soundp_read_pos) - 1;
  } else {
    space_available = soundp_read_pos - soundp_write_pos - 1;
  }

  /* Clamp samples to available space */
  if (samples > space_available) {
    LOG_VERBOSE(("Warning: Ring buffer overflow, dropping %d samples",
                 samples - space_available));
    samples = space_available;
  }

  /* Copy samples to ring buffer */
  for (i = 0; i < samples; i++) {
    soundp_ring_buffer[soundp_write_pos * 2] = (int16_t)left[i];
    soundp_ring_buffer[soundp_write_pos * 2 + 1] = (int16_t)right[i];
    soundp_write_pos = (soundp_write_pos + 1) % RING_BUFFER_SIZE;
  }

  SDL_UnlockMutex(soundp_mutex);
}
