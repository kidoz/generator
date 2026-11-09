/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sh2_cache.hpp"

#include "sh2.hpp"

namespace generator {

Sh2Cache::Sh2Cache(Sh2BusInterface &bus) : m_bus(bus)
{
}

void Sh2Cache::reset()
{
  m_ccr = 0;
  for (auto &way : m_entries) {
    for (auto &entry : way)
      entry = Entry{};
  }
  m_wait_states = 0;
}

uint8_t Sh2Cache::read_ccr() const
{
  return m_ccr & 0xEF;
}

void Sh2Cache::write_ccr(uint8_t value)
{
  m_ccr = value & 0xEF;
  if (value & 0x10)
    purge();
}

uint32_t Sh2Cache::take_wait_states()
{
  const uint32_t result = m_wait_states;
  m_wait_states = 0;
  return result;
}

size_t Sh2Cache::way_index(uint32_t address)
{
  return (address >> 10) & 3;
}

size_t Sh2Cache::line_index(uint32_t address)
{
  return (address >> 4) & 0x3F;
}

Sh2Cache::Entry *Sh2Cache::find(uint32_t address, size_t *way)
{
  const uint32_t tag = address & 0x1FFFFE00;
  for (size_t current = 0; current < 4; current++) {
    Entry &entry = m_entries[current][line_index(address)];
    if (entry.valid && entry.tag == tag) {
      if (way != nullptr)
        *way = current;
      return &entry;
    }
  }
  return nullptr;
}

void Sh2Cache::update_lru(size_t index, size_t way)
{
  uint8_t &lru = m_entries[way][index].lru;
  uint8_t value = lru;
  if (way == 0)
    value &= ~0x38;
  else if (way == 1)
    value = (value | 0x20) & ~0x06;
  else if (way == 2)
    value = (value | 0x14) & ~0x01;
  else
    value |= 0x0B;
  for (size_t other = 0; other < 4; other++)
    m_entries[other][index].lru = value;
}

Sh2Cache::Entry &Sh2Cache::replacement(uint32_t address)
{
  const size_t index = line_index(address);
  const uint8_t lru = m_entries[0][index].lru;
  const bool two_way = (m_ccr & 8) != 0;
  if (!two_way && (lru & 0x38) == 0x38)
    return m_entries[0][index];
  if (!two_way && !(lru & 0x20) && (lru & 6) == 6)
    return m_entries[1][index];
  if (!(lru & 0x10) && !(lru & 4) && (lru & 1))
    return m_entries[2][index];
  return m_entries[3][index];
}

void Sh2Cache::fill(Entry &entry, size_t way, uint32_t address)
{
  const uint32_t line = address & ~0xFu;
  const uint32_t target = address & 0xC;
  for (uint32_t offset = 0; offset < 16; offset += 4) {
    if (!m_bus.can_access(line + ((target + offset + 4) & 0xC), false)) {
      (void)m_bus.read32(address & ~3u);
      return;
    }
  }
  entry.tag = address & 0x1FFFFE00;
  entry.valid = true;
  for (uint32_t offset = 0; offset < 16; offset += 4) {
    const uint32_t current = (target + offset + 4) & 0xC;
    const uint32_t value = m_bus.read32(line + current);
    for (uint32_t byte = 0; byte < 4; byte++)
      entry.data[current + byte] = value >> (24 - byte * 8);
  }
  update_lru(line_index(address), way);
  m_wait_states += 4;
}

uint32_t Sh2Cache::read_bus(uint32_t address, uint32_t size)
{
  if (size == 1)
    return m_bus.read8(address);
  if (size == 2)
    return m_bus.read16(address);
  return m_bus.read32(address);
}

void Sh2Cache::write_bus(uint32_t address, uint32_t size, uint32_t value)
{
  if (size == 1)
    m_bus.write8(address, value >> 24);
  else if (size == 2)
    m_bus.write16(address, value >> 16);
  else
    m_bus.write32(address, value);
}

uint32_t Sh2Cache::read(uint32_t address, uint32_t size, bool instruction)
{
  const uint32_t area = address >> 29;
  if (area == 6)
    return m_entries[way_index(address)][line_index(address)]
        .data[address & 0xF];
  if (area == 2) {
    purge(address);
    return 0;
  }
  if (area == 3) {
    if (size != 4)
      return 0;
    const Entry &entry = m_entries[(m_ccr >> 6) & 3][line_index(address)];
    return entry.tag | (uint32_t(entry.lru & 0x3F) << 4) |
           (entry.valid ? 4 : 0);
  }
  if (area != 0 || !(m_ccr & 1))
    return read_bus(address, size);
  size_t way = 0;
  Entry *entry = find(address, &way);
  if (entry == nullptr) {
    const bool disabled = instruction ? (m_ccr & 2) : (m_ccr & 4);
    if (disabled)
      return read_bus(address, size);
    Entry &selected = replacement(address);
    const size_t selected_way =
        &selected == &m_entries[0][line_index(address)]   ? 0
        : &selected == &m_entries[1][line_index(address)] ? 1
        : &selected == &m_entries[2][line_index(address)] ? 2
                                                          : 3;
    fill(selected, selected_way, address);
    entry = find(address);
    if (entry == nullptr)
      return 0;
  } else {
    update_lru(line_index(address), way);
  }
  uint32_t value = 0;
  for (uint32_t byte = 0; byte < size; byte++)
    value = (value << 8) | entry->data[(address + byte) & 0xF];
  return value;
}

void Sh2Cache::write(uint32_t address, uint32_t size, uint32_t value)
{
  const uint32_t area = address >> 29;
  if (area == 6) {
    for (uint32_t byte = 0; byte < size; byte++)
      m_entries[way_index(address)][line_index(address)]
          .data[(address + byte) & 0xF] = value >> (24 - byte * 8);
    return;
  }
  if (area == 2) {
    if (size == 4) {
      purge(address);
      m_wait_states++;
    }
    return;
  }
  if (area == 3) {
    if (size == 4) {
      Entry &entry = m_entries[(m_ccr >> 6) & 3][line_index(address)];
      entry.tag = address & 0x1FFFFE00;
      entry.lru = (value >> 4) & 0x3F;
      entry.valid = (address & 4) != 0;
      for (size_t way = 0; way < 4; way++)
        m_entries[way][line_index(address)].lru = entry.lru;
    }
    return;
  }
  if (area == 0 && (m_ccr & 1)) {
    size_t way = 0;
    if (Entry *entry = find(address, &way)) {
      for (uint32_t byte = 0; byte < size; byte++)
        entry->data[(address + byte) & 0xF] = value >> (24 - byte * 8);
      update_lru(line_index(address), way);
    }
  }
  write_bus(address, size, value);
}

void Sh2Cache::purge()
{
  for (auto &way : m_entries) {
    for (auto &entry : way) {
      entry.valid = false;
      entry.lru = 0;
    }
  }
}

void Sh2Cache::purge(uint32_t address)
{
  const uint32_t tag = address & 0x1FFFFE00;
  for (size_t way = 0; way < 4; way++) {
    Entry &entry = m_entries[way][line_index(address)];
    if (entry.valid && entry.tag == tag)
      entry.valid = false;
  }
}

uint8_t Sh2Cache::read8(uint32_t address, bool instruction)
{
  if ((address >> 29) == 6)
    return m_entries[way_index(address)][line_index(address)]
        .data[address & 0xF];
  return read(address, 1, instruction);
}

uint16_t Sh2Cache::read16(uint32_t address, bool instruction)
{
  if ((address >> 29) == 6) {
    const auto &data = m_entries[way_index(address)][line_index(address)].data;
    return (uint16_t)data[address & 0xF] << 8 | data[(address + 1) & 0xF];
  }
  return read(address, 2, instruction);
}

uint32_t Sh2Cache::read32(uint32_t address, bool instruction)
{
  if ((address >> 29) == 6) {
    const auto &data = m_entries[way_index(address)][line_index(address)].data;
    return (uint32_t)data[address & 0xF] << 24 |
           (uint32_t)data[(address + 1) & 0xF] << 16 |
           (uint32_t)data[(address + 2) & 0xF] << 8 | data[(address + 3) & 0xF];
  }
  return read(address, 4, instruction);
}

void Sh2Cache::write8(uint32_t address, uint8_t value)
{
  if ((address >> 29) == 6) {
    m_entries[way_index(address)][line_index(address)].data[address & 0xF] =
        value;
    return;
  }
  write(address, 1, value << 24);
}

void Sh2Cache::write16(uint32_t address, uint16_t value)
{
  if ((address >> 29) == 6) {
    auto &data = m_entries[way_index(address)][line_index(address)].data;
    data[address & 0xF] = value >> 8;
    data[(address + 1) & 0xF] = value;
    return;
  }
  write(address, 2, value << 16);
}

void Sh2Cache::write32(uint32_t address, uint32_t value)
{
  if ((address >> 29) == 6) {
    auto &data = m_entries[way_index(address)][line_index(address)].data;
    data[address & 0xF] = value >> 24;
    data[(address + 1) & 0xF] = value >> 16;
    data[(address + 2) & 0xF] = value >> 8;
    data[(address + 3) & 0xF] = value;
    return;
  }
  write(address, 4, value);
}

}  // namespace generator
