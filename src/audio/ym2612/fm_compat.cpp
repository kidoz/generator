/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Transitional flat API over the System-owned YM2612 instance. */

#include "support.h"
#include "fm.h"

#include "system.hpp"
#include "ym2612.hpp"

namespace {

generator::Ym2612 *active_ym2612()
{
  generator::System *active_system = generator::system();
  return active_system == nullptr ? nullptr : &active_system->ym2612();
}

}  // namespace

int YM2612Init(int num, int clock, int rate, FM_TIMERHANDLER timer_handler,
               FM_IRQHANDLER irq_handler)
{
  generator::Ym2612 *chip = active_ym2612();
  return chip == nullptr
             ? -1
             : chip->init(num, clock, rate, timer_handler, irq_handler);
}

void YM2612Shutdown()
{
  if (generator::Ym2612 *chip = active_ym2612(); chip != nullptr)
    chip->shutdown();
}

void YM2612ResetChip(int num)
{
  if (generator::Ym2612 *chip = active_ym2612(); chip != nullptr)
    chip->reset_chip(num);
}

void YM2612UpdateOne(int num, INT16 **buffer, int length)
{
  if (generator::Ym2612 *chip = active_ym2612(); chip != nullptr)
    chip->update_one(num, buffer, length);
}

int YM2612Write(int num, int address, UINT8 value)
{
  generator::Ym2612 *chip = active_ym2612();
  return chip == nullptr ? 0 : chip->write(num, address, value);
}

UINT8 YM2612Read(int num, int address)
{
  generator::Ym2612 *chip = active_ym2612();
  return chip == nullptr ? 0 : chip->read(num, address);
}

int YM2612TimerOver(int num, int timer)
{
  generator::Ym2612 *chip = active_ym2612();
  return chip == nullptr ? 0 : chip->timer_over(num, timer);
}

void YM2612_save_state()
{
  if (generator::Ym2612 *chip = active_ym2612(); chip != nullptr)
    chip->save_state();
}
