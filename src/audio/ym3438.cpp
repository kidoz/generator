/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ym3438.hpp"

#include <algorithm>

namespace generator {

namespace {

constexpr int kMclkPerFmClock = 7;
/* 144 chip clocks per output sample. */
constexpr uint64_t kMclkPerFmSample = 144ULL * kMclkPerFmClock;

/* DT1 phase-increment offsets, indexed by [detune & 3][key code]. */
constexpr uint8_t kDetuneTable[4][32] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
     2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8},
    {1, 1, 1, 1, 2, 2, 2, 2,  2,  3,  3,  3,  4,  4,  4,  5,
     5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16},
    {2,  2,  2,  2,  4,  4,  4,  4,  4,  6,  6,  6,  8,  8,  8,  10,
     10, 12, 12, 14, 16, 16, 18, 20, 22, 24, 26, 28, 32, 32, 32, 32}};

/* The F-number's top bits fold into a two-bit note index for the key
 * code, which drives both detune and key scaling. */
constexpr uint8_t kFnumToNote[16] = {0, 0, 0, 0, 0, 0, 0, 1,
                                     2, 3, 3, 3, 3, 3, 3, 3};
/* Busy: ~17 FM clock cycles after a register write */
constexpr uint32_t kBusyMclk = 17 * kMclkPerFmClock;

/* status bits:
 * status = (busy << 7) | (timer_b << 1) | timer_a;
 * The timer flags live at bits 0-1, NOT bits 5-6 as some docs say.
 * The GEMS driver polls BIT 0 for Timer A and AND #2 for Timer B. */
constexpr uint8_t kStBusy = 0x80;
constexpr uint8_t kStTimerA = 0x01;
constexpr uint8_t kStTimerB = 0x02;

/* mode register $27 */
constexpr uint8_t kModeLoadA = 0x01;
constexpr uint8_t kModeLoadB = 0x02;
constexpr uint8_t kModeEnableA = 0x04;
constexpr uint8_t kModeEnableB = 0x08;
constexpr uint8_t kModeResetFlagA = 0x10;
constexpr uint8_t kModeResetFlagB = 0x20;
constexpr uint8_t kModeIrqA = 0x40;
constexpr uint8_t kModeIrqB = 0x80;

int64_t timer_a_period(uint8_t hi, uint8_t lo)
{
  /* NA = (reg24 & 3) << 8 | reg25 */
  const int na = ((hi & 3) << 8) | lo;
  return (int64_t)(1024 - na) * 12 * kMclkPerFmClock;
}

int64_t timer_b_period(uint8_t val)
{
  return (int64_t)(256 - val) * 192 * kMclkPerFmClock;
}

}  // namespace

void Ym3438::reset()
{
  m_regs = {};
  m_latch_addr[0] = m_latch_addr[1] = 0;
  m_status = 0;
  m_busy_mclk = 0;
  m_timer_a = 0;
  m_timer_b = 0;
  m_irq = false;
  m_ops = {};
  for (auto &op : m_ops) {
    op.eg_level = 0x3FF;
    op.eg_state = 3;
  }
  m_channels = {};
  m_lfo_enabled = false;
  m_lfo_freq = 0;
  m_lfo_counter = 0;
  m_lfo_am_value = 0;
  m_lfo_pm_value = 0;
  m_dac_enabled = false;
  m_dac_value = 0x80;
  m_dac_output = 0;
  m_sample_l = 0;
  m_sample_r = 0;
  m_sample_timer = 0;
  m_eg_timer = 0;
  m_eg_prescaler = 0;
}

uint8_t Ym3438::read_status() const
{
  return m_status;
}

void Ym3438::write_address(uint8_t addr, uint8_t bank)
{
  m_latch_addr[bank & 1] = addr;
  m_busy_mclk = kBusyMclk;
  m_status |= kStBusy; /* set immediately: the CPU polls right after */
}

void Ym3438::write_data(uint8_t data, uint8_t bank)
{
  bank &= 1;
  const uint8_t addr = m_latch_addr[bank];
  write_register(bank, addr, data);
  m_busy_mclk = kBusyMclk;
  m_status |= kStBusy; /* set immediately */
}

