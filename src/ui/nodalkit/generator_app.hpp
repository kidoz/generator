/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Application controller for the NodalKit UI backend */

#pragma once

#include "emulator_thread.hpp"
#include "main_window.hpp"
#include "preferences_dialog.hpp"

#include <nk/model/settings.h>
#include <nk/platform/application.h>
#include <nk/widgets/about_dialog.h>

#include <memory>
#include <string>
#include <string_view>

namespace generator::nkui {

/* Owns the application lifetime and routes every command surface through a
 * single handler. The widget tree renders state and forwards intents; it
 * never touches the emulator core directly. */
class GeneratorApp {
public:
  GeneratorApp();
  ~GeneratorApp();

  GeneratorApp(const GeneratorApp &) = delete;
  GeneratorApp &operator=(const GeneratorApp &) = delete;

  int run();

private:
  void handle_command(std::string_view action);

  void open_rom_dialog();
  void load_rom(const std::string &path);
  void open_recent(std::string_view index_text);
  void clear_recent();
  void set_paused(bool paused);
  void toggle_pause();
  void reset_machine(bool soft);
  void save_state();
  void load_state();
  void set_state_slot(int slot);
  void show_preferences();
  void show_about();

  void set_smooth_scaling(bool smooth);
  int video_mode_choice() const;
  void set_video_mode_choice(int choice);
  int color_scheme_choice() const;
  void set_color_scheme_choice(int choice);
  void apply_color_scheme(int choice);
  void apply_startup_video_mode();

  MenuState make_menu_state() const;
  void refresh_menus();

  nk::Application app_;
  nk::Settings settings_;
  EmulatorThread emulator_;
  std::unique_ptr<MainWindow> window_;
  std::unique_ptr<PreferencesDialog> preferences_;
  std::shared_ptr<nk::AboutDialog> about_;
  std::string startup_rom_;
  int state_slot_ = 0;
  bool paused_ = false;
};

}  // namespace generator::nkui
