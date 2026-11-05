/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstdint>

namespace generator {

class Sh2Frt {
public:
  struct SavedState {
    uint16_t frc = 0;
    uint16_t ocra = 0xFFFF;
    uint16_t ocrb = 0xFFFF;
    uint16_t icr = 0;
    uint8_t tier = 1;
    uint8_t ftcsr = 0;
    uint8_t tcr = 0;
    uint8_t tocr = 0xE0;
    uint8_t temp = 0;
    uint8_t read_flags = 0;
    uint32_t clock_accumulator = 0;
  };

  void reset();
  uint8_t read(uint32_t address);
  void write(uint32_t address, uint8_t value);
  void advance(uint32_t cycles);

  bool compare_interrupt_pending() const;
  bool overflow_interrupt_pending() const;
  SavedState save() const
  {
    return {m_frc,
            m_ocra,
            m_ocrb,
            m_icr,
            m_tier,
            m_ftcsr,
            m_tcr,
            m_tocr,
            m_temp,
            m_read_flags,
            m_clock_accumulator};
  }
  void restore(const SavedState &state)
  {
    m_frc = state.frc;
    m_ocra = state.ocra;
    m_ocrb = state.ocrb;
    m_icr = state.icr;
    m_tier = state.tier;
    m_ftcsr = state.ftcsr;
    m_tcr = state.tcr;
    m_tocr = state.tocr;
    m_temp = state.temp;
    m_read_flags = state.read_flags;
    m_clock_accumulator = state.clock_accumulator;
  }

private:
  void tick();

  uint16_t m_frc = 0;
  uint16_t m_ocra = 0xFFFF;
  uint16_t m_ocrb = 0xFFFF;
  uint16_t m_icr = 0;
  uint8_t m_tier = 1;
  uint8_t m_ftcsr = 0;
  uint8_t m_tcr = 0;
  uint8_t m_tocr = 0xE0;
  uint8_t m_temp = 0;
  uint8_t m_read_flags = 0;
  uint32_t m_clock_accumulator = 0;
};

}  // namespace generator
