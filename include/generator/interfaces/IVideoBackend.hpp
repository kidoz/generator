/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Video Backend Interface - Modern C++ DI abstraction */

#pragma once

#include <span>
#include <cstdint>

namespace generator {

class IVideoBackend {
public:
    virtual ~IVideoBackend() = default;

    /*
     * Render a single scanline to the backend's internal buffer.
     * line is the scanline number (0 to vislines-1).
     * pixels can contain raw 8-bit paletted VDP data (optional depending on UI implementation).
     */
    virtual void render_line(int line, std::span<const uint8_t> pixels) = 0;

    /*
     * Present the completed field (frame) to the screen.
     */
    virtual void present_field() = 0;
};

} // namespace generator
