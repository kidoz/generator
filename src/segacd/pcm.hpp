/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace generator {

struct SegaCdPcmChannelConfig {
  bool enabled = true;
  bool loop = false;
  uint32_t start = 0;
  uint32_t end = 0;
  uint32_t loop_start = 0;
  uint32_t pitch_16_16 = 0x10000;
  uint8_t volume_left = 255;
  uint8_t volume_right = 255;
};

class SegaCdPcm {
public:
  static constexpr size_t kWaveRamSize = 0x10000;
  static constexpr size_t kChannels = 8;
  static constexpr uint64_t kSampleRate = 32000;

  explicit SegaCdPcm(uint64_t master_frequency);

  void reset();
  std::expected<void, std::string>
  configure(size_t channel, const SegaCdPcmChannelConfig &config);
  void advance_mclk(uint64_t ticks);
  int32_t sample_left() const
  {
    return m_left;
  }
  int32_t sample_right() const
  {
    return m_right;
  }
  std::span<uint8_t, kWaveRamSize> wave_ram()
  {
    return m_wave_ram;
  }
  bool channel_enabled(size_t channel) const;

  struct SavedState {
    std::array<uint8_t, kWaveRamSize> wave_ram{};
    std::array<SegaCdPcmChannelConfig, kChannels> configs{};
    std::array<uint64_t, kChannels> positions{};
    std::array<int32_t, kChannels> left{};
    std::array<int32_t, kChannels> right{};
    uint64_t clock_acc = 0;
    int32_t mix_left = 0;
    int32_t mix_right = 0;
  };
  SavedState save() const;
  bool restore(const SavedState &state);
  bool valid_saved_state(const SavedState &state) const;

private:
  struct Channel {
    SegaCdPcmChannelConfig config{};
    uint64_t position = 0;
    int32_t left = 0;
    int32_t right = 0;
  };

  uint64_t m_master_frequency;
  std::array<uint8_t, kWaveRamSize> m_wave_ram{};
  std::array<Channel, kChannels> m_channels{};
  uint64_t m_clock_acc = 0;
  int32_t m_left = 0;
  int32_t m_right = 0;
};

}  // namespace generator
