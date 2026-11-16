/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstdint>

namespace generator {

class Sh2Bsc {
public:
  struct SavedState {
    uint16_t bcr1 = 0x03F0;
    uint16_t bcr2 = 0x00FC;
    uint16_t wcr = 0xAAFF;
    uint16_t mcr = 0;
    uint8_t rtcsr = 0;
    uint8_t rtcnt = 0;
    uint8_t rtcor = 0;
    uint8_t read_cmf = 0;
    uint16_t clock_accumulator = 0;
    bool refresh_pending = false;
  };

  void reset();
  uint16_t read16(uint32_t address);
  uint32_t read32(uint32_t address);
  void write16(uint32_t address, uint16_t value);
  void write32(uint32_t address, uint32_t value);
  void advance(uint32_t cycles);
  bool compare_interrupt_pending() const
  {
    return (m_rtcsr & 0xC0) == 0xC0;
  }
  bool refresh_pending() const
  {
    return m_refresh_pending;
  }
  bool take_refresh_request();
  SavedState save() const
  {
    return {m_bcr1,
            m_bcr2,
            m_wcr,
            m_mcr,
            m_rtcsr,
            m_rtcnt,
            m_rtcor,
            m_read_cmf,
            m_clock_accumulator,
            m_refresh_pending};
  }
  void restore(const SavedState &state)
  {
    m_bcr1 = state.bcr1;
    m_bcr2 = state.bcr2;
    m_wcr = state.wcr;
    m_mcr = state.mcr;
    m_rtcsr = state.rtcsr;
    m_rtcnt = state.rtcnt;
    m_rtcor = state.rtcor;
    m_read_cmf = state.read_cmf;
    m_clock_accumulator = state.clock_accumulator;
    m_refresh_pending = state.refresh_pending;
  }
  static bool valid_saved_state(const SavedState &state)
  {
    return (state.bcr1 & 0x6008) == 0 && (state.bcr2 & 0xFF03) == 0 &&
           (state.mcr & 0x0103) == 0 && (state.rtcsr & 7) == 0 &&
           state.read_cmf <= 1 && state.clock_accumulator < 4096;
  }

private:
  void tick();
  void compare_match();

  uint16_t m_bcr1 = 0x03F0;
  uint16_t m_bcr2 = 0x00FC;
  uint16_t m_wcr = 0xAAFF;
  uint16_t m_mcr = 0;
  uint8_t m_rtcsr = 0;
  uint8_t m_rtcnt = 0;
  uint8_t m_rtcor = 0;
  uint8_t m_read_cmf = 0;
  uint16_t m_clock_accumulator = 0;
  bool m_refresh_pending = false;
};

}  // namespace generator
