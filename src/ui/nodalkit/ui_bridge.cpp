/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NodalKit UI backend entry points and core backends */

#include "ui_bridge.hpp"
#include "generator_app.hpp"

#include "emulator_core.hpp"
#include "vdp.hpp"

#include "generator.h"
#include "initcart.h"
#include "ui.h"
#include "uiplot.h"
#include "vdp.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <span>

namespace generator::nkui {

std::unique_ptr<EmulatorCore> g_emulator_core;
FrameBuffer g_frames;
std::string g_startup_rom;

namespace {

void print_usage(const char *program)
{
  fprintf(stderr,
          "Usage: %s [-v level] [rom]\n"
          "  -v level   log verbosity, 0 (silent) to 7 (debug)\n"
          "  rom        Mega Drive / Genesis image to load at startup\n",
          program);
}

/* Generator's main() hands the whole command line to the UI backend. This
 * one takes a verbosity flag and a ROM path; everything else the emulator
 * exposes is reachable from the menus. */
std::string parse_arguments(int argc, char **argv)
{
  std::string rom;

  for (int i = 1; i < argc; i++) {
    const std::string_view arg = argv[i] ? argv[i] : "";
    if (arg == "-v" || arg == "--verbose") {
      if (i + 1 < argc)
        gen_loglevel = static_cast<unsigned int>(atoi(argv[++i]));
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      continue;
    }
    if (!arg.empty() && arg.front() != '-' && rom.empty())
      rom = arg;
  }

  return rom;
}

/* Audio reaches the hardware through the SDL3 platform layer
 * (gensoundp_sdl3.cpp), which gensound.cpp calls directly. The backend seam
 * exists so the core does not need to know that. */
class NodalkitAudio : public IAudioBackend {
public:
  void output_samples(std::span<const uint16_t> /*left*/,
                      std::span<const uint16_t> /*right*/) override
  {
  }
};

/* Converts VDP scanlines into the ARGB8888 fields nk::ImageView consumes. */
class NodalkitVideo : public IVideoBackend {
public:
  void render_line(int line, std::span<const uint8_t> /*pixels*/) override
  {
    auto &vdp = generator::vdp();

    if (line < 0 || line >= static_cast<int>(vdp.vdp_vislines))
      return;

    /* Rows are packed at the field's width, so the width has to be fixed for
     * the whole field: a game that flips H32/H40 mid-frame would otherwise
     * leave rows at two different strides in one buffer. The first line of
     * the field decides, and the rest of the field follows it. */
    if (field_width_ == 0)
      field_width_ = (vdp.vdp_reg[12] & 1) ? 320 : 256;

    const unsigned int width = static_cast<unsigned int>(field_width_);
    uint32_t *dest = g_frames.row(line, field_width_);
    if (!dest)
      return;

    field_lines_ = std::max(field_lines_, line + 1);

    switch ((vdp.vdp_reg[12] >> 1) & 3) {
    case 0: /* normal */
    case 1: /* interlace, simply doubled up */
    case 2: /* invalid */
      vdp_renderline(static_cast<unsigned int>(line), gfx_, 0);
      break;
    case 3: /* interlace with double resolution */
      vdp_renderline(static_cast<unsigned int>(line), gfx_, vdp.vdp_oddframe);
      break;
    }

    uiplot_checkpalcache(0);
    uiplot_convertdata32(gfx_, dest, width);

    /* uiplot's palette cache carries no alpha channel, but NodalKit reads
     * the buffer as ARGB8888 and honours it, so opaque has to be set here. */
    for (unsigned int x = 0; x < width; x++)
      dest[x] |= 0xFF000000U;
  }

  void present_field() override
  {
    /* Publish only what was actually rendered; a field the VDP cut short
     * would otherwise show stale rows from the previous one. */
    if (field_width_ > 0 && field_lines_ > 0)
      g_frames.publish(field_width_, field_lines_);

    field_width_ = 0;
    field_lines_ = 0;
  }

private:
  uint8 gfx_[320] = {};
  int field_width_ = 0;
  int field_lines_ = 0;
};

class NodalkitLogger : public ILogger {
public:
  void log(LogLevel level, std::string_view message) override
  {
    switch (level) {
    case LogLevel::None:
    case LogLevel::Debug3:
    case LogLevel::Debug2:
    case LogLevel::Debug1:
      break;
    case LogLevel::Verbose:
    case LogLevel::Normal:
    case LogLevel::User:
      std::cout << message << std::endl;
      break;
    case LogLevel::Critical:
      std::cerr << "CRITICAL: " << message << std::endl;
      break;
    }
  }
};

}  // namespace

}  // namespace generator::nkui

/*** ui_init - called by main() before the UI loop starts ***/

int ui_init(int argc, char *argv[])
{
  /* Parsed here rather than in the app controller: the verbosity flag has
   * to be in place before EmulatorCore initialises the subsystems. */
  generator::nkui::g_startup_rom = generator::nkui::parse_arguments(argc, argv);

  /* ARGB8888: NodalKit's ImageNode reads alpha from bits 24-31, red from
   * 16-23, green from 8-15 and blue from 0-7. */
  uiplot_setshifts(16, 8, 0);
  uiplot_setmasks(0x00FF0000, 0x0000FF00, 0x000000FF);

  return 0;
}

/*** ui_loop - build the emulator core and enter the NodalKit event loop ***/

int ui_loop(void)
{
  using generator::nkui::g_emulator_core;

  try {
    g_emulator_core = std::make_unique<generator::EmulatorCore>(
        std::make_unique<generator::nkui::NodalkitAudio>(),
        std::make_unique<generator::nkui::NodalkitVideo>(),
        std::make_shared<generator::nkui::NodalkitLogger>());

    const std::span<const uint8_t> splash(initcart, initcart_len);
    auto loaded = g_emulator_core->load_rom_mem(splash);
    if (!loaded) {
      std::cerr << "Failed to load splash screen ROM: " << loaded.error()
                << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << "FATAL ERROR: failed to initialise the emulator core: "
              << e.what() << std::endl;
    return 1;
  }

  /* The app has to go out of scope before ui_final(): its destructor joins
   * the emulation thread, and that thread dereferences the core ui_final()
   * destroys. */
  int status = 0;
  {
    generator::nkui::GeneratorApp app;
    status = app.run();
  }

  ui_final();
  return status;
}

/*** ui_err - fatal error exit ***/

[[noreturn]] void ui_err(const char *text, ...)
{
  va_list ap;

  va_start(ap, text);
  fprintf(stderr, "FATAL ERROR: ");
  vfprintf(stderr, text, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

/*** ui_final - graceful shutdown ***/

void ui_final(void)
{
  generator::nkui::g_emulator_core.reset();
}
