// Unit tests for xbrz/xbrz_tools.h — pure pixel manipulation helpers.

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "xbrz_tools.h"

TEST_CASE("getByte<N> extracts the Nth byte", "[xbrz][tools]") {
  constexpr uint32_t pix = 0xDEADBEEFu;
  CHECK(xbrz::getByte<0>(pix) == 0xEF);
  CHECK(xbrz::getByte<1>(pix) == 0xBE);
  CHECK(xbrz::getByte<2>(pix) == 0xAD);
  CHECK(xbrz::getByte<3>(pix) == 0xDE);
}

TEST_CASE("ARGB channel accessors map to the documented byte positions",
          "[xbrz][tools]") {
  constexpr uint32_t pix = 0x11223344u;  // A=11 R=22 G=33 B=44
  CHECK(xbrz::getAlpha(pix) == 0x11);
  CHECK(xbrz::getRed(pix) == 0x22);
  CHECK(xbrz::getGreen(pix) == 0x33);
  CHECK(xbrz::getBlue(pix) == 0x44);
}

TEST_CASE("makePixel packs ARGB in the same layout the getters read",
          "[xbrz][tools]") {
  const uint32_t packed = xbrz::makePixel(0x11, 0x22, 0x33, 0x44);
  CHECK(packed == 0x11223344u);
  CHECK(xbrz::getAlpha(packed) == 0x11);
  CHECK(xbrz::getRed(packed) == 0x22);
  CHECK(xbrz::getGreen(packed) == 0x33);
  CHECK(xbrz::getBlue(packed) == 0x44);
}

TEST_CASE("makePixel RGB overload leaves the alpha byte zero",
          "[xbrz][tools]") {
  const uint32_t packed = xbrz::makePixel(0x22, 0x33, 0x44);
  CHECK(packed == 0x00223344u);
  CHECK(xbrz::getAlpha(packed) == 0x00);
}

TEST_CASE("rgb555to888 expands 5-bit channels into the high bits of each byte",
          "[xbrz][tools]") {
  // Spot-check the extremes.
  CHECK(xbrz::rgb555to888(0x0000) == 0x00000000u);
  CHECK(xbrz::rgb555to888(0x7FFF) == 0x00F8F8F8u);  // max 5-bit => 0xF8

  // Pure red channel: 0x7C00 -> R=0xF8.
  CHECK(xbrz::rgb555to888(0x7C00) == 0x00F80000u);
  // Pure green channel: 0x03E0 -> G=0xF8.
  CHECK(xbrz::rgb555to888(0x03E0) == 0x0000F800u);
  // Pure blue channel: 0x001F -> B=0xF8.
  CHECK(xbrz::rgb555to888(0x001F) == 0x000000F8u);
}

TEST_CASE("rgb565to888 expands 5/6/5 channels into high-byte positions",
          "[xbrz][tools]") {
  CHECK(xbrz::rgb565to888(0x0000) == 0x00000000u);
  CHECK(xbrz::rgb565to888(0xFFFF) == 0x00F8FCF8u);  // 5-bit => 0xF8, 6-bit => 0xFC

  CHECK(xbrz::rgb565to888(0xF800) == 0x00F80000u);  // red only
  CHECK(xbrz::rgb565to888(0x07E0) == 0x0000FC00u);  // green only (6-bit)
  CHECK(xbrz::rgb565to888(0x001F) == 0x000000F8u);  // blue only
}

TEST_CASE("rgb888 down-converters truncate to the top 5/6 bits per channel",
          "[xbrz][tools]") {
  // 0xFF round-trips to its high-bits-preserved form.
  CHECK(xbrz::rgb888to555(0x00FFFFFFu) == 0x7FFF);
  CHECK(xbrz::rgb888to565(0x00FFFFFFu) == 0xFFFF);

  // Low bits of each channel get dropped.
  CHECK(xbrz::rgb888to555(0x00F80000u) == 0x7C00);  // red only
  CHECK(xbrz::rgb888to555(0x0000F800u) == 0x03E0);  // green only
  CHECK(xbrz::rgb888to555(0x000000F8u) == 0x001F);  // blue only

  CHECK(xbrz::rgb888to565(0x00F80000u) == 0xF800);
  CHECK(xbrz::rgb888to565(0x0000FC00u) == 0x07E0);
  CHECK(xbrz::rgb888to565(0x000000F8u) == 0x001F);
}

TEST_CASE("888 <-> 555 round-trips for values already at 5-bit precision",
          "[xbrz][tools]") {
  for (uint16_t v = 0; v < 0x8000u; v += 0x123) {
    const uint32_t wide = xbrz::rgb555to888(v);
    CHECK(xbrz::rgb888to555(wide) == v);
  }
}

TEST_CASE("888 <-> 565 round-trips for values already at 5/6/5 precision",
          "[xbrz][tools]") {
  for (uint32_t v = 0; v <= 0xFFFFu; v += 0x123) {
    const uint32_t wide = xbrz::rgb565to888(static_cast<uint16_t>(v));
    CHECK(xbrz::rgb888to565(wide) == static_cast<uint16_t>(v));
  }
}
