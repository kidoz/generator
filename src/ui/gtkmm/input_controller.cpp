#include "input_controller.hpp"

#include <iostream>
#include <string>

#include "ui_bridge.hpp"
#include "gtkopts.h"

#include "emulator_core.hpp"
#include "generator.h"

/* Pad state the backend accumulates on its way to the core.
 *
 * The machine takes input through EmulatorCore::set_input rather than
 * exposing its controller ports, so the backend holds what is currently
 * pressed and republishes both pads whenever anything changes. All twelve
 * pad lines are tracked; the core answers to the directions, A/B/C and
 * start today and grows into x/y/z/mode through its six-button handshake. */
namespace {

struct PadState {
  unsigned int b[PAD_BUTTON_COUNT] = {};
};

PadState g_pads[2];

void publish_pads()
{
  if (!g_emulator_core)
    return;

  for (int player = 0; player < 2; player++) {
    const PadState &pad = g_pads[player];
    g_emulator_core->set_input(player, pad.b[PAD_UP], pad.b[PAD_DOWN],
                               pad.b[PAD_LEFT], pad.b[PAD_RIGHT],
                               pad.b[PAD_START], pad.b[PAD_A], pad.b[PAD_B],
                               pad.b[PAD_C], pad.b[PAD_X], pad.b[PAD_Y],
                               pad.b[PAD_Z], pad.b[PAD_MODE]);
  }
}

/* Keyboard fallbacks for a slot the gtkopts table has no usable keysym
 * for. Player 1: arrows + z/x/c (A/B/C) + a/s/d (X/Y/Z) + Return + Tab.
 * Player 2 mirrors the documented gtkopts defaults on the keypad cluster,
 * which keeps the two maps disjoint and leaves Space free for pause. */
constexpr guint kFallbackKeys[2][PAD_BUTTON_COUNT] = {
    {GDK_KEY_Up, GDK_KEY_Down, GDK_KEY_Left, GDK_KEY_Right, GDK_KEY_z,
     GDK_KEY_x, GDK_KEY_c, GDK_KEY_Return, GDK_KEY_a, GDK_KEY_s, GDK_KEY_d,
     GDK_KEY_Tab},
    {GDK_KEY_KP_8, GDK_KEY_KP_5, GDK_KEY_KP_4, GDK_KEY_KP_6, GDK_KEY_KP_Divide,
     GDK_KEY_KP_Multiply, GDK_KEY_KP_Subtract, GDK_KEY_KP_Enter, GDK_KEY_u,
     GDK_KEY_i, GDK_KEY_o, GDK_KEY_Shift_R}};

/* gtkopts key names for the first eight slots; x/y/z/mode have no conf
 * slots and always use the fallbacks. */
const char *const kConfSlotNames[PAD_BUTTON_COUNT] = {
    "up", "down",  "left",  "right", "a",     "b",
    "c",  "start", nullptr, nullptr, nullptr, nullptr};

guint conf_keyval(int player, int button)
{
  if (!kConfSlotNames[button])
    return GDK_KEY_VoidSymbol;

  const std::string key = std::string("key") + std::to_string(player + 1) +
                          "_" + kConfSlotNames[button];
  const char *value = gtkopts_getvalue(key.c_str());
  if (!value || !*value)
    return GDK_KEY_VoidSymbol;

  return gdk_keyval_from_name(value);
}

}  // namespace

InputController::InputController()
{
  /* Resolve every slot from the loaded configuration, falling back to the
     built-in map where the keysym is missing or unrecognized. */
  for (int player = 0; player < 2; player++) {
    for (int button = 0; button < PAD_BUTTON_COUNT; button++) {
      guint keyval = conf_keyval(player, button);
      if (keyval == GDK_KEY_VoidSymbol)
        keyval = kFallbackKeys[player][button];
      m_keys[player][button] = keyval;
    }
  }

  m_key_controller = Gtk::EventControllerKey::create();
  m_key_controller->signal_key_pressed().connect(
      sigc::mem_fun(*this, &InputController::on_key_pressed), false);
  m_key_controller->signal_key_released().connect(
      sigc::mem_fun(*this, &InputController::on_key_released), false);

  /* No gamepad event ever arrives unless the subsystem is up, and the
     audio backend only brings SDL_INIT_AUDIO, so this is ours to start. */
  m_sdl_gamepad_ready = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
  if (!m_sdl_gamepad_ready) {
    std::cerr << "SDL gamepad support unavailable: " << SDL_GetError()
              << std::endl;
  }
}

InputController::~InputController()
{
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (m_gamepads[i].gamepad) {
      SDL_CloseGamepad(m_gamepads[i].gamepad);
      m_gamepads[i].gamepad = nullptr;
    }
  }
  if (m_sdl_gamepad_ready)
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
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
  handle_key(keyval, true);
  return false;  // Let the event propagate
}

void InputController::on_key_released(guint keyval, guint /*keycode*/,
                                      Gdk::ModifierType /*state*/)
{
  handle_key(keyval, false);
}

void InputController::handle_key(guint keyval, bool pressed)
{
  /* A key drives exactly one pad: the first player whose map claims it.
     The default maps are disjoint, so this only arbitrates when a custom
     configuration binds one keysym to both. */
  for (int player = 0; player < 2; player++) {
    for (int button = 0; button < PAD_BUTTON_COUNT; button++) {
      if (m_keys[player][button] == keyval) {
        g_pads[player].b[button] = pressed ? 1u : 0u;
        publish_pads();
        return;
      }
    }
  }
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
    controller.b[PAD_UP] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    controller.b[PAD_DOWN] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    controller.b[PAD_LEFT] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    controller.b[PAD_RIGHT] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_SOUTH:
    controller.b[PAD_A] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_EAST:
    controller.b[PAD_B] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_WEST:
    controller.b[PAD_C] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_START:
    controller.b[PAD_START] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    controller.b[PAD_X] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    controller.b[PAD_Y] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_LEFT_STICK:
    controller.b[PAD_Z] = pressed;
    break;
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
    controller.b[PAD_MODE] = pressed;
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
      controller.b[PAD_LEFT] = 1;
      controller.b[PAD_RIGHT] = 0;
    } else if (event.value > DEADZONE) {
      controller.b[PAD_LEFT] = 0;
      controller.b[PAD_RIGHT] = 1;
    } else {
      controller.b[PAD_LEFT] = 0;
      controller.b[PAD_RIGHT] = 0;
    }
  } else if (event.axis == SDL_GAMEPAD_AXIS_LEFTY) {
    if (event.value < -DEADZONE) {
      controller.b[PAD_UP] = 1;
      controller.b[PAD_DOWN] = 0;
    } else if (event.value > DEADZONE) {
      controller.b[PAD_UP] = 0;
      controller.b[PAD_DOWN] = 1;
    } else {
      controller.b[PAD_UP] = 0;
      controller.b[PAD_DOWN] = 0;
    }
  }

  publish_pads();
}
