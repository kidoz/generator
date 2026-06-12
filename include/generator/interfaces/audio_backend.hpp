/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Audio Backend Interface - Modern C++ DI abstraction */

#pragma once

#include <span>
#include <cstdint>

namespace generator {

class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    /*
     * Output audio samples to the audio device.
     * Both spans must have the same size.
     */
    virtual void output_samples(std::span<const uint16_t> left, 
                                std::span<const uint16_t> right) = 0;
};

} // namespace generator
