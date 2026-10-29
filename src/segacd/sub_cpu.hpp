/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "m68k/m68k.hpp"
#include "sub_bus.hpp"

#include <cstdint>

namespace generator {

class SegaCdSubCpu final : private MasterClockSink {
public:
  static constexpr uint64_t kFrequency = 12500000;
  static constexpr uint64_t kDefaultMasterFrequency = 53693175;

  explicit SegaCdSubCpu(uint64_t master_frequency = kDefaultMasterFrequency);

  void power_on_reset();
  void set_running(bool running);
  bool running() const
  {
    return m_running;
  }
  void set_ipl(int level)
  {
    m_cpu.set_ipl(level);
  }
  void advance_mclk(uint64_t ticks);

  M68k &cpu()
  {
    return m_cpu;
  }
  const M68k &cpu() const
  {
    return m_cpu;
  }
  SegaCdSubBus &bus()
  {
    return m_bus;
  }
  const SegaCdSubBus &bus() const
  {
    return m_bus;
  }
  uint64_t master_ticks() const
  {
    return m_master_ticks;
  }
  int64_t clock_credit() const
  {
    return m_clock_credit;
  }

  struct SavedState {
    SegaCdSubBus::SavedState bus{};
    M68k::SavedState cpu{};
    bool running = false;
    uint64_t master_ticks = 0;
    uint64_t clock_acc = 0;
    int64_t clock_credit = 0;
  };
  SavedState save() const;
  bool restore(const SavedState &state);
  bool valid_saved_state(const SavedState &state) const;

private:
  void advance_68k_clocks(uint64_t clocks) override;

  uint64_t m_master_frequency;
  SegaCdSubBus m_bus;
  M68k m_cpu;
  bool m_running = false;
  uint64_t m_master_ticks = 0;
  uint64_t m_clock_acc = 0;
  int64_t m_clock_credit = 0;
};

}  // namespace generator
