/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* UI Callback Interface - Decouples emulator core from UI implementation */

#ifndef GEN_UI_CALLBACKS_H
#define GEN_UI_CALLBACKS_H

#include "machine.h"

/* Forward declaration */
typedef struct gen_context gen_context_t;

/*
 * UI Callback Interface
 *
 * This interface allows the emulator core to communicate with UI backends
 * without direct dependencies. Different backends (GTK4, Console, Headless)
 * implement these callbacks to handle rendering, audio, and logging.
 *
 * All callbacks receive the context pointer for access to emulator state.
 * Callbacks may be nullptr if the backend doesn't need that functionality.
 */

typedef struct gen_ui_callbacks {
  /*
   * Video Callbacks
   */

  /* Called for each visible scanline during rendering.
   * Parameters:
   *   ctx - Emulator context
   *   line - Scanline number (0 to vislines-1)
   * Note: The actual pixel data is accessed through vdp state or uiplot. */
  void (*line)(gen_context_t *ctx, int line);

  /* Called at the end of each video field.
   * This is where the UI should present the completed frame to the display.
   * Parameters:
   *   ctx - Emulator context */
  void (*end_field)(gen_context_t *ctx);

  /*
   * Audio Callbacks
   */

  /* Called to output audio samples.
   * Parameters:
   *   ctx - Emulator context
   *   left - Left channel samples (16-bit signed)
   *   right - Right channel samples (16-bit signed)
   *   samples - Number of samples to output
   * Note: This is called from gensound to send audio to the platform layer. */
  void (*audio_output)(gen_context_t *ctx, const uint16 *left,
                       const uint16 *right, unsigned int samples);

  /*
   * Logging Callbacks
   */

  /* Log a debug level 3 message (most verbose) */
  void (*log_debug3)(gen_context_t *ctx, const char *msg);

  /* Log a debug level 2 message */
  void (*log_debug2)(gen_context_t *ctx, const char *msg);

  /* Log a debug level 1 message */
  void (*log_debug1)(gen_context_t *ctx, const char *msg);

  /* Log a user-directed message */
  void (*log_user)(gen_context_t *ctx, const char *msg);

  /* Log a verbose message */
  void (*log_verbose)(gen_context_t *ctx, const char *msg);

  /* Log a normal message */
  void (*log_normal)(gen_context_t *ctx, const char *msg);

  /* Log a critical error message */
  void (*log_critical)(gen_context_t *ctx, const char *msg);

  /* Log a request message */
  void (*log_request)(gen_context_t *ctx, const char *msg);

  /*
   * Music Logging Callbacks
   */

  /* Called for music logging (GYM/GNM format).
   * Parameters:
   *   ctx - Emulator context
   *   data - Music data to log
   *   length - Length of data in bytes */
  void (*musiclog)(gen_context_t *ctx, const uint8 *data, unsigned int length);

  /*
   * Error Handling
   */

  /* Called for fatal errors. Should not return.
   * Parameters:
   *   ctx - Emulator context
   *   msg - Error message */
  [[noreturn]] void (*fatal_error)(gen_context_t *ctx, const char *msg);

} gen_ui_callbacks_t;

/*
 * No-op callbacks for headless/testing mode.
 * These do nothing but satisfy the interface.
 */

/* No-op implementations for headless backend */
void gen_ui_noop_line(gen_context_t *ctx, int line);
void gen_ui_noop_end_field(gen_context_t *ctx);
void gen_ui_noop_audio_output(gen_context_t *ctx, const uint16 *left,
                               const uint16 *right, unsigned int samples);
void gen_ui_noop_log(gen_context_t *ctx, const char *msg);
void gen_ui_noop_musiclog(gen_context_t *ctx, const uint8 *data,
                           unsigned int length);
[[noreturn]] void gen_ui_noop_fatal_error(gen_context_t *ctx, const char *msg);

/* Pre-defined no-op callback structure for headless mode */
extern const gen_ui_callbacks_t gen_ui_callbacks_noop;

/*
 * Utility Functions
 */

/* Set UI callbacks for a context.
 * If callbacks is nullptr, sets no-op callbacks. */
void gen_ui_set_callbacks(gen_context_t *ctx, const gen_ui_callbacks_t *callbacks,
                          void *ui_data);

/* Call a callback if it exists, otherwise do nothing.
 * These macros safely handle nullptr callbacks. */
#define GEN_UI_CALL(ctx, callback, ...) \
  do { \
    if ((ctx) != nullptr && (ctx)->ui != nullptr && (ctx)->ui->callback != nullptr) { \
      (ctx)->ui->callback((ctx), ##__VA_ARGS__); \
    } \
  } while (0)

/* Call a callback with a return value, or return default if nullptr */
#define GEN_UI_CALL_RET(ctx, callback, default_val, ...) \
  (((ctx) != nullptr && (ctx)->ui != nullptr && (ctx)->ui->callback != nullptr) \
    ? (ctx)->ui->callback((ctx), ##__VA_ARGS__) \
    : (default_val))

#endif /* GEN_UI_CALLBACKS_H */
