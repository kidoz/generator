/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cdc.hpp"

#include "sub_cpu.hpp"

namespace generator {

SegaCdCdc::SegaCdCdc(uint64_t master_frequency)
    : m_master_frequency(master_frequency == 0
                             ? SegaCdSubCpu::kDefaultMasterFrequency
                             : master_frequency)
{
}

void SegaCdCdc::reset()
{
  m_registers = {};
  m_read_pos = 0;
  m_write_pos = 0;
  m_used = 0;
  m_clock_acc = 0;
  m_dma_active = false;
  m_irq_pending = false;
  m_dma_destination = 0;
  m_dma_remaining = 0;
}

std::expected<void, std::string>
SegaCdCdc::push_sector(std::span<const uint8_t, kSegaCdSectorBytes> sector)
{
  if (m_used + sector.size() > m_buffer.size()) {
    return std::unexpected("CDC buffer is full");
  }
  for (const uint8_t byte : sector) {
    m_buffer[m_write_pos] = byte;
    m_write_pos = (m_write_pos + 1) % m_buffer.size();
  }
  m_used += sector.size();
  return {};
}

uint8_t SegaCdCdc::read_byte()
{
  if (m_used == 0) {
    return 0xFF;
  }
  const uint8_t value = m_buffer[m_read_pos];
  m_read_pos = (m_read_pos + 1) % m_buffer.size();
  m_used--;
  return value;
}

std::expected<void, std::string> SegaCdCdc::start_dma(SegaCdCdcDmaTarget target,
                                                      uint32_t destination,
                                                      uint32_t byte_count)
{
  if (m_dma_active ||
      (m_registers.transferring && (m_registers.mode & 7) >= 4)) {
    return std::unexpected("CDC DMA is already active");
  }
  if ((uint8_t)target < 1 || (uint8_t)target > 4 || byte_count == 0 ||
      byte_count > m_used) {
    return std::unexpected("CDC DMA length exceeds buffered data");
  }
  const uint32_t target_size = target == SegaCdCdcDmaTarget::ProgramRam
                                   ? SegaCdSubBus::kProgramRamSize
                               : target == SegaCdCdcDmaTarget::PcmRam ? 0x10000
                                                                      : 0x40000;
  if (destination > target_size || byte_count > target_size - destination) {
    return std::unexpected("CDC DMA destination is out of range");
  }
  m_dma_target = target;
  m_dma_destination = destination;
  m_dma_remaining = byte_count;
  m_dma_active = true;
  m_irq_pending = false;
  m_clock_acc = 0;
  return {};
}

void SegaCdCdc::advance_mclk(uint64_t ticks)
{
  if (m_dma_active ||
      (m_registers.transferring && (m_registers.mode & 7) >= 4)) {
    m_clock_acc += ticks * kDmaFrequency;
  }
}

std::optional<SegaCdCdcTransfer> SegaCdCdc::take_dma_transfer()
{
  if (m_registers.transferring && (m_registers.mode & 7) >= 4) {
    if (m_clock_acc < m_master_frequency)
      return std::nullopt;
    m_clock_acc -= m_master_frequency;
    const uint8_t mode = m_registers.mode & 7;
    const auto target = mode == 5   ? SegaCdCdcDmaTarget::ProgramRam
                        : mode == 4 ? SegaCdCdcDmaTarget::PcmRam
                                    : SegaCdCdcDmaTarget::WordRamSub;
    const uint32_t mask = mode == 5 ? 0x7ffff : mode == 4 ? 0xffff : 0x3ffff;
    const uint32_t address = m_registers.destination++ & mask;
    return SegaCdCdcTransfer{target, address, transfer_byte()};
  }
  if (!m_dma_active || m_clock_acc < m_master_frequency || m_used == 0) {
    return std::nullopt;
  }
  m_clock_acc -= m_master_frequency;
  const SegaCdCdcTransfer transfer{m_dma_target, m_dma_destination,
                                   read_byte()};
  m_dma_destination++;
  m_dma_remaining--;
  if (m_dma_remaining == 0) {
    m_dma_active = false;
    m_irq_pending = true;
    m_clock_acc = 0;
  }
  return transfer;
}

void SegaCdCdc::acknowledge_interrupt()
{
  m_irq_pending = false;
}

void SegaCdCdc::update_irq()
{
  m_irq_pending = ((~m_registers.ifstat) & m_registers.ifctrl & 0x60) != 0;
}

uint16_t SegaCdCdc::read_mode() const
{
  return (uint16_t)((uint16_t)m_registers.mode << 8 | m_registers.index);
}

void SegaCdCdc::write_mode(uint16_t data, bool upper, bool lower)
{
  m_registers.enabled = true;
  if (upper) {
    m_registers.mode = (uint8_t)(data >> 8) & 7;
    m_registers.destination = 0;
  }
  if (lower)
    m_registers.index = data & 31;
}

uint16_t SegaCdCdc::dma_address() const
{
  return (uint16_t)(m_registers.destination >>
                    ((m_registers.mode & 7) == 4 ? 2 : 3));
}

void SegaCdCdc::set_dma_address(uint16_t address)
{
  m_registers.destination = (uint32_t)address
                            << ((m_registers.mode & 7) == 4 ? 2 : 3);
}

void SegaCdCdc::decode_sector(const SegaCdSector &sector, int64_t lba)
{
  auto &r = m_registers;
  if (!r.enabled) {
    (void)push_sector(sector);
    return;
  }
  if (!(r.control[0] & 0x80))
    return;
  const uint64_t msf = lba + 150;
  auto bcd = [](unsigned value) {
    return (uint8_t)((value / 10 << 4) | (value % 10));
  };
  r.header = {bcd((unsigned)(msf / 4500)), bcd((unsigned)(msf / 75 % 60)),
              bcd((unsigned)(msf % 75)), 1};
  r.status = {0x80, 0, (uint8_t)(r.control[1] & 0x0c), 0};
  r.ifstat &= (uint8_t)~0x20;
  if (r.control[0] & 4) {
    r.pointer += 2352;
    r.write_address += 2352;
    for (size_t i = 0; i < 2052; i++)
      m_buffer[(r.pointer + i) & (kBufferSize - 1)] =
          i < 4 ? r.header[i] : sector[i + 12];
  }
  update_irq();
}

void SegaCdCdc::advance_decoder(uint64_t ticks, bool reading)
{
  auto &r = m_registers;
  r.decoder_clock_acc += ticks * kSegaCdSectorsPerSecond;
  while (r.decoder_clock_acc >= m_master_frequency) {
    r.decoder_clock_acc -= m_master_frequency;
    if (!reading && r.enabled && (r.control[0] & 0x80)) {
      r.header.fill(0);
      r.status = {0x80, 0, (uint8_t)(r.control[1] & 0x0c), 0};
      r.ifstat &= (uint8_t)~0x20;
      update_irq();
    }
  }
}

uint8_t SegaCdCdc::read_register()
{
  auto &r = m_registers;
  const unsigned index = r.index;
  if (index != 0)
    r.index = (r.index + 1) & 31;
  switch (index) {
  case 1:
    return r.ifstat;
  case 2:
    return (uint8_t)r.count;
  case 3:
    return (uint8_t)(r.count >> 8);
  case 4:
  case 5:
  case 6:
  case 7:
    return r.header[index - 4];
  case 8:
    return (uint8_t)r.pointer;
  case 9:
    return (uint8_t)(r.pointer >> 8);
  case 10:
    return (uint8_t)r.write_address;
  case 11:
    return (uint8_t)(r.write_address >> 8);
  case 12:
  case 13:
  case 14:
    return r.status[index - 12];
  case 15: {
    const uint8_t status = r.status[3];
    r.status[3] = 0x80;
    r.ifstat |= 0x20;
    update_irq();
    return status;
  }
  default:
    return 0xff;
  }
}

void SegaCdCdc::write_register(uint8_t data)
{
  auto &r = m_registers;
  r.enabled = true;
  const unsigned index = r.index;
  if (index != 0)
    r.index = (r.index + 1) & 31;
  auto low = [](uint16_t &value, uint8_t byte) {
    value = (value & 0xff00) | byte;
  };
  auto high = [](uint16_t &value, uint8_t byte) {
    value = (uint16_t)((value & 0xff) | (uint16_t)byte << 8);
  };
  switch (index) {
  case 1:
    r.ifctrl = data;
    if (!(data & 2)) {
      r.transferring = false;
      r.ifstat |= 0x0a;
    }
    break;
  case 2:
    low(r.count, data);
    break;
  case 3:
    high(r.count, data & 15);
    break;
  case 4:
    low(r.source, data);
    break;
  case 5:
    high(r.source, data);
    break;
  case 6:
    if ((r.ifctrl & 2) && (r.mode & 7) >= 2 && (r.mode & 7) != 6) {
      r.transferring = true;
      r.count &= 0xfff;
      r.ifstat &= (uint8_t)~0x0a;
      r.mode = (r.mode & 7) | ((r.mode & 7) < 4 ? 0x40 : 0);
      m_clock_acc = 0;
    }
    break;
  case 7:
    r.ifstat |= 0x40;
    r.count &= 0xfff;
    break;
  case 8:
    low(r.write_address, data);
    break;
  case 9:
    high(r.write_address, data);
    break;
  case 10:
    r.control[0] = data;
    if (!(data & 0x80))
      r.ifstat |= 0x20;
    break;
  case 11:
    r.control[1] = data;
    break;
  case 12:
    low(r.pointer, data);
    break;
  case 13:
    high(r.pointer, data);
    break;
  case 15:
    r = {};
    r.enabled = true;
    break;
  default:
    break;
  }
  update_irq();
}

void SegaCdCdc::finish_transfer()
{
  auto &r = m_registers;
  r.transferring = false;
  r.count = 0xffff;
  r.ifstat = (r.ifstat | 0x0a) & (uint8_t)~0x40;
  r.mode = (r.mode & 7) | 0x80;
  m_clock_acc = 0;
  update_irq();
}

uint8_t SegaCdCdc::transfer_byte()
{
  auto &r = m_registers;
  const uint8_t byte = m_buffer[r.source++ & (kBufferSize - 1)];
  if (r.count-- == 0)
    finish_transfer();
  return byte;
}

uint16_t SegaCdCdc::read_host(bool main)
{
  if (!m_registers.transferring || (m_registers.mode & 7) != (main ? 2 : 3))
    return 0xffff;
  const uint16_t upper = (uint16_t)transfer_byte() << 8;
  return upper | (m_registers.transferring ? transfer_byte() : 0xff);
}

SegaCdCdc::SavedState SegaCdCdc::save() const
{
  return SavedState{m_buffer,        m_read_pos,   m_write_pos,
                    m_used,          m_clock_acc,  m_dma_active,
                    m_irq_pending,   m_dma_target, m_dma_destination,
                    m_dma_remaining, m_registers};
}

bool SegaCdCdc::valid_saved_state(const SavedState &state) const
{
  const uint32_t target = (uint8_t)state.dma_target;
  const uint32_t capacity = target == 1   ? 0x80000
                            : target == 4 ? 0x10000
                                          : 0x40000;
  if (state.dma_destination > capacity ||
      state.dma_remaining > capacity - state.dma_destination)
    return false;
  return state.read_pos < kBufferSize && state.write_pos < kBufferSize &&
         state.used <= kBufferSize && target >= 1 && target <= 4 &&
         state.registers.decoder_clock_acc < m_master_frequency &&
         state.registers.index < 32 && (state.registers.mode & 0x38) == 0 &&
         (!state.registers.transferring || (state.registers.mode & 7) >= 2) &&
         (!state.dma_active || state.dma_remaining <= state.used);
}

bool SegaCdCdc::restore(const SavedState &state)
{
  if (!valid_saved_state(state)) {
    return false;
  }
  m_registers = state.registers;
  m_buffer = state.buffer;
  m_read_pos = state.read_pos;
  m_write_pos = state.write_pos;
  m_used = state.used;
  m_clock_acc = state.clock_acc;
  m_dma_active = state.dma_active;
  m_irq_pending = state.irq_pending;
  m_dma_target = state.dma_target;
  m_dma_destination = state.dma_destination;
  m_dma_remaining = state.dma_remaining;
  return true;
}

}  // namespace generator
