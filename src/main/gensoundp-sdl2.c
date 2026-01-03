/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* SDL2 sound platform implementation */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdatomic.h>
#include <SDL2/SDL.h>

#include "generator.h"
#include "gensound.h"
#include "gensoundp.h"
#include "vdp.h"
#include "ui.h"

/*** file scoped variables ***/

static SDL_AudioDeviceID soundp_dev = 0;
static SDL_AudioSpec soundp_spec;

/* Lock-free SPSC ring buffer for audio samples
 * Using acquire/release memory ordering for thread safety:
 * - Producer (emulator): reads read_pos with acquire, writes samples, stores
 *   write_pos with release
 * - Consumer (SDL callback): reads write_pos with acquire, reads samples,
 *   stores read_pos with release
 * This is safe because there's exactly one producer and one consumer. */
#define RING_BUFFER_SIZE (SOUND_MAXRATE * 4) /* 4 seconds of buffering */
static int16_t soundp_ring_buffer[RING_BUFFER_SIZE * 2]; /* stereo */
static _Atomic unsigned int soundp_write_pos = 0;
static _Atomic unsigned int soundp_read_pos = 0;

/*** soundp_detect_pipewire - Detect if PipeWire is being used ***/

static const char *soundp_detect_audio_backend(void)
{
  const char *driver;
  char runtime_dir[512];
  const char *xdg_runtime;

  /* Get the current SDL audio driver name */
  driver = SDL_GetCurrentAudioDriver();

  /* Check if PipeWire runtime directory exists */
  xdg_runtime = getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime) {
    snprintf(runtime_dir, sizeof(runtime_dir), "%s/pipewire-0", xdg_runtime);
    if (access(runtime_dir, F_OK) == 0) {
      return "PipeWire (via PulseAudio compatibility)";
    }
  }

  /* Check for direct PipeWire detection via environment */
  if (getenv("PIPEWIRE_RUNTIME_DIR")) {
    return "PipeWire (native)";
  }

  /* Otherwise, return the SDL driver name */
  if (driver) {
    if (strcmp(driver, "pulseaudio") == 0) {
      return "PulseAudio";
    } else if (strcmp(driver, "alsa") == 0) {
      return "ALSA (direct)";
    } else if (strcmp(driver, "pipewire") == 0) {
      return "PipeWire (native)";
    } else {
      return driver;
    }
  }

  return "Unknown";
}

/*** soundp_audio_callback - SDL2 audio callback (lock-free consumer) ***/