uint8_t Ym3438::read_data() const
{
  return 0xFF; /* data port reads float high on real hardware */
}

void Ym3438::write_register(uint8_t bank, uint8_t addr, uint8_t data)
{
  m_regs[bank][addr] = data;

  if (bank == 0 && addr == 0x27) {
    /* mode register: timer load/enable/flag-reset/irq-enable */
    if (data & kModeLoadA) {
      m_timer_a = timer_a_period(m_regs[0][0x24], m_regs[0][0x25]);
    }
    if (data & kModeLoadB) {
      m_timer_b = timer_b_period(m_regs[0][0x26]);
    }
    if (data & kModeResetFlagA) {
      m_status &= ~kStTimerA;
    }
    if (data & kModeResetFlagB) {
      m_status &= ~kStTimerB;
    }
    /* the IRQ line reflects the flag state immediately (level-triggered
     * toward the Z80), not on the next clock tick */
    update_irq();
  }
  if (bank == 0 && addr == 0x2B) {
    m_dac_enabled = (data & 0x80) != 0;
  }
  if (bank == 0 && addr == 0x2A) {
    m_dac_value = data;
    /* The YM2612 DAC: 8-bit unsigned input, nonlinear output.
     * Approximate: center at 0x80, scale to int16. */
    const int dac = ((int)data - 128) * 256;
    m_dac_output = (int16_t)(dac >> 1);
  }
  if (addr == 0x24 || addr == 0x25) {
    /* timer A period changed: reload if not running */
  }
}

bool Ym3438::advance_mclk(uint64_t ticks)
{
  /* busy countdown */
  if (m_busy_mclk > 0) {
    const uint32_t consume = (uint32_t)std::min<uint64_t>(m_busy_mclk, ticks);
    m_busy_mclk -= consume;
    if (m_busy_mclk == 0) {
      m_status &= ~kStBusy;
    } else {
      m_status |= kStBusy;
    }
  }

  const uint8_t mode = m_regs[0][0x27];
  const bool en_a = (mode & kModeEnableA) != 0;
  const bool en_b = (mode & kModeEnableB) != 0;

  /* Timer overflow sets the flag (sticky until the next $27 reset
   * write). The reset bit is MOMENTARY — active only during the
   * actual write cycle. So the flag clears on write and can be
   * re-set by the next overflow regardless of the stored $27
   * value. */
  if (en_a && m_timer_a > 0) {
    m_timer_a -= (int64_t)ticks;
    if (m_timer_a <= 0) {
      m_status |= kStTimerA;
      m_timer_a += timer_a_period(m_regs[0][0x24], m_regs[0][0x25]);
    }
  }
  if (en_b && m_timer_b > 0) {
    m_timer_b -= (int64_t)ticks;
    if (m_timer_b <= 0) {
      m_status |= kStTimerB;
      m_timer_b += timer_b_period(m_regs[0][0x26]);
    }
  }

  update_irq();

  /* The chip produces one sample every 144 of its own clocks, and its
   * clock is the master divided by seven: 1008 master clocks, not 1216.
   * The period sets the pitch of everything the chip plays. */
  m_sample_timer += ticks;
  while (m_sample_timer >= kMclkPerFmSample) {
    m_sample_timer -= kMclkPerFmSample;
    update_operators();
  }

  return m_irq;
}

void Ym3438::update_irq()
{
  const uint8_t mode = m_regs[0][0x27];
  m_irq = ((m_status & kStTimerA) && (mode & kModeIrqA)) != 0 ||
          ((m_status & kStTimerB) && (mode & kModeIrqB)) != 0;
}


// ------------------------------------------------------------------
// ------------------------------------------------------------------
// FM operator pipeline — proper YM2612 structure
// ------------------------------------------------------------------

