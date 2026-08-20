/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared VDP scanline -> ARGB8888 conversion for the windowed UI backends */

#pragma once

#include "machine.h"

#include <cstdint>
#include <span>

namespace generator::ui {

/* Turns the core's paletted scanlines into the ARGB8888 rows a windowed
 * backend displays.
 *
 * The core pushes each visible line through IVideoBackend::render_line as
 * CRAM indices; every windowed backend then needs the same two steps —
 * refresh the palette cache and convert — and each one used to carry its
 * own copy of them.
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
