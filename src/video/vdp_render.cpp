/* SPDX-License-Identifier: GPL-2.0-or-later */

/* video display processor emulation - raster renderer */

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

/* Renders a scanline the way the hardware does: sprites and both scroll
   layers are composited per pixel through a priority mask. The cell-based
   plotter in vdp_render_cell.cpp is the faster, less accurate alternative. */

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

#define PRIBIT_LAYERB 0
#define PRIBIT_LAYERA 1
#define PRIBIT_SPRITE 2

#define LINEDATASPR(offset, value, palette, priority) \
  if (value) {                                        \
    if (outdata[offset] < 62)                         \
      vdp_collision = 1;                              \
    if (priority)                                     \
      pridata[offset] |= 1 << PRIBIT_SPRITE;          \
    else                                              \
      pridata[offset] &= ~(1 << PRIBIT_SPRITE);       \
    outdata[offset] = (palette) * 16 + value;         \
  }

#define LINEDATALAYER(offset, value, palette, priority)                 \
  if (priority)                                                         \
    pridata[offset] |= layer ? 1 << PRIBIT_LAYERB : 1 << PRIBIT_LAYERA; \
  if (value)                                                            \
    outdata[offset] = (palette) * 16 + value;                           \
  else                                                                  \
    outdata[offset] = 0;

