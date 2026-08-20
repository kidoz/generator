/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Application controller for the NodalKit UI backend */

#include "generator_app.hpp"
#include "commands.hpp"
#include "rom_files.hpp"
#include "ui_bridge.hpp"

#include <nk/platform/file_dialog.h>

#include "emulator_core.hpp"
#include "log.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <string>

namespace generator::nkui {

namespace {

/* Settings keys. The store lives at nk::Settings::default_path("Generator")
 * and every value is applied live, so there is nothing to flush besides the
 * final save in the destructor and the cheap saves on change. */
constexpr const char *kKeySmoothScaling = "video.smooth-scaling";
constexpr const char *kKeyVideoMode = "video.mode"; /* 0 auto, 1 ntsc, 2 pal */
constexpr const char *kKeyColorScheme = "ui.color-scheme"; /* 0 sys, 1 l, 2 d */
constexpr const char *kKeyWindowWidth = "window.width";
constexpr const char *kKeyWindowHeight = "window.height";
constexpr const char *kKeyStateSlot = "state.slot";

constexpr int kDefaultWidth = 960;
constexpr int kDefaultHeight = 800;

std::string title_for_rom(const std::string &path)
{
  if (g_emulator_core) {
    const char *name = g_emulator_core->rom_name();
    if (name && name[0] != '\0')
      return name;
  }
  return std::filesystem::path(path).filename().string();
}

int parse_index(std::string_view text, int fallback)
{
  int value = fallback;
  const auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || ptr != text.data() + text.size())
    return fallback;
  return value;
}

}  // namespace

GeneratorApp::GeneratorApp()
    : app_({
          .app_id = "org.generator.Emulator",
          .app_name = "Generator",
      }),
      settings_(nk::Settings::default_path("Generator")),
      startup_rom_(g_startup_rom)
{
  (void)settings_.load();
  state_slot_ =
      std::clamp(static_cast<int>(settings_.get_int(kKeyStateSlot, 0)), 0, 9);
}

GeneratorApp::~GeneratorApp()
{
  if (!settings_.save())
    LOG_VERBOSE("Could not persist settings to %s",
                settings_.path().string().c_str());

  /* The window cancels its timers before the event loop goes away, and the
   * emulation thread must be joined before the core is destroyed. */
  window_.reset();
  emulator_.stop();
}

int GeneratorApp::run()
{
  apply_color_scheme(color_scheme_choice());

  const int width = std::clamp(
      static_cast<int>(settings_.get_int(kKeyWindowWidth, kDefaultWidth)), 480,
      8192);
  const int height = std::clamp(
      static_cast<int>(settings_.get_int(kKeyWindowHeight, kDefaultHeight)),
      360, 8192);

  window_ = std::make_unique<MainWindow>(app_, width, height);
  window_->set_command_handler(
      [this](std::string_view action) { handle_command(action); });
  window_->set_rom_drop_handler([this](std::string path) { load_rom(path); });
  window_->set_smooth_scaling(settings_.get_bool(kKeySmoothScaling, false));
  refresh_menus();

  /* Remember the windowed size only; a fullscreen allocation would restore
   * as a giant window. */
  (void)window_->window().on_resize().connect([this](int w, int h) {
    if (!window_ || window_->window().is_fullscreen())
      return;
    settings_.set_int(kKeyWindowWidth, w);
    settings_.set_int(kKeyWindowHeight, h);
  });

  window_->present();

  /* Before the emulation thread exists, so no pause dance is needed. */
  apply_startup_video_mode();

  emulator_.start();

  if (!startup_rom_.empty()) {
    load_rom(startup_rom_);
  } else {
    /* The splash cart loaded in ui_loop() runs until a ROM arrives. */
    set_paused(false);
  }

  return app_.run();
}

