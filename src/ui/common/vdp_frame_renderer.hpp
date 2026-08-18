/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared VDP scanline -> ARGB8888 conversion for the windowed UI backends */

#pragma once

#include "machine.h"

#include <cstdint>

namespace generator::ui {

/* Turns VDP scanlines into the ARGB8888 rows a windowed backend displays.
 *
 * Every windowed backend needs the same sequence: pick the field's pixel
 * width, branch on the interlace mode in register 12, call vdp_renderline
 * into a scratch buffer, refresh the palette cache and convert. Each one
 * used to carry its own copy, and the copies drifted -- the two corrections
 * below existed in the NodalKit backend only.
 *
 * Usage, per scanline:
 *
 *     const unsigned int width = renderer.begin_line(line);
 *     if (width == 0)
 *       return;                       // line is outside the visible field
 *     uint32_t *row = <destination for `line`, at least `width` pixels>;
 *     renderer.render_into(line, row);
 *
 * and once the field completes:
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

  /* Prepare to render `line`. Returns the field's pixel width, or 0 when
   * the line falls outside the visible field and must be skipped.
   *
   * The width is latched on the field's first visible line and held for the
   * rest of the field. A game that flips H32/H40 partway through a frame
   * would otherwise leave rows at two different widths in one buffer, which
   * either shifts the tail of the field (packed rows) or leaves stale
   * pixels beyond the narrower rows (fixed-stride rows). */
  unsigned int begin_line(int line);

  /* Render the line prepared by begin_line() into `dest`, which must have
   * room for field_width() pixels. Bits 24-31 are forced opaque: the
   * uiplot palette cache carries no alpha, and a backend that honours the
   * channel would otherwise draw a fully transparent field. */
  void render_into(int line, uint32_t *dest);

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
