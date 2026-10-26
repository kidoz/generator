/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <gtkmm.h>
#include <adwaita.h>

#include <functional>

class PreferencesDialog {
public:
  PreferencesDialog(Gtk::Window &parent, std::function<void()> video_changed,
                    std::function<void(int)> resize_video);
  ~PreferencesDialog();

  void present();

private:
  void setup_ui();
  void populate_video_page(AdwPreferencesPage *page);
  void populate_audio_page(AdwPreferencesPage *page);
  void on_video_changed();
  void update_video_descriptions();
  void reset_video();
  void on_audio_driver_changed();

  Gtk::Window &m_parent;
  std::function<void()> m_video_changed;
  std::function<void(int)> m_resize_video;
  AdwPreferencesDialog *m_dialog{nullptr};
  AdwComboRow *m_aspect_row{nullptr};
  AdwComboRow *m_scaling_row{nullptr};
  AdwComboRow *m_sampling_row{nullptr};
  AdwComboRow *m_scaler_row{nullptr};
  AdwComboRow *m_audio_driver_row{nullptr};
  GtkWidget *m_reset_button{nullptr};
  bool m_updating_video{false};
};
