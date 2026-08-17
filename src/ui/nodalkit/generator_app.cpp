/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Application controller for the NodalKit UI backend */

#include "generator_app.hpp"
#include "commands.hpp"
#include "ui_bridge.hpp"

#include <nk/platform/file_dialog.h>

#include "emulator_core.hpp"
#include "log.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace generator::nkui {

namespace {

/* gen_loadimage() reads raw images and Richard Bannister's interleaved SMD
 * format; there is no archive support, so no *.zip here. */
const std::vector<std::string> kRomFilters = {"*.bin", "*.smd", "*.gen", "*.md",
                                              "*.rom"};

std::string title_for_rom(const std::string &path)
{
  if (g_emulator_core) {
    const char *name = g_emulator_core->rom_name();
    if (name && name[0] != '\0')
      return name;
  }
  return std::filesystem::path(path).filename().string();
}

}  // namespace

GeneratorApp::GeneratorApp()
    : app_({
          .app_id = "org.generator.Emulator",
          .app_name = "Generator",
      }),
      startup_rom_(g_startup_rom)
{
}

GeneratorApp::~GeneratorApp()
{
  /* The window cancels its timers before the event loop goes away, and the
   * emulation thread must be joined before the core is destroyed. */
  window_.reset();
  emulator_.stop();
}

int GeneratorApp::run()
{
  window_ = std::make_unique<MainWindow>(app_);
  window_->set_command_handler(
      [this](std::string_view action) { handle_command(action); });
  window_->present();

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
    window_->set_smooth_scaling(false);
  } else if (action == commands::kScaleSmooth) {
    window_->set_smooth_scaling(true);
  } else if (action == commands::kAbout) {
    show_about();
  }
}

void GeneratorApp::open_rom_dialog()
{
  if (!app_.supports_open_file_dialog()) {
    window_->set_status("Open ROM unavailable on this platform");
    return;
  }

  app_.open_file_dialog_async(
      "Open ROM", kRomFilters, [this](nk::OpenFileDialogResult result) {
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
    emulator_.start();
    return;
  }

  window_->set_rom_title(title_for_rom(path));
  window_->input().release_all();

  emulator_.start();
  set_paused(false);
}

void GeneratorApp::set_paused(bool paused)
{
  paused_ = paused;
  emulator_.set_emulation_running(!paused);
  if (paused)
    window_->input().release_all();
  window_->set_paused(paused);
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

  const int result = g_emulator_core->save_state_slot(0);
  window_->set_status(result == 0 ? "State saved to slot 0"
                                  : "Save state failed");
}

void GeneratorApp::load_state()
{
  if (!g_emulator_core)
    return;

  const int result = g_emulator_core->load_state_slot(0);
  window_->set_status(result == 0 ? "State loaded from slot 0"
                                  : "Load state failed");
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
