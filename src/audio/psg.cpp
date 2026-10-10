/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "psg.hpp"

#include <algorithm>

namespace generator {

namespace {
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
  m_lfsr = kNoiseSeed;
  m_noise_mode = 0;
  m_noise_rate = 0;
  m_output_value = 0;
  m_mclk_acc = 0;
}

Psg::SavedState Psg::save() const
{
  SavedState state{};
  std::copy_n(m_counter.data(), 4, state.counter);
  std::copy_n(m_period.data(), 4, state.period);
  std::copy_n(m_atten.data(), 4, state.atten);
  std::copy_n(m_output_bit.data(), 4, state.output_bit);
  state.latch_channel = m_latch_channel;
  state.latch_volume = m_latch_volume ? 1 : 0;
  state.lfsr = m_lfsr;
  state.noise_mode = m_noise_mode;
  state.noise_rate = m_noise_rate;
  state.output_value = m_output_value;
  state.mclk_acc = m_mclk_acc;
  return state;
}

void Psg::restore(const SavedState &state)
{
  std::copy_n(state.counter, 4, m_counter.data());
  std::copy_n(state.period, 4, m_period.data());
  std::copy_n(state.atten, 4, m_atten.data());
  for (auto &a : m_atten) {
    a &= 0x0F; /* volume table index */
  }
  std::copy_n(state.output_bit, 4, m_output_bit.data());
  for (auto &b : m_output_bit) {
    b &= 1;
  }
  m_latch_channel = state.latch_channel & 3;
  m_latch_volume = state.latch_volume != 0;
  m_lfsr = state.lfsr;
  m_noise_mode = state.noise_mode & 1;
  m_noise_rate = state.noise_rate & 3;
  m_output_value = state.output_value;
  m_mclk_acc = state.mclk_acc % kMclkPerPsgTick;
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
      /* A write to the noise control register restarts the LFSR from its
       * seed (MAME sn76496.cpp: any register-6 write resets m_RNG for the
       * non-NCR parts), so repeated hits are deterministic. */
      m_lfsr = kNoiseSeed;
      m_period[3] = noise_period();
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

uint16_t Psg::noise_period() const
{
  /* The noise register selects N/512, N/1024 or N/2048 of the PSG clock, or
   * channel 2's tone as the clock. The chip toggles a flip-flop at that rate
   * and shifts the LFSR on alternate toggles, so the counter reload is twice
   * the programmed rate — shifting on every expiry (which this used to do)
   * ran noise percussion an octave high. MAME models the doubling directly
   * (sn76496.cpp: `1 << (5 + rate)`, rate 3 = `m_period[2] << 1`). */
  if (m_noise_rate == 3) {
    return (uint16_t)(m_period[2] << 1);
  }
  return (uint16_t)(0x20 << m_noise_rate);
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

  /* Noise channel: LFSR shifts on its period (see noise_period) */
  m_period[3] = noise_period();
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

  /* Mix output: each tone/noise channel drives a square wave that swings
   * ±half its attenuation level around zero, the way the chip's output
   * leaves the die (a silent channel, level 0, contributes nothing).
   * Emitting 0..level instead put a DC pedestal of half the instantaneous
   * amplitude into the mix — +907 counts measured in Comix Zone, with
   * 1.8:1 positive/negative peak asymmetry and no DC block downstream. */
  int32_t mix = 0;
  for (int ch = 0; ch < 4; ch++) {
    const int32_t v = kVolumeTable[m_atten[ch]];
    mix += m_output_bit[ch] ? v : -v;
  }
  m_output_value = (int16_t)(mix >> 3); /* avoid clipping */
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
