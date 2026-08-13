/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fm_write_queue.cpp - timestamped YM2612 write queue implementation.
 *
 * Pure module: no globals beyond its own state, no dependencies, so it is
 * directly unit-testable (tests/test_fm_write_queue.cpp). Not yet wired into
 * the audio path; sound_ym2612store_at/sound_process integration follows.
 */

#include "fm_write_queue.hpp"

namespace {

struct fmq_entry {
  uint16_t pos_frac;
  uint8_t port;
  uint8_t val;
};

fmq_entry g_queue[FMQ_CAPACITY];
unsigned int g_head = 0;  /* oldest entry index */
unsigned int g_count = 0; /* entries in use */
int g_overflowed = 0;

}  // namespace

void fmq_reset(void)
{
  g_head = 0;
  g_count = 0;
  g_overflowed = 0;
}

void fmq_push(uint16_t pos_frac, uint8_t port, uint8_t val)
{
  if (pos_frac >= FMQ_FRAC_ONE)
    pos_frac = FMQ_FRAC_ONE - 1;

  if (g_count >= FMQ_CAPACITY) {
    /* Degrade gracefully: remember the overflow; the drain path force-applies
     * what it can at position 0, which reproduces the legacy line-collapsed
     * timing rather than corrupting register order. */
    g_overflowed = 1;
    return;
  }

  unsigned int tail = (g_head + g_count) % FMQ_CAPACITY;
  g_queue[tail].pos_frac = pos_frac;
  g_queue[tail].port = port;
  g_queue[tail].val = val;
  g_count++;
}

int fmq_pop(uint16_t limit_frac, uint8_t *port, uint8_t *val)
{
  if (g_count == 0)
    return 0;
  if (g_queue[g_head].pos_frac > limit_frac)
    return 0;

  if (port)
    *port = g_queue[g_head].port;
  if (val)
    *val = g_queue[g_head].val;
  g_head = (g_head + 1) % FMQ_CAPACITY;
  g_count--;
  return 1;
}

uint16_t fmq_peek_pos(void)
{
  if (g_count == 0)
    return FMQ_FRAC_ONE;
  return g_queue[g_head].pos_frac;
}

int fmq_overflowed(void)
{
  return g_overflowed;
}
