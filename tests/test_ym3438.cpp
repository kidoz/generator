/* SPDX-License-Identifier: GPL-2.0-or-later */
/* YM3438 timer/status/busy tests. */

#include "ym3438.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>

using namespace generator;

TEST_CASE("ym3438 status is idle after reset", "[ym]")
{
  Ym3438 ym;
  ym.reset();
  CHECK(ym.read_status() == 0x00);
  CHECK_FALSE(ym.irq_line());
}

TEST_CASE("ym3438 busy flag sets immediately on write", "[ym]")
{
  Ym3438 ym;
  ym.reset();
  ym.write_address(0x24);
  CHECK((ym.read_status() & 0x80) != 0); /* busy */
  /* advance past the busy period (17 FM clocks = 119 mclk) */
  ym.advance_mclk(200);
  CHECK((ym.read_status() & 0x80) == 0); /* not busy */
}

TEST_CASE("ym3438 timer a overflows and sets flag", "[ym]")
{
  Ym3438 ym;
  ym.reset();
  /* set timer A period: NA = 0 -> period = 1024*12*7 = 86016 mclk */
  ym.write_address(0x24);
  ym.write_data(0x00);
  ym.write_address(0x25);
  ym.write_data(0x00);
  /* load + enable + clear: $27 = $15 */
  ym.write_address(0x27);
  ym.write_data(0x15);
  /* re-enable without the reset bit so the flag can latch */
  ym.write_address(0x27);
  ym.write_data(0x05);

  /* advance just under the period */
  ym.advance_mclk(86000);
  CHECK((ym.read_status() & 0x01) == 0);
  /* cross the period */
  ym.advance_mclk(100);
  CHECK((ym.read_status() & 0x01) != 0); /* timer A overflow */
}

TEST_CASE("ym3438 timer a flag resets via reg 27", "[ym]")
{
  Ym3438 ym;
  ym.reset();
  ym.write_address(0x24);
  ym.write_data(0x00);
  ym.write_address(0x25);
  ym.write_data(0x00);
  /* load + enable (no reset bit) */
  ym.write_address(0x27);
  ym.write_data(0x05);
  ym.advance_mclk(90000);
  CHECK((ym.read_status() & 0x01) != 0);
  /* write with the reset bit: flag clears momentarily */
  ym.write_address(0x27);
  ym.write_data(0x15);
  CHECK((ym.read_status() & 0x01) == 0);
  /* more time passes: flag can be set again (reset is momentary) */
  ym.advance_mclk(90000);
  CHECK((ym.read_status() & 0x01) != 0);
}

TEST_CASE("ym3438 irq asserts when timer overflow and irq enable", "[ym]")
{
  Ym3438 ym;
  ym.reset();
  ym.write_address(0x24);
  ym.write_data(0x00);
  ym.write_address(0x25);
  ym.write_data(0x00);
  /* load + enable + IRQ enable (no reset bit): $27 = $45 */
  ym.write_address(0x27);
  ym.write_data(0x45);
  ym.advance_mclk(90000);
  CHECK((ym.read_status() & 0x01) != 0);
  CHECK(ym.irq_line()); /* IRQ asserted */

  /* write with the reset bit -> IRQ deasserts immediately */
  ym.write_address(0x27);
  ym.write_data(0x55);
  CHECK_FALSE(ym.irq_line());
}

TEST_CASE("ym3438 register banks follow the address port", "[ym]")
{
  Ym3438 ym;
  ym.reset();
  ym.write_address(0x30, 1);
  ym.write_data(0xAB, 1);
  CHECK(ym.reg(1, 0x30) == 0xAB);
  CHECK(ym.reg(0, 0x30) == 0x00);
}

TEST_CASE("ym3438 dac registers drive channel six output", "[ym]")
{
  Ym3438 ym;
  ym.reset();
  ym.write_address(0x2B);
  ym.write_data(0x80);
  ym.write_address(0x2A);
  ym.write_data(0xFF);
  ym.advance_mclk(1008);
  CHECK(ym.sample_left() > 1000);
  CHECK(ym.sample_right() > 1000);
}

