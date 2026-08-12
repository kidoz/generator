// Characterization tests for the extracted OPN/YM2612 LFO step
// (src/audio/ym2612/fm_lfo.cpp: opn_lfo_step).
//
// Pins the CURRENT behavior of the per-sample LFO advance so TD-021 (the PM
// clock 4x-division fix) lands as a visible diff. The helper is pure: the
// waveform table is supplied by the caller, so these tests use a synthetic
// identity ramp (wave[i] == i) rather than the real triangle, making the
// expected AM/PM values trivial to derive.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include "fm_lfo.hpp"  // opn_lfo_step, LFO_SH, LFO_RATE

namespace {

// Identity-ramp waveform: wave[i] == i. Large enough for any index the tests
// produce (idx = lfo_cnt >> LFO_SH; tests use small lfo_cnt values).
constexpr int WAVE_SIZE = 16;
std::array<INT32, WAVE_SIZE> make_ramp()
{
  std::array<INT32, WAVE_SIZE> w{};
  for (int i = 0; i < WAVE_SIZE; ++i)
    w[i] = i;
  return w;
}

// Build a lfo_cnt whose >> LFO_SH yields a desired index.
constexpr UINT32 lfo_cnt_for_index(UINT32 idx)
{
  return idx << LFO_SH;
}

}  // namespace

TEST_CASE("opn_lfo_step advances LFOCnt by lfo_incr", "[ym2612][lfo]")
{
  const auto wave = make_ramp();
  UINT32 amd = 0;
  INT32 pmd = 0;
  const UINT32 cnt = lfo_cnt_for_index(3);

  const UINT32 result = opn_lfo_step(cnt, 1u, wave.data(), 0, &amd, &pmd);
  REQUIRE(result == cnt + 1u);
}

TEST_CASE("opn_lfo_step standard AM uses the precise waveform index",
          "[ym2612][lfo]")
{
  const auto wave = make_ramp();
  UINT32 amd = 0;
  INT32 pmd = 0;
  const UINT32 cnt = lfo_cnt_for_index(5);  // idx == 5

  (void)opn_lfo_step(cnt, 1u, wave.data(), /*invert_am=*/0, &amd, &pmd);

  // AM = wave[5] = 5 (no inversion).
  REQUIRE(amd == 5u);
}

TEST_CASE("opn_lfo_step inverted AM (YM2612) reflects around LFO_RATE",
          "[ym2612][lfo]")
{
  const auto wave = make_ramp();
  UINT32 amd = 0;
  INT32 pmd = 0;
  const UINT32 cnt = lfo_cnt_for_index(5);  // idx == 5, wave[5] == 5

  (void)opn_lfo_step(cnt, 1u, wave.data(), /*invert_am=*/1, &amd, &pmd);

  // Inverted AM = LFO_RATE - wave[5] = 0x10000 - 5.
  REQUIRE(amd == static_cast<UINT32>(LFO_RATE - 5));
}

TEST_CASE("opn_lfo_step PM uses 1/4-rate quantized index (TD-021 fixed)",
          "[ym2612][lfo][gems-fix]")
{
  // TD-021 applied: PM index is quantized to 1/4 the AM rate (idx & ~3u).
  // At idx=5 the PM sample comes from wave[5 & ~3u == 4], not wave[5].
  const auto wave = make_ramp();
  UINT32 amd = 0;
  INT32 pmd = 0;
  const UINT32 cnt = lfo_cnt_for_index(5);  // idx == 5

  (void)opn_lfo_step(cnt, 1u, wave.data(), /*invert_am=*/0, &amd, &pmd);

  // AM still uses the precise index: amd = wave[5] = 5.
  REQUIRE(amd == 5u);
  // PM uses the quantized index: pmd = wave[4] - LFO_RATE/2 = 4 - 32768.
  REQUIRE(pmd == static_cast<INT32>(4 - LFO_RATE / 2));
  // AM and PM now read DIFFERENT samples (the pre-fix code read the same).
  REQUIRE(amd != static_cast<UINT32>(pmd + LFO_RATE / 2));
}

TEST_CASE("opn_lfo_step PM groups four AM samples per PM sample (TD-021)",
          "[ym2612][lfo][gems-fix]")
{
  // The 4x clock division means indices 4,5,6,7 all map to PM index 4.
  const auto wave = make_ramp();
  for (UINT32 idx = 4; idx < 8; ++idx) {
    UINT32 amd = 0;
    INT32 pmd = 0;
    const UINT32 cnt = lfo_cnt_for_index(idx);

    (void)opn_lfo_step(cnt, 1u, wave.data(), /*invert_am=*/0, &amd, &pmd);

    // AM tracks the precise index; PM is pinned to wave[4].
    REQUIRE(amd == idx);
    REQUIRE(pmd == static_cast<INT32>(4 - LFO_RATE / 2));
  }
}
