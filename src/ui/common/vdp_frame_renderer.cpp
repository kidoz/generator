/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared VDP scanline -> ARGB8888 conversion for the windowed UI backends */

#include "vdp_frame_renderer.hpp"

#include "uiplot.h"

#include <algorithm>

namespace generator::ui {

void VdpFrameRenderer::render_pushed(int line, std::span<const uint8_t> pixels,
                                     uint32_t *dest)
{
  if (dest == nullptr || pixels.empty()) {
    return;
  }
  field_width_ = std::min<unsigned int>(kMaxWidth, (unsigned int)pixels.size());
  field_lines_ = std::max(field_lines_, line + 1);

  std::copy_n(pixels.begin(), field_width_, gfx_);

  uiplot_checkpalcache(0);
  uiplot_convertdata32(gfx_, dest, field_width_);

  for (unsigned int x = 0; x < field_width_; x++)
    dest[x] |= 0xFF000000U;
}

int VdpFrameRenderer::end_field()
{
  const int lines = field_lines_;

  field_width_ = 0;
  field_lines_ = 0;

  return lines;
}

}  // namespace generator::ui
