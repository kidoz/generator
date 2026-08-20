/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Console UI — SDL3-backed frontend for the emulation core.
 *
 * Rewritten to use only the EmulatorCore API: load ROM, run frames,
 * receive video/audio through the backend interfaces. The legacy
 * uip_sdl3 layer provides the SDL window/bank management and
 * keyboard/joystick input; uiplot converts the emulation core's
 * pushed paletted scanlines through the CRAM snapshot. */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <memory>
#include <string>

#include <SDL3/SDL.h>

#include "emulator_core.hpp"
#include "generator.h"
#include "sdl_audio_backend.hpp"

#include "gensoundp.h"

#include "ui.h"
#include "ui_console.h"
#include "uip.h"
#include "uiplot.h"
#include "uiplot_cram.h"
#include "logo.h"
#include "font.h"

/*** Version info ***/
#ifndef VERSION
#define VERSION "0.50"
#endif

static std::unique_ptr<generator::EmulatorCore> console_core;

/* Legacy globals referenced by shared headers */
uint32_t ui_fkeys = 0;
uint8_t ui_vdpsimple = 0;
uint8_t ui_clearnext = 0;
uint8_t ui_fullscreen = 0;
uint8_t ui_info = 0;
uint8_t ui_vsync = 1;
t_interlace ui_interlace = DEINTERLACE_BOB;
t_binding ui_bindings[2] = {{-1, 0}, {-1, 0}};
static int ui_frameskip = 1;
static uint8_t ui_info_wrapper = 0;
static int ui_running = 1;
static unsigned int ui_frame_count = 0;
static int ui_pal = 0;

/* Keyboard state for input mapping */
static struct {
  int up, down, left, right, start, a, b, c;
} pads[2];


/*** Video Backend — receives pushed scanlines ***/
class ConsoleVideo : public generator::IVideoBackend {
public:
  void render_line(int line, std::span<const uint8_t> pixels) override
  {
    if (line < 0 || line >= 240 || pixels.empty()) {
      return;
    }
    /* Convert paletted line to 16-bit through uiplot */
    static uint8_t linebuf[320];
    const unsigned int n =
        std::min((unsigned int)pixels.size(), (unsigned int)sizeof(linebuf));
    memcpy(linebuf, pixels.data(), n);
    for (unsigned int x = n; x < 320; x++) {
      linebuf[x] = 0;
    }
    uiplot_checkpalcache(0);
    uiplot_convertdata16(linebuf, gfx_ + line * 320, 320);
    lines_rendered_ = line + 1;
  }

  void present_field() override
  {
    if (lines_rendered_ > 0) {
      uip_displaybank(0);
      lines_rendered_ = 0;
    }
  }

  uint16_t *framebuffer()
  {
    return gfx_;
  }
  int lines_rendered() const
  {
    return lines_rendered_;
  }

private:
  uint16_t gfx_[320 * 240] = {};
  int lines_rendered_ = 0;
};

static ConsoleVideo *video_backend = nullptr;

/*** Logger ***/
class ConsoleLogger : public generator::ILogger {
public:
  void log(generator::LogLevel level, std::string_view message) override
  {
    switch (level) {
    case generator::LogLevel::Critical:
      fprintf(stderr, "[ERROR] %.*s\n", (int)message.size(), message.data());
      break;
    case generator::LogLevel::User:
    case generator::LogLevel::Normal:
      fprintf(stderr, "[INFO] %.*s\n", (int)message.size(), message.data());
      break;
    default:
      break;
    }
  }
};

/*** Input handling ***/
static void update_input(void)
{
  pads[0].up = pads[0].down = pads[0].left = pads[0].right = 0;
  pads[0].start = pads[0].a = pads[0].b = pads[0].c = 0;
  pads[1].up = pads[1].down = pads[1].left = pads[1].right = 0;
  pads[1].start = pads[1].a = pads[1].b = pads[1].c = 0;

  const bool *keys = SDL_GetKeyboardState(nullptr);
  if (keys[SDL_SCANCODE_UP])
    pads[0].up = 1;
  if (keys[SDL_SCANCODE_DOWN])
    pads[0].down = 1;
  if (keys[SDL_SCANCODE_LEFT])
    pads[0].left = 1;
  if (keys[SDL_SCANCODE_RIGHT])
    pads[0].right = 1;
  if (keys[SDL_SCANCODE_RETURN])
    pads[0].start = 1;
  if (keys[SDL_SCANCODE_A])
    pads[0].a = 1;
  if (keys[SDL_SCANCODE_S])
    pads[0].b = 1;
  if (keys[SDL_SCANCODE_D])
    pads[0].c = 1;

  if (console_core) {
    console_core->set_input(0, pads[0].up, pads[0].down, pads[0].left,
                            pads[0].right, pads[0].start, pads[0].a, pads[0].b,
                            pads[0].c);
    console_core->set_input(1, 0, 0, 0, 0, 0, 0, 0, 0);
  }
}

