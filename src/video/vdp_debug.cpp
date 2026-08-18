/* SPDX-License-Identifier: GPL-2.0-or-later */

/* video display processor emulation - state dumpers */

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

/* Human-readable dumps of the register file, the sprite list and the layer
   configuration. Called from the debugger and the console UI; not on any
   rendering path. */

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


void Vdp::vdp_showregs(void)
{
  int i;

  for (i = 0; i < 25; i++) {
    printf("[%02d] %02X: ", i, vdp_reg[i]);
    switch (i) {
    case 0:
      printf("%s ", vdp_reg[0] & 1 << 1 ? "HV-stop" : "HV-enable");
      printf("%s ", vdp_reg[0] & 1 << 4 ? "HInt-enable" : "HInt-disable");
      break;
    case 1:
      printf("%s ", vdp_reg[1] & 1 << 3 ? "30-cell" : "28-cell");
      printf("%s ", vdp_reg[1] & 1 << 4 ? "DMA-enable" : "DMA-disable");
      printf("%s ", vdp_reg[1] & 1 << 5 ? "VInt-enable" : "VInt-disable");
      printf("%s ", vdp_reg[1] & 1 << 6 ? "Disp-enable" : "Disp-disable");
      break;
    case 2:
      printf("Scroll A @ %04X", (vdp_reg[2] & 0x38) << 10);
      break;
    case 3:
      printf("Window @ %04X", (vdp_reg[3] & 0x3E) << 10);
      break;
    case 4:
      printf("Scroll B @ %04X", (vdp_reg[4] & 7) << 13);
      break;
    case 5:
      printf("Sprites @ %04X", (vdp_reg[5] & 0x7F) << 9);
      break;
    case 7:
      printf("bgpal %d col %d", (vdp_reg[7] >> 4 & 3), (vdp_reg[7] & 15));
      break;
    case 10:
      printf("hintreg %04X", vdp_reg[10]);
      break;
    case 11:
      printf("V-mode %d H-mode %d ", (vdp_reg[11] >> 2) & 1, (vdp_reg[11] & 3));
      printf("%s", (vdp_reg[11] & 1 << 3) ? "ExtInt-enable" : "ExtInt-disable");
      break;
    case 12:
      printf("Interlace %d ", (vdp_reg[12] >> 1) & 3);
      printf("%s ", (vdp_reg[12] & 1 << 0) ? "40-cell" : "32-cell");
      printf("%s ",
             (vdp_reg[12] & 1 << 3) ? "Shadow-enable" : "Shadow-disable");
      break;
    case 13:
      printf("Scroll A @ %04X", (vdp_reg[13] & 0x3F) << 10);
      break;
    case 15:
      printf("Autoinc %d", vdp_reg[15]);
      break;
    case 16:
      printf("Vsize %d Hsize %d", (vdp_reg[16] >> 4) & 3, (vdp_reg[16] & 3));
      break;
    case 17:
      printf("Window H %s ", (vdp_reg[17] & 1 << 7) ? "right" : "left");
      printf("%d", vdp_reg[17] & 0x1F);
      break;
    case 18:
      printf("Window V %s ", (vdp_reg[18] & 1 << 7) ? "lower" : "upper");
      printf("%d", vdp_reg[18] & 0x1F);
      break;
    case 19:
      printf("DMA-length-low %02X", vdp_reg[19]);
      break;
    case 20:
      printf("DMA-length-high %02X", vdp_reg[20]);
      break;
    case 21:
      printf("DMA-source-low %02X", vdp_reg[21]);
      break;
    case 22:
      printf("DMA-source-mid %02X", vdp_reg[22]);
      break;
    case 23:
      printf("DMA-source-high %02X", vdp_reg[23]);
      break;
    }
    printf("\n");
  }
  printf("Cur hpos = %02X\n", vdp_gethpos());
  printf("Cur line = %02X (NB: could be +1 after h-int)\n", vdp_line);
}

