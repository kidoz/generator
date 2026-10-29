/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sms_vdp.hpp"

#include <algorithm>
#include <cstring>

namespace generator {

void SmsVdp::reset(bool pal)
{
  std::memset(m_reg, 0, sizeof(m_reg));
  std::memset(m_vram, 0, sizeof(m_vram));
  std::memset(m_cram, 0, sizeof(m_cram));
  m_reg[2] = 0x0E;
  m_reg[5] = 0x7E;
  m_reg[6] = 0x04;
  m_reg[8] = 0;
  m_reg[9] = 0;
  m_reg[10] = 0xFF;
  m_address = 0;
  m_code = 0;
  m_latch = false;
  m_read_buffer = 0;
  m_status = 0;
  m_pal = pal;
  m_mclk = 0;
  m_line = 0;
  m_frames = 0;
  m_line_counter = 0xFF;
  m_line_pending = false;
  m_frame_pending = false;
  m_hcounter_latch = 0;
  m_frame.fill(0);
}

uint32_t SmsVdp::video_mode() const
{
  return ((m_reg[0] & 0x04) != 0 ? 8 : 0) | ((m_reg[1] & 0x08) != 0 ? 4 : 0) |
         ((m_reg[0] & 0x02) != 0 ? 2 : 0) | ((m_reg[1] & 0x10) != 0 ? 1 : 0);
}

uint32_t SmsVdp::visible_lines() const
{
  switch (video_mode()) {
  case 0x0B:
    return 224;
  case 0x0E:
    return 240;
  default:
    return 192;
  }
}

uint64_t SmsVdp::frame_index() const
{
  return m_frames;
}

const uint32_t *SmsVdp::line_pixels(uint32_t line) const
{
  return m_frame.data() + (size_t)line * kWidth;
}

uint32_t SmsVdp::rgba(uint8_t index) const
{
  const uint8_t color = m_cram[index & 31];
  const uint32_t red = color & 3;
  const uint32_t green = (color >> 2) & 3;
  const uint32_t blue = (color >> 4) & 3;
  return 0xFF000000U | (red * 85 << 16) | (green * 85 << 8) | blue * 85;
}

uint32_t SmsVdp::background_pixel(uint32_t line, uint32_t x, bool &opaque,
                                  bool &priority) const
{
  opaque = false;
  priority = false;
  if ((m_reg[0] & 0x04) == 0) {
    return 0;
  }

  const bool hscroll_locked = (m_reg[0] & 0x40) != 0 && line < 16;
  const bool vscroll_locked = (m_reg[0] & 0x80) != 0 && x >= 192;
  const uint32_t sx = (x - (hscroll_locked ? 0 : m_reg[8])) & 0xFF;
  const uint32_t sy = (line + (vscroll_locked ? 0 : m_reg[9])) % 224;
  const uint32_t tile_x = sx / 8;
  const uint32_t tile_y = sy / 8;
  const uint32_t name_address =
      ((m_reg[2] & 0x0E) << 10) + tile_y * 64 + tile_x * 2;
  const uint16_t entry = (uint16_t)(m_vram[(name_address + 1) & 0x3FFF] << 8 |
                                    m_vram[name_address & 0x3FFF]);
  const bool hflip = (entry & 0x0200) != 0;
  const bool vflip = (entry & 0x0400) != 0;
  priority = (entry & 0x1000) != 0;

  const uint32_t px = hflip ? 7 - sx % 8 : sx % 8;
  const uint32_t py = vflip ? 7 - sy % 8 : sy % 8;
  const uint32_t pattern = (entry & 0x01FF) * 32 + py * 4;
  const uint8_t bit = (uint8_t)(0x80 >> px);
  const uint8_t color = (uint8_t)(((m_vram[pattern] & bit) ? 1 : 0) |
                                  ((m_vram[pattern + 1] & bit) ? 2 : 0) |
                                  ((m_vram[pattern + 2] & bit) ? 4 : 0) |
                                  ((m_vram[pattern + 3] & bit) ? 8 : 0));
  opaque = color != 0;
  return (entry & 0x0800 ? 16 : 0) | color;
}

void SmsVdp::render_line(uint32_t line)
{
  uint32_t *out = m_frame.data() + (size_t)line * kWidth;
  const uint32_t backdrop = rgba(16 + (m_reg[7] & 0x0F));
  if ((m_reg[1] & 0x40) == 0) {
    std::fill_n(out, kWidth, backdrop);
    return;
  }

  bool bg_opaque[kWidth]{};
  bool bg_priority[kWidth]{};
  uint8_t bg_color[kWidth]{};
  for (uint32_t x = 0; x < kWidth; x++) {
    bg_color[x] =
        (uint8_t)background_pixel(line, x, bg_opaque[x], bg_priority[x]);
    out[x] = bg_opaque[x] ? rgba(bg_color[x]) : backdrop;
  }

  if ((m_reg[0] & 0x04) != 0) {
    const uint16_t sat = (m_reg[5] & 0x7E) << 7;
    const uint16_t pattern_base = (m_reg[6] & 0x04) << 11;
    const bool tall = (m_reg[1] & 0x02) != 0;
    const bool zoom = (m_reg[1] & 0x01) != 0;
    const uint32_t sprite_height = (tall ? 16 : 8) << (zoom ? 1 : 0);
    int sprites = 0;
    bool overflow = false;

    for (uint32_t index = 0; index < 64 && sprites < 8; index++) {
      const uint8_t y_code = m_vram[(sat + index) & 0x3FFF];
      if (visible_lines() == 192 && y_code == 0xD0) {
        break;
      }
      const int y = (int)y_code + 1;
      if (y > (int)line || y + (int)sprite_height <= (int)line) {
        continue;
      }
      if (sprites == 7) {
        overflow = true;
      }
      sprites++;

      const uint16_t pair = sat + 128 + index * 2;
      const uint32_t x = m_vram[pair & 0x3FFF];
      uint16_t pattern = m_vram[(pair + 1) & 0x3FFF];
      if (tall) {
        pattern &= 0xFE;
      }
      const uint32_t row = ((int)line - y) >> (zoom ? 1 : 0);
      for (uint32_t dx = 0; dx < 8; dx++) {
        const uint32_t screen_x = x + (dx << (zoom ? 1 : 0));
        if (screen_x >= kWidth) {
          continue;
        }
        const uint8_t bit = (uint8_t)(0x80 >> dx);
        const uint32_t address = pattern_base + pattern * 32 + row * 4;
        const uint8_t color = (uint8_t)(((m_vram[address] & bit) ? 1 : 0) |
                                        ((m_vram[address + 1] & bit) ? 2 : 0) |
                                        ((m_vram[address + 2] & bit) ? 4 : 0) |
                                        ((m_vram[address + 3] & bit) ? 8 : 0));
        if (color == 0) {
          continue;
        }
        if (bg_opaque[screen_x]) {
          m_status |= 0x20;
        }
        if (!bg_priority[screen_x] || !bg_opaque[screen_x]) {
          out[screen_x] = rgba(16 + color);
          if (zoom && screen_x + 1 < kWidth) {
            out[screen_x + 1] = rgba(16 + color);
          }
        }
      }
    }
    if (overflow) {
      m_status |= 0x40;
    }
  }

  if ((m_reg[0] & 0x20) != 0) {
    std::fill_n(out, 8, backdrop);
  }
}

void SmsVdp::service_line_events(uint32_t line)
{
  if (line <= visible_lines()) {
    if (m_line_counter == 0) {
      m_line_pending = true;
      m_line_counter = m_reg[10];
    } else {
      m_line_counter--;
    }
  } else {
    m_line_counter = m_reg[10];
  }
  if (line == visible_lines() + 1) {
    m_frame_pending = true;
    m_status |= 0x80;
  }
}

void SmsVdp::advance_mclk(uint64_t ticks)
{
  const uint32_t total_lines = m_pal ? 313 : 262;
  while (ticks != 0) {
    const uint64_t chunk = std::min(ticks, kMclkPerLine - m_mclk);
    m_mclk += chunk;
    ticks -= chunk;
    if (m_mclk != kMclkPerLine) {
      continue;
    }
    m_mclk = 0;
    if (m_line < visible_lines()) {
      render_line(m_line);
    }
    service_line_events(m_line);
    m_line++;
    if (m_line == total_lines) {
      m_line = 0;
      m_frames++;
    }
  }
}

uint8_t SmsVdp::read_data()
{
  m_latch = false;
  const uint8_t value = m_read_buffer;
  m_read_buffer = m_vram[m_address & 0x3FFF];
  m_address = (m_address + 1) & 0x3FFF;
  return value;
}

uint8_t SmsVdp::read_control()
{
  m_latch = false;
  const uint8_t value = m_status;
  m_status = 0;
  m_line_pending = false;
  m_frame_pending = false;
  return value;
}

uint8_t SmsVdp::read_vcounter() const
{
  if (m_pal) {
    return m_line <= 0xF2 ? (uint8_t)m_line : (uint8_t)(m_line - 57);
  }
  return m_line <= 0xDA ? (uint8_t)m_line : (uint8_t)(m_line - 6);
}

uint8_t SmsVdp::read_hcounter() const
{
  return m_hcounter_latch;
}

void SmsVdp::write_data(uint8_t value)
{
  m_latch = false;
  m_read_buffer = value;
  if (m_code == 3) {
    m_cram[m_address & 31] = value & 0x3F;
  } else {
    m_vram[m_address & 0x3FFF] = value;
  }
  m_address = (m_address + 1) & 0x3FFF;
}

void SmsVdp::write_control(uint8_t value)
{
  if (!m_latch) {
    m_address = (m_address & 0x3F00) | value;
    m_latch = true;
    return;
  }

  m_latch = false;
  m_address = (m_address & 0x00FF) | ((value & 0x3F) << 8);
  m_code = value >> 6;
  if (m_code == 0) {
    m_read_buffer = m_vram[m_address & 0x3FFF];
    m_address = (m_address + 1) & 0x3FFF;
  } else if (m_code == 2 && (value & 0x0F) < 16) {
    m_reg[value & 0x0F] = m_address & 0xFF;
  }
}

void SmsVdp::latch_hcounter()
{
  const int clock = (int)m_mclk / 5 - 94;
  m_hcounter_latch = (uint8_t)(clock / 4);
}

bool SmsVdp::irq() const
{
  return (m_frame_pending && (m_reg[1] & 0x20) != 0) ||
         (m_line_pending && (m_reg[0] & 0x10) != 0);
}

SmsVdp::SavedState SmsVdp::save() const
{
  SavedState state{};
  std::copy_n(m_reg, 16, state.reg);
  std::copy_n(m_vram, 0x4000, state.vram);
  std::copy_n(m_cram, 32, state.cram);
  state.address = m_address;
  state.code = m_code;
  state.latch = m_latch ? 1 : 0;
  state.read_buffer = m_read_buffer;
  state.status = m_status;
  state.pal = m_pal ? 1 : 0;
  state.mclk = m_mclk;
  state.line = m_line;
  state.frames = m_frames;
  state.line_counter = m_line_counter;
  state.line_pending = m_line_pending ? 1 : 0;
  state.frame_pending = m_frame_pending ? 1 : 0;
  state.hcounter_latch = m_hcounter_latch;
  return state;
}

bool SmsVdp::valid_saved_state(const SavedState &state)
{
  return state.address < 0x4000 && state.code < 4 && state.latch <= 1 &&
         state.pal <= 1 && state.mclk < kMclkPerLine &&
         state.line < (state.pal ? 313u : 262u) && state.line_pending <= 1 &&
         state.frame_pending <= 1;
}

void SmsVdp::restore(const SavedState &state)
{
  std::copy_n(state.reg, 16, m_reg);
  std::copy_n(state.vram, 0x4000, m_vram);
  std::copy_n(state.cram, 32, m_cram);
  m_address = state.address;
  m_code = state.code;
  m_latch = state.latch != 0;
  m_read_buffer = state.read_buffer;
  m_status = state.status;
  m_pal = state.pal != 0;
  m_mclk = state.mclk;
  m_line = state.line;
  m_frames = state.frames;
  m_line_counter = state.line_counter;
  m_line_pending = state.line_pending != 0;
  m_frame_pending = state.frame_pending != 0;
  m_hcounter_latch = state.hcounter_latch;
  m_frame.fill(0);
  for (uint32_t line = 0; line < visible_lines(); line++) {
    render_line(line);
  }
}

}  // namespace generator
