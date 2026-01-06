/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* SDL3 sound platform implementation */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <SDL3/SDL.h>

#include "generator.h"
#include "gensound.h"
#include "gensoundp.h"
#include "vdp.h"
#include "ui.h"

/*** file scoped variables ***/

static SDL_AudioStream *soundp_stream = nullptr;
static SDL_AudioDeviceID soundp_dev = 0;

/* Track samples we've written but not yet confirmed played */
static _Atomic unsigned int soundp_samples_pending = 0;

/*** soundp_detect_audio_backend - Detect audio backend ***/

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

/*** soundp_start - start sound hardware ***/

int soundp_start(void)
{
  SDL_AudioSpec src_spec, dst_spec;
  int num_devices;
  SDL_AudioDeviceID *devices;
  SDL_AudioDeviceID dev_id;

  fprintf(stderr, "[AUDIO] soundp_start() called\n");

  /* Initialize SDL audio if not already done */
  if (!SDL_WasInit(SDL_INIT_AUDIO)) {
    fprintf(stderr, "[AUDIO] Initializing SDL audio subsystem...\n");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      fprintf(stderr, "[AUDIO] SDL_InitSubSystem(AUDIO) FAILED: %s\n", SDL_GetError());
      return 1;
    }
    fprintf(stderr, "[AUDIO] SDL audio initialized, driver: %s\n",
            SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown");
  }

  LOG_VERBOSE(("SDL3 audio subsystem initialized, driver: %s",
               SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown"));

  /* Get list of audio playback devices */
  devices = SDL_GetAudioPlaybackDevices(&num_devices);
  if (devices == nullptr || num_devices == 0) {
    LOG_CRITICAL(("No audio playback devices found: %s", SDL_GetError()));
    if (devices) SDL_free(devices);
    return 1;
  }

  LOG_VERBOSE(("Found %d audio playback device(s)", num_devices));

  /* Use first available device */
  dev_id = devices[0];
  SDL_free(devices);

  /* Configure audio format */
  src_spec.freq = sound_speed;
  src_spec.format = SDL_AUDIO_S16;  /* 16-bit signed */
  src_spec.channels = 2;            /* stereo */

  /* Open the audio device */
  soundp_dev = SDL_OpenAudioDevice(dev_id, &src_spec);
  if (soundp_dev == 0) {
    LOG_CRITICAL(("SDL_OpenAudioDevice failed: %s", SDL_GetError()));
    return 1;
  }

  /* Get the actual device spec */
  if (!SDL_GetAudioDeviceFormat(soundp_dev, &dst_spec, nullptr)) {
    LOG_CRITICAL(("SDL_GetAudioDeviceFormat failed: %s", SDL_GetError()));
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
    return 1;
  }

  LOG_VERBOSE(("Audio device opened: %d Hz, %d channels, format 0x%x",
               dst_spec.freq, dst_spec.channels, dst_spec.format));

  /* Create an audio stream to convert from our format to device format */
  soundp_stream = SDL_CreateAudioStream(&src_spec, &dst_spec);
  if (soundp_stream == nullptr) {
    LOG_CRITICAL(("SDL_CreateAudioStream failed: %s", SDL_GetError()));
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
    return 1;
  }

  /* Bind the stream to the audio device */
  if (!SDL_BindAudioStream(soundp_dev, soundp_stream)) {
    LOG_CRITICAL(("SDL_BindAudioStream failed: %s", SDL_GetError()));
    SDL_DestroyAudioStream(soundp_stream);
    soundp_stream = nullptr;
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
    return 1;
  }

  /* Initialize pending samples counter */
  atomic_store(&soundp_samples_pending, 0);

  /* Start audio playback */
  SDL_ResumeAudioDevice(soundp_dev);

  fprintf(stderr, "[AUDIO] Audio device started: dev=%u, stream=%p, %dHz stereo\n",
          (unsigned)soundp_dev, (void*)soundp_stream, src_spec.freq);

  /* Detect and log audio backend */
  const char *backend = soundp_detect_audio_backend();

  LOG_VERBOSE(("SDL3 Audio started: %d Hz, %d channels",
               src_spec.freq, src_spec.channels));
  LOG_VERBOSE(
      ("Audio backend: %s (SDL driver: %s)", backend,
       SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown"));
  LOG_VERBOSE(("Threshold = %d bytes (%d fields of sound === %dms latency)",
               sound_threshold * 4, sound_minfields,
               (int)(1000 * (float)sound_minfields / (float)vdp_framerate)));

  /* Provide helpful information for PulseAudio users */
  if (strstr(backend, "PulseAudio") != nullptr &&
      strstr(backend, "PipeWire") == nullptr) {
    LOG_VERBOSE(("Tip: For lower latency, consider switching to PipeWire"));
    LOG_VERBOSE(
        ("     PipeWire provides 3-10ms latency vs PulseAudio's 50-100ms"));
  }

  /* Pre-fill audio buffer with silence to bootstrap the buffer level.
     This prevents the frame-skip logic from starving because the buffer
     never reaches the threshold when production == consumption rate.
     Pre-fill with ~3 frames worth of silence (half the threshold). */
  {
    unsigned int prefill_samples = sound_sampsperfield * 3;
    size_t prefill_bytes = prefill_samples * 4; /* stereo 16-bit = 4 bytes */
    int16_t *silence = (int16_t *)calloc(prefill_samples * 2, sizeof(int16_t));
    if (silence) {
      if (SDL_PutAudioStreamData(soundp_stream, silence, (int)prefill_bytes)) {
        LOG_VERBOSE(("Pre-filled audio buffer with %u samples of silence",
                     prefill_samples));
      }
      free(silence);
    }
  }

  return 0;
}

/*** soundp_stop - stop sound hardware ***/

void soundp_stop(void)
{
  fprintf(stderr, "[AUDIO] soundp_stop() called, stream=%p, dev=%u\n",
          (void*)soundp_stream, (unsigned)soundp_dev);
  if (soundp_stream) {
    SDL_DestroyAudioStream(soundp_stream);
    soundp_stream = nullptr;
  }
  if (soundp_dev != 0) {
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
  }
}

/*** soundp_pause - pause audio playback ***/

void soundp_pause(void)
{
  if (soundp_dev != 0) {
    SDL_PauseAudioDevice(soundp_dev);
  }
}

/*** soundp_resume - resume audio playback ***/

void soundp_resume(void)
{
  if (soundp_dev != 0) {
    SDL_ResumeAudioDevice(soundp_dev);
  }
}

/*** soundp_samplesbuffered - how many samples are currently buffered? ***/

int soundp_samplesbuffered(void)
{
  if (!soundp_stream)
    return 0;

  /* SDL3: Get queued bytes in the audio stream */
  int queued_bytes = SDL_GetAudioStreamQueued(soundp_stream);

  /* Convert bytes to samples (4 bytes per stereo sample: 2 channels * 2 bytes) */
  return queued_bytes / 4;
}

/*** soundp_output - output samples to SDL3 ***/
/* Thread Safety: SDL3 audio streams are internally synchronized.
   SDL_PutAudioStreamData() and SDL_GetAudioStreamQueued() are thread-safe
   when using bound audio streams, so no additional mutex is needed here.
   The emulation thread calls this function to push samples, and SDL3's
   audio callback thread consumes them safely. */

/*** soundp_reset - full audio subsystem restart ***/

int soundp_reset(void)
{
  fprintf(stderr, "[AUDIO] soundp_reset() - full subsystem restart\n");

  /* Stop current audio */
  soundp_stop();

  /* Quit SDL audio subsystem completely */
  if (SDL_WasInit(SDL_INIT_AUDIO)) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }

  /* Restart audio */
  return soundp_start();
}

/*** soundp_output - output samples to SDL3 ***/

void soundp_output(uint16 *left, uint16 *right, unsigned int samples)
{
  int16_t *interleaved;
  unsigned int i;
  static int debug_count = 0;

  if (!soundp_stream || samples == 0) {
    if (debug_count < 3) {
      fprintf(stderr, "[AUDIO] soundp_output: NO STREAM! stream=%p samples=%u\n",
              (void*)soundp_stream, samples);
      debug_count++;
    }
    return;
  }

  /* Debug: show first few outputs */
  if (debug_count < 5) {
    /* Check for non-zero audio data */
    int has_audio = 0;
    for (unsigned int j = 0; j < samples && j < 50; j++) {
      if (left[j] != 0 || right[j] != 0) {
        has_audio = 1;
        break;
      }
    }
    /* Check device state */
    bool paused = SDL_AudioDevicePaused(soundp_dev);
    fprintf(stderr, "[AUDIO] soundp_output: %u samples, has_audio=%d, queued=%d, dev_paused=%d\n",
            samples, has_audio, SDL_GetAudioStreamQueued(soundp_stream), (int)paused);
    debug_count++;
  }

  /* Allocate interleaved buffer */
  interleaved = malloc(samples * 2 * sizeof(int16_t));
  if (!interleaved)
    return;

  /* Interleave left and right channels */
  for (i = 0; i < samples; i++) {
    interleaved[i * 2] = (int16_t)left[i];
    interleaved[i * 2 + 1] = (int16_t)right[i];
  }

  /* Push audio data to the stream */
  if (!SDL_PutAudioStreamData(soundp_stream, interleaved, samples * 4)) {
    static int err_count = 0;
    if (err_count < 3) {
      fprintf(stderr, "[AUDIO] SDL_PutAudioStreamData FAILED: %s\n", SDL_GetError());
      err_count++;
    }
  }

  free(interleaved);
}
