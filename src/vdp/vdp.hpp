/* SPDX-License-Identifier: GPL-2.0-or-later */
/* YM7101 VDP: register/data-port semantics, line-granularity event
 * timing and line-based rendering; the 4-slot FIFO and dot-accurate
 * event positions are not modelled yet. Registers latch, the address
 * counter walks, HV counts, and VINT/HINT reach the CPU on the IPL
 * lines.
 *
 * Port map within 0xC00000-0xDFFFFF (mirrored every 0x20):
 *   0x00-0x03 data, 0x04-0x07 control/status, 0x08-0x0F HV counter,
 *   0x10-0x17 PSG, 0x18-0x1F test. */

#pragma once

#include <cstdint>
#include <functional>

namespace generator {

class Vdp {
public:
  Vdp()
  {
    m_frame.assign((size_t)224 * kLineBufferWidth, 0);
  }
  void reset(bool pal);

  /* 68K-side port access (address already masked to 0x1F by the caller
   * or here). */
  uint16_t port_read(uint32_t addr);
  void port_write(uint32_t addr, uint16_t data);

  /* Advance by master clocks; dot rate follows the H32/H40 mode bit
   * (register 12 bit 0). Returns the interrupt level to assert on the
   * 68K IPL lines (7 = none). */
  int advance_mclk(uint64_t ticks);

  /* DMA: 68K-bus reads for memory-to-VDP transfers route through the
   * machine's bus (no direct ROM/RAM pointers). */
  void set_dma_reader(std::function<uint16_t(uint32_t)> reader)
  {
    m_dma_reader = std::move(reader);
  }

  /* Master clocks the 68K owes for completed DMA (the bus was stolen);
   * the machine drains this after the triggering instruction. */
  uint64_t take_dma_debt()
  {
    const uint64_t debt = m_dma_debt_mclk;
    m_dma_debt_mclk = 0;
    return debt;
  }
  bool dma_active() const
  {
    return m_dma_busy;
  }

  /* --- state --- */
  int ipl() const;

  /* The 68K's interrupt acknowledge releases the request line. VINT is
   * asserted for the whole of vertical blanking, so this is the only
   * thing that ends it within a field. */
  void acknowledge_int(int level);
  /* Dot on the vblank line at which the Z80 INT pulse is released. */
  static constexpr uint32_t kZintReleaseH40 = 322;
  static constexpr uint32_t kZintReleaseH32 = 258;

  /* Z80 INT line: the VDP pulses the sound CPU interrupt once per field.
   * The pulse is asserted at the start of the first vblank line and
   * released partway through that same line — a fixed width that, unlike
   * the 68K's VINT, is neither gated by the VINT enable bit nor cleared
   * by a status read or an interrupt acknowledge. Sound drivers rely on
   * seeing exactly one interrupt per field; holding the line for the
   * whole vertical blank re-enters their handler dozens of times. */
  bool zint() const
  {
    return m_line == visible_lines() &&
           m_dot < (h40() ? kZintReleaseH40 : kZintReleaseH32);
  }
  uint8_t reg(int i) const
  {
    return m_reg[i & 0x17];
  }
  bool in_vblank() const;
  uint32_t line() const
  {
    return m_line;
  }
  uint32_t dot() const
  {
    return m_dot;
  }
  uint64_t frame_index() const
  {
    return m_frames;
  }
  /* Register 1 bit 3 (M2) selects the 30-cell field; the machine's own
   * video standard does not. A PAL machine boots in 28-cell mode until the
   * game programs the register, and an NTSC machine will honour a 30-cell
   * request even though the display rolls on real hardware. */
  uint32_t visible_lines() const
  {
    return (m_reg[1] & 0x08) != 0 ? kMaxVisibleLines : 224;
  }
  uint32_t visible_width() const
  {
    return h40() ? 320 : 256;
  }
  bool h40() const
  {
    return (m_reg[12] & 1) != 0;
  }
  bool display_enable() const
  {
    return (m_reg[1] & 0x40) != 0;
  }

  /* Video output seam: the machine reads the line buffer per scanline. The
   * frame buffer is always allocated for the tallest field so a mid-frame
   * switch into 30-cell mode cannot run past its end. */
  static constexpr uint32_t kLineBufferWidth = 320;
  static constexpr uint32_t kMaxVisibleLines = 240;

  /* --- rendering (line-based composite inside the dot model) --- */
  void render_line(uint32_t line);
  const uint8_t *line_pixels(uint32_t line) const
  {
    return m_frame.data() + (size_t)line * kLineBufferWidth;
  }

  /* CRAM snapshot for the shared uiplot palette cache. */
  const uint16_t *cram() const
  {
    return m_cram;
  }
  const uint8_t *cram_dirty() const
  {
    return m_cram_dirty;
  }
  struct Px {
    uint8_t value; /* palette<<4 | color, 0 = transparent */
    uint8_t prio;  /* priority bit */
  };
  Px layer_pixel(int layer, uint32_t line, uint32_t x) const;
  Px window_pixel(uint32_t line, uint32_t x) const;
  void render_sprites(uint32_t line, Px *out) const;

  uint16_t vram_word(uint32_t addr) const
  {
    return (uint16_t)((uint16_t)m_vram[addr & 0xFFFE] << 8 |
                      m_vram[(addr & 0xFFFE) + 1]);
  }

private:
  void control_write(uint16_t data);
  void data_write(uint16_t data);
  uint16_t data_read();
  void write_register(int index, uint8_t value);
  void service_line_events();
  uint32_t dot_mclk() const;

  uint8_t m_reg[0x18]{};
  uint8_t m_vram[0x10000]{};
  uint16_t m_cram[64]{};
  uint8_t m_cram_dirty[64]{}; /* 1 = entry changed */
  uint16_t m_vsram[40]{};

  uint32_t m_address = 0; /* 14-bit address latch + CD bits */
  uint32_t m_cd = 0;      /* command bits 0-3 */
  bool m_latch_second = false;
  bool m_pending_read = false;
  uint16_t m_read_buffer = 0;
  bool m_dma_pending = false;

  bool m_pal = false;
  uint64_t m_mclk = 0; /* master clocks into the current line */
  uint32_t m_dot = 0;
  uint32_t m_line = 0;
  uint64_t m_frames = 0;

  std::vector<uint8_t> m_frame; /* visible_lines x 320 CRAM indices */
  uint8_t m_hint_counter = 0;
  bool m_vint_pending = false; /* IPL request, cleared by the acknowledge */
  bool m_vint_flag = false;    /* status bit 7, cleared by a status read */
  bool m_hint_pending = false;

  /* DMA unit (correct data movement, approximate timing) */
  std::function<uint16_t(uint32_t)> m_dma_reader;
  uint64_t m_dma_debt_mclk = 0;
  bool m_dma_busy = false;
  bool m_dma_fill_armed = false;

  void trigger_dma();
  void run_dma_transfer();
  void run_dma_fill(uint16_t latch);
  void run_dma_copy();
  void charge_dma(uint32_t words);
};

}  // namespace generator
