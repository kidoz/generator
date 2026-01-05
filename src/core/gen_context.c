/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* Core context - implementation of context lifecycle functions */

#include <stdlib.h>
#include <string.h>

#include "gen_context.h"
#include "generator.h"
#include "vdp.h"
#include "gensound.h"
#include "cpu68k.h"

/*
 * Global context pointer (transition aid)
 * This allows existing code to work during migration.
 * Will be removed in Phase 7 when all functions take ctx parameter.
 */
gen_context_t *g_ctx = nullptr;

/*** gen_context_create - allocate a new emulator context ***/

gen_context_t *gen_context_create(void)
{
  gen_context_t *ctx;

  /* Allocate zeroed context */
  ctx = calloc(1, sizeof(gen_context_t));
  if (ctx == nullptr) {
    return nullptr;
  }

  /* Allocate 68k RAM */
  ctx->cpu68k.ram = calloc(1, GEN_CONTEXT_RAM_SIZE);
  if (ctx->cpu68k.ram == nullptr) {
    free(ctx);
    return nullptr;
  }

  /* Allocate Z80 RAM */
  ctx->cpuz80.ram = calloc(1, GEN_CONTEXT_Z80_RAM_SIZE);
  if (ctx->cpuz80.ram == nullptr) {
    free(ctx->cpu68k.ram);
    free(ctx);
    return nullptr;
  }

  return ctx;
}

/*** gen_context_destroy - free an emulator context and all resources ***/

/*
 * External globals from subsystems - used to check if context shares memory
 * with subsystems (in which case we don't free it here).
 */
extern uint8 *cpu68k_ram;
extern uint8 *cpuz80_ram;

void gen_context_destroy(gen_context_t *ctx)
{
  if (ctx == nullptr) {
    return;
  }

  /*
   * Free 68k RAM only if it was allocated by context, not by subsystem.
   * After gen_core_init(), ctx->cpu68k.ram points to cpu68k_ram (global).
   */
  if (ctx->cpu68k.ram != nullptr && ctx->cpu68k.ram != cpu68k_ram) {
    free(ctx->cpu68k.ram);
  }
  ctx->cpu68k.ram = nullptr;

  /* Free ROM if we own it */
  if (ctx->freerom && ctx->cpu68k.rom != nullptr) {
    free(ctx->cpu68k.rom);
    ctx->cpu68k.rom = nullptr;
  }

  /*
   * Free Z80 RAM only if it was allocated by context, not by subsystem.
   */
  if (ctx->cpuz80.ram != nullptr && ctx->cpuz80.ram != cpuz80_ram) {
    free(ctx->cpuz80.ram);
  }
  ctx->cpuz80.ram = nullptr;

  /* Free sound log data if allocated */
  if (ctx->sound.logdata != nullptr) {
    free(ctx->sound.logdata);
    ctx->sound.logdata = nullptr;
  }

  /* Clear global pointer if this was the active context */
  if (g_ctx == ctx) {
    g_ctx = nullptr;
  }

  free(ctx);
}

/*** gen_context_init - initialize context with default values ***/

