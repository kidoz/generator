/* SPDX-License-Identifier: GPL-2.0-or-later */
/* YM3438 timer/status/busy tests. */

#include "ym3438.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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
  /* set timer A period: NA = 0 -> 1024 samples = 1024*144*7 mclk */
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
  ym.advance_mclk(1024 * 144 * 7 - 1000);
  CHECK((ym.read_status() & 0x01) == 0);
  /* cross the period */
  ym.advance_mclk(2000);
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
  ym.advance_mclk(1024 * 144 * 7 + 1000);
  CHECK((ym.read_status() & 0x01) != 0);
  /* write with the reset bit: flag clears momentarily */
  ym.write_address(0x27);
  ym.write_data(0x15);
  CHECK((ym.read_status() & 0x01) == 0);
  /* more time passes: flag can be set again (reset is momentary) */
  ym.advance_mclk(1024 * 144 * 7 + 1000);
  CHECK((ym.read_status() & 0x01) != 0);
}

TEST_CASE("ym3438 timer keeps running across repeated reg 27 writes", "[ym]")
{
  /* The load bit is a run/stop level, not a restart strobe. A driver that
   * rewrites $27 every tick -- once to clear the overflow flag, once more
   * when it has finished its work -- must not push the counter back to
   * the top, or the period grows by the driver's own processing time and
   * the music drags. */
  constexpr int64_t kPeriodB = 256 * 16 * 144 * 7; /* NB = 0 */

  Ym3438 ym;
  ym.reset();
  ym.write_address(0x26);
  ym.write_data(0x00);
  /* load + enable timer B */
  ym.write_address(0x27);
  ym.write_data(0x0A);

  ym.advance_mclk((uint64_t)(kPeriodB / 2));
  CHECK((ym.read_status() & 0x02) == 0);

  /* mid-period rewrites with the load bit still set: no restart */
  for (int i = 0; i < 4; i++) {
    ym.write_address(0x27);
    ym.write_data(0x2A); /* reset flag B + enable + load */
    ym.write_address(0x27);
    ym.write_data(0x0A); /* enable + load */
  }

  ym.advance_mclk((uint64_t)(kPeriodB / 2) + 1000);
  CHECK((ym.read_status() & 0x02) != 0);
}

TEST_CASE("ym3438 clearing the load bit reloads the timer on restart", "[ym]")
{
  constexpr int64_t kPeriodB = 256 * 16 * 144 * 7; /* NB = 0 */

  Ym3438 ym;
  ym.reset();
  ym.write_address(0x26);
  ym.write_data(0x00);
  ym.write_address(0x27);
  ym.write_data(0x0A);

  ym.advance_mclk((uint64_t)(kPeriodB * 3 / 4));

  /* stop, then start again: the 0->1 edge reloads, so the three quarters
   * already counted are discarded */
  ym.write_address(0x27);
  ym.write_data(0x08); /* enable, load cleared */
  ym.write_address(0x27);
  ym.write_data(0x0A); /* load again */

  ym.advance_mclk((uint64_t)(kPeriodB / 2));
  CHECK((ym.read_status() & 0x02) == 0);
  ym.advance_mclk((uint64_t)(kPeriodB / 2) + 1000);
  CHECK((ym.read_status() & 0x02) != 0);
}

