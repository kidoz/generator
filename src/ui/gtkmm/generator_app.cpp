#include "generator_app.hpp"
#include "main_window.hpp"

#include "ui_bridge.hpp"
#include <iostream>

#include "generator.h"

GeneratorApp::GeneratorApp()
    : Gtk::Application("org.generator.Emulator",
                       Gio::Application::Flags::HANDLES_OPEN |
                           Gio::Application::Flags::NON_UNIQUE)
{
}

Glib::RefPtr<GeneratorApp> GeneratorApp::create()
{
  return Glib::make_refptr_for_instance<GeneratorApp>(new GeneratorApp());
}

void GeneratorApp::on_open(const Gio::Application::type_vec_files &files,
                           const Glib::ustring & /*hint*/)
{
  // If we receive files via command line, activate the window first
  on_activate();

  if (!files.empty()) {
    std::string rom_path = files[0]->get_path();
    std::cout << "Loading ROM: " << rom_path << std::endl;

    // Stop the emulation thread to prevent race conditions during reset
    m_emu_thread.stop();
    // Let the core handle loading the ROM
    if (g_emulator_core) {
      auto res = g_emulator_core->load_rom(rom_path);
      if (!res) {
        std::cerr << "Failed to load ROM: " << res.error() << std::endl;
      }
    }
    m_emu_thread.start();
  }
}

void GeneratorApp::on_startup()
{
  // Call base class first
  Gtk::Application::on_startup();

  // Initialize libadwaita (must be called after Gtk::Application startup)
  adw_init();

  std::cout << "GeneratorApp: Started up with libadwaita." << std::endl;

  // Register simple actions
  add_action("open-rom",
             sigc::mem_fun(*this, &GeneratorApp::on_action_open_rom));
  add_action("preferences",
             sigc::mem_fun(*this, &GeneratorApp::on_action_preferences));
  add_action("about", sigc::mem_fun(*this, &GeneratorApp::on_action_about));
  add_action("quit", sigc::mem_fun(*this, &GeneratorApp::on_action_quit));

  // Stateful pause toggle — bound to the header-bar pause button and Space.
  m_pause_action = add_action_bool(
      "pause", sigc::mem_fun(*this, &GeneratorApp::on_action_pause), false);

  // Setup accels/shortcuts
  set_accel_for_action("app.open-rom", "<Ctrl>O");
  set_accel_for_action("app.pause", "space");
  set_accel_for_action("app.quit", "<Ctrl>Q");
  set_accel_for_action("app.preferences", "<Ctrl>comma");
}

void GeneratorApp::on_activate()
{
  // If window already exists, present it
  if (m_main_window) {
    m_main_window->present();
    return;
  }

  // Start the emulation thread
  m_emu_thread.start();
  m_emu_thread.set_emulation_running(true);  // Auto-start for now

  // Create the main window
  m_main_window = new MainWindow(m_emu_thread);
  add_window(*m_main_window);
  m_main_window->present();
}

void GeneratorApp::on_window_removed(Gtk::Window *window)
{
  if (window == m_main_window) {
    m_emu_thread.stop();
    m_main_window = nullptr;
  }
  Gtk::Application::on_window_removed(window);
}

void GeneratorApp::on_action_open_rom()
{
  if (!m_main_window)
    return;

  auto dialog = Gtk::FileDialog::create();
  dialog->set_title("Open ROM");

  auto filter_roms = Gtk::FileFilter::create();
  filter_roms->set_name("Mega Drive / Genesis ROMs");
  filter_roms->add_pattern("*.bin");
  filter_roms->add_pattern("*.smd");
  filter_roms->add_pattern("*.gen");
  filter_roms->add_pattern("*.md");
  filter_roms->add_pattern("*.rom");

  auto filter_all = Gtk::FileFilter::create();
  filter_all->set_name("All files");
  filter_all->add_pattern("*");

  auto filters = Gio::ListStore<Gtk::FileFilter>::create();
  filters->append(filter_roms);
  filters->append(filter_all);
  dialog->set_filters(filters);
  dialog->set_default_filter(filter_roms);

  dialog->open(
      *m_main_window,
      [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
          auto file = dialog->open_finish(result);
          if (!file)
            return;
          std::string path = file->get_path();
          std::cout << "Loading ROM: " << path << std::endl;
          m_emu_thread.stop();
          if (g_emulator_core) {
            auto res = g_emulator_core->load_rom(path);
            if (!res) {
              std::cerr << "Failed to load ROM: " << res.error() << std::endl;
              m_emu_thread.start();
              return;
            }
          }
          m_emu_thread.start();
          m_emu_thread.set_emulation_running(true);
          if (m_pause_action) {
            m_pause_action->change_state(Glib::Variant<bool>::create(false));
          }
        } catch (const Glib::Error & /*dismissed*/) {
          // User cancelled — ignore.
        }
      });
}

void GeneratorApp::on_action_pause()
{
  bool paused = false;
  m_pause_action->get_state(paused);
  paused = !paused;
  m_pause_action->set_state(Glib::Variant<bool>::create(paused));
  m_emu_thread.set_emulation_running(!paused);
}

void GeneratorApp::on_action_preferences()
{
  if (!m_prefs_dialog && m_main_window) {
    m_prefs_dialog = std::make_unique<PreferencesDialog>(*m_main_window);
  }
  if (m_prefs_dialog) {
    m_prefs_dialog->present();
  }
}

void GeneratorApp::on_action_about()
{
  std::cout << "Action: About requested" << std::endl;
  // TODO: Show about dialog
}

void GeneratorApp::on_action_quit()
{
  std::cout << "Action: Quit requested" << std::endl;
  quit();
}
