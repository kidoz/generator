/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ym3438.hpp"

#include <algorithm>

namespace generator {

namespace {

/* ---------------- output domain ----------------
 * The operator output stage works in the logarithmic domain the way the
 * chip does: the sine is stored as attenuation, the envelope adds to it,
 * and one exponential lookup turns the total back into a level.
 * Attenuation units are the OPN2 envelope steps of about 0.09375 dB; the
 * envelope generator accumulates in 16.16 fixed point of the same unit. */
constexpr int32_t kMaxAttenuation = (1024 << 16) - 1;
constexpr int32_t kMinAttenuation = (1 << 16) - 1;
constexpr int32_t kSsgThreshold = 512 << 16;
/* Past this envelope total the exponential lookup reaches zero; the
 * operator output can be skipped outright (the OPN2 cores do the same).
 * Values include the total level, so compare after it is added. */
constexpr int32_t kEgQuiet = 832;

/* SSG-EG control bits ($90-$9F low nibble). */
constexpr uint8_t kSsgHold = 0x01;
constexpr uint8_t kSsgAlternate = 0x02;
constexpr uint8_t kSsgAttack = 0x04;
constexpr uint8_t kSsgEnable = 0x08;

/* status bits: (busy << 7) | (timer B << 1) | timer A. The overflow
 * flags live at bits 0-1, NOT bits 5-6 as some docs say; GEMS-era
 * drivers poll bit 0 for Timer A and AND #2 for Timer B. */
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

/* Timer A counts output samples: one tick per 144 chip clocks, the same
 * period the operator pipeline runs on, so NA = 0 is 1024 samples
 * (19.2 ms). Timer B ticks once per sixteen samples. Billing a tick at
 * 12 chip clocks instead ran both twelve times too fast — and drivers
 * that pace themselves by polling the overflow flags, which is most of
 * them, play at whatever speed the flag comes up. */
constexpr int kMclkPerFmClock = 7;
constexpr int64_t kTimerATick = 144 * kMclkPerFmClock;
constexpr int64_t kTimerBTick = 16 * kTimerATick;
/* Busy: ~17 FM clock cycles after a register write. */
constexpr uint32_t kBusyMclk = 17 * kMclkPerFmClock;

int64_t timer_a_period(uint8_t hi, uint8_t lo)
{
  /* $24 carries NA bits 9-2 and $25 the low two. */
  const int na = (int(hi) << 2) | (lo & 3);
  return (int64_t)(1024 - na) * kTimerATick;
}

int64_t timer_b_period(uint8_t val)
{
  return (int64_t)(256 - val) * kTimerBTick;
}

/* ---------------- compile-time tables ---------------- */

struct FmTables {
  uint16_t logsin[256]; /* quarter wave, -log2(sin) << 8 */
  uint16_t pow[256];    /* 2^-(x/256), scaled to the 14-bit operator range */
};

/* Precomputed using the original formulas, for i = 0..255:
 *   logsin[i] = lround(-log2(sin((i + 0.5) * pi / 512)) * 256)
 *   pow[i]    = lround(exp2(-i / 256.0) * 8192)
 * These math functions are not constexpr in C++23 on Clang. Storing the
 * integer results preserves the existing table values across compilers
 * without depending on runtime libm initialization. */
inline constexpr FmTables kFm = {
    {
        0x0859, 0x06C3, 0x0607, 0x058B, 0x052E, 0x04E4, 0x04A6, 0x0471, 0x0443,
        0x041A, 0x03F5, 0x03D3, 0x03B5, 0x0398, 0x037E, 0x0365, 0x034E, 0x0339,
        0x0324, 0x0311, 0x02FF, 0x02ED, 0x02DC, 0x02CD, 0x02BD, 0x02AF, 0x02A0,
        0x0293, 0x0286, 0x0279, 0x026D, 0x0261, 0x0256, 0x024B, 0x0240, 0x0236,
        0x022C, 0x0222, 0x0218, 0x020F, 0x0206, 0x01FD, 0x01F5, 0x01EC, 0x01E4,
        0x01DC, 0x01D4, 0x01CD, 0x01C5, 0x01BE, 0x01B7, 0x01B0, 0x01A9, 0x01A2,
        0x019B, 0x0195, 0x018F, 0x0188, 0x0182, 0x017C, 0x0177, 0x0171, 0x016B,
        0x0166, 0x0160, 0x015B, 0x0155, 0x0150, 0x014B, 0x0146, 0x0141, 0x013C,
        0x0137, 0x0133, 0x012E, 0x0129, 0x0125, 0x0121, 0x011C, 0x0118, 0x0114,
        0x010F, 0x010B, 0x0107, 0x0103, 0x00FF, 0x00FB, 0x00F8, 0x00F4, 0x00F0,
        0x00EC, 0x00E9, 0x00E5, 0x00E2, 0x00DE, 0x00DB, 0x00D7, 0x00D4, 0x00D1,
        0x00CD, 0x00CA, 0x00C7, 0x00C4, 0x00C1, 0x00BE, 0x00BB, 0x00B8, 0x00B5,
        0x00B2, 0x00AF, 0x00AC, 0x00A9, 0x00A7, 0x00A4, 0x00A1, 0x009F, 0x009C,
        0x0099, 0x0097, 0x0094, 0x0092, 0x008F, 0x008D, 0x008A, 0x0088, 0x0086,
        0x0083, 0x0081, 0x007F, 0x007D, 0x007A, 0x0078, 0x0076, 0x0074, 0x0072,
        0x0070, 0x006E, 0x006C, 0x006A, 0x0068, 0x0066, 0x0064, 0x0062, 0x0060,
        0x005E, 0x005C, 0x005B, 0x0059, 0x0057, 0x0055, 0x0053, 0x0052, 0x0050,
        0x004E, 0x004D, 0x004B, 0x004A, 0x0048, 0x0046, 0x0045, 0x0043, 0x0042,
        0x0040, 0x003F, 0x003E, 0x003C, 0x003B, 0x0039, 0x0038, 0x0037, 0x0035,
        0x0034, 0x0033, 0x0031, 0x0030, 0x002F, 0x002E, 0x002D, 0x002B, 0x002A,
        0x0029, 0x0028, 0x0027, 0x0026, 0x0025, 0x0024, 0x0023, 0x0022, 0x0021,
        0x0020, 0x001F, 0x001E, 0x001D, 0x001C, 0x001B, 0x001A, 0x0019, 0x0018,
        0x0017, 0x0017, 0x0016, 0x0015, 0x0014, 0x0014, 0x0013, 0x0012, 0x0011,
        0x0011, 0x0010, 0x000F, 0x000F, 0x000E, 0x000D, 0x000D, 0x000C, 0x000C,
        0x000B, 0x000A, 0x000A, 0x0009, 0x0009, 0x0008, 0x0008, 0x0007, 0x0007,
        0x0007, 0x0006, 0x0006, 0x0005, 0x0005, 0x0005, 0x0004, 0x0004, 0x0004,
        0x0003, 0x0003, 0x0003, 0x0002, 0x0002, 0x0002, 0x0002, 0x0001, 0x0001,
        0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000,
    },
    {
        0x2000, 0x1FEA, 0x1FD4, 0x1FBE, 0x1FA8, 0x1F92, 0x1F7C, 0x1F66, 0x1F50,
        0x1F3B, 0x1F25, 0x1F10, 0x1EFA, 0x1EE5, 0x1ECF, 0x1EBA, 0x1EA5, 0x1E8F,
        0x1E7A, 0x1E65, 0x1E50, 0x1E3B, 0x1E26, 0x1E11, 0x1DFD, 0x1DE8, 0x1DD3,
        0x1DBE, 0x1DAA, 0x1D95, 0x1D81, 0x1D6C, 0x1D58, 0x1D44, 0x1D30, 0x1D1B,
        0x1D07, 0x1CF3, 0x1CDF, 0x1CCB, 0x1CB7, 0x1CA3, 0x1C8F, 0x1C7C, 0x1C68,
        0x1C54, 0x1C41, 0x1C2D, 0x1C1A, 0x1C06, 0x1BF3, 0x1BDF, 0x1BCC, 0x1BB9,
        0x1BA6, 0x1B93, 0x1B7F, 0x1B6C, 0x1B59, 0x1B47, 0x1B34, 0x1B21, 0x1B0E,
        0x1AFB, 0x1AE9, 0x1AD6, 0x1AC3, 0x1AB1, 0x1A9E, 0x1A8C, 0x1A7A, 0x1A67,
        0x1A55, 0x1A43, 0x1A31, 0x1A1E, 0x1A0C, 0x19FA, 0x19E8, 0x19D6, 0x19C5,
        0x19B3, 0x19A1, 0x198F, 0x197E, 0x196C, 0x195A, 0x1949, 0x1937, 0x1926,
        0x1914, 0x1903, 0x18F2, 0x18E0, 0x18CF, 0x18BE, 0x18AD, 0x189C, 0x188B,
        0x187A, 0x1869, 0x1858, 0x1847, 0x1836, 0x1826, 0x1815, 0x1804, 0x17F4,
        0x17E3, 0x17D2, 0x17C2, 0x17B1, 0x17A1, 0x1791, 0x1780, 0x1770, 0x1760,
        0x1750, 0x1740, 0x1730, 0x171F, 0x170F, 0x16FF, 0x16F0, 0x16E0, 0x16D0,
        0x16C0, 0x16B0, 0x16A1, 0x1691, 0x1681, 0x1672, 0x1662, 0x1653, 0x1643,
        0x1634, 0x1624, 0x1615, 0x1606, 0x15F7, 0x15E7, 0x15D8, 0x15C9, 0x15BA,
        0x15AB, 0x159C, 0x158D, 0x157E, 0x156F, 0x1560, 0x1552, 0x1543, 0x1534,
        0x1525, 0x1517, 0x1508, 0x14FA, 0x14EB, 0x14DD, 0x14CE, 0x14C0, 0x14B1,
        0x14A3, 0x1495, 0x1487, 0x1478, 0x146A, 0x145C, 0x144E, 0x1440, 0x1432,
        0x1424, 0x1416, 0x1408, 0x13FA, 0x13EC, 0x13DF, 0x13D1, 0x13C3, 0x13B5,
        0x13A8, 0x139A, 0x138D, 0x137F, 0x1372, 0x1364, 0x1357, 0x1349, 0x133C,
        0x132F, 0x1321, 0x1314, 0x1307, 0x12FA, 0x12ED, 0x12E0, 0x12D3, 0x12C5,
        0x12B8, 0x12AC, 0x129F, 0x1292, 0x1285, 0x1278, 0x126B, 0x125F, 0x1252,
        0x1245, 0x1238, 0x122C, 0x121F, 0x1213, 0x1206, 0x11FA, 0x11ED, 0x11E1,
        0x11D5, 0x11C8, 0x11BC, 0x11B0, 0x11A3, 0x1197, 0x118B, 0x117F, 0x1173,
        0x1167, 0x115B, 0x114F, 0x1143, 0x1137, 0x112B, 0x111F, 0x1113, 0x1107,
        0x10FB, 0x10F0, 0x10E4, 0x10D8, 0x10CD, 0x10C1, 0x10B5, 0x10AA, 0x109E,
        0x1093, 0x1087, 0x107C, 0x1070, 0x1065, 0x105A, 0x104E, 0x1043, 0x1038,
        0x102D, 0x1021, 0x1016, 0x100B,
    },
};

/* Envelope rate deltas, 16.16 fixed point of attenuation per sample,
 * indexed 32+rate-code with the rate code = 2*rate + ksr (release uses
 * 4*rate + 2 + ksr). Entries below code 2 are zero: rate 0 freezes the
 * envelope. Rates are averaged per native FM sample; an additional
 * 39/48 factor would slow the decay relative to the reference chip.
 * Discrete envelope step timing is not represented by this table. */
consteval std::array<uint32_t, 128> make_eg_rate_table()
{
  std::array<uint32_t, 128> table{};
  for (int code = 2; code < 64; code++) {
    double rate = 1.0;               /* chip clocks at native rate */
    rate *= 1.0 + (code & 3) * 0.25; /* low bits: x1 .. x1.75 */
    rate *= 1 << (code >> 2);        /* high bits: shift */
    rate /= 12.0 * 1024.0;           /* native sample rate */
    rate *= double(1u << 16);        /* 16.16 */
    table[32 + code] = uint32_t(rate);
  }
  /* Codes above 63 saturate at the fastest rate. */
  for (int i = 0; i < 32; i++) {
    table[96 + i] = table[32 + 63];
  }
  return table;
}

inline constexpr auto kEgRate = make_eg_rate_table();

/* Sustain level in attenuation units: 32 envelope units per 3 dB step;
 * the all-ones code selects 31 steps, near-silence. */
consteval std::array<int32_t, 16> make_sustain_level_table()
{
  std::array<int32_t, 16> table{};
  for (int i = 0; i < 15; i++) {
    table[i] = int32_t(uint32_t(32 * i) << 16);
  }
  table[15] = int32_t(uint32_t(32 * 31) << 16);
  return table;
}

inline constexpr auto kSustainLevel = make_sustain_level_table();

/* DT1 phase-increment offsets, indexed [detune & 3][key code]; the
 * detune register bit 2 negates the offset. These are the canonical
 * OPN2 values shared by the hardware-verified cores. */
constexpr uint8_t kDetuneOffset[4][32] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
     2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8},
    {1, 1, 1, 1, 2, 2, 2, 2,  2,  3,  3,  3,  4,  4,  4,  5,
     5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16},
    {2, 2, 2, 2,  2,  3,  3,  3,  4,  4,  4,  5,  5,  6,  6,  7,
     8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22}};

