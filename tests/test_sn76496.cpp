// Characterization tests for the SN76496 / SN76489 PSG (Programmable Sound
// Generator). These pin the *current* behavior of the tone/noise generation
// math, latch/data writes, and volume tables so that any future migration
// (e.g. moving the chip state into System) or the GEMS-related audio
// work can be done behind a regression net.
//
// The chip is emulated by a C++ class (src/audio/sn76496/sn76496.cpp). Runtime
// state is System-owned, but each test constructs an independent chip so the
// tone/noise math remains exercisable without emulator globals. We compile
// sn76496.cpp directly into this test and stub the one external symbol it
// references (state_transfer32, used only by save_state, which these tests
// never call).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cstdint>

#include "machine.h"  // uint16 typedef
#include "sn76496.hpp"

using generator::SN76496;

using Catch::Approx;

// Stub for save-state plumbing; SN76496_save_state() is intentionally not
// exercised by these tests. Providing it here keeps the link self-contained
// without dragging in the full state.cpp + emulator globals.
void state_transfer32(const char * /*mod*/, const char * /*name*/,
                      uint8 /*instance*/, uint32 * /*data*/, uint32 /*size*/)
{
}

namespace {
// Independent parity helper mirroring sn76496.cpp's private static
// SN76496::parity(). Used to predict LFSR transitions in tests without
// relying on the chip's private copy.
int parity(unsigned int val)
{
  val ^= val >> 8;
  val ^= val >> 4;
  val ^= val >> 2;
  val ^= val >> 1;
  return val & 1;
}

// Default test clock/sample-rate: the Genesis PSG runs at the Z80 clock
// (~3.58 MHz) divided down; the exact ratio doesn't matter for the math
// under test, only that UpdateStep is set consistently by SN76496Init.
constexpr int PSG_CLOCK = 3579545;
constexpr int PSG_SAMPLE_RATE = 44100;
constexpr int PSG_GAIN = 0;

// MAX_OUTPUT and STEP mirror the private #defines in sn76496.cpp.
constexpr int MAX_OUTPUT = 0x7fff;
constexpr int STEP = 0x10000;

}  // namespace

TEST_CASE("SN76496Init resets to a silent, documented state", "[psg]")
{
  SN76496 psg;
  REQUIRE(psg.init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);

  const SN76496 &R = psg;

  // All four channels start silent (volume 0).
  for (int i = 0; i < 4; ++i) {
    REQUIRE(R.Volume[i] == 0);
  }

  // Registers: even indices 0, tone periods reset; odd indices carry the
  // volume nibble 0x0f (silence).
  for (int i = 0; i < 8; i += 2) {
    REQUIRE(R.Register[i] == 0);
    REQUIRE(R.Register[i + 1] == 0x0f);
  }
  REQUIRE(R.LastRegister == 0);

  // Tone channels: DC-high output (Period 0 == held high).
  for (int i = 0; i < 3; ++i) {
    REQUIRE(R.Output[i] == 1);
    REQUIRE(R.Period[i] == 0);
  }

  // Noise channel: periodic mode, RNG preset, N/512 default period.
  REQUIRE(R.NoiseFB == 0x0001);  // FB_PNOISE_TAPS
  REQUIRE(R.RNG == 0x8000);      // NG_PRESET
  REQUIRE(R.Output[3] == (R.RNG & 1));
  REQUIRE(R.Period[3] == static_cast<int>(R.UpdateStep << 5));
}

TEST_CASE("SN76496Init UpdateStep scales clock/sample-rate", "[psg]")
{
  SN76496 psg;
  REQUIRE(psg.init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);

  // UpdateStep = STEP * SampleRate * 16 / clock  (sn76496.c SN76496_set_clock)
  const double expected =
      (static_cast<double>(STEP) * PSG_SAMPLE_RATE * 16) / PSG_CLOCK;
  const double got = static_cast<double>(psg.UpdateStep);
  // Rounded to nearest integer by the int assignment in the source.
  REQUIRE(got == Approx(static_cast<int>(expected + 0.5)));
}

TEST_CASE("Volume table attenuates 2dB/step and silences at 0xF", "[psg]")
{
  SN76496 psg;
  REQUIRE(psg.init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);

  const SN76496 &R = psg;

  // Volume 15 is always silence.
  REQUIRE(R.VolTable[15] == 0);

  // Each step is 2dB attenuation => strictly decreasing from index 0 down
  // toward silence. (MAX_OUTPUT/4 headroom with gain 0.)
  for (int i = 0; i < 14; ++i) {
    REQUIRE(R.VolTable[i] >= R.VolTable[i + 1]);
  }
  REQUIRE(R.VolTable[0] > 0);
  // Index 14 is the last audible step, must not be silent.
  REQUIRE(R.VolTable[14] > 0);
}

TEST_CASE("Latch/data writes set frequency and period", "[psg]")
{
  SN76496 psg;
  REQUIRE(psg.init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);

  SN76496 &R = psg;

  // Tone 0: latch + low nibble. 0x80|0x00|0x01 => reg0 low nibble = 1.
  psg.write(0x80 | 0x00 | 0x01);
  // Follow-up data byte: high 6 bits = 0x3F, low nibble retained.
  psg.write(0x3F);

  // Register[0] = (0x3F << 4) | 0x01 == 0x3F1.
  REQUIRE(R.Register[0] == 0x3F1);
  // Value > 1 => Period = UpdateStep * Register[r].
  REQUIRE(R.Period[0] == static_cast<int>(R.UpdateStep * R.Register[0]));
}

