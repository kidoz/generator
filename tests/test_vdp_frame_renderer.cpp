// Characterization tests for generator::ui::VdpFrameRenderer
// (src/ui/common/vdp_frame_renderer.cpp), the VDP scanline -> ARGB8888
// conversion the windowed UI backends share.
//
// Before the extraction each backend carried its own copy of this loop and
// they drifted. Two of the behaviours pinned here existed in one copy only:
//
//   * the field-width latch, which holds the width chosen on the field's
//     first visible line for the rest of the field. Without it a game that
//     flips H32/H40 partway through a frame leaves rows at two different
//     widths in one buffer.
//   * the opaque-alpha fixup, without which a backend that honours the
//     alpha channel draws a fully transparent field, because the uiplot
//     palette cache carries no alpha.
//
// vdp.cpp, uiplot.cpp and system.cpp are compiled directly into the test;
// the emulator globals they reference are stubbed below, following
// tests/test_vdp.cpp.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "generator.h"
#include "cpu68k.h"
#include "event.h"
#include "ui.h"
#include "uiplot.h"

#include "system.hpp"
#include "vdp.hpp"
#include "vdp_frame_renderer.hpp"

// --- storage and stubs referenced by vdp.cpp ---

static uint8 ram_storage[0x10000];
static uint8 rom_storage[0x200000];

void state_transfer8(const char *, const char *, uint8, uint8 *, uint32) {}
void state_transfer16(const char *, const char *, uint8, uint16 *, uint32) {}
void state_transfer32(const char *, const char *, uint8, uint32 *, uint32) {}

uint8 *cpu68k_ram = ram_storage;
uint8 *cpu68k_rom = rom_storage;
unsigned int cpu68k_clocks = 0;
t_regs regs;

void event_freeze(unsigned int bytes) { (void)bytes; }
void event_freeze_clocks(unsigned int clocks) { (void)clocks; }

[[noreturn]] void ui_err(const char *msg, ...)
{
  fprintf(stderr, "ui_err: %s\n", msg);
  exit(1);
}

namespace {

/* Installs a System for the duration of a test: VdpFrameRenderer and
   uiplot both reach the chip through generator::vdp(), which requires an
   active System. */
class ActiveSystem {
public:
  ActiveSystem()
      : system_(std::unique_ptr<generator::IAudioBackend>{},
                std::unique_ptr<generator::IVideoBackend>{},
                std::shared_ptr<generator::ILogger>{})
  {
    generator::set_system(&system_);

    generator::Vdp &chip = system_.vdp();
    chip.vdp_reset();

    /* vdp_reset leaves the geometry fields at whatever setupvideo computed;
       pin the visible height so the visibility checks below are about the
       renderer and not about mode detection. */
    chip.vdp_vislines = 224;

    /* uiplot converts through its palette cache, which is only refreshed
       for entries the VDP marked dirty. Force a full rebuild once so
       render_into produces defined pixels. */
    uiplot_setshifts(16, 8, 0);
    uiplot_setmasks(0x00FF0000, 0x0000FF00, 0x000000FF);
    uiplot_checkpalcache(1);
  }

  ~ActiveSystem()
  {
    generator::set_system(nullptr);
  }

  ActiveSystem(const ActiveSystem &) = delete;
  ActiveSystem &operator=(const ActiveSystem &) = delete;

  generator::Vdp &vdp()
  {
    return system_.vdp();
  }

private:
  generator::System system_;
};

/* Register 12 bit 0 selects H40 (320 pixels); clear means H32 (256). */
void set_h40(generator::Vdp &chip, bool h40)
{
  if (h40)
    chip.vdp_reg[12] |= 1;
  else
    chip.vdp_reg[12] &= ~1;
}

}  // namespace

TEST_CASE("field width follows register 12 at the start of a field",
          "[vdp-frame-renderer]")
{
  ActiveSystem active;
  generator::ui::VdpFrameRenderer renderer;

  SECTION("H40 reports 320")
  {
    set_h40(active.vdp(), true);
    REQUIRE(renderer.begin_line(0) == 320);
    REQUIRE(renderer.field_width() == 320);
  }

  SECTION("H32 reports 256")
  {
    set_h40(active.vdp(), false);
    REQUIRE(renderer.begin_line(0) == 256);
    REQUIRE(renderer.field_width() == 256);
  }
}