TEST_CASE("ym3438 timer runs on load alone and flags only when enabled", "[ym]")
{
  constexpr int64_t kPeriodB = 256 * 16 * 144 * 7; /* NB = 0 */

  Ym3438 ym;
  ym.reset();
  ym.write_address(0x26);
  ym.write_data(0x00);
  /* load without enable: the counter runs, the flag stays down */
  ym.write_address(0x27);
  ym.write_data(0x02);
  ym.advance_mclk((uint64_t)kPeriodB + 1000);
  CHECK((ym.read_status() & 0x02) == 0);

  /* enabling mid-flight lets the next overflow through, and because the
   * counter was never restarted that arrives one period later */
  ym.write_address(0x27);
  ym.write_data(0x0A);
  ym.advance_mclk((uint64_t)(kPeriodB / 2));
  CHECK((ym.read_status() & 0x02) == 0);
  ym.advance_mclk((uint64_t)(kPeriodB / 2) + 2000);
  CHECK((ym.read_status() & 0x02) != 0);
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
  ym.advance_mclk(1024 * 144 * 7 + 1000);
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
  /* The DAC takes channel 6's place in the mix, so it leaves through
   * channel 6's output switches: with $B6 clear the sample reaches
   * neither speaker. */
  ym.write_address(0xB6, 1);
  ym.write_data(0xC0, 1);
  ym.write_address(0x2B);
  ym.write_data(0x80);
  ym.write_address(0x2A);
  ym.write_data(0xFF);
  ym.advance_mclk(1008);
  CHECK(ym.sample_left() > 1000);
  CHECK(ym.sample_right() > 1000);
}

