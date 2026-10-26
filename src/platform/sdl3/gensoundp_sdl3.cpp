/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SDL3 sound platform implementation */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "audio_output.hpp"
#include <stdatomic.h>
#include <SDL3/SDL.h>

#include "generator.h"
#include "gensoundp.h"
#include "ui.h"

/*** file scoped variables ***/

static SDL_AudioStream *soundp_stream = nullptr;
static SDL_AudioDeviceID soundp_dev = 0;
namespace {

struct PlaybackControls {
  SDL_AudioStream *test_stream = nullptr;
  int volume = 100;
  bool muted = false;
};

PlaybackControls &playback_controls()
{
  static PlaybackControls controls;
  return controls;
}

}  // namespace

/* Track samples we've written but not yet confirmed played */
static _Atomic(unsigned int) soundp_samples_pending = 0;

/*** soundp_detect_audio_backend - Detect audio backend ***/

static const char *soundp_detect_audio_backend(void)
{
  const char *driver = SDL_GetCurrentAudioDriver();
  if (!driver)
    return "Unknown";
  if (strcmp(driver, "pulseaudio") == 0)
    return "PulseAudio";
  if (strcmp(driver, "pipewire") == 0)
    return "PipeWire";
  if (strcmp(driver, "alsa") == 0)
    return "ALSA";
  if (strcmp(driver, "jack") == 0)
    return "JACK";
  if (strcmp(driver, "dummy") == 0)
    return "Silent (dummy)";
  if (strcmp(driver, "disk") == 0)
    return "File output (disk)";
  return driver;
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
      LOG_CRITICAL("SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
      return 1;
    }
  }

  LOG_VERBOSE("SDL3 audio subsystem initialized, driver: %s",
              SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver()
                                          : "unknown");

  /* Get list of audio playback devices */
  devices = SDL_GetAudioPlaybackDevices(&num_devices);
  if (devices == nullptr || num_devices == 0) {
    LOG_CRITICAL("No audio playback devices found: %s", SDL_GetError());
    if (devices)
      SDL_free(devices);
    return 1;
  }

  LOG_VERBOSE("Found %d audio playback device(s)", num_devices);
  SDL_free(devices);

  /* Play to the system default rather than whichever device the platform
     happens to enumerate first. On macOS devices[0] is commonly an external
     interface (a USB audio box, a capture device) while the user is
     listening to the built-in speakers, so picking it means silence with no
     error anywhere. The default id also follows the user switching outputs
     while the emulator runs. */
  dev_id = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;

  /* Configure audio format */
  src_spec.freq = SOUND_SAMPLERATE;
  src_spec.format = SDL_AUDIO_S16; /* 16-bit signed */
  src_spec.channels = 2;           /* stereo */

  /* Open the audio device */
  soundp_dev = SDL_OpenAudioDevice(dev_id, &src_spec);
  if (soundp_dev == 0) {
    LOG_CRITICAL("SDL_OpenAudioDevice failed: %s", SDL_GetError());
    return 1;
  }

  /* Get the actual device spec */
  if (!SDL_GetAudioDeviceFormat(soundp_dev, &dst_spec, nullptr)) {
    LOG_CRITICAL("SDL_GetAudioDeviceFormat failed: %s", SDL_GetError());
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
    return 1;
  }

  {
    const char *dev_name = SDL_GetAudioDeviceName(soundp_dev);
    LOG_VERBOSE("Audio device opened: %s (%d Hz, %d channels, format 0x%x)",
                dev_name ? dev_name : "system default", dst_spec.freq,
                dst_spec.channels, dst_spec.format);
  }

  /* Create an audio stream to convert from our format to device format */
  soundp_stream = SDL_CreateAudioStream(&src_spec, &dst_spec);
  if (soundp_stream == nullptr) {
    LOG_CRITICAL("SDL_CreateAudioStream failed: %s", SDL_GetError());
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
    return 1;
  }

  /* Bind the stream to the audio device */
  if (!SDL_BindAudioStream(soundp_dev, soundp_stream)) {
    LOG_CRITICAL("SDL_BindAudioStream failed: %s", SDL_GetError());
    SDL_DestroyAudioStream(soundp_stream);
    soundp_stream = nullptr;
    SDL_CloseAudioDevice(soundp_dev);
    soundp_dev = 0;
    return 1;
  }

  if (!generator::audio::SdlAudioOutput::set_controls(
          playback_controls().volume, playback_controls().muted)) {
    LOG_CRITICAL("Failed to set audio volume: %s", SDL_GetError());
    soundp_stop();
    return 1;
  }

  /* Initialize pending samples counter */
  atomic_store(&soundp_samples_pending, 0);

  /* Start audio playback */
  SDL_ResumeAudioDevice(soundp_dev);

  /* Detect and log audio backend */
  const char *backend = soundp_detect_audio_backend();

  LOG_VERBOSE("SDL3 Audio started: %d Hz, %d channels", src_spec.freq,
              src_spec.channels);
  LOG_VERBOSE("Audio backend: %s (SDL driver: %s)", backend,
              SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver()
                                          : "unknown");
  LOG_VERBOSE("Threshold = %d bytes (%d fields of sound === %dms latency)",
              SOUNDP_THRESHOLD * 4, SOUNDP_MIN_FIELDS,
              SOUNDP_MIN_FIELDS * 1000 / 50);

  /* Provide helpful information for PulseAudio users */
  if (strstr(backend, "PulseAudio") != nullptr &&
      strstr(backend, "PipeWire") == nullptr) {
    LOG_VERBOSE("Tip: For lower latency, consider switching to PipeWire");
    LOG_VERBOSE(
        "     PipeWire provides 3-10ms latency vs PulseAudio's 50-100ms");
  }

  /* Pre-fill audio buffer with silence to bootstrap the buffer level.
     This prevents the frame-skip logic from starving because the buffer
     never reaches the threshold when production == consumption rate.
     Pre-fill with ~3 frames worth of silence (half the threshold). */
  {
    unsigned int prefill_samples = SOUNDP_SAMPLES_PER_FIELD * 3;
    size_t prefill_bytes = prefill_samples * 4; /* stereo 16-bit = 4 bytes */
    int16_t *silence = (int16_t *)calloc(prefill_samples * 2, sizeof(int16_t));
    if (silence) {
      if (SDL_PutAudioStreamData(soundp_stream, silence, (int)prefill_bytes)) {
        LOG_VERBOSE("Pre-filled audio buffer with %u samples of silence",
                    prefill_samples);
      }
      free(silence);
    }
  }

  return 0;
}

