/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "asic.hpp"

#include "sub_cpu.hpp"

#include <algorithm>

namespace generator {

SegaCdAsic::SegaCdAsic(uint64_t master_frequency)
    : m_master_frequency(master_frequency == 0
                             ? SegaCdSubCpu::kDefaultMasterFrequency
                             : master_frequency)
{
}

void SegaCdAsic::reset()
{
  m_stamp = {};
  m_stamp_ram = {};
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
  if (m_stamp.busy) {
    m_stamp.clock_acc += ticks * kFrequency;
    while (m_stamp.busy && m_stamp.clock_acc >= m_master_frequency * 5) {
      m_stamp.clock_acc -= m_master_frequency * 5;
      render_stamp_pixel();
    }
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

void SegaCdAsic::start_stamp(std::span<uint8_t> word_ram,
                             std::span<const uint8_t> registers,
                             uint8_t priority)
{
  if (word_ram.size() != 0x40000 || registers.size() != 16)
    return;
  m_stamp = {};
  std::copy(registers.begin(), registers.end(), m_stamp.registers.begin());
  m_stamp.priority = priority & 3;
  m_stamp_ram = word_ram;
  const uint32_t width = ((uint32_t)registers[10] << 8 | registers[11]) & 511;
  m_stamp.busy = width && registers[13];
  m_irq_pending = false;
}

uint16_t SegaCdAsic::stamp_lines_remaining() const
{
  const auto &r = m_stamp.registers;
  const uint32_t width = ((uint32_t)r[10] << 8 | r[11]) & 511;
  return m_stamp.busy && width ? (uint16_t)(r[13] - m_stamp.pixel / width) : 0;
}

void SegaCdAsic::render_stamp_pixel()
{
  const auto &r = m_stamp.registers;
  auto reg = [&](unsigned index) {
    return (uint32_t)r[index] << 8 | r[index + 1];
  };
  auto word = [&](uint32_t address) {
    address &= 0x3fffe;
    return (uint16_t)((uint16_t)m_stamp_ram[address] << 8 |
                      m_stamp_ram[address + 1]);
  };
  const uint32_t width = reg(10) & 511;
  const uint32_t x = m_stamp.pixel % width;
  const uint32_t y = m_stamp.pixel / width;
  const uint32_t trace = ((reg(14) << 2) & 0x3fff8) + y * 8;
  uint32_t sx = ((uint32_t)word(trace) << 8) +
                (uint32_t)((int16_t)word(trace + 4) * (int32_t)x);
  uint32_t sy = ((uint32_t)word(trace + 2) << 8) +
                (uint32_t)((int16_t)word(trace + 6) * (int32_t)x);
  const unsigned side = (r[1] & 2) ? 32 : 16;
  const unsigned map_side = (r[1] & 4) ? 4096 : 256;
  const unsigned map_cells = map_side / side;
  sx = (sx & 0xffffff) >> 11;
  sy = (sy & 0xffffff) >> 11;
  if (r[1] & 1) {
    sx &= map_side - 1;
    sy &= map_side - 1;
  }
  uint8_t pixel = 0;
  if (sx < map_side && sy < map_side) {
    const uint32_t map_mask = (r[1] & 4) ? ((r[1] & 2) ? 0x38000 : 0x20000)
                                         : ((r[1] & 2) ? 0x3ff80 : 0x3fe00);
    const uint16_t entry = word(((reg(2) << 2) & map_mask) +
                                ((sy / side) * map_cells + sx / side) * 2);
    const uint32_t stamp = entry & ((r[1] & 2) ? 0x7fc : 0x7ff);
    if (stamp) {
      unsigned px = sx % side, py = sy % side;
      if (entry & 0x8000)
        px = side - 1 - px;
      if (entry & 0x4000) {
        px = side - 1 - px;
        py = side - 1 - py;
      }
      if (entry & 0x2000) {
        const auto old_x = px;
        px = side - 1 - py;
        py = old_x;
      }
      const uint32_t dot = stamp * 256 + ((px / 8) * (side / 8) + py / 8) * 64 +
                           (py % 8) * 8 + px % 8;
      pixel = (m_stamp_ram[(dot >> 1) & 0x3ffff] >> ((dot & 1) ? 0 : 4)) & 15;
    }
  }
  const uint32_t start = ((reg(6) << 3) & 0x7ffc0) + (r[9] & 63);
  const uint32_t column_stride = ((r[5] & 31) + 1) * 64;
  const uint32_t dot =
      (start + y * 8 + x + (((start & 7) + x) / 8) * (column_stride - 8)) &
      0x7ffff;
  auto &byte = m_stamp_ram[dot >> 1];
  const unsigned shift = (dot & 1) ? 0 : 4;
  const uint8_t old = (byte >> shift) & 15;
  if (m_stamp.priority == 0 || (m_stamp.priority == 1 && old == 0) ||
      (m_stamp.priority == 2 && pixel != 0))
    byte = (uint8_t)((byte & ~(15 << shift)) | pixel << shift);
  if (++m_stamp.pixel == width * r[13]) {
    m_stamp.busy = false;
    m_stamp.clock_acc = 0;
    m_irq_pending = true;
  }
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
                    m_irq_pending,
                    m_stamp};
}

bool SegaCdAsic::valid_saved_state(const SavedState &state,
                                   std::span<const uint8_t> word_ram) const
{
  const auto &r = state.stamp.registers;
  const uint32_t width = ((uint32_t)r[10] << 8 | r[11]) & 511;
  const uint32_t height = r[13];
  if (state.stamp.priority > 3 ||
      state.stamp.clock_acc >= m_master_frequency * 5 ||
      (state.stamp.busy &&
       (!width || !height || state.stamp.pixel >= width * height)))
    return false;
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
  m_stamp = state.stamp;
  m_stamp_ram = word_ram;
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
