#pragma once

#include <gtkmm.h>
#include <SDL3/SDL.h>

#define MAX_GAMEPADS 4

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

  void update_keyboard_controller(int player, guint keyval, bool pressed);

  // SDL3 Gamepad handling
  void open_gamepad(SDL_JoystickID id);
  void close_gamepad(SDL_JoystickID id);
  int gamepad_id_to_player(SDL_JoystickID id);
  void handle_gamepad_button(const SDL_GamepadButtonEvent &event);
  void handle_gamepad_axis(const SDL_GamepadAxisEvent &event);

  Glib::RefPtr<Gtk::EventControllerKey> m_key_controller;

  GamepadSlot m_gamepads[MAX_GAMEPADS];
  int m_num_gamepads{0};
};