namespace {

void ymw(Ym3438 &ym, uint8_t addr, uint8_t data)
{
  ym.write_address(addr);
  ym.write_data(data);
  ym.advance_mclk(200); /* clear the busy window */
}

/* Channel 0 on algorithm 7: all four operators go straight to the output
 * at full level, which is the loudest thing the chip can be asked for. */
void program_loud_voice(Ym3438 &ym)
{
  ymw(ym, 0x22, 0x00); /* LFO off */
  ymw(ym, 0x27, 0x00); /* normal mode */
  ymw(ym, 0xB0, 0x07); /* algorithm 7, no feedback */
  ymw(ym, 0xB4, 0xC0); /* both speakers */
  for (uint8_t op = 0; op < 4; op++) {
    ymw(ym, (uint8_t)(0x30 + op * 4), 0x01); /* detune 0, multiple 1 */
    ymw(ym, (uint8_t)(0x40 + op * 4), 0x00); /* total level 0 = loudest */
    ymw(ym, (uint8_t)(0x50 + op * 4), 0x1F); /* fastest attack */
    ymw(ym, (uint8_t)(0x60 + op * 4), 0x00); /* no decay */
    ymw(ym, (uint8_t)(0x70 + op * 4), 0x00); /* no sustain decay */
    ymw(ym, (uint8_t)(0x80 + op * 4), 0x0F);
  }
  ymw(ym, 0xA4, 0x22); /* block / f-number high */
  ymw(ym, 0xA0, 0x69);
}

}  // namespace

TEST_CASE("ym3438 renders a keyed voice as a full-scale waveform", "[ym]")
{
  /* Guards three defects that each reduced the chip to silence: a sine
   * lookup that threw away eight of the ten phase bits, an envelope
   * counter that never advanced, and an attack that never opened. */
  Ym3438 ym;
  ym.reset();
  program_loud_voice(ym);
  ymw(ym, 0x28, 0xF0); /* key on all four operators of channel 0 */

  int32_t lo = 0;
  int32_t hi = 0;
  for (int i = 0; i < 20000; i++) {
    ym.advance_mclk(1216); /* one operator sample period */
    const int32_t s = ym.sample_left();
    lo = std::min(lo, s);
    hi = std::max(hi, s);
  }
  CHECK(hi > 2000);  /* a real waveform, not a handful of LSBs */
  CHECK(lo < -2000); /* and it swings both ways */
}

TEST_CASE("ym3438 key off releases the voice", "[ym]")
{
  Ym3438 ym;
  ym.reset();
  program_loud_voice(ym);
  ymw(ym, 0x80, 0xFF); /* fast release on operator 1 */
  ymw(ym, 0x84, 0xFF);
  ymw(ym, 0x88, 0xFF);
  ymw(ym, 0x8C, 0xFF);
  ymw(ym, 0x28, 0xF0);
  for (int i = 0; i < 2000; i++) {
    ym.advance_mclk(1216);
  }

  ymw(ym, 0x28, 0x00); /* key off: every operator flag clear */
  for (int i = 0; i < 20000; i++) {
    ym.advance_mclk(1216);
  }
  int32_t peak = 0;
  for (int i = 0; i < 2000; i++) {
    ym.advance_mclk(1216);
    peak = std::max<int32_t>(peak, std::abs((int32_t)ym.sample_left()));
  }
  CHECK(peak < 200);
}