/* The F-number's top bits fold into a two-bit note index for the key
 * code, which drives both detune and key scaling. */
constexpr uint8_t kFnumToNote[16] = {0, 0, 0, 0, 0, 0, 0, 1,
                                     2, 3, 3, 3, 3, 3, 3, 3};

/* LFO waveform: one triangle over 512 entries, 16.16 depth. */
consteval std::array<int32_t, 512> make_lfo_wave()
{
  std::array<int32_t, 512> wave{};
  for (int i = 0; i < 512; i++) {
    wave[i] = i < 256 ? i * 0x10000 / 256 : (512 - i) * 0x10000 / 256;
  }
  return wave;
}

inline constexpr auto kLfoWave = make_lfo_wave();

/* LFO phase increments per sample for the eight rates, 3.98 Hz to
 * 72.2 Hz at the 8 MHz reference (the rates the manuals quote). */
consteval std::array<uint32_t, 8> make_lfo_rate_incr()
{
  constexpr double kHertz[8] = {3.98, 5.56, 6.02, 6.37, 6.88, 9.63, 48.1, 72.2};
  constexpr double kReference = 8000000.0 / 144;
  std::array<uint32_t, 8> table{};
  for (int i = 0; i < 8; i++) {
    table[i] = uint32_t(512.0 * 8388608.0 * kHertz[i] / kReference);
  }
  return table;
}