void Vdp::vdp_sprites(unsigned int line, uint8 *pridata, uint8 *outdata)
{
  uint8 interlace = (vdp_reg[12] >> 1) & 3;
  uint8 *spritelist = vdp_vram + ((vdp_reg[5] & 0x7F) << 9);
  t_spriteinfo si[128]; /* 128 - max sprites supported per line */
  unsigned int sprites;
  unsigned int idx;
  int i;
  uint8 link;
  uint8 *sprite = spritelist;
  int plotter; /* flag */
  unsigned int screencells = (vdp_reg[12] & 1) ? 40 : 32;
  unsigned int maxspl = (vdp_reg[12] & 1) ? 20 : 16; /* max sprs/line */
  unsigned int cells;
  uint8 loops = 0;

  for (idx = 0; loops < 255 && idx < maxspl; loops++) {
    link = sprite[3] & 0x7F;
    si[idx].sprite = sprite;
    if (interlace == 3)
      si[idx].vpos = (LOCENDIAN16(*(uint16 *)(sprite)) & 0x3FF) - 0x100;
    else
      si[idx].vpos = (LOCENDIAN16(*(uint16 *)(sprite)) & 0x1FF) - 0x080;
    if ((signed int)line < si[idx].vpos)
      goto next;
    si[idx].vsize = 1 + (sprite[2] & 3);
    if (interlace == 3)
      si[idx].vsize <<= 1;
    si[idx].vmax = si[idx].vpos + si[idx].vsize * 8;
    if ((signed int)line >= si[idx].vmax)
      goto next;
    si[idx].hpos = (LOCENDIAN16(*(uint16 *)(sprite + 6)) & 0x1FF) - 0x80;
    si[idx].hsize = 1 + ((sprite[2] >> 2) & 3);
    si[idx].hplot = si[idx].hsize;
    si[idx].hmax = si[idx].hpos + si[idx].hsize * 8;
    idx++;
  next:
    if (!link)
      break;
    sprite = spritelist + (link << 3);
  }
  if (idx < 1)
    return;
  sprites = idx;
  plotter = 1;
  cells = (vdp_reg[12] & 1) ? 40 : 32; /* 320 or 256 pixels */
  /* loop masking */
  for (i = 0; i < (signed int)sprites; i++) {
    if (plotter == 0) {
      sprites = i;
      break;
    }
    if (si[i].hpos == -128) {
      /* mask sprite - but does it? */
      if (i > 0) {
        /* is there a higher priority sprite? */
        if (si[i - 1].vpos <= (signed int)line &&
            si[i - 1].vmax > (signed int)line) {
          /* match, mask time */
          plotter = 0;
        }
      } else {
        /* higest priority sprite, so mask */
        plotter = 0;
      }
      si[i].hplot = 0;
    }
    if (si[i].hpos == -127) {
      LOG_VERBOSE("Warning: Use of hpos = 1 in plotter");
      plotter = 1;
    }
    if (cells >= si[i].hplot) {
      cells -= si[i].hplot;
    } else {
      si[i].hplot = cells; /* only room for this many */
      cells = 0;
    }
  }

  {
    sint16 hpos, vpos;
    uint16 hsize, vsize, hplot;
    sint16 hmax, vmax;
    uint16 cellinfo;
    uint16 pattern;
    uint8 palette;
    uint8 *cellline;
    uint32 data;
    uint8 pixel;
    uint8 priority;
    int j, k;

    /* loop around sprites until end of list marker or no more */
    for (i = sprites - 1; i >= 0; i--) {
      hpos = si[i].hpos;
      vpos = si[i].vpos;
      vsize = si[i].vsize; /* doubled when in interlace mode */
      hsize = si[i].hsize;
      hmax = si[i].hmax;
      vmax = si[i].vmax;
      hplot = si[i].hplot; /* number of cells to plot for this sprite */
      cellinfo = LOCENDIAN16(*(uint16 *)(si[i].sprite + 4));
      pattern = cellinfo & 0x7FF;
      palette = (cellinfo >> 13) & 3;
      priority = (cellinfo >> 15) & 1;

      cellline =
          vdp_vram + ((interlace == 3) ? (pattern << 6) : (pattern << 5));
      if (cellinfo & 1 << 12) /* vertical flip */
        cellline += (vmax - line - 1) * 4;
      else
        cellline += (line - vpos) * 4;
      for (k = 0; k < hsize && hplot--; k++) {
        if (hpos > -8 && hpos < 0) {
          if (cellinfo & 1 << 11) {
            /* horizontal flip */
            data = LOCENDIAN32(
                *(uint32 *)(cellline + (hsize - k * 2 - 1) * (vsize << 5)));
            data >>= (-hpos) * 4; /* get first pixel in bottom 4 bits */
            for (j = 0; j < 8 + hpos; j++) {
              pixel = data & 15;
              LINEDATASPR(j, pixel, palette, priority);
              data >>= 4;
            }
          } else {
            data = LOCENDIAN32(*(uint32 *)cellline);
            data <<= (-hpos) * 4; /* get first pixel in top 4 bits */
            for (j = 0; j < 8 + hpos; j++) {
              pixel = (data >> 28) & 15;
              LINEDATASPR(j, pixel, palette, priority);
              data <<= 4;
            }
          }
        } else if (hpos >= 0 && hpos <= (signed int)(screencells - 1) * 8) {
          if (cellinfo & 1 << 11) {
            /* horizontal flip */
            data = LOCENDIAN32(
                *(uint32 *)(cellline + (hsize - k * 2 - 1) * (vsize << 5)));
            for (j = 0; j < 8; j++) {
              pixel = data & 15;
              LINEDATASPR(hpos + j, pixel, palette, priority);
              data >>= 4;
            }
          } else {
            data = LOCENDIAN32(*(uint32 *)cellline);
            for (j = 0; j < 8; j++) {
              pixel = (data >> 28) & 15;
              LINEDATASPR(hpos + j, pixel, palette, priority);
              data <<= 4;
            }
          }
        } else if (hpos > (signed int)(screencells - 1) * 8 &&
                   hpos < (signed int)screencells * 8) {
          if (cellinfo & 1 << 11) {
            /* horizontal flip */
            data = LOCENDIAN32(
                *(uint32 *)(cellline + (hsize - k * 2 - 1) * (vsize << 5)));
            for (j = 0; j < (signed int)(screencells * 8 - hpos); j++) {
              pixel = data & 15;
              LINEDATASPR(hpos + j, pixel, palette, priority);
              data >>= 4;
            }
          } else {
            data = LOCENDIAN32(*(uint32 *)cellline);
            for (j = 0; j < (signed int)(screencells * 8 - hpos); j++) {
              pixel = (data >> 28) & 15;
              LINEDATASPR(hpos + j, pixel, palette, priority);
              data <<= 4;
            }
          }
        }
        cellline += vsize << 5; /* 32 bytes per cell (note vsize is doubled
                                   when interlaced) */
        hpos += 8;
      }
    }
  }
}

/* layer 0 = A (top), layer 1 = B (bottom) */