void Ym3438::update_phase(int ch, int op)
{
  Operator &o = m_ops[ch * 4 + op];

  /* F-number: low 8 bits from $A0-A2/$A1-A3, high 2 from $A4-A6/$A5-A7 */
  const int bank = ch / 3;
  const int slot = ch % 3;
  const uint8_t fnum_lo = m_regs[bank][0xA0 + slot];
  const uint8_t block_fnum_hi = m_regs[bank][0xA4 + slot];
  o.fnum = (uint16_t)(fnum_lo | ((block_fnum_hi & 0x07) << 8));
  o.block = (uint16_t)((block_fnum_hi >> 3) & 0x07);

  /* Multi-frequency mode (CH3 special): $A6/$A7 overrides for op 2+ */
  if (ch == 2 && op >= 2 && (m_regs[0][0x22] & 0x40) != 0) {
    if (op == 2) {
      o.fnum = (uint16_t)(m_regs[0][0xA6] | ((m_regs[0][0xAD] & 0x07) << 8));
      o.block = (uint16_t)((m_regs[0][0xAD] >> 3) & 0x07);
    } else {
      o.fnum = (uint16_t)(m_regs[0][0xA7] | ((m_regs[0][0xAE] & 0x07) << 8));
      o.block = (uint16_t)((m_regs[0][0xAE] >> 3) & 0x07);
    }
  }

  /* Detune and multiple both live in $30-$3F: bits 6-4 select the detune
   * amount and sign, bits 3-0 the frequency multiple. MUL is what gives
   * each operator its ratio to the note, so ignoring it collapses a patch
   * into four operators all sounding the same pitch. */
  const uint8_t dt_mul = m_regs[bank][0x30 + op * 4 + slot];
  const int mul = dt_mul & 0x0F;
  const int detune = (dt_mul >> 4) & 0x07;

  const uint8_t keycode =
      (uint8_t)((o.block << 2) | kFnumToNote[(o.fnum >> 7) & 0x0F]);

  /* Base increment for the note, then the operator's multiple: code 0 is
   * a half rather than a zero. */
  int32_t inc = (int32_t)(((uint32_t)o.fnum << o.block) >> 1);
  inc = mul == 0 ? inc >> 1 : inc * mul;

  const int32_t det_phase = kDetuneTable[detune & 3][keycode];
  inc += (detune & 4) != 0 ? -det_phase : det_phase;
  if (inc < 0) {
    inc = 0;
  }

  o.freq = (uint32_t)inc;
  o.phase = (o.phase + o.freq) & 0xFFFFF;
}

uint8_t Ym3438::eg_rate_compute(uint8_t rate, uint8_t ksv)
{
  /* KS: bits 6-7 of the detune/KS register; higher KS = more effect */
  if (rate == 0) {
    return 0;
  }
  if (rate < 48) {
    return (uint8_t)(rate + (ksv >> (3 - (rate >> 4))));
  }
  return rate;
}

