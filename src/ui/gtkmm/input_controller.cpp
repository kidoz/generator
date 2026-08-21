#include "input_controller.hpp"

#include <iostream>

#include "ui_bridge.hpp"

#include "emulator_core.hpp"
#include "generator.h"

/* Pad state the backend accumulates on its way to the core.
 *
 * The machine takes input through EmulatorCore::set_input rather than
 * exposing its controller ports, so the backend holds what is currently
 * pressed and republishes both pads whenever anything changes. The core
 * reads a 3-button pad today; x/y/z/mode are tracked because the key map
 * already binds them, and go nowhere until it grows the 6-button
 * handshake. */
namespace {

struct PadState {
  unsigned int up = 0, down = 0, left = 0, right = 0;
  unsigned int a = 0, b = 0, c = 0, start = 0;
  unsigned int x = 0, y = 0, z = 0, mode = 0;
};

PadState g_pads[2];

void publish_pads()
{
  if (!g_emulator_core)
    return;

  for (int player = 0; player < 2; player++) {
    const PadState &pad = g_pads[player];
    g_emulator_core->set_input(player, pad.up, pad.down, pad.left, pad.right,
                               pad.start, pad.a, pad.b, pad.c, pad.x, pad.y,
                               pad.z, pad.mode);
  }
}

}  // namespace

// Default keyboard mappings for two players (6-button mode)
// Player 1: Arrow keys + Z/X/C/Enter + A/S/D/Tab
// Player 2: WASD + J/K/L/Space + U/I/O/RShift
static const struct {
  guint up, down, left, right, a, b, c, start;
  guint x, y, z, mode;
} default_keys[2] = {{GDK_KEY_Up, GDK_KEY_Down, GDK_KEY_Left, GDK_KEY_Right,
                      GDK_KEY_z, GDK_KEY_x, GDK_KEY_c, GDK_KEY_Return,
                      GDK_KEY_a, GDK_KEY_s, GDK_KEY_d, GDK_KEY_Tab},
                     {GDK_KEY_w, GDK_KEY_s, GDK_KEY_a, GDK_KEY_d, GDK_KEY_j,
                      GDK_KEY_k, GDK_KEY_l, GDK_KEY_space, GDK_KEY_u, GDK_KEY_i,
                      GDK_KEY_o, GDK_KEY_Shift_R}};

InputController::InputController()
{
  m_key_controller = Gtk::EventControllerKey::create();
  m_key_controller->signal_key_pressed().connect(
      sigc::mem_fun(*this, &InputController::on_key_pressed), false);
  m_key_controller->signal_key_released().connect(
      sigc::mem_fun(*this, &InputController::on_key_released), false);
}

InputController::~InputController()
{
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (m_gamepads[i].gamepad) {
      SDL_CloseGamepad(m_gamepads[i].gamepad);
      m_gamepads[i].gamepad = nullptr;
    }
  }
}

void InputController::attach_to_widget(Gtk::Widget &widget)
{
  widget.add_controller(m_key_controller);
}

void InputController::poll_sdl_events()
{
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
    }
  }
}

bool InputController::on_key_pressed(guint keyval, guint /*keycode*/,
                                     Gdk::ModifierType /*state*/)
{
  update_keyboard_controller(0, keyval, true);
  update_keyboard_controller(1, keyval, true);
  return false;  // Let the event propagate
}

void InputController::on_key_released(guint keyval, guint /*keycode*/,
                                      Gdk::ModifierType /*state*/)
{
  update_keyboard_controller(0, keyval, false);
  update_keyboard_controller(1, keyval, false);
}

void InputController::update_keyboard_controller(int player, guint keyval,
                                                 bool pressed)
{
  if (player < 0 || player > 1)
    return;

  PadState &controller = g_pads[player];
  if (keyval == default_keys[player].up)
    controller.up = pressed ? 1 : 0;
  else if (keyval == default_keys[player].down)
    controller.down = pressed ? 1 : 0;
  else if (keyval == default_keys[player].left)
    controller.left = pressed ? 1 : 0;
  else if (keyval == default_keys[player].right)
    controller.right = pressed ? 1 : 0;
  else if (keyval == default_keys[player].a)
    controller.a = pressed ? 1 : 0;
  else if (keyval == default_keys[player].b)
    controller.b = pressed ? 1 : 0;
  else if (keyval == default_keys[player].c)
    controller.c = pressed ? 1 : 0;
  else if (keyval == default_keys[player].start)
    controller.start = pressed ? 1 : 0;
  else if (keyval == default_keys[player].x)
    controller.x = pressed ? 1 : 0;
  else if (keyval == default_keys[player].y)
    controller.y = pressed ? 1 : 0;
  else if (keyval == default_keys[player].z)
    controller.z = pressed ? 1 : 0;
  else if (keyval == default_keys[player].mode)
    controller.mode = pressed ? 1 : 0;

  publish_pads();
}

void InputController::open_gamepad(SDL_JoystickID id)
{
  if (m_num_gamepads >= MAX_GAMEPADS)
    return;

  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (!m_gamepads[i].gamepad) {
      m_gamepads[i].gamepad = SDL_OpenGamepad(id);
      if (m_gamepads[i].gamepad) {
        m_gamepads[i].id = id;
        m_gamepads[i].player = m_num_gamepads;
        m_gamepads[i].axis_x_active = 0;
        m_gamepads[i].axis_y_active = 0;
        m_num_gamepads++;
        std::cout << "Gamepad attached: "
                  << SDL_GetGamepadName(m_gamepads[i].gamepad) << std::endl;
      }
      break;
    }
  }
}

void InputController::close_gamepad(SDL_JoystickID id)
{
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (m_gamepads[i].gamepad && m_gamepads[i].id == id) {
      SDL_CloseGamepad(m_gamepads[i].gamepad);
      m_gamepads[i].gamepad = nullptr;
      m_gamepads[i].id = 0;
      m_gamepads[i].player = -1;
      m_num_gamepads--;
      std::cout << "Gamepad disconnected." << std::endl;
      break;
    }
  }
}

int InputController::gamepad_id_to_player(SDL_JoystickID id)
{
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (m_gamepads[i].gamepad && m_gamepads[i].id == id) {
      return m_gamepads[i].player;
    }
  }
  return -1;
}

void InputController::handle_gamepad_button(const SDL_GamepadButtonEvent &event)
{
  int player = gamepad_id_to_player(event.which);
  if (player < 0 || player > 1)
    return;

  bool pressed = (event.down);
  PadState &controller = g_pads[player];
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
  }

  publish_pads();
}

void InputController::handle_gamepad_axis(const SDL_GamepadAxisEvent &event)
{
  int player = gamepad_id_to_player(event.which);
  if (player < 0 || player > 1)
    return;

  const int DEADZONE = 8000;
  PadState &controller = g_pads[player];

  if (event.axis == SDL_GAMEPAD_AXIS_LEFTX) {
    if (event.value < -DEADZONE) {
      controller.left = 1;
      controller.right = 0;
    } else if (event.value > DEADZONE) {
      controller.left = 0;
      controller.right = 1;
    } else {
      controller.left = 0;
      controller.right = 0;
    }
  } else if (event.axis == SDL_GAMEPAD_AXIS_LEFTY) {
    if (event.value < -DEADZONE) {
      controller.up = 1;
      controller.down = 0;
    } else if (event.value > DEADZONE) {
      controller.up = 0;
      controller.down = 1;
    } else {
      controller.up = 0;
      controller.down = 0;
    }
  }

  publish_pads();
}
