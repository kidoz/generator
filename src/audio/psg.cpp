/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "psg.hpp"

namespace generator {

namespace {
constexpr int kMclkPerPsgClock = 15; /* PSG clock = master / 15 */

/* Volume table: attenuation 0-15, 2 dB per step, 0 = full */
constexpr int16_t kVolumeTable[16] = {
    32767, 26072, 20732, 16460, 13069, 10379, 8243, 6546,
    5197,  4130,  3281,  2605,  2069,  1643,  1304, 0,
};

}  // namespace

void Psg::reset()
{
  m_counter = {};
  m_period = {};

  m_atten.fill(15); /* all channels silent */
  m_output_bit = {};
  m_latch_channel = 0;
  m_latch_tone_pending = false;
  m_lfsr = 0x8000; /* seed: bit 15 set */
  m_noise_mode = 0;
  m_noise_rate = 0;
  m_output_value = 0;
  m_mclk_acc = 0;
}

void Psg::write(uint8_t value)
{
  if ((value & 0x80) == 0) {
    /* Latch command: 0lll dddd */
    m_latch_channel = (value >> 4) & 3;
    const uint8_t data = value & 0x0F;
    if (m_latch_channel == 3) {
      /* noise channel: mode and rate */
      m_noise_mode = (data >> 2) & 1;
      m_noise_rate = data & 3;
      /* rate 3 uses channel 2's output as the clock */
      if (m_noise_rate == 3) {
        m_period[3] = m_period[2];
      } else {
        m_period[3] = 0x10 << m_noise_rate;
      }
    } else {
      /* tone channel: first 4 bits of a 10-bit period, or attenuation */
      if ((value & 0x40) != 0) {
        /* attenuation write */
        m_atten[m_latch_channel] = data & 0x0F;
      } else {
        /* period high bits */
        m_period[m_latch_channel] =
            (m_period[m_latch_channel] & 0x3F) | ((uint16_t)(data & 3) << 8);
        m_latch_tone_pending = true;
      }
    }
  } else {
    /* Data command: continuation */
    const uint8_t data = value & 0x3F;
    if (m_latch_channel == 3) {
      /* noise: shouldn't happen but handle it */
      m_noise_mode = (data >> 2) & 1;
      m_noise_rate = data & 3;
    } else {
      /* period low 6 bits */
      m_period[m_latch_channel] = (m_period[m_latch_channel] & 0x300) | data;
      m_latch_tone_pending = false;
    }
  }
}

void Psg::step()
{
  /* Tone channels: decrement counter, toggle output on zero */
  for (int ch = 0; ch < 3; ch++) {
    if (m_period[ch] == 0) {
      m_output_bit[ch] = 0;
      continue;
    }
    if (++m_counter[ch] >= m_period[ch]) {
      m_counter[ch] = 0;
      m_output_bit[ch] ^= 1;
    }
  }

  /* Noise channel: LFSR shifts on its period */
  if (m_noise_rate == 3) {
    /* clocked by channel 2's output toggle */
    m_period[3] = m_period[2];
  } else {
    m_period[3] = 0x10 << m_noise_rate;
  }
  if (m_period[3] > 0) {
    if (++m_counter[3] >= m_period[3]) {
      m_counter[3] = 0;
      /* 16-bit LFSR: feedback = bit 0 XOR bit 1 (white) or bit 0 (periodic) */
      const uint16_t feedback =
          m_noise_mode ? (m_lfsr & 1)
                       : (uint16_t)((m_lfsr & 1) ^ ((m_lfsr >> 1) & 1));
      m_lfsr = (m_lfsr >> 1) | (feedback << 15);
      m_output_bit[3] = m_lfsr & 1;
    }
  }

  /* Mix output */
  int32_t mix = 0;
  for (int ch = 0; ch < 4; ch++) {
    if (m_output_bit[ch]) {
      mix += kVolumeTable[m_atten[ch]];
    }
  }
  m_output_value = (int16_t)(mix >> 2); /* avoid clipping */
}

void Psg::advance_mclk(uint64_t ticks)
{
  m_mclk_acc += (int64_t)ticks;
  while (m_mclk_acc >= kMclkPerPsgClock) {
    step();
    m_mclk_acc -= kMclkPerPsgClock;
  }
}

int16_t Psg::output() const
{
  return m_output_value;
}

}  // namespace generator
