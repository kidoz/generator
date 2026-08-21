/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "vdp.hpp"

namespace generator {

namespace {

/* Every scanline is 3420 master clocks, in both H modes. H32 gets there
 * with 342 dots at master/10; H40 has 420 dots but cannot: 420 x 8 is
 * only 3360, so the chip runs the tail of the line on the slower clock
 * and 30 dots inside horizontal blanking cost master/10 instead. Field
 * length: 262 lines NTSC / 313 PAL (the PAL field rate on hardware is
 * crystal / (3420 x 313) = 49.70 Hz, not an even 50). Interrupt
 * behaviour (VINT enable reg 1 bit 5, HINT enable reg 0 bit 4, counter
 * reload from reg 10) follows the YM7101 documentation; HINT/VINT are
 * still raised at line granularity.
 */
constexpr uint32_t kDotsH40 = 420;
constexpr uint32_t kDotsH32 = 342;
constexpr uint32_t kLinesNtsc = 262;
constexpr uint32_t kLinesPal = 313;

/* First H40 dot billed at master/10, and how many follow. Placed in
 * blanking, past the point where the Z80 interrupt pulse is released, so
 * the visible raster keeps a uniform master/8 dot. */
constexpr uint32_t kH40SlowFirst = 390;
constexpr uint32_t kH40SlowCount = 30;

}  // namespace

void Vdp::reset(bool pal)
{
  m_pal = pal;
  for (auto &r : m_reg) {
    r = 0;
  }
  /* The chip memories go with the registers: this is a power-on, and a
   * cartridge swap runs it. Leaving VRAM and CRAM behind would paint the
   * previous game's tiles and palette over the first fields of the new
   * one. Every CRAM entry is marked dirty so the UI palette cache, which
   * only recomputes on the dirty flag, drops its cached colours too. */
  for (auto &v : m_vram) {
    v = 0;
  }
  for (auto &c : m_cram) {
    c = 0;
  }
  for (auto &d : m_cram_dirty) {
    d = 1;
  }
  for (auto &v : m_vsram) {
    v = 0;
  }
  m_address = 0;
  m_cd = 0;
  m_latch_second = false;
  m_pending_read = false;
  m_read_buffer = 0;
  m_dma_pending = false;
  m_mclk = 0;
  m_dot = 0;
  m_line = 0;
  m_frames = 0;
  m_hint_counter = 0;
  m_vint_pending = false;
  m_vint_flag = false;
  m_hint_pending = false;
  m_dma_debt_mclk = 0;
  m_dma_busy = false;
  m_dma_fill_armed = false;
  m_frame.assign((size_t)kMaxVisibleLines * kLineBufferWidth, 0);
}

uint16_t Vdp::port_read(uint32_t addr)
{
  /* Canonical decode within the 0x20 mirror: bit 2 (0x04) selects the
   * control port, bit 3 the HV counter — 0x04-0x07 IS control, so the
   * register-setup longs the boot code writes to 0xC00004 land in
   * control_write (they previously leaked into the data port and the
   * VINT enable was never programmed). */
  switch ((addr & 0x1C) >> 2) {
  case 0: /* 0x00-0x03: data port */
    return data_read();
  case 1: /* 0x04-0x07: control/status */
  {
    /* Status bits: 9 FIFO empty, 7 VINT happened, 4 odd frame, 3 vblank,
     * 2 hblank, 1 DMA busy, 0 PAL. Games poll bit 1 to wait a DMA out and
     * bit 0 to pick their timing tables, so both have to sit where the
     * chip puts them. */
    uint16_t status = 0x0200;
    if (m_pal) {
      status |= 0x0001;
    }
    if (m_dma_busy) {
      status |= 0x0002;
    }
    if (m_dot >= visible_width()) {
      status |= 0x0004;
    }
    if (in_vblank()) {
      status |= 0x0008;
    }
    if (m_frames & 1) {
      status |= 0x0010;
    }
    if (m_vint_flag) {
      status |= 0x0080;
    }
    /* Reading the control port clears the VINT status bit and cancels a
     * half-written command, but it does not retract the interrupt: only
     * the 68K's acknowledge does that. */
    m_vint_flag = false;
    m_latch_second = false;
    return status;
  }
  case 2:
  case 3: /* 0x08-0x0F: HV counter */
  {
    /* H counts 0..0xB6 in H40 (each 2 dots), V counts interlaced even/odd
     * lines. Raw line/dot scaling; latching (reg 0 bit 2) is not
     * modelled yet. */
    const uint32_t v = m_line;
    const uint32_t h = (m_dot / 2) & 0xFF;
    return (uint16_t)((v << 8) | h);
  }
  default:
    /* PSG region 0x10-0x17 reads open bus on the MD; test 0x18-0x1F */
    return 0;
  }
}

void Vdp::port_write(uint32_t addr, uint16_t data)
{
  switch ((addr & 0x1C) >> 2) {
  case 0: /* 0x00-0x03: data port */
    data_write(data);
    return;
  case 1: /* 0x04-0x07: control port */
    control_write(data);
    return;
  default:
    /* PSG region 0x10-0x17: writes are not forwarded here yet */
    return;
  }
}


void Vdp::control_write(uint16_t data)
{
  /* A word with bits 15-13 = 100 is a self-contained register write: it
   * consumes any pending latch state. Games program the VDP as single
   * move.w stores of 0x80|r<<8|v, so this check must come first. */
  if ((data & 0xE000) == 0x8000) {
    m_latch_second = false;
    write_register((data >> 8) & 0x1F, (uint8_t)(data & 0xFF));
    return;
  }

  /* Two-word address/command protocol:
   *   word 1 = [CD1 CD0][A13..A0]
   *   word 2 = [0 x 8][CD5 CD4 CD3 CD2][0 0][A15 A14]
   * CD5..CD2 sit in bits 7-4 of the second word, so they land two places
   * above their home in the command register — not eight. CD5 is the DMA
   * request; CD4 distinguishes a VRAM copy from a transfer. */
  if (!m_latch_second) {
    m_address = (m_address & 0xC000) | (data & 0x3FFF);
    m_cd = (m_cd & 0x3C) | ((data >> 14) & 3);
    m_latch_second = true;
    return;
  }
  m_latch_second = false;
  m_address = ((data & 3) << 14) | (m_address & 0x3FFF);
  m_cd = (m_cd & 3) | ((data >> 2) & 0x3C);

  switch (m_cd & 0xF) {
  case 0x0: /* VRAM read setup */
    m_pending_read = true;
    m_read_buffer = (uint16_t)((uint16_t)m_vram[m_address & 0xFFFE] << 8 |
                               m_vram[(m_address & 0xFFFE) + 1]);
    break;
  case 0x8: /* CRAM read setup. unverified */
    m_pending_read = true;
    m_read_buffer = m_cram[(m_address >> 1) & 0x3F];
    break;
  case 0x4: /* VSRAM read setup. unverified */
    m_pending_read = true;
    m_read_buffer = m_vsram[((m_address >> 1) < 40 ? (m_address >> 1) : 0)];
    break;
  default:
    break;
  }
  /* DMA: an armed address write (CD5) with DMA enabled (reg 1 bit 4)
   * starts the transfer — except fill (type 2), which waits for the
   * next data write to supply the latch value. Register map (decimal
   * indices = register numbers): length = $13 | $14<<8 words; source
   * offset = $15 | $16<<8 with bank $17; type = $17 >> 6 (0/1 = 68K-VDP,
   * 2 = fill, 3 = copy). */
  if ((m_cd & 0x20) != 0 && (m_reg[1] & 0x10) != 0) {
    const int type = (m_reg[23] >> 6) & 3;
    if (type == 2) {
      m_dma_fill_armed = true;
      m_dma_busy = true;
    } else if (type == 0 || type == 1) {
      run_dma_transfer();
    } else {
      run_dma_copy();
    }
  }
}

void Vdp::write_register(int index, uint8_t value)
{
  if (index >= 0x18) {
    return; /* registers 0x18-0x1F are read-as-zero writes dropped */
  }
  m_reg[index] = value;
  if (index == 0x0F) { /* HINT counter reload value */
  }
}

void Vdp::data_write(uint16_t data)
{
  m_latch_second = false;
  if (m_dma_fill_armed) {
    m_dma_fill_armed = false;
    run_dma_fill(data);
    return;
  }
  switch (m_cd & 0xF) {
  case 1: { /* VRAM write */
    const uint32_t addr = m_address & 0xFFFE;
    if ((addr & 1) == 0) {
      m_vram[addr] = (uint8_t)(data >> 8);
      m_vram[addr + 1] = (uint8_t)data;
    }
    break;
  }
  case 3: { /* CRAM write (two nibbles per byte, 9-bit entries) */
    const uint32_t idx = (m_address >> 1) & 0x3F;
    m_cram[idx] = data & 0x0EEE;
    m_cram_dirty[idx] = 1;
    break;
  }
  case 5: { /* VSRAM write */
    const uint32_t idx = (m_address >> 1) & 0x3F;
    if (idx < 40) {
      m_vsram[idx] = data & 0x07FF;
    }
    break;
  }
  default:
    break;
  }
  /* The VDP address counter is 16 bits wide. Register 15 is the complete
   * auto-increment value; register 16 controls plane size and must not
   * participate in data-port addressing. */
  m_address = (m_address + m_reg[15]) & 0xFFFF;
}

uint16_t Vdp::data_read()
{
  m_latch_second = false;
  if (!m_pending_read) {
    m_read_buffer =
        m_vram[m_address & 0xFFFE] << 8 | m_vram[(m_address & 0xFFFE) + 1];
  }
  const uint16_t value = m_read_buffer;
  m_pending_read = false;
  /* buffered read fills for the next access */
  m_read_buffer = m_vram[(m_address + 2) & 0xFFFE] << 8 |
                  m_vram[((m_address + 2) & 0xFFFE) + 1];
  m_address = (m_address + m_reg[15]) & 0xFFFF;
  return value;
}

bool Vdp::in_vblank() const
{
  return m_line >= visible_lines();
}

int Vdp::ipl() const
{
  /* A request only reaches the CPU while its enable bit is set, so
   * clearing IE0/IE1 retracts a pending line instead of leaving it stuck
   * until something acknowledges it. */
  if (m_vint_pending && (m_reg[1] & 0x20) != 0) {
    return 6;
  }
  if (m_hint_pending && (m_reg[0] & 0x10) != 0) {
    return 4;
  }
  return 7;
}

void Vdp::acknowledge_int(int level)
{
  if (level == 6) {
    m_vint_pending = false;
  } else if (level == 4) {
    m_hint_pending = false;
  }
}

void Vdp::service_line_events()
{
  /* Called when the line counter wraps. VINT raises at the start
   * of the first vblank line if enabled and auto-clears at the top of the
   * next field (the real VDP clears it at the end of vertical blanking);
   * HINT decrements at the end of each display line and raises on
   * underflow. Clear-on-status-read is handled in port_read. */
  if (m_line == visible_lines()) {
    m_vint_pending = true;
    m_vint_flag = true;
  }
  if (m_line < visible_lines()) {
    if (m_hint_counter == 0) {
      m_hint_pending = true;
      m_hint_counter = m_reg[10];
    } else {
      m_hint_counter--;
    }
  }
}


void Vdp::charge_dma(uint32_t words)
{
  /* Approximate bus-steal cost per transferred word: the VDP takes two
   * 68K cycles (16 master clocks) per word in blanking; during active
   * display the FIFO slot budget limits it (H40: 16 words per 3360-mclk
   * line = 210 mclk/word). The 68K is halted for the whole transfer —
   * the machine drains this debt from the master clock after the
   * triggering instruction. unverified */
  const bool blank = in_vblank() || !display_enable();
  const uint32_t per_word = blank ? 16 : 210;
  m_dma_debt_mclk += (uint64_t)words * per_word;
}

void Vdp::run_dma_transfer()
{
  m_dma_busy = true;
  const uint32_t length = (uint32_t)m_reg[19] | ((uint32_t)m_reg[20] << 8);
  const uint32_t src = (((uint32_t)m_reg[23] & 0xFF) << 17) |
                       ((uint32_t)m_reg[22] << 9) | ((uint32_t)m_reg[21] << 1);
  for (uint32_t i = 0; i < length; i++) {
    uint16_t word = 0;
    if (m_dma_reader) {
      word = m_dma_reader((src + 2 * i) & 0xFFFFFF);
    }
    data_write(word);
  }
  charge_dma(length);
  m_dma_busy = false;
}

void Vdp::run_dma_fill(uint16_t latch)
{
  m_dma_busy = true;
  const uint32_t length = (uint32_t)m_reg[19] | ((uint32_t)m_reg[20] << 8);
  const uint8_t hi = (uint8_t)(latch >> 8);
  const uint8_t lo = (uint8_t)latch;
  for (uint32_t i = 0; i < length; i++) {
    if ((m_cd & 0xF) == 1) {
      /* VRAM fill replicates the latch bytes across even/odd addresses */
      const uint32_t addr = m_address & 0xFFFE;
      m_vram[addr] = hi;
      m_vram[addr + 1] = lo;
      m_address = (m_address + m_reg[15]) & 0xFFFF;
    } else {
      /* CRAM/VSRAM fills write the word through the normal path (which
       * applies the increment itself) */
      data_write(latch);
    }
  }
  charge_dma(length);
  m_dma_busy = false;
}

void Vdp::run_dma_copy()
{
  m_dma_busy = true;
  const uint32_t length = (uint32_t)m_reg[19] | ((uint32_t)m_reg[20] << 8);
  uint32_t src = ((((uint32_t)m_reg[23] & 0xFF) << 17) |
                  ((uint32_t)m_reg[22] << 9) | ((uint32_t)m_reg[21] << 1)) &
                 0xFFFE;
  for (uint32_t i = 0; i < length; i++) {
    const uint16_t word = (uint16_t)((uint16_t)m_vram[src & 0xFFFE] << 8 |
                                     m_vram[(src & 0xFFFE) + 1]);
    data_write(word);
    src = (src + 2) & 0xFFFF;
  }
  charge_dma(length);
  m_dma_busy = false;
}

/* Master clocks charged for the dot about to complete. */
uint32_t Vdp::dot_mclk() const
{
  if (!h40()) {
    return 10;
  }
  const bool slow =
      m_dot >= kH40SlowFirst && m_dot < kH40SlowFirst + kH40SlowCount;
  return slow ? 10 : 8;
}

int Vdp::advance_mclk(uint64_t ticks)
{
  m_mclk += ticks;

  /* Dot by dot: the cost varies within an H40 line, and the H mode can
   * change mid-line when the game rewrites register 12. */
  for (;;) {
    const uint32_t cost = dot_mclk();
    if (m_mclk < cost) {
      break;
    }
    m_mclk -= cost;
    m_dot++;
    if (m_dot < (h40() ? kDotsH40 : kDotsH32)) {
      continue;
    }
    m_dot = 0;
    if (m_line < visible_lines()) {
      render_line(m_line);
    }
    m_line++;
    if (m_line >= (m_pal ? kLinesPal : kLinesNtsc)) {
      m_line = 0;
      m_frames++;
    }
    service_line_events();
  }
  return ipl();
}

}  // namespace generator
