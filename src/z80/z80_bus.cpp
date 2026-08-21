/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "z80_bus.hpp"

#include "bus/m68k_bus.hpp"

namespace generator {

void Z80Bus::reset()
{
  m_zram.fill(0);
  m_ym.reset();
  m_psg.reset();
  m_bank = 0;
}

uint8_t Z80Bus::read_byte(uint16_t zaddr)
{
  if (zaddr < 0x4000) {
    return m_zram[zaddr & 0x1FFF];
  }
  if (zaddr < 0x6000) {
    /* YM3438: $4000/$4002 = address (status read), $4001/$4003 = data */
    if ((zaddr & 3) == 0 || (zaddr & 3) == 2) {
      return m_ym.read_status();
    }
    return m_ym.read_data();
  }
  if (zaddr >= 0x8000) {
    const uint32_t mapped = m_bank | (zaddr & 0x7FFF);
    uint16_t word = 0;
    if (m_m68kbus != nullptr) {
      /* byte access: select the lane of the aligned 68K word */
      const bool upper = (mapped & 1) == 0;
      m_m68kbus->read(mapped & ~1u, upper, !upper, &word);
      return upper ? (uint8_t)(word >> 8) : (uint8_t)word;
    }
    return 0x00;
  }
  /* bank latch and PSG area read as float */
  return 0x00;
}

void Z80Bus::write_byte(uint16_t zaddr, uint8_t value)
{
  if (zaddr < 0x4000) {
    m_zram[zaddr & 0x1FFF] = value;
    return;
  }
  if (zaddr < 0x6000) {
    const uint8_t bank = (uint8_t)((zaddr >> 1) & 1);
    if ((zaddr & 1) == 0) {
      m_ym.write_address(value, bank);
    } else {
      m_ym.write_data(value, bank);
    }
    return;
  }
  if (zaddr >= 0x8000) {
    const uint32_t mapped = m_bank | (zaddr & 0x7FFF);
    if (m_m68kbus != nullptr) {
      /* drive the selected byte lane of the 68K word */
      const uint16_t word = (uint16_t)((uint16_t)value << 8 | value);
      m_m68kbus->write(mapped, word, (zaddr & 1) == 0, (zaddr & 1) != 0);
    }
    return;
  }
  if ((zaddr & 0xFF00) == 0x6000) {
    /* bank latch: bit-serial, LSB first into bits 23..15. The decode is
     * only $6000-$60FF — the rest of $6100-$7EFF is unmapped, and letting
     * it fall through here shifts stray bits into the bank register. */
    m_bank = ((m_bank >> 1) | ((uint32_t)(value & 1) << 23)) & 0xFF8000;
    return;
  }
  if ((zaddr & 0xFF00) == 0x7F00) {
    /* VDP window ($7F00-$7FFF maps to $C00000-$C000FF, mirrored every
     * 0x20). The PSG is the odd byte of the $10-$17 block; the rest of
     * the window is not routed from this side. */
    const uint32_t port = (zaddr & 0x1C) >> 2;
    if ((port == 4 || port == 5) && (zaddr & 1) != 0) {
      m_psg.write(value);
    }
    return;
  }
}

uint16_t Z80Bus::read_word_68k(uint32_t zaddr)
{
  const uint16_t hi = read_byte((uint16_t)(zaddr & 0xFFFF));
  const uint16_t lo = read_byte((uint16_t)((zaddr + 1) & 0xFFFF));
  return (uint16_t)((uint16_t)hi << 8 | lo);
}

void Z80Bus::write_word_68k(uint32_t zaddr, uint16_t data, bool upper,
                            bool lower)
{
  const uint16_t addr = (uint16_t)(zaddr & 0xFFFF);
  /* upper strobe drives the even byte lane, lower the odd one */
  if (upper) {
    write_byte(addr, (uint8_t)(data >> 8));
  }
  if (lower) {
    write_byte((uint16_t)(addr + 1), (uint8_t)data);
  }
}

}  // namespace generator
