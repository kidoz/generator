/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <string>
#include <string_view>

namespace generator::audio {

struct AudioOutputStatus {
  bool active = false;
  std::string driver;
  std::string device;
  int sample_rate = 0;
  int channels = 0;
  float gain = -1;
};

/* UI-thread controls for the process's SDL output. Stream gains are applied
 * by SDL during playback, including to audio already queued by the worker.
 * Opening/closing the output still requires quiescing the emulation worker. */
class SdlAudioOutput {
public:
  static bool set_controls(int volume_percent, bool muted);
  static AudioOutputStatus status();
  static std::string driver_label(std::string_view driver);
  // A short stereo cue on a separate stream, preserving game queue pacing.
  static bool play_test();
  static void stop_test();
};

}  // namespace generator::audio
