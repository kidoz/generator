/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */

#include "generator.h"
#include "vdp.h"
#include "cpu68k.h"
#include "cpuz80.h"
#include "reg68k.h"
#include "ui.h"
#include "gensound.h"

#include "snprintf.h"

/* due to DMA transfers, event_nextevent can be called during an instruction
   cycle (reg68k_external_execute -> instruction -> vdp write -> dma ->
   event_freeze -> event_nextevent).  Be careful */

/* Genesis VDP Event State Machine
 *
 * This implements cycle-accurate timing for the Sega Genesis Video Display Processor (VDP).
 * The Genesis runs at ~7.67 MHz (NTSC) and each scanline takes ~488 CPU cycles.
 *
 * VDP Event States (per scanline):
 *   State 0: LINE_START    - Start of new scanline, initialize counters
 *   State 1: VINT_CHECK    - Vertical interrupt timing check (only at line 224)
 *   State 2: HINT_PROCESS  - Horizontal interrupt processing and DMA handling
 *   State 3: HDISPLAY      - Horizontal display active, trigger UI line render
 *   State 4: LINE_END      - End of scanline, sync sound and advance to next line
 *
 * The state machine uses fall-through with goto to handle timing precisely:
 * - Each state checks if enough CPU cycles have elapsed (vdp_nextevent > 0)
 * - If not enough time, break and return (will be called again later)
 * - If enough time, fall through to next state immediately
 * - At end of scanline (state 4), goto back to LINE_START for next scanline
 *
 * Note: C17 migration - removed 'inline' keyword to provide external linkage.
 * In C99/C17, plain 'inline' without 'extern' may not emit external definition.
 */

