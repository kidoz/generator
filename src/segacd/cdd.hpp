/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "disc.hpp"
#include "sub_cpu.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace generator {

enum class SegaCdCddCommand : uint8_t {
  Stop = 1,
  Play = 2,
  Pause = 3,
  Resume = 4,
  Seek = 5,
  ReadSector = 7,
};

enum class SegaCdCddStatus : uint8_t {
  Ready = 0,
  Playing = 1,
  Paused = 2,
  Error = 3,
};

struct SegaCdTocEntry {
  unsigned int track = 0;
  SegaCdTrackType type = SegaCdTrackType::Mode1;
  uint64_t start_lba = 0;
  uint64_t sector_count = 0;
};

class SegaCdCdd {
public:
  explicit SegaCdCdd(
      const SegaCdDisc &disc,
      uint64_t master_frequency = SegaCdSubCpu::kDefaultMasterFrequency);

  void reset();
  std::expected<SegaCdTocEntry, std::string>
  toc_entry(unsigned int track) const;
  std::expected<void, std::string> execute(SegaCdCddCommand command,
                                           uint32_t parameter = 0);
  void advance_mclk(uint64_t ticks);
  std::optional<SegaCdSector> take_sector();
  void acknowledge_interrupt();

  SegaCdCddStatus status() const
  {
    return m_status;
  }
  uint64_t lba() const
  {
    return m_lba;
  }
  bool playing() const
  {
    return m_status == SegaCdCddStatus::Playing;
  }
  bool sector_pending() const
  {
    return m_sector.has_value();
  }
  bool irq_pending() const
  {
    return m_irq_pending;
  }
  uint32_t parameter() const
  {
    return m_parameter;
  }
  void set_parameter(uint32_t parameter)
  {
    m_parameter = parameter;
  }

  struct SavedState {
    uint64_t clock_acc = 0;
    uint64_t lba = 0;
    SegaCdCddStatus status = SegaCdCddStatus::Ready;
    bool sector_pending = false;
    SegaCdSector sector{};
    bool irq_pending = false;
    uint32_t parameter = 0;
  };
  SavedState save() const;
  bool restore(const SavedState &state);
  bool valid_saved_state(const SavedState &state) const;

private:
  std::expected<void, std::string> deliver_sector();

  const SegaCdDisc &m_disc;
  uint64_t m_master_frequency;
  uint64_t m_clock_acc = 0;
  uint64_t m_lba = 0;
  SegaCdCddStatus m_status = SegaCdCddStatus::Ready;
  std::optional<SegaCdSector> m_sector;
  bool m_irq_pending = false;
  uint32_t m_parameter = 0;
};

}  // namespace generator
