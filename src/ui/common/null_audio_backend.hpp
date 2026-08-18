/* SPDX-License-Identifier: GPL-2.0-or-later */
/* IAudioBackend that discards samples, for backends driving SDL3 directly */

#pragma once

#include "interfaces/audio_backend.hpp"

namespace generator::ui {

/* Audio reaches the hardware through the SDL3 platform layer
 * (src/platform/sdl3/gensoundp_sdl3.cpp), which gensound.cpp calls directly.
 * The backend seam still exists so the core does not have to know that, and
 * the gtkmm, NodalKit and console UIs all satisfy it with this no-op.
 *
 * When the sound pipeline moves into System and starts emitting through the
 * seam for real, this is the class each of those backends replaces. */
class NullAudioBackend : public IAudioBackend {
public:
  void output_samples(std::span<const uint16_t> /*left*/,
                      std::span<const uint16_t> /*right*/) override
  {
  }
};

}  // namespace generator::ui
