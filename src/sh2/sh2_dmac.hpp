/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstddef>
#include <cstdint>

namespace generator {

class Sh2;

class Sh2Dmac {
public:
  struct SavedState {
    struct Channel {
      uint32_t sar = 0;
      uint32_t dar = 0;
      uint32_t tcr = 0;
      uint32_t chcr = 0;
      uint32_t vcr = 0;
      uint32_t unit_cycles = 2;
      bool active = false;
      bool read_te = false;
    };
    Channel channels[2]{};
    uint32_t dmaor = 0;
    uint32_t cycle_credit = 0;
    uint8_t drcr[2]{};
    uint8_t next_channel = 1;
    bool dreq_level[2]{};
    bool edge_pending[2]{};
    bool module_pending[2]{};
  };

  explicit Sh2Dmac(Sh2 &cpu);
  void reset();
  uint8_t read8(uint32_t address);
  uint32_t read32(uint32_t address);
  void write8(uint32_t address, uint8_t value);
  void write32(uint32_t address, uint32_t value);
  void set_dreq(size_t channel, bool asserted);
  void set_module_request(size_t channel, bool asserted);
  void set_nmi();
  void advance(uint32_t cycles);

  bool completion_pending(size_t channel) const;
  bool take_address_error();
  uint8_t vector(size_t channel) const;
  SavedState save() const
  {
    SavedState state;
    for (size_t index = 0; index < 2; index++) {
      const Channel &source = m_channels[index];
      state.channels[index] = {source.sar,    source.dar,    source.tcr,
                               source.chcr,   source.vcr,    source.unit_cycles,
                               source.active, source.read_te};
    }
    state.dmaor = m_dmaor;
    state.cycle_credit = m_cycle_credit;
    state.drcr[0] = m_drcr[0];
    state.drcr[1] = m_drcr[1];
    state.next_channel = m_next_channel;
    state.dreq_level[0] = m_dreq_level[0];
    state.dreq_level[1] = m_dreq_level[1];
    state.edge_pending[0] = m_edge_pending[0];
    state.edge_pending[1] = m_edge_pending[1];
    state.module_pending[0] = m_module_pending[0];
    state.module_pending[1] = m_module_pending[1];
    return state;
  }
  void restore(const SavedState &state)
  {
    for (size_t index = 0; index < 2; index++) {
      const SavedState::Channel &source = state.channels[index];
      m_channels[index] = {source.sar,    source.dar,    source.tcr,
                           source.chcr,   source.vcr,    source.unit_cycles,
                           source.active, source.read_te};
    }
    m_dmaor = state.dmaor;
    m_cycle_credit = state.cycle_credit;
    m_drcr[0] = state.drcr[0];
    m_drcr[1] = state.drcr[1];
    m_next_channel = state.next_channel;
    m_dreq_level[0] = state.dreq_level[0];
    m_dreq_level[1] = state.dreq_level[1];
    m_edge_pending[0] = state.edge_pending[0];
    m_edge_pending[1] = state.edge_pending[1];
    m_module_pending[0] = state.module_pending[0];
    m_module_pending[1] = state.module_pending[1];
  }

private:
  struct Channel {
    uint32_t sar = 0;
    uint32_t dar = 0;
    uint32_t tcr = 0;
    uint32_t chcr = 0;
    uint32_t vcr = 0;
    uint32_t unit_cycles = 2;
    bool active = false;
    bool read_te = false;
  };

  bool ready(const Channel &channel) const;
  bool external_ready(size_t index) const;
  bool transfer(Channel &channel);
  void advance_channel(Channel &channel, uint32_t &credits);
  uint32_t adjusted(uint32_t address, uint32_t mode, uint32_t width) const;

  Sh2 &m_cpu;
  Channel m_channels[2]{};
  uint32_t m_dmaor = 0;
  uint32_t m_cycle_credit = 0;
  uint8_t m_drcr[2]{};
  uint8_t m_next_channel = 1;
  bool m_dreq_level[2]{};
  bool m_edge_pending[2]{};
  bool m_module_pending[2]{};
  bool m_address_error_pending = false;
};

}  // namespace generator
