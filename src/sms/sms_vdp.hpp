/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Master System 315-5124/315-5246 VDP ports, timing, and Mode 4 renderer. */

#pragma once

#include <array>
#include <cstdint>

namespace generator {

class SmsVdp {
public:
  static constexpr uint32_t kWidth = 256;
  static constexpr uint32_t kMaxVisibleLines = 240;
  static constexpr uint64_t kMclkPerLine = 3420;

  void reset(bool pal);
  void advance_mclk(uint64_t ticks);

  uint8_t read_data();
  uint8_t read_control();
  uint8_t read_vcounter() const;
  uint8_t read_hcounter() const;
  void write_data(uint8_t value);
  void write_control(uint8_t value);
  void latch_hcounter();

  bool irq() const;
  uint64_t frame_index() const;
  uint32_t visible_lines() const;
  const uint32_t *line_pixels(uint32_t line) const;

  struct SavedState {
    uint8_t reg[16];
    uint8_t vram[0x4000];
    uint8_t cram[32];
    uint16_t address;
    uint8_t code;
    uint8_t latch;
    uint8_t read_buffer;
    uint8_t status;
    uint8_t pal;
    uint64_t mclk;
    uint32_t line;
    uint64_t frames;
    uint8_t line_counter;
    uint8_t line_pending;
    uint8_t frame_pending;
    uint8_t hcounter_latch;
  };
  SavedState save() const;
  void restore(const SavedState &state);
  static bool valid_saved_state(const SavedState &state);

private:
  uint32_t video_mode() const;
  uint32_t rgba(uint8_t index) const;
  uint32_t background_pixel(uint32_t line, uint32_t x, bool &opaque,
                            bool &priority) const;
  void render_line(uint32_t line);
  void service_line_events(uint32_t line);

  uint8_t m_reg[16]{};
  uint8_t m_vram[0x4000]{};
  uint8_t m_cram[32]{};
  uint16_t m_address = 0;
  uint8_t m_code = 0;
  bool m_latch = false;
  uint8_t m_read_buffer = 0;
  uint8_t m_status = 0;

  bool m_pal = false;
  uint64_t m_mclk = 0;
  uint32_t m_line = 0;
  uint64_t m_frames = 0;
  uint8_t m_line_counter = 0xFF;
  bool m_line_pending = false;
  bool m_frame_pending = false;
  uint8_t m_hcounter_latch = 0;
  std::array<uint32_t, kWidth * kMaxVisibleLines> m_frame{};
};

}  // namespace generator
