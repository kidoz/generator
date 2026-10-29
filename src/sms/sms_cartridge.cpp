/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sms_cartridge.hpp"

#include "core/state_files.hpp"

#include <algorithm>

namespace generator {

namespace {

constexpr size_t kMinRomBytes = 0x200;
constexpr size_t kMaxRomBytes = 32u * 1024u * 1024u;

}  // namespace

std::expected<std::vector<uint8_t>, std::string>
normalize_sms_rom(std::span<const uint8_t> image)
{
  if (image.size() > kMaxRomBytes) {
    return std::unexpected("SMS ROM image is too large");
  }
  if (image.size() < kMinRomBytes) {
    return std::unexpected("SMS ROM image is too small");
  }
  if (image[0] == 'P' && image[1] == 'K' &&
      ((image[2] == 3 && image[3] == 4) || (image[2] == 5 && image[3] == 6) ||
       (image[2] == 7 && image[3] == 8))) {
    return std::unexpected(
        "ZIP archives are not supported; extract the ROM before loading");
  }

  size_t offset = 0;
  if (image.size() >= kMinRomBytes + 512 && image.size() % 0x4000 == 512) {
    offset = 512;
  }
  std::vector<uint8_t> rom(image.begin() + (std::ptrdiff_t)offset, image.end());
  if (rom.size() < kMinRomBytes) {
    return std::unexpected("SMS ROM image is too small");
  }
  return rom;
}

std::expected<void, std::string> SmsCartridge::load(std::vector<uint8_t> rom)
{
  if (rom.empty()) {
    return std::unexpected("SMS ROM image is empty");
  }
  m_rom = std::move(rom);
  m_sram.fill(0);
  m_sram_dirty = false;
  reset();
  return {};
}

void SmsCartridge::unload()
{
  m_rom.clear();
  m_sram.fill(0);
  m_sram_dirty = false;
  reset();
}

void SmsCartridge::reset()
{
  m_control = 0;
  if (m_mapper == SmsMapper::Sega) {
    m_banks[0] = 0;
    m_banks[1] = 1;
    m_banks[2] = 2;
  } else {
    m_banks[0] = 0;
    m_banks[1] = 1;
    m_banks[2] = 0;
  }
}

void SmsCartridge::set_mapper(SmsMapper mapper)
{
  m_mapper = mapper;
  reset();
}

bool SmsCartridge::loaded() const
{
  return !m_rom.empty();
}

uint64_t SmsCartridge::rom_id() const
{
  return state_fnv1a64(m_rom);
}

uint32_t SmsCartridge::map_rom(uint8_t bank, uint16_t addr) const
{
  const size_t banks = std::max<size_t>(1, (m_rom.size() + 0x3FFF) / 0x4000);
  return (uint32_t)(bank % banks) * 0x4000 + (addr & 0x3FFF);
}

uint8_t SmsCartridge::read(uint16_t addr) const
{
  if (m_rom.empty()) {
    return 0xFF;
  }

  if (m_mapper == SmsMapper::Sega) {
    if ((m_control & 0x10) != 0 && addr >= 0xC000) {
      return m_sram[addr & 0x3FFF];
    }
    if ((m_control & 0x08) != 0 && addr >= 0x8000 && addr < 0xC000) {
      const uint8_t ram_bank = (m_control & 0x04) != 0 ? 1 : 0;
      return m_sram[ram_bank * 0x4000 + (addr & 0x3FFF)];
    }
  } else if ((m_banks[1] & 0x80) != 0 && addr >= 0xA000 && addr < 0xC000) {
    return m_sram[addr & 0x1FFF];
  }

  if (addr < 0x4000) {
    if (addr < 0x400 && m_mapper == SmsMapper::Sega) {
      return m_rom[addr % m_rom.size()];
    }
    return m_rom[map_rom(m_banks[0], addr) % m_rom.size()];
  }
  if (addr < 0x8000) {
    return m_rom[map_rom(m_banks[1], addr) % m_rom.size()];
  }
  if (addr < 0xC000) {
    return m_rom[map_rom(m_banks[2], addr) % m_rom.size()];
  }
  return 0xFF;
}

void SmsCartridge::write(uint16_t addr, uint8_t value)
{
  if (m_mapper == SmsMapper::Codemasters) {
    if (addr == 0x0000) {
      m_banks[0] = value;
    } else if (addr == 0x4000) {
      m_banks[1] = value;
    } else if (addr == 0x8000) {
      m_banks[2] = value;
    }
  } else if (addr >= 0xFFFC) {
    static constexpr uint8_t shifts[] = {0, 0x18, 0x10, 0x08};
    if (addr == 0xFFFC) {
      m_control = value;
    } else {
      m_banks[addr - 0xFFFD] = (uint8_t)(value + shifts[m_control & 3]);
    }
  }

  if (m_mapper == SmsMapper::Sega && (m_control & 0x08) != 0 &&
      addr >= 0x8000 && addr < 0xC000) {
    const uint8_t ram_bank = (m_control & 0x04) != 0 ? 1 : 0;
    m_sram[ram_bank * 0x4000 + (addr & 0x3FFF)] = value;
    m_sram_dirty = true;
  } else if (m_mapper == SmsMapper::Sega && (m_control & 0x10) != 0 &&
             addr >= 0xC000) {
    m_sram[addr & 0x3FFF] = value;
    m_sram_dirty = true;
  } else if (m_mapper == SmsMapper::Codemasters && (m_banks[1] & 0x80) != 0 &&
             addr >= 0xA000 && addr < 0xC000) {
    m_sram[addr & 0x1FFF] = value;
    m_sram_dirty = true;
  }
}

const std::vector<uint8_t> &SmsCartridge::rom() const
{
  return m_rom;
}

const std::array<uint8_t, SmsCartridge::kSramSize> &SmsCartridge::sram() const
{
  return m_sram;
}

std::array<uint8_t, SmsCartridge::kSramSize> &SmsCartridge::sram()
{
  return m_sram;
}

bool SmsCartridge::sram_dirty() const
{
  return m_sram_dirty;
}

void SmsCartridge::clear_sram_dirty()
{
  m_sram_dirty = false;
}

SmsCartridge::SavedState SmsCartridge::save() const
{
  return SavedState{
      m_mapper, m_control, {m_banks[0], m_banks[1], m_banks[2]}, m_sram};
}

void SmsCartridge::restore(const SavedState &state)
{
  m_mapper = state.mapper;
  m_control = state.control;
  std::copy_n(state.banks, 3, m_banks);
  m_sram = state.sram;
  m_sram_dirty = false;
}

}  // namespace generator
