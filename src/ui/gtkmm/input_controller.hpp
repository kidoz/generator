#pragma once

#include <gtkmm.h>
#include <SDL3/SDL.h>

#define MAX_GAMEPADS 4

/* The twelve pad lines EmulatorCore::set_input consumes, in call order. */
enum PadButton {
  PAD_UP,
  PAD_DOWN,
  PAD_LEFT,
  PAD_RIGHT,
  PAD_A,
  PAD_B,
  PAD_C,
  PAD_START,
  PAD_X,
  PAD_Y,
  PAD_Z,
  PAD_MODE,
  PAD_BUTTON_COUNT
};

struct GamepadSlot {
  SDL_Gamepad *gamepad{nullptr};
  SDL_JoystickID id{0};
  int player{-1};
  int axis_x_active{0};
  int axis_y_active{0};
};

class InputController {
public:
  InputController();
  ~InputController();

  // Map the keyboard event controller to a widget (usually the main window)
  void attach_to_widget(Gtk::Widget &widget);

  // Call periodically (e.g. from the tick callback) to poll SDL3 events
  void poll_sdl_events();

private:
  // GTKmm Keyboard signals
  bool on_key_pressed(guint keyval, guint keycode, Gdk::ModifierType state);
  void on_key_released(guint keyval, guint keycode, Gdk::ModifierType state);

  // A keysym drives exactly one pad: the first player whose map claims it
  void handle_key(guint keyval, bool pressed);

  // SDL3 Gamepad handling
  void open_gamepad(SDL_JoystickID id);
  void close_gamepad(SDL_JoystickID id);
  int gamepad_id_to_player(SDL_JoystickID id);
  void handle_gamepad_button(const SDL_GamepadButtonEvent &event);
  void handle_gamepad_axis(const SDL_GamepadAxisEvent &event);

  Glib::RefPtr<Gtk::EventControllerKey> m_key_controller;

  GamepadSlot m_gamepads[MAX_GAMEPADS];
  int m_num_gamepads{0};
  bool m_sdl_gamepad_ready{false};

  /* Resolved keysym per pad line: m_keys[player][PadButton]. Built from
   * the key1_x / key2_x gtkopts entries with built-in fallbacks. */
  guint m_keys[2][PAD_BUTTON_COUNT]{};
};
