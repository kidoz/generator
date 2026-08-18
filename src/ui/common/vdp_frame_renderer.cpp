/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared VDP scanline -> ARGB8888 conversion for the windowed UI backends */

#include "vdp_frame_renderer.hpp"

#include "system.hpp"
#include "uiplot.h"
#include "vdp.h"
#include "vdp.hpp"

#include <algorithm>

namespace generator::ui {

unsigned int VdpFrameRenderer::begin_line(int line)
{
  const Vdp &chip = generator::vdp();

  if (line < 0 || line >= static_cast<int>(chip.vdp_vislines))
    return 0;

  if (field_width_ == 0)
    field_width_ = (chip.vdp_reg[12] & 1) ? 320 : 256;

  return field_width_;
}

void VdpFrameRenderer::render_into(int line, uint32_t *dest)
{
  if (dest == nullptr || field_width_ == 0)
    return;

  const Vdp &chip = generator::vdp();

  field_lines_ = std::max(field_lines_, line + 1);

  switch ((chip.vdp_reg[12] >> 1) & 3) {
  case 0: /* normal */
  case 1: /* interlace, simply doubled up */
  case 2: /* invalid */
    vdp_renderline(static_cast<unsigned int>(line), gfx_, 0);
    break;
  case 3: /* interlace with double resolution */
    vdp_renderline(static_cast<unsigned int>(line), gfx_, chip.vdp_oddframe);
    break;
  }

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
