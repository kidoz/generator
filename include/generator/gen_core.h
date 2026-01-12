/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* Core API - Clean interface for emulator control */

#ifndef GEN_CORE_H
#define GEN_CORE_H

#include <time.h>
#include "gen_context.h"

/*
 * Core API provides a clean abstraction layer between UI backends
 * and the emulator core. This enables:
 * - Multiple UI backends (GTK4, Console, Headless)
 * - Testable emulation logic
 * - Clear separation of concerns
 */

/*
 * Initialization
 */

/* Initialize the emulator core (CPU tables, sound chips, etc.)
 * Must be called once before using other core functions.
 * Returns 0 on success, non-zero on error. */
int gen_core_init(gen_context_t *ctx);

/* Attach context to already-initialized subsystems.
 * Use this when main() has already called subsystem init functions
 * and you just need to sync the context with global state.
 * Returns 0 on success, non-zero on error. */
int gen_core_attach(gen_context_t *ctx);

/* Shutdown the emulator core (cleanup resources)
 * Should be called before destroying the context. */
void gen_core_shutdown(gen_context_t *ctx);

/*
 * ROM Management
 */

/* Load a ROM image from file.
 * Supports .bin, .smd, .gen, .zip formats.
 * Returns nullptr on success, error message on failure.
 * The returned string should not be freed. */
const char *gen_core_load_rom(gen_context_t *ctx, const char *filename);

/* Load a ROM image from memory.
 * If copy is true, the core will allocate and copy the data.
 * If copy is false, the core takes ownership of the memory.
 * Returns nullptr on success, error message on failure. */
const char *gen_core_load_rom_mem(gen_context_t *ctx, const uint8 *rom,
                                   unsigned int romlen, int copy);

/* Unload the current ROM and reset state. */
void gen_core_unload_rom(gen_context_t *ctx);

/* Check if a ROM is currently loaded. */
int gen_core_rom_loaded(gen_context_t *ctx);

/*
 * Emulation Control
 */

/* Run one frame of emulation.
 * This executes CPU cycles until the next vertical blank.
 * The UI should call this repeatedly in its main loop. */
void gen_core_run_frame(gen_context_t *ctx);

/* Perform a hard reset (equivalent to power cycle). */
void gen_core_reset(gen_context_t *ctx);

/* Perform a soft reset (reset button press). */
void gen_core_soft_reset(gen_context_t *ctx);

/* Pause/resume emulation (affects frame execution). */
void gen_core_pause(gen_context_t *ctx, int paused);

/* Check if emulation is paused. */
int gen_core_is_paused(gen_context_t *ctx);

/*
 * State Management
 */

/* Save emulator state to file.
 * Returns 0 on success, non-zero on error. */
int gen_core_save_state(gen_context_t *ctx, const char *filename);

/* Load emulator state from file.
 * Returns 0 on success, non-zero on error. */
int gen_core_load_state(gen_context_t *ctx, const char *filename);

/* Save emulator state to slot (0-9).
 * Filename is derived from ROM name: <romname>.gt<slot>
 * Returns 0 on success, non-zero on error. */
int gen_core_save_state_slot(gen_context_t *ctx, int slot);

/* Load emulator state from slot (0-9).
 * Returns 0 on success, non-zero on error. */
int gen_core_load_state_slot(gen_context_t *ctx, int slot);

/* Check if a state slot exists.
 * Returns modification time if exists, 0 if not. */
time_t gen_core_state_slot_date(gen_context_t *ctx, int slot);

/*
 * Input
 */

/* Set controller input state for a player (0 or 1).
 * The input struct contains button states. */
void gen_core_set_input(gen_context_t *ctx, int player,
                        unsigned int up, unsigned int down,
                        unsigned int left, unsigned int right,
                        unsigned int a, unsigned int b, unsigned int c,
                        unsigned int start);

/*
 * Audio
 */

/* Start audio output.
 * Returns 0 on success, non-zero on error. */
int gen_core_audio_start(gen_context_t *ctx);

/* Stop audio output. */
void gen_core_audio_stop(gen_context_t *ctx);

/* Pause audio playback. */
void gen_core_audio_pause(gen_context_t *ctx);

/* Resume audio playback. */
void gen_core_audio_resume(gen_context_t *ctx);

/* Get number of audio samples currently buffered. */
int gen_core_audio_samples_buffered(gen_context_t *ctx);

/*
 * Configuration
 */

/* Set video mode (PAL or NTSC).
 * If autodetect is true, mode will be determined from ROM region. */
void gen_core_set_video_mode(gen_context_t *ctx, int pal, int autodetect);

/* Get current video mode.
 * Returns 1 for PAL, 0 for NTSC. */
int gen_core_get_video_mode(gen_context_t *ctx);

/* Get current frame rate (50 for PAL, 60 for NTSC). */
unsigned int gen_core_get_framerate(gen_context_t *ctx);

/* Get visible screen dimensions. */
void gen_core_get_screen_size(gen_context_t *ctx, int *width, int *height);

/*
 * ROM Information
 */

/* Get ROM information (name, region, checksum, etc.)
 * Returns pointer to internal cartinfo struct. */
const gen_cartinfo_t *gen_core_get_rom_info(gen_context_t *ctx);

/* Get ROM leaf name (filename without path). */
const char *gen_core_get_rom_name(gen_context_t *ctx);

/*
 * Debug/Development
 */

/* Set debug mode. */
void gen_core_set_debug(gen_context_t *ctx, int enabled);

/* Set log level (0=quiet, 1=normal, 2=verbose, 3+=debug). */
void gen_core_set_loglevel(gen_context_t *ctx, int level);

/* Get current frame count. */
unsigned int gen_core_get_frame_count(gen_context_t *ctx);

#endif /* GEN_CORE_H */
