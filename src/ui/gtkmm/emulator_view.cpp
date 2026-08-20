#include "emulator_view.hpp"
#include "emulator_thread.hpp"
#include "generator_app.hpp"
#include "main_window.hpp"
#include "screen_geometry.hpp"
#include "ui_bridge.hpp"


#include <gdkmm/memorytexture.h>
#include <glibmm/bytes.h>

// Forward declarations for UI pixel buffers managed in ui_bridge.cpp
extern uint8_t *g_screen0;
extern uint8_t *g_screen1;
extern std::atomic<int> g_whichbank;

EmulatorView::EmulatorView(EmulatorThread &emu_thread)
    : m_emu_thread(emu_thread)
{
  set_vexpand(true);
  set_hexpand(true);
  set_can_shrink(true);

  // Stretch the frame to fill the whole widget instead of preserving the
  // 320x224 aspect ratio (the default CONTAIN, which pillarboxes the image
  // with empty bars on the left and right when the window is wider).
  set_content_fit(Gtk::ContentFit::FILL);

  add_tick_callback(sigc::mem_fun(*this, &EmulatorView::on_tick));
}

EmulatorView::~EmulatorView() = default;

bool EmulatorView::on_tick(
    const Glib::RefPtr<Gdk::FrameClock> & /* frame_clock */)
{
  extern Glib::RefPtr<GeneratorApp> g_app;
  if (g_app && g_app->get_main_window()) {
    g_app->get_main_window()->get_input_controller().poll_sdl_events();
  }

  // If a frame finished rendering, swap buffers and update texture
  if (m_emu_thread.render_complete.exchange(0) == 1) {
    update_texture();
    m_frames_since_sample.fetch_add(1, std::memory_order_relaxed);
  }

  // Always request the next frame at the GTK vsync tick rate
  m_emu_thread.request_frame();

  return true;  // Continue ticking
}

void EmulatorView::update_texture()
{
  if (!g_screen0 || !g_screen1 || !g_emulator_core)
    return;

  // The core pushes each field flush to the top-left of the buffer, so the
  // displayed region starts at the origin and is exactly as large as the
  // VDP's current mode.
  int core_width = 0;
  int core_height = 0;
  g_emulator_core->screen_size(&core_width, &core_height);
  if (core_width <= 0 || core_height <= 0)
    return;

  // Default no-scale; xBRZ/scale integrations to come later
  int scale = 1;
  unsigned int display_width = static_cast<unsigned int>(core_width) * scale;
  unsigned int display_height = static_cast<unsigned int>(core_height) * scale;

  int current_bank = g_whichbank.load();
  uint8_t *screen_data = (current_bank == 0) ? g_screen0 : g_screen1;

  uint8_t *display_start = screen_data;

  auto bytes =
      Glib::Bytes::create(display_start, display_height * HMAXSIZE * 4);

  auto texture = Gdk::MemoryTexture::create(
      display_width, display_height, Gdk::MemoryTexture::Format::B8G8R8X8,
      bytes, HMAXSIZE * 4);

  set_paintable(texture);
}