/*** UI init/final/loop — the ui.h contract ***/

int ui_init(int argc, char *argv[])
{
  t_uipinfo uip_info;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return -1;
  }

  /* Create the EmulatorCore instance */
  ui_create_core();

  /* Load ROM from argv if present */
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') {
      if (console_core) {
        auto result = console_core->load_rom(argv[i]);
        if (!result) {
          fprintf(stderr, "Failed to load ROM: %s\n", result.error().c_str());
          return 1;
        }
        fprintf(stderr, "Loaded: %s\n", argv[i]);
      }
      break;
    }
  }

  memset(&uip_info, 0, sizeof(uip_info));
  /* t_uipinfo fields set by uip_init or left for the platform layer */
  if (uip_init(&uip_info) != 0) {
    fprintf(stderr, "uip_init failed\n");
    return -1;
  }

  uip_initjoysticks();
  uip_textmode();

  return 0;
}

int ui_loop(void)
{
  unsigned int frame = 0;
  time_t start = time(nullptr);
  time_t last_report = start;
  unsigned int frames_since_report = 0;

  while (ui_running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        ui_running = 0;
      }
      if (event.type == SDL_EVENT_KEY_DOWN) {
        switch (event.key.key) {
        case SDLK_ESCAPE:
          ui_running = 0;
          break;
        case SDLK_F1:
          if (console_core)
            console_core->save_state_slot(0);
          break;
        case SDLK_F2:
          if (console_core)
            console_core->load_state_slot(0);
          break;
        case SDLK_F5:
          if (console_core)
            console_core->reset();
          break;
        case SDLK_F9:
          if (console_core)
            console_core->set_video_mode(ui_pal ^ 1, 0);
          ui_pal ^= 1;
          break;
        default:
          break;
        }
      }
    }

    update_input();

    if (console_core) {
      console_core->run_frame();
      ui_frame_count++;
    }

    uip_vsync();
    uip_checkkeyboard();

    frame++;
    frames_since_report++;

    time_t now = time(nullptr);
    if (now - last_report >= 5) {
      double fps = (double)frames_since_report / (double)(now - last_report);
      fprintf(stderr, "FPS: %.1f (%u frames total)\n", fps, frame);
      last_report = now;
      frames_since_report = 0;
    }
  }

  time_t end = time(nullptr);
  double elapsed = (double)(end - start);
  if (elapsed > 0) {
    fprintf(stderr, "\nCompleted %u frames in %.0f seconds\n", frame, elapsed);
    fprintf(stderr, "Average: %.1f frames/sec\n", frame / elapsed);
  }

  return 0;
}

void ui_final(void)
{
  if (console_core) {
    console_core->audio_stop();
    console_core.reset();
  }
  soundp_stop();
  uip_textmode();
  SDL_Quit();
}

[[noreturn]] void ui_err(const char *text, ...)
{
  va_list args;
  va_start(args, text);
  vfprintf(stderr, text, args);
  va_end(args);
  fputc('\n', stderr);
  exit(1);
}

/*** Public helpers for main() ***/

void ui_create_core(void)
{
  if (!video_backend) {
    video_backend = new ConsoleVideo();
  }
  /* Opened before the core so the first field it emits has somewhere to
   * go. With no device the platform layer drops samples and the emulator
   * runs silently rather than failing. */
  if (soundp_start() != 0) {
    fprintf(stderr, "Continuing without sound: no audio device\n");
  }
  console_core = std::make_unique<generator::EmulatorCore>(
      std::make_unique<generator::ui::SdlAudioBackend>(),
      std::unique_ptr<generator::IVideoBackend>(video_backend),
      std::make_shared<ConsoleLogger>());
}

void ui_destroy_core(void)
{
  if (console_core) {
    console_core->audio_stop();
    console_core.reset();
  }
}

void *ui_get_core_ptr(void)
{
  return console_core.get();
}

void ui_set_frameskip(int skip)
{
  ui_frameskip = skip > 0 ? skip : 1;
}
