/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Z80 address space: 8K RAM, YM3438, PSG,
 * write stub, bit-serial bank latch and the 68K bank window — shared by
 * the Z80 core and the 68K-side window at 0xA00000-0xA0FFFF.
 *
 * Decode (Z80 view, documented MD map):
 *   0x0000-0x3FFF  RAM (8K, mirrored)
 *   0x4000-0x5FFF  YM2612 (0x4000 addr / 0x4001 data, 2-byte mirror;
 *                  port 1 at 0x4002/3)
 *   0x6000-0x7FFF  bank latch (0x6000-0x6FFF) and PSG (0x7F10-0x7F1F)
 *   0x8000-0xFFFF  68K bank window through the serial latch */

#pragma once

#include "audio/psg.hpp"
#include "audio/ym3438.hpp"

#include <array>
#include <cstdint>

namespace generator {

class M68kBus;

class Z80Bus {
public:
  Z80Bus() = default;

  void attach_68k(M68kBus *bus)
  {
    m_m68kbus = bus;
  }

  void reset();

  /* Byte access from the Z80 core (or the 68K window). */
  uint8_t read_byte(uint16_t zaddr);
  void write_byte(uint16_t zaddr, uint8_t value);

  /* 68K-side word access (strobes applied by the caller's bus engine). */
  uint16_t read_word_68k(uint32_t zaddr);
  void write_word_68k(uint32_t zaddr, uint16_t data, bool upper, bool lower);

  /* --- state (savestate v3) --- */
  const std::array<uint8_t, 0x2000> &ram() const
  {
    return m_zram;
  }
  std::array<uint8_t, 0x2000> &ram()
  {
    return m_zram;
  }
  uint32_t bank() const
  {
    return m_bank;
  }
  void set_bank(uint32_t bank)
  {
    m_bank = bank;
  }

  /* The FM chip (status/timers/IRQ this phase; operators next). */
  const Ym3438 &ym() const
  {
    return m_ym;
  }
  Ym3438 &ym()
  {
    return m_ym;
  }
  const Psg &psg() const
  {
    return m_psg;
  }
  Psg &psg()
  {
    return m_psg;
  }

private:
  std::array<uint8_t, 0x2000> m_zram{};
  Ym3438 m_ym;
  Psg m_psg;
  uint32_t m_bank = 0;
  M68kBus *m_m68kbus = nullptr;
};

}  // namespace generator
