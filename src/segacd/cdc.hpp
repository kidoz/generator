/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "disc.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace generator {

enum class SegaCdCdcDmaTarget : uint8_t {
  ProgramRam = 1,
  WordRamMain = 2,
  WordRamSub = 3,
};

struct SegaCdCdcTransfer {
  SegaCdCdcDmaTarget target = SegaCdCdcDmaTarget::ProgramRam;
  uint32_t destination = 0;
  uint8_t value = 0;
};

class SegaCdCdc {
public:
  static constexpr size_t kBufferSize = 16u * 1024u;
  static constexpr uint64_t kDmaFrequency = 1000000;

  explicit SegaCdCdc(uint64_t master_frequency);

  void reset();
  std::expected<void, std::string>
  push_sector(std::span<const uint8_t, kSegaCdSectorBytes> sector);
  uint8_t read_byte();
  std::expected<void, std::string> start_dma(SegaCdCdcDmaTarget target,
                                             uint32_t destination,
                                             uint32_t byte_count);
  void advance_mclk(uint64_t ticks);
  std::optional<SegaCdCdcTransfer> take_dma_transfer();
  void acknowledge_interrupt();

  size_t available() const
  {
    return m_used;
  }
  bool dma_active() const
  {
    return m_dma_active;
  }
  bool irq_pending() const
  {
    return m_irq_pending;
  }
  uint32_t dma_destination() const
  {
    return m_dma_destination;
  }
  uint32_t dma_remaining() const
  {
    return m_dma_remaining;
  }

  struct SavedState {
    std::array<uint8_t, kBufferSize> buffer{};
    size_t read_pos = 0;
    size_t write_pos = 0;
    size_t used = 0;
    uint64_t clock_acc = 0;
    bool dma_active = false;
    bool irq_pending = false;
    SegaCdCdcDmaTarget dma_target = SegaCdCdcDmaTarget::ProgramRam;
    uint32_t dma_destination = 0;
    uint32_t dma_remaining = 0;
  };
  SavedState save() const;
  bool restore(const SavedState &state);
  bool valid_saved_state(const SavedState &state) const;

private:
  uint64_t m_master_frequency;
  std::array<uint8_t, kBufferSize> m_buffer{};
  size_t m_read_pos = 0;
  size_t m_write_pos = 0;
  size_t m_used = 0;
  uint64_t m_clock_acc = 0;
  bool m_dma_active = false;
  bool m_irq_pending = false;
  SegaCdCdcDmaTarget m_dma_target = SegaCdCdcDmaTarget::ProgramRam;
  uint32_t m_dma_destination = 0;
  uint32_t m_dma_remaining = 0;
};

}  // namespace generator
