/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "emulator_view.hpp"
#include "emulator_thread.hpp"
#include "generator_app.hpp"
#include "main_window.hpp"
#include "screen_geometry.hpp"
#include "ui_bridge.hpp"

#include "generator.h"

#include <gdkmm/memorytexture.h>
#include <glibmm/bytes.h>
#include <gtkmm/snapshot.h>

EmulatorView::EmulatorView(EmulatorThread &emu_thread)
    : Glib::ObjectBase("GeneratorEmulatorView"), m_emu_thread(emu_thread),
      m_video_settings(generator::gtkui::VideoSettings::load())
{
  set_vexpand(true);
  set_hexpand(true);
  /* Not a frame-clock tick. GTK 4.22 records one presentation per clock
   * frame; swapping the paintable from inside the tick presented that
   * frame twice, one refresh (about 16.7ms) apart, and the late feedback
   * then landed on a frame the clock had already discarded. A plain
   * timeout uploads the texture between frames, so queue_draw schedules
   * a single paint. */
  m_poll = Glib::signal_timeout().connect(
      sigc::mem_fun(*this, &EmulatorView::on_poll), 8);
}

EmulatorView::~EmulatorView()
{
  m_poll.disconnect();
}

bool EmulatorView::on_poll()
{
  extern Glib::RefPtr<GeneratorApp> g_app;

  if (gen_quit) {
    if (g_app) {
      if (auto *window = g_app->get_main_window())
        window->close();
      g_app->quit();
    }
    return false;
  }

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

  return true;
}

void EmulatorView::set_video_settings(
    const generator::gtkui::VideoSettings &settings)
{
  if (settings == m_video_settings)
    return;
  bool filter_changed = settings.scaler != m_video_settings.scaler;
  m_video_settings = settings;
  // Retain the last source field so changing filters also works while paused.
  if (filter_changed)
    rebuild_texture();
  queue_draw();
}

void EmulatorView::update_texture()
{
  int width = 0;
  int height = 0;
  auto bytes = ui_take_frame(&width, &height);
  if (!bytes || width <= 0 || height <= 0)
    return;
  m_source = std::move(bytes);
  m_source_width = width;
  m_source_height = height;
  rebuild_texture();
}

void EmulatorView::rebuild_texture()
{
  if (!m_source)
    return;
  gsize size = 0;
  auto *data = static_cast<const uint8_t *>(m_source->get_data(size));
  auto frame = m_scaler.apply({data, size}, m_source_width, m_source_height,
                              HMAXSIZE * 4, m_video_settings.scaler);
  if (frame.pixels.empty())
    return;
  // Bytes owns a copy; the scaler can reuse its buffers for the next field.
  auto bytes =
      Glib::Bytes::create(frame.pixels.data(), frame.pixels.size_bytes());
  m_texture = Gdk::MemoryTexture::create(frame.width, frame.height,
                                         Gdk::MemoryTexture::Format::B8G8R8X8,
                                         bytes, frame.width * 4);
  queue_draw();
}

void EmulatorView::snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot> &snapshot)
{
  if (!m_texture)
    return;
  auto rect = m_video_settings.bounds(get_width(), get_height(), m_source_width,
                                      m_source_height);
  if (rect.width <= 0 || rect.height <= 0)
    return;
  graphene_rect_t bounds =
      GRAPHENE_RECT_INIT(rect.x, rect.y, rect.width, rect.height);
  // The C API is available since GTK 4.10; gtkmm's wrapper requires 4.22.
  gtk_snapshot_append_scaled_texture(snapshot->gobj(), m_texture->gobj(),
                                     m_video_settings.sampling ==
                                             generator::gtkui::Sampling::NEAREST
                                         ? GSK_SCALING_FILTER_NEAREST
                                         : GSK_SCALING_FILTER_LINEAR,
                                     &bounds);
}

void EmulatorView::measure_vfunc(Gtk::Orientation orientation, int,
                                 int &minimum, int &natural,
                                 int &minimum_baseline,
                                 int &natural_baseline) const
{
  minimum = 1;
  natural = orientation == Gtk::Orientation::HORIZONTAL ? 320 : 240;
  minimum_baseline = natural_baseline = -1;
}
