#include "generator_app.hpp"
#include "main_window.hpp"

#include "ui_bridge.hpp"
#include <iostream>

#include "generator.h"
#include "gensoundp.h"

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

void GeneratorApp::load_rom_from_path(const std::string &path)
{
  // Stop the emulation thread to prevent race conditions during reset
  m_emu_thread.stop();
  if (g_emulator_core) {
    auto res = g_emulator_core->load_rom(path);
    if (!res) {
      show_error_dialog("Failed to load ROM", res.error());
    }
  }
  m_emu_thread.start();
  m_emu_thread.set_emulation_running(true);
  if (m_pause_action) {
    m_pause_action->change_state(Glib::Variant<bool>::create(false));
  }
  if (m_main_window) {
    m_main_window->set_runtime_state("Running", true);
  }
}

void GeneratorApp::show_error_dialog(const Glib::ustring &heading,
                                     const Glib::ustring &body)
{
  std::cerr << heading << ": " << body << std::endl;
  if (!m_main_window)
    return;

  auto *dialog =
      ADW_ALERT_DIALOG(adw_alert_dialog_new(heading.c_str(), body.c_str()));
  adw_alert_dialog_add_response(dialog, "ok", "_OK");
  adw_alert_dialog_set_default_response(dialog, "ok");
  adw_alert_dialog_set_close_response(dialog, "ok");
  /* AdwDialog destroys itself when closed; nothing to hold on to. */
  adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(m_main_window->gobj()));
}

void GeneratorApp::on_open(const Gio::Application::type_vec_files &files,
                           const Glib::ustring & /*hint*/)
{
  // If we receive files via command line, activate the window first
  on_activate();

  if (!files.empty()) {
    const std::string path = files[0]->get_path();
    if (path.empty()) {
      show_error_dialog("Cannot open ROM", "Only local files can be loaded.");
      return;
    }
    std::cout << "Loading ROM: " << path << std::endl;
    load_rom_from_path(path);
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
  m_main_window->set_runtime_state("Running", true);
  m_main_window->set_audio_backend(soundp_backend_name());
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

  dialog->open(*m_main_window,
               [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
                 try {
                   auto file = dialog->open_finish(result);
                   if (!file)
                     return;
                   const std::string path = file->get_path();
                   if (path.empty()) {
                     show_error_dialog("Cannot open ROM",
                                       "Only local files can be loaded.");
                     return;
                   }
                   std::cout << "Loading ROM: " << path << std::endl;
                   load_rom_from_path(path);
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
  if (m_main_window) {
    m_main_window->set_runtime_state(paused ? "Paused" : "Running", !paused);
  }
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
  if (!m_main_window)
    return;

  AdwDialog *about = adw_about_dialog_new();
  AdwAboutDialog *a = ADW_ABOUT_DIALOG(about);
  adw_about_dialog_set_application_name(a, "Generator");
  adw_about_dialog_set_application_icon(a, "org.generator.Emulator");
  adw_about_dialog_set_version(a, VERSION);
  adw_about_dialog_set_comments(a, "Sega Mega Drive / Genesis emulator");
  adw_about_dialog_set_license_type(a, GTK_LICENSE_GPL_2_0_ONLY);
  adw_dialog_present(about, GTK_WIDGET(m_main_window->gobj()));
}

void GeneratorApp::on_action_quit()
{
  std::cout << "Action: Quit requested" << std::endl;
  quit();
}