/*** soundp_stop - stop sound hardware ***/

void soundp_stop(void)
{
  generator::audio::SdlAudioOutput::stop_test();
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

/*** soundp_flush - drop queued samples (after a save-state load) ***/

void soundp_flush(void)
{
  if (soundp_stream) {
    SDL_ClearAudioStream(soundp_stream);
  }
}

/*** soundp_samplesbuffered - how many samples are currently buffered? ***/

int soundp_samplesbuffered(void)
{
  if (!soundp_stream)
    return 0;

  /* SDL3: Get queued bytes in the audio stream */
  int queued_bytes = SDL_GetAudioStreamQueued(soundp_stream);

  /* Convert bytes to samples (4 bytes per stereo sample: 2 channels * 2 bytes)
   */
  return queued_bytes / 4;
}

/*** soundp_backend_name - active output for UI display ***/

const char *soundp_backend_name(void)
{
  if (soundp_dev == 0)
    return "No audio device";
  return soundp_detect_audio_backend();
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

void soundp_output(const uint16 *left, const uint16 *right,
                   unsigned int samples)
{
  int16_t interleaved[SOUND_BUFFER_SAMPLES * 2];

  if (!soundp_stream || samples == 0) {
    return;
  }

  /* A field is normally well under one buffer, but it is not bounded by
   * one: a long DMA bills the 68K for several fields' worth of master
   * clocks in a single advance, and the core hands over every sample it
   * generated during it. Feeding the stream in buffer-sized pieces keeps
   * that audio; dropping the whole field, as this did, punched a quarter
   * of a second of silence into the start of every game that clears VRAM
   * with the display on. */
  unsigned int offset = 0;
  while (offset < samples) {
    const unsigned int chunk = samples - offset > SOUND_BUFFER_SAMPLES
                                   ? (unsigned int)SOUND_BUFFER_SAMPLES
                                   : samples - offset;
    for (unsigned int i = 0; i < chunk; i++) {
      interleaved[i * 2] = (int16_t)left[offset + i];
      interleaved[i * 2 + 1] = (int16_t)right[offset + i];
    }
    if (!SDL_PutAudioStreamData(soundp_stream, interleaved, (int)chunk * 4)) {
      static int err_count = 0;
      if (err_count < 3) {
        LOG_CRITICAL("SDL_PutAudioStreamData failed: %s", SDL_GetError());
        err_count++;
      }
      return;
    }
    offset += chunk;
  }
}

namespace generator::audio {

bool SdlAudioOutput::set_controls(int volume_percent, bool muted)
{
  int volume = std::clamp(volume_percent, 0, 100);
  float gain = muted ? 0.0f : volume / 100.0f;
  if (soundp_stream && !SDL_SetAudioStreamGain(soundp_stream, gain))
    return false;
  if (playback_controls().test_stream &&
      !SDL_SetAudioStreamGain(playback_controls().test_stream, gain))
    return false;
  playback_controls().volume = volume;
  playback_controls().muted = muted;
  return true;
}

AudioOutputStatus SdlAudioOutput::status()
{
  AudioOutputStatus result;
  if (const char *driver = SDL_GetCurrentAudioDriver())
    result.driver = driver;
  if (!soundp_dev || !soundp_stream)
    return result;
  SDL_AudioSpec spec{};
  if (!SDL_GetAudioDeviceFormat(soundp_dev, &spec, nullptr))
    return result;
  result.active = true;
  result.sample_rate = spec.freq;
  result.channels = spec.channels;
  result.gain = SDL_GetAudioStreamGain(soundp_stream);
  if (const char *name = SDL_GetAudioDeviceName(soundp_dev))
    result.device = name;
  return result;
}

std::string SdlAudioOutput::driver_label(std::string_view driver)
{
  if (driver == "auto")
    return "Automatic";
  if (driver == "pulseaudio")
    return "PulseAudio";
  if (driver == "pipewire")
    return "PipeWire";
  if (driver == "alsa")
    return "ALSA";
  if (driver == "jack")
    return "JACK";
  if (driver == "coreaudio")
    return "Core Audio";
  if (driver == "wasapi")
    return "Windows Audio (WASAPI)";
  if (driver == "directsound")
    return "DirectSound";
  if (driver == "dummy")
    return "Silent (dummy)";
  if (driver == "disk")
    return "File output (disk)";
  return std::string(driver);
}

bool SdlAudioOutput::play_test()
{
  stop_test();
  if (!soundp_dev || !soundp_stream || playback_controls().muted ||
      playback_controls().volume == 0)
    return false;
  SDL_AudioSpec source{SDL_AUDIO_S16, 2, SOUND_SAMPLERATE};
  SDL_AudioSpec destination{};
  if (!SDL_GetAudioDeviceFormat(soundp_dev, &destination, nullptr))
    return false;
  playback_controls().test_stream =
      SDL_CreateAudioStream(&source, &destination);
  if (!playback_controls().test_stream)
    return false;
  if (!SDL_BindAudioStream(soundp_dev, playback_controls().test_stream) ||
      !SDL_SetAudioStreamGain(playback_controls().test_stream,
                              playback_controls().volume / 100.0f)) {
    stop_test();
    return false;
  }
  constexpr int samples = SOUND_SAMPLERATE * 3 / 10;
  constexpr int half = samples / 2;
  std::vector<int16_t> cue(samples * 2, 0);
  for (int i = 0; i < samples; ++i) {
    int position = i % half;
    double envelope =
        std::min({1.0, position / (SOUND_SAMPLERATE * 0.01),
                  (half - 1 - position) / (SOUND_SAMPLERATE * 0.01)});
    double frequency = i < half ? 440.0 : 660.0;
    double phase =
        2 * std::numbers::pi * frequency * position / SOUND_SAMPLERATE;
    cue[i * 2 + (i < half ? 0 : 1)] =
        static_cast<int16_t>(2400 * envelope * std::sin(phase));
  }
  if (!SDL_PutAudioStreamData(playback_controls().test_stream, cue.data(),
                              static_cast<int>(cue.size() * sizeof(int16_t))) ||
      !SDL_FlushAudioStream(playback_controls().test_stream)) {
    stop_test();
    return false;
  }
  return true;
}

void SdlAudioOutput::stop_test()
{
  if (playback_controls().test_stream) {
    SDL_DestroyAudioStream(playback_controls().test_stream);
    playback_controls().test_stream = nullptr;
  }
}

}  // namespace generator::audio