void GeneratorApp::handle_command(std::string_view action)
{
  if (action == commands::kOpenRom) {
    open_rom_dialog();
  } else if (action == commands::kClearRecent) {
    clear_recent();
  } else if (action == commands::kQuit) {
    app_.quit(0);
  } else if (action == commands::kPause) {
    toggle_pause();
  } else if (action == commands::kReset) {
    reset_machine(false);
  } else if (action == commands::kSoftReset) {
    reset_machine(true);
  } else if (action == commands::kSaveState) {
    save_state();
  } else if (action == commands::kLoadState) {
    load_state();
  } else if (action == commands::kFullscreen) {
    window_->toggle_fullscreen();
  } else if (action == commands::kScaleNearest) {
    set_smooth_scaling(false);
  } else if (action == commands::kScaleSmooth) {
    set_smooth_scaling(true);
  } else if (action == commands::kPreferences) {
    show_preferences();
  } else if (action == commands::kAbout) {
    show_about();
  } else if (action.starts_with(commands::kOpenRecentPrefix)) {
    open_recent(
        action.substr(std::string_view(commands::kOpenRecentPrefix).size()));
  } else if (action.starts_with(commands::kStateSlotPrefix)) {
    set_state_slot(parse_index(
        action.substr(std::string_view(commands::kStateSlotPrefix).size()),
        state_slot_));
  }
}

void GeneratorApp::open_rom_dialog()
{
  if (!app_.supports_open_file_dialog()) {
    window_->set_status("Open ROM unavailable on this platform");
    return;
  }

  app_.open_file_dialog_async(
      "Open ROM", rom_filters(), [this](nk::OpenFileDialogResult result) {
        if (!result.has_value()) {
          if (result.error() != nk::FileDialogError::Cancelled)
            window_->set_status("Could not open the file chooser");
          return;
        }
        load_rom(result.value());
      });
}

void GeneratorApp::load_rom(const std::string &path)
{
  if (!g_emulator_core)
    return;

  /* Loading rewrites memory the emulation thread is reading, so the thread
   * has to be parked first. */
  emulator_.stop();

  const auto loaded = g_emulator_core->load_rom(path);
  if (!loaded) {
    LOG_CRITICAL("Failed to load ROM %s: %s", path.c_str(),
                 loaded.error().c_str());
    window_->set_status("Load failed: " + loaded.error());
    window_->show_toast("Could not load " +
                        std::filesystem::path(path).filename().string());
    emulator_.start();
    return;
  }

  const std::string title = title_for_rom(path);
  window_->set_rom_title(title);
  window_->input().release_all();

  settings_.push_recent_file(path);
  (void)settings_.save();
  refresh_menus();
  window_->show_toast("Loaded " + title);

  emulator_.start();
  set_paused(false);
}

void GeneratorApp::open_recent(std::string_view index_text)
{
  const auto recent = settings_.recent_files();
  const int index = parse_index(index_text, -1);
  if (index < 0 || index >= static_cast<int>(recent.size()))
    return;
  load_rom(recent[static_cast<std::size_t>(index)]);
}

void GeneratorApp::clear_recent()
{
  settings_.remove("recent_files");
  (void)settings_.save();
  refresh_menus();
}

void GeneratorApp::set_paused(bool paused)
{
  paused_ = paused;
  emulator_.set_emulation_running(!paused);
  if (paused)
    window_->input().release_all();
  window_->set_paused(paused);
  refresh_menus();
}

void GeneratorApp::toggle_pause()
{
  set_paused(!paused_);
}

void GeneratorApp::reset_machine(bool soft)
{
  if (!g_emulator_core)
    return;

  emulator_.stop();
  if (soft)
    g_emulator_core->soft_reset();
  else
    g_emulator_core->reset();
  window_->input().release_all();
  emulator_.start();
  set_paused(false);
}

void GeneratorApp::save_state()
{
  if (!g_emulator_core)
    return;

  /* The state writer walks machine state the emulation thread mutates, so
   * park the thread for the (short) duration. */
  emulator_.stop();
  const int result = g_emulator_core->save_state_slot(state_slot_);
  emulator_.start();

  if (result == 0) {
    window_->show_toast("State saved to slot " + std::to_string(state_slot_));
    refresh_menus(); /* the slot is now marked as saved */
  } else {
    window_->show_toast("Save state failed");
  }
}

void GeneratorApp::load_state()
{
  if (!g_emulator_core)
    return;

  emulator_.stop();
  const int result = g_emulator_core->load_state_slot(state_slot_);
  window_->input().release_all();
  emulator_.start();

  window_->show_toast(
      result == 0 ? "State loaded from slot " + std::to_string(state_slot_)
                  : "No saved state in slot " + std::to_string(state_slot_));
}

