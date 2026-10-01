#include "ui_bridge.hpp"
#include "generator_app.hpp"
#include "emulator_core.hpp"
#include "screen_geometry.hpp"

#include "sdl_audio_backend.hpp"
#include "stream_logger.hpp"
#include "vdp_frame_renderer.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <span>

#include "generator.h"
#include "ui.h"
#include "uiplot.h"
#include "gensoundp.h"
#include "initcart.h"

using namespace generator;

// Global UI application reference
Glib::RefPtr<GeneratorApp> g_app;

// Global emulator core instance
std::unique_ptr<EmulatorCore> g_emulator_core;

// Stored argc/argv for ui_loop
static int g_argc = 0;
static char **g_argv = nullptr;

/* Pixel buffers (geometry in screen_geometry.hpp). The bank pointers stay
 * private to this file: the emulation thread rotates them in
 * present_field() and the UI thread reads them only through
 * ui_take_frame(), which holds g_frame_mutex so a snapshot can never
 * overlap the rotation or the next field's rendering into g_newscreen. */
static uint8_t *g_screen_buffers[3] = {nullptr, nullptr, nullptr};
static uint8_t *g_screen0 = nullptr;
static uint8_t *g_screen1 = nullptr;
static uint8_t *g_newscreen = nullptr;
static int g_whichbank = 0;
static bool g_plotfield = true;

/* Guards the bank rotation and the published field size below. Contended
 * only for the span of one memcpy out of a bank, well under a field. */
static std::mutex g_frame_mutex;
static int g_frame_width = 0;
static int g_frame_height = 0;

/* Audio and logging come from src/ui/common; only the video path is
   specific to this backend, and only because of how it publishes fields. */

/* Converts VDP scanlines into the fixed-stride ARGB8888 buffer GDK uploads.
   The conversion itself -- the palette cache and the opaque-alpha fixup --
   lives in the shared generator::ui::VdpFrameRenderer. */
class UiBridgeVideo : public IVideoBackend {
public:
  void render_line(int line, std::span<const uint8_t> pixels) override
  {
    if (!g_plotfield || !g_emulator_core)
      return;

    if (line < 0 || line >= static_cast<int>(VMAXSIZE))
      return;

    renderer_.render_pushed(
        line, pixels,
        reinterpret_cast<uint32_t *>(g_newscreen + line * HMAXSIZE * 4));
  }

  void present_field() override
  {
    /* Read the width before ending the field: end_field() resets the
       latch, so reading it afterwards always yields 0. Always end the
       field, even when not plotting, so the latch does not carry over
       into the next one. */
    const unsigned int width = renderer_.field_width();
    const int lines = renderer_.end_field();

    if (!g_plotfield)
      return;

    /* Publish under the frame lock: rotate the finished field into a
       display bank and record what it contains. Publishing only the
       lines actually rendered keeps a field the VDP cut short from
       showing stale rows out of the previous one. */
    std::lock_guard<std::mutex> lock(g_frame_mutex);

    int current_bank = g_whichbank;
    int next_bank = current_bank ^ 1;

    uint8_t *temp = g_newscreen;
    if (next_bank == 0) {
      g_newscreen = g_screen0;
      g_screen0 = temp;
    } else {
      g_newscreen = g_screen1;
      g_screen1 = temp;
    }
    g_whichbank = next_bank;

    if (lines > 0) {
      g_frame_width = static_cast<int>(width);
      g_frame_height = lines;
    }
  }

private:
  generator::ui::VdpFrameRenderer renderer_;
};

Glib::RefPtr<Glib::Bytes> ui_take_frame(int *width, int *height)
{
  *width = 0;
  *height = 0;

  std::lock_guard<std::mutex> lock(g_frame_mutex);
  if (g_frame_width <= 0 || g_frame_height <= 0)
    return {};

  const uint8_t *bank = (g_whichbank == 0) ? g_screen0 : g_screen1;
  if (!bank)
    return {};

  /* Glib::Bytes::create copies, so the snapshot stays coherent even after
     the emulation thread rotates this bank back under the writer. */
  *width = g_frame_width;
  *height = g_frame_height;
  return Glib::Bytes::create(bank,
                             static_cast<gsize>(g_frame_height) * HMAXSIZE * 4);
}

/*** ui_init - called by main() in generator.c ***/
int ui_init(int argc, char *argv[])
{
  g_argc = argc;
  g_argv = argv;

  // Allocate pixel buffers
  g_screen_buffers[0] = (uint8_t *)calloc(1, 4 * HMAXSIZE * VMAXSIZE);
  g_screen_buffers[1] = (uint8_t *)calloc(1, 4 * HMAXSIZE * VMAXSIZE);
  g_screen_buffers[2] = (uint8_t *)calloc(1, 4 * HMAXSIZE * VMAXSIZE);
  g_screen0 = g_screen_buffers[0];
  g_screen1 = g_screen_buffers[1];
  g_newscreen = g_screen_buffers[2];

  uiplot_setshifts(16, 8, 0);
  uiplot_setmasks(0x00FF0000, 0x0000FF00, 0x000000FF);

  return 0;
}

/*** ui_loop - enters the main application loop ***/
int ui_loop(void)
{
  /* Opened before the core: the emulation thread pushes a field as soon
   * as it runs, and the field pacing reads the queue depth. Failing here
   * is not fatal - the platform layer drops samples with no device. */
  if (soundp_start() != 0) {
    std::cerr << "Continuing without sound: no audio device" << std::endl;
  }

  try {
    g_emulator_core = std::make_unique<EmulatorCore>(
        std::make_unique<generator::ui::SdlAudioBackend>(),
        std::make_unique<UiBridgeVideo>(),
        std::make_shared<generator::ui::StreamLogger>());

    std::span<const uint8_t> initcart_span(initcart, initcart_len);
    auto res = g_emulator_core->load_rom_mem(initcart_span);
    if (!res) {
      std::cerr << "Failed to load splash screen ROM: " << res.error()
                << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << "FATAL ERROR: Failed to initialize EmulatorCore: " << e.what()
              << std::endl;
    return 1;
  }

  g_app = GeneratorApp::create();
  return g_app->run(g_argc, g_argv);
}

/* ui_err comes from src/ui/common/ui_error.cpp. */

/*** ui_final - graceful shutdown ***/
void ui_final(void)
{
  g_emulator_core.reset();
  soundp_stop();

  // Free buffers
  free(g_screen_buffers[0]);
  free(g_screen_buffers[1]);
  free(g_screen_buffers[2]);
}