inline constexpr auto kLfoRateIncr = make_lfo_rate_incr();

/* PMS names a vibrato depth in cents at the top of the LFO swing. */
constexpr double kPmdCents[8] = {0, 3.4, 6.7, 10, 14, 20, 40, 80};
/* AMS picks a tremolo depth in attenuation units: 0, 1.4, 5.9 or
 * 11.9 dB. */
constexpr uint32_t kAmdDepth[4] = {0, 15, 63, 127};

/* $28 key-on bit N (N = 4..7) gates the operator in register-slot
 * order 0, 2, 1, 3 — the chip's internal slot numbering is S1, S3, S2,
 * S4, and the key-on bits follow the internal order, not the register
 * address order. */
constexpr int kKeyBitSlot[4] = {0, 2, 1, 3};

}  // namespace

/* ------------------------------------------------------------------ */
/* bus interface                                                       */
/* ------------------------------------------------------------------ */

void Ym3438::reset()
{
  m_regs = {};
  m_latch_addr[0] = m_latch_addr[1] = 0;
  m_channels = {};
  m_status = 0;
  m_busy_mclk = 0;
  m_timer_a = 0;
  m_timer_b = 0;
  m_irq = false;
  m_lfo_count = 0;
  m_lfo_incr = 0;
  m_lfo_am = 0;
  m_lfo_pm = 0;
  m_dac_enabled = false;
  m_dac_output = 0;
  m_sample_l = 0;
  m_sample_r = 0;
  m_sample_timer = 0;
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
  m_status |= kStBusy;
}

