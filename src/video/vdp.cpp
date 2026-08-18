/* SPDX-License-Identifier: GPL-2.0-or-later */

/* video display processor emulation - ports, DMA, FIFO and frame timing */

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

/* for dma 'bytes per line' stuff we use 'memory to vram' figures as a baseline
   and double the number of real bytes for 'vram copy' in order to adjust to
   our model; also there is a subtraction of one for vram fill to compensate
   there too - probably being far too picky and it isn't exactly perfect
   anyway */

/* FIFO implementation notes:
 * The Genesis VDP has a 4-entry write FIFO. Writes to VRAM/CRAM/VSRAM go into
 * the FIFO and are processed during active display. The FIFO drains at roughly
 * 1 entry per 2 scanlines during active display (faster during blank).
 * Status register bits 8-9 report FIFO full/empty state. */

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

#define VDP_FIFO_SIZE 4 /* Genesis VDP has 4-entry FIFO */

namespace generator {

#define PRIBIT_LAYERB 0
#define PRIBIT_LAYERA 1
#define PRIBIT_SPRITE 2

/*** vdp_fifo_add - add an entry to the FIFO (called on VDP writes) ***/

void Vdp::vdp_fifo_add(void)
{
  if (vdp_fifo_count < VDP_FIFO_SIZE) {
    vdp_fifo_count++;
  }
  vdp_fifoempty = (vdp_fifo_count == 0) ? 1 : 0;
  vdp_fifofull = (vdp_fifo_count >= VDP_FIFO_SIZE) ? 1 : 0;
}

/*** vdp_fifo_drain - drain FIFO entries (called during display) ***/

void Vdp::vdp_fifo_drain(int count)
{
  vdp_fifo_count -= count;
  if (vdp_fifo_count < 0) {
    vdp_fifo_count = 0;
  }
  vdp_fifoempty = (vdp_fifo_count == 0) ? 1 : 0;
  vdp_fifofull = (vdp_fifo_count >= VDP_FIFO_SIZE) ? 1 : 0;
}

/*** vdp_init - initialise this sub-unit ***/

int Vdp::vdp_init(void)
{
  vdp_reset();
  return 0;
}

/*** vdp_setupvideo - setup parameters dependant on vdp_pal ***/

void Vdp::vdp_setupvideo(void)
{
  int v30 = (vdp_reg[1] & 1 << 3) ? 1 : 0;

  if (!vdp_pal && v30)
    ui_err("Impossible VDP mode - vertical 30 cell NTSC");

  /* What speed is the PAL clock? */
  vdp_clock = vdp_pal ? 53200000 : 53693100;
  vdp_68kclock = vdp_clock / 7;
  vdp_vislines = v30 ? 240 : 224;
  vdp_visstartline = v30 ? 46 : (vdp_pal ? 54 : 19);
  vdp_visendline = vdp_visstartline + vdp_vislines;
  vdp_totlines = vdp_pal ? 312 : 262;
  vdp_framerate = vdp_pal ? 50 : 60;
  vdp_clksperline_68k = (vdp_68kclock / vdp_framerate / vdp_totlines);
}

/*** vdp_softreset - soft reset ***/

void Vdp::vdp_softreset(void)
{
  /* a soft reset involves resetting the cpu so we need to reset the
     vdp event timers */
  vdp_eventinit();
}

/*** vdp_reset - reset vdp sub-unit ***/

void Vdp::vdp_reset(void)
{
  int i;

  /* clear registers */

  for (i = 0; i < 25; i++)
    vdp_reg[i] = 0;

  /* set PAL/NTSC variables */

  vdp_setupvideo();
  vdp_vblank = 0;
  vdp_hblank = 0;
  vdp_oddframe = 0;
  vdp_collision = 0;
  vdp_overflow = 0;
  vdp_vsync = 0;
  vdp_fifo_count = 0;
  vdp_fifofull = 0;
  vdp_fifoempty = 1;
  vdp_ctrlflag = 0;
  vdp_first = 0;
  vdp_second = 0;
  vdp_dmabytes = 0;
  vdp_address = 0;

  memset(vdp_cram, 0, LEN_CRAM);
  memset(vdp_vsram, 0, LEN_VSRAM);
  memset(vdp_vram, 0, LEN_VRAM);

  /* clear CRAM */

  for (i = 0; i < 64; i++) {
    (vdp_cram + i * 2)[0] = (i & 7) << 1;
    (vdp_cram + i * 2)[1] = (i & 7) << 5 | (i & 7) << 1;
    vdp_cramf[i] = 1;
  }
  vdp_eventinit();
  LOG_VERBOSE(
      "VDP: totlines = %d (%s)", vdp_totlines, vdp_pal ? "PAL" : "NTSC");
}

uint16 Vdp::vdp_status(void)
{
  uint16 ret;

  /* bit      meaning (when set)
   * 0        0:ntsc 1:pal
   * 1        dma busy
   * 2        during h blanking
   * 3        during v blanking
   * 4        frame in interlace mode - 0:even 1:odd
   * 5        collision happened between non-zero pixels in two sprites
   * 6        too many sprites in one line
   * 7        v interrupt has happened
   * 8        write fifo full
   * 9        write fifo empty
   * 10-15 are next word on bus, i.e. next word in ROM - CM
   */
  ret = vdp_pal | vdp_dmabusy << 1 | vdp_hblank << 2 | vdp_vblank << 3 |
        vdp_oddframe << 4 | vdp_collision << 5 | vdp_overflow << 6 |
        vdp_vsync << 7 | vdp_fifofull << 8 | vdp_fifoempty << 9;

#ifdef DEBUG_VDP
  if (vdp_collision)
    LOG_VERBOSE("%08X Collision read %d", regs.pc, vdp_collision);
#endif

  vdp_vsync = vdp_collision = vdp_overflow = 0;
  vdp_ctrlflag = 0; /* Charles MacDonald - so he claims ;) */

  LOG_DEBUG1("%08X STATUS READ %02X", regs.pc, ret);
  LOG_VERBOSE("%08X STATUS READ %02X", regs.pc, ret);

  return (ret);
}

void Vdp::vdp_storectrl(uint16 data)
{
  uint8 reg;
  uint8 regdata;

#ifdef DEBUG_VDP
  LOG_VERBOSE("%08X [VDP] Ctrl write of %04X (vdp_ctrlflag before=%d)",
               regs.pc, data, vdp_ctrlflag);
#endif

  if (!vdp_ctrlflag) {
    if ((data & 0xE000) == 0x8000) {
      /* register set */
      reg = (data >> 8) & 31;
      regdata = data & 255;
      if (reg > 24) {
        LOG_NORMAL(
            "%08X [VDP] Invalid register (%d)", regs.pc, ((data >> 8) & 31));
        return;
      }
      vdp_reg[reg] = regdata;
      vdp_code = (t_code)0;
#ifdef DEBUG_VDP
      LOG_VERBOSE(
          "%08X [VDP] Register %d set to %04X", regs.pc, reg, regdata);
#endif
      return;
    } else {
      vdp_ctrlflag = 1;
      vdp_first = data;
      return;
    }
  } else {
    vdp_second = data;
    vdp_ctrlflag = 0;
    vdp_code = (t_code)(((vdp_first >> 14) & 3) | ((data >> 2) & (3 << 2)));
    vdp_address = (vdp_first & 0x3FFF) | (data << 14 & 0xC000);

#ifdef DEBUG_VDP
    LOG_VERBOSE("%08X [VDP] Ctrl: %08X; code=%d address=%X", regs.pc,
                 (vdp_first << 16) | vdp_second, vdp_code, vdp_address);
#endif

    if ((data & 1 << 7) && (vdp_reg[1] & 1 << 4)) { /* CD5 - DMA ? */
      if (vdp_dmabusy) {
        vdp_dmabusy = 1; /* null statement to avoid gcc warnings */
        LOG_DEBUG1("DMA initiation during DMA!");
      }
      /* CD4 - not read - need to verify */
      vdp_dmabusy = 1;
      switch ((vdp_reg[23] >> 6) & 3) {
      case 0:
      case 1: /* ram to vram */
        switch (vdp_code) {
        case 1: /* ram copy to vram */
          vdp_ramcopy_vram(0);
          break;
        case 3: /* ram copy to cram */
          vdp_ramcopy_vram(1);
          break;
        case 5: /* ram copy to vsram */
          vdp_ramcopy_vram(2);
          break;
        default: /* undefined */
          LOG_NORMAL("%08X [VDP] start of type %d to address %X", regs.pc,
                      vdp_code, vdp_address);
          break;
        }
        vdp_dmabusy = 0; /* 68k was frozen */
        break;
      case 2: /* VRAM fill */
        /* VRAM fill is triggered by the next data write to VDP.
         * vdp_dmabusy remains set (from line 306 above), and when
         * vdp_storedata() is called, it detects vdp_dmabusy and
         * calls vdp_dma_fill() to perform the actual fill operation.
         * See vdp_storedata() for the fill trigger logic. */
        break;
      case 3: /* VRAM copy */
        vdp_dma_vramcopy();
        /* vdp_dmabusy is cleared when vdp_dmabytes is empty */
        break;
      }
    }
  }
}

void Vdp::vdp_ramcopy_vram(int type)
{
  uint16 length = vdp_reg[19] | vdp_reg[20] << 8;
  uint8 srcbank = vdp_reg[23];
  uint16 srcoffset = vdp_reg[21] | vdp_reg[22] << 8;
  uint8 increment = vdp_reg[15];
  uint16 *srcmemory;
  uint16 srcmask;
  uint16 data;
  unsigned int i;

#ifdef DEBUG_VDP
  LOG_VERBOSE("%08X [VDP] VRAM copy from source %08X "
               "vdpaddr=%08X length=%d (%s)",
               regs.pc, (srcbank * 0x10000 + srcoffset) * 2, vdp_address,
               length,
               type == 0   ? "vram"
               : type == 1 ? "cram"
               : type == 2 ? "vsram"
                           : "??");
#endif

  if (srcbank & 1 << 6) {
    srcmemory = (uint16 *)cpu68k_ram;
    srcmask = 0x7fff; /* 32k words = 64k */
  } else {
    srcmemory = (uint16 *)(cpu68k_rom + srcbank * 0x20000);
    srcmask = 0xffff; /* 64k words = 128k */
  }
  for (i = 0; i < length; i++) {
    data = LOCENDIAN16(srcmemory[srcoffset & srcmask]);
    switch (type) {
    case 0: /* VRAM */
      vdp_vram[vdp_address] = data >> 8;
      vdp_vram[vdp_address ^ 1] = data & 0xff;
      break;
    case 1: /* CRAM */
      vdp_cram[vdp_address & 0x7e] = data >> 8;
      vdp_cram[(vdp_address & 0x7e) | 1] = data & 0xff;
      vdp_cramf[(vdp_address & 0x7e) >> 1] = 1;
#ifdef DEBUG_VDPCRAM
      LOG_VERBOSE("%08X CRAM %X = %04X", regs.pc, vdp_address >> 1, data);
#endif
      break;
    case 2: /* VSRAM */
      if ((vdp_address & 0x7e) < LEN_VSRAM) {
        vdp_vsram[vdp_address & 0x7e] = data >> 8;
        vdp_vsram[(vdp_address & 0x7e) | 1] = data & 0xff;
      }
      break;
    }
    srcoffset += 1;
    vdp_address += increment;
  }
  vdp_reg[19] = 0;
  vdp_reg[20] = 0;
  vdp_reg[22] = (srcoffset >> 8) & 0xff;
  vdp_reg[21] = srcoffset & 0xff;
  /* vram sends bytes, cram/vsram send words so are twice as efficient */
  event_freeze(type == 0 ? length * 2 : length);
}

void Vdp::vdp_dma_vramcopy()
{
  uint32 length = vdp_reg[19] | vdp_reg[20] << 8;
  uint8 increment = vdp_reg[15];
  uint16 srcaddr = vdp_reg[21] | vdp_reg[22] << 8;
  unsigned int i;

  if (length == 0) {
    LOG_NORMAL("%08X [VDP] Warning - length of 0 used in vram copy", regs.pc);
    length = 0x10000; /* could be 0xffff */
  }
#ifdef DEBUG_VDPDMA
  LOG_VERBOSE("%08X [VDP] COPY length %04X dstaddr %08X inc %02X "
               "srcaddr %04X",
               regs.pc, length, vdp_address, increment, srcaddr);
#endif

  for (i = 0; i < length; i++) {
    vdp_vram[vdp_address] = vdp_vram[srcaddr++];
    vdp_address += increment;
  }

  vdp_reg[19] = 0;
  vdp_reg[20] = 0;
  vdp_reg[22] = (srcaddr >> 8) & 0xff;
  vdp_reg[21] = srcaddr & 0xff;
  vdp_dmabytes = length * 2; /* factor of 2 vram copy to vram fill (p36) */
}

/*** vdp_dma_fill - implement the DMA part of the fill operation - note
     that the low byte of the 16 bit word has already been written in the
     non-dma stage ***/

void Vdp::vdp_dma_fill(uint8 data)
{
  uint16 length = vdp_reg[19] | vdp_reg[20] << 8;
  uint8 increment = vdp_reg[15];
  unsigned int i;

  if (increment != 1 && increment != 2 && increment != 4)
    LOG_NORMAL("VDP fill used with strange increment %d", increment);

  for (i = 0; i < length; i++) {
    vdp_vram[vdp_address ^ 1] = data;
    vdp_address += increment; /* 16 bit wrap */
  }
  vdp_reg[19] = 0;
  vdp_reg[20] = 0;
  vdp_dmabytes = length + 1; /* extra byte used (see p36) */
}

void Vdp::vdp_storedata(uint16 data)
{
  uint16 address;
  uint16 sdata;

  if (vdp_ctrlflag) {
    /* Note: Don't log here - vdp_storedata() is called thousands of times per
       frame during VDP writes. Logging causes severe performance issues and
       audio freezing. This condition indicates an "unterminated control write"
       but is not critical. */
    // LOG_NORMAL("%08X [VDP] Unterminated ctrl setting %04X/%04X", regs.pc,
    //             vdp_first, vdp_second);
    vdp_storectrl(vdp_second);
  }
#ifdef DEBUG_VDPDATA
  LOG_NORMAL("%08X [VDP] code=%d (%s) data=%04X addr=%X inc=%d", regs.pc,
              vdp_code,
              vdp_code == cd_vram_store    ? "vram"
              : vdp_code == cd_cram_store  ? "cram"
              : vdp_code == cd_vsram_store ? "vsram"
                                           : "??",
              data, vdp_address, vdp_reg[15]);
#endif
  switch (vdp_code) {
  case cd_vram_store:
    sdata = (vdp_address & 1) ? SWAP16(data) : data; /* only for VRAM */
    *(uint16 *)(vdp_vram + (vdp_address & 0xfffe)) = LOCENDIAN16(sdata);
    vdp_fifo_add(); /* Track FIFO entry */
    break;
  case cd_cram_store:
    address = vdp_address & 0x7e; /* address lines used */
    *(uint16 *)(vdp_cram + address) = LOCENDIAN16(data);
    vdp_cramf[address >> 1] = 1;
    vdp_fifo_add(); /* Track FIFO entry */
    break;
  case cd_vsram_store:
    address = vdp_address & 0x7e; /* address lines used */
    if (address < LEN_VSRAM)
      *(uint16 *)(vdp_vsram + address) = LOCENDIAN16(data);
    vdp_fifo_add(); /* Track FIFO entry */
    break;
  default: /* undefined */
    LOG_NORMAL("%08X [VDP] Bad word store to %08X of type %d data = %04X",
                regs.pc, vdp_address, vdp_code, data);
    break;
  }
  vdp_address += vdp_reg[15]; /* 16 bit wrap */

  /* note fall-through from normal write to DMA initiation - this is
     correct operation */

  if (vdp_dmabusy) {
    if (vdp_code == 1)
      /* other vdp_codes consume time but don't result in fills */
      /* vdp_dmabusy is cleared when vdp_dmabytes is empty */
      vdp_dma_fill((data >> 8) & 0xff);
  }
}

uint16 Vdp::vdp_fetchdata(void)
{
  uint16 address;
  uint16 data;

  if (vdp_ctrlflag) {
    /* Note: Don't log here - vdp_fetchdata() is called thousands of times per
       frame during VDP reads. Logging causes severe performance issues and
       audio freezing. */
    // LOG_NORMAL("%08X [VDP] Unterminated ctrl setting %04X/%04X", regs.pc,
    //             vdp_first, vdp_second);
    vdp_storectrl(vdp_second);
  }

  switch (vdp_code) {
  case cd_vram_fetch:
    data = LOCENDIAN16(*(uint16 *)(vdp_vram + (vdp_address & 0xfffe)));
    break;
  case cd_cram_fetch:
    address = vdp_address & 0x7e; /* address lines used */
    data = LOCENDIAN16(*(uint16 *)(vdp_cram + address));
    break;
  case cd_vsram_fetch:
    address = vdp_address & 0x7e; /* address lines used */
    if (address < LEN_VSRAM)
      data = LOCENDIAN16(*(uint16 *)(vdp_vsram + address));
    else
      data = 0; /* tests show this range appears random
                   (although I'm sure it isn't) */
    break;
  default: /* undefined */
    /* reading in write mode suspends 68000 on a real machine */
    LOG_NORMAL("%08X [VDP] Bad word fetch of %08X of type %d", regs.pc,
                vdp_address, vdp_code);
    data = 0;
    break;
  }
  vdp_address += vdp_reg[15]; /* 16 bit wrap */
  return data;
}


void Vdp::vdp_eventinit(void)
{
  /* Facts from documentation:
     H-Blank is 73.7 clock cycles long.
     The VDP settings are aquired 36 clocks after start of H-Blank.
     The display period is 413.3 clocks in duration.
     V-Int occurs 14.7us after H-Int (which is 112 clock cycles)
     Facts from clock data:
     One line takes 488 clocks (vdp_clksperline_68k)
     Assumptions: (not sure if these are true anymore)
     We 'approximate' and make H-Int occur at the same time as H-Blank.
     V-Blank starts at V-Int and ends at the start of line 0.

     vdp_event_start    = start of line, end of v-blank
     vdp_event_vint     = v-int time on line 224 (or 240)
     (112 clocks after h-int)
     vdp_event_hint     = h-int time at end of each line
     vdp_event_hdisplay = settings are aquired and current line displayed
     vdp_event_end      = end of line, end of h-blank

     Note that if the program stays in H-Int 224 longer than 112 clocks, V-Int
     is not supposed to occur due to the processor acknowledging the wrong
     interrupt from the VDP, thus programs disable H-Ints on 223 to prevent
     this problem.  We don't worry about this.
   */
  vdp_event = 0;
  vdp_event_start = 0;
  vdp_event_vint = 112 - 74;
  vdp_event_hint = vdp_clksperline_68k - 74;
  vdp_event_hdisplay = vdp_event_hint + 36;
  vdp_event_end = vdp_clksperline_68k;
  vdp_nextevent = 0;
}

void Vdp::vdp_endfield(void)
{
  vdp_line = 0;
  vdp_eventinit();
  vdp_oddframe ^= 1; /* toggle */
  /* printf("(%d,%d,%d,%d,%d)\n", vdp_event_type,
     vdp_event_startline, vdp_event_hint, vdp_event_vdpplot,
     vdp_event_endline); */
}

uint8 Vdp::vdp_gethpos(void)
{
  float percent;

  /* vdp_event = 0/1/2 -> beginning of line until 74 clocks before end
     3     -> between hint and hdisplay (36 clocks)
     4     -> between hdisplay and end  (38 clocks)
     This routine goes from 0 to the maximum number allowed not within
     H-blank, and then goes slightly beyond up to hdisplay.  Then
     between hdisplay and end we go negative.  I'm not sure how negative
     it is supposed to be, this goes from -38 to 0.

     40 horizontal cells, H goes from $00 to $B6, $E4 to $FF
     32 horizontal cells, H goes from $00 to $93, $E8 to $FF

     this is such a bodge - any changes, check '3 Ninjas kick back'
   */
  LOG_DEBUG1("gethpos %X: clocks=%X : startofline=%X : hint=%X : "
              "end=%X",
              vdp_event, cpu68k_clocks, vdp_event_start, vdp_event_hint,
              vdp_event_end);
  if (vdp_event < 3) {
    percent = ((float)(cpu68k_clocks - vdp_event_start) /
               (float)(vdp_event_hint - vdp_event_start));
    if (vdp_reg[12] & 1)
      return (cpu68k_clocks > vdp_event_hint) ? 0xE4 : percent * 0xB6;
    else
      return (cpu68k_clocks > vdp_event_hint) ? 0xE8 : percent * 0x93;
  } else {
    percent = ((float)(cpu68k_clocks - vdp_event_hint) /
               (float)(vdp_event_end - vdp_event_hint));
    if (vdp_reg[12] & 1)
      return ((cpu68k_clocks > vdp_event_end)
                  ? 0
                  : ((uint8)(0xE4 + percent * 28) & 0xff));
    else
      return ((cpu68k_clocks > vdp_event_end)
                  ? 0
                  : ((uint8)(0xE8 + percent * 24) & 0xff));
  }
}


void Vdp::vdp_save_state()
{
  /* Chunk keys are load-bearing: the save-state format (version 2) has used
   * these exact mod/name/instance/size tuples since the beginning. */
  state_transfer8("vdp", "vram", 0, vdp_vram, LEN_VRAM);
  state_transfer8("vdp", "cram", 0, vdp_cram, LEN_CRAM);
  state_transfer8("vdp", "vsram", 0, vdp_vsram, LEN_VSRAM);
  state_transfer8("vdp", "regs", 0, vdp_reg, 25);
  state_transfer8("vdp", "pal", 0, &vdp_pal, 1);
  state_transfer8("vdp", "overseas", 0, &vdp_overseas, 1);
  state_transfer8("vdp", "ctrlflag", 0, &vdp_ctrlflag, 1);
  /* this cast is probably very bad */
  state_transfer8("vdp", "code", 0, (uint8 *)&vdp_code, 1);
  state_transfer16("vdp", "first", 0, &vdp_first, 1);
  state_transfer16("vdp", "second", 0, &vdp_second, 1);
  state_transfer32("vdp", "dmabytes", 0, (uint32 *)&vdp_dmabytes, 1);
  state_transfer16("vdp", "address", 0, &vdp_address, 1);
}
}  // namespace generator