int gen_context_init(gen_context_t *ctx)
{
  if (ctx == nullptr) {
    return -1;
  }

  /* 68k CPU defaults */
  ctx->cpu68k.rom = nullptr;
  ctx->cpu68k.romlen = 0;
  ctx->cpu68k.pc = 0;
  ctx->cpu68k.sp = 0;
  ctx->cpu68k.sr = 0x2700; /* Supervisor mode, all interrupts masked */
  ctx->cpu68k.stop = 0;
  ctx->cpu68k.pending = 0;
  ctx->cpu68k.clocks = 0;
  ctx->cpu68k.clocks_curevent = 0;
  ctx->cpu68k.frames = 0;
  ctx->cpu68k.line = 0;
  ctx->cpu68k.frozen = 0;
  ctx->cpu68k.totalinstr = 0;
  ctx->cpu68k.totalfuncs = 0;
  ctx->cpu68k.adaptive = 0;
  memset(ctx->cpu68k.regs, 0, sizeof(ctx->cpu68k.regs));
  memset(ctx->cpu68k.ram, 0, GEN_CONTEXT_RAM_SIZE);

  /* Z80 CPU defaults */
  ctx->cpuz80.bank = 0;
  ctx->cpuz80.active = 0;
  ctx->cpuz80.resetting = 1; /* Start in reset state */
  ctx->cpuz80.on = 0;
  memset(ctx->cpuz80.ram, 0, GEN_CONTEXT_Z80_RAM_SIZE);
  memset(&ctx->cpuz80.z80, 0, sizeof(ctx->cpuz80.z80));

  /* VDP defaults (NTSC) */
  ctx->vdp.line = 0;
  ctx->vdp.event = 0;
  ctx->vdp.nextevent = 0;
  ctx->vdp.oddframe = 0;
  ctx->vdp.vblank = 1;
  ctx->vdp.hblank = 0;
  ctx->vdp.vsync = 0;
  ctx->vdp.dmabusy = 0;
  ctx->vdp.pal = 0;        /* NTSC by default */
  ctx->vdp.overseas = 1;   /* USA by default */
  ctx->vdp.address = 0;
  ctx->vdp.code = 0;
  ctx->vdp.ctrlflag = 0;
  ctx->vdp.first = 0;
  ctx->vdp.second = 0;
  ctx->vdp.dmabytes = 0;
  ctx->vdp.hskip_countdown = -1;
  ctx->vdp.fifo_count = 0;
  ctx->vdp.fifofull = 0;
  ctx->vdp.fifoempty = 1;
  ctx->vdp.collision = 0;
  ctx->vdp.overflow = 0;
  ctx->vdp.cramchange = 0;
  memset(ctx->vdp.vram, 0, GEN_CONTEXT_VRAM_SIZE);
  memset(ctx->vdp.cram, 0, GEN_CONTEXT_CRAM_SIZE);
  memset(ctx->vdp.vsram, 0, GEN_CONTEXT_VSRAM_SIZE);
  memset(ctx->vdp.reg, 0, sizeof(ctx->vdp.reg));
  memset(ctx->vdp.cramf, 0, sizeof(ctx->vdp.cramf));

  /* VDP timing - set NTSC defaults */
  ctx->vdp.vislines = 224;
  ctx->vdp.visstartline = 0;
  ctx->vdp.visendline = 224;
  ctx->vdp.totlines = 262;     /* NTSC */
  ctx->vdp.framerate = 60;     /* NTSC */
  ctx->vdp.clock = 53693175;   /* NTSC master clock */
  ctx->vdp.clk68k = ctx->vdp.clock / 7;
  ctx->vdp.clksperline_68k = ctx->vdp.clk68k / ctx->vdp.framerate /
                              ctx->vdp.totlines;

  /* Layer visibility - all enabled */
  ctx->vdp.layerB = 1;
  ctx->vdp.layerBp = 1;
  ctx->vdp.layerA = 1;
  ctx->vdp.layerAp = 1;
  ctx->vdp.layerW = 1;
  ctx->vdp.layerWp = 1;
  ctx->vdp.layerH = 1;
  ctx->vdp.layerS = 1;
  ctx->vdp.layerSp = 1;

  /* Sound defaults */
  ctx->sound.on = 1;
  ctx->sound.psg = 1;
  ctx->sound.fm = 1;
  ctx->sound.filter = 50;     /* 50% low-pass filter */
  ctx->sound.speed = 44100;   /* 44.1kHz */
  ctx->sound.sampsperfield = ctx->sound.speed / ctx->vdp.framerate;
  ctx->sound.threshold = ctx->sound.sampsperfield * 5; /* 5 fields buffer */
  ctx->sound.minfields = 5;
  ctx->sound.maxfields = 10;
  ctx->sound.feedback = 0;
  ctx->sound.debug = 0;
  ctx->sound.logsample = 0;
  ctx->sound.active = 0;
  ctx->sound.logdata = nullptr;
  ctx->sound.logdata_size = 0;
  ctx->sound.logdata_p = 0;
  ctx->sound.fieldhassamples = 0;
  ctx->sound.address1 = 0;
  ctx->sound.address2 = 0;
  memset(ctx->sound.regs1, 0, GEN_CONTEXT_SOUND_REGS_SIZE);
  memset(ctx->sound.regs2, 0, GEN_CONTEXT_SOUND_REGS_SIZE);
  memset(ctx->sound.keys, 0, sizeof(ctx->sound.keys));
  memset(ctx->sound.soundbuf, 0, sizeof(ctx->sound.soundbuf));

  /* Memory/controller defaults */
  memset(&ctx->mem, 0, sizeof(ctx->mem));
  ctx->mem.cont1ctrl = 0;
  ctx->mem.cont2ctrl = 0;
  ctx->mem.contEctrl = 0;
  ctx->mem.cont1output = 0;
  ctx->mem.cont2output = 0;
  ctx->mem.contEoutput = 0;

  /* Cartridge info */
  memset(&ctx->cartinfo, 0, sizeof(ctx->cartinfo));

  /* Other state */
  memset(ctx->leafname, 0, sizeof(ctx->leafname));
  ctx->modifiedrom = 0;
  ctx->freerom = 0;

  /* Configuration defaults */
  ctx->config.debugmode = 0;
  ctx->config.loglevel = GEN_LOG_NORMAL;
  ctx->config.autodetect = 1;  /* Auto-detect PAL/NTSC */

  /* Sound config defaults */
  ctx->config.sound_on = 1;
  ctx->config.sound_psg = 1;
  ctx->config.sound_fm = 1;
  ctx->config.sound_filter = 50;  /* 50% low-pass filter */
  ctx->config.musiclog = GEN_MUSICLOG_OFF;

  /* VDP layer visibility (all enabled by default) */
  ctx->config.vdp_layer_a = 1;
  ctx->config.vdp_layer_b = 1;
  ctx->config.vdp_layer_w = 1;
  ctx->config.vdp_layer_s = 1;

  /* Runtime state */
  ctx->quit = 0;

  /* UI callbacks (none by default) */
  ctx->ui = nullptr;
  ctx->ui_data = nullptr;

  return 0;
}

