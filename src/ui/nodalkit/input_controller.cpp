/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Keyboard and gamepad input for the NodalKit UI backend */

#include "input_controller.hpp"
#include "commands.hpp"

#include <nk/actions/shortcut.h>
#include <nk/platform/key_codes.h>

#include "controller_ports.hpp"
#include "generator.h"
#include "log.h"

#include <utility>

namespace generator::nkui {

namespace {

using nk::KeyCode;

/* Two-player keyboard layout, carried over from the GTK4 backend so muscle
 * memory transfers between the two frontends.
 *   Player 1: arrows, Z/X/C, Return, A/S/D, Tab
 *   Player 2: WASD, J/K/L, Space, U/I/O, Right Shift */
struct KeyMap {
  KeyCode up, down, left, right;
  KeyCode a, b, c, start;
  KeyCode x, y, z, mode;
};

constexpr KeyMap kDefaultKeys[2] = {
    {KeyCode::Up, KeyCode::Down, KeyCode::Left, KeyCode::Right, KeyCode::Z,
     KeyCode::X, KeyCode::C, KeyCode::Return, KeyCode::A, KeyCode::S,
     KeyCode::D, KeyCode::Tab},
    {KeyCode::W, KeyCode::S, KeyCode::A, KeyCode::D, KeyCode::J, KeyCode::K,
     KeyCode::L, KeyCode::Space, KeyCode::U, KeyCode::I, KeyCode::O,
     KeyCode::RightShift},
};

constexpr int kAxisDeadzone = 8000;

bool has_modifier(int modifiers, nk::Modifiers flag)
{
  return (static_cast<uint32_t>(modifiers) & static_cast<uint32_t>(flag)) != 0;
}

/* Ctrl on Linux/Windows, Command on macOS. Both are reported separately, so
 * accepting either keeps one table for every platform. */
bool is_accelerator(int modifiers)
{
  return has_modifier(modifiers, nk::Modifiers::Ctrl) ||
         has_modifier(modifiers, nk::Modifiers::Super);
}

void set_button(int player, KeyCode key, const KeyMap &map, bool pressed)
{
  const unsigned int value = pressed ? 1 : 0;
  auto &controller = generator::controllers().controller(player);

  if (key == map.up)
    controller.up = value;
  else if (key == map.down)
    controller.down = value;
  else if (key == map.left)
    controller.left = value;
  else if (key == map.right)
    controller.right = value;
  else if (key == map.a)
    controller.a = value;
  else if (key == map.b)
    controller.b = value;
  else if (key == map.c)
    controller.c = value;
  else if (key == map.start)
    controller.start = value;
  else if (key == map.x)
    controller.x = value;
  else if (key == map.y)
    controller.y = value;
  else if (key == map.z)
    controller.z = value;
  else if (key == map.mode)
    controller.mode = value;
}

}  // namespace

InputController::InputController()
{
  keys_ = std::make_shared<nk::KeyboardController>();
  (void)keys_->on_key_pressed().connect(
      [this](int key, int modifiers) { on_key(key, modifiers, true); });
  (void)keys_->on_key_released().connect(
      [this](int key, int modifiers) { on_key(key, modifiers, false); });

  /* Audio already initialised SDL; adding the gamepad subsystem does not
   * pull in SDL video, so NodalKit keeps sole ownership of the window
   * system and the platform run loop. */
  if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
    sdl_gamepad_ready_ = true;
  } else {
    LOG_VERBOSE("SDL_InitSubSystem(GAMEPAD) failed: %s", SDL_GetError());
  }
}

