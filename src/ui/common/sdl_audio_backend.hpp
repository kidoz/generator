/* SPDX-License-Identifier: GPL-2.0-or-later */
/* IAudioBackend that plays samples through the SDL3 platform layer */

#pragma once

#include "interfaces/audio_backend.hpp"

#include "gensoundp.h"

#include <algorithm>

namespace generator::ui {

/* The core hands the board's field of samples straight to the platform
 * queue. soundp_start() must have opened a device first; with none open
 * soundp_output() discards the field, so a backend that fails to start
 * still runs, silently. */
class SdlAudioBackend : public IAudioBackend {
public:
  void output_samples(std::span<const uint16_t> left,
                      std::span<const uint16_t> right) override
  {
    const std::size_t samples = std::min(left.size(), right.size());
    if (samples == 0) {
      return;
    }
    soundp_output(left.data(), right.data(), (unsigned int)samples);
  }
};

}  // namespace generator::ui
