#pragma once

#include <gtkmm.h>
#include <adwaita.h>

#include <memory>

#include "emulator_thread.hpp"
#include "preferences_dialog.hpp"

class MainWindow;  // Forward declaration

class GeneratorApp : public Gtk::Application {
public:
  static Glib::RefPtr<GeneratorApp> create();

  EmulatorThread &get_emulator_thread()
  {
    return m_emu_thread;
  }
  MainWindow *get_main_window()
  {
    return m_main_window;
  }

protected:
  GeneratorApp();
  ~GeneratorApp() override = default;

  // Overrides from Gio::Application / Gtk::Application
  void on_startup() override;
  void on_activate() override;
  void on_window_removed(Gtk::Window *window) override;
  void on_open(const Gio::Application::type_vec_files &files,
               const Glib::ustring &hint) override;

private:
  void on_action_open_rom();
  void on_action_pause();
  void on_action_preferences();
  void on_action_about();
  void on_action_quit();

  Glib::RefPtr<Gio::SimpleAction> m_pause_action;

  MainWindow *m_main_window{nullptr};
  std::unique_ptr<PreferencesDialog> m_prefs_dialog;
  EmulatorThread m_emu_thread;
};