uint8_t Ym3438::read_data() const
{
  return 0xFF; /* data port reads float high on real hardware */
}

/* ------------------------------------------------------------------ */
/* register decode                                                     */
/* ------------------------------------------------------------------ */

void Ym3438::write_register(uint8_t bank, uint8_t addr, uint8_t data)
{
  const uint8_t prev = m_regs[bank][addr];
  m_regs[bank][addr] = data;

  if (bank == 0 && addr == 0x27) {
    /* Mode register: the load bits are level-sensitive run/stop
     * controls, not restart strobes. While one stays set its counter
     * free-runs, and only a 0->1 edge (re)loads it — a driver that
     * rewrites $27 each tick must not push the counter back to the
     * top, or the tempo drags by the driver's own processing time.
     * The flag-reset bits are momentary: they clear the flag on this
     * write only, and a later overflow can set it again. */
    if ((data & kModeLoadA) != 0 && (prev & kModeLoadA) == 0) {
      m_timer_a = timer_a_period(m_regs[0][0x24], m_regs[0][0x25]);
    }
    if ((data & kModeLoadB) != 0 && (prev & kModeLoadB) == 0) {
      m_timer_b = timer_b_period(m_regs[0][0x26]);
    }
    if ((data & kModeResetFlagA) != 0) {
      m_status &= uint8_t(~kStTimerA);
    }
    if ((data & kModeResetFlagB) != 0) {
      m_status &= uint8_t(~kStTimerB);
    }
    update_irq();
    return;
  }
  if (bank == 0 && addr == 0x22) {
    /* LFO: bit 3 enables, bits 0-2 select the rate. */
    m_lfo_incr = (data & 0x08) != 0 ? kLfoRateIncr[data & 7] : 0;
    return;
  }
  if (bank == 0 && addr == 0x2A) {
    /* The DAC output replaces channel 6's slot in the mix, at the same
     * scale as an operator sum: 8-bit unsigned around 0x80, shifted
     * into the 13-bit operator range. */
    m_dac_output = (int(data) - 0x80) << 5;
    return;
  }
  if (bank == 0 && addr == 0x2B) {
    m_dac_enabled = (data & 0x80) != 0;
    return;
  }
  if (bank == 0 && addr == 0x28) {
    /* Key on/off: bits 7-4 gate the four operators (in internal slot
     * order), bits 2-0 pick the channel — bit 2 moves the range to
     * channels 4-6, and channel index 3 is only valid there. */
    const int ch = (data & 0x03) + ((data & 0x04) != 0 ? 3 : 0);
    if ((data & 0x03) == 3 && (data & 0x04) == 0) {
      return;
    }
    for (int bit = 0; bit < 4; bit++) {
      const bool on = (data & (0x10 << bit)) != 0;
      if (on != m_channels[ch].slots[kKeyBitSlot[bit]].keyed) {
        key_set(ch, kKeyBitSlot[bit], on);
      }
    }
    return;
  }

  if (addr < 0x30 || addr >= 0xB8) {
    return;
  }

  /* Operator/channel registers: the channel is bits 1-0 (bank moves it
   * to 3-5), the operator bits 3-2. Row 3 of bank 1 has no channel —
   * drivers do write it, and the hardware ignores it. */
  if ((addr & 3) == 3 && bank != 0) {
    return;
  }
  const int ch = (addr & 3) + (bank != 0 ? 3 : 0);
  const int op = (addr >> 2) & 3;
  Channel &chan = m_channels[ch];
  Slot &slot = chan.slots[op];

  switch (addr & 0xF0) {
  case 0x30:
    /* DT1/MUL: changes the phase increment. */
    chan.pitch_dirty = true;
    break;
  case 0x40:
    /* Total level. */
    slot.total_level = int32_t(data & 0x7F) << 3;
    break;
  case 0x50:
    /* RS/AR: the key-scale part shifts the key code into the rate
     * lookup, the attack part feeds it. */
    refresh_rates(ch, op);
    break;
  case 0x60:
    /* AM-enable bit 7 gates this operator's LFO tremolo; the rest is
     * the decay rate. */
    slot.am_enabled = (data & 0x80) != 0;
    slot.am_depth = slot.am_enabled ? chan.ams : 0;
    refresh_rates(ch, op);
    break;
  case 0x70:
    refresh_rates(ch, op);
    break;
  case 0x80:
    /* Sustain level (bits 7-4) and release rate (bits 3-0). */
    refresh_rates(ch, op);
    break;
  case 0x90:
    /* SSG-EG control. */
    slot.ssg_control = data & 0x0F;
    break;
  case 0xA0:
    /* F-number / block. */
    chan.pitch_dirty = true;
    break;
  case 0xB0:
    if (op == 0) {
      /* FB/ALGO ($B0-$B2). */
      chan.algorithm = data & 0x07;
      chan.feedback = (data >> 3) & 0x07;
    } else if (op == 1) {
      /* Pan bits 7-6, AMS bits 5-4, PMS bits 2-0 ($B4-$B6); the class
       * addresses four channels, so it lands on operator index 1. */
      chan.pan_left = (data >> 7) & 1;
      chan.pan_right = (data >> 6) & 1;
      chan.pms = uint8_t(1.5 / 1200.0 * kPmdCents[data & 7] * 1024.0);
      chan.ams = kAmdDepth[(data >> 4) & 0x03];
      for (auto &s : chan.slots) {
        s.am_depth = s.am_enabled ? chan.ams : 0;
      }
    }
    break;
  default:
    break;
  }
}

