#include "emulator_view.hpp"
#include "emulator_thread.hpp"
#include "generator_app.hpp"
#include "main_window.hpp"
#include "screen_geometry.hpp"
#include "ui_bridge.hpp"

#include <gdkmm/memorytexture.h>
#include <glibmm/bytes.h>

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

  // Always request the next frame, but only while emulation is allowed to
  // run; while paused the nudges would just wake the thread for nothing.
  if (m_emu_thread.emulation_running()) {
    m_emu_thread.request_frame();
  }

  return true;  // Continue ticking
}

void EmulatorView::update_texture()
{
  // The bridge hands over a private snapshot of the most recently
  // completed field, already copy-safe against the emulation thread.
  int width = 0;
  int height = 0;
  auto bytes = ui_take_frame(&width, &height);
  if (!bytes || width <= 0 || height <= 0)
    return;

  auto texture = Gdk::MemoryTexture::create(
      width, height, Gdk::MemoryTexture::Format::B8G8R8X8, bytes, HMAXSIZE * 4);

  set_paintable(texture);
}
