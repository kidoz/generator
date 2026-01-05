/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* Core API - Implementation of clean interface for emulator control */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gen_core.h"
#include "generator.h"
#include "cpu68k.h"
#include "cpuz80.h"
#include "vdp.h"
#include "gensound.h"
#include "gensoundp.h"
#include "mem68k.h"
#include "state.h"

/* External functions */
extern void event_doframe(void);
extern int memz80_init(void);

/* Paused state (will move to context in later phase) */
static int core_paused = 0;

/*** gen_core_sync_from_globals - Sync context from subsystem globals ***/

static void gen_core_sync_from_globals(gen_context_t *ctx)
{
  /*
   * Sync context pointers to subsystem globals.
   * This allows code using the context to access the same data
   * as code using the old globals.
   *
   * For pointers: free context's allocation and point to global
   * For values: copy from global to context
   */

  /* Free context's pre-allocated RAM and use subsystem's */
  if (ctx->cpu68k.ram != nullptr && ctx->cpu68k.ram != cpu68k_ram) {
    free(ctx->cpu68k.ram);
  }
  ctx->cpu68k.ram = cpu68k_ram;

  if (ctx->cpuz80.ram != nullptr && ctx->cpuz80.ram != cpuz80_ram) {
    free(ctx->cpuz80.ram);
  }
  ctx->cpuz80.ram = cpuz80_ram;

  /* Sync 68k state - ROM is set by gen_loadimage */
  ctx->cpu68k.rom = cpu68k_rom;
  ctx->cpu68k.romlen = cpu68k_romlen;
  ctx->cpu68k.frames = cpu68k_frames;

  /* Sync Z80 state */
  ctx->cpuz80.bank = cpuz80_bank;
  ctx->cpuz80.active = cpuz80_active;
  ctx->cpuz80.resetting = cpuz80_resetting;
  ctx->cpuz80.z80 = cpuz80_z80;

  /* Sync VDP timing (arrays are copied at save/load time) */
  ctx->vdp.pal = vdp_pal;
  ctx->vdp.overseas = vdp_overseas;
  ctx->vdp.framerate = vdp_framerate;
  ctx->vdp.vislines = vdp_vislines;
  ctx->vdp.totlines = vdp_totlines;

  /* Sync sound state */
  ctx->sound.on = sound_on;
  ctx->sound.psg = sound_psg;
  ctx->sound.fm = sound_fm;
  ctx->sound.filter = sound_filter;
  ctx->sound.speed = sound_speed;
  ctx->sound.sampsperfield = sound_sampsperfield;
  ctx->sound.threshold = sound_threshold;

  /* Sync config */
  ctx->config.debugmode = gen_debugmode;
  ctx->config.loglevel = gen_loglevel;
  ctx->config.autodetect = gen_autodetect;
  ctx->config.musiclog = (gen_musiclog_t)gen_musiclog;
}

/*** gen_core_init - Initialize the emulator core ***/

int gen_core_init(gen_context_t *ctx)
{
  if (ctx == nullptr) {
    return -1;
  }

  /* Set the global context for transition period */
  g_ctx = ctx;

  /* Initialize subsystems
   * Note: These still use global state, will be migrated in later phases */
  if (mem68k_init() != 0) {
    return -1;
  }

  if (memz80_init() != 0) {
    return -1;
  }

  if (vdp_init() != 0) {
    return -1;
  }

  if (cpu68k_init() != 0) {
    return -1;
  }

  cpuz80_init();

  if (sound_init() != 0) {
    return -1;
  }

  /* Sync context with subsystem globals */
  gen_core_sync_from_globals(ctx);

  return 0;
}

/*** gen_core_attach - Attach context to already-initialized subsystems ***/

int gen_core_attach(gen_context_t *ctx)
{
  if (ctx == nullptr) {
    return -1;
  }

  /* Set the global context for transition period */
  g_ctx = ctx;

  /* Sync context with subsystem globals (already initialized by main()) */
  gen_core_sync_from_globals(ctx);

  return 0;
}

/*** gen_core_shutdown - Shutdown the emulator core ***/

void gen_core_shutdown(gen_context_t *ctx)
{
  if (ctx == nullptr) {
    return;
  }

  /* Stop audio if running */
  gen_core_audio_stop(ctx);

  /* Unload ROM if loaded */
  gen_core_unload_rom(ctx);
}

