/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Transitional flat API for the System-owned SN76496 instance. */

#include "sn76496.hpp"

#include "system.hpp"

namespace {

generator::SN76496 *active_psg(int chip)
{
  generator::System *active_system = generator::system();
  if (chip != 0 || active_system == nullptr)
    return nullptr;
  return &active_system->psg();
}

}  // namespace

int SN76496Init(int chip, int clock, int gain, int sample_rate)
{
  generator::SN76496 *psg = active_psg(chip);
  if (psg == nullptr)
    return -1;
  return psg->init(clock, gain, sample_rate);
}

void SN76496Write(int chip, int data)
{
  if (generator::SN76496 *psg = active_psg(chip); psg != nullptr)
    psg->write(data);
}

void SN76496Update(int chip, uint16 *buffer, int length)
{
  if (generator::SN76496 *psg = active_psg(chip); psg != nullptr)
    psg->update(buffer, length);
}

void SN76496_save_state()
{
  if (generator::SN76496 *psg = active_psg(0); psg != nullptr)
    psg->save_state(0);
}