/*** gen_context_reset - reset context to initial state (preserves ROM) ***/

void gen_context_reset(gen_context_t *ctx)
{
  uint8 *rom;
  unsigned int romlen;
  int freerom;

  if (ctx == nullptr) {
    return;
  }

  /* Save ROM pointer and info */
  rom = ctx->cpu68k.rom;
  romlen = ctx->cpu68k.romlen;
  freerom = ctx->freerom;

  /* Re-initialize context */
  gen_context_init(ctx);

  /* Restore ROM */
  ctx->cpu68k.rom = rom;
  ctx->cpu68k.romlen = romlen;
  ctx->freerom = freerom;
}

/*** Context Accessor Functions (transition aid) ***/

const uint8 *gen_ctx_vdp_reg(void)
{
  return vdp_reg;
}

unsigned int gen_ctx_vdp_pal(void)
{
  return vdp_pal;
}

unsigned int gen_ctx_vdp_framerate(void)
{
  return vdp_framerate;
}

unsigned int gen_ctx_vdp_vislines(void)
{
  return vdp_vislines;
}

uint8 gen_ctx_vdp_oddframe(void)
{
  return vdp_oddframe;
}

unsigned int gen_ctx_sound_threshold(void)
{
  return sound_threshold;
}

int gen_ctx_sound_feedback(void)
{
  return sound_feedback;
}

unsigned int gen_ctx_cpu68k_frames(void)
{
  return cpu68k_frames;
}
