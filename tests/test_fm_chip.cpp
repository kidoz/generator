// End-to-end characterization of the YM2612 core. Unlike test_fm_eg (envelope
// math) and test_fm_lfo (LFO stepping), this drives the whole chip through
// its instance API and pins the rendered waveform with an FNV-1a hash. It
// exists because the splash-cart
// audio fingerprint never touches the FM registers: this test is the
// regression net for changes to the chip's internal wiring (channel
// connection pointers, the FM_OPN work area, the per-chip LFO cache).
//
// fm.cpp (plus the extracted fm_eg/fm_lfo units) is compiled directly into
// this test; the only external symbol the core references (state_transfer*)
// is stubbed below, mirroring tests/test_sn76496.cpp.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "state.h"
#include "ym2612.hpp"

// Save-state plumbing stubs: Ym2612::save_state() is not exercised here.
void state_transfer8(const char *, const char *, uint8, uint8 *, uint32)
{
}
void state_transfer16(const char *, const char *, uint8, uint16 *, uint32)
{
}
void state_transfer32(const char *, const char *, uint8, uint32 *, uint32)
{
}

// Timer/IRQ handlers: installed but never expected to fire in this test.
static int g_timer_calls = 0;
static int g_irq_calls = 0;
static void dummy_timer_handler(int, int, int, double)
{
  g_timer_calls++;
}
static void dummy_irq_handler(int, int)
{
  g_irq_calls++;
}

