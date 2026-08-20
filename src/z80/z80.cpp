/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "z80.hpp"

namespace generator {

namespace {
constexpr int kMclkPerTstate = 15; /* Z80 clock = master / 15 */
}

Z80Chip::Z80Chip(Z80Bus &bus) : m_bus(bus), m_core(*this)
{
  m_core.reset();
}

void Z80Chip::power_on_reset()
{
  m_core.reset();
  m_mclk_acc = 0;
  m_tstates = 0;
  m_reset_asserted = true;
}

void Z80Chip::reset_line(bool asserted)
{
  if (asserted && !m_reset_asserted) {
    m_core.reset();
    m_mclk_acc = 0;
  }
  m_reset_asserted = asserted;
}

void Z80Chip::set_int(bool active)
{
  m_core.set_int_line(active);
}

void Z80Chip::advance_mclk(uint64_t ticks, bool bus_granted)
{
  if (m_reset_asserted) {
    return; /* held in reset: time passes, CPU frozen */
  }
  if (bus_granted) {
    /* 68K owns the bus: the chip is halted. Preserve any negative
     * accumulator (execution overshoot from the last step) so the Z80
     * doesn't get free time when the bus is released — the overshoot
     * means instructions already ran ahead of the clock. */
    return;
  }

  m_mclk_acc += (int64_t)ticks;
  while (m_mclk_acc >= kMclkPerTstate) {
    const int used = m_core.step();
    m_tstates += (uint64_t)used;
    /* an instruction may overshoot the budget; the negative accumulator
       repays the debt from subsequent ticks */
    m_mclk_acc -= (int64_t)used * kMclkPerTstate;
  }
}

uint8_t Z80Chip::read_memory(uint16_t address)
{
  return m_bus.read_byte(address);
}

void Z80Chip::write_memory(uint16_t address, uint8_t value)
{
  m_bus.write_byte(address, value);
}

}  // namespace generator
