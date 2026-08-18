/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Transitional flat API over the System-owned Z80. */

#include "cpuz80.hpp"
#include "system.hpp"

namespace {

generator::Cpuz80 *active_z80()
{
  generator::System *active_system = generator::system();
  return active_system == nullptr ? nullptr : &active_system->z80();
}

}  // namespace

int cpuz80_init(void)
{
  generator::Cpuz80 *cpu = active_z80();
  return cpu == nullptr ? -1 : cpu->init();
}

void cpuz80_reset(void)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->reset();
}

void cpuz80_resetcpu(void)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->reset_cpu();
}

void cpuz80_unresetcpu(void)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->unreset_cpu();
}

void cpuz80_updatecontext(void)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->update_context();
}

void cpuz80_bankwrite(uint8_t data)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->bank_write(data);
}

void cpuz80_stop(void)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->stop();
}

void cpuz80_start(void)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->start();
}

void cpuz80_endfield(void)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->end_field();
}

void cpuz80_sync(void)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->sync();
}

unsigned int cpuz80_getburstpos(void)
{
  generator::Cpuz80 *cpu = active_z80();
  return cpu == nullptr ? 0 : cpu->burst_position();
}

void cpuz80_interrupt(void)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->interrupt();
}

void cpuz80_uninterrupt(void)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->uninterrupt();
}

uint8_t cpuz80_portread(uint8_t port)
{
  generator::Cpuz80 *cpu = active_z80();
  return cpu == nullptr ? 0 : cpu->port_read(port);
}

void cpuz80_portwrite(uint8_t port, uint8_t value)
{
  if (generator::Cpuz80 *cpu = active_z80())
    cpu->port_write(port, value);
}
