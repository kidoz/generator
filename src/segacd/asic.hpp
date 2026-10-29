/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace generator {

struct SegaCdAsicConfig {
  std::span<const uint8_t> source;
  std::span<uint8_t> destination;
  uint32_t source_width = 0;
  uint32_t source_height = 0;
  uint32_t destination_width = 0;
  uint32_t destination_height = 0;
  uint32_t scale_16_16 = 0x10000;
  uint16_t angle = 0;
  uint32_t destination_offset = 0;
};

struct SegaCdAsicPixel {
  uint32_t destination = 0;
  uint8_t value = 0;
};

class SegaCdAsic {
public:
  static constexpr uint64_t kFrequency = 12500000;

  explicit SegaCdAsic(uint64_t master_frequency);

  void reset();
  std::expected<void, std::string> start(const SegaCdAsicConfig &config);
  void advance_mclk(uint64_t ticks);
  std::optional<SegaCdAsicPixel> take_pixel();
  void acknowledge_interrupt();

  bool busy() const
  {
    return m_busy;
  }
  bool irq_pending() const
  {
    return m_irq_pending;
  }
  uint64_t pixels_remaining() const
  {
    return m_pixels_remaining;
  }

  struct SavedState {
    uint32_t source_offset = 0;
    uint32_t destination_buffer_offset = 0;
    uint32_t source_width = 0;
    uint32_t source_height = 0;
    uint32_t destination_width = 0;
    uint32_t destination_height = 0;
    uint32_t scale_16_16 = 0x10000;
    uint16_t angle = 0;
    uint32_t destination_offset = 0;
    uint64_t clock_acc = 0;
    uint64_t pixels_remaining = 0;
    uint64_t pixel_index = 0;
    bool busy = false;
    bool irq_pending = false;
  };
  SavedState save(std::span<const uint8_t> word_ram) const;
  bool restore(const SavedState &state, std::span<uint8_t> word_ram);
  bool valid_saved_state(const SavedState &state,
                         std::span<const uint8_t> word_ram) const;

private:
  uint8_t render_pixel(uint32_t x, uint32_t y) const;

  uint64_t m_master_frequency;
  SegaCdAsicConfig m_config{};
  uint64_t m_clock_acc = 0;
  uint64_t m_pixels_remaining = 0;
  uint64_t m_pixel_index = 0;
  bool m_busy = false;
  bool m_irq_pending = false;
};

}  // namespace generator
