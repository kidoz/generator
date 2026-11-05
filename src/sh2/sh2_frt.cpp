/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sh2_frt.hpp"

namespace generator {

void Sh2Frt::reset()
{
  m_frc = 0;
  m_ocra = 0xFFFF;
  m_ocrb = 0xFFFF;
  m_icr = 0;
  m_tier = 1;
  m_ftcsr = 0;
  m_tcr = 0;
  m_tocr = 0xE0;
  m_temp = 0;
  m_read_flags = 0;
  m_clock_accumulator = 0;
}

uint8_t Sh2Frt::read(uint32_t address)
{
  switch (address) {
  case 0xFFFFFE10:
    return (m_tier & 0x8F) | 1;
  case 0xFFFFFE11:
    m_read_flags = m_ftcsr & 0x8E;
    return m_ftcsr;
  case 0xFFFFFE12:
    m_temp = m_frc & 0xFF;
    return m_frc >> 8;
  case 0xFFFFFE13:
    return m_temp;
  case 0xFFFFFE14:
    return ((m_tocr & 0x10) ? m_ocrb : m_ocra) >> 8;
  case 0xFFFFFE15:
    return (m_tocr & 0x10) ? m_ocrb : m_ocra;
  case 0xFFFFFE16:
    return m_tcr & 0x83;
  case 0xFFFFFE17:
    return (m_tocr & 0x13) | 0xE0;
  case 0xFFFFFE18:
    return m_icr >> 8;
  case 0xFFFFFE19:
    return m_icr;
  default:
    return 0;
  }
}

void Sh2Frt::write(uint32_t address, uint8_t value)
{
  switch (address) {
  case 0xFFFFFE10:
    m_tier = (value & 0x8E) | 1;
    break;
  case 0xFFFFFE11:
    m_ftcsr &= ~(m_read_flags & ~value & 0x8E);
    m_read_flags = 0;
    m_ftcsr = (m_ftcsr & 0xFE) | (value & 1);
    break;
  case 0xFFFFFE12:
    m_temp = value;
    break;
  case 0xFFFFFE13:
    m_frc = (uint16_t)m_temp << 8 | value;
    break;
  case 0xFFFFFE14:
    m_temp = value;
    break;
  case 0xFFFFFE15:
    if (m_tocr & 0x10)
      m_ocrb = (uint16_t)m_temp << 8 | value;
    else
      m_ocra = (uint16_t)m_temp << 8 | value;
    break;
  case 0xFFFFFE16:
    m_tcr = value & 0x83;
    m_clock_accumulator = 0;
    break;
  case 0xFFFFFE17:
    m_tocr = (value & 0x13) | 0xE0;
    break;
  default:
    break;
  }
}

void Sh2Frt::tick()
{
  if (m_frc == m_ocra)
    m_ftcsr |= 8;
  if (m_frc == m_ocrb)
    m_ftcsr |= 4;
  const bool overflow = m_frc == 0xFFFF;
  m_frc++;
  if (overflow)
    m_ftcsr |= 2;
  if (m_frc == m_ocra) {
    m_ftcsr |= 8;
    if (m_ftcsr & 1)
      m_frc = 0;
  }
  if (m_frc == m_ocrb)
    m_ftcsr |= 4;
}

void Sh2Frt::advance(uint32_t cycles)
{
  if ((m_tcr & 3) == 3) {
    m_clock_accumulator = 0;
    return;
  }
  const uint32_t divider = 8u << ((m_tcr & 3) * 2);
  m_clock_accumulator += cycles;
  while (m_clock_accumulator >= divider) {
    m_clock_accumulator -= divider;
    tick();
  }
}

bool Sh2Frt::compare_interrupt_pending() const
{
  return ((m_ftcsr & 8) && (m_tier & 8)) || ((m_ftcsr & 4) && (m_tier & 4));
}

bool Sh2Frt::overflow_interrupt_pending() const
{
  return (m_ftcsr & 2) && (m_tier & 2);
}

}  // namespace generator
