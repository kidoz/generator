#pragma once

#include <gtkmm.h>
#include <adwaita.h>
#include <memory>

#include "emulator_view.hpp"
#include "emulator_thread.hpp"
#include "input_controller.hpp"

class MainWindow : public Gtk::ApplicationWindow {
public:
  MainWindow(EmulatorThread &emu_thread);
  ~MainWindow() override;

  InputController &get_input_controller()
  {
    return m_input;
  }

private:
  void setup_ui();
  bool on_fps_tick();

  // The root layout container
  Gtk::Box m_vbox{Gtk::Orientation::VERTICAL};

  // The Adwaita HeaderBar (managed via raw pointer wrapped in Glib::wrap)
  Gtk::Widget *m_header_bar{nullptr};

  // Framed emulator display area.
  Gtk::Box m_viewport{Gtk::Orientation::VERTICAL};

  // Status bar at the bottom of the window (FPS for now).
  Gtk::Box m_status_bar{Gtk::Orientation::HORIZONTAL};
  Gtk::Label m_fps_label;
  Gtk::Label m_system_label;
  Gtk::Label m_audio_label;
  Gtk::Label m_state_label;
  sigc::connection m_fps_timer;

  // Emulator display area
  std::unique_ptr<EmulatorView> m_view;
  EmulatorThread &m_emu_thread;

  // Input Handling
  InputController m_input;
};