/*** gen_core_load_rom - Load a ROM from file ***/

const char *gen_core_load_rom(gen_context_t *ctx, const char *filename)
{
  char *error;

  if (ctx == nullptr || filename == nullptr) {
    return "Invalid parameters";
  }

  /* Use existing loader (will be refactored in later phase) */
  error = gen_loadimage(filename);

  return error;
}

/*** gen_core_load_rom_mem - Load a ROM from memory ***/

const char *gen_core_load_rom_mem(gen_context_t *ctx, const uint8 *rom,
                                   unsigned int romlen, int copy)
{
  if (ctx == nullptr || rom == nullptr || romlen == 0) {
    return "Invalid parameters";
  }

  /* Use existing memory loader */
  if (copy) {
    /* Allocate and copy */
    uint8 *romcopy = malloc(romlen);
    if (romcopy == nullptr) {
      return "Out of memory";
    }
    memcpy(romcopy, rom, romlen);
    gen_loadmemrom((const char *)romcopy, romlen);
    ctx->freerom = 1;
  } else {
    gen_loadmemrom((const char *)rom, romlen);
    ctx->freerom = 0;
  }

  return nullptr;
}

/*** gen_core_unload_rom - Unload the current ROM ***/

void gen_core_unload_rom(gen_context_t *ctx)
{
  if (ctx == nullptr) {
    return;
  }

  /* Free ROM if we own it */
  if (ctx->freerom && cpu68k_rom != nullptr) {
    free(cpu68k_rom);
  }

  cpu68k_rom = nullptr;
  cpu68k_romlen = 0;
  ctx->freerom = 0;

  /* Clear cartridge info */
  memset(&gen_cartinfo, 0, sizeof(gen_cartinfo));
  memset(gen_leafname, 0, 128); /* gen_leafname is 128 bytes */
}

/*** gen_core_rom_loaded - Check if a ROM is loaded ***/

int gen_core_rom_loaded(gen_context_t *ctx)
{
  if (ctx == nullptr) {
    return 0;
  }

  return (cpu68k_rom != nullptr && cpu68k_romlen > 0);
}

/*** gen_core_run_frame - Run one frame of emulation ***/

void gen_core_run_frame(gen_context_t *ctx)
{
  if (ctx == nullptr || core_paused) {
    return;
  }

  /* Use existing frame loop (will be refactored) */
  event_doframe();
}

/*** gen_core_reset - Perform a hard reset ***/

void gen_core_reset(gen_context_t *ctx)
{
  if (ctx == nullptr) {
    return;
  }

  gen_reset();
}

/*** gen_core_soft_reset - Perform a soft reset ***/

void gen_core_soft_reset(gen_context_t *ctx)
{
  if (ctx == nullptr) {
    return;
  }

  gen_softreset();
}

/*** gen_core_pause - Pause/resume emulation ***/

void gen_core_pause(gen_context_t *ctx, int paused)
{
  (void)ctx;
  core_paused = paused;

  if (paused) {
    gen_core_audio_pause(ctx);
  } else {
    gen_core_audio_resume(ctx);
  }
}

/*** gen_core_is_paused - Check if emulation is paused ***/

int gen_core_is_paused(gen_context_t *ctx)
{
  (void)ctx;
  return core_paused;
}

/*** gen_core_save_state - Save emulator state to file ***/

int gen_core_save_state(gen_context_t *ctx, const char *filename)
{
  if (ctx == nullptr || filename == nullptr) {
    return -1;
  }

  return state_savefile(filename);
}

/*** gen_core_load_state - Load emulator state from file ***/

int gen_core_load_state(gen_context_t *ctx, const char *filename)
{
  if (ctx == nullptr || filename == nullptr) {
    return -1;
  }

  return state_loadfile(filename);
}

/*** gen_core_save_state_slot - Save to numbered slot ***/

int gen_core_save_state_slot(gen_context_t *ctx, int slot)
{
  if (ctx == nullptr || slot < 0 || slot > 9) {
    return -1;
  }

  /* Use existing slot-based save */
  return state_save(slot);
}

/*** gen_core_load_state_slot - Load from numbered slot ***/

int gen_core_load_state_slot(gen_context_t *ctx, int slot)
{
  if (ctx == nullptr || slot < 0 || slot > 9) {
    return -1;
  }

  /* Use existing slot-based load */
  return state_load(slot);
}

