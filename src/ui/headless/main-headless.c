/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* Headless Backend - Run emulator without UI for testing/benchmarking */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#include "gen_context.h"
#include "gen_core.h"
#include "gen_ui_callbacks.h"
#include "generator.h"

/* Version info */
#ifndef VERSION
#define VERSION "0.35"
#endif

/* Default number of frames to run */
#define DEFAULT_FRAMES 600

/* Command line options */
static struct option long_options[] = {
  {"help",       no_argument,       0, 'h'},
  {"version",    no_argument,       0, 'v'},
  {"frames",     required_argument, 0, 'f'},
  {"verbose",    no_argument,       0, 'V'},
  {"quiet",      no_argument,       0, 'q'},
  {"load-state", required_argument, 0, 'l'},
  {"save-state", required_argument, 0, 's'},
  {0, 0, 0, 0}
};

static void print_usage(const char *progname)
{
  printf("Usage: %s [OPTIONS] <rom-file>\n", progname);
  printf("\n");
  printf("Run Genesis emulator in headless mode (no display, no audio).\n");
  printf("Useful for benchmarking, testing, and automated processing.\n");
  printf("\n");
  printf("Options:\n");
  printf("  -h, --help          Show this help message\n");
  printf("  -v, --version       Show version information\n");
  printf("  -f, --frames N      Run N frames (default: %d)\n", DEFAULT_FRAMES);
  printf("  -l, --load-state F  Load state from file before running\n");
  printf("  -s, --save-state F  Save state to file after running\n");
  printf("  -V, --verbose       Enable verbose output\n");
  printf("  -q, --quiet         Suppress all output except errors\n");
  printf("\n");
  printf("Examples:\n");
  printf("  %s game.bin                        # Run 600 frames\n", progname);
  printf("  %s -f 3600 game.bin                # Run 1 minute (60fps * 60s)\n", progname);
  printf("  %s -s state.gt0 -f 60 game.bin     # Run 60 frames and save state\n", progname);
  printf("  %s -l state.gt0 -f 60 game.bin     # Load state and run 60 frames\n", progname);
}

static void print_version(void)
{
  printf("Generator Headless Backend v%s\n", VERSION);
  printf("Sega Genesis/Mega Drive Emulator\n");
  printf("(c) James Ponder 1997-2003\n");
}

/* Verbose logging callback */
static int verbose_mode = 0;
static int quiet_mode = 0;

static void headless_log_verbose(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  if (verbose_mode && !quiet_mode) {
    printf("[VERBOSE] %s\n", msg);
  }
}

static void headless_log_normal(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  if (!quiet_mode) {
    printf("[INFO] %s\n", msg);
  }
}

static void headless_log_critical(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  fprintf(stderr, "[ERROR] %s\n", msg);
}

/* Headless callbacks - mostly no-ops but with optional logging */
static gen_ui_callbacks_t headless_callbacks = {
  .line = gen_ui_noop_line,
  .end_field = gen_ui_noop_end_field,
  .audio_output = gen_ui_noop_audio_output,
  .log_debug3 = gen_ui_noop_log,
  .log_debug2 = gen_ui_noop_log,
  .log_debug1 = gen_ui_noop_log,
  .log_user = headless_log_normal,
  .log_verbose = headless_log_verbose,
  .log_normal = headless_log_normal,
  .log_critical = headless_log_critical,
  .log_request = headless_log_normal,
  .musiclog = gen_ui_noop_musiclog,
  .fatal_error = gen_ui_noop_fatal_error
};

