/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Keyboard and gamepad input for the NodalKit UI backend */

#include "input_controller.hpp"
#include "commands.hpp"

#include <nk/actions/shortcut.h>
#include <nk/platform/key_codes.h>

#include "generator.h"
#include "cpu68k.h" /* mem68k.h's DIRECTRAM fast paths need these first */
#include "mem68k.h"
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

  if (key == map.up)
    mem68k_cont[player].up = value;
  else if (key == map.down)
    mem68k_cont[player].down = value;
  else if (key == map.left)
    mem68k_cont[player].left = value;
  else if (key == map.right)
    mem68k_cont[player].right = value;
  else if (key == map.a)
    mem68k_cont[player].a = value;
  else if (key == map.b)
    mem68k_cont[player].b = value;
  else if (key == map.c)
    mem68k_cont[player].c = value;
  else if (key == map.start)
    mem68k_cont[player].start = value;
  else if (key == map.x)
    mem68k_cont[player].x = value;
  else if (key == map.y)
    mem68k_cont[player].y = value;
  else if (key == map.z)
    mem68k_cont[player].z = value;
  else if (key == map.mode)
    mem68k_cont[player].mode = value;
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
    mem68k_cont[player].up = 0;
    mem68k_cont[player].down = 0;
    mem68k_cont[player].left = 0;
    mem68k_cont[player].right = 0;
    mem68k_cont[player].a = 0;
    mem68k_cont[player].b = 0;
    mem68k_cont[player].c = 0;
    mem68k_cont[player].start = 0;
    mem68k_cont[player].x = 0;
    mem68k_cont[player].y = 0;
    mem68k_cont[player].z = 0;
    mem68k_cont[player].mode = 0;
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
  switch (event.button) {
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    mem68k_cont[player].up = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    mem68k_cont[player].down = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    mem68k_cont[player].left = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    mem68k_cont[player].right = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_SOUTH:
    mem68k_cont[player].a = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_EAST:
    mem68k_cont[player].b = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_WEST:
    mem68k_cont[player].c = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_START:
    mem68k_cont[player].start = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    mem68k_cont[player].x = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    mem68k_cont[player].y = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_LEFT_STICK:
    mem68k_cont[player].z = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
    mem68k_cont[player].mode = pressed;
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

  if (event.axis == SDL_GAMEPAD_AXIS_LEFTX) {
    mem68k_cont[player].left = event.value < -kAxisDeadzone ? 1 : 0;
    mem68k_cont[player].right = event.value > kAxisDeadzone ? 1 : 0;
  } else if (event.axis == SDL_GAMEPAD_AXIS_LEFTY) {
    mem68k_cont[player].up = event.value < -kAxisDeadzone ? 1 : 0;
    mem68k_cont[player].down = event.value > kAxisDeadzone ? 1 : 0;
  }
}

}  // namespace generator::nkui