void event_nextevent(void)
{
  /* Execute the next VDP event based on current state (vdp_event).
   * vdp_event++ advances the state machine through each scanline event. */

  switch (vdp_event++) {
  case 0:  /* LINE_START: Start of new scanline */
  EVENT_NEWLINE:
    LOG_DEBUG1(("%08X due %08X, %d A: %d (cd=%d)",
                cpu68k_clocks, vdp_event_start,
                vdp_line - vdp_visstartline, vdp_reg[10],
                vdp_hskip_countdown));
    /* If first scanline of frame (line 0), initialize sound generation for this field */
    if (vdp_line == 0)
      sound_startfield();

    /* If we're about to enter the visible display area (line before vdp_visstartline),
     * clear vertical blank flag and reset H-interrupt counter */
    if (vdp_line == (vdp_visstartline - 1)) {
      vdp_vblank = 0;
      vdp_hskip_countdown = vdp_reg[10];  /* VDP register 10 = H-interrupt interval */
    }

    /* Calculate cycles until next event (VINT). If not enough time elapsed, break and return */
    if ((vdp_nextevent = vdp_event_vint - cpu68k_clocks) > 0)
      break;
    /* Fall through to next state if enough time has elapsed */
    vdp_event++;

  case 1:  /* VINT_CHECK: Vertical interrupt timing */
    LOG_DEBUG1(("%08X due %08X, %d B: %d (cd=%d)",
                cpu68k_clocks, vdp_event_vint,
                vdp_line - vdp_visstartline, vdp_reg[10],
                vdp_hskip_countdown));

    /* If we've reached the end of visible display (line 224 NTSC, line 240 PAL),
     * enter vertical blank period and trigger V-BLANK interrupt if enabled */
    if (vdp_line == vdp_visendline) {
      vdp_vblank = 1;  /* Set vertical blank flag */
      vdp_vsync = 1;   /* Set vertical sync flag */
      /* VDP register 1 bit 5 = V-INT enable. Trigger 68k interrupt level 6 if enabled */
      if (vdp_reg[1] & 1 << 5)
        reg68k_external_autovector(6);  /* Vertical interrupt (highest priority) */
    }

    /* Calculate cycles until next event (HINT). If not enough time, break */
    if ((vdp_nextevent = vdp_event_hint - cpu68k_clocks) > 0)
      break;
    vdp_event++;

  case 2:  /* HINT_PROCESS: Horizontal interrupt and DMA processing */
    LOG_DEBUG1(("%08X due %08X, %d C: %d (cd=%d)",
                cpu68k_clocks, vdp_event_hint,
                vdp_line - vdp_visstartline, vdp_reg[10],
                vdp_hskip_countdown));

    /* Set horizontal blank flag during active display scanlines */
    if (vdp_line >= vdp_visstartline && vdp_line < vdp_visendline)
      vdp_hblank = 1;

    /* Reset H-interrupt counter at boundaries (before visible area or after) */
    if (vdp_line == (vdp_visstartline - 1) || (vdp_line > vdp_visendline)) {
      vdp_hskip_countdown = vdp_reg[10];  /* VDP register 10 = H-INT interval */
      LOG_DEBUG1(("H counter reset to %d", vdp_hskip_countdown));
    }

    /* Horizontal interrupt (H-INT) logic: VDP register 0 bit 4 = H-INT enable */
    if (vdp_reg[0] & 1 << 4) {
      LOG_DEBUG1(("pre = %d", vdp_hskip_countdown));
      /* Decrement H-INT counter. When it reaches 0, trigger interrupt */
      if (vdp_hskip_countdown-- == 0) {
        LOG_DEBUG1(("in = %d", vdp_hskip_countdown));
        /* Re-initialize counter for next H-INT */
        vdp_hskip_countdown = vdp_reg[10];
        LOG_DEBUG1(("H counter looped to %d", vdp_hskip_countdown));

        /* Trigger 68k interrupt level 4 (H-INT) if we're in the right scanline range */
        if (vdp_line >= vdp_visstartline - 1 && vdp_line < vdp_visendline - 1)
          reg68k_external_autovector(4);  /* Horizontal interrupt (medium priority) */

        /* Timing-critical adjustment: For games sensitive to H-INT timing,
         * we synchronize CPU clock to ensure accurate emulation of code
         * that runs immediately after H-INT fires */
        cpu68k_clocks = vdp_event_hint;
      }
      LOG_DEBUG1(("post = %d", vdp_hskip_countdown));
    }

    /* DMA (Direct Memory Access) processing: Transfer bytes from 68k RAM/ROM to VRAM
     * The 68k is frozen during DMA (see event_freeze). DMA transfer rate depends on:
     * - Display mode: Active display (slower) vs blank period (faster)
     * - Screen width: 320px (H40 mode) vs 256px (H32 mode)
     *
     * Bytes per scanline:
     * - H40 mode (320px): 18 bytes during active, 205 bytes during blank
     * - H32 mode (256px): 16 bytes during active, 167 bytes during blank */
    if (vdp_dmabytes) {
      /* Calculate DMA bytes transferred this scanline based on display state */
      vdp_dmabytes -= (vdp_vblank || !(vdp_reg[1] & 1 << 6))  /* Blank or display off? */
        ? ((vdp_reg[12] & 1) ? 205 : 167)  /* Fast transfer (blank period) */
        : ((vdp_reg[12] & 1) ? 18 : 16);   /* Slow transfer (active display) */

      /* DMA complete when counter reaches 0 */
      if (vdp_dmabytes <= 0) {
        vdp_dmabytes = 0;
        vdp_dmabusy = 0;  /* Allow 68k to resume */
      }
    }

    /* Calculate cycles until next event (HDISPLAY). If not enough time, break */
    if ((vdp_nextevent = vdp_event_hdisplay - cpu68k_clocks) > 0)
      break;
    vdp_event++;

  case 3:  /* HDISPLAY: Horizontal display active - trigger UI line rendering */
    LOG_DEBUG1(("%08X due %08X, %d D: %d (cd=%d)",
                cpu68k_clocks, vdp_event_hdisplay,
                vdp_line - vdp_visstartline, vdp_reg[10],
                vdp_hskip_countdown));

    /* Notify UI that this scanline is ready to be rendered to the screen.
     * The UI will read vdp_regs[] and vdp_vram[] to render graphics/sprites */
    ui_line(vdp_line - vdp_visstartline + 1);

    /* Calculate cycles until next event (LINE_END). If not enough time, break */
    if ((vdp_nextevent = vdp_event_end - cpu68k_clocks) > 0)
      break;
    /* Note: vdp_event++; not needed, we reset vdp_event to 1 below and jump back */

  case 4:  /* LINE_END: End of scanline - sync sound, advance to next line */
    LOG_DEBUG1(("%08X due %08X, %d E: %d (cd=%d)",
                cpu68k_clocks, vdp_event_end,
                vdp_line - vdp_visstartline, vdp_reg[10],
                vdp_hskip_countdown));

    /* Clear horizontal blank flag at end of scanline */
    if (vdp_line >= vdp_visstartline && vdp_line < vdp_visendline)
      vdp_hblank = 0;

    /* Drain VDP FIFO: The 4-entry write FIFO drains during display.
     * During active display: drain ~1 entry per 2 scanlines
     * During blank period: drain faster (~2 entries per scanline)
     * This affects games that poll FIFO status before writes. */
    if (vdp_vblank || !(vdp_reg[1] & 1 << 6)) {
      /* Blank period or display off - fast drain */
      vdp_fifo_drain(2);
    } else if ((vdp_line & 1) == 0) {
      /* Active display - drain 1 entry every 2 scanlines */
      vdp_fifo_drain(1);
    }

    /* Synchronize Z80 CPU and generate sound samples for this scanline.
     * The Genesis has a separate Z80 CPU for sound processing that runs
     * concurrently with the 68k. We sync it here to keep audio in lockstep. */
    cpuz80_sync();
    sound_line();

    /* Advance to next scanline */
    vdp_line++;

    /* At end of visible display, trigger Z80 interrupt (used by some games) */
    if (vdp_line == vdp_visendline)
      cpuz80_interrupt();

    /* End of frame reached (vdp_totlines = 262 for NTSC, 313 for PAL) */
    if (vdp_line == vdp_totlines) {
      /* IMPORTANT: Order of these calls matters for correct emulation! */
      sound_endfield();  /* Must be first: Finalizes sound buffer for GYM/AVI output */
      ui_endfield();     /* Notify UI that frame is complete, trigger screen update */
      vdp_endfield();    /* Must be after ui_endfield: Resets VDP state for next frame */
      cpuz80_endfield(); /* Reset Z80 state */
      cpu68k_endfield(); /* Reset 68k state */
      cpu68k_frames++;   /* Increment frame counter */
    }

    /* Advance all event timers by one scanline (vdp_clksperline_68k cycles).
     * This shifts the timing window forward for the next scanline. */
    vdp_event_start += vdp_clksperline_68k;
    vdp_event_vint += vdp_clksperline_68k;
    vdp_event_hint += vdp_clksperline_68k;
    vdp_event_hdisplay += vdp_clksperline_68k;
    vdp_event_end += vdp_clksperline_68k;

    /* Reset state machine to state 1 (skip state 0 initialization) and jump
     * back to EVENT_NEWLINE to start processing the next scanline.
     * We use goto here instead of recursion to avoid stack overflow. */
    vdp_event = 1;
    goto EVENT_NEWLINE;
  }                             /* switch */
}

