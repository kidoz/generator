/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fm_write_queue.cpp - timestamped YM2612 write queue implementation.
 *
 * The queue is directly unit-testable (tests/test_fm_write_queue.cpp). The
 * runtime instance is owned by System and exposed to the legacy audio path
 * through fm_write_queue_compat.cpp.
 */

#include "fm_write_queue.hpp"

namespace generator {

void FmWriteQueue::reset()
{
  m_head = 0;
  m_count = 0;
  m_overflowed = false;
}

void FmWriteQueue::push(uint16_t pos_frac, uint8_t port, uint8_t val)
{
  if (pos_frac >= FMQ_FRAC_ONE)
    pos_frac = FMQ_FRAC_ONE - 1;

  if (m_count >= FMQ_CAPACITY) {
    /* Degrade gracefully: remember the overflow; the drain path force-applies
     * what it can at position 0, which reproduces the legacy line-collapsed
     * timing rather than corrupting register order. */
    m_overflowed = true;
    return;
  }

  const unsigned int tail = (m_head + m_count) % FMQ_CAPACITY;
  m_queue[tail].pos_frac = pos_frac;
  m_queue[tail].port = port;
  m_queue[tail].val = val;
  m_count++;
}

bool FmWriteQueue::pop(uint16_t limit_frac, uint8_t *port, uint8_t *val)
{
  if (m_count == 0)
    return false;
  if (m_queue[m_head].pos_frac > limit_frac)
    return false;

  if (port)
    *port = m_queue[m_head].port;
  if (val)
    *val = m_queue[m_head].val;
  m_head = (m_head + 1) % FMQ_CAPACITY;
  m_count--;
  return true;
}

uint16_t FmWriteQueue::peek_pos() const
{
  if (m_count == 0)
    return FMQ_FRAC_ONE;
  return m_queue[m_head].pos_frac;
}

bool FmWriteQueue::overflowed() const
{
  return m_overflowed;
}

}  // namespace generator
