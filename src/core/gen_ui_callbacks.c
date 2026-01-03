/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* UI Callback Interface - No-op implementation for headless/testing */

#include <stdio.h>
#include <stdlib.h>

#include "gen_ui_callbacks.h"
#include "gen_context.h"

/*** No-op callback implementations ***/

void gen_ui_noop_line(gen_context_t *ctx, int line)
{
  (void)ctx;
  (void)line;
  /* No-op: headless mode doesn't render */
}

void gen_ui_noop_end_field(gen_context_t *ctx)
{
  (void)ctx;
  /* No-op: headless mode doesn't render */
}

void gen_ui_noop_audio_output(gen_context_t *ctx, const uint16 *left,
                               const uint16 *right, unsigned int samples)
{
  (void)ctx;
  (void)left;
  (void)right;
  (void)samples;
  /* No-op: headless mode doesn't play audio */
}

void gen_ui_noop_log(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  (void)msg;
  /* No-op: headless mode doesn't log by default */
}

void gen_ui_noop_musiclog(gen_context_t *ctx, const uint8 *data,
                           unsigned int length)
{
  (void)ctx;
  (void)data;
  (void)length;
  /* No-op: headless mode doesn't log music */
}

[[noreturn]] void gen_ui_noop_fatal_error(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  fprintf(stderr, "Fatal error: %s\n", msg);
  exit(1);
}

/*** Pre-defined no-op callback structure ***/

const gen_ui_callbacks_t gen_ui_callbacks_noop = {
  .line = gen_ui_noop_line,
  .end_field = gen_ui_noop_end_field,
  .audio_output = gen_ui_noop_audio_output,
  .log_debug3 = gen_ui_noop_log,
  .log_debug2 = gen_ui_noop_log,
  .log_debug1 = gen_ui_noop_log,
  .log_user = gen_ui_noop_log,
  .log_verbose = gen_ui_noop_log,
  .log_normal = gen_ui_noop_log,
  .log_critical = gen_ui_noop_log,
  .log_request = gen_ui_noop_log,
  .musiclog = gen_ui_noop_musiclog,
  .fatal_error = gen_ui_noop_fatal_error
};

/*** gen_ui_set_callbacks - Set UI callbacks for a context ***/

void gen_ui_set_callbacks(gen_context_t *ctx, const gen_ui_callbacks_t *callbacks,
                          void *ui_data)
{
  if (ctx == nullptr) {
    return;
  }

  if (callbacks != nullptr) {
    ctx->ui = (gen_ui_callbacks_t *)callbacks;
  } else {
    ctx->ui = (gen_ui_callbacks_t *)&gen_ui_callbacks_noop;
  }

  ctx->ui_data = ui_data;
}