/* ------------------------------------------------------------------ */
/* channel state refresh                                               */
/* ------------------------------------------------------------------ */

void Ym3438::refresh_pitch(int ch)
{
  Channel &chan = m_channels[ch];
  const int bank = ch / 3;
  const int slot_index = ch % 3;
  /* Multi-frequency mode ($27 bit 6): channel 3's operators 3 and 4
   * take their pitch from their own registers instead of the channel's
   * ($A6/$A7 and $AD/$AE), $22 is the LFO register and never was. */
  const bool ch3_special = ch == 2 && (m_regs[0][0x27] & 0x40) != 0;

  for (int op = 0; op < 4; op++) {
    Slot &slot = chan.slots[op];
    uint8_t fnum_lo;
    uint8_t fnum_hi;
    if (ch3_special && op >= 2) {
      fnum_lo = m_regs[0][0xA6 + (op - 2)];
      fnum_hi = m_regs[0][0xAD + (op - 2)];
    } else {
      fnum_lo = m_regs[bank][0xA0 + slot_index];
      fnum_hi = m_regs[bank][0xA4 + slot_index];
    }
    const uint16_t fnum = uint16_t(fnum_lo | ((fnum_hi & 0x07) << 8));
    const uint8_t block = uint8_t((fnum_hi >> 3) & 0x07);
    chan.key_code = uint8_t((block << 2) | kFnumToNote[(fnum >> 7) & 0x0F]);

    /* 17-bit phase increment: fnum x 2^(block-1). The detune offset is
     * added inside the same 17-bit range — the hardware folds it on
     * overflow (what keeps GEMS-era detune in tune) — and the
     * multiple rescales it; multiple 0 halves instead of silencing. */
    int32_t fc = int32_t((uint32_t(fnum) << block) >> 1);
    const uint8_t dt_mul = m_regs[bank][uint8_t(0x30 + op * 4 + slot_index)];
    const int dt_code = (dt_mul >> 4) & 0x07;
    const int dt = kDetuneOffset[dt_code & 3][chan.key_code];
    fc += (dt_code & 4) != 0 ? -dt : dt;
    fc &= 0x1FFFF;
    const int mul = dt_mul & 0x0F;
    slot.incr = mul == 0 ? fc >> 1 : fc * mul;
    refresh_rates(ch, op);
  }
  chan.pitch_dirty = false;
}

void Ym3438::refresh_rates(int ch, int op)
{
  const int bank = ch / 3;
  const int slot_index = ch % 3;
  Slot &slot = m_channels[ch].slots[op];

  /* Key-scale value: the same key code the phase generator derives,
   * right-shifted by the RS setting (its encoding is inverted). */
  const uint8_t rs_ar = m_regs[bank][uint8_t(0x50 + op * 4 + slot_index)];
  const int rs = (rs_ar >> 6) & 0x03;
  const uint8_t ksr = m_channels[ch].key_code >> (3 - rs);
  slot.key_scale = ksr;

  const int ar = rs_ar & 0x1F;
  const int arval = ar != 0 ? 32 + (ar << 1) : 0;
  /* Near the top of the scale the attack completes within one sample:
   * the delta saturates past the whole range. */
  slot.delta_attack = (arval + ksr < 32 + 62) ? int32_t(kEgRate[arval + ksr])
                                              : kMaxAttenuation + 1;

  const int dr = m_regs[bank][uint8_t(0x60 + op * 4 + slot_index)] & 0x1F;
  slot.delta_decay = dr != 0 ? int32_t(kEgRate[32 + (dr << 1) + ksr]) : 0;

  const int sr = m_regs[bank][uint8_t(0x70 + op * 4 + slot_index)] & 0x1F;
  slot.delta_sustain = sr != 0 ? int32_t(kEgRate[32 + (sr << 1) + ksr]) : 0;

  const uint8_t sl_rr = m_regs[bank][uint8_t(0x80 + op * 4 + slot_index)];
  slot.sustain_level = kSustainLevel[sl_rr >> 4];
  const int rr = sl_rr & 0x0F;
  slot.delta_release = int32_t(kEgRate[34 + (rr << 2) + ksr]);
}