void Vdp::vdp_spritelist(void)
{
  uint8 *spritelist = vdp_vram + ((vdp_reg[5] & 0x7F) << 9);
  uint8 *sprite;
  uint8 link = 0;
  uint16 pattern;
  uint8 palette;
  uint16 cellinfo;
  sint16 vpos, hpos, vmax;
  uint8 vsize, hsize;

  LOG_REQUEST("SPRITE DUMP: (base=vram+%X)", (vdp_reg[5] & 0x7f) << 9);
  do {
    sprite = spritelist + (link << 3);
    hpos = (LOCENDIAN16(*(uint16 *)(sprite + 6)) & 0x1FF) - 0x80;
    vpos = (LOCENDIAN16(*(uint16 *)(sprite)) & 0x3FF) - 0x80;
    vsize = 1 + (sprite[2] & 3);
    hsize = 1 + ((sprite[2] >> 2) & 3);
    cellinfo = LOCENDIAN16(*(uint16 *)(sprite + 4));
    pattern = cellinfo & 0x7FF;
    palette = (cellinfo >> 13) & 3;
    vmax = vpos + vsize * 8;

    LOG_REQUEST(
        "Sprite %d @ %X", link, (link << 3) | (vdp_reg[5] & 0x7f) << 9);
    LOG_REQUEST("  Pos:  %d,%d", hpos, vpos);
    LOG_REQUEST("  Size: %d,%d", hsize, vsize);
    LOG_REQUEST("  Pri: %d, Pal: %d, Vflip: %d, Hflip: %d",
                 (cellinfo >> 15 & 1), (cellinfo >> 13 & 3),
                 (cellinfo >> 12 & 1), (cellinfo >> 11 & 1));
    LOG_REQUEST("  Pattern: %d (%x) @ vram+%X (%X if interlaced)",
                 (cellinfo & 0x7FF), (cellinfo & 0x7FF),
                 (cellinfo & 0x7FF) * 32, (cellinfo & 0x7FF) * 32);
    link = sprite[3] & 0x7F;
  } while (link);
}

void Vdp::vdp_describe(void)
{
  int layer;
  unsigned int line;
  uint32 o_patterndata, o_hscrolldata;
  uint16 *patterndata, *hscrolldata;
  uint8 hsize = vdp_reg[16] & 3;
  uint8 vsize = (vdp_reg[16] >> 4) & 3;
  uint8 hmode = vdp_reg[11] & 3;
  uint8 vmode = (vdp_reg[11] >> 2) & 1;
  uint16 hwidth, vwidth, hoffset, voffset, raw_hoffset;

  hwidth = 32 + hsize * 32;
  vwidth = 32 + vsize * 32;
  LOG_REQUEST("VDP description:");
  LOG_REQUEST("  hsize = %d (ie. width=%d)", hsize, hwidth);
  LOG_REQUEST("  vsize = %d (ie. width=%d)", vsize, vwidth);
  LOG_REQUEST("  hmode = %d (0=full, 2=cell, 3=line)", hmode);
  LOG_REQUEST("  vmode = %d (0=full, 1=2cell", vmode);

  for (layer = 0; layer < 2; layer++) {
    LOG_REQUEST("  Layer %s:", layer == 0 ? "A" : "B");
    o_patterndata =
        (layer == 0 ? ((vdp_reg[2] & 0x38) << 10) : ((vdp_reg[4] & 7) << 13));
    o_hscrolldata = layer * 2 + ((vdp_reg[13] & 63) << 10);
    LOG_REQUEST("    Pattern data @ vram+%08X", o_patterndata);
    LOG_REQUEST("    Hscroll data @ vram+%08X", o_hscrolldata);
    patterndata = (uint16 *)(vdp_vram + o_patterndata);
    hscrolldata = (uint16 *)(vdp_vram + o_hscrolldata);
    for (line = 0; line < vdp_vislines; line++) {
      switch (hmode) {
      case 0: /* full screen */
        hoffset = (0x400 - LOCENDIAN16(hscrolldata[0])) & 0x3FF;
        break;
      case 1: /* line scroll with first 8 lines */
        hoffset = (0x400 - LOCENDIAN16(hscrolldata[2 * (line & 7)])) & 0x3FF;
        break;
      case 2: /* cell scroll */
        hoffset = (0x400 - LOCENDIAN16(hscrolldata[2 * (line & ~7)])) & 0x3FF;
        break;
      case 3: /* line scroll */
        hoffset = (0x400 - LOCENDIAN16(hscrolldata[2 * line])) & 0x3FF;
        break;
      default:
        hoffset = 0;
        break;
      }
      raw_hoffset = hoffset;
      hoffset &= (hwidth << 8) - 1; /* put offset in range */
      voffset = (line + LOCENDIAN16(((uint16 *)vdp_vsram)[layer])) & 0x3FF;
      voffset &= (vwidth << 8) - 1; /* put offset in range */
      LOG_REQUEST(
          "     line %d: hoffset=%d=%d, voffset=%d, "
           "firstcell=vram+%08X",
           line, raw_hoffset, hoffset, voffset,
           o_patterndata + 2 * ((hoffset >> 3) + hwidth * (voffset >> 3)));
    }
  }
}
}  // namespace generator
