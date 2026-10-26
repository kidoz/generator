/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <gtkmm.h>

#include <functional>
#include <future>
#include <stop_token>
#include <string>
#include <vector>

class EmulatorThread;

/* Session commands run away from GTK while the emulator is parked. The
 * window can be hidden without disconnecting an active session. */
class NetplayDialog : public Gtk::Window {
public:
  NetplayDialog(Gtk::Window &parent, EmulatorThread &thread,
                std::function<void()> choose_rom);
  ~NetplayDialog() override;

  bool busy() const
  {
    return operation_.valid();
  }
  bool in_room() const;
  sigc::signal<void(const Glib::ustring &, bool, bool)> signal_session_changed;

private:
  enum class Phase {
    DISCONNECTED,
    LOBBY,
    ROOM,
    PLAYING,
    ERROR,
    UNAVAILABLE
  };
  struct Room {
    int id;
    std::string name;
    std::string owner;
    int players;
    int capacity;
    int status;
    bool operator==(const Room &) const = default;
  };
  struct Result {
    bool success;
    std::string message;
  };

  void refresh();
  void update_controls();
  void rebuild_rooms(std::vector<Room> rooms);
  void set_feedback(const std::string &message, bool error = false);
  void run_operation(std::function<Result(std::stop_token)> fn,
                     const std::string &progress);
  void on_connect();
  void on_disconnect();
  void on_host();
  void on_join();
  void on_start();
  void on_leave();
  void return_to_game();

  Gtk::Window &parent_;
  EmulatorThread &thread_;
  Gtk::Entry server_;
  Gtk::SpinButton port_;
  Gtk::Entry username_;
  Gtk::Box connection_form_{Gtk::Orientation::VERTICAL, 12};
  Gtk::Box browser_{Gtk::Orientation::VERTICAL, 12};
  Gtk::Box room_{Gtk::Orientation::VERTICAL, 12};
  Gtk::Button connect_{"Connect"};
  Gtk::Button disconnect_{"Disconnect"};
  Gtk::Button cancel_{"Cancel"};
  Gtk::Button host_{"Create room"};
  Gtk::Button join_{"Join room"};
  Gtk::Button start_{"Start game"};
  Gtk::Button leave_{"Leave room"};
  Gtk::Button choose_rom_{"Choose ROM…"};
  Gtk::Button resume_{"Return to game"};
  Gtk::Label status_;
  Gtk::Label feedback_;
  Gtk::Label rom_;
  Gtk::Label room_title_;
  Gtk::Label players_;
  Gtk::Label room_hint_;
  Gtk::Label empty_;
  Gtk::Spinner spinner_;
  Gtk::ListBox games_;
  Gtk::Stack room_list_;
  sigc::connection timer_;
  std::future<Result> operation_;
  std::stop_source cancel_source_;
  Phase phase_ = Phase::DISCONNECTED;
  bool connecting_ = false;
  bool host_role_ = false;
  bool was_playing_ = false;
  int player_count_ = 0;
  int selected_id_ = -1;
  std::string room_name_;
  std::string progress_;
  std::string session_error_;
  std::vector<Room> rooms_;
};
