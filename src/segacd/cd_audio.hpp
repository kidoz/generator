/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "disc.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace generator {

class SegaCdAudio {
public:
  static constexpr size_t kFramesPerSector = 588;
  static constexpr uint64_t kSampleRate = 44100;

  explicit SegaCdAudio(const SegaCdDisc &disc, uint64_t master_frequency);

  void reset();
  std::expected<void, std::string> play(unsigned int track);
  void pause();
  void resume();
  void stop();
  void advance_mclk(uint64_t ticks);
  int32_t sample_left() const
  {
    return m_left;
  }
  int32_t sample_right() const
  {
    return m_right;
  }
  bool playing() const
  {
    return m_playing;
  }
  uint64_t current_lba() const
  {
    return m_lba;
  }
  void set_volume(uint8_t volume)
  {
    m_volume = volume;
  }

  struct SavedState {
    uint64_t clock_acc = 0;
    SegaCdSector sector{};
    size_t frame = kFramesPerSector;
    uint64_t lba = 0;
    uint64_t end_lba = 0;
    bool playing = false;
    uint8_t volume = 255;
    int32_t left = 0;
    int32_t right = 0;
  };
  SavedState save() const;
  bool restore(const SavedState &state);
  bool valid_saved_state(const SavedState &state) const;

private:
  bool load_sector();

  const SegaCdDisc &m_disc;
  uint64_t m_master_frequency;
  uint64_t m_clock_acc = 0;
  SegaCdSector m_sector{};
  size_t m_frame = kFramesPerSector;
  uint64_t m_lba = 0;
  uint64_t m_end_lba = 0;
  bool m_playing = false;
  uint8_t m_volume = 255;
  int32_t m_left = 0;
  int32_t m_right = 0;
};

}  // namespace generator