/* ------------------------------------------------------------------ */
/* envelope generator                                                  */
/* ------------------------------------------------------------------ */

void Ym3438::key_set(int ch, int op, bool on)
{
  Slot &slot = m_channels[ch].slots[op];
  if (on) {
    if (slot.keyed) {
      return;
    }
    slot.keyed = true;
    slot.phase = 0;
    /* SSG-EG starts each key-on cycle in the direction its attack bit
     * names; plain envelopes start open. A rising key-on resets the
     * operator phase. This sample-level reset approximates the reference
     * chip pipeline timing; an already-held key returns above. */
    slot.ssg_inverted = (slot.ssg_control & kSsgEnable) != 0 &&
                                (slot.ssg_control & kSsgAttack) != 0
                            ? uint8_t(1)
                            : uint8_t(0);
    slot.state = EgState::attack;
    return;
  }
  if (!slot.keyed) {
    return;
  }
  slot.keyed = false;
  if ((slot.ssg_control & kSsgEnable) != 0 && slot.ssg_inverted != 0) {
    /* De-invert around the SSG threshold; a level already past it is
     * effectively silent. */
    slot.volume = kSsgThreshold - slot.volume;
    if (slot.volume < 0) {
      slot.volume = 0;
    }
    slot.ssg_inverted = 0;
    if (slot.volume >= kSsgThreshold) {
      slot.state = EgState::off;
      return;
    }
  }
  if (slot.state > EgState::release) {
    slot.state = EgState::release;
  }
}

void Ym3438::ssg_cycle_complete(Slot &slot)
{
  if ((slot.ssg_control & kSsgHold) != 0) {
    if ((slot.ssg_control & kSsgAlternate) != 0) {
      slot.ssg_inverted ^= 1;
    }
    slot.volume = kSsgThreshold;
    slot.state = EgState::off;
    return;
  }
  if ((slot.ssg_control & kSsgAlternate) != 0) {
    slot.ssg_inverted ^= 1;
  } else {
    /* Non-alternate loops restart the phase generator; the envelope
     * volume is kept and the attack closes it from where it is. */
    slot.phase = 0;
  }
  slot.state = EgState::attack;
}

void Ym3438::envelope_step(Slot &slot)
{
  switch (slot.state) {
  case EgState::attack: {
    /* The attack is exponential: each step closes a fixed fraction of
     * what is left, so it is fast while quiet and eases in as it
     * approaches full level. */
    int32_t step = slot.volume;
    slot.volume -= slot.delta_attack;
    step = (step >> 16) - int32_t(uint32_t(slot.volume) >> 16);
    if (step > 0) {
      int32_t level = slot.volume + (step << 16);
      do {
        level -= (1 << 16) + ((level >> 4) & ~0xFFFF);
        if (level <= kMinAttenuation) {
          break;
        }
      } while (--step != 0);
      slot.volume = level;
    }
    if (slot.volume <= kMinAttenuation) {
      if (slot.volume < 0) {
        slot.volume = 0;
      }
      slot.state = EgState::decay;
    }
    break;
  }
  case EgState::decay:
    /* SSG-EG cycles complete at half scale, not at the sustain level. */
    if ((slot.ssg_control & kSsgEnable) != 0) {
      slot.volume += slot.delta_decay;
      if (slot.volume >= kSsgThreshold) {
        ssg_cycle_complete(slot);
      }
      break;
    }
    slot.volume += slot.delta_decay;
    if (slot.volume >= slot.sustain_level) {
      slot.volume = slot.sustain_level;
      slot.state = EgState::sustain;
    }
    break;
  case EgState::sustain:
    slot.volume += slot.delta_sustain;
    if ((slot.ssg_control & kSsgEnable) != 0) {
      if (slot.volume >= kSsgThreshold) {
        ssg_cycle_complete(slot);
      }
      break;
    }
    if (slot.volume > kMaxAttenuation) {
      slot.volume = kMaxAttenuation;
      slot.state = EgState::off;
    }
    break;
  case EgState::release:
    slot.volume += slot.delta_release;
    if (slot.volume > kMaxAttenuation) {
      slot.volume = kMaxAttenuation;
      slot.state = EgState::off;
    }
    break;
  case EgState::off:
    break;
  }
}