namespace {

/* Counts half-cycles over a fixed span, which tracks pitch without
 * needing a transform. The threshold is hysteresis: a bare sign test
 * chatters on any low-level content riding through zero. */
int half_cycles(Ym3438 &ym, int samples)
{
  constexpr int32_t kThreshold = 1000;
  int count = 0;
  int state = 0;
  for (int i = 0; i < samples; i++) {
    ym.advance_mclk(1008); /* one chip sample */
    const int32_t s = ym.sample_left();
    if (s > kThreshold && state <= 0) {
      state = 1;
      count++;
    } else if (s < -kThreshold && state >= 0) {
      state = -1;
      count++;
    }
  }
  return count;
}

/* Total variation per unit amplitude: how much the waveform moves from
 * sample to sample relative to how loud it is. A plain sine sits low; the
 * extra partials modulation folds in push it up. */
double waveform_activity(Ym3438 &ym, int samples)
{
  int64_t variation = 0;
  int32_t peak = 1;
  int32_t prev = ym.sample_left();
  for (int i = 0; i < samples; i++) {
    ym.advance_mclk(1008);
    const int32_t s = ym.sample_left();
    variation += std::abs(s - prev);
    peak = std::max(peak, std::abs(s));
    prev = s;
  }
  return (double)variation / ((double)peak * samples);
}

/* One audible operator on algorithm 7, at the given frequency multiple. */
void program_single_operator(Ym3438 &ym, uint8_t mul)
{
  ymw(ym, 0x22, 0x00);
  ymw(ym, 0x27, 0x00);
  ymw(ym, 0xB0, 0x07); /* algorithm 7: every operator straight out */
  ymw(ym, 0xB4, 0xC0);
  for (uint8_t op = 0; op < 4; op++) {
    ymw(ym, (uint8_t)(0x30 + op * 4), op == 0 ? mul : 0x01);
    ymw(ym, (uint8_t)(0x40 + op * 4), op == 0 ? 0x00 : 0x7F); /* silence 2-4 */
    ymw(ym, (uint8_t)(0x50 + op * 4), 0x1F);
    ymw(ym, (uint8_t)(0x60 + op * 4), 0x00);
    ymw(ym, (uint8_t)(0x70 + op * 4), 0x00);
    ymw(ym, (uint8_t)(0x80 + op * 4), 0x0F);
  }
  ymw(ym, 0xA4, 0x22);
  ymw(ym, 0xA0, 0x69);
  ymw(ym, 0x28, 0xF0);
  for (int i = 0; i < 500; i++) {
    ym.advance_mclk(1008);
  }
}

}  // namespace

TEST_CASE("ym3438 operator multiple scales the operator's pitch", "[ym]")
{
  /* MUL is the operator's ratio to the note. Ignoring it - the phase
   * generator used to read detune out of the key-scale register and never
   * looked at the multiple at all - leaves every operator of a patch
   * sounding the same pitch. */
  Ym3438 one;
  one.reset();
  program_single_operator(one, 0x01);
  const int base = half_cycles(one, 16000);

  Ym3438 two;
  two.reset();
  program_single_operator(two, 0x02);
  const int doubled = half_cycles(two, 16000);

  CHECK(base > 20);
  /* MUL 2 is an octave up: twice the crossings, within a few percent. */
  CHECK(doubled > base * 19 / 10);
  CHECK(doubled < base * 21 / 10);
}

TEST_CASE("ym3438 chains operators through the algorithm", "[ym]")
{
  /* A four-operator chain has to sound different from four independent
   * sines. When the modulation input never reaches the next operator,
   * every algorithm degenerates to a sum of plain tones at one pitch. */
  auto render = [](int algorithm) {
    Ym3438 ym;
    ym.reset();
    ymw(ym, 0x22, 0x00);
    ymw(ym, 0x27, 0x00);
    ymw(ym, 0xB0, (uint8_t)algorithm);
    ymw(ym, 0xB4, 0xC0);
    for (uint8_t op = 0; op < 4; op++) {
      ymw(ym, (uint8_t)(0x30 + op * 4), 0x01);
      /* Carrier loud, modulators moderately attenuated. */
      ymw(ym, (uint8_t)(0x40 + op * 4), op == 3 ? 0x00 : 0x30);
      ymw(ym, (uint8_t)(0x50 + op * 4), 0x1F);
      ymw(ym, (uint8_t)(0x60 + op * 4), 0x00);
      ymw(ym, (uint8_t)(0x70 + op * 4), 0x00);
      ymw(ym, (uint8_t)(0x80 + op * 4), 0x0F);
    }
    ymw(ym, 0xA4, 0x22);
    ymw(ym, 0xA0, 0x69);
    ymw(ym, 0x28, 0xF0);
    for (int i = 0; i < 2000; i++) {
      ym.advance_mclk(1008);
    }
    return waveform_activity(ym, 16000);
  };

  /* Algorithm 0 runs the note through three modulators; algorithm 7 puts
   * the same four operators side by side at the same pitch, which is a
   * plain tone. The chained voice carries far more high partials. */
  const double chained = render(0);
  const double parallel = render(7);
  /* Measured around 1.4x; unmodulated it is 1.0 by construction, since
   * both algorithms would then be sums of the same tone. */
  CHECK(chained > parallel * 1.2);
}
