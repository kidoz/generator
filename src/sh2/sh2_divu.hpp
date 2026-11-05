/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstdint>

namespace generator {

class Sh2Divu {
public:
  struct SavedState {
    uint32_t dvsr = 0;
    uint32_t dvdnth = 0;
    uint32_t dvdntl = 0;
    uint32_t dvcr = 0;
    uint32_t vcrdiv = 0;
    uint32_t pending_remainder = 0;
    uint32_t pending_quotient = 0;
    uint32_t remaining_cycles = 0;
    bool pending_overflow = false;
    bool overflow_positive = false;
  };

  void reset();
  uint16_t read16(uint32_t address) const;
  uint32_t read32(uint32_t address) const;
  void write16(uint32_t address, uint16_t value);
  void write32(uint32_t address, uint32_t value);
  void advance(uint32_t cycles);

  bool busy() const
  {
    return m_remaining_cycles != 0;
  }
  uint32_t remaining_cycles() const
  {
    return m_remaining_cycles;
  }
  bool overflow_interrupt_pending() const
  {
    return (m_dvcr & 3) == 3;
  }
  uint8_t vector() const
  {
    return m_vcrdiv & 0x7F;
  }
  SavedState save() const
  {
    return {m_dvsr,
            m_dvdnth,
            m_dvdntl,
            m_dvcr,
            m_vcrdiv,
            m_pending_remainder,
            m_pending_quotient,
            m_remaining_cycles,
            m_pending_overflow,
            m_overflow_positive};
  }
  void restore(const SavedState &state)
  {
    m_dvsr = state.dvsr;
    m_dvdnth = state.dvdnth;
    m_dvdntl = state.dvdntl;
    m_dvcr = state.dvcr;
    m_vcrdiv = state.vcrdiv;
    m_pending_remainder = state.pending_remainder;
    m_pending_quotient = state.pending_quotient;
    m_remaining_cycles = state.remaining_cycles;
    m_pending_overflow = state.pending_overflow;
    m_overflow_positive = state.overflow_positive;
  }

private:
  void start(uint64_t dividend_bits, int32_t divisor);
  void finish();

  uint32_t m_dvsr = 0;
  uint32_t m_dvdnth = 0;
  uint32_t m_dvdntl = 0;
  uint32_t m_dvcr = 0;
  uint32_t m_vcrdiv = 0;
  uint32_t m_pending_remainder = 0;
  uint32_t m_pending_quotient = 0;
  uint32_t m_remaining_cycles = 0;
  bool m_pending_overflow = false;
  bool m_overflow_positive = false;
};

}  // namespace generator