void Vdp::vdp_newlayer(unsigned int line, uint8 *pridata, uint8 *outdata,
                  unsigned int layer)
{
  int i, j;
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
  uint16 hwidth, vwidth, vmask, hoffset, voffset;
  uint16 cellinfo;
  uint8 *pattern;
  uint32 data;
  uint8 palette;
  uint8 pixel;
  uint8 priority;
  int interlace = (((vdp_reg[12] >> 1) & 3) == 3) ? 1 : 0;
  int realline = interlace ? line >> 1 : line;
  int column;

  /* window stuff */
  int vcell = realline >> 3;
  int topbottom = vdp_reg[18] & 0x80;
  int winvpos = vdp_reg[18] & 0x1f;
  int leftright = vdp_reg[17] & 0x80;
  int winhpos = vdp_reg[17] & 0x1f;
  int corrupted = 0;

  if (layer == 0) {
    /* sometimes the window covers the whole line, so we skip layer A for
       that bit - for lines where layer A appears even for just one cell we
       plot the entire line - would it be worth improving this? */

    if (topbottom ? (vcell >= winvpos) : (vcell < winvpos))
      return; /* window is on the whole line */

    /* it could be that the vdp is in such a state that there is no whole line
       part to the window, but reg[17] is either 00 or 1F, meaning that
       infact there is (this is one way of turning on/off window) in which
       case we really should optimise this case otherwise we could be
       drawing layer A pointlessly as window will just clobber all our work) */

    if (leftright ? (winhpos == 0) : (winhpos >= (screencells >> 1)))
      return; /* window is on the whole line */
  }

  /* select horizontal scrolling offset based on mode */

  switch (hmode) {
  case 0: /* full screen */
    hoffset = (0x400 - LOCENDIAN16(hscrolldata[0])) & 0x3FF;
    break;
  case 1: /* line scroll with first 8 lines */
    hoffset = (0x400 - LOCENDIAN16(hscrolldata[2 * (realline & 7)])) & 0x3FF;
    break;
  case 2: /* cell scroll */
    hoffset = (0x400 - LOCENDIAN16(hscrolldata[2 * (realline & ~7)])) & 0x3FF;
    break;
  case 3: /* line scroll */
    hoffset = (0x400 - LOCENDIAN16(hscrolldata[2 * realline])) & 0x3FF;
    break;
  default:
    hoffset = 0;
  }

  /* vsram works in two-column lumps, so if hoffset & 15 is 1-8 then
     the first of our columns is going to use a fixed hoffset of 0,
     if it's 9-15 then the first two of our columns are going to use
     a fixed hoffset of 0 - pengo, air diver */

  /* what column are we going to do first?  -2, -1 or 0 */

  column = (hoffset & 15) == 0 ? 0 : ((hoffset & 15) > 8 ? -2 : -1);

  /* to implement the bug in the VDP's window code, we check to see if
     the window is on the left (leftright is 0), that there really is a
     window (winhpos not 0, winhpos < max is checked earlier) and that
     there is a non-aligned horizontal offset - then we adjust the number
     of cells to plot (screencells), skip over the data we're not going
     to plot (outdata, pridata) and temporarily increase the hoffset */

  if (layer == 0 && !leftright && winhpos && (hoffset & 15)) {
    screencells -= winhpos;
    hoffset += 16; /* this is the corruption */
    hoffset += winhpos * 16;
    outdata += winhpos * 16;
    pridata += winhpos * 16;
    corrupted = (hoffset & 15) >= 8 ? 1 : 2; /* one lump or two? */
  }
  vwidth = ((vsize + 1) << 5) * (interlace + 1);
  vmask = (vwidth << 3) - 1; /* to put offsets into range */
  hwidth = (hsize + 1) << 5;
  /* if 2-cell mode and vsram not valid yet, use 0 as the offset */
  voffset =
      ((vmode && column < 0) ? 0 : LOCENDIAN16(((uint16 *)vdp_vsram)[layer]));
  voffset = (voffset + line) & vmask;
  hoffset &= (hwidth << 3) - 1; /* put offset in range */
  if (hsize == 2) {
    /* hsize=2 is special - only use top row in table */
    voffset &= 7;
    hwidth = 32;
  }
  cellinfo = LOCENDIAN16(
      patterndata[(hoffset >> 3) + hwidth * ((voffset >> 3) >> interlace)]);
  /* 32 bytes per pattern or 64 in interlace mode 2 */
  pattern = vdp_vram + (((cellinfo & 2047) << 5) << interlace);
  /* now get correct line from pattern data */
  if (interlace) {
    /* interlace - double height cells */
    if (cellinfo & 1 << 12) {
      /* vertical flip */
      pattern += 4 * (15 - (voffset & 15));
    } else {
      /* no vertical flip */
      pattern += 4 * (voffset & 15);
    }
  } else {
    /* no interlace */
    if (cellinfo & 1 << 12) {
      /* vertical flip */
      pattern += 4 * (7 - (voffset & 7));
    } else {
      /* no vertical flip */
      pattern += 4 * (voffset & 7);
    }
  }
  priority = (cellinfo >> 15) & 1;
  palette = (cellinfo >> 13) & 3;
  data = LOCENDIAN32(*(uint32 *)pattern);
  if (cellinfo & 1 << 11) {
    /* horizontal flip */
    data >>= (hoffset & 7) * 4; /* get first pixel in bottom 4 bits */
    for (i = 0; i < 8 - (hoffset & 7); i++) {
      pixel = data & 15;
      LINEDATALAYER(i, pixel, palette, priority);
      data >>= 4;
    }
  } else {
    data <<= (hoffset & 7) * 4; /* get first pixel in top 4 bits */
    for (i = 0; i < 8 - (hoffset & 7); i++) {
      pixel = (data >> 28) & 15;
      LINEDATALAYER(i, pixel, palette, priority);
      data <<= 4;
    }
  }
  outdata += 8 - (hoffset & 7);
  pridata += 8 - (hoffset & 7);
  hoffset += 8;
  if (--corrupted == 0)
    hoffset -= 16;              /* turn off corruption */
  hoffset &= (hwidth << 3) - 1; /* put offset in range */
  column++;
  for (j = 1; j < screencells; j++) {
    if (vmode) {
      /* 2-cell scroll */
      if (column >= 0)
        voffset = LOCENDIAN16(((uint16 *)vdp_vsram)[(column & ~1) + layer]);
      else
        voffset = 0;
    } else {
      /* full screen */
      voffset = LOCENDIAN16(((uint16 *)vdp_vsram)[layer]);
    }
    voffset = (voffset + line) & vmask;
    if (hsize == 2)
      /* hsize=2 is special - only use top row in table */
      voffset &= 7;
    cellinfo = LOCENDIAN16(
        patterndata[(hoffset >> 3) + hwidth * ((voffset >> 3) >> interlace)]);
    priority = (cellinfo >> 15) & 1;
    /* printf("hoff: %04X voff: %04X hwid: %04X cell: %08X info: %04X\n",
       hoffset, voffset, hwidth, (hoffset>>3)+hwidth*(voffset>>3),
       cellinfo); */
    /* printf("loc %08X cellinfo %08X\n",
       (hoffset>>3)+hwidth*(voffset>>3),
       cellinfo); */
    palette = (cellinfo >> 13) & 3;
    /* 32 bytes per pattern or 64 in interlace mode 2 */
    pattern = vdp_vram + (((cellinfo & 2047) << 5) << interlace);
    /* now get correct line from pattern data */
    if (interlace) {
      /* interlace - double height cells */
      if (cellinfo & 1 << 12) {
        /* vertical flip */
        pattern += 4 * (15 - (voffset & 15));
      } else {
        /* no vertical flip */
        pattern += 4 * (voffset & 15);
      }
    } else {
      /* no interlace */
      if (cellinfo & 1 << 12) {
        /* vertical flip */
        pattern += 4 * (7 - (voffset & 7));
      } else {
        /* no vertical flip */
        pattern += 4 * (voffset & 7);
      }
    }
    data = LOCENDIAN32(*(uint32 *)pattern);
    if (cellinfo & 1 << 11) {
      /* horizontal flip */
      for (i = 0; i < 8; i++) {
        pixel = data & 15;
        LINEDATALAYER(i, pixel, palette, priority);
        data >>= 4;
      }
    } else {
      for (i = 0; i < 8; i++) {
        pixel = (data >> 28) & 15;
        LINEDATALAYER(i, pixel, palette, priority);
        data <<= 4;
      }
    }
    outdata += 8;
    pridata += 8;
    hoffset += 8;
    if (--corrupted == 0)
      hoffset -= 16;              /* turn off corruption */
    hoffset &= (hwidth << 3) - 1; /* put offset in range */
    column++;
  }
  if (hoffset & 7) {
    if (vmode) {
      /* 2-cell scroll */
      if (column >= 0)
        voffset = LOCENDIAN16(((uint16 *)vdp_vsram)[(column & ~1) + layer]);
      else
        voffset = 0;
    } else {
      /* full screen */
      voffset = LOCENDIAN16(((uint16 *)vdp_vsram)[layer]);
    }
    voffset = (voffset + line) & vmask;
    if (hsize == 2)
      /* hsize=2 is special - only use top row in table */
      voffset &= 7;
    cellinfo = LOCENDIAN16(
        patterndata[(hoffset >> 3) + hwidth * ((voffset >> 3) >> interlace)]);
    priority = (cellinfo >> 15) & 1;
    palette = (cellinfo >> 13) & 3;
    /* 32 bytes per pattern or 64 in interlace mode 2 */
    pattern = vdp_vram + (((cellinfo & 2047) << 5) << interlace);
    /* now get correct line from pattern data */
    if (interlace) {
      /* interlace - double height cells */
      if (cellinfo & 1 << 12) {
        /* vertical flip */
        pattern += 4 * (15 - (voffset & 15));
      } else {
        /* no vertical flip */
        pattern += 4 * (voffset & 15);
      }
    } else {
      /* no interlace */
      if (cellinfo & 1 << 12) {
        /* vertical flip */
        pattern += 4 * (7 - (voffset & 7));
      } else {
        /* no vertical flip */
        pattern += 4 * (voffset & 7);
      }
    }
    data = LOCENDIAN32(*(uint32 *)pattern);
    if (cellinfo & 1 << 11) {
      /* horizontal flip */
      for (i = 0; i < (hoffset & 7); i++) {
        pixel = data & 15;
        LINEDATALAYER(i, pixel, palette, priority);
        data >>= 4;
      }
    } else {
      for (i = 0; i < (hoffset & 7); i++) {
        pixel = (data >> 28) & 15;
        LINEDATALAYER(i, pixel, palette, priority);
        data <<= 4;
      }
    }
  }
}

