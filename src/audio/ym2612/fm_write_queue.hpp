/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fm_write_queue.hpp - timestamped YM2612 write queue.
 *
 * Z80-originated YM2612 register writes must apply at the sample position
 * corresponding to the Z80 cycle that produced them, not at the scanline
 * boundary. The Z80 core runs in one burst per scanline (cpuz80_sync), so all
 * of a line's writes currently collapse to the line start. This queue holds
 * (position, port, value) entries pushed during the burst and drained in order
 * by the mixer at the matching sample offset (see sound_process).
 *
 * Positions are expressed as pos_frac in [0, FMQ_FRAC_ONE): the write's
 * location within the current scanline burst, in 1/4096 units. The caller
 * (Z80 adapter) derives it from cycles_since_reset()/burst_budget.
 *
 * FIFO ordering is load-bearing: the YM2612 has address/data latch pairs, so
 * an address byte must always be applied before the data bytes that follow it.
 */

#ifndef FM_WRITE_QUEUE_HPP
#define FM_WRITE_QUEUE_HPP

#include <stdint.h>

#define FMQ_FRAC_ONE 4096u /* pos_frac units per scanline (12-bit fraction) */
#define FMQ_CAPACITY                                      \
  256u /* entries; a scanline is ~228 Z80 cycles, so this \
        * cannot realistically overflow */

/* Reset the queue to empty and clear the overflow flag. Call at frame start
 * (or on sound reset) to discard stale entries. */
void fmq_reset(void);

/* Queue one YM2612 port write at the given position within the scanline.
 * pos_frac >= FMQ_FRAC_ONE is clamped to FMQ_FRAC_ONE - 1. On overflow the
 * entry is dropped and the sticky overflow flag is set; the drain then
 * force-applies remaining entries at position 0 (degrades to the legacy
 * apply-at-line-start behavior). */
void fmq_push(uint16_t pos_frac, uint8_t port, uint8_t val);

/* Pop the oldest entry whose position is <= limit_frac. Returns true and
 * fills *port/*val when an entry was popped; false when the queue is empty or
 * the oldest entry lies beyond limit_frac. Entries are returned strictly in
 * push order. */
int fmq_pop(uint16_t limit_frac, uint8_t *port, uint8_t *val);

/* Peek the oldest entry's position without popping it. Returns FMQ_FRAC_ONE
 * when the queue is empty (i.e. "nothing pending"). */
uint16_t fmq_peek_pos(void);

/* Sticky overflow flag (see fmq_push). */
int fmq_overflowed(void);

#endif /* FM_WRITE_QUEUE_HPP */
