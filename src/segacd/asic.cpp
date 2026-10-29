/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "asic.hpp"

#include "sub_cpu.hpp"

namespace generator {

SegaCdAsic::SegaCdAsic(uint64_t master_frequency)
    : m_master_frequency(master_frequency == 0
                             ? SegaCdSubCpu::kDefaultMasterFrequency
                             : master_frequency)
{
}

void SegaCdAsic::reset()
{
  m_config = {};
  m_clock_acc = 0;
  m_pixels_remaining = 0;
  m_pixel_index = 0;
  m_busy = false;
  m_irq_pending = false;
}

std::expected<void, std::string>
SegaCdAsic::start(const SegaCdAsicConfig &config)
{
  if (m_busy) {
    return std::unexpected("graphics ASIC is already active");
  }
  if (config.source_width == 0 || config.source_height == 0 ||
      config.destination_width == 0 || config.destination_height == 0 ||
      config.scale_16_16 == 0 || config.angle % 90 != 0) {
    return std::unexpected("invalid graphics ASIC dimensions, scale, or angle");
  }
  const uint64_t source_size =
      (uint64_t)config.source_width * config.source_height;
  const uint64_t destination_size =
      (uint64_t)config.destination_width * config.destination_height;
  if (config.source.size() < source_size ||
      config.destination.size() < destination_size ||
      config.destination_offset >
          config.destination.size() - destination_size ||
      destination_size > 0xFFFFFFFFULL - config.destination_offset) {
    return std::unexpected("graphics ASIC buffer is too small");
  }
  m_config = config;
  m_pixels_remaining = destination_size;
  m_pixel_index = 0;
  m_clock_acc = 0;
  m_busy = true;
  m_irq_pending = false;
  return {};
}

uint8_t SegaCdAsic::render_pixel(uint32_t x, uint32_t y) const
{
  int64_t source_x = 0;
  int64_t source_y = 0;
  switch ((m_config.angle / 90) % 4) {
  case 0:
    source_x = x;
    source_y = y;
    break;
  case 1:
    source_x = (int64_t)m_config.destination_height - 1 - y;
    source_y = x;
    break;
  case 2:
    source_x = (int64_t)m_config.destination_width - 1 - x;
    source_y = (int64_t)m_config.destination_height - 1 - y;
    break;
  default:
    source_x = y;
    source_y = (int64_t)m_config.destination_width - 1 - x;
    break;
  }
  source_x = (source_x << 16) / m_config.scale_16_16;
  source_y = (source_y << 16) / m_config.scale_16_16;
  if (source_x < 0 || source_y < 0 || source_x >= m_config.source_width ||
      source_y >= m_config.source_height) {
    return 0;
  }
  return m_config.source[source_y * m_config.source_width + source_x];
}

void SegaCdAsic::advance_mclk(uint64_t ticks)
{
  if (m_busy) {
    m_clock_acc += ticks * kFrequency;
  }
}

std::optional<SegaCdAsicPixel> SegaCdAsic::take_pixel()
{
  if (!m_busy || m_clock_acc < m_master_frequency) {
    return std::nullopt;
  }
  m_clock_acc -= m_master_frequency;
  const uint32_t x = (uint32_t)(m_pixel_index % m_config.destination_width);
  const uint32_t y = (uint32_t)(m_pixel_index / m_config.destination_width);
  const SegaCdAsicPixel pixel{
      (uint32_t)(m_config.destination_offset + m_pixel_index),
      render_pixel(x, y)};
  m_pixel_index++;
  m_pixels_remaining--;
  if (m_pixels_remaining == 0) {
    m_busy = false;
    m_irq_pending = true;
    m_clock_acc = 0;
  }
  return pixel;
}

void SegaCdAsic::acknowledge_interrupt()
{
  m_irq_pending = false;
}

SegaCdAsic::SavedState SegaCdAsic::save(std::span<const uint8_t> word_ram) const
{
  const auto offset = [&](const uint8_t *pointer) {
    const auto base = (uintptr_t)word_ram.data();
    const auto address = (uintptr_t)pointer;
    return address >= base && address <= base + word_ram.size()
               ? (uint32_t)(address - base)
               : 0;
  };
  return SavedState{offset(m_config.source.data()),
                    offset(m_config.destination.data()),
                    m_config.source_width,
                    m_config.source_height,
                    m_config.destination_width,
                    m_config.destination_height,
                    m_config.scale_16_16,
                    m_config.angle,
                    m_config.destination_offset,
                    m_clock_acc,
                    m_pixels_remaining,
                    m_pixel_index,
                    m_busy,
                    m_irq_pending};
}

bool SegaCdAsic::valid_saved_state(const SavedState &state,
                                   std::span<const uint8_t> word_ram) const
{
  if (state.clock_acc >= m_master_frequency || state.angle % 90 != 0 ||
      (!state.busy && state.pixels_remaining != 0)) {
    return false;
  }
  if (!state.busy) {
    return true;
  }
  const uint64_t source_size =
      (uint64_t)state.source_width * state.source_height;
  const uint64_t destination_size =
      (uint64_t)state.destination_width * state.destination_height;
  return state.source_width != 0 && state.source_height != 0 &&
         state.destination_width != 0 && state.destination_height != 0 &&
         state.scale_16_16 != 0 && state.pixels_remaining != 0 &&
         state.pixel_index + state.pixels_remaining == destination_size &&
         state.source_offset <= word_ram.size() &&
         source_size <= word_ram.size() - state.source_offset &&
         state.destination_buffer_offset <= word_ram.size() &&
         destination_size <=
             word_ram.size() - state.destination_buffer_offset &&
         state.destination_offset <= word_ram.size() -
                                         state.destination_buffer_offset -
                                         destination_size;
}

bool SegaCdAsic::restore(const SavedState &state, std::span<uint8_t> word_ram)
{
  if (!valid_saved_state(state, word_ram)) {
    return false;
  }
  m_config = {};
  m_config.source = word_ram.subspan(state.source_offset);
  m_config.destination = word_ram.subspan(state.destination_buffer_offset);
  m_config.source_width = state.source_width;
  m_config.source_height = state.source_height;
  m_config.destination_width = state.destination_width;
  m_config.destination_height = state.destination_height;
  m_config.scale_16_16 = state.scale_16_16;
  m_config.angle = state.angle;
  m_config.destination_offset = state.destination_offset;
  m_clock_acc = state.clock_acc;
  m_pixels_remaining = state.pixels_remaining;
  m_pixel_index = state.pixel_index;
  m_busy = state.busy;
  m_irq_pending = state.irq_pending;
  return true;
}

}  // namespace generator
