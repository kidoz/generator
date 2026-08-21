/* SPDX-License-Identifier: GPL-2.0-or-later */
/* YM3438 (CMOS YM2612) — the bus interface, timers, status/busy
 * behavior and the IRQ line that unblock sound-driver handshakes, plus
 * the operator pipeline (EG/PG/LFO/DAC).
 *
 * Bus (Z80 view, also reachable from the 68K window):
 *   A0=0 address, A0=1 data, A1=0 bank 0, A1=1 bank 1
 *
 * Clock: master / 7 (same as the 68K). Timer periods in YM clocks:
 *   A = 12 * (1024 - NA), B = 192 * (256 - NB).
 * Busy flag lasts ~17 FM clock cycles after a register write. */

#pragma once

#include <array>
#include <cstdint>

namespace generator {

class Ym3438 {
public:
  void reset();

  /* Z80-side / 68K-window access: address for A0=0 and data for A0=1;
   * A1 selects register bank 0/1. */
  uint8_t read_status() const;
  void write_address(uint8_t addr, uint8_t bank = 0); /* A0 = 0 */
  void write_data(uint8_t data, uint8_t bank = 0);    /* A0 = 1 */
  uint8_t read_data() const; /* A0 = 1 (unused on HW) */

  /* Advance by master clocks; returns the IRQ level toward the Z80
   * (true = assert). */
  bool advance_mclk(uint64_t ticks);

  /* --- state --- */
  bool irq_line() const
  {
    return m_irq;
  }
  uint8_t status() const
  {
    return m_status;
  }

  /* Register access for tests / the operator pipeline. */
  uint8_t reg(uint8_t bank, uint8_t addr) const
  {
    return m_regs[bank & 1][addr];
  }

  /* Audio output: call advance_mclk first, then read the mixed sample. */
  int16_t sample_left() const
  {
    return m_sample_l;
  }
  int16_t sample_right() const
  {
    return m_sample_r;
  }

private:
  void write_register(uint8_t bank, uint8_t addr, uint8_t data);
  void update_irq();

  std::array<std::array<uint8_t, 0x100>, 2> m_regs{};
  uint8_t m_latch_addr[2] = {};

  /* status: bit 7 busy, bit 6 timer A, bit 5 timer B */
  uint8_t m_status = 0;
  uint32_t m_busy_mclk = 0;

  /* timers (in master clocks remaining) */
  int64_t m_timer_a = 0;
  int64_t m_timer_b = 0;
  bool m_irq = false;

  /* FM operator pipeline (sample-accurate) */
  /* FM operator pipeline — proper YM2612 structure.
   * Envelope: 10-bit level (0 = loudest, 0x3FF = silence).
   * Phase: 20-bit accumulator per operator. */
  struct Operator {
    uint32_t phase = 0;
    uint32_t freq = 0;
    uint16_t block = 0;
    uint16_t fnum = 0;
    uint32_t eg_level = 0x3FF;
    uint8_t eg_state = 3;
    bool key_on = false;
    bool key_on_prev = false;
    uint8_t eg_rate_attack = 0;
    uint8_t eg_rate_decay = 0;
    uint8_t eg_rate_sustain = 0;
    uint8_t eg_rate_release = 0;
    uint8_t eg_total_level = 0;
    uint8_t eg_sustain_level = 0;
    uint8_t eg_ks = 0;
    bool ssg_enable = false;
    uint8_t ssg_mode = 0;
    int16_t output = 0;
    int32_t mod_input = 0; /* phase modulation from previous operators */
  };
  std::array<Operator, 6 * 4> m_ops{};

  /* Per-channel state */
  struct Channel {
    uint8_t algorithm = 0;             /* from $B0-$BF bits 0-2 */
    uint8_t feedback = 0;              /* from $B0-$BF bits 3-5 */
    uint8_t pms = 0;                   /* phase modulation sensitivity */
    uint8_t ams = 0;                   /* amplitude modulation sensitivity */
    uint8_t pan_left = 1;              /* from $B4-$BF bit 7 */
    uint8_t pan_right = 1;             /* from $B4-$BF bit 6 */
    int32_t feedback_hist[2] = {0, 0}; /* OP1 feedback delay */
  };
  std::array<Channel, 6> m_channels{};

  /* LFO */
  bool m_lfo_enabled = false;
  uint8_t m_lfo_freq = 0; /* 0-7 from $22 bits 0-2 */
  uint32_t m_lfo_counter = 0;
  uint8_t m_lfo_am_value = 0; /* 0-255 AM LFO output */
  int8_t m_lfo_pm_value = 0;  /* signed PM LFO output */

  void update_lfo();
  int32_t apply_feedback(int ch);
  int32_t apply_operator(int ch, int op, int32_t mod);

  /* DAC (channel 6 in DAC mode, register $2B enable, $2A data) */
  bool m_dac_enabled = false;
  uint8_t m_dac_value = 0x80;
  int16_t m_dac_output = 0;
  int16_t m_sample_l = 0;
  int16_t m_sample_r = 0;
  uint64_t m_sample_timer = 0;
  uint32_t m_eg_timer = 0;
  uint32_t m_eg_prescaler = 0;

  void update_operators();
  void update_phase(int ch, int op);
  void update_envelope(int ch, int op);
  int16_t calculate_output();
  static int16_t sine_table(int phase, int envelope);
  static uint8_t eg_rate_compute(uint8_t rate, uint8_t ksv);
};

}  // namespace generator
