/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "x32_pwm.hpp"

#include <algorithm>
#include <limits>

namespace generator {

void X32Pwm::power_on()
{
  m_channels = {};
  m_pulse_latch.fill(0);
  m_control = 0;
  m_cycle = 0;
  m_clock_accumulator = 0;
  m_cycle_remaining = 0;
  m_timer_counter = 0;
  m_timer_event = false;
}

uint32_t X32Pwm::period() const
{
  return std::max<uint32_t>(m_cycle, 1);
}

bool X32Pwm::enabled() const
{
  const uint32_t left = m_control & 3;
  const uint32_t right = (m_control >> 2) & 3;
  return (left == 1 || left == 2 || right == 1 || right == 2) &&
         (left != 3 || right != 3);
}

void X32Pwm::update_enabled()
{
  if (!enabled()) {
    m_clock_accumulator = 0;
    m_cycle_remaining = 0;
    m_timer_counter = 0;
    for (auto &channel : m_channels)
      channel.active = false;
    return;
  }
  if (m_cycle_remaining == 0) {
    m_clock_accumulator = 0;
    m_cycle_remaining = period();
    m_timer_counter = 0;
  }
}

uint64_t X32Pwm::mclk_until_update() const
{
  if (!enabled() || m_cycle_remaining == 0)
    return std::numeric_limits<uint64_t>::max();
  const uint64_t scaled = m_cycle_remaining * 7 - m_clock_accumulator;
  return std::max<uint64_t>((scaled + 2) / 3, 1);
}

void X32Pwm::advance_mclk(uint64_t ticks)
{
  if (!enabled())
    return;
  m_clock_accumulator += ticks * 3;
  while (m_clock_accumulator >= 7) {
    m_clock_accumulator -= 7;
    if (--m_cycle_remaining != 0)
      continue;
    m_cycle_remaining = period();
    for (auto &channel : m_channels)
      pop(channel);
    if (m_timer_counter == 0) {
      m_timer_event = true;
      m_timer_counter = (m_control >> 8) & 0xF;
    } else {
      m_timer_counter--;
    }
  }
}

uint16_t X32Pwm::read_register(uint32_t offset) const
{
  switch (offset & ~1u) {
  case 0:
    return m_control;
  case 2:
    return m_cycle;
  case 4:
  case 6:
  case 8: {
    const Channel &channel = m_channels[(offset - 4) & 1];
    return (channel.count == 3 ? 0x8000 : 0) |
           (channel.count == 0 ? 0x4000 : 0);
  }
  default:
    return 0;
  }
}

void X32Pwm::write_register(uint32_t offset, uint16_t value, bool md_side)
{
  switch (offset & ~1u) {
  case 0:
    m_control = md_side ? (m_control & 0xFFF0) | (value & 0xF) : value & 0xFFF;
    update_enabled();
    break;
  case 2:
    m_cycle = value;
    update_enabled();
    break;
  case 4:
  case 6:
    push((offset - 4) / 2, value & 0xFFF);
    break;
  case 8:
    push(0, value & 0xFFF);
    push(1, value & 0xFFF);
    break;
  default:
    break;
  }
}

uint8_t X32Pwm::read_register_byte(uint32_t offset) const
{
  const uint16_t value = read_register(offset);
  return (offset & 1) ? value : value >> 8;
}

void X32Pwm::write_register_byte(uint32_t offset, uint8_t value, bool md_side)
{
  const uint32_t aligned = offset & ~1u;
  if (aligned >= 4 && aligned <= 8) {
    const size_t first = aligned == 8 ? 0 : (aligned - 4) / 2;
    const size_t last = aligned == 8 ? 2 : first + 1;
    for (size_t channel = first; channel < last; channel++) {
      if (offset & 1)
        m_pulse_latch[channel] = (m_pulse_latch[channel] & 0xFF00) | value;
      else
        m_pulse_latch[channel] =
            (m_pulse_latch[channel] & 0x00FF) | (uint16_t)value << 8;
    }
    if (offset & 1) {
      for (size_t channel = first; channel < last; channel++)
        push(channel, m_pulse_latch[channel] & 0xFFF);
    }
    return;
  }
  uint16_t word = read_register(aligned);
  if (offset & 1)
    word = (word & 0xFF00) | value;
  else
    word = (word & 0x00FF) | (uint16_t)value << 8;
  write_register(aligned, word, md_side);
}

void X32Pwm::push(size_t channel, uint16_t value)
{
  Channel &target = m_channels[channel];
  if (target.count < 3) {
    target.fifo[target.count++] = value;
    return;
  }
  target.fifo[0] = target.fifo[1];
  target.fifo[1] = target.fifo[2];
  target.fifo[2] = value;
}

void X32Pwm::pop(Channel &channel)
{
  if (channel.count == 0)
    return;
  channel.current = channel.fifo[0];
  channel.fifo[0] = channel.fifo[1];
  channel.fifo[1] = channel.fifo[2];
  channel.count--;
  channel.active = true;
}

int32_t X32Pwm::sample(const Channel &channel) const
{
  if (!channel.active)
    return 0;
  const int64_t scaled =
      ((int64_t)channel.current * 2 - period()) * 8192 / period();
  return (int32_t)std::clamp<int64_t>(scaled, -8192, 8192);
}

int32_t X32Pwm::audio_left() const
{
  switch (m_control & 3) {
  case 1:
    return sample(m_channels[0]);
  case 2:
    return sample(m_channels[1]);
  default:
    return 0;
  }
}

int32_t X32Pwm::audio_right() const
{
  switch ((m_control >> 2) & 3) {
  case 1:
    return sample(m_channels[1]);
  case 2:
    return sample(m_channels[0]);
  default:
    return 0;
  }
}

bool X32Pwm::dreq() const
{
  return enabled() && (m_control & 0x80) &&
         (m_channels[0].count == 0 || m_channels[1].count == 0);
}

bool X32Pwm::take_timer_event()
{
  const bool event = m_timer_event;
  m_timer_event = false;
  return event;
}

}  // namespace generator
