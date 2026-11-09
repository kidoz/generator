/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstdint>

namespace generator {

class Sh2Intc {
public:
  struct SavedState {
    uint16_t ipra = 0;
    uint16_t iprb = 0;
    uint16_t vcra = 0;
    uint16_t vcrb = 0;
    uint16_t vcrc = 0;
    uint16_t vcrd = 0;
    uint16_t vcrwdt = 0;
    uint16_t icr = 0x8000;
    uint8_t irl_level = 0;
    uint8_t irl_vector = 0;
  };

  void reset();
  uint16_t read16(uint32_t address) const;
  void write16(uint32_t address, uint16_t value);
  bool set_nmi_level(bool asserted);
  void set_irl_level(uint8_t level, uint8_t vector);

  uint8_t divu_priority() const
  {
    return m_ipra >> 12;
  }
  uint8_t frt_priority() const
  {
    return (m_iprb >> 8) & 0xF;
  }
  uint8_t dmac_priority() const
  {
    return (m_ipra >> 8) & 0xF;
  }
  uint8_t frt_compare_vector() const
  {
    return m_vcrc & 0x7F;
  }
  uint8_t frt_overflow_vector() const
  {
    return (m_vcrd >> 8) & 0x7F;
  }
  uint8_t irl_level() const
  {
    return m_irl_level;
  }
  uint8_t irl_vector() const
  {
    if (m_icr & 1)
      return m_irl_vector;
    return 64 + (m_irl_level - 1) / 2;
  }
  SavedState save() const
  {
    return {m_ipra, m_iprb,   m_vcra, m_vcrb,      m_vcrc,
            m_vcrd, m_vcrwdt, m_icr,  m_irl_level, m_irl_vector};
  }
  void restore(const SavedState &state)
  {
    m_ipra = state.ipra;
    m_iprb = state.iprb;
    m_vcra = state.vcra;
    m_vcrb = state.vcrb;
    m_vcrc = state.vcrc;
    m_vcrd = state.vcrd;
    m_vcrwdt = state.vcrwdt;
    m_icr = state.icr;
    m_nmi_level = (state.icr & 0x8000) != 0;
    m_irl_level = state.irl_level;
    m_irl_vector = state.irl_vector;
  }

private:
  uint16_t m_ipra = 0;
  uint16_t m_iprb = 0;
  uint16_t m_vcra = 0;
  uint16_t m_vcrb = 0;
  uint16_t m_vcrc = 0;
  uint16_t m_vcrd = 0;
  uint16_t m_vcrwdt = 0;
  uint16_t m_icr = 0x8000;
  bool m_nmi_level = true;
  uint8_t m_irl_level = 0;
  uint8_t m_irl_vector = 0;
};

}  // namespace generator