void GeneratorApp::set_state_slot(int slot)
{
  if (slot < 0 || slot > 9 || slot == state_slot_)
    return;

  state_slot_ = slot;
  settings_.set_int(kKeyStateSlot, slot);
  (void)settings_.save();
  refresh_menus();
  window_->show_toast("State slot " + std::to_string(slot) + " selected");
}

void GeneratorApp::set_smooth_scaling(bool smooth)
{
  window_->set_smooth_scaling(smooth);
  settings_.set_bool(kKeySmoothScaling, smooth);
  (void)settings_.save();
}

int GeneratorApp::video_mode_choice() const
{
  return std::clamp(static_cast<int>(settings_.get_int(kKeyVideoMode, 0)), 0,
                    2);
}

void GeneratorApp::set_video_mode_choice(int choice)
{
  choice = std::clamp(choice, 0, 2);
  settings_.set_int(kKeyVideoMode, choice);
  (void)settings_.save();

  if (!g_emulator_core)
    return;

  /* Switching NTSC/PAL reprograms the VDP timing tables the emulation
   * thread reads every field. */
  emulator_.stop();
  if (choice == 0)
    g_emulator_core->set_video_mode(0, 1);
  else
    g_emulator_core->set_video_mode(choice == 2 ? 1 : 0, 0);
  emulator_.start();

  if (choice == 0)
    window_->show_toast("Autodetect applies when a ROM is loaded");
}

int GeneratorApp::color_scheme_choice() const
{
  return std::clamp(static_cast<int>(settings_.get_int(kKeyColorScheme, 0)), 0,
                    2);
}

void GeneratorApp::set_color_scheme_choice(int choice)
{
  choice = std::clamp(choice, 0, 2);
  settings_.set_int(kKeyColorScheme, choice);
  (void)settings_.save();
  apply_color_scheme(choice);
}

void GeneratorApp::apply_color_scheme(int choice)
{
  auto selection = app_.theme_selection();
  if (choice == 1)
    selection.color_scheme_override = nk::ColorScheme::Light;
  else if (choice == 2)
    selection.color_scheme_override = nk::ColorScheme::Dark;
  else
    selection.color_scheme_override.reset();
  app_.set_theme_selection(selection);
}

void GeneratorApp::apply_startup_video_mode()
{
  if (!g_emulator_core)
    return;

  const int choice = video_mode_choice();
  if (choice == 0)
    g_emulator_core->set_video_mode(0, 1);
  else
    g_emulator_core->set_video_mode(choice == 2 ? 1 : 0, 0);
}

MenuState GeneratorApp::make_menu_state() const
{
  MenuState state;
  state.recent_roms = settings_.recent_files();
  state.state_slot = state_slot_;
  state.paused = paused_;
  if (g_emulator_core) {
    for (std::size_t slot = 0; slot < state.slot_saved.size(); slot++) {
      state.slot_saved[slot] =
          g_emulator_core->state_slot_date(static_cast<int>(slot)) != 0;
    }
  }
  return state;
}

void GeneratorApp::refresh_menus()
{
  window_->set_menu_state(make_menu_state());
}

void GeneratorApp::show_preferences()
{
  if (!preferences_) {
    preferences_ =
        std::make_unique<PreferencesDialog>(PreferencesDialog::Options{
            .smooth_scaling = [this] { return window_->smooth_scaling(); },
            .set_smooth_scaling =
                [this](bool smooth) { set_smooth_scaling(smooth); },
            .video_mode = [this] { return video_mode_choice(); },
            .set_video_mode =
                [this](int choice) { set_video_mode_choice(choice); },
            .color_scheme = [this] { return color_scheme_choice(); },
            .set_color_scheme =
                [this](int choice) { set_color_scheme_choice(choice); },
        });
  }
  preferences_->present(window_->window());
}

void GeneratorApp::show_about()
{
  if (!about_) {
    about_ = nk::AboutDialog::create({
        .application_name = "Generator",
        .version = VERSION,
        .comments = "Sega Genesis / Mega Drive emulator",
        .developer_name = "James Ponder and contributors",
        .website = "https://github.com/kidoz/generator",
        .copyright = "Copyright (C) James Ponder and contributors",
        .license = "GPL-2.0-or-later",
    });
  }
  about_->present(window_->window());
}

}  // namespace generator::nkui
