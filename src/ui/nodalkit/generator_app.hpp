/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Application controller for the NodalKit UI backend */

#pragma once

#include "emulator_thread.hpp"
#include "main_window.hpp"

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
  void set_paused(bool paused);
  void toggle_pause();
  void reset_machine(bool soft);
  void save_state();
  void load_state();
  void show_about();

  nk::Application app_;
  EmulatorThread emulator_;
  std::unique_ptr<MainWindow> window_;
  std::shared_ptr<nk::AboutDialog> about_;
  std::string startup_rom_;
  bool paused_ = false;
};

}  // namespace generator::nkui
