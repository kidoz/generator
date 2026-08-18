/* SPDX-License-Identifier: GPL-2.0-or-later */

/* video display processor emulation - cell-based plotter */

/* One of the four translation units making up generator::Vdp. The chip was
 * a single 2,092-line file; the split is by responsibility and moved no
 * code:
 *
 *   vdp.cpp             ports, DMA, FIFO, reset and frame timing (this file)
 *   vdp_render.cpp      the raster renderer (sprites, layers, window)
 *   vdp_render_cell.cpp the cell-based "simple" plotter
 *   vdp_debug.cpp       the register/sprite/layer dumpers
 *
 * All four define members of the same class; the state lives in vdp.hpp. */

/* The "simple" renderer: plots whole 8x8 cells rather than compositing per
   pixel. Faster than the raster path in vdp_render.cpp and correspondingly
   less accurate; selected by the console UI's ui_vdpsimple. */

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "generator.h"
#include "vdp.h"
#include "cpu68k.h"
#include "ui.h"
#include "event.h"
#include "state.h"

#include "vdp.hpp"

#undef DEBUG_VDP
#undef DEBUG_VDPDMA
#undef DEBUG_VDPDATA
#undef DEBUG_VDPCRAM

namespace generator {


/* C17 migration: removed 'inline' to provide external linkage */
void Vdp::vdp_plotcell(uint8 *patloc, uint8 palette, uint8 flags, uint8 *cellloc,
                  unsigned int lineoffset)
{
  int y, x;
  uint8 value;
  uint32 data;

  switch (flags) {
  case 0:
    /* normal tile - no s/ten */
    for (y = 0; y < 8; y++, cellloc += lineoffset) {
      data = LOCENDIAN32(((uint32 *)patloc)[y]);
      for (x = 0; x < 8; x++, data <<= 4) {
        value = data >> 28;
        if (value)
          cellloc[x] = palette * 16 + value;
      }
    }
    break;
  case 1:
    /* h flipped tile - no s/ten */
    for (y = 0; y < 8; y++, cellloc += lineoffset) {
      data = LOCENDIAN32(((uint32 *)patloc)[y]);
      for (x = 0; x < 8; x++, data >>= 4) {
        value = data & 15;
        if (value)
          cellloc[x] = palette * 16 + value;
      }
    }
    break;
  case 2:
    /* v flipped tile - no s/ten */
    for (y = 0; y < 8; y++, cellloc += lineoffset) {
      data = LOCENDIAN32(((uint32 *)patloc)[7 - y]);
      for (x = 0; x < 8; x++, data <<= 4) {
        value = data >> 28;
        if (value)
          cellloc[x] = palette * 16 + value;
      }
    }
    break;
  case 3:
    /* h and v flipped tile - no s/ten */
    for (y = 0; y < 8; y++, cellloc += lineoffset) {
      data = LOCENDIAN32(((uint32 *)patloc)[7 - y]);
      for (x = 0; x < 8; x++, data >>= 4) {
        value = data & 15;
        if (value)
          cellloc[x] = palette * 16 + value;
      }
    }
    break;
  case 4:
    /* normal tile - s/ten enabled */
    for (y = 0; y < 8; y++, cellloc += lineoffset) {
      data = LOCENDIAN32(((uint32 *)patloc)[y]);
      for (x = 0; x < 8; x++, data <<= 4) {
        value = data >> 28;
        if (value) {
          if (palette == 3 && value == 14) {
            cellloc[x] = (cellloc[x] & 63) + 64;
          } else if (palette == 3 && value == 15) {
            cellloc[x] = (cellloc[x] & 63) + 128;
          } else {
            cellloc[x] = palette * 16 + value;
          }
        }
      }
    }
    break;
  case 5:
    /* h flipped tile - s/ten */
    for (y = 0; y < 8; y++, cellloc += lineoffset) {
      data = LOCENDIAN32(((uint32 *)patloc)[y]);
      for (x = 0; x < 8; x++, data >>= 4) {
        value = data & 15;
        if (value) {
          if (palette == 3 && value == 14) {
            cellloc[x] = (cellloc[x] & 63) + 64;
          } else if (palette == 3 && value == 15) {
            cellloc[x] = (cellloc[x] & 63) + 128;
          } else {
            cellloc[x] = palette * 16 + value;
          }
        }
      }
    }
    break;
  case 6:
    /* v flipped tile - s/ten enabled */
    for (y = 0; y < 8; y++, cellloc += lineoffset) {
      data = LOCENDIAN32(((uint32 *)patloc)[7 - y]);
      for (x = 0; x < 8; x++, data <<= 4) {
        value = data >> 28;
        if (value) {
          if (palette == 3 && value == 14) {
            cellloc[x] = (cellloc[x] & 63) + 64;
          } else if (palette == 3 && value == 15) {
            cellloc[x] = (cellloc[x] & 63) + 128;
          } else {
            cellloc[x] = palette * 16 + value;
          }
        }
      }
    }
    break;
  case 7:
    /* h and v flipped tile - s/ten enabled */
    for (y = 0; y < 8; y++, cellloc += lineoffset) {
      data = LOCENDIAN32(((uint32 *)patloc)[7 - y]);
      for (x = 0; x < 8; x++, data >>= 4) {
        value = data & 15;
        if (value) {
          if (palette == 3 && value == 14) {
            cellloc[x] = (cellloc[x] & 63) + 64;
          } else if (palette == 3 && value == 15) {
            cellloc[x] = (cellloc[x] & 63) + 128;
          } else {
            cellloc[x] = palette * 16 + value;
          }
        }
      }
    }
    break;
  default:
    ui_err("Unknown plotcell flags");
  }
}

/* must be 8*lineoffset bytes scrap before and after fielddata and also
   8 bytes before each line and 8 bytes after each line */

void Vdp::vdp_layer_simple(unsigned int layer, unsigned int priority,
                      uint8 *framedata, unsigned int lineoffset)
{
  uint8 hsize = vdp_reg[16] & 3;
  uint8 vsize = (vdp_reg[16] >> 4) & 3;
  uint8 hmode = vdp_reg[11] & 3;
  uint8 vmode = (vdp_reg[11] >> 2) & 1;
  uint16 vramoffset =
      (layer ? ((vdp_reg[4] & 7) << 13) : ((vdp_reg[2] & (7 << 3)) << 10));
  uint16 *patterndata = (uint16 *)(vdp_vram + vramoffset);
  uint16 *hscrolldata =
      (uint16 *)(((vdp_reg[13] & 63) << 10) + vdp_vram + layer * 2);
  uint8 screencells = (vdp_reg[12] & 1) ? 40 : 32;
  uint16 hwidth = 32 + hsize * 32;
  uint16 vwidth = 32 + vsize * 32;
  uint16 hoffset, voffset;
  uint16 cellinfo;
  uint8 *pattern;
  uint8 palette;
  unsigned int xcell, ycell;
  uint8 *toploc, *cellloc;
  uint8 flags;
  uint32 hscroll, vscroll;

  if (layer == 0 &&
      ((vdp_reg[18] == 0x9F && vdp_reg[17] == 0x9F) ||
       (vdp_reg[18] == 0x9F && vdp_reg[17] == 0x80) || (vdp_reg[18] == 0x80) ||
       (vdp_reg[18] == 0x00 && vdp_reg[17] > (screencells << 1))))
    /* quick hack to remove layer A when it definitely shouldn't be plotted */
    return;

  for (xcell = 0; xcell <= screencells; xcell++) {
    if (vmode) {
      /* 2-cell scroll */
      vscroll = ((xcell >= screencells ? xcell - 2 : xcell) & ~1) + layer;
    } else {
      /* full screen */
      vscroll = layer;
    }
    voffset = LOCENDIAN16(((uint16 *)vdp_vsram)[vscroll]) & 0x3FF;
    toploc = framedata - lineoffset * (voffset & 7);
    for (ycell = 0; ycell <= 28;
         ycell++, voffset += 8, toploc += lineoffset * 8) {
      switch (hmode) {
      case 0: /* full screen */
      case 1: /* line scroll (first 8 lines) - approximation */
        hscroll = 0;
        break;
      case 2: /* cell scroll */
        hscroll = 2 * (ycell >= 28 ? ycell - 2 : ycell) * 8;
        break;
      case 3: /* line scroll - approximation */
        hscroll = 2 * (ycell >= 28 ? ycell - 2 : ycell) * 8;
        vdp_complex = 1;
        break;
      default:
        hscroll = 0;
        break;
      }
      voffset &= (vwidth * 8) - 1;
      hoffset = (0x400 - LOCENDIAN16(hscrolldata[hscroll])) & 0x3FF;
      hoffset = (hoffset + xcell * 8) & ((hwidth * 8) - 1);
      cellinfo =
          LOCENDIAN16(patterndata[(hoffset >> 3) + hwidth * (voffset >> 3)]);
      if (((uint8)((cellinfo & 1 << 15) ? 1 : 0)) == priority) {
        /* plot cell */
        palette = (cellinfo >> 13) & 3;
        pattern = vdp_vram + ((cellinfo & 2047) << 5);
        flags = (cellinfo >> 11) & 3; /* bit0=H flip, bit1=V flip */
        cellloc = toploc - (hoffset & 7) + xcell * 8;
        vdp_plotcell(pattern, palette, flags, cellloc, lineoffset);
      }
    } /* ycell */
  } /* xcell */
}

void Vdp::vdp_shadow_simple(uint8 *framedata, unsigned int lineoffset)
{
  unsigned int vertcells = vdp_reg[1] & 1 << 3 ? 30 : 28;
  uint8 *linedata;
  unsigned int line;
  int i;

  for (line = 0; line < vertcells * 8; line++) {
    linedata = framedata + line * lineoffset;
    /* this could be done 4 bytes at a time */
    for (i = 0; i < 320; i++)
      linedata[i] = (linedata[i] & 63) + 128;
  }
}

void Vdp::vdp_sprites_simple(unsigned int priority, uint8 *framedata,
                        unsigned int lineoffset)
{
  uint8 *spritelist = vdp_vram + ((vdp_reg[5] & 0x7F) << 9);

  vdp_sprite_simple(priority, framedata, lineoffset, 1, spritelist, spritelist);
}

int Vdp::vdp_sprite_simple(unsigned int priority, uint8 *framedata,
                      unsigned int lineoffset, unsigned int number,
                      uint8 *spritelist, uint8 *sprite)
{
  int plotted = 1;
  uint8 link;
  uint16 pattern;
  uint8 palette;
  uint16 cellinfo;
  sint16 vpos, hpos, vmax;
  uint16 xcell, ycell;
  uint8 vsize, hsize;
  uint8 *cellloc;
  uint8 flags;
  uint8 *patloc;

  if (number > 80) {
    LOG_VERBOSE("%08X [VDP] Maximum of 80 sprites exceeded", regs.pc);
    return 0;
  }

  link = sprite[3] & 0x7F;
  hpos = (LOCENDIAN16(*(uint16 *)(sprite + 6)) & 0x1FF) - 0x80;
  vpos = (LOCENDIAN16(*(uint16 *)(sprite)) & 0x3FF) - 0x80;
  vsize = 1 + (sprite[2] & 3);
  hsize = 1 + ((sprite[2] >> 2) & 3);
  cellinfo = LOCENDIAN16(*(uint16 *)(sprite + 4));
  pattern = cellinfo & 0x7FF;
  palette = (cellinfo >> 13) & 3;
  vmax = vpos + vsize * 8;

  if (link) {
    if (hpos == -128)
      /* we do not support 'masking' in simple mode */
      vdp_complex = 1;
    plotted = vdp_sprite_simple(priority, framedata, lineoffset, number + 1,
                                spritelist, spritelist + (link << 3));
    plotted++;
  }

  if (((uint8)((cellinfo & 1 << 15) ? 1 : 0)) != priority)
    return plotted;
  if (vpos >= 240 || hpos >= 320 || vpos + vsize * 8 <= 0 ||
      hpos + hsize * 8 <= 0)
    /* sprite is not on screen */
    return plotted;
  flags = (cellinfo >> 11) & 3; /* bit0=H flip, bit1=V flip */
  if (vdp_reg[12] & 1 << 3)     /* s/ten enabled? */
    flags |= 1 << 2;
  switch (flags) {
  case 0:
  case 4:
    /* normal orientation */
    for (ycell = 0; ycell < vsize; ycell++) {
      if (ycell * 8 + vpos < -7 || ycell * 8 + vpos >= 240)
        /* cell out of plotting area (remember scrap area) */
        continue;
      patloc = vdp_vram + (pattern << 5) + ycell * 32;
      for (xcell = 0; xcell < hsize; xcell++, patloc += vsize * 32) {
        if (xcell * 8 + hpos < -7 || xcell * 8 + hpos >= 320)
          /* cell out of plotting area */
          continue;
        cellloc =
            framedata + ((vpos + ycell * 8) * lineoffset + (hpos + xcell * 8));
        vdp_plotcell(patloc, palette, flags, cellloc, lineoffset);
      }
    }
    break;
  case 1:
  case 5:
    /* H flip */
    for (ycell = 0; ycell < vsize; ycell++) {
      if (ycell * 8 + vpos < -7 || ycell * 8 + vpos >= 240)
        /* cell out of plotting area (remember scrap area) */
        continue;
      patloc =
          vdp_vram + (pattern << 5) + ycell * 32 + vsize * 32 * (hsize - 1);
      for (xcell = 0; xcell < hsize; xcell++, patloc -= vsize * 32) {
        if (xcell * 8 + hpos < -7 || xcell * 8 + hpos >= 320)
          /* cell out of plotting area */
          continue;
        cellloc =
            framedata + ((vpos + ycell * 8) * lineoffset + (hpos + xcell * 8));
        vdp_plotcell(patloc, palette, flags, cellloc, lineoffset);
      }
    }
    break;
  case 2:
  case 6:
    /* V flip */
    for (ycell = 0; ycell < vsize; ycell++) {
      if (ycell * 8 + vpos < -7 || ycell * 8 + vpos >= 240)
        /* cell out of plotting area (remember scrap area) */
        continue;
      patloc = vdp_vram + (pattern << 5) + (vsize - ycell - 1) * 32;
      for (xcell = 0; xcell < hsize; xcell++, patloc += vsize * 32) {
        if (xcell * 8 + hpos < -7 || xcell * 8 + hpos >= 320)
          /* cell out of plotting area */
          continue;
        cellloc =
            framedata + ((vpos + ycell * 8) * lineoffset + (hpos + xcell * 8));
        vdp_plotcell(patloc, palette, flags, cellloc, lineoffset);
      }
    }
    break;
  case 3:
  case 7:
    /* H and V flip */
    for (ycell = 0; ycell < vsize; ycell++) {
      if (ycell * 8 + vpos < -7 || ycell * 8 + vpos >= 240)
        /* cell out of plotting area (remember scrap area) */
        continue;
      patloc = vdp_vram + (pattern << 5) +
               ((vsize - ycell - 1) * 32 + vsize * 32 * (hsize - 1));
      for (xcell = 0; xcell < hsize; xcell++, patloc -= vsize * 32) {
        if (xcell * 8 + hpos < -7 || xcell * 8 + hpos >= 320)
          /* cell out of plotting area */
          continue;
        cellloc =
            framedata + ((vpos + ycell * 8) * lineoffset + (hpos + xcell * 8));
        vdp_plotcell(patloc, palette, flags, cellloc, lineoffset);
      }
    }
    break;
  }
  return plotted;
}
}  // namespace generator