/* this function updates outdata/pridata which came from the layerA generation
   routines */

void Vdp::vdp_newwindow(unsigned int line, uint8 *pridata, uint8 *outdata)
{
  int interlace = (((vdp_reg[12] >> 1) & 3) == 3) ? 1 : 0;
  int realline = line >> interlace;
  int voffset = line;
  uint16 *patterndata = (uint16 *)(vdp_vram + ((vdp_reg[3] & 0x3e) << 10));
  uint8 winhpos = vdp_reg[17] & 0x1f;
  uint8 winvpos = vdp_reg[18] & 0x1f;
  uint8 vcell = realline / 8;
  uint8 hcell;
  uint8 screencells = (vdp_reg[12] & 1) ? 40 : 32;
  uint8 topbottom = vdp_reg[18] & 0x80;
  uint8 leftright = vdp_reg[17] & 0x80;
  uint8 patternshift = (vdp_reg[12] & 1) ? 6 : 5;
  uint16 cellinfo;
  uint8 *pattern;
  uint32 data;
  uint8 palette;
  uint8 pixel;
  unsigned int i;
  unsigned int wholeline = 0;
  uint8 priority;
  int layer = 0; /* for LINEDATALAYER macro */

  /* if topbottom is set then the wholeline part of the window is to the
     bottom, if it is clear then it is to the top
     in the other part of the window, if leftright is set then the window
     is on the right in this part of the screen
     this section below is repeated in the main renderline code, so that
     layerA is not unnecessarily calculated */

  if (topbottom ? (vcell >= winvpos) : (vcell < winvpos))
    wholeline = 1;

  if (!wholeline) {
    if (winhpos >= 20)
      winhpos = 20; /* 0x1F might be special? */
    if (leftright) {
      /* clear out priority bits on right of line (winhpos units of 16 pix */
      for (i = winhpos * 4; i < 320 / 4; i++) /* winhpos units of 16 pix */
        ((uint32 *)pridata)[i] &= (0xffffffff - (0x01010101 << PRIBIT_LAYERA));
      memset(outdata + winhpos * 16, 0, 320 - (winhpos * 16));
    } else {
      /* clear out priority bits on left of line (winhpos units of 16 pix) */
      for (i = 0; i < (unsigned int)(winhpos / 4); i++)
        ((uint32 *)pridata)[i] &= (0xffffffff - (0x01010101 << PRIBIT_LAYERA));
      memset(outdata, 0, winhpos * 16);
    }
  }
  for (hcell = 0; hcell < screencells; hcell++, outdata += 8, pridata += 8) {
    if (!wholeline) {
      if (leftright) {
        if ((hcell >> 1) < winhpos)
          continue;
      } else {
        if ((hcell >> 1) >= winhpos)
          continue;
      }
    }
    cellinfo = LOCENDIAN16(patterndata[vcell << patternshift | hcell]);
    priority = (cellinfo >> 15) & 1;
    palette = (cellinfo >> 13) & 3;
    /* 32 bytes per pattern */
    pattern = vdp_vram + (((cellinfo & 2047) << 5) << interlace);
    /* now get correct line from pattern data */
    if (interlace) {
      /* interlace - double height cells */
      if (cellinfo & 1 << 12) {
        /* vertical flip */
        pattern += 4 * (15 - (voffset & 15));
      } else {
        /* no vertical flip */
        pattern += 4 * (voffset & 15);
      }
    } else {
      /* no interlace */
      if (cellinfo & 1 << 12) {
        /* vertical flip */
        pattern += 4 * (7 - (voffset & 7));
      } else {
        /* no vertical flip */
        pattern += 4 * (voffset & 7);
      }
    }
    data = LOCENDIAN32(*(uint32 *)pattern);
    if (cellinfo & 1 << 11) {
      /* horizontal flip */
      for (i = 0; i < 8; i++) {
        pixel = data & 15;
        LINEDATALAYER(i, pixel, palette, priority);
        data >>= 4;
      }
    } else {
      for (i = 0; i < 8; i++) {
        pixel = (data >> 28) & 15;
        LINEDATALAYER(i, pixel, palette, priority);
        data <<= 4;
      }
    }
  } /* hcell */
}

