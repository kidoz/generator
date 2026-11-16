/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sh2_bsc.hpp"

#include <array>

namespace generator {

void Sh2Bsc::reset()
{
  m_bcr1 = 0x03F0;
  m_bcr2 = 0x00FC;
  m_wcr = 0xAAFF;
  m_mcr = 0;
  m_rtcsr = 0;
  m_rtcnt = 0;
  m_rtcor = 0;
  m_read_cmf = 0;
  m_clock_accumulator = 0;
  m_refresh_pending = false;
}

uint16_t Sh2Bsc::read16(uint32_t address)
{
  switch (address) {
  case 0xFFFFFFE2:
    return m_bcr1;
  case 0xFFFFFFE6:
    return m_bcr2;
  case 0xFFFFFFEA:
    return m_wcr;
  case 0xFFFFFFEE:
    return m_mcr;
  case 0xFFFFFFF2:
    m_read_cmf = m_rtcsr >> 7;
    return m_rtcsr;
  case 0xFFFFFFF6:
    return m_rtcnt;
  case 0xFFFFFFFA:
    return m_rtcor;
  default:
    return 0;
  }
}

uint32_t Sh2Bsc::read32(uint32_t address)
{
  if (address < 0xFFFFFFE0 || address > 0xFFFFFFF8 || (address & 3) != 0)
    return 0;
  return read16(address + 2);
}

void Sh2Bsc::write16(uint32_t address, uint16_t value)
{
  switch (address) {
  case 0xFFFFFFE2:
    m_bcr1 = (m_bcr1 & 0x8000) | (value & 0x1FF7);
    break;
  case 0xFFFFFFE6:
    m_bcr2 = value & 0x00FC;
    break;
  case 0xFFFFFFEA:
    m_wcr = value;
    break;
  case 0xFFFFFFEE:
    m_mcr = value & 0xFEFC;
    break;
  case 0xFFFFFFF2:
    if (m_read_cmf && !(value & 0x80))
      m_rtcsr &= ~0x80;
    m_read_cmf = 0;
    m_rtcsr = (m_rtcsr & 0x80) | (value & 0x78);
    break;
  case 0xFFFFFFF6:
    m_rtcnt = value;
    break;
  case 0xFFFFFFFA:
    m_rtcor = value;
    break;
  default:
    break;
  }
}

void Sh2Bsc::write32(uint32_t address, uint32_t value)
{
  if ((value >> 16) == 0xA55A)
    write16(address + 2, value);
}

void Sh2Bsc::compare_match()
{
  m_rtcnt = 0;
  m_rtcsr |= 0x80;
  if ((m_mcr & 0x0C) == 0x08)
    m_refresh_pending = true;
}

void Sh2Bsc::tick()
{
  if (m_rtcnt == m_rtcor) {
    compare_match();
    return;
  }
  m_rtcnt++;
  if (m_rtcnt == m_rtcor)
    compare_match();
}

void Sh2Bsc::advance(uint32_t cycles)
{
  static constexpr std::array<uint16_t, 8> dividers{0,   4,    16,   64,
                                                    256, 1024, 2048, 4096};
  const uint16_t divider = dividers[(m_rtcsr >> 3) & 7];
  if (divider == 0) {
    m_clock_accumulator = 0;
    return;
  }
  m_clock_accumulator += cycles;
  while (m_clock_accumulator >= divider) {
    m_clock_accumulator -= divider;
    tick();
  }
}

bool Sh2Bsc::take_refresh_request()
{
  const bool pending = m_refresh_pending;
  m_refresh_pending = false;
  return pending;
}

}  // namespace generator