void Ym3438::update_envelope(int ch, int op)
{
  Operator &o = m_ops[ch * 4 + op];
  const int bank = ch / 3;
  const int slot = ch % 3;

  /* Key on/off from register $28. Bits 7-4 are the key-on flags, one per
   * operator, so a write with all four clear is the key off. The channel
   * is bits 1-0 within the part that bit 2 selects — bit 3 takes no part
   * in it, and reading the channel as the low three bits puts the second
   * part's three channels on the wrong indices. */
  const uint8_t kon_reg = m_regs[0][0x28];
  const int kon_channel = (kon_reg & 0x03) + ((kon_reg & 0x04) != 0 ? 3 : 0);
  if (kon_channel == ch && (kon_reg & 0x03) != 3) {
    o.key_on = (kon_reg & (0x10 << op)) != 0;
  }

  /* Read rates from register file:
   * Attack: $50-$5F bit 0-5, Decay: $60-$6F, Sustain: $70-$7F,
   * Release: $80-$8F bit 0-4 */
  const uint8_t det_ks = m_regs[bank][0x50 + op * 4 + slot];
  o.eg_ks = (det_ks >> 6) & 0x03;
  o.eg_rate_attack = det_ks & 0x1F;
  o.eg_rate_decay = m_regs[bank][0x60 + op * 4 + slot] & 0x1F;
  o.eg_rate_sustain = m_regs[bank][0x70 + op * 4 + slot] & 0x1F;
  o.eg_rate_release = m_regs[bank][0x80 + op * 4 + slot] & 0x0F;

  /* Total level: $40-$4F */
  o.eg_total_level = m_regs[bank][0x40 + op * 4 + slot] & 0x7F;

  /* Sustain level: $80-$8F bits 4-7 */
  o.eg_sustain_level = (m_regs[bank][0x80 + op * 4 + slot] >> 4) & 0x0F;

  /* SSG-EG: $90-$9F */
  const uint8_t ssg = m_regs[bank][0x90 + op * 4 + slot] & 0x0F;
  o.ssg_enable = (ssg & 0x08) != 0;
  o.ssg_mode = ssg & 0x07;

  /* Key-scale value: the same key code the phase generator derives. */
  const uint8_t kcode =
      (uint8_t)((o.block << 2) | kFnumToNote[(o.fnum >> 7) & 0x0F]);
  const uint8_t ksv = kcode >> (o.eg_ks ^ 3);

  /* Select rate based on state */
  uint8_t rate;
  switch (o.eg_state) {
  case 0: /* attack */
    rate = eg_rate_compute(o.eg_rate_attack, ksv);
    break;
  case 1: /* decay */
    rate = eg_rate_compute(o.eg_rate_decay, ksv);
    break;
  case 2: /* sustain */
    rate = eg_rate_compute(o.eg_rate_sustain, ksv);
    break;
  default: /* release */
    rate = eg_rate_compute((uint8_t)(o.eg_rate_release * 2 + 1), ksv);
    break;
  }

  /* State transitions */
  if (o.key_on && !o.key_on_prev) {
    /* Key-on event: reset to attack */
    o.eg_state = 0;
    o.eg_level = 0x3FF;
  } else if (!o.key_on && o.key_on_prev) {
    /* Key-off event: enter release */
    o.eg_state = 3;
  }
  o.key_on_prev = o.key_on;

  /* Envelope stepping. A rate names how often the counter's low bits
   * come up clear (the shift) and which of eight sub-patterns supplies
   * the increment, so slow rates move a step every few thousand ticks and
   * fast ones move several steps every tick. */
  uint32_t step = 0;
  if (rate >= 60) {
    step = o.eg_state == 0 ? 0x3FF : 8; /* effectively instant */
  } else if (rate >= 4) {
    static const uint8_t inc_tab[4][8] = {
        {0, 1, 0, 1, 0, 1, 0, 1},
        {0, 1, 0, 1, 1, 1, 0, 1},
        {0, 1, 1, 1, 0, 1, 1, 1},
        {0, 1, 1, 1, 1, 1, 1, 1},
    };
    const int shift = rate < 48 ? (11 - (rate >> 2)) : 0;
    const uint32_t mask = (1u << shift) - 1u;
    if ((m_eg_timer & mask) == 0) {
      if (rate < 48) {
        step = inc_tab[rate & 3][(m_eg_timer >> shift) & 7];
      } else {
        step = 1u << ((rate >> 2) - 12);
      }
    }
  }

  /* Apply the step */
  if (o.eg_state == 0) { /* attack: attenuation falls towards 0 */
    if (step != 0) {
      /* The attack is exponential in attenuation: each step closes a
       * fixed fraction of what is left, so it is fast while the operator
       * is quiet and eases in as it approaches full level. */
      const int32_t level = (int32_t)o.eg_level;
      const int32_t next = level + (((~level) * (int32_t)step) >> 4);
      if (next <= 0) {
        o.eg_level = 0;
        o.eg_state = 1; /* -> decay */
      } else {
        o.eg_level = (uint32_t)next;
      }
    }
  } else {
    /* decay/sustain/release: level increases (toward 0x3FF) */
    const uint32_t new_level = o.eg_level + step;
    if (new_level >= 0x3FF) {
      o.eg_level = 0x3FF;
      /* At max, stay in current state */
    } else {
      o.eg_level = new_level;
    }

    /* Check sustain level for decay -> sustain transition */
    if (o.eg_state == 1) {
      /* SL is four bits of a ten-bit attenuation, and the all-ones code
       * means full attenuation rather than 15 steps. */
      const uint32_t sl = o.eg_sustain_level == 0x0F
                              ? 0x3E0u
                              : (uint32_t)o.eg_sustain_level << 5;
      if (o.eg_level >= sl) {
        o.eg_state = 2; /* -> sustain */
      }
    }
  }
}


