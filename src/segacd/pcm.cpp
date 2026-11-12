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
  m_registers = {};
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
  if (channel >= m_channels.size())
    return false;
  return m_registers.active ? (m_registers.control & 0x80) &&
                                  !(m_registers.disabled & (1 << channel))
                            : m_channels[channel].config.enabled;
}

void SegaCdPcm::advance_mclk(uint64_t ticks)
{
  const uint64_t period = m_master_frequency * (m_registers.active ? 1536 : 1);
  m_clock_acc += ticks * (m_registers.active ? 50000000 : kSampleRate);
  while (m_clock_acc >= period) {
    m_clock_acc -= period;
    if (m_registers.active) {
      m_left = m_right = 0;
      if (!(m_registers.control & 0x80))
        continue;
      for (size_t i = 0; i < kChannels; i++) {
        if (m_registers.disabled & (1 << i))
          continue;
        const auto &r = m_registers.channels[i];
        auto &position = m_registers.positions[i];
        uint8_t data = m_wave_ram[(position >> 11) & 0xffff];
        if (data == 0xff) {
          position = (uint32_t)((uint16_t)r[4] | (uint16_t)r[5] << 8) << 11;
          data = m_wave_ram[(position >> 11) & 0xffff];
          if (data == 0xff)
            continue;
        }
        const int32_t sample = (data & 0x80) ? data & 0x7f : -(data & 0x7f);
        m_left += sample * r[0] * (r[1] & 15) / 32;
        m_right += sample * r[0] * (r[1] >> 4) / 32;
        position =
            (position + ((uint16_t)r[2] | (uint16_t)r[3] << 8)) & 0x7ffffff;
      }
      continue;
    }
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

uint8_t SegaCdPcm::read_bus(uint32_t offset) const
{
  if (offset >= 0x2000 && offset < 0x4000)
    return m_wave_ram[(uint32_t)m_registers.bank * 4096 +
                      ((offset & 0x1fff) >> 1)];
  if (offset >= 0x20 && offset < 0x40) {
    const unsigned channel = (offset - 0x20) / 4;
    return (uint8_t)(m_registers.positions[channel] >>
                     ((offset & 2) ? 19 : 11));
  }
  return 0xff;
}

void SegaCdPcm::write_bus(uint32_t offset, uint8_t data)
{
  auto &r = m_registers;
  if (!r.active) {
    r.active = true;
    m_clock_acc = 0;
  }
  if (offset >= 0x2000 && offset < 0x4000) {
    m_wave_ram[(uint32_t)r.bank * 4096 + ((offset & 0x1fff) >> 1)] = data;
    return;
  }
  const unsigned index = offset / 2;
  if (index < 7) {
    r.channels[r.channel][index] = data;
    if (index == 6 && (r.disabled & (1 << r.channel)))
      r.positions[r.channel] = (uint32_t)data << 19;
  } else if (index == 7) {
    r.control = data;
    if (data & 0x40)
      r.channel = data & 7;
    else
      r.bank = data & 15;
  } else if (index == 8) {
    r.disabled = data;
    for (size_t i = 0; i < kChannels; i++)
      if (data & (1 << i))
        r.positions[i] = (uint32_t)r.channels[i][6] << 19;
  }
}

SegaCdPcm::SavedState SegaCdPcm::save() const
{
  SavedState state;
  state.registers = m_registers;
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
  if (state.registers.bank > 15 || state.registers.channel >= kChannels)
    return false;
  for (const auto position : state.registers.positions)
    if (position > 0x7ffffff)
      return false;
  if (state.clock_acc >=
      m_master_frequency * (state.registers.active ? 1536 : 1)) {
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
  m_registers = state.registers;
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
