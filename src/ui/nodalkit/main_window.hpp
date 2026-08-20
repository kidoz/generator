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
#include <nk/widgets/toast_overlay.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace generator::nkui {

/* Everything the menus render that can change at runtime. The app
 * controller owns the truth; the window only rebuilds labels from it. */
struct MenuState {
  std::vector<std::string> recent_roms; /* full paths, most recent first */
  int state_slot = 0;
  std::array<bool, 10> slot_saved{};
  bool paused = false;
};

/* Window chrome: menu surface on top, emulator stage in the middle, status
 * bar along the bottom.
 *
 * The window also owns the UI-side heartbeat. It runs on the NodalKit event
 * loop and does two things: drain SDL gamepad events and adopt whatever
 * field the emulation thread has finished. Emulation speed is not tied to
 * this timer - the emulation thread paces itself. */
class MainWindow {
public:
  MainWindow(nk::Application &app, int width, int height);
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

  /* A ROM dropped onto the emulator stage lands here. */
  void set_rom_drop_handler(std::function<void(std::string)> handler);

  /* Rebuild the menus (native or widget) from the given state. */
  void set_menu_state(MenuState state);

  /* Transient feedback above the emulator stage. */
  void show_toast(std::string title);

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
  void install_menus();
  void start_timers(nk::Application &app);
  void on_tick();
  void on_fps_tick();
  void refresh_state_segment();

  nk::Application &app_;

  nk::Window window_;
  std::shared_ptr<nk::Widget> root_;
  std::shared_ptr<EmulatorView> view_;
  std::shared_ptr<nk::ToastOverlay> toasts_;
  std::shared_ptr<nk::StatusBar> status_;
  std::shared_ptr<nk::MenuBar> menu_bar_;

  InputController input_;
  std::function<void(std::string_view)> command_handler_;
  std::function<void(std::string)> rom_drop_handler_;

  MenuState menu_state_;

  nk::CallbackHandle tick_{};
  nk::CallbackHandle fps_tick_{};

  bool was_focused_ = false;
  std::string status_text_ = "Ready";
  std::string rom_title_ = "No ROM";
  bool smooth_scaling_ = false;
};

}  // namespace generator::nkui
