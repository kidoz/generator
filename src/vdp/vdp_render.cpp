/* SPDX-License-Identifier: GPL-2.0-or-later */
/* VDP renderer (line-based composite).
 *
 * Fresh implementation against the YM7101 documentation; register
 * semantics cross-checked against this repository's legacy renderer
 * (src/video/vdp_render.cpp) which encodes the battle-tested formulas:
 * plane A base (reg2 & 0x38)<<10, plane B (reg4 & 7)<<13, window
 * (reg3 & 0x3E)<<10, hscroll table (reg13 & 63)<<10, sprite table
 * (reg5 & 0x7F)<<9, plane size reg16, hscroll mode reg11 bits 1-0,
 * vscroll 2-cell mode reg11 bit 2.
 *
 * Line granularity: each display line is composited when it completes
 * inside the dot scheduler, from the register/memory state at that
 * moment — so mid-frame CRAM and scroll changes take effect on the next
 * line. Per-dot fetch timing, the 4-slot FIFO interaction, shadow and
 * highlight, and interlace mode 2 are the phase-2 refinements. */

#include "vdp.hpp"

#include <cstring>

namespace generator {

namespace {

/* 4-byte pattern row -> one pixel nibble with h/v flip applied */
inline Vdp::Px pattern_pixel(const uint8_t *vram, uint16_t cell,
                             uint32_t pattern, uint32_t fine_x)
{
  const uint32_t data = (uint32_t)vram[pattern & 0xFFFF] << 24 |
                        (uint32_t)vram[(pattern + 1) & 0xFFFF] << 16 |
                        (uint32_t)vram[(pattern + 2) & 0xFFFF] << 8 |
                        (uint32_t)vram[(pattern + 3) & 0xFFFF];
  const uint32_t shift = (cell & 0x800) ? fine_x * 4 : (7 - fine_x) * 4;
  const uint8_t pixel = (data >> shift) & 0xF;
  if (pixel == 0) {
    return {0, 0};
  }
  return {(uint8_t)(((cell >> 13) & 3) << 4 | pixel), (uint8_t)(cell >> 15)};
}

}  // namespace

Vdp::Px Vdp::layer_pixel(int layer, uint32_t line, uint32_t x) const
{
  const uint32_t hsize = m_reg[16] & 3;
  const uint32_t vsize = (m_reg[16] >> 4) & 3;
  const uint32_t hmode = m_reg[11] & 3;
  const bool vmode = (m_reg[11] & 4) != 0;
  const uint32_t ntbase =
      layer == 0 ? ((m_reg[2] & 0x38) << 10) : ((m_reg[4] & 7) << 13);
  const uint32_t hsbase = ((m_reg[13] & 63) << 10) + layer * 2;

  uint32_t hsentry;
  switch (hmode) {
  case 1: /* per-line for the first 8 lines, then constant (the doc
           * calls this mode erroneous; legacy semantics) */
    hsentry = (line < 8 ? line : 0) * 2;
    break;
  case 2: /* 16-pixel cells. unverified */
    hsentry = (x >> 4) * 16;
    break;
  case 3: /* per scanline */
    hsentry = line * 2;
    break;
  default:
    hsentry = 0;
    break;
  }
  int32_t hoffset = 0x400 - (int32_t)vram_word(hsbase + hsentry);
  hoffset = (hoffset + (int32_t)x) & 0x3FF;

  const uint32_t vsidx =
      vmode ? (((x >> 4) << 1) | (uint32_t)layer) : (uint32_t)layer;
  const uint32_t voffset = (uint32_t)((int32_t)m_vsram[vsidx] + (int32_t)line);

  uint32_t hwidth = (hsize + 1) << 5;
  const uint32_t vmask = (((vsize + 1) << 5) << 3) - 1;
  uint32_t vmasked = voffset & vmask;
  if (hsize == 2) { /* 128-wide mode: only the top name-table row */
    vmasked &= 7;
    hwidth = 32;
  }
  hoffset &= (hwidth << 3) - 1;

  const uint16_t cell =
      vram_word(ntbase + ((hoffset >> 3) + hwidth * (vmasked >> 3)) * 2);
  const uint32_t row = vmasked & 7;
  const uint32_t pattern =
      ((cell & 0x7FF) << 5) + ((cell & 0x1000) ? (7 - row) : row) * 4;
  return pattern_pixel(m_vram, cell, pattern, hoffset & 7);
}

Vdp::Px Vdp::window_pixel(uint32_t line, uint32_t x) const
{
  const uint32_t ntbase = (m_reg[3] & 0x3E) << 10;
  const uint16_t cell = vram_word(ntbase + ((x >> 3) + 40 * (line >> 3)) * 2);
  const uint32_t row = line & 7;
  const uint32_t pattern =
      ((cell & 0x7FF) << 5) + ((cell & 0x1000) ? (7 - row) : row) * 4;
  return pattern_pixel(m_vram, cell, pattern, x & 7);
}

void Vdp::render_sprites(uint32_t line, Px *out) const
{
  const uint32_t table = (m_reg[5] & 0x7F) << 9;
  const uint32_t maxspl = h40() ? 20 : 16;
  const int32_t budget0 = h40() ? 40 : 32;

  struct Sp {
    uint32_t entry;
    int32_t y, x, w, h;
  };
  Sp list[128];
  uint32_t count = 0;
  int32_t budget = budget0;

  uint32_t entry = table;
  for (uint32_t guard = 0; guard < 255 && count < maxspl; guard++) {
    const int32_t y = (int32_t)(vram_word(entry) & 0x1FF) - 0x80;
    /* Second word: the size nibble is the high byte (bits 11-10 width,
     * bits 9-8 height, both in cells minus one) and the link is the low
     * seven bits. Reading the size from the low byte takes it out of the
     * link field, so every multi-cell sprite comes out the wrong shape. */
    const uint16_t sz = vram_word(entry + 2);
    const int32_t h = 1 + ((sz >> 8) & 3);
    const int32_t w = 1 + ((sz >> 10) & 3);
    const int32_t x = (int32_t)(vram_word(entry + 6) & 0x1FF) - 0x80;
    const bool on_line = (int32_t)line >= y && (int32_t)line < y + h * 8;
    if (on_line && budget > 0) {
      list[count++] = {entry, y, x, w, h};
      budget -= w;
    } else if (x == -128 && on_line) {
      /* mask sprite: a sprite at x=-128 whose vertical range covers
       * this line stops the plotting of lower-priority sprites
       * (legacy semantics, itself approximate) */
      break;
    } else if (x == -128 && count > 0 && (int32_t)line >= list[count - 1].y &&
               (int32_t)line < list[count - 1].y + list[count - 1].h * 8) {
      break;
    }
    const uint32_t link = sz & 0x7F;
    if (link == 0) {
      break;
    }
    entry = table + link * 8;
  }

  /* plot back to front: earlier list entries overwrite later ones */
  for (uint32_t s = count; s-- > 0;) {
    const Sp &sp = list[s];
    const uint16_t tile = vram_word(sp.entry + 4);
    const uint32_t base = (tile & 0x7FF) << 5;
    const uint32_t row = (uint32_t)((int32_t)line - sp.y);
    const uint32_t rowoff =
        (tile & 0x1000) ? (sp.h * 8 - 1 - row) * 4 : row * 4;
    for (int32_t c = 0; c < sp.w; c++) {
      const int32_t col = (tile & 0x800) ? sp.w - 1 - c : c;
      const uint32_t pattern =
          (base + (uint32_t)col * (sp.h << 5) + rowoff) & 0xFFFF;
      for (int32_t px = 0; px < 8; px++) {
        const int32_t sx = sp.x + c * 8 + px;
        if (sx < 0 || sx >= (int32_t)kLineBufferWidth) {
          continue;
        }
        const uint32_t nib = (tile & 0x800) ? (uint32_t)(7 - px) : (uint32_t)px;
        const uint32_t shift = (7 - nib) * 4;
        const uint32_t data = (uint32_t)m_vram[pattern] << 24 |
                              (uint32_t)m_vram[(pattern + 1) & 0xFFFF] << 16 |
                              (uint32_t)m_vram[(pattern + 2) & 0xFFFF] << 8 |
                              (uint32_t)m_vram[(pattern + 3) & 0xFFFF];
        const uint8_t pixel = (data >> shift) & 0xF;
        if (pixel != 0) {
          out[sx].value = (uint8_t)((((tile >> 13) & 3) << 4) | pixel);
          out[sx].prio = (uint8_t)(tile >> 15);
        }
      }
    }
  }
}

void Vdp::render_line(uint32_t line)
{
  uint8_t *dst = m_frame.data() + (size_t)line * kLineBufferWidth;
  const uint8_t backdrop = m_reg[7] & 63;
  const uint32_t width = visible_width();

  if ((m_reg[1] & 0x40) == 0) { /* display disabled */
    std::memset(dst, backdrop, kLineBufferWidth);
    return;
  }

  Px sprite_px[kLineBufferWidth];
  std::memset(sprite_px, 0, sizeof(sprite_px));
  render_sprites(line, sprite_px);

  /* window band for this line */
  const uint32_t vcell = line >> 3;
  const bool win_v = (m_reg[18] & 0x80) ? (vcell >= (m_reg[18] & 0x1F))
                                        : (vcell < (m_reg[18] & 0x1F));
  const bool win_right = (m_reg[17] & 0x80) != 0;
  const uint32_t win_h = (m_reg[17] & 0x1F) * 16;

  for (uint32_t x = 0; x < kLineBufferWidth; x++) {
    const bool in_window =
        win_v && x < width && (win_right ? x >= win_h : x < win_h);
    const Px a = in_window ? window_pixel(line, x)
                           : (x < width ? layer_pixel(0, line, x) : Px{0, 0});
    const Px b = (x < width && !in_window) ? layer_pixel(1, line, x) : Px{0, 0};
    const Px s = x < width ? sprite_px[x] : Px{0, 0};

    /* per-pixel priority: tier = priority bit; within a tier the order
     * is sprite > plane A > plane B (YM7101 doc + legacy case table) */
    int best = -1;
    uint8_t value = backdrop;
    const auto consider = [&](const Px &p, int kind) {
      if (p.value == 0) {
        return;
      }
      const int score = ((int)p.prio << 2) | kind;
      if (score > best) {
        best = score;
        value = p.value;
      }
    };
    consider(s, 3);
    consider(a, 2);
    consider(b, 1);
    dst[x] = value;
  }
}

}  // namespace generator
