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
  default:
    break;
  }
}

}  // namespace generator
