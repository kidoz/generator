/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace generator {

class X32Video {
public:
  struct SavedState {
    std::array<std::array<uint8_t, 0x20000>, 2> framebuffers{};
    std::array<uint8_t, 512> palette{};
    uint16_t bitmap_mode = 0x8000;
    uint16_t bitmap_mode_pending = 0x8000;
    uint16_t shift = 0;
    uint16_t shift_pending = 0;
    uint16_t fill_length = 0;
    uint16_t fill_address = 0;
    uint16_t fill_data = 0;
    uint32_t fill_remaining = 0;
    uint8_t fill_startup = 0;
    uint8_t fill_subcycle = 0;
    bool display_buffer = false;
    bool pending_buffer = false;
    bool vblank = false;
    bool hblank = false;
    bool bitmap_latched = false;
  };

  static constexpr size_t kWidth = 320;
  static constexpr size_t kFramebufferSize = 0x20000;
  static constexpr uint64_t kHblankStartLineMclk = 2560;
  static constexpr uint64_t kBitmapLatchHblankMclk = 668;

  void power_on();
  void set_pal(bool pal);
  void set_blanking(bool vblank, bool hblank, uint64_t hblank_mclk);
  void advance_mclk(uint64_t ticks);

  uint16_t read_register(uint32_t offset) const;
  void write_register(uint32_t offset, uint16_t value);
  uint8_t read_register_byte(uint32_t offset) const;
  void write_register_byte(uint32_t offset, uint8_t value);
  uint16_t read_palette(uint32_t offset) const;
  void write_palette(uint32_t offset, uint16_t value);
  uint8_t read_palette_byte(uint32_t offset) const;
  void write_palette_byte(uint32_t offset, uint8_t value);
  uint8_t read_framebuffer(uint32_t offset) const;
  void write_framebuffer(uint32_t offset, uint8_t value);

  bool blank() const
  {
    return (m_bitmap_mode & 3) == 0;
  }
  void composite_line(uint32_t line, std::span<const uint8_t> md_pixels,
                      const uint16_t *md_cram,
                      std::span<uint32_t> output) const;
  SavedState save() const
  {
    return {m_framebuffers,   m_palette,
            m_bitmap_mode,    m_bitmap_mode_pending,
            m_shift,          m_shift_pending,
            m_fill_length,    m_fill_address,
            m_fill_data,      m_fill_remaining,
            m_fill_startup,   m_fill_subcycle,
            m_display_buffer, m_pending_buffer,
            m_vblank,         m_hblank,
            m_bitmap_latched};
  }
  void restore(const SavedState &state)
  {
    m_framebuffers = state.framebuffers;
    m_palette = state.palette;
    m_bitmap_mode = state.bitmap_mode;
    m_bitmap_mode_pending = state.bitmap_mode_pending;
    m_shift = state.shift;
    m_shift_pending = state.shift_pending;
    m_fill_length = state.fill_length;
    m_fill_address = state.fill_address;
    m_fill_data = state.fill_data;
    m_fill_remaining = state.fill_remaining;
    m_fill_startup = state.fill_startup;
    m_fill_subcycle = state.fill_subcycle;
    m_display_buffer = state.display_buffer;
    m_pending_buffer = state.pending_buffer;
    m_vblank = state.vblank;
    m_hblank = state.hblank;
    m_bitmap_latched = state.bitmap_latched;
  }
  static bool valid_saved_state(const SavedState &state)
  {
    return state.fill_remaining <= 256 && state.fill_startup <= 7 &&
           state.fill_subcycle <= 2;
  }

private:
  struct Pixel {
    uint16_t color = 0;
    bool opaque = false;
  };

  static uint32_t rgba15(uint16_t color);
  static uint32_t md_rgba(uint16_t color);
  void start_fill();
  uint16_t framebuffer_word(bool display, uint32_t offset) const;
  uint16_t palette_word(uint32_t offset) const;

  std::array<std::array<uint8_t, kFramebufferSize>, 2> m_framebuffers{};
  std::array<uint8_t, 512> m_palette{};
  uint16_t m_bitmap_mode = 0x8000;
  uint16_t m_bitmap_mode_pending = 0x8000;
  uint16_t m_shift = 0;
  uint16_t m_shift_pending = 0;
  uint16_t m_fill_length = 0;
  uint16_t m_fill_address = 0;
  uint16_t m_fill_data = 0;
  uint32_t m_fill_remaining = 0;
  uint8_t m_fill_startup = 0;
  uint8_t m_fill_subcycle = 0;
  bool m_display_buffer = false;
  bool m_pending_buffer = false;
  bool m_vblank = false;
  bool m_hblank = false;
  bool m_bitmap_latched = false;
};

}  // namespace generator
