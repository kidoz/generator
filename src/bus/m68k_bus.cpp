/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "m68k_bus.hpp"

#include <cstring>

namespace generator {

M68kBus::M68kBus(BusDevices &devices) : m_devices(devices)
{
}

void M68kBus::attach_rom(const uint8_t *rom, std::size_t size)
{
  m_rom = rom;
  m_rom_size = size;
}

void M68kBus::attach_ram(uint8_t *ram)
{
  m_ram = ram;
}

void M68kBus::enable_sram(uint8_t *sram, uint32_t start, uint32_t size,
                          bool writable)
{
  m_sram = sram;
  m_sram_start = start;
  m_sram_end = start + size;
  m_sram_writable = writable;
}

void M68kBus::disable_sram()
{
  m_sram = nullptr;
  m_sram_start = 0;
  m_sram_end = 0;
  m_sram_writable = false;
}

void M68kBus::reset()
{
  m_data_bus = 0;
}

int M68kBus::read(uint32_t addr, bool upper, bool lower, uint16_t *out)
{
  /* The MC68000 exposes A23-A1 (plus UDS/LDS), so the upper byte of an
   * address register never reaches the Genesis bus. Some games deliberately
   * keep flags there and call or dereference the tagged address. */
  addr &= 0xFFFFFF;
  uint16_t value = 0;

  if (m_sram != nullptr && addr >= m_sram_start && addr < m_sram_end) {
    value = (uint16_t)((uint16_t)m_sram[(addr - m_sram_start) & 0x7FFF] << 8 |
                       m_sram[(addr - m_sram_start + 1) & 0x7FFF]);
  } else if (addr < 0x400000) {
    /* Cartridge ROM mirrors at its actual length. A bit mask only works
       for power-of-two dumps; homebrew images are often an exact,
       non-power-of-two size. The bus is big-endian. */
    if (m_rom != nullptr && m_rom_size > 1) {
      const std::size_t offset = (std::size_t)addr % m_rom_size;
      value = (uint16_t)((uint16_t)m_rom[offset] << 8 |
                         m_rom[(offset + 1) % m_rom_size]);
    } else {
      value = m_data_bus;
    }
  } else if (addr >= 0xE00000) {
    /* 68K DRAM, mirrored through the top 8MB (md.c resolves it every
       cycle with a row/column scramble; the scramble is an identity map
       as long as reads and writes agree, so a flat 64K mirror is
       externally equivalent). */
    const uint32_t offset = addr & 0xFFFE;
    value = (uint16_t)((uint16_t)m_ram[offset] << 8 | m_ram[offset + 1]);
  } else if (addr >= 0xC00000 && addr < 0xE00000) {
    value = m_devices.vdp_read(addr, upper, lower);
  } else if (addr >= 0xA00000 && addr < 0xA20000) {
    /* Z80 bus / IO region. Only three patterns decode the IO chip
       (0xA100xx version/ports, 0xA111xx BUSREQ, 0xA112xx ZRESET);
       everything else in the 128K region mirrors the Z80 space —
       Contra's boot mailbox at 0xA1B8 reads zram[0x1B8] this way. */
    const uint32_t page = addr & 0xFFFF00;
    if (page == 0xA10000 || page == 0xA11100 || page == 0xA11200) {
      value = m_devices.io_read(addr, upper, lower);
    } else {
      value = m_devices.z80_read(addr, upper, lower);
    }
  } else {
    /* unmapped: data bus keeps its last value */
    value = m_data_bus;
  }

  m_data_bus = value;
  *out = value;
  return 0;
}

int M68kBus::write(uint32_t addr, uint16_t data, bool upper, bool lower)
{
  addr &= 0xFFFFFF;
  if (addr >= 0xE00000) {
    const uint32_t offset = addr & 0xFFFE;
    if (upper) {
      m_ram[offset] = (uint8_t)(data >> 8);
    }
    if (lower) {
      m_ram[offset + 1] = (uint8_t)(data & 0xFF);
    }
  } else if (addr >= 0xC00000 && addr < 0xE00000) {
    m_devices.vdp_write(addr, data, upper, lower);
  } else if (addr >= 0xA00000 && addr < 0xA20000) {
    const uint32_t page = addr & 0xFFFF00;
    if (page == 0xA10000 || page == 0xA11100 || page == 0xA11200) {
      m_devices.io_write(addr, data, upper, lower);
    } else {
      m_devices.z80_write(addr, data, upper, lower);
    }
  } else if (m_sram != nullptr && addr >= m_sram_start && addr < m_sram_end) {
    const uint32_t offset = addr - m_sram_start;
    if (m_sram_writable) {
      if (upper) {
        m_sram[offset & 0x7FFF] = (uint8_t)(data >> 8);
      }
      if (lower) {
        m_sram[(offset + 1) & 0x7FFF] = (uint8_t)data;
      }
    }
  }
  /* ROM and unmapped regions: writes are silently dropped (no /DTACK from
     anywhere the CPU can see; the real machine would hang — we keep the
     CPU running until arbiter behaviour is modelled). */

  m_data_bus = data;
  return 0;
}

}  // namespace generator
