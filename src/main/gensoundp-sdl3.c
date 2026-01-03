/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* SDL3 sound platform implementation */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdatomic.h>
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

  /* Initialize SDL audio if not already done */
  if (!SDL_WasInit(SDL_INIT_AUDIO)) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      LOG_CRITICAL(("SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError()));
      return 1;
    }
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

  return 0;
}

/*** soundp_stop - stop sound hardware ***/

void soundp_stop(void)
{
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

void soundp_output(uint16 *left, uint16 *right, unsigned int samples)
{
  int16_t *interleaved;
  unsigned int i;

  if (!soundp_stream || samples == 0)
    return;

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
    /* Note: Don't log here - this is in the hot audio path */
  }

  free(interleaved);
}
