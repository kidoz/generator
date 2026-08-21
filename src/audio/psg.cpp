/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "psg.hpp"

namespace generator {

namespace {
/* The PSG receives master/15 and its tone divider advances once per 16
 * input clocks. Omitting the internal divider raises every tone by four
 * octaves. */
constexpr int kMclkPerPsgTick = 15 * 16;

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
  m_latch_volume = false;
  m_lfsr = 0x8000; /* seed: bit 15 set */
  m_noise_mode = 0;
  m_noise_rate = 0;
  m_output_value = 0;
  m_mclk_acc = 0;
}

void Psg::write(uint8_t value)
{
  if ((value & 0x80) != 0) {
    /* Latch command: 1ccr dddd (channel, register, low nibble). */
    m_latch_channel = (value >> 5) & 3;
    m_latch_volume = (value & 0x10) != 0;
    const uint8_t data = value & 0x0F;
    if (m_latch_volume) {
      m_atten[m_latch_channel] = data;
    } else if (m_latch_channel == 3) {
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
      /* Tone latch supplies period bits 3..0. */
      m_period[m_latch_channel] = (m_period[m_latch_channel] & 0x3F0) | data;
    }
  } else {
    const uint8_t data = value & 0x3F;
    if (m_latch_volume) {
      m_atten[m_latch_channel] = data & 0x0F;
    } else if (m_latch_channel < 3) {
      /* Data byte supplies tone-period bits 9..4. */
      m_period[m_latch_channel] =
          (m_period[m_latch_channel] & 0x00F) | ((uint16_t)data << 4);
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
      /* 16-bit LFSR: Sega's white-noise taps are bits 0 and 3; periodic
       * noise feeds bit 0 back directly. */
      const uint16_t feedback =
          m_noise_mode ? (uint16_t)((m_lfsr & 1) ^ ((m_lfsr >> 3) & 1))
                       : (m_lfsr & 1);
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
  while (m_mclk_acc >= kMclkPerPsgTick) {
    step();
    m_mclk_acc -= kMclkPerPsgTick;
  }
}

int16_t Psg::output() const
{
  return m_output_value;
}

}  // namespace generator
