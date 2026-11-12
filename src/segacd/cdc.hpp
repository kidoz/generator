/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "disc.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace generator {

enum class SegaCdCdcDmaTarget : uint8_t {
  ProgramRam = 1,
  WordRamMain = 2,
  WordRamSub = 3,
  PcmRam = 4,
};

struct SegaCdCdcTransfer {
  SegaCdCdcDmaTarget target = SegaCdCdcDmaTarget::ProgramRam;
  uint32_t destination = 0;
  uint8_t value = 0;
};

class SegaCdCdc {
public:
  static constexpr size_t kBufferSize = 16u * 1024u;
  static constexpr uint64_t kDmaFrequency = 1000000;

  explicit SegaCdCdc(uint64_t master_frequency);

  void reset();
  std::expected<void, std::string>
  push_sector(std::span<const uint8_t, kSegaCdSectorBytes> sector);
  uint8_t read_byte();
  std::expected<void, std::string> start_dma(SegaCdCdcDmaTarget target,
                                             uint32_t destination,
                                             uint32_t byte_count);
  void advance_mclk(uint64_t ticks);
  std::optional<SegaCdCdcTransfer> take_dma_transfer();
  void acknowledge_interrupt();
  void decode_sector(const SegaCdSector &sector, int64_t lba);
  void advance_decoder(uint64_t ticks, bool reading);
  uint16_t read_mode() const;
  void write_mode(uint16_t data, bool upper, bool lower);
  uint8_t read_register();
  void write_register(uint8_t data);
  uint16_t read_host(bool main);
  uint16_t dma_address() const;
  void set_dma_address(uint16_t address);

  struct RegisterState {
    bool enabled = false;
    uint8_t index = 0;
    uint8_t mode = 0;
    uint8_t ifstat = 0xff;
    uint8_t ifctrl = 0;
    std::array<uint8_t, 2> control{};
    std::array<uint8_t, 4> header{};
    std::array<uint8_t, 4> status{0, 0, 0, 0x80};
    uint16_t count = 0;
    uint16_t source = 0;
    uint16_t pointer = 0;
    uint16_t write_address = 0;
    uint32_t destination = 0;
    bool transferring = false;
    uint64_t decoder_clock_acc = 0;
  };

  size_t available() const
  {
    return m_used;
  }
  bool dma_active() const
  {
    return m_dma_active ||
           (m_registers.transferring && (m_registers.mode & 7) >= 4);
  }
  bool irq_pending() const
  {
    return m_irq_pending;
  }
  uint32_t dma_destination() const
  {
    return m_registers.enabled ? m_registers.destination : m_dma_destination;
  }
  uint32_t dma_remaining() const
  {
    return m_registers.enabled
               ? (m_registers.transferring ? (uint32_t)m_registers.count + 1
                                           : 0)
               : m_dma_remaining;
  }

  struct SavedState {
    std::array<uint8_t, kBufferSize> buffer{};
    size_t read_pos = 0;
    size_t write_pos = 0;
    size_t used = 0;
    uint64_t clock_acc = 0;
    bool dma_active = false;
    bool irq_pending = false;
    SegaCdCdcDmaTarget dma_target = SegaCdCdcDmaTarget::ProgramRam;
    uint32_t dma_destination = 0;
    uint32_t dma_remaining = 0;
    RegisterState registers{};
  };
  SavedState save() const;
  bool restore(const SavedState &state);
  bool valid_saved_state(const SavedState &state) const;

private:
  uint64_t m_master_frequency;
  std::array<uint8_t, kBufferSize> m_buffer{};
  size_t m_read_pos = 0;
  size_t m_write_pos = 0;
  size_t m_used = 0;
  uint64_t m_clock_acc = 0;
  bool m_dma_active = false;
  bool m_irq_pending = false;
  SegaCdCdcDmaTarget m_dma_target = SegaCdCdcDmaTarget::ProgramRam;
  uint32_t m_dma_destination = 0;
  uint32_t m_dma_remaining = 0;
  RegisterState m_registers;
  void update_irq();
  uint8_t transfer_byte();
  void finish_transfer();
};

}  // namespace generator