int32_t Ym3438::envelope_output(const Slot &slot) const
{
  uint32_t out;
  if ((slot.ssg_control & kSsgEnable) != 0 && slot.ssg_inverted != 0 &&
      slot.state != EgState::off) {
    /* Inverted SSG-EG reflects around the half-scale threshold; a
     * level already past it wraps loud-and-skipped, the way the
     * unsigned hardware domain behaves. */
    out = uint32_t(slot.total_level) +
          (uint32_t(kSsgThreshold - slot.volume) >> 16);
  } else {
    out = uint32_t(slot.total_level) + (uint32_t(slot.volume) >> 16);
  }
  if (slot.am_depth != 0) {
    out += slot.am_depth * m_lfo_am >> 16;
  }
  return int32_t(out);
}

/* ------------------------------------------------------------------ */
/* per-sample rendering                                                */
/* ------------------------------------------------------------------ */

int16_t Ym3438::operator_output(uint32_t phase, int32_t mod,
                                uint32_t attenuation) const
{
  /* The sine is indexed by the top ten bits of the 20-bit phase
   * accumulator; the modulation input arrives already in index units. */
  const uint32_t index = uint32_t(((int32_t(phase) >> 10) + mod) & 0x3FF);
  const uint32_t quarter =
      (index & 0x100) != 0 ? (~index) & 0xFF : index & 0xFF;
  /* The exponential table halves amplitude every 256 entries. One EG
   * unit is 1/64 of that, so its index shift is two. The older signed
   * table used a third bit to select +/- entries; keeping that bit here
   * doubles attenuation and changes both envelopes and FM timbre. */
  const uint32_t atten = uint32_t(kFm.logsin[quarter]) + (attenuation << 2);
  const uint32_t shift = atten >> 8;
  if (shift >= 14) {
    return 0; /* the exponential table has run out of range */
  }
  const int32_t level = int32_t(uint32_t(kFm.pow[atten & 0xFF]) >> shift);
  return int16_t((index & 0x200) != 0 ? -level : level);
}

void Ym3438::render_sample()
{
  int32_t mix_l = 0;
  int32_t mix_r = 0;

  for (int ch = 0; ch < 6; ch++) {
    Channel &chan = m_channels[ch];
    if (chan.pitch_dirty) {
      refresh_pitch(ch);
    }

    /* Advance the envelopes for every operator first, then evaluate
     * the algorithm graph: slots are S1, S3, S2, S4 in register order,
     * so the graph (written in S1..S4 terms) indexes through the map. */
    std::array<int32_t, 4> eg_out{};
    for (int op = 0; op < 4; op++) {
      envelope_step(chan.slots[op]);
      eg_out[op] = envelope_output(chan.slots[op]);
    }

    /* SLOT 1 carries its own two-sample delayed feedback. */
    const int32_t feedback_sum = chan.op1_history[0] + chan.op1_history[1];
    chan.op1_history[0] = chan.op1_history[1];
    chan.op1_history[1] = 0;
    const int32_t s1 =
        eg_out[0] < kEgQuiet
            ? operator_output(
                  chan.slots[0].phase,
                  chan.feedback != 0 ? feedback_sum >> (10 - chan.feedback) : 0,
                  uint32_t(eg_out[0]))
            : 0;
    chan.op1_history[1] = s1;

    auto voice = [&](int reg_slot, int32_t mod) -> int32_t {
      if (eg_out[reg_slot] >= kEgQuiet) {
        return 0;
      }
      return operator_output(chan.slots[reg_slot].phase, mod,
                             uint32_t(eg_out[reg_slot]));
    };

    int32_t s2 = 0;
    int32_t s3 = 0;
    int32_t s4 = 0;
    int32_t chan_out = 0;
    switch (chan.algorithm) {
    case 0: /* S1 -> S2 -> S3 -> S4 */
      s2 = voice(2, s1 >> 1);
      s3 = voice(1, s2 >> 1);
      s4 = voice(3, s3 >> 1);
      chan_out = s4;
      break;
    case 1: /* (S1 + S2) -> S3 -> S4 */
      s2 = voice(2, 0);
      s3 = voice(1, (s1 + s2) >> 1);
      s4 = voice(3, s3 >> 1);
      chan_out = s4;
      break;
    case 2: /* S1 and (S2 -> S3) both modulate S4 */
      s2 = voice(2, 0);
      s3 = voice(1, s2 >> 1);
      s4 = voice(3, (s1 + s3) >> 1);
      chan_out = s4;
      break;
    case 3: /* (S1 -> S2) and S3 both modulate S4 */
      s2 = voice(2, s1 >> 1);
      s3 = voice(1, 0);
      s4 = voice(3, (s2 + s3) >> 1);
      chan_out = s4;
      break;
    case 4: /* two independent pairs: S1 -> S2, S3 -> S4 */
      s2 = voice(2, s1 >> 1);
      s3 = voice(1, 0);
      s4 = voice(3, s3 >> 1);
      chan_out = s2 + s4;
      break;
    case 5: /* S1 modulates S2, S3 and S4 in parallel */
      s2 = voice(2, s1 >> 1);
      s3 = voice(1, s1 >> 1);
      s4 = voice(3, s1 >> 1);
      chan_out = s2 + s3 + s4;
      break;
    case 6: /* S1 -> S2, with S3 and S4 standing alone */
      s2 = voice(2, s1 >> 1);
      s3 = voice(1, 0);
      s4 = voice(3, 0);
      chan_out = s2 + s3 + s4;
      break;
    default: /* 7: all four straight to the output */
      s2 = voice(2, 0);
      s3 = voice(1, 0);
      s4 = voice(3, 0);
      chan_out = s1 + s2 + s3 + s4;
      break;
    }

    /* $2B bit 7 hands channel 6's slot in the output to the DAC. */
    if (ch == 5 && m_dac_enabled) {
      chan_out = m_dac_output;
    }

    if (chan.pan_left != 0) {
      mix_l += chan_out;
    }
    if (chan.pan_right != 0) {
      mix_r += chan_out;
    }

    /* Phase increments happen after the outputs. The LFO's vibrato
     * bends the increment, not the phase, so the wobble tracks the
     * note. */
    const int32_t pm_term = m_lfo_pm != 0 && chan.pms != 0
                                ? int32_t(int64_t(m_lfo_pm) * chan.pms >> 16)
                                : 0;
    for (int op = 0; op < 4; op++) {
      Slot &slot = chan.slots[op];
      int32_t incr = slot.incr;
      if (pm_term != 0) {
        incr += int32_t(int64_t(pm_term) * slot.incr / 1024);
      }
      slot.phase = (slot.phase + uint32_t(incr)) & 0xFFFFF;
    }
  }

  /* Six channels of 14-bit operator sums do not fit a 16-bit sample
   * without headroom; the /3 keeps the mixer's downstream contract. */
  m_sample_l = int16_t(std::clamp(mix_l / 3, -32768, 32767));
  m_sample_r = int16_t(std::clamp(mix_r / 3, -32768, 32767));

  /* The LFO steps after the sample, and its tremolo waveform is
   * inverted relative to the table while vibrato shares the phase at a
   * quarter of the stepping rate. */
  if (m_lfo_incr != 0) {
    m_lfo_count += m_lfo_incr;
    const uint32_t index = (m_lfo_count >> 23) & 511;
    m_lfo_am = 0x10000 - uint32_t(kLfoWave[index]);
    m_lfo_pm = kLfoWave[index & ~3u] - 0x8000;
  }
}

