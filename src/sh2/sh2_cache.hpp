/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace generator {

class Sh2Cache {
public:
  struct SavedState {
    struct Entry {
      uint32_t tag = 0;
      uint8_t lru = 0;
      bool valid = false;
    };
    uint8_t ccr = 0;
    std::array<std::array<Entry, 64>, 4> entries{};
  };

  void reset();
  uint8_t read_ccr() const;
  void write_ccr(uint8_t value);
  uint32_t read32(uint32_t address);
  void write32(uint32_t address, uint32_t value);
  SavedState save() const
  {
    SavedState state;
    state.ccr = m_ccr;
    for (size_t way = 0; way < 4; way++) {
      for (size_t index = 0; index < 64; index++) {
        state.entries[way][index] = {m_entries[way][index].tag,
                                     m_entries[way][index].lru,
                                     m_entries[way][index].valid};
      }
    }
    return state;
  }
  void restore(const SavedState &state)
  {
    m_ccr = state.ccr;
    for (size_t way = 0; way < 4; way++) {
      for (size_t index = 0; index < 64; index++) {
        m_entries[way][index] = {state.entries[way][index].tag,
                                 state.entries[way][index].lru,
                                 state.entries[way][index].valid};
      }
    }
  }

private:
  struct Entry {
    uint32_t tag = 0;
    uint8_t lru = 0;
    bool valid = false;
  };

  void purge();
  void purge(uint32_t address);

  uint8_t m_ccr = 0;
  std::array<std::array<Entry, 64>, 4> m_entries{};
};

}  // namespace generator
