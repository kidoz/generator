// Characterization tests for the extracted YM2612 envelope + phase core
// (src/audio/ym2612/fm_eg.cpp: calc_eg, CALC_FCSLOT).
//
// These pin the CURRENT behavior of the envelope generator and phase-increment
// math so the deferred GEMS sound-driver fixes can land as visible, reviewed
// changes against pinned expectations:
//   - TD-019 detune overflow mask lands in CALC_FCSLOT
//   - TD-020 SSG-EG threshold/loop/inversion lands in calc_eg
//   - TD-021 LFO PM clock is upstream of this core
//
// Where a test documents behavior that the GEMS fixes will CHANGE, it carries a
// "GEMS-PIN" comment so the fix commit knows to update (not just add) the
// assertion. Values are hand-derived from fm_eg.cpp arithmetic, not captured.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "fm_eg.hpp"  // FM_SLOT, calc_eg, CALC_FCSLOT, env constants

namespace {

// Env values are 16.16 fixed point. MIN_ATT_INDEX = 0.ffff (loudest),
// MAX_ATT_INDEX = 1023.ffff (silence). See fm_eg.hpp.
constexpr int32_t ENV_UNITS(int levels)
{
  return levels << ENV_SH;
}

// Build a minimal FM_SLOT in a given envelope phase. Only the fields the
// envelope math reads are populated; the rest is zero.
FM_SLOT make_slot(uint8_t state, int32_t volume)
{
  FM_SLOT s{};
  s.state = state;
  s.volume = volume;
  s.TLL = 0;  // no total-level offset, so output == raw envelope
  s.ams = 0;  // disable LFO AM unless a test sets it
  s.SEG = 0;
  s.ssg_inv = 0;
  s.sl = 0;
  s.delta_ar = 0;
  s.delta_dr = 0;
  s.delta_sr = 0;
  s.delta_rr = 0;
  return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// calc_eg: attack phase (EG_ATT)
// ---------------------------------------------------------------------------

TEST_CASE("calc_eg attack decreases volume and crosses into decay",
          "[ym2612][eg]")
{
  // Start near max attenuation (silence), with a non-trivial attack step.
  FM_SLOT s = make_slot(EG_ATT, ENV_UNITS(500));
  s.delta_ar = ENV_UNITS(10);  // attack subtracts this per call

  const int32_t vol_before = s.volume;
  (void)calc_eg(&s, 0);

  // Attack phase subtracts delta_ar (then refines); volume must drop.
  REQUIRE(s.volume < vol_before);
  // Not yet at the bottom, so still attacking.
  REQUIRE(s.state == EG_ATT);
}

TEST_CASE("calc_eg attack transitions to decay at MIN_ATT_INDEX",
          "[ym2612][eg]")
{
  // Place volume one step above the loud floor; a single attack step crosses.
  FM_SLOT s = make_slot(EG_ATT, ENV_UNITS(2));
  s.delta_ar = ENV_UNITS(5);

  (void)calc_eg(&s, 0);

  // Crossing MIN_ATT_INDEX clamps to >= 0 and switches to EG_DEC.
  REQUIRE(s.volume >= 0);
  REQUIRE(s.volume <= MIN_ATT_INDEX);
  REQUIRE(s.state == EG_DEC);
}

// ---------------------------------------------------------------------------
// calc_eg: decay -> sustain (EG_DEC)
// ---------------------------------------------------------------------------

TEST_CASE("calc_eg decay adds delta_dr and reaches sustain level",
          "[ym2612][eg]")
{
  const int32_t sustain_level = ENV_UNITS(100);
  FM_SLOT s = make_slot(EG_DEC, ENV_UNITS(10));
  s.sl = static_cast<UINT32>(sustain_level);
  s.delta_dr = ENV_UNITS(200);  // large step: 10 -> 210, past sl=100

  (void)calc_eg(&s, 0);

  // Decay clamps to the sustain level and switches to EG_SUS.
  REQUIRE(s.volume == sustain_level);
  REQUIRE(s.state == EG_SUS);
}

TEST_CASE("calc_eg decay below sustain level stays in decay", "[ym2612][eg]")
{
  const int32_t sustain_level = ENV_UNITS(100);
  FM_SLOT s = make_slot(EG_DEC, ENV_UNITS(10));
  s.sl = static_cast<UINT32>(sustain_level);
  s.delta_dr = ENV_UNITS(20);  // 10 -> 30, still below sl=100

  (void)calc_eg(&s, 0);

  REQUIRE(s.volume == ENV_UNITS(30));
  REQUIRE(s.state == EG_DEC);
}

// ---------------------------------------------------------------------------
// calc_eg: normal (non-SSG) sustain -> off, and release -> off
// ---------------------------------------------------------------------------

TEST_CASE("calc_eg normal sustain ramps to silence then off", "[ym2612][eg]")
{
  FM_SLOT s = make_slot(EG_SUS, ENV_UNITS(500));
  s.delta_sr = ENV_UNITS(600);  // crosses MAX_ATT_INDEX in one step

  (void)calc_eg(&s, 0);

  REQUIRE(s.volume == MAX_ATT_INDEX);
  REQUIRE(s.state == EG_OFF);
}

TEST_CASE("calc_eg normal release ramps to silence then off", "[ym2612][eg]")
{
  FM_SLOT s = make_slot(EG_REL, ENV_UNITS(500));
  s.delta_rr = ENV_UNITS(600);

  (void)calc_eg(&s, 0);

  REQUIRE(s.volume == MAX_ATT_INDEX);
  REQUIRE(s.state == EG_OFF);
}

// ---------------------------------------------------------------------------
// calc_eg: output level (TLL + volume), incl. LFO AM add
// ---------------------------------------------------------------------------

TEST_CASE("calc_eg output is TLL plus shifted volume", "[ym2612][eg]")
{
  FM_SLOT s =
      make_slot(EG_SUS, ENV_UNITS(400));  // hold volume, no delta crossing
  s.delta_sr = 0;
  s.TLL = 1000;
  s.ams = 0;

  const unsigned int out = calc_eg(&s, 0);

  // out = TLL + (volume >> ENV_SH) = 1000 + 400.
  REQUIRE(out == 1000u + 400u);
}

TEST_CASE("calc_eg LFO AM adds ams*lfo_amd/LFO_RATE to output", "[ym2612][eg]")
{
  FM_SLOT s = make_slot(EG_SUS, ENV_UNITS(400));
  s.delta_sr = 0;
  s.TLL = 0;
  s.ams = 3;  // non-zero AMS depth gates the add

  constexpr UINT32 lfo_amd = LFO_RATE * 2;  // so add == ams*2
  const unsigned int out = calc_eg(&s, lfo_amd);

  // out = (volume>>ENV_SH) + (ams * lfo_amd / LFO_RATE) = 400 + (3*2) = 406.
  REQUIRE(out == 400u + 3u * 2u);
}

// ---------------------------------------------------------------------------
// calc_eg: SSG-EG paths (GEMS-PIN — TD-020 will change these)
// ---------------------------------------------------------------------------
// The current SSG-EG implementation is known-incorrect vs Genesis Plus GX:
//   - uses MAX_ATT_INDEX (1023) where hardware wants SSG_ATT_THRESHOLD (512)
//   - resets volume to MIN_ATT_INDEX on loop instead of keeping it
// These tests pin the CURRENT behavior so the TD-020 fix is a visible diff.

TEST_CASE(
    "calc_eg SSG-EG sustain loop resets volume to MIN and toggles inversion",
    "[ym2612][eg][gems-pin]")
{
  // GEMS-PIN: TD-020 changes the loop branch (no volume reset, threshold=512).
  FM_SLOT s = make_slot(EG_SUS, ENV_UNITS(500));
  s.SEG = SSG_ENABLE;           // SSG on, no HOLD, no ALTERNATE => loop mode
  s.delta_sr = ENV_UNITS(600);  // crosses MAX_ATT_INDEX in one step

  (void)calc_eg(&s, 0);

  // Current behavior: loop restarts from MIN_ATT_INDEX, back to attack.
  REQUIRE(s.volume == MIN_ATT_INDEX);
  REQUIRE(s.state == EG_ATT);
  REQUIRE(s.ssg_inv == 0);  // no ALTERNATE bit => no toggle
}

TEST_CASE("calc_eg SSG-EG hold clamps to MAX_ATT_INDEX and goes off",
          "[ym2612][eg][gems-pin]")
{
  // GEMS-PIN: TD-020 changes the threshold (512 vs MAX 1023).
  FM_SLOT s = make_slot(EG_SUS, ENV_UNITS(500));
  s.SEG = SSG_ENABLE | SSG_HOLD;  // hold mode
  s.delta_sr = ENV_UNITS(600);

  (void)calc_eg(&s, 0);

  REQUIRE(s.volume == MAX_ATT_INDEX);  // GEMS-PIN: should be SSG_ATT_THRESHOLD
  REQUIRE(s.state == EG_OFF);
}

TEST_CASE("calc_eg SSG-EG alternate toggles ssg_inv on loop",
          "[ym2612][eg][gems-pin]")
{
  // GEMS-PIN: TD-020 changes inversion math (SSG_ATT_THRESHOLD - volume).
  FM_SLOT s = make_slot(EG_SUS, ENV_UNITS(500));
  s.SEG = SSG_ENABLE | SSG_ALTERNATE;  // loop + alternate
  s.ssg_inv = 0;
  s.delta_sr = ENV_UNITS(600);

  (void)calc_eg(&s, 0);

  REQUIRE(s.ssg_inv == 1);  // ALTERNATE toggles inversion each loop
  REQUIRE(s.state == EG_ATT);
}

TEST_CASE("calc_eg SSG-EG inversion reflects output as MAX - volume",
          "[ym2612][eg][gems-pin]")
{
  // GEMS-PIN: TD-020 uses SSG_ATT_THRESHOLD - volume, not MAX_ATT_INDEX.
  FM_SLOT s = make_slot(EG_SUS, ENV_UNITS(400));  // held, no crossing
  s.delta_sr = 0;
  s.SEG = SSG_ENABLE;
  s.ssg_inv = 1;  // force inverted output
  s.TLL = 0;

  const unsigned int out = calc_eg(&s, 0);

  // Inverted output = (MAX_ATT_INDEX - volume) >> ENV_SH = (1023 - 400) = 623.
  REQUIRE(out == static_cast<unsigned int>((MAX_ATT_INDEX - ENV_UNITS(400)) >>
                                           ENV_SH));
}

// ---------------------------------------------------------------------------
// CALC_FCSLOT: phase increment (TD-019 — 17-bit detune overflow mask applied)
// ---------------------------------------------------------------------------

TEST_CASE("CALC_FCSLOT sets Incr from masked detune+multiple", "[ym2612][eg]")
{
  // TD-019 applied: the detune addition is masked to 17 bits before the
  // multiply. For non-overflowing inputs the result is unchanged from the
  // previous unmasked form.
  // DT is indexed by kc (the key code), which in the real chip spans 0..127;
  // size the local table accordingly.
  INT32 dt_table[128] = {0};
  dt_table[3] = 300;  // detune value at the kc we will use
  FM_SLOT s{};
  s.DT = dt_table;
  s.mul = 2;
  s.KSR = 1;
  s.ksr = 0;  // force a kcode that yields ksr != current, exercising the rate
              // branch
  s.ARval = 0;
  // Provide rate tables so the rate-recalc branch can index them.
  static UINT32 rates[64] = {0};
  for (int i = 0; i < 64; ++i)
    rates[i] = 1000 + i;
  s.AR = rates;
  s.DR = rates;
  s.SR = rates;
  s.RR = rates;

  const int fc = 0x1234;
  const int kc = 3;  // ksr = kc >> KSR = 3 >> 1 = 1

  CALC_FCSLOT(&s, fc, kc);

  // fc + DT[kc] = 0x1234 + 300 = 0x1360 (< 0x1ffff, no truncation).
  // Incr = (0x1360 * 2) >> 1 = 0x1360.
  const int masked_fc = (fc + dt_table[kc]) & 0x1ffff;
  const int expected_incr = (masked_fc * static_cast<int>(s.mul)) >> 1;
  REQUIRE(static_cast<int>(s.Incr) == expected_incr);
  // ksr changed (0 -> 1), so the rate-recalc branch must have run.
  REQUIRE(s.ksr == 1);
  REQUIRE(s.delta_ar == rates[1]);
}

TEST_CASE("CALC_FCSLOT masks detune overflow to 17 bits (TD-019)",
          "[ym2612][eg][gems-fix]")
{
  // The regression TD-019 fixes: when fc + DT[kc] exceeds the 17-bit FNUM
  // range, the old unmasked code would carry the overflow into the multiply,
  // producing a wildly wrong phase increment. The mask truncates it.
  INT32 dt_table[128] = {0};
  dt_table[5] = 0x8000;  // large detune at kc=5
  FM_SLOT s{};
  s.DT = dt_table;
  s.mul = 1;
  s.KSR = 0;
  s.ksr = -1;  // any value != 5 to exercise the rate branch
  s.ARval = 0;
  static UINT32 rates[64] = {0};
  s.AR = s.DR = s.SR = s.RR = rates;

  const int fc = 0x18000;  // fc + DT = 0x18000 + 0x8000 = 0x20000 (> 0x1ffff)
  const int kc = 5;

  CALC_FCSLOT(&s, fc, kc);

  // Masked: 0x20000 & 0x1ffff = 0 (bit 17 is cleared by the 17-bit mask).
  // Incr = (0 * 1) >> 1 = 0.
  // Unmasked (pre-TD-019): (0x20000 * 1) >> 1 = 0x10000 — wrong.
  const int masked = (fc + dt_table[kc]) & 0x1ffff;
  REQUIRE(masked == 0);
  REQUIRE(static_cast<int>(s.Incr) ==
          ((masked * static_cast<int>(s.mul)) >> 1));
  REQUIRE(static_cast<int>(s.Incr) == 0);
}

TEST_CASE("CALC_FCSLOT skips rate recalc when ksr unchanged", "[ym2612][eg]")
{
  INT32 dt_table[8] = {0};
  FM_SLOT s{};
  s.DT = dt_table;
  s.mul = 1;
  s.KSR = 0;  // ksr = kc >> 0 = kc
  s.ksr = 5;  // pre-set; kc=5 yields ksr=5 => unchanged
  s.ARval = 0;
  static UINT32 rates[64] = {};
  s.AR = s.DR = s.SR = s.RR = rates;

  CALC_FCSLOT(&s, 100, 5);

  REQUIRE(s.ksr == 5);  // unchanged
  // delta_* untouched because the rate-recalc branch didn't run.
  REQUIRE(s.delta_ar == 0u);
}
