/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SN76489 PSG.
 *
 * Clock: master / 15 (same as the Z80). The chip hangs off the low data
 * byte inside the VDP's address window, so it is written as the odd byte
 * of the $10-$17 block from either side: $7F11 from the Z80, $C00011
 * from the 68K.
 * Latch/data protocol: a write with bit 7 = 0 latches the channel
 * + attenuation; bit 7 = 1 writes the 10-bit tone period (low 4 bits
 * for the follow-up, or 6 bits for the first tone write). */

#pragma once

#include <array>
#include <cstdint>

namespace generator {

class Psg {
public:
  void reset();

  /* Write a byte from the Z80 (or VDP port area). */
  void write(uint8_t value);

  /* Advance by master clocks; returns true if output changed. */
  void advance_mclk(uint64_t ticks);

  /* Current mixed output (-1.0..1.0 scaled). */
  int16_t output() const;

  /* --- state --- */
  uint8_t attenuation(int ch) const
  {
    return m_atten[ch & 3];
  }

private:
  void step();

  /* 4 channels: 3 tone + 1 noise */
  std::array<uint16_t, 4> m_counter{};
  std::array<uint16_t, 4> m_period{};
  std::array<uint8_t, 4> m_atten{}; /* 0-15 (0 = loudest) */
  std::array<uint8_t, 4> m_output_bit{};

  /* latch state */
  uint8_t m_latch_channel = 0;
  bool m_latch_tone_pending = false; /* waiting for period low nibble */

  /* noise */
  uint16_t m_lfsr = 0;
  uint8_t m_noise_mode = 0; /* 0 = white, 1 = periodic */
  uint8_t m_noise_rate = 0; /* 0-3 */

  /* output */
  int16_t m_output_value = 0;
  uint64_t m_mclk_acc = 0;
};

}  // namespace generator