int16_t Ym3438::sine_table(int phase, int envelope)
{
  /* 10-bit phase from the 20-bit accumulator, 10-bit envelope */
  static const uint8_t quarter[256] = {
      0,   1,   2,   3,   5,   6,   7,   8,   10,  11,  12,  13,  15,  16,  17,
      18,  20,  21,  22,  23,  25,  26,  27,  28,  30,  31,  32,  33,  35,  36,
      37,  38,  40,  41,  42,  43,  45,  46,  47,  48,  50,  51,  52,  53,  55,
      56,  57,  58,  60,  61,  62,  63,  65,  66,  67,  68,  70,  71,  72,  73,
      75,  76,  77,  78,  80,  81,  82,  83,  85,  86,  87,  88,  90,  91,  92,
      93,  95,  96,  97,  98,  100, 101, 102, 103, 105, 106, 107, 108, 110, 111,
      112, 113, 115, 116, 117, 118, 120, 121, 122, 123, 125, 126, 127, 128, 129,
      131, 132, 133, 134, 136, 137, 138, 139, 141, 142, 143, 144, 146, 147, 148,
      149, 151, 152, 153, 154, 155, 157, 158, 159, 160, 162, 163, 164, 165, 167,
      168, 169, 170, 171, 173, 174, 175, 176, 178, 179, 180, 181, 183, 184, 185,
      186, 188, 189, 190, 191, 192, 194, 195, 196, 197, 199, 200, 201, 202, 203,
      205, 206, 207, 208, 210, 211, 212, 213, 215, 216, 217, 218, 219, 221, 222,
      223, 224, 226, 227, 228, 229, 230, 232, 233, 234, 235, 237, 238, 239, 240,
      241, 243, 244, 245, 246, 248, 249, 250, 251, 252, 254, 255,
  };

  /* The caller already reduced the accumulator to the ten bits the table
   * is indexed by. Shifting again collapsed the whole waveform onto four
   * points near zero, which is what made every voice inaudible. */
  const int q = phase & 0x3FF;
  int sign = 1;
  int table_idx;

  if (q < 256) {
    table_idx = q;
  } else if (q < 512) {
    table_idx = 511 - q;
  } else if (q < 768) {
    table_idx = q - 512;
    sign = -1;
  } else {
    table_idx = 1023 - q;
    sign = -1;
  }

  const int sine = quarter[table_idx & 255];
  const int level = (sine * (1023 - envelope)) >> 10;
  return (int16_t)(sign * level * 32);
}

void Ym3438::update_lfo()
{
  if (!m_lfo_enabled) {
    m_lfo_am_value = 0;
    m_lfo_pm_value = 0;
    return;
  }
  /* LFO counter: a 17-bit shift register in hardware.
   * LFO rate: 8 frequencies from master/(2^19 * 2^freq). We run the
   * counter at the sample rate (~44kHz) and derive the LFO period
   * from that. */
  m_lfo_counter++;
  const uint32_t phase = (m_lfo_counter >> (17 - (m_lfo_freq & 7))) & 0x3FFFF;

  /* PM LFO: signed triangular/sine approximation.
   * Output range: approximately -128..127 */
  const uint32_t pm_phase = (phase >> 5) & 0x1FFF; /* 13-bit */
  if (pm_phase < 1024) {
    m_lfo_pm_value = (int8_t)(pm_phase >> 3); /* 0..127 */
  } else if (pm_phase < 2048) {
    m_lfo_pm_value = (int8_t)(255 - (pm_phase >> 3)); /* 127..-128 */
  } else if (pm_phase < 3072) {
    m_lfo_pm_value = -(int8_t)((pm_phase - 2048) >> 3);
  } else {
    m_lfo_pm_value = (int8_t)(255 - ((pm_phase - 2048) >> 3));
  }

  /* AM LFO: 4-bit counter from the PM LFO's high bits.
   * Output range: 0-255 (0 = no attenuation) */
  m_lfo_am_value = (uint8_t)(pm_phase >> 5) & 0xFF;
}

