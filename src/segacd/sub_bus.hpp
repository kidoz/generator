/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "m68k/bus_interface.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace generator {

class SegaCdSubBus final : public M68kBusInterface {
public:
  static constexpr size_t kProgramRamSize = 0x80000;

  int read(uint32_t addr, bool upper, bool lower, uint16_t *out) override;
  int write(uint32_t addr, uint16_t data, bool upper, bool lower) override;
  void interrupt_ack(int level) override;

  void reset();
  void attach_expansion(M68kBusInterface *expansion)
  {
    m_expansion = expansion;
  }
  std::span<uint8_t, kProgramRamSize> program_ram()
  {
    return m_program_ram;
  }
  std::span<const uint8_t, kProgramRamSize> program_ram() const
  {
    return m_program_ram;
  }
  uint16_t open_bus_value() const
  {
    return m_data_bus;
  }
  void set_program_ram_wait_states(int clocks)
  {
    m_program_ram_wait_states = clocks;
  }
  unsigned int interrupt_ack_count() const
  {
    return m_interrupt_acks;
  }
  int acknowledged_level() const
  {
    return m_acknowledged_level;
  }

  struct SavedState {
    std::array<uint8_t, kProgramRamSize> ram{};
    uint16_t data_bus = 0;
    int wait_states = 0;
    unsigned int interrupt_acks = 0;
    int acknowledged_level = 0;
  };
  SavedState save() const;
  bool restore(const SavedState &state);
  static bool valid_saved_state(const SavedState &state);

private:
  std::array<uint8_t, kProgramRamSize> m_program_ram{};
  M68kBusInterface *m_expansion = nullptr;
  uint16_t m_data_bus = 0;
  int m_program_ram_wait_states = 0;
  unsigned int m_interrupt_acks = 0;
  int m_acknowledged_level = 0;
};

}  // namespace generator
