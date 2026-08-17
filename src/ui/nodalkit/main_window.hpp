/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Main window for the NodalKit UI backend */

#pragma once

#include "emulator_view.hpp"
#include "input_controller.hpp"

#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/runtime/event_loop.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/status_bar.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace generator::nkui {

/* Window chrome: menu surface on top, emulator stage in the middle, status
 * bar along the bottom.
 *
 * The window also owns the UI-side heartbeat. It runs on the NodalKit event
 * loop and does two things: drain SDL gamepad events and adopt whatever
 * field the emulation thread has finished. Emulation speed is not tied to
 * this timer - the emulation thread paces itself. */
class MainWindow {
public:
  explicit MainWindow(nk::Application &app);
  ~MainWindow();

  MainWindow(const MainWindow &) = delete;
  MainWindow &operator=(const MainWindow &) = delete;

  void present();

  nk::Window &window()
  {
    return window_;
  }

  InputController &input()
  {
    return input_;
  }

  /* Commands raised by the menus and the keyboard land here. */
  void set_command_handler(std::function<void(std::string_view)> handler);

  void set_status(std::string text);
  void set_rom_title(std::string title);
  void set_paused(bool paused);
  void toggle_fullscreen();
  void set_smooth_scaling(bool smooth);
  bool smooth_scaling() const
  {
    return smooth_scaling_;
  }

private:
  void build_ui(nk::Application &app);
  void start_timers(nk::Application &app);
  void on_tick();
  void on_fps_tick();
  void refresh_state_segment();

  nk::Application &app_;

  nk::Window window_;
  std::shared_ptr<nk::Widget> root_;
  std::shared_ptr<EmulatorView> view_;
  std::shared_ptr<nk::StatusBar> status_;
  std::shared_ptr<nk::MenuBar> menu_bar_;

  InputController input_;
  std::function<void(std::string_view)> command_handler_;

  nk::CallbackHandle tick_{};
  nk::CallbackHandle fps_tick_{};

  bool was_focused_ = false;
  std::string status_text_ = "Ready";
  std::string rom_title_ = "No ROM";
  bool smooth_scaling_ = false;
};

}  // namespace generator::nkui