TEST_CASE("ym3438 dac follows the channel six panning", "[ym]")
{
  Ym3438 ym;
  ym.reset();
  ym.write_address(0xB6, 1);
  ym.write_data(0x80, 1); /* left only */
  ym.write_address(0x2B);
  ym.write_data(0x80);
  ym.write_address(0x2A);
  ym.write_data(0xFF);
  ym.advance_mclk(1008);
  CHECK(ym.sample_left() > 1000);
  CHECK(ym.sample_right() == 0);
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
      /* Carrier and modulators both at full level. Total level is a
       * logarithmic control -- 0x30 is 36 dB down, which barely bends the
       * carrier at all -- so a chain only shows up against a plain tone
       * when the modulators are actually driven. */
      ymw(ym, (uint8_t)(0x40 + op * 4), 0x00);
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

TEST_CASE("ym3438 total level halves amplitude every eight steps",
          "[ym][attenuation]")
{
  /* Reference: Nuked-OPN2 335747d78cb0abbc3b55b004e62dad9763140115,
   * OPN2_EnvelopePrepare() and OPN2_FMGenerate(): TL adds eight EG units
   * per step, and 64 EG units halve amplitude. The older MAME-derived
   * table interleaved +/- entries; our exponential table has no sign bit.
   * https://github.com/nukeykt/Nuked-OPN2/blob/
   * 335747d78cb0abbc3b55b004e62dad9763140115/ym3438.c */
  auto energy = [](uint8_t total_level) {
    Ym3438 ym;
    ym.reset();
    program_loud_voice(ym);
    ymw(ym, 0x40, total_level);
    ymw(ym, 0x28, 0x10); /* Only operator 1, no modulation. */
    ym.advance_mclk(1008 * 32);
    int64_t sum = 0;
    for (int i = 0; i < 4096; i++) {
      ym.advance_mclk(1008);
      const int64_t sample = ym.sample_left();
      sum += sample * sample;
    }
    return double(sum);
  };
  const double full = energy(0);
  REQUIRE(full > 0);
  for (uint8_t level : {8, 16, 32}) {
    CAPTURE(level);
    const double ratio = energy(level) / full;
    const double expected = std::exp2(-double(level) / 4.0);
    CHECK(std::abs(ratio / expected - 1.0) < 0.02);
  }
}

TEST_CASE("ym3438 decay follows reference envelope timing", "[ym][envelope]")
{
  /* Nuked OPN2 335747d78cb0abbc3b55b004e62dad9763140115, with the
   * register sequence below: DR 12 reaches attenuation 24 after 3000
   * samples; DR 20 reaches 189 after 1500. Keep the latter window short
   * enough that the output is still measurable above PCM quantization.
   * Reference: https://github.com/nukeykt/Nuked-OPN2/blob/
   * 335747d78cb0abbc3b55b004e62dad9763140115/ym3438.c */
  struct DecayCase {
    uint8_t rate;
    int samples;
    double attenuation;
  };
  constexpr DecayCase cases[] = {{12, 3000, 24.0}, {20, 1500, 189.0}};

  for (const auto &test : cases) {
    CAPTURE(test.rate, test.samples);
    auto program = [](Ym3438 &ym, uint8_t decay) {
      ym.reset();
      auto write = [&](uint8_t addr, uint8_t data) {
        ym.write_address(addr);
        ym.advance_mclk(1008);
        ym.write_data(data);
        ym.advance_mclk(1008);
      };
      write(0x30, 0x01); /* operator 1, multiple 1 */
      write(0x40, 0x00);
      write(0x50, 0x1F); /* maximum attack, no key scaling */
      write(0x60, decay);
      write(0x70, 0x00);
      write(0x80, 0xF0); /* sustain target below the measurement range */
      write(0xA4, 0x22);
      write(0xA0, 0x69);
      write(0xB0, 0x07);
      write(0xB4, 0xC0);
      write(0x28, 0x10); /* only operator 1 is keyed */
      ym.advance_mclk(10 * 1008);
    };

    Ym3438 decaying;
    Ym3438 steady;
    program(decaying, test.rate);
    program(steady, 0);
    decaying.advance_mclk((uint64_t)test.samples * 1008);
    steady.advance_mclk((uint64_t)test.samples * 1008);
    /* Freeze the measured level. The steady voice cancels oscillator
     * phase and output gain, so this measures only envelope attenuation. */
    decaying.write_address(0x60);
    decaying.write_data(0);
    int64_t decaying_energy = 0;
    int64_t steady_energy = 0;
    for (int i = 0; i < 512; i++) {
      decaying.advance_mclk(1008);
      steady.advance_mclk(1008);
      const int64_t a = decaying.sample_left();
      const int64_t b = steady.sample_left();
      decaying_energy += a * a;
      steady_energy += b * b;
    }
    REQUIRE(decaying_energy > 0);
    REQUIRE(steady_energy > decaying_energy);
    /* In the OPN2 output domain 64 attenuation units halve amplitude;
     * energy squares amplitude. Allow two units for discrete EG stepping
     * and the final PCM rounding, rather than requiring waveform identity. */
    const double attenuation =
        32.0 * std::log2((double)steady_energy / (double)decaying_energy);
    CHECK(std::abs(attenuation - test.attenuation) < 2.0);
  }
}

TEST_CASE("ym3438 fresh key on restarts each operator phase", "[ym][phase]")
{
  for (uint8_t key : {0x10, 0x20, 0x40, 0x80}) {
    CAPTURE(key);
    Ym3438 first;
    first.reset();
    program_loud_voice(first);
    ymw(first, 0x28, key);
    first.advance_mclk(1024 * 1008);
    ymw(first, 0x28, 0x00);
    first.advance_mclk(10000 * 1008); /* let the previous note release */

    Ym3438 later = first;
    later.advance_mclk(37 * 1008); /* a different free-running phase */
    ymw(first, 0x28, key);
    ymw(later, 0x28, key);
    std::array<int16_t, 128> first_note{};
    std::array<int16_t, 128> later_note{};
    for (size_t i = 0; i < first_note.size(); i++) {
      first.advance_mclk(1008);
      later.advance_mclk(1008);
      first_note[i] = first.sample_left();
      later_note[i] = later.sample_left();
    }
    REQUIRE(*std::max_element(first_note.begin(), first_note.end()) > 1000);
    CHECK(first_note == later_note);
  }
}

TEST_CASE("ym3438 repeated key on preserves a held note", "[ym][phase]")
{
  Ym3438 held;
  held.reset();
  program_single_operator(held, 1);
  Ym3438 repeated = held;
  ymw(repeated, 0x28, 0xF0);
  held.advance_mclk(200); /* same elapsed time without a key write */
  for (int i = 0; i < 128; i++) {
    held.advance_mclk(1008);
    repeated.advance_mclk(1008);
    CHECK(repeated.sample_left() == held.sample_left());
  }
}
