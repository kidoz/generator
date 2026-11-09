/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "x32_video.hpp"

#include <algorithm>

namespace generator {

void X32Video::power_on()
{
  for (auto &framebuffer : m_framebuffers)
    framebuffer.fill(0);
  m_palette.fill(0);
  m_bitmap_mode = 0x8000;
  m_bitmap_mode_pending = 0x8000;
  m_shift = 0;
  m_shift_pending = 0;
  m_fill_length = 0;
  m_fill_address = 0;
  m_fill_data = 0;
  m_fill_remaining = 0;
  m_fill_startup = 0;
  m_fill_subcycle = 0;
  m_display_buffer = false;
  m_pending_buffer = false;
  m_vblank = false;
  m_hblank = false;
  m_bitmap_latched = false;
}

void X32Video::advance_mclk(uint64_t ticks)
{
  while (ticks != 0 && m_fill_remaining != 0) {
    if (m_fill_startup != 0) {
      const uint64_t consume = std::min<uint64_t>(ticks, m_fill_startup);
      m_fill_startup -= consume;
      ticks -= consume;
      continue;
    }
    const uint64_t needed = 3 - m_fill_subcycle;
    const uint64_t consume = std::min(ticks, needed);
    m_fill_subcycle += consume;
    ticks -= consume;
    if (m_fill_subcycle != 3)
      continue;
    m_fill_subcycle = 0;
    const uint32_t offset = (uint32_t)m_fill_address * 2;
    auto &framebuffer = m_framebuffers[!m_display_buffer];
    framebuffer[offset] = m_fill_data >> 8;
    framebuffer[offset + 1] = m_fill_data;
    m_fill_address = (m_fill_address & 0xFE00) | ((m_fill_address + 1) & 0x1FF);
    m_fill_remaining--;
  }
}

void X32Video::set_pal(bool pal)
{
  m_bitmap_mode = (m_bitmap_mode & 0x7FFF) | (pal ? 0 : 0x8000);
  m_bitmap_mode_pending = (m_bitmap_mode_pending & 0x7FFF) | (pal ? 0 : 0x8000);
}

void X32Video::set_blanking(bool vblank, bool hblank, uint64_t hblank_mclk)
{
  if (!hblank) {
    m_bitmap_latched = false;
  } else if (!m_bitmap_latched && hblank_mclk >= kBitmapLatchHblankMclk) {
    m_bitmap_mode = m_bitmap_mode_pending;
    m_shift = m_shift_pending;
    m_bitmap_latched = true;
  }
  m_vblank = vblank;
  m_hblank = hblank;
  if (m_vblank || blank())
    m_display_buffer = m_pending_buffer;
}

uint16_t X32Video::read_register(uint32_t offset) const
{
  switch (offset & ~1u) {
  case 0:
    return m_bitmap_mode_pending;
  case 2:
    return m_shift_pending;
  case 4:
    return m_fill_length;
  case 6:
    return m_fill_address;
  case 8:
    return m_fill_data;
  case 10: {
    const bool palette_access =
        m_vblank || m_hblank || blank() || (m_bitmap_mode & 3) == 2;
    return (m_vblank ? 0x8000 : 0) | (m_hblank ? 0x4000 : 0) |
           (palette_access ? 0x2000 : 0) | (m_fill_remaining != 0 ? 2 : 0) |
           (m_display_buffer ? 1 : 0);
  }
  default:
    return 0;
  }
}

void X32Video::write_register(uint32_t offset, uint16_t value)
{
  switch (offset & ~1u) {
  case 0:
    m_bitmap_mode_pending = (m_bitmap_mode_pending & 0x8000) | (value & 0x2083);
    break;
  case 2:
    m_shift_pending = value & 1;
    break;
  case 4:
    m_fill_length = value & 0xFF;
    break;
  case 6:
    m_fill_address = value;
    break;
  case 8:
    m_fill_data = value;
    start_fill();
    break;
  case 10:
    m_pending_buffer = (value & 1) != 0;
    if (m_vblank || blank())
      m_display_buffer = m_pending_buffer;
    break;
  default:
    break;
  }
}

uint8_t X32Video::read_register_byte(uint32_t offset) const
{
  const uint16_t value = read_register(offset);
  return (offset & 1) ? value : value >> 8;
}

void X32Video::write_register_byte(uint32_t offset, uint8_t value)
{
  const uint32_t aligned = offset & ~1u;
  if (aligned == 8) {
    if (offset & 1) {
      m_fill_data = (m_fill_data & 0xFF00) | value;
      start_fill();
    } else {
      m_fill_data = (m_fill_data & 0x00FF) | (uint16_t)value << 8;
    }
    return;
  }
  uint16_t word = read_register(aligned);
  if (offset & 1)
    word = (word & 0xFF00) | value;
  else
    word = (word & 0x00FF) | (uint16_t)value << 8;
  write_register(aligned, word);
}

uint16_t X32Video::palette_word(uint32_t offset) const
{
  const uint32_t index = offset & 0x1FE;
  return (uint16_t)m_palette[index] << 8 | m_palette[index + 1];
}

uint16_t X32Video::read_palette(uint32_t offset) const
{
  return palette_word(offset);
}

void X32Video::write_palette(uint32_t offset, uint16_t value)
{
  const uint32_t index = offset & 0x1FE;
  m_palette[index] = value >> 8;
  m_palette[index + 1] = value;
}

uint8_t X32Video::read_palette_byte(uint32_t offset) const
{
  return m_palette[offset & 0x1FF];
}

void X32Video::write_palette_byte(uint32_t offset, uint8_t value)
{
  m_palette[offset & 0x1FF] = value;
}

uint8_t X32Video::read_framebuffer(uint32_t offset) const
{
  if (m_fill_remaining != 0)
    return 0;
  return m_framebuffers[!m_display_buffer][offset & (kFramebufferSize - 1)];
}

void X32Video::write_framebuffer(uint32_t offset, uint8_t value)
{
  if (value != 0 && m_fill_remaining == 0)
    m_framebuffers[!m_display_buffer][offset & (kFramebufferSize - 1)] = value;
}

void X32Video::start_fill()
{
  m_fill_remaining = (m_fill_length & 0xFF) + 1;
  m_fill_startup = 7;
  m_fill_subcycle = 0;
}

uint16_t X32Video::framebuffer_word(bool display, uint32_t offset) const
{
  const auto &framebuffer = m_framebuffers[display];
  const uint32_t first = offset & (kFramebufferSize - 1);
  const uint32_t second = (offset + 1) & (kFramebufferSize - 1);
  return (uint16_t)framebuffer[first] << 8 | framebuffer[second];
}

uint32_t X32Video::rgba15(uint16_t color)
{
  const uint32_t red = color & 0x1F;
  const uint32_t green = (color >> 5) & 0x1F;
  const uint32_t blue = (color >> 10) & 0x1F;
  return 0xFF000000 | ((red << 3) | (red >> 2)) << 16 |
         ((green << 3) | (green >> 2)) << 8 | (blue << 3) | (blue >> 2);
}

uint32_t X32Video::md_rgba(uint16_t color)
{
  const uint32_t red = color & 0xE;
  const uint32_t green = (color >> 4) & 0xE;
  const uint32_t blue = (color >> 8) & 0xE;
  return 0xFF000000 | ((red << 4) | (red << 1) | (red >> 2)) << 16 |
         ((green << 4) | (green << 1) | (green >> 2)) << 8 | (blue << 4) |
         (blue << 1) | (blue >> 2);
}

void X32Video::composite_line(uint32_t line, std::span<const uint8_t> md_pixels,
                              const uint16_t *md_cram,
                              std::span<uint32_t> output) const
{
  const size_t count = std::min({md_pixels.size(), output.size(), kWidth});
  if (count == 0)
    return;
  for (size_t x = 0; x < count; x++)
    output[x] = md_rgba(md_cram[md_pixels[x] & 0x3F]);
  if (blank() || line >= 256)
    return;

  std::array<Pixel, kWidth> pixels{};
  const uint32_t base = (uint32_t)framebuffer_word(m_display_buffer, line * 2)
                        << 1;
  const uint32_t mode = m_bitmap_mode & 3;
  if (mode == 2) {
    for (size_t x = 0; x < kWidth; x++)
      pixels[x] = {framebuffer_word(m_display_buffer, base + x * 2), true};
  } else if (mode == 1) {
    const size_t shift = m_shift & 1;
    for (size_t x = 0; x < kWidth; x++) {
      const uint32_t offset = (base + x + shift) & (kFramebufferSize - 1);
      const uint8_t index = m_framebuffers[m_display_buffer][offset];
      pixels[x] = {palette_word((uint32_t)index * 2), true};
    }
  } else {
    size_t produced = 0;
    uint32_t offset = base;
    for (size_t runs = 0; runs < kFramebufferSize / 2 && produced < kWidth;
         runs++) {
      const uint16_t run = framebuffer_word(m_display_buffer, offset);
      const size_t length = run >> 8;
      if (length == 0)
        break;
      const Pixel pixel{palette_word((run & 0xFF) * 2), true};
      const size_t end = std::min(produced + length, kWidth);
      std::fill(pixels.begin() + produced, pixels.begin() + end, pixel);
      produced = end;
      offset += 2;
    }
  }

  const bool md_priority = (m_bitmap_mode & 0x2000) == 0;
  for (size_t x = 0; x < count; x++) {
    const Pixel x32 = pixels[x];
    const bool through = (x32.color & 0x8000) != 0;
    const bool md_visible = md_pixels[x] != 0 && (md_priority != through);
    if (!md_visible && x32.opaque)
      output[x] = rgba15(x32.color);
  }
}

}  // namespace generator
