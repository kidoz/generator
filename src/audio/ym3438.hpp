/* SPDX-License-Identifier: GPL-2.0-or-later */
/* YM3438 (CMOS YM2612) — OPN2 synthesis core.
 *
 * Rebuilt around the Nuked-OPN2 / MAME envelope-generator semantics that
 * the driver ecosystem is tuned against: a 16.16 fixed-point attenuation
 * accumulator stepped from a per-rate delta table at the native FM
 * sample rate, exponential attack shaping, and
 * full SSG-EG cycling (threshold at half scale, inversion, hold and
 * alternate modes, phase reset on non-alternate loops).
 *
 * Bus (Z80 view, also reachable from the 68K window):
 *   A0=0 address, A0=1 data, A1=0 bank 0, A1=1 bank 1
 *
 * Clock: master / 7 (same as the 68K); one operator sample per 144 chip
 * clocks. Timer periods in YM clocks:
 *   A = 144 * (1024 - NA), B = 2304 * (256 - NB).
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
  [[nodiscard]] uint8_t read_status() const;
  void write_address(uint8_t addr, uint8_t bank = 0); /* A0 = 0 */
  void write_data(uint8_t data, uint8_t bank = 0);    /* A0 = 1 */
  [[nodiscard]] uint8_t read_data() const;            /* A0 = 1 (floats high) */

  /* Advance by master clocks; returns the IRQ level toward the Z80
   * (true = assert). */
  bool advance_mclk(uint64_t ticks);

  /* Positive master-clock interval until the next output update. */
  [[nodiscard]] uint64_t mclk_until_output() const
  {
    return kMclkPerFmSample - m_sample_timer;
  }

  /* --- state --- */
  [[nodiscard]] bool irq_line() const
  {
    return m_irq;
  }
  [[nodiscard]] uint8_t status() const
  {
    return m_status;
  }

  /* Register access for tests / the operator pipeline. */
  [[nodiscard]] uint8_t reg(uint8_t bank, uint8_t addr) const
  {
    return m_regs[bank & 1][addr];
  }

  /* Audio output: call advance_mclk first, then read the mixed sample. */
  [[nodiscard]] int16_t sample_left() const
  {
    return m_sample_l;
  }
  [[nodiscard]] int16_t sample_right() const
  {
    return m_sample_r;
  }

private:
  /* One sample per 144 chip clocks, with the chip clock at master/7. */
  static constexpr uint64_t kMclkPerFmSample = 144ULL * 7;

  /* Envelope generator phase. The numeric order (off < release <
   * sustain < decay < attack) lets key-off drop to release with one
   * comparison, the way the chip walks its states. */
  enum class EgState : uint8_t {
    off,
    release,
    sustain,
    decay,
    attack
  };

  /* One operator (slot): phase generator + envelope generator state. */
  struct Slot {
    /* Phase generator */
    uint32_t phase = 0; /* 20-bit accumulator */
    int32_t incr = 0;   /* per-sample increment, fnum/block/mul/detune */
    bool incr_dirty = true;

    /* Envelope generator: 16.16 attenuation, 0 = loudest */
    int32_t volume = (1024 << 16) - 1;
    EgState state = EgState::off;
    int32_t delta_attack = 0;
    int32_t delta_decay = 0;
    int32_t delta_sustain = 0;
    int32_t delta_release = 0;
    int32_t sustain_level = (1024 << 16) - 1;
    int32_t total_level = 0; /* register $40-$4F, << 3, applied at output */
    uint8_t key_scale = 0;   /* cached ksr the rate deltas were built for */

    /* SSG-EG ($90) */
    uint8_t ssg_control = 0;
    uint8_t ssg_inverted = 0;

    /* Key and amplitude modulation */
    bool keyed = false;
    bool am_enabled = false; /* register $60-$6F bit 7 */
    uint32_t am_depth = 0;   /* channel AMS depth gated by am_enabled */
  };

  /* One of the six output channels. */
  struct Channel {
    std::array<Slot, 4> slots{};
    uint8_t algorithm = 0;
    uint8_t feedback = 0;
    std::array<int32_t, 2> op1_history = {};
    uint8_t key_code = 0; /* block/fnum key code for EG rates and detune */
    uint8_t pms = 0;      /* phase-modulation depth, pre-scaled */
    uint32_t ams = 0;     /* amplitude-modulation depth */
    uint8_t pan_left = 1;
    uint8_t pan_right = 1;
    bool pitch_dirty = true;
  };

  void write_register(uint8_t bank, uint8_t addr, uint8_t data);
  void update_irq();
  void render_sample();
  void refresh_pitch(int ch);
  void refresh_rates(int ch, int op);
  void key_set(int ch, int op, bool on);
  void ssg_cycle_complete(Slot &slot);
  void envelope_step(Slot &slot);
  [[nodiscard]] int32_t envelope_output(const Slot &slot) const;
  [[nodiscard]] int16_t operator_output(uint32_t phase, int32_t mod,
                                        uint32_t attenuation) const;

  std::array<std::array<uint8_t, 0x100>, 2> m_regs{};
  uint8_t m_latch_addr[2] = {};
  std::array<Channel, 6> m_channels{};

  /* status: bit 7 busy, bit 0 timer A, bit 1 timer B */
  uint8_t m_status = 0;
  uint32_t m_busy_mclk = 0;

  /* timers (in master clocks remaining) */
  int64_t m_timer_a = 0;
  int64_t m_timer_b = 0;
  bool m_irq = false;

  /* LFO */
  uint32_t m_lfo_count = 0;
  uint32_t m_lfo_incr = 0;
  uint32_t m_lfo_am = 0; /* 0..0x10000, tremolo depth for the next sample */
  int32_t m_lfo_pm = 0;  /* -0x8000..0x8000, vibrato source */

  /* DAC (channel 6 in DAC mode, register $2B enable, $2A data) */
  bool m_dac_enabled = false;
  int32_t m_dac_output = 0;

  int16_t m_sample_l = 0;
  int16_t m_sample_r = 0;
  uint64_t m_sample_timer = 0;
};

}  // namespace generator
