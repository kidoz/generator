/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sh2_intc.hpp"

namespace generator {

void Sh2Intc::reset()
{
  m_ipra = 0;
  m_iprb = 0;
  m_vcra = 0;
  m_vcrb = 0;
  m_vcrc = 0;
  m_vcrd = 0;
  m_vcrwdt = 0;
  m_icr = 0x8000;
  m_nmi_level = true;
  m_irl_level = 0;
  m_irl_vector = 0;
}

uint16_t Sh2Intc::read16(uint32_t address) const
{
  switch (address) {
  case 0xFFFFFEE2:
    return m_ipra;
  case 0xFFFFFE60:
    return m_iprb;
  case 0xFFFFFE62:
    return m_vcra;
  case 0xFFFFFE64:
    return m_vcrb;
  case 0xFFFFFE66:
    return m_vcrc;
  case 0xFFFFFE68:
    return m_vcrd;
  case 0xFFFFFEE4:
    return m_vcrwdt;
  case 0xFFFFFEE0:
    return m_icr;
  default:
    return 0;
  }
}

void Sh2Intc::write16(uint32_t address, uint16_t value)
{
  switch (address) {
  case 0xFFFFFEE2:
    m_ipra = value & 0xFFF0;
    break;
  case 0xFFFFFE60:
    m_iprb = value & 0xFF00;
    break;
  case 0xFFFFFE62:
    m_vcra = value & 0x7F7F;
    break;
  case 0xFFFFFE64:
    m_vcrb = value & 0x7F7F;
    break;
  case 0xFFFFFE66:
    m_vcrc = value & 0x7F7F;
    break;
  case 0xFFFFFE68:
    m_vcrd = value & 0x7F00;
    break;
  case 0xFFFFFEE4:
    m_vcrwdt = value & 0x7F7F;
    break;
  case 0xFFFFFEE0:
    m_icr = (m_icr & 0x8000) | (value & 0x101);
    break;
  default:
    break;
  }
}

bool Sh2Intc::set_nmi_level(bool asserted)
{
  const bool selected = asserted == ((m_icr & 0x100) != 0);
  const bool edge = asserted != m_nmi_level && selected;
  m_nmi_level = asserted;
  m_icr = (m_icr & 0x7FFF) | (asserted ? 0x8000 : 0);
  return edge;
}

void Sh2Intc::set_irl_level(uint8_t level, uint8_t vector)
{
  m_irl_level = level <= 15 ? level : 0;
  m_irl_vector = vector & 0x7F;
}

}  // namespace generator