/*** gen_core_state_slot_date - Get slot modification date ***/

time_t gen_core_state_slot_date(gen_context_t *ctx, int slot)
{
  if (ctx == nullptr || slot < 0 || slot > 9) {
    return 0;
  }

  return state_date(slot);
}

/*** gen_core_set_input - Set controller input state ***/

void gen_core_set_input(gen_context_t *ctx, int player,
                        unsigned int up, unsigned int down,
                        unsigned int left, unsigned int right,
                        unsigned int a, unsigned int b, unsigned int c,
                        unsigned int start)
{
  if (ctx == nullptr || player < 0 || player > 1) {
    return;
  }

  mem68k_cont[player].up = up;
  mem68k_cont[player].down = down;
  mem68k_cont[player].left = left;
  mem68k_cont[player].right = right;
  mem68k_cont[player].a = a;
  mem68k_cont[player].b = b;
  mem68k_cont[player].c = c;
  mem68k_cont[player].start = start;
}

/*** gen_core_audio_start - Start audio output ***/

int gen_core_audio_start(gen_context_t *ctx)
{
  (void)ctx;
  return soundp_start();
}

/*** gen_core_audio_stop - Stop audio output ***/

void gen_core_audio_stop(gen_context_t *ctx)
{
  (void)ctx;
  soundp_stop();
}

/*** gen_core_audio_pause - Pause audio playback ***/

void gen_core_audio_pause(gen_context_t *ctx)
{
  (void)ctx;
  soundp_pause();
}

/*** gen_core_audio_resume - Resume audio playback ***/

void gen_core_audio_resume(gen_context_t *ctx)
{
  (void)ctx;
  soundp_resume();
}

/*** gen_core_audio_samples_buffered - Get buffered sample count ***/

int gen_core_audio_samples_buffered(gen_context_t *ctx)
{
  (void)ctx;
  return soundp_samplesbuffered();
}

/*** gen_core_set_video_mode - Set PAL/NTSC mode ***/

void gen_core_set_video_mode(gen_context_t *ctx, int pal, int autodetect)
{
  if (ctx == nullptr) {
    return;
  }

  gen_autodetect = autodetect;
  if (!autodetect) {
    vdp_pal = pal;
    vdp_setupvideo();
  }
}

/*** gen_core_get_video_mode - Get current video mode ***/

int gen_core_get_video_mode(gen_context_t *ctx)
{
  (void)ctx;
  return vdp_pal;
}

/*** gen_core_get_framerate - Get current frame rate ***/

unsigned int gen_core_get_framerate(gen_context_t *ctx)
{
  (void)ctx;
  return vdp_framerate;
}

/*** gen_core_get_screen_size - Get visible screen dimensions ***/

void gen_core_get_screen_size(gen_context_t *ctx, int *width, int *height)
{
  (void)ctx;

  if (width != nullptr) {
    /* Check H40 mode (vdp_reg[12] bit 0) */
    *width = (vdp_reg[12] & 1) ? 320 : 256;
  }

  if (height != nullptr) {
    *height = vdp_vislines;
  }
}

/*** gen_core_get_rom_info - Get ROM information ***/

const gen_cartinfo_t *gen_core_get_rom_info(gen_context_t *ctx)
{
  if (ctx == nullptr) {
    return nullptr;
  }

  /* For now, return pointer to global (will be ctx->cartinfo later) */
  return (const gen_cartinfo_t *)&gen_cartinfo;
}

/*** gen_core_get_rom_name - Get ROM leaf name ***/

const char *gen_core_get_rom_name(gen_context_t *ctx)
{
  (void)ctx;
  return gen_leafname;
}

/*** gen_core_set_debug - Set debug mode ***/

void gen_core_set_debug(gen_context_t *ctx, int enabled)
{
  (void)ctx;
  gen_debugmode = enabled;
}

/*** gen_core_set_loglevel - Set log level ***/

void gen_core_set_loglevel(gen_context_t *ctx, int level)
{
  (void)ctx;
  gen_loglevel = level;
}

/*** gen_core_get_frame_count - Get current frame count ***/

unsigned int gen_core_get_frame_count(gen_context_t *ctx)
{
  (void)ctx;
  return cpu68k_frames;
}
