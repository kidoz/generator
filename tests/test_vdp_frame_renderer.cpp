// Characterization tests for generator::ui::VdpFrameRenderer
// (src/ui/common/vdp_frame_renderer.cpp), the paletted-scanline ->
// ARGB8888 conversion the windowed UI backends share.
//
// Before the extraction each backend carried its own copy of this loop and
// they drifted. The behaviour pinned here existed in one copy only: the
// opaque-alpha fixup, without which a backend that honours the alpha
// channel draws a fully transparent field, because the uiplot palette
// cache carries no alpha.
//
// The renderer reaches the palette through the CRAM snapshot the core
// publishes (uiplot_set_cram), so these tests publish one directly instead
// of standing up a machine.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "generator.h"
#include "ui.h"
#include "uiplot.h"
#include "uiplot_cram.h"

#include "vdp_frame_renderer.hpp"

[[noreturn]] void ui_err(const char *msg, ...)
{
  fprintf(stderr, "ui_err: %s\n", msg);
  exit(1);
}

namespace {

constexpr unsigned int kWidth = generator::ui::VdpFrameRenderer::kMaxWidth;

/* A CRAM snapshot with every entry distinct, so a conversion that picked
 * the wrong index would produce the wrong colour rather than a coincidence.
 * CRAM is 64 words of 0000BBB0GGG0RRR0. */
struct Palette {
  Palette()
  {
    for (unsigned int i = 0; i < 64; i++) {
      cram[i] = (uint16_t)(((i & 7) << 1) |        /* red */
                           (((i >> 3) & 7) << 5) | /* green */
                           (((i >> 3) & 7) << 9)); /* blue */
      dirty[i] = 1;
    }
    uiplot_set_cram(cram.data(), dirty.data());
  }

  std::array<uint16_t, 64> cram{};
  std::array<uint8_t, 64> dirty{};
};

}  // namespace

TEST_CASE("render_pushed converts a full-width line", "[vdp-frame-renderer]")
{
  Palette palette;
  uiplot_setshifts(16, 8, 0);
  uiplot_setmasks(0x00FF0000, 0x0000FF00, 0x000000FF);

  generator::ui::VdpFrameRenderer renderer;
  std::array<uint8_t, kWidth> pixels{};
  for (unsigned int x = 0; x < kWidth; x++)
    pixels[x] = (uint8_t)(x & 0x3F);

  std::array<uint32_t, kWidth> row{};
  renderer.render_pushed(0, pixels, row.data());

  REQUIRE(renderer.field_width() == kWidth);
  REQUIRE(renderer.field_lines() == 1);
}

/* The core stores CRAM as 16-bit words, so the palette cache has to read
 * words. It used to be handed a byte pointer instead, which on a
 * little-endian host swapped red with blue and zeroed green -- a
 * whole-screen colour fault that no other test would have caught. */
TEST_CASE("render_pushed maps each CRAM channel to the right output channel",
          "[vdp-frame-renderer]")
{
  uiplot_setshifts(16, 8, 0);
  uiplot_setmasks(0x00FF0000, 0x0000FF00, 0x000000FF);

  /* One pure channel each, at full level: 0000_BBB0_GGG0_RRR0. */
  std::array<uint16_t, 64> cram{};
  std::array<uint8_t, 64> dirty{};
  cram[1] = 0x000E; /* red  */
  cram[2] = 0x00E0; /* green */
  cram[3] = 0x0E00; /* blue */
  dirty.fill(1);
  uiplot_set_cram(cram.data(), dirty.data());

  generator::ui::VdpFrameRenderer renderer;
  std::array<uint8_t, 4> pixels{0, 1, 2, 3};
  std::array<uint32_t, 4> row{};

  renderer.render_pushed(0, pixels, row.data());

  /* 3-bit level 7 expands to (14 << 4) | (14 >> 1) = 231. */
  REQUIRE((row[1] & 0x00FFFFFFU) == 0x00E70000U); /* red only   */
  REQUIRE((row[2] & 0x00FFFFFFU) == 0x0000E700U); /* green only */
  REQUIRE((row[3] & 0x00FFFFFFU) == 0x000000E7U); /* blue only  */
}

TEST_CASE("render_pushed forces every converted pixel opaque",
          "[vdp-frame-renderer]")
{
  Palette palette;
  uiplot_setshifts(16, 8, 0);
  uiplot_setmasks(0x00FF0000, 0x0000FF00, 0x000000FF);

  generator::ui::VdpFrameRenderer renderer;
  std::array<uint8_t, kWidth> pixels{}; /* all index 0 -- the darkest entry */
  std::array<uint32_t, kWidth> row{};

  renderer.render_pushed(0, pixels, row.data());

  for (unsigned int x = 0; x < kWidth; x++)
    REQUIRE((row[x] & 0xFF000000U) == 0xFF000000U);
}

TEST_CASE("render_pushed takes its width from the pushed span",
          "[vdp-frame-renderer]")
{
  Palette palette;
  generator::ui::VdpFrameRenderer renderer;

  std::array<uint8_t, 256> narrow{};
  std::array<uint32_t, kWidth> row{};
  row.fill(0xDEADBEEF);

  renderer.render_pushed(0, narrow, row.data());

  REQUIRE(renderer.field_width() == 256);

  /* Pixels past the field are the backend's to manage; the renderer must
     not touch them. */
  for (unsigned int x = 256; x < kWidth; x++)
    REQUIRE(row[x] == 0xDEADBEEF);
}

TEST_CASE("render_pushed clamps a span wider than a field",
          "[vdp-frame-renderer]")
{
  Palette palette;
  generator::ui::VdpFrameRenderer renderer;

  std::array<uint8_t, kWidth * 2> oversized{};
  std::array<uint32_t, kWidth> row{};

  renderer.render_pushed(0, oversized, row.data());

  REQUIRE(renderer.field_width() == kWidth);
}

TEST_CASE("render_pushed tolerates a null destination and an empty span",
          "[vdp-frame-renderer]")
{
  Palette palette;
  generator::ui::VdpFrameRenderer renderer;
  std::array<uint8_t, kWidth> pixels{};
  std::array<uint32_t, kWidth> row{};

  renderer.render_pushed(0, pixels, nullptr);
  renderer.render_pushed(0, std::span<const uint8_t>{}, row.data());

  REQUIRE(renderer.field_lines() == 0);
}

TEST_CASE("end_field reports the tallest line and resets the latch",
          "[vdp-frame-renderer]")
{
  Palette palette;
  generator::ui::VdpFrameRenderer renderer;
  std::array<uint8_t, kWidth> pixels{};
  std::array<uint32_t, kWidth> row{};

  for (int line = 0; line < 224; line++)
    renderer.render_pushed(line, pixels, row.data());

  REQUIRE(renderer.end_field() == 224);
  REQUIRE(renderer.field_lines() == 0);
  REQUIRE(renderer.field_width() == 0);

  /* A field the core cut short publishes only the rows it produced. */
  for (int line = 0; line < 10; line++)
    renderer.render_pushed(line, pixels, row.data());

  REQUIRE(renderer.end_field() == 10);
}
