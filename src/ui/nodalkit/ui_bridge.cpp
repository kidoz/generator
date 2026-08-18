/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NodalKit UI backend entry points and core backends */

#include "ui_bridge.hpp"
#include "generator_app.hpp"

#include "emulator_core.hpp"
#include "null_audio_backend.hpp"
#include "stream_logger.hpp"
#include "vdp_frame_renderer.hpp"

#include "generator.h"
#include "initcart.h"
#include "ui.h"
#include "uiplot.h"

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

/* Converts VDP scanlines into the ARGB8888 fields nk::ImageView consumes.
 * The conversion itself -- interlace handling, the field-width latch, the
 * palette cache and the opaque-alpha fixup -- lives in the shared
 * generator::ui::VdpFrameRenderer; this class only routes the rows into the
 * frame buffer and publishes completed fields. */
class NodalkitVideo : public IVideoBackend {
public:
  void render_line(int line, std::span<const uint8_t> /*pixels*/) override
  {
    const unsigned int width = renderer_.begin_line(line);
    if (width == 0)
      return;

    uint32_t *dest = g_frames.row(line, static_cast<int>(width));
    if (dest == nullptr)
      return;

    renderer_.render_into(line, dest);
  }

  void present_field() override
  {
    /* Publish only what was actually rendered; a field the VDP cut short
     * would otherwise show stale rows from the previous one. */
    const unsigned int width = renderer_.field_width();
    const int lines = renderer_.end_field();

    if (width > 0 && lines > 0)
      g_frames.publish(static_cast<int>(width), lines);
  }

private:
  generator::ui::VdpFrameRenderer renderer_;
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
        std::make_unique<generator::ui::NullAudioBackend>(),
        std::make_unique<generator::nkui::NodalkitVideo>(),
        std::make_shared<generator::ui::StreamLogger>());

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

/* ui_err comes from src/ui/common/ui_error.cpp. */

/*** ui_final - graceful shutdown ***/

void ui_final(void)
{
  generator::nkui::g_emulator_core.reset();
}