/*** event_doframe - execute until the end of the current frame ***/

void event_doframe(void)
{
  unsigned int startframe = cpu68k_frames;

  do {
    while (vdp_nextevent > 0)
      vdp_nextevent = -reg68k_external_execute(vdp_nextevent);
    event_nextevent();
  }
  while (startframe == cpu68k_frames);

}

/*** event_step - execute one instruction with no caching ***/

void event_dostep(void)
{
  /* execute one instruction and subtract from vdp_nextevent those cycles */
  vdp_nextevent -= reg68k_external_step();
  /* if negative or 0, i.e. we have done all the cycles we need to this event,
     call event_nextevent! */
  while (vdp_nextevent <= 0)
    event_nextevent();
}

/*** event_freeze_clocks - freeze 68k for given clock cycles ***/

/* NB: this routine is called by event_freeze which is called by
   vdp routines.  This could all happen IN THE MIDDLE OF A BLOCK at which
   point cpu68k_clocks will have been updated but vdp_nextevent will not! 
   therefore we must at this point work out what vdp_nextevent should be
   at the current moment in time, add on the clocks, wind forward time and
   then subtract the clocks that will be added later */

void event_freeze_clocks(unsigned int clocks)
{
  int missed = 0;

  /* first - fix vdp_nextevent to be correct for right now, due to block
     marking delay */

  /* find out how many clocks vdp_nextevent has missed */
  /* modify vdp_nextevent to reflect the real state as of now */

  switch (vdp_event) {
  case 0:
    missed = vdp_nextevent - (vdp_event_start - cpu68k_clocks);
    vdp_nextevent -= missed;
    break;
  case 1:
    missed = vdp_nextevent - (vdp_event_vint - cpu68k_clocks);
    vdp_nextevent -= missed;
    break;
  case 2:
    missed = vdp_nextevent - (vdp_event_hint - cpu68k_clocks);
    vdp_nextevent -= missed;
    break;
  case 3:
    missed = vdp_nextevent - (vdp_event_hdisplay - cpu68k_clocks);
    vdp_nextevent -= missed;
    break;
  case 4:
    missed = vdp_nextevent - (vdp_event_end - cpu68k_clocks);
    vdp_nextevent -= missed;
    break;
  default:
    printf("assertion failed: bad vdp_event in event_freeze_clocks: %d\n",
           vdp_event);
  }

  /* move cpu68k_clocks and vdp_nextevent forward in time */

  cpu68k_clocks += clocks;
  vdp_nextevent -= clocks;

  /* now catch up events */

  while (vdp_nextevent <= 0)
    event_nextevent();

  /* and then un-adjust vdp_nextevent, as the block marking code will update
     this later */

  vdp_nextevent += missed;
}

/*** event_freeze - freeze 68k for given VDP byte transfer ***/

void event_freeze(unsigned int bytes)
{
  int wide = vdp_reg[12] & 1;
  int clocks, possible;
  double percent_possible;
  int togo = (int)bytes;

  cpu68k_frozen = 1;            /* prohibit interrupts since PC is not known in the
                                   middle of a 68k block due to register mappings */

  while (togo > 0) {
    /* clocks will be negative if we're in the middle of a cpu block */
    clocks = vdp_event_end - cpu68k_clocks;
    if (clocks < 0)
      clocks = 0;
    percent_possible = clocks / vdp_clksperline_68k;
    if (vdp_reg[1] & 1 << 6 && !vdp_vblank) {
      /* vdp active */
      possible = (unsigned int)(percent_possible * (wide ? 18 : 16));
    } else {
      /* vdp inactive */
      possible = (unsigned int)(percent_possible * (wide ? 205 : 167));
    }
    if (togo >= possible) {
      event_freeze_clocks(clocks);
      togo -= possible;
    } else {
      event_freeze_clocks(((double)togo / possible) * clocks);
      togo = 0;
    }
  }
  cpu68k_frozen = 0;
}
