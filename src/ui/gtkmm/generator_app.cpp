#include "generator_app.hpp"
#include "main_window.hpp"

#include "input_controller.hpp"
#include "ui_bridge.hpp"
#include <algorithm>
#include <iostream>

#include "generator.h"
#include "gensoundp.h"

#if defined(NETPLAY)
#include "netplay.h"
#endif

GeneratorApp::GeneratorApp()
    : Gtk::Application("org.generator.Emulator",
                       Gio::Application::Flags::HANDLES_OPEN |
                           Gio::Application::Flags::NON_UNIQUE)
{
}

GeneratorApp::~GeneratorApp()
{
  m_prefs_dialog.reset();
  // Connection work uses the emulator thread; join it before that member dies.
  m_netplay_dialog.reset();
  m_emu_thread.stop();
}

Glib::RefPtr<GeneratorApp> GeneratorApp::create()
{
  return Glib::make_refptr_for_instance<GeneratorApp>(new GeneratorApp());
}

void GeneratorApp::load_rom_from_path(const std::string &path)
{
  if (m_netplay_dialog &&
      (m_netplay_dialog->busy() || m_netplay_dialog->in_room())) {
    show_error_dialog("Cannot replace ROM",
                      "Finish the netplay operation and leave the room before "
                      "loading another ROM.");
    return;
  }
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
  add_action("netplay", sigc::mem_fun(*this, &GeneratorApp::on_action_netplay));
  add_action("about", sigc::mem_fun(*this, &GeneratorApp::on_action_about));
  add_action("quit", sigc::mem_fun(*this, &GeneratorApp::on_action_quit));
  add_action("save-state",
             sigc::mem_fun(*this, &GeneratorApp::on_action_save_state));
  add_action("load-state",
             sigc::mem_fun(*this, &GeneratorApp::on_action_load_state));
  m_state_slot_action = add_action_radio_integer(
      "state-slot", sigc::mem_fun(*this, &GeneratorApp::on_action_state_slot),
      m_state_slot);

  // Stateful pause toggle — bound to the header-bar pause button and Space.
  m_pause_action = add_action_bool(
      "pause", sigc::mem_fun(*this, &GeneratorApp::on_action_pause), false);

  // Setup accels/shortcuts
  set_accel_for_action("app.open-rom", "<Ctrl>O");
  set_accel_for_action("app.pause", "space");
  set_accel_for_action("app.quit", "<Ctrl>Q");
  set_accel_for_action("app.preferences", "<Ctrl>comma");
  set_accel_for_action("app.save-state", "<Ctrl>S");
  set_accel_for_action("app.load-state", "<Ctrl>L");
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
    m_prefs_dialog.reset();
    // Cancel and join connection work before tearing down its session.
    m_netplay_dialog.reset();
    m_emu_thread.stop();
#if defined(NETPLAY)
    netplay_disconnect();
    netplay_shutdown();
#endif
    input_set_netplay_drive(false);
    m_main_window = nullptr;
  }
  Gtk::Application::on_window_removed(window);
}

void GeneratorApp::on_action_open_rom()
{
  if (!m_main_window)
    return;

  open_rom_dialog(*m_main_window);
}

void GeneratorApp::open_rom_dialog(Gtk::Window &parent)
{
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

  dialog->open(parent, [this,
                        dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
    try {
      auto file = dialog->open_finish(result);
      if (!file)
        return;
      const std::string path = file->get_path();
      if (path.empty()) {
        show_error_dialog("Cannot open ROM", "Only local files can be loaded.");
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

void GeneratorApp::on_action_save_state()
{
  save_or_load_state(true);
}

void GeneratorApp::on_action_load_state()
{
  save_or_load_state(false);
}

void GeneratorApp::on_action_state_slot(int slot)
{
  m_state_slot = std::clamp(slot, 0, 9);
  if (m_state_slot_action) {
    m_state_slot_action->set_state(Glib::Variant<int>::create(m_state_slot));
  }
}

void GeneratorApp::save_or_load_state(bool save)
{
  if (!g_emulator_core) {
    show_error_dialog(save ? "Save State Failed" : "Load State Failed",
                      "No ROM is loaded.");
    return;
  }

  std::expected<void, generator::StateError> result;
  m_emu_thread.run_exclusive([&] {
    result = save ? g_emulator_core->save_state_slot(m_state_slot)
                  : g_emulator_core->load_state_slot(m_state_slot);
  });

  if (!result) {
    show_error_dialog(
        save ? "Save State Failed" : "Load State Failed",
        std::string(generator::state_error_message(result.error())));
    return;
  }

  if (!save) {
    /* Drop pre-load audio and input state so the restored timeline is not
     * driven by the old one. */
    soundp_flush();
    if (m_main_window) {
      m_main_window->get_input_controller().release_all();
    }
  }

  std::cout << (save ? "State saved to slot " : "State loaded from slot ")
            << m_state_slot << std::endl;
}

void GeneratorApp::on_action_netplay()
{
  if (!m_main_window)
    return;
  if (!m_netplay_dialog) {
    m_netplay_dialog =
        std::make_unique<NetplayDialog>(*m_main_window, m_emu_thread, [this] {
          open_rom_dialog(*m_netplay_dialog);
        });
    m_netplay_dialog->signal_session_changed.connect(
        [this](const Glib::ustring &status, bool busy, bool in_room) {
          if (!m_main_window)
            return;
          m_main_window->set_netplay_status(status);
          for (const auto *name :
               {"open-rom", "save-state", "load-state", "pause"}) {
            auto action = std::dynamic_pointer_cast<Gio::SimpleAction>(
                lookup_action(name));
            if (action)
              action->set_enabled(!busy && !in_room);
          }
          if (in_room) {
            m_emu_thread.set_emulation_running(true);
            m_pause_action->set_state(Glib::Variant<bool>::create(false));
            m_main_window->set_runtime_state("Running", true);
          }
        });
  }
  m_netplay_dialog->present();
}

void GeneratorApp::on_action_preferences()
{
  if (!m_prefs_dialog && m_main_window) {
    m_prefs_dialog = std::make_unique<PreferencesDialog>(
        *m_main_window, [this] { m_main_window->apply_video_settings(); },
        [this](int factor) { m_main_window->resize_video(factor); });
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