static void soundp_audio_callback(void *userdata, Uint8 *stream, int len)
{
  (void)userdata; /* unused */
  int samples_requested = len / 4; /* 2 bytes per sample, 2 channels */
  int16_t *output = (int16_t *)stream;
  unsigned int samples_available;
  unsigned int i;

  /* Lock-free read: acquire write_pos to see all samples written by producer */
  unsigned int write_pos = atomic_load_explicit(&soundp_write_pos,
                                                memory_order_acquire);
  unsigned int read_pos = atomic_load_explicit(&soundp_read_pos,
                                               memory_order_relaxed);

  /* Calculate available samples in ring buffer */
  if (write_pos >= read_pos) {
    samples_available = write_pos - read_pos;
  } else {
    samples_available = RING_BUFFER_SIZE - read_pos + write_pos;
  }

  /* Copy samples from ring buffer to output */
  if (samples_available >= (unsigned int)samples_requested) {
    for (i = 0; i < (unsigned int)samples_requested; i++) {
      output[i * 2] = soundp_ring_buffer[read_pos * 2];
      output[i * 2 + 1] = soundp_ring_buffer[read_pos * 2 + 1];
      read_pos = (read_pos + 1) % RING_BUFFER_SIZE;
    }
  } else {
    /* Not enough samples, output what we have and fill rest with silence */
    for (i = 0; i < samples_available; i++) {
      output[i * 2] = soundp_ring_buffer[read_pos * 2];
      output[i * 2 + 1] = soundp_ring_buffer[read_pos * 2 + 1];
      read_pos = (read_pos + 1) % RING_BUFFER_SIZE;
    }
    /* Fill remaining with silence */
    memset(&output[samples_available * 2], 0,
           (samples_requested - samples_available) * 4);
  }

  /* Release store: publish the new read position */
  atomic_store_explicit(&soundp_read_pos, read_pos, memory_order_release);
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

  /* Configure audio format */
  memset(&desired, 0, sizeof(desired));
  desired.freq = sound_speed;
  desired.format = AUDIO_S16SYS; /* 16-bit signed, system byte order */
  desired.channels = 2;          /* stereo */
  /* Buffer size: clamp to audio threshold to avoid starving the callback.
     GTK4 rendering occasionally blocks longer than one frame; keeping the SDL
     request below the amount we normally stage (sound_threshold ≈ 5 fields)
     prevents the callback from zero-filling and crackling. */
  unsigned int requested_samples = sound_threshold;
  if (requested_samples > 2048)
    requested_samples = 2048; /* ~46ms @ 44.1kHz */
  if (requested_samples < 1024)
    requested_samples = 1024; /* keep latency reasonable on PAL */
  desired.samples = (Uint16)requested_samples;
  desired.callback = soundp_audio_callback;
  desired.userdata = nullptr;

  /* Open audio device */
  soundp_dev = SDL_OpenAudioDevice(nullptr, 0, &desired, &soundp_spec,
                                   SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (soundp_dev == 0) {
    LOG_CRITICAL(("SDL_OpenAudioDevice failed: %s", SDL_GetError()));
    return 1;
  }

  /* Check if we got the format we wanted */
  if (soundp_spec.format != AUDIO_S16SYS) {
    LOG_CRITICAL(("SDL audio format not supported (must be 16 bit signed)"));
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
    return 1;
  }

  if (soundp_spec.channels != 2) {
    LOG_CRITICAL(("SDL audio does not support stereo"));
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
    return 1;
  }

  if (abs((int)soundp_spec.freq - (int)sound_speed) > 100) {
    LOG_CRITICAL(("SDL audio does not support sample rate %d (got %d)",
                  sound_speed, soundp_spec.freq));
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
    return 1;
  }

  if (soundp_spec.freq != sound_speed) {
    LOG_NORMAL(("Warning: Sample rate not exactly %d (got %d)", sound_speed,
                soundp_spec.freq));
  }

  /* Initialize ring buffer (atomics) */
  atomic_store(&soundp_write_pos, 0);
  atomic_store(&soundp_read_pos, 0);
  memset(soundp_ring_buffer, 0, sizeof(soundp_ring_buffer));

  /* Start audio playback */
  SDL_PauseAudioDevice(soundp_dev, 0);

  /* Detect and log audio backend */
  const char *backend = soundp_detect_audio_backend();

  LOG_VERBOSE(("SDL2 Audio opened: %d Hz, %d channels, %d samples buffer",
               soundp_spec.freq, soundp_spec.channels, soundp_spec.samples));
  LOG_VERBOSE(
      ("Audio backend: %s (SDL driver: %s)", backend,
       SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown"));
  LOG_VERBOSE(("Threshold = %d bytes (%d fields of sound === %dms latency)",
               sound_threshold * 4, sound_minfields,
               (int)(1000 * (float)sound_minfields / (float)vdp_framerate)));

  /* Provide helpful information for PulseAudio users (but not PipeWire users)
   */
  if (strstr(backend, "PulseAudio") != nullptr &&
      strstr(backend, "PipeWire") == nullptr) {
    LOG_VERBOSE(("Tip: For lower latency, consider switching to PipeWire"));
    LOG_VERBOSE(
        ("     PipeWire provides 3-10ms latency vs PulseAudio's 50-100ms"));
  }

  return 0;
}

/*** soundp_stop - stop sound hardware ***/

void soundp_stop(void)
{
  if (soundp_dev) {
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
  }
}

/*** soundp_pause - pause audio playback ***/

void soundp_pause(void)
{
  if (soundp_dev) {
    SDL_PauseAudioDevice(soundp_dev, 1);
  }
}

/*** soundp_resume - resume audio playback ***/

void soundp_resume(void)
{
  if (soundp_dev) {
    SDL_PauseAudioDevice(soundp_dev, 0);
  }
}

/*** soundp_samplesbuffered - how many samples are currently buffered? ***/

int soundp_samplesbuffered(void)
{
  unsigned int samples_buffered;

  if (!soundp_dev)
    return 0;

  /* Lock-free read of positions */
  unsigned int write_pos = atomic_load_explicit(&soundp_write_pos,
                                                memory_order_relaxed);
  unsigned int read_pos = atomic_load_explicit(&soundp_read_pos,
                                               memory_order_relaxed);

  /* Calculate buffered samples in ring buffer */
  if (write_pos >= read_pos) {
    samples_buffered = write_pos - read_pos;
  } else {
    samples_buffered = RING_BUFFER_SIZE - read_pos + write_pos;
  }

  /* Account for SDL's internal audio buffer - samples that have been pulled
     from our ring buffer by the audio callback but are still queued in SDL's
     hardware buffer waiting to be played. This ensures accurate timing for
     frame rate synchronization. */
  samples_buffered += soundp_spec.samples;

  return samples_buffered;
}

/*** soundp_output - output samples to SDL2 (lock-free producer) ***/

void soundp_output(uint16 *left, uint16 *right, unsigned int samples)
{
  unsigned int i;
  unsigned int space_available;

  if (!soundp_dev)
    return;

  /* Lock-free read: acquire read_pos to calculate available space */
  unsigned int read_pos = atomic_load_explicit(&soundp_read_pos,
                                               memory_order_acquire);
  unsigned int write_pos = atomic_load_explicit(&soundp_write_pos,
                                                memory_order_relaxed);

  /* Calculate available space in ring buffer */
  if (write_pos >= read_pos) {
    space_available = RING_BUFFER_SIZE - (write_pos - read_pos) - 1;
  } else {
    space_available = read_pos - write_pos - 1;
  }

  /* Clamp samples to available space */
  if (samples > space_available) {
    /* Note: Don't log here - this is in the hot audio path and logging can
       block/freeze audio. The backpressure mechanism in ui_tick_callback should
       prevent this from happening. */
    samples = space_available;
  }

  /* Copy samples to ring buffer */
  for (i = 0; i < samples; i++) {
    soundp_ring_buffer[write_pos * 2] = (int16_t)left[i];
    soundp_ring_buffer[write_pos * 2 + 1] = (int16_t)right[i];
    write_pos = (write_pos + 1) % RING_BUFFER_SIZE;
  }

  /* Release store: publish the new write position after all samples written */
  atomic_store_explicit(&soundp_write_pos, write_pos, memory_order_release);
}
