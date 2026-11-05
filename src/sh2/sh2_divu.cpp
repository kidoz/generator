/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sh2_divu.hpp"

#include <bit>

namespace generator {

void Sh2Divu::reset()
{
  m_dvsr = 0;
  m_dvdnth = 0;
  m_dvdntl = 0;
  m_dvcr = 0;
  m_vcrdiv = 0;
  m_pending_remainder = 0;
  m_pending_quotient = 0;
  m_remaining_cycles = 0;
  m_pending_overflow = false;
  m_overflow_positive = false;
}

uint16_t Sh2Divu::read16(uint32_t address) const
{
  if (address == 0xFFFFFF08)
    return m_dvcr;
  if (address == 0xFFFFFF0C)
    return m_vcrdiv;
  return 0;
}

uint32_t Sh2Divu::read32(uint32_t address) const
{
  switch (address) {
  case 0xFFFFFF00:
    return m_dvsr;
  case 0xFFFFFF04:
  case 0xFFFFFF14:
    return m_dvdntl;
  case 0xFFFFFF08:
    return m_dvcr;
  case 0xFFFFFF0C:
    return m_vcrdiv;
  case 0xFFFFFF10:
    return m_dvdnth;
  default:
    return 0;
  }
}

void Sh2Divu::write16(uint32_t address, uint16_t value)
{
  if (address == 0xFFFFFF08)
    m_dvcr = value & 3;
  else if (address == 0xFFFFFF0C)
    m_vcrdiv = value & 0x7F;
}

void Sh2Divu::write32(uint32_t address, uint32_t value)
{
  switch (address) {
  case 0xFFFFFF00:
    m_dvsr = value;
    break;
  case 0xFFFFFF04:
    m_dvdnth = (int32_t)value < 0 ? 0xFFFFFFFF : 0;
    start((uint64_t)(int64_t)(int32_t)value, (int32_t)m_dvsr);
    break;
  case 0xFFFFFF08:
    m_dvcr = value & 3;
    break;
  case 0xFFFFFF0C:
    m_vcrdiv = value & 0x7F;
    break;
  case 0xFFFFFF10:
    m_dvdnth = value;
    break;
  case 0xFFFFFF14:
    start((uint64_t)m_dvdnth << 32 | value, (int32_t)m_dvsr);
    break;
  default:
    break;
  }
}

void Sh2Divu::start(uint64_t dividend_bits, int32_t divisor)
{
  const int64_t dividend = std::bit_cast<int64_t>(dividend_bits);
  int64_t quotient = 0;
  int64_t remainder = 0;
  m_pending_overflow = divisor == 0 || (dividend == INT64_MIN && divisor == -1);
  m_overflow_positive =
      divisor == 0 ? dividend >= 0 : (dividend < 0) == (divisor < 0);
  if (!m_pending_overflow) {
    quotient = dividend / divisor;
    remainder = dividend % divisor;
    m_pending_overflow = quotient > INT32_MAX || quotient < INT32_MIN;
    m_overflow_positive = quotient > 0;
  }
  if (m_pending_overflow) {
    m_pending_quotient = m_overflow_positive ? 0x7FFFFFFF : 0x80000000;
    m_pending_remainder = 0;
    m_remaining_cycles = 6;
  } else {
    m_pending_quotient = (uint32_t)quotient;
    m_pending_remainder = (uint32_t)remainder;
    m_remaining_cycles = 39;
  }
}

void Sh2Divu::finish()
{
  m_dvdntl = m_pending_quotient;
  m_dvdnth = m_pending_remainder;
  if (m_pending_overflow)
    m_dvcr |= 1;
}

void Sh2Divu::advance(uint32_t cycles)
{
  if (cycles < m_remaining_cycles) {
    m_remaining_cycles -= cycles;
    return;
  }
  if (m_remaining_cycles != 0) {
    m_remaining_cycles = 0;
    finish();
  }
}

}  // namespace generator