namespace {

constexpr int FM_CLOCK = 7670454;  // NTSC master / 7 (YM2612 input clock)
constexpr int FM_RATE = 48000;     // matches the default build's output rate
constexpr int OUT_SAMPLES = 240;   // one scanline's worth of output

uint64_t fnv1a64(const void *data, size_t len)
{
  auto *p = static_cast<const uint8_t *>(data);
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

// Register write to port 0 (address, then data) - same sequence the FM smoke
// ROM performs: one loud carrier (algorithm 7), fast attack/decay, LFO on.
void write_reg(generator::Ym2612 &chip, uint8_t reg, uint8_t val)
{
  REQUIRE(chip.write(0, 0, reg) == 0);
  REQUIRE(chip.write(0, 1, val) == 0);
}

}  // namespace

TEST_CASE("Ym2612 renders an audible keyed-on carrier", "[fm][chip]")
{
  generator::Ym2612 chip;
  REQUIRE(chip.init(1, FM_CLOCK, FM_RATE, dummy_timer_handler,
                    dummy_irq_handler) == 0);

  write_reg(chip, 0x30, 0x06);  // ch0 op1 DT=0 MULT=6
  write_reg(chip, 0x34, 0x06);  // ch0 op2
  write_reg(chip, 0x38, 0x06);  // ch0 op3
  write_reg(chip, 0x3C, 0x06);  // ch0 op4
  write_reg(chip, 0x40, 0x7F);  // op1..op3 total level = silent
  write_reg(chip, 0x44, 0x7F);
  write_reg(chip, 0x48, 0x7F);
  write_reg(chip, 0x4C, 0x10);  // op4 (carrier) total level = audible
  write_reg(chip, 0x5C,
            0x1F);  // op4 KS/AR: attack rate = max (0x50 block = KS/AR)
  write_reg(chip, 0x6C, 0x07);  // op4 AM/DR: decay rate (0x60 block = AM/DR)
  write_reg(chip, 0x8C, 0x3F);  // op4 sustain level 3, fast release rate
  write_reg(chip, 0xA4, 0x22);  // block 2, fnum high
  write_reg(chip, 0xA0, 0xAA);  // fnum low - audible pitch
  write_reg(chip, 0xB0, 0x07);  // algorithm 7 (all operators parallel)
  write_reg(chip, 0xB4,
            0xC3);  // L+R enable, PMS=3 (LFO phase modulation path)
  write_reg(chip, 0x22,
            0x38);              // LFO enable, high frequency (amd/pmd stepping)
  write_reg(chip, 0x28, 0xF0);  // key on ch0, all four slots

  std::vector<int16_t> left(OUT_SAMPLES), right(OUT_SAMPLES);
  int16_t *buffers[2] = {left.data(), right.data()};

  chip.update_one(0, buffers, OUT_SAMPLES);

  // The keyed-on carrier must produce a non-trivial, bounded waveform.
  int nonzero = 0;
  int16_t peak = 0;
  for (int i = 0; i < OUT_SAMPLES; ++i) {
    if (left[i] != 0)
      nonzero++;
    int a = left[i] < 0 ? -left[i] : left[i];
    if (a > peak)
      peak = a;
  }
  REQUIRE(nonzero > OUT_SAMPLES / 2);
  REQUIRE(peak > 1000);
  REQUIRE(peak <= 32767);
  // L+R both enabled: the two channels carry the same mix.
  REQUIRE(0 == std::memcmp(left.data(), right.data(), OUT_SAMPLES * 2));

  // Pinned waveform hash: any change in chip wiring, envelope math, LFO
  // stepping, or the DAC ladder alters this value. Update it only with a
  // justified behavior change.
  const uint64_t h = fnv1a64(left.data(), OUT_SAMPLES * 2);
  REQUIRE(h == 0x5c824a3104017cc6ULL);
}

TEST_CASE("YM2612 envelope decays after key-off across update calls",
          "[fm][chip]")
{
  generator::Ym2612 chip;
  REQUIRE(chip.init(1, FM_CLOCK, FM_RATE, dummy_timer_handler,
                    dummy_irq_handler) == 0);

  write_reg(chip, 0x4C, 0x00);  // carrier at full level
  write_reg(chip, 0x5C, 0x1F);  // KS/AR: fast attack
  write_reg(chip, 0x6C,
            0x00);  // AM/DR: slow decay (holds near the attack peak)
  write_reg(chip, 0x8C, 0x3F);  // SL/RR: fast release once keyed off
  write_reg(chip, 0xA4, 0x21);
  write_reg(chip, 0xA0, 0x55);
  write_reg(chip, 0xB0, 0x07);
  write_reg(chip, 0xB4, 0xC0);  // L+R, no LFO modulation
  write_reg(chip, 0x28, 0xF0);  // key on

  std::vector<int16_t> left(OUT_SAMPLES), right(OUT_SAMPLES);
  int16_t *buffers[2] = {left.data(), right.data()};

  // First render: attack phase - loud.
  chip.update_one(0, buffers, OUT_SAMPLES);
  int peak_attack = 0;
  for (int i = 0; i < OUT_SAMPLES; ++i) {
    int a = left[i] < 0 ? -left[i] : left[i];
    if (a > peak_attack)
      peak_attack = a;
  }
  REQUIRE(peak_attack > 1000);

  // Key off: the release envelope must fade toward silence over subsequent
  // renders (per-chip LFO/envelope caches persist across UpdateOne calls).
  REQUIRE(chip.write(0, 0, 0x28) == 0);
  REQUIRE(chip.write(0, 1, 0x00) == 0);

  int peak_final = 0;
  for (int chunk = 0; chunk < 200; ++chunk) {
    chip.update_one(0, buffers, OUT_SAMPLES);
    if (chunk == 199) {  // compare the tail, after the release has had time
      for (int i = 0; i < OUT_SAMPLES; ++i) {
        int a = left[i] < 0 ? -left[i] : left[i];
        if (a > peak_final)
          peak_final = a;
      }
    }
  }
  REQUIRE(peak_final < peak_attack / 2);
}

TEST_CASE("Ym2612 instances own independent chip allocations", "[fm][chip]")
{
  generator::Ym2612 first;
  generator::Ym2612 second;

  REQUIRE(first.init(1, FM_CLOCK, FM_RATE, dummy_timer_handler,
                     dummy_irq_handler) == 0);
  REQUIRE(second.init(1, FM_CLOCK, FM_RATE, dummy_timer_handler,
                      dummy_irq_handler) == 0);
  REQUIRE(first.init(1, FM_CLOCK, FM_RATE, dummy_timer_handler,
                     dummy_irq_handler) == -1);
}