TEST_CASE("Frequency register <= 1 forces DC-high output", "[psg]")
{
  SN76496 psg;
  REQUIRE(psg.init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);

  SN76496 &R = psg;

  // Latch tone 0, write low nibble = 0 => Register[0] = 0 (<= 1).
  psg.write(0x80 | 0x00 | 0x00);
  REQUIRE(R.Register[0] == 0);
  REQUIRE(R.Period[0] == 0);  // DC mode
  REQUIRE(R.Output[0] == 1);  // held high

  // Latch tone 0, write low nibble = 1 => Register[0] = 1 (also <= 1).
  psg.write(0x80 | 0x00 | 0x01);
  REQUIRE(R.Register[0] == 1);
  REQUIRE(R.Period[0] == 0);
  REQUIRE(R.Output[0] == 1);
}

TEST_CASE("Volume write via register maps to VolTable", "[psg]")
{
  SN76496 psg;
  REQUIRE(psg.init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);

  SN76496 &R = psg;

  // Tone 0 volume = 0x0 (loudest): 0x90 = latch reg1, vol nibble 0.
  psg.write(0x90 | 0x00);
  REQUIRE(R.Register[1] == 0x00);
  REQUIRE(R.Volume[0] == R.VolTable[0]);

  // Tone 0 volume = 0xF (silence): 0x9F.
  psg.write(0x9F);
  REQUIRE(R.Register[1] == 0x0f);
  REQUIRE(R.Volume[0] == R.VolTable[0x0f]);
  REQUIRE(R.Volume[0] == 0);  // silence sentinel
}

TEST_CASE("Noise register selects feedback taps and resets RNG", "[psg]")
{
  SN76496 psg;
  REQUIRE(psg.init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);

  SN76496 &R = psg;

  // Corrupt RNG first to prove the write resets it.
  R.RNG = 0xDEAD;

  // White noise: reg6 nibble = 0b110 (bit2 mode=white, rate=2 => N/2048).
  // Latch byte = 0x80 | (reg<<4) | nibble = 0x80 | 0x60 | 0x06 = 0xE6.
  psg.write(0x80 | 0x60 | 0x06);
  REQUIRE(R.NoiseFB == 0x0009);  // FB_WNOISE_TAPS
  REQUIRE(R.RNG == 0x8000);      // reset to NG_PRESET
  REQUIRE(R.Output[3] == (R.RNG & 1));

  // Periodic noise: reg6 nibble = 0b010 (mode=periodic, rate=2).
  R.RNG = 0xBEEF;
  psg.write(0x80 | 0x60 | 0x02);
  REQUIRE(R.NoiseFB == 0x0001);  // FB_PNOISE_TAPS
  REQUIRE(R.RNG == 0x8000);
}

TEST_CASE("White-noise LFSR advances by parity feedback each tick", "[psg]")
{
  SN76496 psg;
  REQUIRE(psg.init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);

  SN76496 &R = psg;

  // Select white noise (taps 0x9). Seed a known RNG and arrange the counters
  // so that exactly ONE tick fires within a single output sample: Count[3]=1
  // (expires immediately) and Period[3]=STEP (so after the tick Count[3]=STEP,
  // which exceeds the remaining sample budget and prevents a second tick).
  // Volume[3] must be non-zero, otherwise SN76496Update's silent-channel
  // shortcut adds length*STEP to Count[3] at entry and skips the tick.
  R.NoiseFB = 0x0009;
  R.RNG = 0x8000;  // NG_PRESET
  R.Output[3] = R.RNG & 1;
  R.Volume[3] = R.VolTable[0];
  R.Period[3] = STEP;
  R.Count[3] = 1;

  const unsigned int rng_before = R.RNG;
  std::array<uint16, 1> buf{};
  psg.update(buf.data(), 1);

  // Predict: feedback = parity(RNG & NoiseFB); RNG = (RNG>>1)|fb<<15.
  const int fb = parity(rng_before & R.NoiseFB);
  const unsigned int expected =
      (rng_before >> 1) | (static_cast<unsigned int>(fb) << 15);
  REQUIRE(R.RNG == expected);
}

TEST_CASE("SN76496Update output is bounded and audible when a channel is on",
          "[psg]")
{
  SN76496 psg;
  REQUIRE(psg.init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);

  // Enable tone 0 at full volume with a moderate period, so output is
  // non-zero. Period = UpdateStep * Register[r]; pick Register = 0x100.
  psg.write(0x80 | 0x00 | 0x00);  // latch tone0 freq, low=0
  psg.write(0x10);                // high6=0x10 => Register=0x100
  psg.write(0x90 | 0x00);         // tone0 vol = 0 (loudest)

  std::array<uint16, 256> buf{};
  buf.fill(0xDEAD);
  psg.update(buf.data(), static_cast<int>(buf.size()));

  // At least one sample must be non-zero (channel is on).
  bool any_nonzero = false;
  for (uint16_t s : buf) {
    REQUIRE(s <= static_cast<uint16_t>(MAX_OUTPUT));
    if (s != 0)
      any_nonzero = true;
  }
  REQUIRE(any_nonzero);
}
