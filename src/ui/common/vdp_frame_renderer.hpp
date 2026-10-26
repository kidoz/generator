/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared VDP scanline -> ARGB8888 conversion for the windowed UI backends */

#pragma once

#include "machine.h"

#include <cstdint>
#include <span>

namespace generator::ui {

/* Turns the core's paletted or final-color scanlines into ARGB8888 rows.
 *
 * Paletted lines arrive through IVideoBackend::render_line as CRAM indices;
 * final-color lines arrive through render_line_rgba. The shared renderer
 * refreshes the palette cache when needed and forces alpha opaque.
 *
 * Usage, per scanline:
 *
 *     renderer.render_pushed(line, pixels, row);
 *
 * where `row` has room for pixels.size() destination pixels, and once the
 * field completes:
 *
 *     const int lines = renderer.end_field();
 *
 * The renderer holds the scratch line buffer, so each backend needs one
 * instance and must not share it across threads. */
class VdpFrameRenderer {
public:
  /* A Mega Drive field is at most 320 visible pixels wide (H40); H32 mode
   * narrows it to 256. */
  static constexpr unsigned int kMaxWidth = 320;

  /* Convert one pushed scanline into `dest`, which must have room for
   * field_width() pixels. Bits 24-31 are forced opaque: the uiplot palette
   * cache carries no alpha, and a backend that honours the channel would
   * otherwise draw a fully transparent field. */
  void render_pushed(int line, std::span<const uint8_t> pixels, uint32_t *dest);

  /* Copy a final-color scanline and force its alpha channel opaque. */
  void render_pushed_rgba(int line, std::span<const uint32_t> pixels,
                          uint32_t *dest);

  /* End the field and reset the latch. Returns the number of lines that
   * were rendered into it, which is what a backend publishing a
   * variable-height field needs -- publishing the full height would show
   * stale rows from the previous field when the VDP cut this one short. */
  int end_field();

  unsigned int field_width() const
  {
    return field_width_;
  }

  int field_lines() const
  {
    return field_lines_;
  }

private:
  uint8 gfx_[kMaxWidth] = {};
  unsigned int field_width_ = 0;
  int field_lines_ = 0;
};

}  // namespace generator::ui
