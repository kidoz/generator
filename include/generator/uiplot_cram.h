/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CRAM snapshot access for the shared uiplot palette cache.
 *
 * The core publishes its colour RAM and the per-entry dirty flags here at
 * each field boundary; uiplot reads through these accessors rather than
 * including a chip header, which keeps presentation code off the VDP.
 *
 * Entries are the VDP's own 16-bit words (0000BBB0GGG0RRR0), not bytes: a
 * byte view of the same array means something different on a big- and a
 * little-endian host, and getting that wrong silently swaps red and blue. */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Published by the core. `dirty` has one flag per colour, set when the
 * game writes that entry, so the palette cache can skip unchanged ones. */
void uiplot_set_cram(const uint16_t *cram, const uint8_t *dirty);

/* Accessors used by uiplot_checkpalcache. Both return a valid pointer even
 * before the core publishes anything, so tests and early frames read
 * zeroes instead of faulting. */
const uint16_t *uiplot_cram_ptr(void);
const uint8_t *uiplot_cram_dirty_ptr(void);

#ifdef __cplusplus
}
#endif
