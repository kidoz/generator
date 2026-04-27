/* SPDX-License-Identifier: GPL-2.0-or-later */
/* machine.h */

/* #include "config.h" */ /* Meson passes all config via compiler flags */

#include <assert.h> /* C11: static_assert support */
#include <stdint.h>

typedef uint8_t uint8;
typedef int8_t sint8;
typedef uint16_t uint16;
typedef int16_t sint16;
typedef uint32_t uint32;
typedef int32_t sint32;

/* C11 Static Assertions: Verify critical type sizes at compile time
   Note: These are wrapped in #ifndef to avoid breaking build tools (def68k,
   gen68k) which compile with older C standards */
#ifndef GENERATOR_BUILD_TOOL
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/* Use C11 static_assert for compile-time type verification */
static_assert(sizeof(uint8) == 1, "uint8 must be exactly 1 byte");
static_assert(sizeof(sint8) == 1, "sint8 must be exactly 1 byte");
static_assert(sizeof(uint16) == 2, "uint16 must be exactly 2 bytes");
static_assert(sizeof(sint16) == 2, "sint16 must be exactly 2 bytes");
static_assert(sizeof(uint32) == 4, "uint32 must be exactly 4 bytes");
static_assert(sizeof(sint32) == 4, "sint32 must be exactly 4 bytes");

/* Ensure correct alignment for performance-critical types */
static_assert(_Alignof(uint16) >= 1,
              "uint16 alignment must be at least 1 byte");
static_assert(_Alignof(uint32) >= 1,
              "uint32 alignment must be at least 1 byte");
#endif
#endif
