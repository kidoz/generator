/* SPDX-License-Identifier: GPL-2.0-or-later */
/* VDP state class - C++ core of the video display processor emulation */

#pragma once

/* This header is the C++ companion to vdp.h: vdp.h remains the C surface
 * (LEN_* constants, function declarations, transitional accessors for the
 * still-C 68K memory subsystem), while this header owns the chip state.
 *
 * The module-level globals that vdp.cpp used to export (vdp_vram, vdp_reg,
 * ...) are now members of generator::Vdp, with a single transitional
 * instance `vdp`. C++ consumers reach state as vdp.<field>; the public
 * function API keeps its C names as free-function wrappers. Field and method
 * names intentionally keep their historical vdp_ prefix so the port stays a
 * pure rename+translation; a later cleanup pass may drop the prefix.
 * Members stay public: the save-state layer and tests access them directly
 * until serialization moves into the class. */

#include "machine.h"
#include "vdp.h"

#include <cstdint>

namespace generator {

class Vdp {
public:
  /* timing / frame geometry */
  unsigned int vdp_event;
  unsigned int vdp_vislines;
  unsigned int vdp_visstartline;
  unsigned int vdp_visendline;
  unsigned int vdp_totlines;
  unsigned int vdp_framerate;
  unsigned int vdp_clock;
  unsigned int vdp_68kclock;
  unsigned int vdp_clksperline_68k;
  unsigned int vdp_line = 0; /* current line number */
  uint8 vdp_oddframe = 0;    /* odd/even frame */
  uint8 vdp_vblank = 0;      /* set during vertical blanking */
  uint8 vdp_hblank = 0;      /* set during horizontal blanking */
  uint8 vdp_vsync = 0;       /* a vsync just happened */
  uint8 vdp_dmabusy = 0;     /* dma busy flag */
  uint8 vdp_pal = 0;         /* set for pal mode */
  uint8 vdp_overseas = 1;    /* set for overseas model */

  /* rendering mode flags */
  uint8 vdp_layerB = 1;
  uint8 vdp_layerBp = 1;
  uint8 vdp_layerA = 1;
  uint8 vdp_layerAp = 1;
  uint8 vdp_layerW = 1;
  uint8 vdp_layerWp = 1;
  uint8 vdp_layerH = 1;
  uint8 vdp_layerS = 1;
  uint8 vdp_layerSp = 1;

  /* memories */
  uint8 vdp_cram[LEN_CRAM];
  uint8 vdp_vsram[LEN_VSRAM];
  uint8 vdp_vram[LEN_VRAM];
  uint8 vdp_cramf[LEN_CRAM / 2];

  /* scanline event positions */
  unsigned int vdp_event_start;
  unsigned int vdp_event_vint;
  unsigned int vdp_event_hint;
  unsigned int vdp_event_hdisplay;
  unsigned int vdp_event_end;
  signed int vdp_nextevent = 0;

  /* DMA / port state */
  sint32 vdp_dmabytes = 0;            /* bytes left in DMA - must be fixed size */
  signed int vdp_hskip_countdown = 0; /* actual countdown */
  uint16 vdp_address;                 /* address for data/dma transfers */
  t_code vdp_code;    /* code number for data/dma transfers CD3-CD0 */
  uint8 vdp_ctrlflag; /* set inbetween ctrl writes */
  uint16 vdp_first;   /* first word of address set */
  uint16 vdp_second;  /* second word of address set */

  /* registers */
  uint8 vdp_reg[25];

  /* internal flags (ex-public statics; kept public for tests) */
  int vdp_collision;   /* set during a sprite collision */
  int vdp_overflow;    /* set when too many sprites in one line */
  int vdp_fifofull;    /* set when write fifo full (4 entries) */
  int vdp_fifoempty;   /* set when write fifo empty (0 entries) */
  int vdp_fifo_count;  /* number of entries in FIFO (0-4) */
  int vdp_complex;     /* set when simple routines can't cope */

  /* save-state serialization: registers the chip's chunks through the
   * state_transfer* layer with the same mod/name keys the state module has
   * always written (format stability). */
  void vdp_save_state();

  /* public API (mirrors the free-function wrappers in vdp.h) */
  void vdp_fifo_drain(int count);
  int vdp_init(void);
  void vdp_setupvideo(void);
  void vdp_softreset(void);
  void vdp_reset(void);
  uint16 vdp_status(void);
  void vdp_storectrl(uint16 data);
  void vdp_storedata(uint16 data);
  uint16 vdp_fetchdata(void);
  void vdp_renderline(unsigned int line, uint8 *linedata, unsigned int odd);
  void vdp_renderframe(uint8 *framedata, unsigned int lineoffset);
  void vdp_showregs(void);
  void vdp_spritelist(void);
  void vdp_describe(void);
  void vdp_endfield(void);
  uint8 vdp_gethpos(void);

  /* internals */
  void vdp_fifo_add(void);
  void vdp_ramcopy_vram(int type);
  void vdp_dma_vramcopy(void);
  void vdp_dma_fill(uint8 data);
  void vdp_eventinit(void);
  void vdp_layer_simple(unsigned int layer, unsigned int priority,
                        uint8 *fielddata, unsigned int lineoffset);
  void vdp_plotcell(uint8 *patloc, uint8 palette, uint8 flags, uint8 *cellloc,
                    unsigned int lineoffset);
  void vdp_sprites(unsigned int line, uint8 *pridata, uint8 *outdata);
  int vdp_sprite_simple(unsigned int priority, uint8 *framedata,
                        unsigned int lineoffset, unsigned int number,
                        uint8 *spritelist, uint8 *sprite);
  void vdp_sprites_simple(unsigned int priority, uint8 *framedata,
                          unsigned int lineoffset);
  void vdp_shadow_simple(uint8 *framedata, unsigned int lineoffset);
  void vdp_newlayer(unsigned int line, uint8 *pridata, uint8 *outdata,
                    unsigned int layer);
  void vdp_newwindow(unsigned int line, uint8 *pridata, uint8 *outdata);
};

extern Vdp vdp;

} // namespace generator
