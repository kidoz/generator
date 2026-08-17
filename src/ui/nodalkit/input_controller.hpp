/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Keyboard and gamepad input for the NodalKit UI backend */

#pragma once

#include <nk/controllers/event_controller.h>
#include <nk/ui_core/widget.h>

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string_view>

namespace generator::nkui {

/* Translates NodalKit key events and SDL3 gamepad events into Mega Drive
 * controller state, and turns the remaining keys into application commands.
 *
 * NodalKit has no gamepad layer by design, so SDL3 keeps that job (it is
 * already linked for audio output). SDL video is never initialised, so this
 * only drives the joystick subsystem and does not compete with NodalKit for
 * the platform event loop. */
class InputController {
public:
  static constexpr int kMaxGamepads = 4;

  InputController();
  ~InputController();

  InputController(const InputController &) = delete;
  InputController &operator=(const InputController &) = delete;

  /* Attach keyboard handling at the root of the widget tree, where key
   * events end up after bubbling out of the focused widget. */
  void attach_to(nk::Widget &root);

  /* UI thread: drain pending SDL gamepad events. */
  void poll_gamepads();

  /* Non-input keys are forwarded as menu-style action names. */
  void set_command_handler(std::function<void(std::string_view)> handler);

  /* Whether Ctrl/Cmd accelerators are matched here. Off when the platform
   * has a native app menu, which dispatches them itself. */
  void set_accelerators_enabled(bool enabled);

  /* Release every button; used when the window loses focus or emulation
   * stops so a held direction does not stick. */
  void release_all();

private:
  struct GamepadSlot {
    SDL_Gamepad *gamepad = nullptr;
    SDL_JoystickID id = 0;
    int player = -1;
  };

  void on_key(int key_code, int modifiers, bool pressed);
  bool apply_emulator_key(int key_code, bool pressed);
  void dispatch_command(std::string_view action);

  void open_gamepad(SDL_JoystickID id);
  void close_gamepad(SDL_JoystickID id);
  int player_for(SDL_JoystickID id) const;
  void handle_gamepad_button(const SDL_GamepadButtonEvent &event);
  void handle_gamepad_axis(const SDL_GamepadAxisEvent &event);

  std::shared_ptr<nk::KeyboardController> keys_;
  std::function<void(std::string_view)> command_handler_;
  bool accelerators_enabled_ = true;
  bool sdl_gamepad_ready_ = false;

  GamepadSlot gamepads_[kMaxGamepads];
  int num_gamepads_ = 0;
};

}  // namespace generator::nkui