/* ------------------------------------------------------------------ */
/* clocking                                                            */
/* ------------------------------------------------------------------ */

bool Ym3438::advance_mclk(uint64_t ticks)
{
  /* busy countdown */
  if (m_busy_mclk > 0) {
    const uint32_t consume = (uint32_t)std::min<uint64_t>(m_busy_mclk, ticks);
    m_busy_mclk -= consume;
    if (m_busy_mclk == 0) {
      m_status &= uint8_t(~kStBusy);
    } else {
      m_status |= kStBusy;
    }
  }

  const uint8_t mode = m_regs[0][0x27];
  /* Load runs the counter; enable only decides whether an overflow may
   * raise the status flag. They are separate bits, and a driver is free
   * to run a timer it never flags. */
  const bool run_a = (mode & kModeLoadA) != 0;
  const bool run_b = (mode & kModeLoadB) != 0;
  const bool en_a = (mode & kModeEnableA) != 0;
  const bool en_b = (mode & kModeEnableB) != 0;

  /* Timer overflow sets the flag (sticky until the next $27 reset
   * write); CSM ($27 bit 7) retriggers channel 3 on every Timer A
   * overflow. */
  if (run_a && m_timer_a > 0) {
    m_timer_a -= (int64_t)ticks;
    /* One advance can span more than a whole period at the short end
     * of timer A's range, so make up whole periods until the counter
     * is positive again rather than dropping the extra overflows. */
    while (m_timer_a <= 0) {
      if (en_a) {
        m_status |= kStTimerA;
      }
      if ((mode & 0x80) != 0) {
        for (int op = 0; op < 4; op++) {
          key_set(2, kKeyBitSlot[op], false);
          key_set(2, kKeyBitSlot[op], true);
        }
      }
      m_timer_a += timer_a_period(m_regs[0][0x24], m_regs[0][0x25]);
    }
  }
  if (run_b && m_timer_b > 0) {
    m_timer_b -= (int64_t)ticks;
    while (m_timer_b <= 0) {
      if (en_b) {
        m_status |= kStTimerB;
      }
      m_timer_b += timer_b_period(m_regs[0][0x26]);
    }
  }

  update_irq();

  /* The chip produces one sample every 144 of its own clocks, and its
   * clock is the master divided by seven: 1008 master clocks. The
   * period sets the pitch of everything the chip plays. */
  m_sample_timer += ticks;
  while (m_sample_timer >= kMclkPerFmSample) {
    m_sample_timer -= kMclkPerFmSample;
    render_sample();
  }

  return m_irq;
}

void Ym3438::update_irq()
{
  const uint8_t mode = m_regs[0][0x27];
  m_irq = ((m_status & kStTimerA) != 0 && (mode & 0x40) != 0) ||
          ((m_status & kStTimerB) != 0 && (mode & 0x80) != 0);
}

}  // namespace generator
