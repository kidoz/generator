/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* Compatibility macros for gradual migration to context-based architecture */

#ifndef GEN_COMPAT_H
#define GEN_COMPAT_H

#include "gen_context.h"

/*
 * These macros map old global variable names to the new context struct fields.
 * This allows existing code to continue working during the migration.
 *
 * Usage: Include this header instead of (or after) the original headers.
 * The macros will only be defined when GEN_COMPAT_ENABLE is set and g_ctx exists.
 *
 * These macros will be removed in Phase 7 when migration is complete.
 */

#ifdef GEN_COMPAT_ENABLE

/* CPU 68k state */
#define cpu68k_rom            (g_ctx->cpu68k.rom)
#define cpu68k_romlen         (g_ctx->cpu68k.romlen)
#define cpu68k_ram            (g_ctx->cpu68k.ram)
#define cpu68k_clocks         (g_ctx->cpu68k.clocks)
#define cpu68k_clocks_curevent (g_ctx->cpu68k.clocks_curevent)
#define cpu68k_frames         (g_ctx->cpu68k.frames)
#define cpu68k_line           (g_ctx->cpu68k.line)
#define cpu68k_frozen         (g_ctx->cpu68k.frozen)
#define cpu68k_totalinstr     (g_ctx->cpu68k.totalinstr)
#define cpu68k_totalfuncs     (g_ctx->cpu68k.totalfuncs)
#define cpu68k_adaptive       (g_ctx->cpu68k.adaptive)

/* Note: regs is a struct, mapping individual fields */
#define regs_pc               (g_ctx->cpu68k.pc)
#define regs_sp               (g_ctx->cpu68k.sp)
#define regs_sr               (g_ctx->cpu68k.sr)
#define regs_stop             (g_ctx->cpu68k.stop)
#define regs_regs             (g_ctx->cpu68k.regs)
#define regs_pending          (g_ctx->cpu68k.pending)

/* CPU Z80 state */
#define cpuz80_ram            (g_ctx->cpuz80.ram)
#define cpuz80_bank           (g_ctx->cpuz80.bank)
#define cpuz80_active         (g_ctx->cpuz80.active)
#define cpuz80_resetting      (g_ctx->cpuz80.resetting)
#define cpuz80_on             (g_ctx->cpuz80.on)
#define cpuz80_z80            (g_ctx->cpuz80.z80)

/* VDP state - video memory */
#define vdp_vram              (g_ctx->vdp.vram)
#define vdp_cram              (g_ctx->vdp.cram)
#define vdp_vsram             (g_ctx->vdp.vsram)
#define vdp_reg               (g_ctx->vdp.reg)
#define vdp_cramf             (g_ctx->vdp.cramf)

/* VDP state - timing */
#define vdp_line              (g_ctx->vdp.line)
#define vdp_event             (g_ctx->vdp.event)
#define vdp_nextevent         (g_ctx->vdp.nextevent)
#define vdp_vislines          (g_ctx->vdp.vislines)
#define vdp_visstartline      (g_ctx->vdp.visstartline)
#define vdp_visendline        (g_ctx->vdp.visendline)
#define vdp_totlines          (g_ctx->vdp.totlines)
#define vdp_framerate         (g_ctx->vdp.framerate)
#define vdp_clock             (g_ctx->vdp.clock)
#define vdp_68kclock          (g_ctx->vdp.clk68k)
#define vdp_clksperline_68k   (g_ctx->vdp.clksperline_68k)

/* VDP state - flags */
#define vdp_oddframe          (g_ctx->vdp.oddframe)
#define vdp_vblank            (g_ctx->vdp.vblank)
#define vdp_hblank            (g_ctx->vdp.hblank)
#define vdp_vsync             (g_ctx->vdp.vsync)
#define vdp_dmabusy           (g_ctx->vdp.dmabusy)
#define vdp_pal               (g_ctx->vdp.pal)
#define vdp_overseas          (g_ctx->vdp.overseas)

/* VDP state - data transfer */
#define vdp_address           (g_ctx->vdp.address)
#define vdp_code              (g_ctx->vdp.code)
#define vdp_ctrlflag          (g_ctx->vdp.ctrlflag)
#define vdp_first             (g_ctx->vdp.first)
#define vdp_second            (g_ctx->vdp.second)
#define vdp_dmabytes          (g_ctx->vdp.dmabytes)
#define vdp_hskip_countdown   (g_ctx->vdp.hskip_countdown)

/* VDP state - events */
#define vdp_event_start       (g_ctx->vdp.event_start)
#define vdp_event_vint        (g_ctx->vdp.event_vint)
#define vdp_event_hint        (g_ctx->vdp.event_hint)
#define vdp_event_hdisplay    (g_ctx->vdp.event_hdisplay)
#define vdp_event_end         (g_ctx->vdp.event_end)

/* VDP state - layers */
#define vdp_layerB            (g_ctx->vdp.layerB)
#define vdp_layerBp           (g_ctx->vdp.layerBp)
#define vdp_layerA            (g_ctx->vdp.layerA)
#define vdp_layerAp           (g_ctx->vdp.layerAp)
#define vdp_layerW            (g_ctx->vdp.layerW)
#define vdp_layerWp           (g_ctx->vdp.layerWp)
#define vdp_layerH            (g_ctx->vdp.layerH)
#define vdp_layerS            (g_ctx->vdp.layerS)
#define vdp_layerSp           (g_ctx->vdp.layerSp)

/* VDP state - FIFO */
#define vdp_fifofull          (g_ctx->vdp.fifofull)
#define vdp_fifoempty         (g_ctx->vdp.fifoempty)
#define vdp_cramchange        (g_ctx->vdp.cramchange)

/* Sound state */
#define sound_regs1           (g_ctx->sound.regs1)
#define sound_regs2           (g_ctx->sound.regs2)
#define sound_address1        (g_ctx->sound.address1)
#define sound_address2        (g_ctx->sound.address2)
#define sound_keys            (g_ctx->sound.keys)
#define sound_on              (g_ctx->sound.on)
#define sound_psg             (g_ctx->sound.psg)
#define sound_fm              (g_ctx->sound.fm)
#define sound_filter          (g_ctx->sound.filter)
#define sound_speed           (g_ctx->sound.speed)
#define sound_sampsperfield   (g_ctx->sound.sampsperfield)
#define sound_threshold       (g_ctx->sound.threshold)
#define sound_minfields       (g_ctx->sound.minfields)
#define sound_maxfields       (g_ctx->sound.maxfields)
#define sound_feedback        (g_ctx->sound.feedback)
#define sound_debug           (g_ctx->sound.debug)
#define sound_soundbuf        (g_ctx->sound.soundbuf)

/* Memory/controller state */
#define mem68k_cont           (g_ctx->mem.cont)

/* Generator state */
#define gen_quit              (g_ctx->quit)
#define gen_debugmode         (g_ctx->config.debugmode)
#define gen_loglevel          (g_ctx->config.loglevel)
#define gen_autodetect        (g_ctx->config.autodetect)
#define gen_musiclog          (g_ctx->config.musiclog)
#define gen_cartinfo          (g_ctx->cartinfo)
#define gen_leafname          (g_ctx->leafname)
#define gen_modifiedrom       (g_ctx->modifiedrom)

#endif /* GEN_COMPAT_ENABLE */

#endif /* GEN_COMPAT_H */
