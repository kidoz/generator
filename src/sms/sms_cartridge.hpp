/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Master System cartridge image, Sega mapper, and Codemasters mapper. */

#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace generator {

enum class SmsMapper : uint8_t {
  Sega,
  Codemasters,
};

class SmsCartridge {
public:
  static constexpr size_t kSramSize = 0x8000;

  std::expected<void, std::string> load(std::vector<uint8_t> rom);
  void unload();
  void reset();
  void set_mapper(SmsMapper mapper);
  bool loaded() const;
  uint64_t rom_id() const;

  uint8_t read(uint16_t addr) const;
  void write(uint16_t addr, uint8_t value);

  const std::vector<uint8_t> &rom() const;
  const std::array<uint8_t, kSramSize> &sram() const;
  std::array<uint8_t, kSramSize> &sram();
  bool sram_dirty() const;
  void clear_sram_dirty();

  struct SavedState {
    SmsMapper mapper = SmsMapper::Sega;
    uint8_t control = 0;
    uint8_t banks[3] = {0, 1, 2};
    std::array<uint8_t, kSramSize> sram{};
  };
  SavedState save() const;
  void restore(const SavedState &state);

private:
  uint32_t map_rom(uint8_t bank, uint16_t addr) const;

  std::vector<uint8_t> m_rom;
  std::array<uint8_t, kSramSize> m_sram{};
  SmsMapper m_mapper = SmsMapper::Sega;
  uint8_t m_control = 0;
  uint8_t m_banks[3] = {0, 1, 2};
  bool m_sram_dirty = false;
};

std::expected<std::vector<uint8_t>, std::string>
normalize_sms_rom(std::span<const uint8_t> image);

}  // namespace generator
