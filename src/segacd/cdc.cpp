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
  if (m_dma_active) {
    return std::unexpected("CDC DMA is already active");
  }
  if (byte_count == 0 || byte_count > m_used) {
    return std::unexpected("CDC DMA length exceeds buffered data");
  }
  const uint32_t target_size = target == SegaCdCdcDmaTarget::ProgramRam
                                   ? SegaCdSubBus::kProgramRamSize
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
  if (m_dma_active) {
    m_clock_acc += ticks * kDmaFrequency;
  }
}

std::optional<SegaCdCdcTransfer> SegaCdCdc::take_dma_transfer()
{
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

SegaCdCdc::SavedState SegaCdCdc::save() const
{
  return SavedState{m_buffer,       m_read_pos,   m_write_pos,
                    m_used,         m_clock_acc,  m_dma_active,
                    m_irq_pending,  m_dma_target, m_dma_destination,
                    m_dma_remaining};
}

bool SegaCdCdc::valid_saved_state(const SavedState &state) const
{
  const uint32_t target = (uint8_t)state.dma_target;
  return state.read_pos < kBufferSize && state.write_pos < kBufferSize &&
         state.used <= kBufferSize && target >= 1 && target <= 3 &&
         (!state.dma_active || state.dma_remaining <= state.used);
}

bool SegaCdCdc::restore(const SavedState &state)
{
  if (!valid_saved_state(state)) {
    return false;
  }
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