TEST_CASE("the field width latches for the whole field",
          "[vdp-frame-renderer]")
{
  ActiveSystem active;
  generator::ui::VdpFrameRenderer renderer;

  set_h40(active.vdp(), true);
  REQUIRE(renderer.begin_line(0) == 320);

  // The game switches to H32 partway down the field. Every remaining line
  // must still report the width the field started at: rows of two different
  // widths in one buffer either shift the tail of the field or leave stale
  // pixels beyond the narrower rows.
  set_h40(active.vdp(), false);

  REQUIRE(renderer.begin_line(1) == 320);
  REQUIRE(renderer.begin_line(100) == 320);
  REQUIRE(renderer.begin_line(223) == 320);

  // The next field picks up the current setting.
  renderer.end_field();
  REQUIRE(renderer.begin_line(0) == 256);
}

TEST_CASE("lines outside the visible field are rejected",
          "[vdp-frame-renderer]")
{
  ActiveSystem active;
  generator::ui::VdpFrameRenderer renderer;

  set_h40(active.vdp(), true);

  REQUIRE(renderer.begin_line(-1) == 0);
  REQUIRE(renderer.begin_line(224) == 0);
  REQUIRE(renderer.begin_line(10000) == 0);

  // A rejected line must not latch a width, so the field is still open.
  REQUIRE(renderer.field_width() == 0);
  REQUIRE(renderer.begin_line(0) == 320);
}

TEST_CASE("end_field reports the lines rendered and resets the latch",
          "[vdp-frame-renderer]")
{
  ActiveSystem active;
  generator::ui::VdpFrameRenderer renderer;
  std::array<uint32_t, generator::ui::VdpFrameRenderer::kMaxWidth> row{};

  set_h40(active.vdp(), true);

  REQUIRE(renderer.field_lines() == 0);

  for (int line : {0, 1, 2, 41}) {
    REQUIRE(renderer.begin_line(line) == 320);
    renderer.render_into(line, row.data());
  }

  // Highest line rendered plus one -- a field the VDP cut short must not
  // report the full height, or the backend publishes stale rows.
  REQUIRE(renderer.field_lines() == 42);

  REQUIRE(renderer.end_field() == 42);
  REQUIRE(renderer.field_lines() == 0);
  REQUIRE(renderer.field_width() == 0);
}

TEST_CASE("rendered pixels are opaque", "[vdp-frame-renderer]")
{
  ActiveSystem active;
  generator::ui::VdpFrameRenderer renderer;
  std::array<uint32_t, generator::ui::VdpFrameRenderer::kMaxWidth> row{};

  set_h40(active.vdp(), true);
  row.fill(0);

  const unsigned int width = renderer.begin_line(0);
  REQUIRE(width == 320);
  renderer.render_into(0, row.data());

  for (unsigned int x = 0; x < width; x++)
    REQUIRE((row[x] & 0xFF000000U) == 0xFF000000U);
}

TEST_CASE("render_into leaves pixels beyond the field width untouched",
          "[vdp-frame-renderer]")
{
  ActiveSystem active;
  generator::ui::VdpFrameRenderer renderer;
  std::array<uint32_t, generator::ui::VdpFrameRenderer::kMaxWidth> row{};

  set_h40(active.vdp(), false);
  row.fill(0xDEADBEEF);

  REQUIRE(renderer.begin_line(0) == 256);
  renderer.render_into(0, row.data());

  for (unsigned int x = 256; x < row.size(); x++)
    REQUIRE(row[x] == 0xDEADBEEF);
}

TEST_CASE("render_into tolerates a null destination", "[vdp-frame-renderer]")
{
  ActiveSystem active;
  generator::ui::VdpFrameRenderer renderer;

  set_h40(active.vdp(), true);
  REQUIRE(renderer.begin_line(0) == 320);

  // A backend whose frame buffer refuses the row passes null; that must not
  // render, and must not count the line as rendered either.
  renderer.render_into(0, nullptr);
  REQUIRE(renderer.field_lines() == 0);
}