/*** vdp_renderline - render a line of a field ***/

/* line = field line (0 to 223)
   linedata = buffer to put the output data (console colours: 0-191)
   odd = whether this is an odd field or not (fields are 0 or 1, therefore
                                              odd is the second one in a pair)
   call with odd=0 at all times when not in interlace mode 2
 */

void Vdp::vdp_renderline(unsigned int line, uint8 *linedata, unsigned int odd)
{
  int i;
  uint8 datablock[320 * 4];
  uint8 *data_sprite = datablock;
  uint8 *data_layerA = datablock + 320;
  uint8 *data_layerB = datablock + 320 * 2;
  uint8 *priorities = datablock + 320 * 3;
  uint8 bg = vdp_reg[7] & 63;
  unsigned int interlace = (((vdp_reg[12] >> 1) & 3) == 3) ? 1 : 0;

  memset(datablock, 0, sizeof(datablock));

  if ((vdp_reg[1] & 1 << 6) == 0) {
    /* screen is disabled */
    for (i = 0; i < 320; i++)
      linedata[i] = bg;
    return;
  }

  if (vdp_layerS || vdp_layerSp)
    vdp_sprites(interlace ? (line * 2 + odd) : line, priorities, data_sprite);
  if (vdp_layerA || vdp_layerAp) {
    vdp_newlayer(interlace ? (line * 2 + odd) : line, priorities, data_layerA,
                 0);
    if (vdp_layerW || vdp_layerWp)
      vdp_newwindow(interlace ? (line * 2 + odd) : line, priorities,
                    data_layerA);
  }
  if (vdp_layerB || vdp_layerBp)
    vdp_newlayer(interlace ? (line * 2 + odd) : line, priorities, data_layerB,
                 1);

  for (i = 0; i < 320; i++) {
    switch (priorities[i] | (vdp_reg[12] & 1 << 3)) {
    case 0: /* s/ten=0, B=0, A=0, S=0 */
      if (data_sprite[i])
        linedata[i] = data_sprite[i];
      else if (data_layerA[i])
        linedata[i] = data_layerA[i];
      else if (data_layerB[i])
        linedata[i] = data_layerB[i];
      else
        linedata[i] = bg;
      break;
    case 1: /* s/ten=0, B=1, A=0, S=0 */
      if (data_layerB[i])
        linedata[i] = data_layerB[i];
      else if (data_sprite[i])
        linedata[i] = data_sprite[i];
      else if (data_layerA[i])
        linedata[i] = data_layerA[i];
      else
        linedata[i] = bg;
      break;
    case 2: /* s/ten=0, B=0, A=1, S=0 */
      if (data_layerA[i])
        linedata[i] = data_layerA[i];
      else if (data_sprite[i])
        linedata[i] = data_sprite[i];
      else if (data_layerB[i])
        linedata[i] = data_layerB[i];
      else
        linedata[i] = bg;
      break;
    case 3: /* s/ten=0, B=1, A=1, S=0 */
      if (data_layerA[i])
        linedata[i] = data_layerA[i];
      else if (data_layerB[i])
        linedata[i] = data_layerB[i];
      else if (data_sprite[i])
        linedata[i] = data_sprite[i];
      else
        linedata[i] = bg;
      break;
    case 4: /* s/ten=0, B=0, A=0, S=1 */
      if (data_sprite[i])
        linedata[i] = data_sprite[i];
      else if (data_layerA[i])
        linedata[i] = data_layerA[i];
      else if (data_layerB[i])
        linedata[i] = data_layerB[i];
      else
        linedata[i] = bg;
      break;
    case 5: /* s/ten=0, B=1, A=0, S=1 */
      if (data_sprite[i])
        linedata[i] = data_sprite[i];
      else if (data_layerB[i])
        linedata[i] = data_layerB[i];
      else if (data_layerA[i])
        linedata[i] = data_layerA[i];
      else
        linedata[i] = bg;
      break;
    case 6: /* s/ten=0, B=0, A=1, S=1 */
      if (data_sprite[i])
        linedata[i] = data_sprite[i];
      else if (data_layerA[i])
        linedata[i] = data_layerA[i];
      else if (data_layerB[i])
        linedata[i] = data_layerB[i];
      else
        linedata[i] = bg;
      break;
    case 7: /* s/ten=0, B=1, A=1, S=1 */
      if (data_sprite[i])
        linedata[i] = data_sprite[i];
      else if (data_layerA[i])
        linedata[i] = data_layerA[i];
      else if (data_layerB[i])
        linedata[i] = data_layerB[i];
      else
        linedata[i] = bg;
      break;
    case 8: /* s/ten=1, B=0, A=0, S=0 */
      if (data_sprite[i]) {
        if (data_sprite[i] == 63) { /* shadow operator */
          if (data_layerA[i])
            linedata[i] = data_layerA[i] | 128; /* shadow */
          else if (data_layerB[i])
            linedata[i] = data_layerB[i] | 128; /* shadow */
          else
            linedata[i] = bg | 128;        /* shadow */
        } else if (data_sprite[i] == 62) { /* highlight operator */
          if (data_layerA[i])
            linedata[i] = data_layerA[i]; /* normal */
          else if (data_layerB[i])
            linedata[i] = data_layerB[i]; /* normal */
          else
            linedata[i] = bg; /* normal */
        } else {
          linedata[i] = data_sprite[i] | 128; /* shadow */
        }
      } else {
        if (data_layerA[i])
          linedata[i] = data_layerA[i] | 128; /* shadow */
        else if (data_layerB[i])
          linedata[i] = data_layerB[i] | 128; /* shadow */
        else
          linedata[i] = bg | 128; /* shadow */
      }
      break;
    case 9: /* s/ten=1, B=1, A=0, S=0 */
      if (data_layerB[i]) {
        linedata[i] = data_layerB[i]; /* normal */
      } else {
        if (data_sprite[i]) {
          if (data_sprite[i] == 63) { /* shadow operator */
            if (data_layerA[i])
              linedata[i] = data_layerA[i] | 128; /* shadow */
            else
              linedata[i] = bg | 128;        /* shadow */
          } else if (data_sprite[i] == 62) { /* highlight operator */
            if (data_layerA[i])
              linedata[i] = data_layerA[i] | 64; /* highlight */
            else
              linedata[i] = bg | 64; /* highlight */
          } else {
            linedata[i] = data_sprite[i]; /* normal */
          }
        } else {
          if (data_layerA[i])
            linedata[i] = data_layerA[i]; /* normal */
          else
            linedata[i] = bg; /* normal */
        }
      }
      break;
    case 10: /* s/ten=1, B=0, A=1, S=0 */
      if (data_layerA[i]) {
        linedata[i] = data_layerA[i]; /* normal */
      } else {
        if (data_sprite[i]) {
          if (data_sprite[i] == 63) { /* shadow operator */
            if (data_layerB[i])
              linedata[i] = data_layerB[i] | 128; /* shadow */
            else
              linedata[i] = bg | 128;        /* shadow */
          } else if (data_sprite[i] == 62) { /* highlight operator */
            if (data_layerB[i])
              linedata[i] = data_layerB[i] | 64; /* highlight */
            else
              linedata[i] = bg | 64; /* highlight */
          } else {
            linedata[i] = data_sprite[i]; /* normal */
          }
        } else {
          if (data_layerB[i])
            linedata[i] = data_layerB[i]; /* normal */
          else
            linedata[i] = bg; /* normal */
        }
      }
      break;
    case 11: /* s/ten=1, B=1, A=1, S=0 */
      if (data_layerA[i]) {
        linedata[i] = data_layerA[i]; /* normal */
      } else if (data_layerB[i]) {
        linedata[i] = data_layerB[i]; /* normal */
      } else if (data_sprite[i]) {
        if (data_sprite[i] == 63)      /* shadow operator */
          linedata[i] = bg | 128;      /* shadow */
        else if (data_sprite[i] == 62) /* highlight operator */
          linedata[i] = bg | 64;       /* highlight */
        else
          linedata[i] = data_sprite[i]; /* normal */
      } else {
        linedata[i] = bg; /* normal */
      }
      break;
    case 12: /* s/ten=0, B=0, A=0, S=1 */
      if (data_sprite[i]) {
        if (data_sprite[i] == 63) { /* shadow operator */
          if (data_layerA[i])
            linedata[i] = data_layerA[i] | 128; /* shadow */
          else if (data_layerB[i])
            linedata[i] = data_layerB[i] | 128; /* shadow */
          else
            linedata[i] = bg | 128;        /* shadow */
        } else if (data_sprite[i] == 62) { /* highlight operator */
          if (data_layerA[i])
            linedata[i] = data_layerA[i]; /* normal */
          else if (data_layerB[i])
            linedata[i] = data_layerB[i]; /* normal */
          else
            linedata[i] = bg; /* normal */
        } else {
          linedata[i] = data_sprite[i]; /* normal */
        }
      } else if (data_layerA[i]) {
        linedata[i] = data_layerA[i] | 128; /* shadow */
      } else if (data_layerB[i]) {
        linedata[i] = data_layerB[i] | 128; /* shadow */
      } else {
        linedata[i] = bg | 128; /* shadow */
      }
      break;
    case 13: /* s/ten=1, B=1, A=0, S=1 */
      if (data_sprite[i]) {
        if (data_sprite[i] == 63) { /* shadow operator */
          if (data_layerB[i])
            linedata[i] = data_layerB[i] | 128; /* shadow */
          else if (data_layerA[i])
            linedata[i] = data_layerA[i] | 128; /* shadow */
          else
            linedata[i] = bg | 128;        /* shadow */
        } else if (data_sprite[i] == 62) { /* highlight operator */
          if (data_layerB[i])
            linedata[i] = data_layerB[i] | 64; /* highlight */
          else if (data_layerA[i])
            linedata[i] = data_layerA[i] | 64; /* highlight */
          else
            linedata[i] = bg | 64; /* highlight */
        } else {
          linedata[i] = data_sprite[i]; /* normal */
        }
      } else if (data_layerB[i]) {
        linedata[i] = data_layerB[i]; /* normal */
      } else if (data_layerA[i]) {
        linedata[i] = data_layerA[i]; /* normal */
      }
      break;
    case 14: /* s/ten=1, B=0, A=1, S=1 */
      if (data_sprite[i]) {
        if (data_sprite[i] == 63) { /* shadow operator */
          if (data_layerA[i])
            linedata[i] = data_layerA[i] | 128; /* shadow */
          else if (data_layerB[i])
            linedata[i] = data_layerB[i] | 128; /* shadow */
          else
            linedata[i] = bg | 128;        /* shadow */
        } else if (data_sprite[i] == 62) { /* highlight operator */
          if (data_layerA[i])
            linedata[i] = data_layerA[i] | 64; /* highlight */
          else if (data_layerB[i])
            linedata[i] = data_layerB[i] | 64; /* highlight */
          else
            linedata[i] = bg | 64; /* highlight */
        } else {
          linedata[i] = data_sprite[i]; /* normal */
        }
      } else if (data_layerA[i]) {
        linedata[i] = data_layerA[i]; /* normal */
      } else if (data_layerB[i]) {
        linedata[i] = data_layerB[i]; /* normal */
      }
      break;
    case 15: /* s/ten=1, B=1, A=1, S=1 */
      if (data_sprite[i]) {
        if (data_sprite[i] == 63) { /* shadow operator */
          if (data_layerA[i])
            linedata[i] = data_layerA[i] | 128; /* shadow */
          else if (data_layerB[i])
            linedata[i] = data_layerB[i] | 128; /* shadow */
          else
            linedata[i] = bg | 128;        /* shadow */
        } else if (data_sprite[i] == 62) { /* highlight operator */
          if (data_layerA[i])
            linedata[i] = data_layerA[i] | 64; /* highlight */
          else if (data_layerB[i])
            linedata[i] = data_layerB[i] | 64; /* highlight */
          else
            linedata[i] = bg | 64; /* highlight */
        } else {
          linedata[i] = data_sprite[i]; /* normal */
        }
      } else if (data_layerA[i]) {
        linedata[i] = data_layerA[i]; /* normal */
      } else if (data_layerB[i]) {
        linedata[i] = data_layerB[i]; /* normal */
      }
      break;
    }
  }
}

