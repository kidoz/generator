#pragma once

#include <gtkmm.h>
#include <adwaita.h>

class PreferencesDialog {
public:
  PreferencesDialog(Gtk::Window &parent);
  ~PreferencesDialog() = default;

  void present();

private:
  void setup_ui();
  void populate_video_page(AdwPreferencesPage *page);
  void populate_audio_page(AdwPreferencesPage *page);

  void on_scaler_changed();
  void on_audio_driver_changed();

  Gtk::Window &m_parent;
  AdwPreferencesDialog *m_dialog{nullptr};

  AdwComboRow *m_scaler_row{nullptr};
  AdwComboRow *m_audio_driver_row{nullptr};

  // Model for combo boxes
  Glib::RefPtr<Gtk::StringList> m_scaler_model;
  Glib::RefPtr<Gtk::StringList> m_audio_model;
};
