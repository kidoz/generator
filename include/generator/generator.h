#ifndef GENERATOR_GENERATOR_H
#define GENERATOR_GENERATOR_H

/* #include "config.h" */ /* Meson passes all config via compiler flags */
#include "machine.h"
#include "log.h"
#include <signal.h> /* For sig_atomic_t type used by gen_quit */

/* VERSION set by autoconf */
/* PACKAGE set by autoconf */

#if defined(__linux__) || defined(linux)
#include <byteswap.h>
#define SWAP16(x) bswap_16((x))
#define SWAP32(x) bswap_32((x))
#elif defined(__OpenBSD__)
#include <machine/endian.h>
#define SWAP16(x) bswap_16((x))
#define SWAP32(x) bswap_32((x))
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define SWAP16(x) OSSwapInt16((x))
#define SWAP32(x) OSSwapInt32((x))
#else
#define SWAP16(y) ((((y) >> 8) & 0x00ff) | ((((y) << 8) & 0xff00)))
#define SWAP32(y)                                           \
  ((((y) >> 24) & 0x000000ff) | (((y) >> 8) & 0x0000ff00) | \
   (((y) << 8) & 0x00ff0000) | (((y) << 24) & 0xff000000))
#warning "No native byte conversion"
#endif

/*
 * LOCENDIANxx takes data that came from a big endian source and converts it
 * into the local endian.  On a big endian machine the data will already be
 * loaded correctly, however on a little endian machine the processor will
 * have loaded the data assuming little endian data, so we need to swap the
 * byte ordering.
 *
 * LOCENDIANxxL takes data that came from a little endian source and
 * converts it into the local endian.  This means that on a little endian
 * machine the data will already be loaded correctly, however on a big
 * endian machine the processor will have loaded the data assuming big endian
 * data, so we need to swap the byte ordering.
 *
 * Both LOCENDIANxx and LOCENDIANxxL can be used in reverse - i.e. when
 * you have data in local endian that you need to write in big (LOCENDIANxx)
 * or little (LOCENDIANxxL) endian.
 *
 */

#ifdef WORDS_BIGENDIAN
#define LOCENDIAN16(y) (y)
#define LOCENDIAN32(y) (y)
#define LOCENDIAN16L(y) SWAP16(y)
#define LOCENDIAN32L(y) SWAP32(y)
#define BYTES_HIGHFIRST 1
#else
#define LOCENDIAN16(y) SWAP16(y)
#define LOCENDIAN32(y) SWAP32(y)
#define LOCENDIAN16L(y) (y)
#define LOCENDIAN32L(y) (y)
#endif

typedef enum {
  pt_unknown,
  pt_game,
  pt_education
} t_prodtype;

typedef struct {
  char console[17];
  char copyright[17];
  char name_domestic[49];
  char name_overseas[49];
  t_prodtype prodtype;
  char version[13];
  uint16 checksum;
  char memo[29];
  char country[17];
  uint8 flag_japan; /* old style JUE flags */
  uint8 flag_usa;
  uint8 flag_europe;
  uint8 hardware; /* new style 4-bit bitmap, 0=japan,2=us,3=europe */
} t_cartinfo;

/* Process-wide settings owned by src/app/generator.cpp. gen_loglevel is
 * declared by log.h, which the logging macros need on its own. */
extern volatile sig_atomic_t gen_quit; /* Signal-safe flag for clean shutdown */
extern unsigned int gen_modifiedrom;   /* set when a patch is applied */

#endif /* GENERATOR_GENERATOR_H */