int32_t Ym3438::apply_feedback(int ch)
{
  const Channel &chan = m_channels[ch];
  if (chan.feedback == 0) {
    return 0;
  }
  /* Feedback: (op1_output[n-1] + op1_output[n-2]) >> (6 - fb) */
  const int32_t sum = chan.feedback_hist[0] + chan.feedback_hist[1];
  return sum >> (7 - chan.feedback);
}

int32_t Ym3438::apply_operator(int ch, int op, int32_t mod)
{
  Operator &o = m_ops[ch * 4 + op];
  const Channel &chan = m_channels[ch];

  /* Effective phase: base + modulation (feedback or previous operator) */
  uint32_t phase = o.phase >> 10; /* 10-bit phase for the sine table */

  /* Apply phase modulation (PM LFO for operators with PMS > 0) */
  if (chan.pms > 0 && m_lfo_enabled) {
    const int32_t pm = m_lfo_pm_value * chan.pms;
    phase = (phase + (uint32_t)(pm >> 1)) & 0x3FF;
  }

  /* Apply amplitude modulation (AMS LFO) */
  uint32_t env = o.eg_level + ((uint32_t)o.eg_total_level << 3);
  if (chan.ams > 0 && m_lfo_enabled) {
    const uint32_t am = ((uint32_t)m_lfo_am_value * chan.ams) >> 2;
    env += am;
  }
  if (env > 0x3FF) {
    env = 0x3FF;
  }

  /* Apply the phase modulation from feedback or previous operator */
  phase = (phase + (uint32_t)(mod >> 5)) & 0x3FF;

  return sine_table(phase, env);
}

