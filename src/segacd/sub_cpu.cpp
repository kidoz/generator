/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sub_cpu.hpp"

namespace generator {

SegaCdSubCpu::SegaCdSubCpu(uint64_t master_frequency)
    : m_master_frequency(master_frequency == 0 ? kDefaultMasterFrequency
                                               : master_frequency),
      m_cpu(m_bus, *this)
{
}

void SegaCdSubCpu::power_on_reset()
{
  m_running = false;
  m_master_ticks = 0;
  m_clock_acc = 0;
  m_clock_credit = 0;
  m_bus.reset();
  m_cpu.power_on_reset();
  m_clock_credit = 0;
  m_running = true;
}

void SegaCdSubCpu::set_running(bool running)
{
  m_running = running;
  if (!m_running) {
    m_clock_credit = 0;
  }
}

void SegaCdSubCpu::advance_mclk(uint64_t ticks)
{
  m_master_ticks += ticks;
  m_clock_acc += ticks * kFrequency;
  m_clock_credit += (int64_t)(m_clock_acc / m_master_frequency);
  m_clock_acc %= m_master_frequency;

  if (!m_running) {
    m_clock_credit = 0;
    return;
  }
  while (m_clock_credit > 0) {
    if (m_cpu.halted() || m_cpu.stopped()) {
      m_clock_credit = 0;
      break;
    }
    const uint64_t before = m_cpu.clk();
    m_cpu.step();
    if (m_cpu.clk() == before) {
      m_clock_credit = 0;
      break;
    }
  }
}

void SegaCdSubCpu::advance_68k_clocks(uint64_t clocks)
{
  m_clock_credit -= (int64_t)clocks;
}

SegaCdSubCpu::SavedState SegaCdSubCpu::save() const
{
  return SavedState{m_bus.save(),   m_cpu.save(), m_running,
                    m_master_ticks, m_clock_acc,  m_clock_credit};
}

bool SegaCdSubCpu::valid_saved_state(const SavedState &state) const
{
  return SegaCdSubBus::valid_saved_state(state.bus) &&
         state.cpu.queue_len >= 0 && state.cpu.queue_len <= 2 &&
         state.cpu.fault_kind <= (uint8_t)M68k::Fault::Kind::Stopped &&
         state.clock_acc < m_master_frequency &&
         state.clock_credit > -10000000 && state.clock_credit < 10000000;
}

bool SegaCdSubCpu::restore(const SavedState &state)
{
  if (!valid_saved_state(state)) {
    return false;
  }
  (void)m_bus.restore(state.bus);
  m_cpu.restore(state.cpu);
  m_running = state.running;
  m_master_ticks = state.master_ticks;
  m_clock_acc = state.clock_acc;
  m_clock_credit = state.clock_credit;
  return true;
}

}  // namespace generator
