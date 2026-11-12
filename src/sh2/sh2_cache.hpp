/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace generator {

class Sh2;

class Sh2Cache {
public:
  struct SavedState {
    struct Entry {
      uint32_t tag = 0;
      uint8_t lru = 0;
      bool valid = false;
      std::array<uint8_t, 16> data{};
    };
    uint8_t ccr = 0;
    std::array<std::array<Entry, 64>, 4> entries{};
  };

  explicit Sh2Cache(Sh2 &cpu);
  void reset();
  bool enabled() const
  {
    return (m_ccr & 1) != 0;
  }
  uint8_t read_ccr() const;
  void write_ccr(uint8_t value);
  uint8_t read8(uint32_t address, bool instruction = false);
  uint16_t read16(uint32_t address, bool instruction = false);
  uint32_t read32(uint32_t address, bool instruction = false);
  void write8(uint32_t address, uint8_t value);
  void write16(uint32_t address, uint16_t value);
  void write32(uint32_t address, uint32_t value);
  uint32_t take_wait_states();
  SavedState save() const
  {
    SavedState state;
    state.ccr = m_ccr;
    for (size_t way = 0; way < 4; way++) {
      for (size_t index = 0; index < 64; index++) {
        state.entries[way][index] = {
            m_entries[way][index].tag, m_entries[way][index].lru,
            m_entries[way][index].valid, m_entries[way][index].data};
      }
    }
    return state;
  }
  void restore(const SavedState &state)
  {
    m_ccr = state.ccr;
    for (size_t way = 0; way < 4; way++) {
      for (size_t index = 0; index < 64; index++) {
        m_entries[way][index] = {
            state.entries[way][index].tag, state.entries[way][index].lru,
            state.entries[way][index].valid, state.entries[way][index].data};
      }
    }
    m_wait_states = 0;
  }

private:
  struct Entry {
    uint32_t tag = 0;
    uint8_t lru = 0;
    bool valid = false;
    std::array<uint8_t, 16> data{};
  };

  uint32_t read(uint32_t address, uint32_t size, bool instruction);
  void write(uint32_t address, uint32_t size, uint32_t value);
  uint32_t read_bus(uint32_t address, uint32_t size);
  void write_bus(uint32_t address, uint32_t size, uint32_t value);
  Entry *find(uint32_t address, size_t *way = nullptr);
  Entry &replacement(uint32_t address);
  void update_lru(size_t index, size_t way);
  void fill(Entry &entry, size_t way, uint32_t address);
  void purge();
  void purge(uint32_t address);
  static size_t way_index(uint32_t address);
  static size_t line_index(uint32_t address);

  Sh2 &m_cpu;
  uint8_t m_ccr = 0;
  std::array<std::array<Entry, 64>, 4> m_entries{};
  uint32_t m_wait_states = 0;
};

}  // namespace generator