void Vdp::vdp_renderframe(uint8 *framedata, unsigned int lineoffset)
{
  unsigned int i, line;
  uint32 background;
  unsigned int vertcells = vdp_reg[1] & 1 << 3 ? 30 : 28;
  uint8 *linedata;

  /* fill in background */

  background = vdp_reg[7] & 63;
  background |= background << 8;
  background |= background << 16;

  for (line = 0; line < vertcells * 8; line++) {
    linedata = framedata + line * lineoffset;
    for (i = 0; i < (320 / 4); i++) {
      ((uint32 *)linedata)[i] = background;
    }
  }

  if (vdp_reg[1] & 1 << 6) {
    if (vdp_layerB)
      vdp_layer_simple(1, 0, framedata, lineoffset);
    if (vdp_layerA)
      vdp_layer_simple(0, 0, framedata, lineoffset);
    if (vdp_layerH && (vdp_reg[12] & 1 << 3))
      vdp_shadow_simple(framedata, lineoffset);
    if (vdp_layerS)
      vdp_sprites_simple(0, framedata, lineoffset);
    if (vdp_layerBp)
      vdp_layer_simple(1, 1, framedata, lineoffset);
    if (vdp_layerAp)
      vdp_layer_simple(0, 1, framedata, lineoffset);
    if (vdp_layerSp)
      vdp_sprites_simple(1, framedata, lineoffset);
  }
}
}  // namespace generator
