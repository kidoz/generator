/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Transitional flat API over the System-owned VDP. */

#include "system.hpp"
#include "vdp.hpp"

namespace {

generator::Vdp *vdp()
{
  generator::System *active_system = generator::system();
  return active_system == nullptr ? nullptr : &active_system->vdp();
}

}  // namespace

void vdp_save_state()
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_save_state();
}

int vdp_init()
{
  generator::Vdp *chip = vdp();
  return chip == nullptr ? -1 : chip->vdp_init();
}

void vdp_setupvideo()
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_setupvideo();
}

void vdp_reset()
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_reset();
}

uint16 vdp_status()
{
  generator::Vdp *chip = vdp();
  return chip == nullptr ? 0 : chip->vdp_status();
}

void vdp_storectrl(uint16 data)
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_storectrl(data);
}

void vdp_storedata(uint16 data)
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_storedata(data);
}

uint16 vdp_fetchdata()
{
  generator::Vdp *chip = vdp();
  return chip == nullptr ? 0 : chip->vdp_fetchdata();
}

void vdp_renderline(unsigned int line, uint8 *linedata, unsigned int odd)
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_renderline(line, linedata, odd);
}

void vdp_renderframe(uint8 *framedata, unsigned int lineoffset)
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_renderframe(framedata, lineoffset);
}

void vdp_showregs()
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_showregs();
}

void vdp_spritelist()
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_spritelist();
}

void vdp_describe()
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_describe();
}

void vdp_endfield()
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_endfield();
}

uint8 vdp_gethpos()
{
  generator::Vdp *chip = vdp();
  return chip == nullptr ? 0 : chip->vdp_gethpos();
}

void vdp_fifo_drain(int count)
{
  if (generator::Vdp *chip = vdp())
    chip->vdp_fifo_drain(count);
}
