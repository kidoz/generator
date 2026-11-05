/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace generator {

class X32Pwm {
public:
  struct SavedState {
    struct Channel {
      std::array<uint16_t, 3> fifo{};
      uint8_t count = 0;
      uint16_t current = 0;
      bool active = false;
    };
    std::array<Channel, 2> channels{};
    std::array<uint16_t, 2> pulse_latch{};
    uint16_t control = 0;
    uint16_t cycle = 0;
    uint64_t clock_accumulator = 0;
    uint64_t cycle_remaining = 0;
    uint8_t timer_counter = 0;
    bool timer_event = false;
  };

  void power_on();
  void advance_mclk(uint64_t ticks);
  uint64_t mclk_until_update() const;

  uint16_t read_register(uint32_t offset) const;
  void write_register(uint32_t offset, uint16_t value, bool md_side);
  uint8_t read_register_byte(uint32_t offset) const;
  void write_register_byte(uint32_t offset, uint8_t value, bool md_side);

  int32_t audio_left() const;
  int32_t audio_right() const;
  bool dreq() const;
  bool take_timer_event();
  SavedState save() const
  {
    SavedState state;
    for (size_t index = 0; index < 2; index++) {
      state.channels[index] = {m_channels[index].fifo, m_channels[index].count,
                               m_channels[index].current,
                               m_channels[index].active};
    }
    state.pulse_latch = m_pulse_latch;
    state.control = m_control;
    state.cycle = m_cycle;
    state.clock_accumulator = m_clock_accumulator;
    state.cycle_remaining = m_cycle_remaining;
    state.timer_counter = m_timer_counter;
    state.timer_event = m_timer_event;
    return state;
  }
  void restore(const SavedState &state)
  {
    for (size_t index = 0; index < 2; index++) {
      m_channels[index] = {
          state.channels[index].fifo, state.channels[index].count,
          state.channels[index].current, state.channels[index].active};
    }
    m_pulse_latch = state.pulse_latch;
    m_control = state.control;
    m_cycle = state.cycle;
    m_clock_accumulator = state.clock_accumulator;
    m_cycle_remaining = state.cycle_remaining;
    m_timer_counter = state.timer_counter;
    m_timer_event = state.timer_event;
  }
  static bool valid_saved_state(const SavedState &state)
  {
    return state.channels[0].count <= 3 && state.channels[1].count <= 3 &&
           state.control <= 0xFFF && state.clock_accumulator <= 6 &&
           state.cycle_remaining <= 0xFFFF && state.timer_counter <= 15;
  }

private:
  struct Channel {
    std::array<uint16_t, 3> fifo{};
    uint8_t count = 0;
    uint16_t current = 0;
    bool active = false;
  };

  uint32_t period() const;
  bool enabled() const;
  void update_enabled();
  void push(size_t channel, uint16_t value);
  void pop(Channel &channel);
  int32_t sample(const Channel &channel) const;

  std::array<Channel, 2> m_channels{};
  std::array<uint16_t, 2> m_pulse_latch{};
  uint16_t m_control = 0;
  uint16_t m_cycle = 0;
  uint64_t m_clock_accumulator = 0;
  uint64_t m_cycle_remaining = 0;
  uint8_t m_timer_counter = 0;
  bool m_timer_event = false;
};

}  // namespace generator
