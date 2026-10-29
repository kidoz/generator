/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pcm.hpp"

#include "sub_cpu.hpp"

#include <algorithm>

namespace generator {

SegaCdPcm::SegaCdPcm(uint64_t master_frequency)
    : m_master_frequency(master_frequency == 0
                             ? SegaCdSubCpu::kDefaultMasterFrequency
                             : master_frequency)
{
}

void SegaCdPcm::reset()
{
  m_wave_ram.fill(0);
  for (Channel &channel : m_channels) {
    channel.config.enabled = false;
    channel.position = 0;
    channel.left = 0;
    channel.right = 0;
  }
  m_clock_acc = 0;
  m_left = 0;
  m_right = 0;
}

std::expected<void, std::string>
SegaCdPcm::configure(size_t index, const SegaCdPcmChannelConfig &config)
{
  if (index >= m_channels.size()) {
    return std::unexpected("PCM channel is out of range");
  }
  if (config.start >= config.end || config.end > kWaveRamSize ||
      config.loop_start >= config.end || config.pitch_16_16 == 0) {
    return std::unexpected("invalid PCM address or pitch");
  }
  Channel &channel = m_channels[index];
  channel.config = config;
  channel.position = (uint64_t)config.start << 16;
  channel.left = 0;
  channel.right = 0;
  return {};
}

bool SegaCdPcm::channel_enabled(size_t channel) const
{
  return channel < m_channels.size() && m_channels[channel].config.enabled;
}

void SegaCdPcm::advance_mclk(uint64_t ticks)
{
  m_clock_acc += ticks * kSampleRate;
  while (m_clock_acc >= m_master_frequency) {
    m_clock_acc -= m_master_frequency;
    m_left = 0;
    m_right = 0;
    for (Channel &channel : m_channels) {
      if (!channel.config.enabled) {
        channel.left = 0;
        channel.right = 0;
        continue;
      }
      const uint32_t address = channel.position >> 16;
      const int32_t sample = (int8_t)m_wave_ram[address];
      channel.left = sample * channel.config.volume_left / 255;
      channel.right = sample * channel.config.volume_right / 255;
      channel.position += channel.config.pitch_16_16;
      if ((channel.position >> 16) >= channel.config.end) {
        if (channel.config.loop) {
          channel.position = (uint64_t)channel.config.loop_start << 16;
        } else {
          channel.config.enabled = false;
        }
      }
      m_left += channel.left;
      m_right += channel.right;
    }
  }
}

SegaCdPcm::SavedState SegaCdPcm::save() const
{
  SavedState state;
  state.wave_ram = m_wave_ram;
  for (size_t i = 0; i < m_channels.size(); i++) {
    state.configs[i] = m_channels[i].config;
    state.positions[i] = m_channels[i].position;
    state.left[i] = m_channels[i].left;
    state.right[i] = m_channels[i].right;
  }
  state.clock_acc = m_clock_acc;
  state.mix_left = m_left;
  state.mix_right = m_right;
  return state;
}

bool SegaCdPcm::valid_saved_state(const SavedState &state) const
{
  if (state.clock_acc >= m_master_frequency) {
    return false;
  }
  for (size_t i = 0; i < m_channels.size(); i++) {
    const auto &config = state.configs[i];
    if (config.enabled &&
        (config.start >= config.end || config.end > kWaveRamSize ||
         config.loop_start >= config.end || config.pitch_16_16 == 0 ||
         (state.positions[i] >> 16) > config.end)) {
      return false;
    }
  }
  return true;
}

bool SegaCdPcm::restore(const SavedState &state)
{
  if (!valid_saved_state(state)) {
    return false;
  }
  m_wave_ram = state.wave_ram;
  for (size_t i = 0; i < m_channels.size(); i++) {
    m_channels[i].config = state.configs[i];
    m_channels[i].position = state.positions[i];
    m_channels[i].left = state.left[i];
    m_channels[i].right = state.right[i];
  }
  m_clock_acc = state.clock_acc;
  m_left = state.mix_left;
  m_right = state.mix_right;
  return true;
}

}  // namespace generator