int main(int argc, char *argv[])
{
  gen_context_t *ctx;
  const char *rom_file = nullptr;
  const char *load_state_file = nullptr;
  const char *save_state_file = nullptr;
  const char *error;
  unsigned int num_frames = DEFAULT_FRAMES;
  unsigned int frame;
  int opt;
  clock_t start_time, end_time;
  double elapsed;

  /* Parse command line options */
  while ((opt = getopt_long(argc, argv, "hvf:Vql:s:", long_options, nullptr)) != -1) {
    switch (opt) {
    case 'h':
      print_usage(argv[0]);
      return 0;
    case 'v':
      print_version();
      return 0;
    case 'f':
      num_frames = (unsigned int)atoi(optarg);
      if (num_frames == 0) {
        fprintf(stderr, "Error: Invalid frame count\n");
        return 1;
      }
      break;
    case 'l':
      load_state_file = optarg;
      break;
    case 's':
      save_state_file = optarg;
      break;
    case 'V':
      verbose_mode = 1;
      break;
    case 'q':
      quiet_mode = 1;
      break;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  /* Get ROM file argument */
  if (optind >= argc) {
    fprintf(stderr, "Error: No ROM file specified\n");
    print_usage(argv[0]);
    return 1;
  }
  rom_file = argv[optind];

  if (!quiet_mode) {
    print_version();
    printf("\n");
    printf("ROM file: %s\n", rom_file);
    printf("Frames to run: %u\n", num_frames);
    printf("\n");
  }

  /* Create emulator context */
  ctx = gen_context_create();
  if (ctx == nullptr) {
    fprintf(stderr, "Error: Failed to create emulator context\n");
    return 1;
  }

  /* Initialize context with defaults */
  if (gen_context_init(ctx) != 0) {
    fprintf(stderr, "Error: Failed to initialize emulator context\n");
    gen_context_destroy(ctx);
    return 1;
  }

  /* Set headless callbacks */
  gen_ui_set_callbacks(ctx, &headless_callbacks, nullptr);

  /* Initialize core */
  if (gen_core_init(ctx) != 0) {
    fprintf(stderr, "Error: Failed to initialize emulator core\n");
    gen_context_destroy(ctx);
    return 1;
  }

  /* Load ROM */
  error = gen_core_load_rom(ctx, rom_file);
  if (error != nullptr) {
    fprintf(stderr, "Error: Failed to load ROM: %s\n", error);
    gen_core_shutdown(ctx);
    gen_context_destroy(ctx);
    return 1;
  }

  if (!quiet_mode) {
    const gen_cartinfo_t *info = gen_core_get_rom_info(ctx);
    if (info != nullptr) {
      printf("Loaded: %s\n", info->name_overseas[0] ? info->name_overseas
                                                     : info->name_domestic);
      printf("Region: %s%s%s\n",
             info->flag_japan ? "J" : "",
             info->flag_usa ? "U" : "",
             info->flag_europe ? "E" : "");
    }
  }

  /* Load state if specified */
  if (load_state_file != nullptr) {
    if (gen_core_load_state(ctx, load_state_file) != 0) {
      fprintf(stderr, "Error: Failed to load state from: %s\n", load_state_file);
      gen_core_shutdown(ctx);
      gen_context_destroy(ctx);
      return 1;
    }
    if (!quiet_mode) {
      printf("State loaded from: %s\n", load_state_file);
    }
  }

  if (!quiet_mode) {
    printf("\n");
    printf("Running %u frames...\n", num_frames);
  }

  /* Run emulation */
  start_time = clock();

  for (frame = 0; frame < num_frames; frame++) {
    gen_core_run_frame(ctx);

    /* Check for quit signal */
    if (ctx->quit) {
      if (!quiet_mode) {
        printf("Quit signal received at frame %u\n", frame);
      }
      break;
    }

    /* Progress output every 10 seconds of emulated time */
    if (verbose_mode && !quiet_mode && frame > 0 &&
        (frame % (gen_core_get_framerate(ctx) * 10)) == 0) {
      printf("Frame %u / %u (%.1f%%)\n", frame, num_frames,
             100.0 * frame / num_frames);
    }
  }

  end_time = clock();
  elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;

  /* Report results */
  if (!quiet_mode) {
    printf("\n");
    printf("Completed %u frames in %.2f seconds\n", frame, elapsed);
    printf("Average: %.2f frames/sec (%.2fx realtime at %uhz)\n",
           frame / elapsed,
           (frame / elapsed) / gen_core_get_framerate(ctx),
           gen_core_get_framerate(ctx));
  }

  /* Save state if specified */
  if (save_state_file != nullptr) {
    if (gen_core_save_state(ctx, save_state_file) != 0) {
      fprintf(stderr, "Error: Failed to save state to: %s\n", save_state_file);
      gen_core_shutdown(ctx);
      gen_context_destroy(ctx);
      return 1;
    }
    if (!quiet_mode) {
      printf("State saved to: %s\n", save_state_file);
    }
  }

  /* Cleanup */
  gen_core_shutdown(ctx);
  gen_context_destroy(ctx);

  return 0;
}

/*
 * UI interface stubs required by existing code.
 * These are minimal implementations for headless mode.
 */

#include "ui.h"
#include <stdarg.h>

int ui_init(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  return 0;
}

int ui_loop(void)
{
  return 0;
}

void ui_line(int line)
{
  (void)line;
}

void ui_endfield(void)
{
}

void ui_final(void)
{
}

void ui_log_debug3(const char *text, ...)
{
  (void)text;
}

void ui_log_debug2(const char *text, ...)
{
  (void)text;
}

void ui_log_debug1(const char *text, ...)
{
  (void)text;
}

void ui_log_user(const char *text, ...)
{
  va_list args;
  if (quiet_mode) return;
  va_start(args, text);
  vprintf(text, args);
  va_end(args);
  printf("\n");
}

void ui_log_verbose(const char *text, ...)
{
  va_list args;
  if (!verbose_mode || quiet_mode) return;
  va_start(args, text);
  vprintf(text, args);
  va_end(args);
  printf("\n");
}

void ui_log_normal(const char *text, ...)
{
  va_list args;
  if (quiet_mode) return;
  va_start(args, text);
  vprintf(text, args);
  va_end(args);
  printf("\n");
}

void ui_log_critical(const char *text, ...)
{
  va_list args;
  va_start(args, text);
  vfprintf(stderr, text, args);
  va_end(args);
  fprintf(stderr, "\n");
}

void ui_log_request(const char *text, ...)
{
  va_list args;
  if (quiet_mode) return;
  va_start(args, text);
  vprintf(text, args);
  va_end(args);
  printf("\n");
}

[[noreturn]] void ui_err(const char *text, ...)
{
  va_list args;
  va_start(args, text);
  vfprintf(stderr, text, args);
  va_end(args);
  fprintf(stderr, "\n");
  exit(1);
}

void ui_musiclog(uint8 *data, unsigned int length)
{
  (void)data;
  (void)length;
}
