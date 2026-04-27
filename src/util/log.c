/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "generator/log.h"

static gen_log_sink_fn g_sink = nullptr;
static void *g_sink_user_data = nullptr;

static void default_sink(int level, const char *msg, void *user_data)
{
  (void)level;
  (void)user_data;
  fprintf(stderr, "%s\n", msg);
}

void gen_log_set_sink(gen_log_sink_fn fn, void *user_data)
{
  g_sink = fn;
  g_sink_user_data = user_data;
}

static void dispatch(int level, const char *fmt, va_list ap)
{
  char buf[1024];
  int written = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (written < 0) {
    snprintf(buf, sizeof(buf), "<log formatting error>");
  } else if ((size_t)written >= sizeof(buf)) {
    static const char suffix[] = "... [truncated]";
    size_t suffix_len = sizeof(suffix) - 1;
    if (sizeof(buf) > suffix_len) {
      memcpy(buf + sizeof(buf) - suffix_len - 1, suffix, suffix_len + 1);
    }
  }
  if (g_sink != nullptr)
    g_sink(level, buf, g_sink_user_data);
  else
    default_sink(level, buf, nullptr);
}

#define DEFINE_EMITTER(name, level)         \
  void gen_log_##name(const char *fmt, ...) \
  {                                         \
    va_list ap;                             \
    va_start(ap, fmt);                      \
    dispatch((level), fmt, ap);             \
    va_end(ap);                             \
  }

DEFINE_EMITTER(debug3, GEN_LOG_DEBUG3)
DEFINE_EMITTER(debug2, GEN_LOG_DEBUG2)
DEFINE_EMITTER(debug1, GEN_LOG_DEBUG1)
DEFINE_EMITTER(user, GEN_LOG_USER)
DEFINE_EMITTER(verbose, GEN_LOG_VERBOSE)
DEFINE_EMITTER(normal, GEN_LOG_NORMAL)
DEFINE_EMITTER(critical, GEN_LOG_CRITICAL)
DEFINE_EMITTER(request, GEN_LOG_NORMAL)
