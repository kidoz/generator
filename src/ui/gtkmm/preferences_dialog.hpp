/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <gtkmm.h>
#include <adwaita.h>

#include <functional>

#include "audio_preferences.hpp"

class PreferencesDialog {
public:
  PreferencesDialog(Gtk::Window &parent, std::function<void()> video_changed,
                    std::function<void(int)> resize_video,
                    std::function<void()> audio_changed = {});
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
  void on_audio_controls_changed();
  void refresh_audio_status();
  void reset_audio();
  void test_audio();
  void stop_audio_test();

  Gtk::Window &m_parent;
  std::function<void()> m_video_changed;
  std::function<void(int)> m_resize_video;
  std::function<void()> m_audio_changed;
  std::string m_startup_audio_driver;
  std::vector<generator::gtkui::AudioDriverOption> m_audio_drivers;
  AdwPreferencesDialog *m_dialog{nullptr};
  AdwComboRow *m_aspect_row{nullptr};
  AdwComboRow *m_scaling_row{nullptr};
  AdwComboRow *m_sampling_row{nullptr};
  AdwComboRow *m_scaler_row{nullptr};
  AdwComboRow *m_audio_driver_row{nullptr};
  GtkWidget *m_reset_button{nullptr};
  bool m_updating_video{false};
  AdwSwitchRow *m_audio_mute_row{nullptr};
  GtkWidget *m_volume_scale{nullptr};
  GtkLabel *m_volume_label{nullptr};
  GtkLabel *m_audio_restart_label{nullptr};
  GtkLabel *m_audio_feedback{nullptr};
  AdwActionRow *m_audio_device_row{nullptr};
  GtkWidget *m_audio_test_button{nullptr};
  GtkWidget *m_audio_reset_button{nullptr};
  sigc::connection m_audio_timer;
  sigc::connection m_audio_test_timer;
  bool m_updating_audio{false};
  bool m_testing_audio{false};
};