int16_t Ym3438::calculate_output()
{
  int32_t mix_l = 0;
  int32_t mix_r = 0;

  for (int ch = 0; ch < 6; ch++) {
    const Channel &chan = m_channels[ch];
    const int alg = chan.algorithm & 7;

    /* The eight algorithms differ only in who modulates whom and which
     * operators reach the output. Slots have to be evaluated in
     * dependency order, and a modulator's output becomes the next
     * operator's phase offset — without that the chip is just a bank of
     * independent sine waves and every patch loses its timbre.
     *
     * Registers number the slots S1, S3, S2, S4, so the algorithm graph
     * (written below in S1..S4 terms) indexes through this map. */
    static const int kSlot[4] = {0, 2, 1, 3}; /* S1, S2, S3, S4 */
    int32_t s1 = 0, s2 = 0, s3 = 0, s4 = 0;
    s1 = apply_operator(ch, kSlot[0], apply_feedback(ch));

    int32_t chan_out = 0;
    switch (alg) {
    case 0: /* S1 -> S2 -> S3 -> S4 */
      s2 = apply_operator(ch, kSlot[1], s1);
      s3 = apply_operator(ch, kSlot[2], s2);
      s4 = apply_operator(ch, kSlot[3], s3);
      chan_out = s4;
      break;
    case 1: /* (S1 + S2) -> S3 -> S4 */
      s2 = apply_operator(ch, kSlot[1], 0);
      s3 = apply_operator(ch, kSlot[2], s1 + s2);
      s4 = apply_operator(ch, kSlot[3], s3);
      chan_out = s4;
      break;
    case 2: /* S1 and (S2 -> S3) both modulate S4 */
      s2 = apply_operator(ch, kSlot[1], 0);
      s3 = apply_operator(ch, kSlot[2], s2);
      s4 = apply_operator(ch, kSlot[3], s1 + s3);
      chan_out = s4;
      break;
    case 3: /* (S1 -> S2) and S3 both modulate S4 */
      s2 = apply_operator(ch, kSlot[1], s1);
      s3 = apply_operator(ch, kSlot[2], 0);
      s4 = apply_operator(ch, kSlot[3], s2 + s3);
      chan_out = s4;
      break;
    case 4: /* two independent pairs: S1 -> S2, S3 -> S4 */
      s2 = apply_operator(ch, kSlot[1], s1);
      s3 = apply_operator(ch, kSlot[2], 0);
      s4 = apply_operator(ch, kSlot[3], s3);
      chan_out = s2 + s4;
      break;
    case 5: /* S1 modulates S2, S3 and S4 in parallel */
      s2 = apply_operator(ch, kSlot[1], s1);
      s3 = apply_operator(ch, kSlot[2], s1);
      s4 = apply_operator(ch, kSlot[3], s1);
      chan_out = s2 + s3 + s4;
      break;
    case 6: /* S1 -> S2, with S3 and S4 standing alone */
      s2 = apply_operator(ch, kSlot[1], s1);
      s3 = apply_operator(ch, kSlot[2], 0);
      s4 = apply_operator(ch, kSlot[3], 0);
      chan_out = s2 + s3 + s4;
      break;
    default: /* 7: all four straight to the output */
      s2 = apply_operator(ch, kSlot[1], 0);
      s3 = apply_operator(ch, kSlot[2], 0);
      s4 = apply_operator(ch, kSlot[3], 0);
      chan_out = s1 + s2 + s3 + s4;
      break;
    }

    /* Feedback is taken from S1's own output, averaged over two samples */
    m_channels[ch].feedback_hist[1] = m_channels[ch].feedback_hist[0];
    m_channels[ch].feedback_hist[0] = s1;

    /* Apply panning */
    if (chan.pan_left) {
      mix_l += chan_out;
    }
    if (chan.pan_right) {
      mix_r += chan_out;
    }
  }

  /* DAC mode: channel 6 outputs the DAC sample instead of FM */
  if (m_dac_enabled) {
    mix_l =
        mix_l - (m_channels[5].pan_left ? m_channels[5].feedback_hist[0] : 0);
    mix_r =
        mix_r - (m_channels[5].pan_right ? m_channels[5].feedback_hist[0] : 0);
    mix_l += m_dac_output;
    mix_r += m_dac_output;
  }

  m_sample_l = (int16_t)(mix_l / 3);
  m_sample_r = (int16_t)(mix_r / 3);
  return m_sample_l;
}

void Ym3438::update_operators()
{
  /* The envelope generator runs at a third of the operator rate. The
   * counter has to actually count: the old shift-register form started
   * at zero and every term stayed zero, so no envelope ever moved. */
  if (m_eg_prescaler >= 2) {
    m_eg_prescaler = 0;
    m_eg_timer++;
  } else {
    m_eg_prescaler++;
  }

  /* Update LFO */
  update_lfo();

  /* Read per-channel registers: algorithm/feedback from $B0, pan/PMS/AMS
   * from $B4 */
  for (int ch = 0; ch < 6; ch++) {
    const int bank = ch / 3;
    const int slot = ch % 3;
    const uint8_t alg_fb = m_regs[bank][0xB0 + slot];
    m_channels[ch].algorithm = alg_fb & 0x07;
    m_channels[ch].feedback = (alg_fb >> 3) & 0x07;

    const uint8_t pan_ams = m_regs[bank][0xB4 + slot];
    m_channels[ch].pan_left = (pan_ams >> 7) & 1;
    m_channels[ch].pan_right = (pan_ams >> 6) & 1;
    m_channels[ch].pms = pan_ams & 0x07;
    m_channels[ch].ams = (pan_ams >> 3) & 0x03;
  }

  /* Read LFO settings from $22 */
  m_lfo_enabled = (m_regs[0][0x22] & 0x08) != 0;
  m_lfo_freq = m_regs[0][0x22] & 0x07;

  for (int ch = 0; ch < 6; ch++) {
    for (int op = 0; op < 4; op++) {
      update_phase(ch, op);
      update_envelope(ch, op);
    }
  }
  calculate_output();
}

}  // namespace generator
