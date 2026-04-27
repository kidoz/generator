/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GENERATOR_LOG_H
#define GENERATOR_LOG_H

#include <stdarg.h>

/* Runtime log levels (higher = more verbose). */
#define GEN_LOG_NONE 0
#define GEN_LOG_CRITICAL 1
#define GEN_LOG_NORMAL 2
#define GEN_LOG_VERBOSE 3
#define GEN_LOG_USER 4
#define GEN_LOG_DEBUG1 5
#define GEN_LOG_DEBUG2 6
#define GEN_LOG_DEBUG3 7

#if defined(__GNUC__) || defined(__clang__)
#define GEN_LOG_PRINTF(fmt_index, first_arg) \
  __attribute__((format(printf, fmt_index, first_arg)))
#else
#define GEN_LOG_PRINTF(fmt_index, first_arg)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Verbosity threshold owned by the application. Defined in src/app/generator.c
   (and in standalone test executables). The LOG_* macros gate on this before
   formatting, so messages below threshold cost only a comparison. */
extern unsigned int gen_loglevel;

/* Sink callback. msg is a NUL-terminated formatted line without trailing
   newline. level is one of GEN_LOG_* (never GEN_LOG_NONE). user_data is
   whatever was passed to gen_log_set_sink(). */
typedef void (*gen_log_sink_fn)(int level, const char *msg, void *user_data);

/* Install a sink. Pass nullptr to revert to the built-in default
   (single line to stderr). Not thread-safe; call once during UI init. */
void gen_log_set_sink(gen_log_sink_fn fn, void *user_data);

/* Level-bound emitters. Call sites use the LOG_* macros; these are the
   targets the macros dispatch to after the level check. */
void gen_log_debug3(const char *fmt, ...) GEN_LOG_PRINTF(1, 2);
void gen_log_debug2(const char *fmt, ...) GEN_LOG_PRINTF(1, 2);
void gen_log_debug1(const char *fmt, ...) GEN_LOG_PRINTF(1, 2);
void gen_log_user(const char *fmt, ...) GEN_LOG_PRINTF(1, 2);
void gen_log_verbose(const char *fmt, ...) GEN_LOG_PRINTF(1, 2);
void gen_log_normal(const char *fmt, ...) GEN_LOG_PRINTF(1, 2);
void gen_log_critical(const char *fmt, ...) GEN_LOG_PRINTF(1, 2);
void gen_log_request(const char *fmt, ...) GEN_LOG_PRINTF(1, 2);

#ifdef __cplusplus
}
#endif

/* LOG_X(...) — standard variadic macro. Defining NOLOGGING removes them entirely. */
#ifdef NOLOGGING
#define LOG_DEBUG3(...) ((void)0)
#define LOG_DEBUG2(...) ((void)0)
#define LOG_DEBUG1(...) ((void)0)
#define LOG_USER(...) ((void)0)
#define LOG_VERBOSE(...) ((void)0)
#define LOG_NORMAL(...) ((void)0)
#define LOG_CRITICAL(...) ((void)0)
#define LOG_REQUEST(...) ((void)0)
#else
#define LOG_DEBUG3(...)                 \
  do {                                  \
    if (gen_loglevel >= GEN_LOG_DEBUG3) \
      gen_log_debug3(__VA_ARGS__);      \
  } while (0)
#define LOG_DEBUG2(...)                 \
  do {                                  \
    if (gen_loglevel >= GEN_LOG_DEBUG2) \
      gen_log_debug2(__VA_ARGS__);      \
  } while (0)
#define LOG_DEBUG1(...)                 \
  do {                                  \
    if (gen_loglevel >= GEN_LOG_DEBUG1) \
      gen_log_debug1(__VA_ARGS__);      \
  } while (0)
#define LOG_USER(...)                 \
  do {                                \
    if (gen_loglevel >= GEN_LOG_USER) \
      gen_log_user(__VA_ARGS__);      \
  } while (0)
#define LOG_VERBOSE(...)                 \
  do {                                   \
    if (gen_loglevel >= GEN_LOG_VERBOSE) \
      gen_log_verbose(__VA_ARGS__);      \
  } while (0)
#define LOG_NORMAL(...)                 \
  do {                                  \
    if (gen_loglevel >= GEN_LOG_NORMAL) \
      gen_log_normal(__VA_ARGS__);      \
  } while (0)
#define LOG_CRITICAL(...)                 \
  do {                                    \
    if (gen_loglevel >= GEN_LOG_CRITICAL) \
      gen_log_critical(__VA_ARGS__);      \
  } while (0)
#define LOG_REQUEST(...)                \
  do {                                  \
    if (gen_loglevel >= GEN_LOG_NORMAL) \
      gen_log_request(__VA_ARGS__);     \
  } while (0)
#endif

#endif /* GENERATOR_LOG_H */
