/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sub_bus.hpp"

namespace generator {

int SegaCdSubBus::read(uint32_t addr, bool upper, bool lower, uint16_t *out)
{
  addr &= 0xFFFFFF;
  if (m_expansion != nullptr && addr >= kProgramRamSize) {
    return m_expansion->read(addr, upper, lower, out);
  }
  if (addr < kProgramRamSize) {
    m_data_bus = (uint16_t)((uint16_t)m_program_ram[addr] << 8 |
                            m_program_ram[addr + 1]);
    *out = m_data_bus;
    return m_program_ram_wait_states;
  }
  *out = m_data_bus;
  return 0;
}

int SegaCdSubBus::write(uint32_t addr, uint16_t data, bool upper, bool lower)
{
  addr &= 0xFFFFFF;
  m_data_bus = data;
  if (m_expansion != nullptr && addr >= kProgramRamSize) {
    return m_expansion->write(addr, data, upper, lower);
  }
  if (addr >= kProgramRamSize) {
    return 0;
  }
  if (upper) {
    m_program_ram[addr] = (uint8_t)(data >> 8);
  }
  if (lower) {
    m_program_ram[addr + 1] = (uint8_t)data;
  }
  return m_program_ram_wait_states;
}

void SegaCdSubBus::interrupt_ack(int level)
{
  m_acknowledged_level = level;
  m_interrupt_acks++;
  if (m_expansion != nullptr) {
    m_expansion->interrupt_ack(level);
  }
}

void SegaCdSubBus::reset()
{
  m_data_bus = 0;
  m_interrupt_acks = 0;
  m_acknowledged_level = 0;
}

SegaCdSubBus::SavedState SegaCdSubBus::save() const
{
  return SavedState{m_program_ram, m_data_bus, m_program_ram_wait_states,
                    m_interrupt_acks, m_acknowledged_level};
}

bool SegaCdSubBus::valid_saved_state(const SavedState &state)
{
  return state.wait_states >= 0 && state.acknowledged_level >= 0 &&
         state.acknowledged_level <= 7;
}

bool SegaCdSubBus::restore(const SavedState &state)
{
  if (!valid_saved_state(state)) {
    return false;
  }
  m_program_ram = state.ram;
  m_data_bus = state.data_bus;
  m_program_ram_wait_states = state.wait_states;
  m_interrupt_acks = state.interrupt_acks;
  m_acknowledged_level = state.acknowledged_level;
  return true;
}

}  // namespace generator