InputController::~InputController()
{
  for (auto &slot : gamepads_) {
    if (slot.gamepad) {
      SDL_CloseGamepad(slot.gamepad);
      slot.gamepad = nullptr;
    }
  }

  if (sdl_gamepad_ready_)
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void InputController::attach_to(nk::Widget &root)
{
  root.add_controller(keys_);
}

void InputController::set_command_handler(
    std::function<void(std::string_view)> handler)
{
  command_handler_ = std::move(handler);
}

void InputController::set_accelerators_enabled(bool enabled)
{
  accelerators_enabled_ = enabled;
}

void InputController::release_all()
{
  for (int player = 0; player < 2; player++) {
    generator::controllers().controller(player) = {};
  }
}

void InputController::dispatch_command(std::string_view action)
{
  if (command_handler_)
    command_handler_(action);
}

void InputController::on_key(int key_code, int modifiers, bool pressed)
{
  const auto key = static_cast<KeyCode>(key_code);

  /* A release always clears its button, whatever the modifiers say. If the
   * user presses Z, then takes hold of Cmd, then lets Z go, the release
   * arrives with Super set - dropping it here would leave the button stuck
   * down for the game. */
  if (!pressed) {
    (void)apply_emulator_key(key_code, false);
    return;
  }

  if (is_accelerator(modifiers)) {
    /* A held modifier means the keystroke is a command, never controller
     * input - otherwise Cmd+S would press Player 1's Y button. */
    if (!accelerators_enabled_)
      return;

    switch (key) {
    case KeyCode::O:
      dispatch_command(commands::kOpenRom);
      break;
    case KeyCode::R:
      dispatch_command(commands::kReset);
      break;
    case KeyCode::P:
      dispatch_command(commands::kPause);
      break;
    case KeyCode::Q:
      dispatch_command(commands::kQuit);
      break;
    default:
      break;
    }
    return;
  }

  if (apply_emulator_key(key_code, true))
    return;

  /* Bare-key hotkeys. These stay out of the controller map on purpose. */
  switch (key) {
  case KeyCode::P:
    dispatch_command(commands::kPause);
    break;
  case KeyCode::F5:
    dispatch_command(commands::kSaveState);
    break;
  case KeyCode::F8:
    dispatch_command(commands::kLoadState);
    break;
  case KeyCode::F11:
    dispatch_command(commands::kFullscreen);
    break;
  default:
    break;
  }
}

bool InputController::apply_emulator_key(int key_code, bool pressed)
{
  const auto key = static_cast<KeyCode>(key_code);
  bool matched = false;

  for (int player = 0; player < 2; player++) {
    const KeyMap &map = kDefaultKeys[player];
    if (key == map.up || key == map.down || key == map.left ||
        key == map.right || key == map.a || key == map.b || key == map.c ||
        key == map.start || key == map.x || key == map.y || key == map.z ||
        key == map.mode) {
      set_button(player, key, map, pressed);
      matched = true;
    }
  }

  return matched;
}

void InputController::poll_gamepads()
{
  if (!sdl_gamepad_ready_)
    return;

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
      open_gamepad(event.gdevice.which);
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      close_gamepad(event.gdevice.which);
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      handle_gamepad_button(event.gbutton);
      break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      handle_gamepad_axis(event.gaxis);
      break;
    default:
      break;
    }
  }
}

void InputController::open_gamepad(SDL_JoystickID id)
{
  if (num_gamepads_ >= kMaxGamepads)
    return;

  for (auto &slot : gamepads_) {
    if (slot.gamepad)
      continue;

    slot.gamepad = SDL_OpenGamepad(id);
    if (!slot.gamepad)
      break;

    slot.id = id;
    slot.player = num_gamepads_;
    num_gamepads_++;
    LOG_VERBOSE("Gamepad attached: %s", SDL_GetGamepadName(slot.gamepad));
    break;
  }
}

void InputController::close_gamepad(SDL_JoystickID id)
{
  for (auto &slot : gamepads_) {
    if (!slot.gamepad || slot.id != id)
      continue;

    SDL_CloseGamepad(slot.gamepad);
    slot = GamepadSlot{};
    num_gamepads_--;
    LOG_VERBOSE("Gamepad disconnected");
    break;
  }
}

int InputController::player_for(SDL_JoystickID id) const
{
  for (const auto &slot : gamepads_) {
    if (slot.gamepad && slot.id == id)
      return slot.player;
  }
  return -1;
}

void InputController::handle_gamepad_button(const SDL_GamepadButtonEvent &event)
{
  const int player = player_for(event.which);
  if (player < 0 || player > 1)
    return;

  const unsigned int pressed = event.down ? 1 : 0;
  auto &controller = generator::controllers().controller(player);
  switch (event.button) {
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    controller.up = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    controller.down = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    controller.left = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    controller.right = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_SOUTH:
    controller.a = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_EAST:
    controller.b = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_WEST:
    controller.c = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_START:
    controller.start = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    controller.x = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    controller.y = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_LEFT_STICK:
    controller.z = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
    controller.mode = pressed;
    break;
  default:
    break;
  }
}

void InputController::handle_gamepad_axis(const SDL_GamepadAxisEvent &event)
{
  const int player = player_for(event.which);
  if (player < 0 || player > 1)
    return;

  auto &controller = generator::controllers().controller(player);
  if (event.axis == SDL_GAMEPAD_AXIS_LEFTX) {
    controller.left = event.value < -kAxisDeadzone ? 1 : 0;
    controller.right = event.value > kAxisDeadzone ? 1 : 0;
  } else if (event.axis == SDL_GAMEPAD_AXIS_LEFTY) {
    controller.up = event.value < -kAxisDeadzone ? 1 : 0;
    controller.down = event.value > kAxisDeadzone ? 1 : 0;
  }
}

}  // namespace generator::nkui
