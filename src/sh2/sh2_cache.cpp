/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sh2_cache.hpp"

namespace generator {

void Sh2Cache::reset()
{
  m_ccr = 0;
  for (auto &way : m_entries) {
    for (auto &entry : way)
      entry = Entry{};
  }
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
  const size_t index = (address >> 4) & 0x3F;
  for (auto &way : m_entries) {
    Entry &entry = way[index];
    if (entry.valid && entry.tag == tag) {
      entry.valid = false;
      entry.lru = 0;
    }
  }
}

uint32_t Sh2Cache::read32(uint32_t address)
{
  if (address >= 0x40000000 && address < 0x60000000) {
    purge(address);
    return 0;
  }
  if (address >= 0x60000000 && address <= 0x600003FF) {
    const Entry &entry = m_entries[(m_ccr >> 6) & 3][(address >> 4) & 0x3F];
    return entry.tag | (uint32_t(entry.lru & 0x3F) << 3) |
           (entry.valid ? 2 : 0);
  }
  return 0;
}

void Sh2Cache::write32(uint32_t address, uint32_t value)
{
  if (address >= 0x40000000 && address < 0x60000000) {
    purge(address);
    return;
  }
  if (address >= 0x60000000 && address <= 0x600003FF) {
    Entry &entry = m_entries[(m_ccr >> 6) & 3][(address >> 4) & 0x3F];
    entry.tag = address & 0x1FFFFE00;
    entry.lru = (value >> 16) & 0x3F;
    entry.valid = (address & 2) != 0;
  }
}

}  // namespace generator
