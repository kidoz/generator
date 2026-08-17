#include "input_controller.hpp"

#include <iostream>

extern "C" {
#include "generator.h"
#include "cpu68k.h" /* mem68k.h's DIRECTRAM fast paths need these first */
#include "mem68k.h" // For mem68k_cont
}

// Default keyboard mappings for two players (6-button mode)
// Player 1: Arrow keys + Z/X/C/Enter + A/S/D/Tab
// Player 2: WASD + J/K/L/Space + U/I/O/RShift
static const struct {
    guint up, down, left, right, a, b, c, start;
    guint x, y, z, mode;
} default_keys[2] = {
    {GDK_KEY_Up, GDK_KEY_Down, GDK_KEY_Left, GDK_KEY_Right,
     GDK_KEY_z, GDK_KEY_x, GDK_KEY_c, GDK_KEY_Return,
     GDK_KEY_a, GDK_KEY_s, GDK_KEY_d, GDK_KEY_Tab},
    {GDK_KEY_w, GDK_KEY_s, GDK_KEY_a, GDK_KEY_d, GDK_KEY_j,
     GDK_KEY_k, GDK_KEY_l, GDK_KEY_space,
     GDK_KEY_u, GDK_KEY_i, GDK_KEY_o, GDK_KEY_Shift_R}
};

InputController::InputController() {
    m_key_controller = Gtk::EventControllerKey::create();
    m_key_controller->signal_key_pressed().connect(sigc::mem_fun(*this, &InputController::on_key_pressed), false);
    m_key_controller->signal_key_released().connect(sigc::mem_fun(*this, &InputController::on_key_released), false);
}

InputController::~InputController() {
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (m_gamepads[i].gamepad) {
            SDL_CloseGamepad(m_gamepads[i].gamepad);
            m_gamepads[i].gamepad = nullptr;
        }
    }
}

void InputController::attach_to_widget(Gtk::Widget& widget) {
    widget.add_controller(m_key_controller);
}

void InputController::poll_sdl_events() {
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

bool InputController::on_key_pressed(guint keyval, guint /*keycode*/, Gdk::ModifierType /*state*/) {
    update_keyboard_controller(0, keyval, true);
    update_keyboard_controller(1, keyval, true);
    return false; // Let the event propagate
}

void InputController::on_key_released(guint keyval, guint /*keycode*/, Gdk::ModifierType /*state*/) {
    update_keyboard_controller(0, keyval, false);
    update_keyboard_controller(1, keyval, false);
}

void InputController::update_keyboard_controller(int player, guint keyval, bool pressed) {
    if (player < 0 || player > 1) return;

    if (keyval == default_keys[player].up) mem68k_cont[player].up = pressed ? 1 : 0;
    else if (keyval == default_keys[player].down) mem68k_cont[player].down = pressed ? 1 : 0;
    else if (keyval == default_keys[player].left) mem68k_cont[player].left = pressed ? 1 : 0;
    else if (keyval == default_keys[player].right) mem68k_cont[player].right = pressed ? 1 : 0;
    else if (keyval == default_keys[player].a) mem68k_cont[player].a = pressed ? 1 : 0;
    else if (keyval == default_keys[player].b) mem68k_cont[player].b = pressed ? 1 : 0;
    else if (keyval == default_keys[player].c) mem68k_cont[player].c = pressed ? 1 : 0;
    else if (keyval == default_keys[player].start) mem68k_cont[player].start = pressed ? 1 : 0;
    else if (keyval == default_keys[player].x) mem68k_cont[player].x = pressed ? 1 : 0;
    else if (keyval == default_keys[player].y) mem68k_cont[player].y = pressed ? 1 : 0;
    else if (keyval == default_keys[player].z) mem68k_cont[player].z = pressed ? 1 : 0;
    else if (keyval == default_keys[player].mode) mem68k_cont[player].mode = pressed ? 1 : 0;
}

void InputController::open_gamepad(SDL_JoystickID id) {
    if (m_num_gamepads >= MAX_GAMEPADS) return;

    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (!m_gamepads[i].gamepad) {
            m_gamepads[i].gamepad = SDL_OpenGamepad(id);
            if (m_gamepads[i].gamepad) {
                m_gamepads[i].id = id;
                m_gamepads[i].player = m_num_gamepads; 
                m_gamepads[i].axis_x_active = 0;
                m_gamepads[i].axis_y_active = 0;
                m_num_gamepads++;
                std::cout << "Gamepad attached: " << SDL_GetGamepadName(m_gamepads[i].gamepad) << std::endl;
            }
            break;
        }
    }
}

void InputController::close_gamepad(SDL_JoystickID id) {
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

int InputController::gamepad_id_to_player(SDL_JoystickID id) {
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (m_gamepads[i].gamepad && m_gamepads[i].id == id) {
            return m_gamepads[i].player;
        }
    }
    return -1;
}

void InputController::handle_gamepad_button(const SDL_GamepadButtonEvent& event) {
    int player = gamepad_id_to_player(event.which);
    if (player < 0 || player > 1) return;

    bool pressed = (event.down);
    switch (event.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:    mem68k_cont[player].up = pressed; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  mem68k_cont[player].down = pressed; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  mem68k_cont[player].left = pressed; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: mem68k_cont[player].right = pressed; break;
        case SDL_GAMEPAD_BUTTON_SOUTH:      mem68k_cont[player].a = pressed; break;
        case SDL_GAMEPAD_BUTTON_EAST:       mem68k_cont[player].b = pressed; break;
        case SDL_GAMEPAD_BUTTON_WEST:       mem68k_cont[player].c = pressed; break;
        case SDL_GAMEPAD_BUTTON_START:      mem68k_cont[player].start = pressed; break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: mem68k_cont[player].x = pressed; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: mem68k_cont[player].y = pressed; break;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:  mem68k_cont[player].z = pressed; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: mem68k_cont[player].mode = pressed; break;
    }
}

void InputController::handle_gamepad_axis(const SDL_GamepadAxisEvent& event) {
    int player = gamepad_id_to_player(event.which);
    if (player < 0 || player > 1) return;

    const int DEADZONE = 8000;
    
    if (event.axis == SDL_GAMEPAD_AXIS_LEFTX) {
        if (event.value < -DEADZONE) {
            mem68k_cont[player].left = 1;
            mem68k_cont[player].right = 0;
        } else if (event.value > DEADZONE) {
            mem68k_cont[player].left = 0;
            mem68k_cont[player].right = 1;
        } else {
            mem68k_cont[player].left = 0;
            mem68k_cont[player].right = 0;
        }
    } else if (event.axis == SDL_GAMEPAD_AXIS_LEFTY) {
        if (event.value < -DEADZONE) {
            mem68k_cont[player].up = 1;
            mem68k_cont[player].down = 0;
        } else if (event.value > DEADZONE) {
            mem68k_cont[player].up = 0;
            mem68k_cont[player].down = 1;
        } else {
            mem68k_cont[player].up = 0;
            mem68k_cont[player].down = 0;
        }
    }
}
