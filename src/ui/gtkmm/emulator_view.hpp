/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "video_presentation.hpp"

#include <atomic>

#include <glibmm/main.h>
#include <glibmm/bytes.h>
#include <gdkmm/texture.h>
#include <gtkmm/widget.h>

class EmulatorThread;

class EmulatorView : public Gtk::Widget {
public:
  explicit EmulatorView(EmulatorThread &emu_thread);
  ~EmulatorView() override;

  void set_video_settings(const generator::gtkui::VideoSettings &settings);
  int source_width() const
  {
    return m_source_width;
  }
  int source_height() const
  {
    return m_source_height;
  }

  // Number of emulated fields uploaded since the last FPS sample.
  unsigned int sample_frames()
  {
    return m_frames_since_sample.exchange(0);
  }

protected:
  void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot> &snapshot) override;
  void measure_vfunc(Gtk::Orientation orientation, int for_size, int &minimum,
                     int &natural, int &minimum_baseline,
                     int &natural_baseline) const override;

private:
  bool on_poll();
  void update_texture();
  void rebuild_texture();

  EmulatorThread &m_emu_thread;
  sigc::connection m_poll;
  std::atomic<unsigned int> m_frames_since_sample{0};
  generator::gtkui::VideoSettings m_video_settings;
  generator::gtkui::VideoFrameScaler m_scaler;
  Glib::RefPtr<Glib::Bytes> m_source;
  Glib::RefPtr<Gdk::Texture> m_texture;
  int m_source_width = 320;
  int m_source_height = 224;
};
