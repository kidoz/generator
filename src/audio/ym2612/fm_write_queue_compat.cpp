/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Transitional flat API over the System-owned FM write queue. */

#include "fm_write_queue.hpp"

#include "system.hpp"

namespace {

generator::FmWriteQueue *active_queue()
{
  generator::System *active_system = generator::system();
  return active_system == nullptr ? nullptr : &active_system->fm_write_queue();
}

}  // namespace

void fmq_reset()
{
  if (generator::FmWriteQueue *queue = active_queue(); queue != nullptr)
    queue->reset();
}

void fmq_push(uint16_t pos_frac, uint8_t port, uint8_t val)
{
  if (generator::FmWriteQueue *queue = active_queue(); queue != nullptr)
    queue->push(pos_frac, port, val);
}

int fmq_pop(uint16_t limit_frac, uint8_t *port, uint8_t *val)
{
  generator::FmWriteQueue *queue = active_queue();
  return queue != nullptr && queue->pop(limit_frac, port, val);
}

uint16_t fmq_peek_pos()
{
  generator::FmWriteQueue *queue = active_queue();
  return queue == nullptr ? FMQ_FRAC_ONE : queue->peek_pos();
}

int fmq_overflowed()
{
  generator::FmWriteQueue *queue = active_queue();
  return queue != nullptr && queue->overflowed();
}
