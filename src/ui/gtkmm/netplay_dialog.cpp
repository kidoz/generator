/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "netplay_dialog.hpp"

#include "emulator_thread.hpp"
#include "input_controller.hpp"
#include "netplay_session.hpp"
#include "ui_bridge.hpp"

#include "emulator_core.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

#if defined(NETPLAY)
#include "netplay.h"
#endif

namespace {

Gtk::Label *heading(const Glib::ustring &text)
{
  auto *label = Gtk::make_managed<Gtk::Label>(text);
  label->set_xalign(0);
  label->add_css_class("heading");
  return label;
}

Gtk::Box *field(const Glib::ustring &title, Gtk::Widget &widget)
{
  auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  auto *label = Gtk::make_managed<Gtk::Label>(title);
  label->set_xalign(0);
  label->set_mnemonic_widget(widget);
  box->append(*label);
  box->append(widget);
  return box;
}

std::string loaded_rom()
{
  if (!g_emulator_core || !g_emulator_core->rom_loaded())
    return {};
  const char *name = g_emulator_core->rom_name();
  return name != nullptr && name[0] != '\0' ? name : "ROM";
}

#if defined(NETPLAY)
std::string last_error(const char *fallback)
{
  const char *error = netplay_get_error();
  return error != nullptr ? error : fallback;
}
#endif

}  // namespace

NetplayDialog::NetplayDialog(Gtk::Window &parent, EmulatorThread &thread,
                             std::function<void()> choose_rom)
    : parent_(parent), thread_(thread)
{
  set_transient_for(parent);
  set_modal(false);
  set_hide_on_close(true);
  set_title("Netplay");
  set_default_size(560, 440);

  auto *root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
  root->set_margin(24);
  set_child(*root);

  connection_form_.append(*heading("Connect to a Kaillera server"));
  auto *address = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  server_.set_text("127.0.0.1");
  server_.set_placeholder_text("Hostname or IP address");
  server_.set_hexpand(true);
  server_.set_max_length(255);
  auto *server_field = field("Server", server_);
  server_field->set_hexpand(true);
  address->append(*server_field);
  port_.set_range(1, 65535);
  port_.set_increments(1, 10);
  port_.set_value(27888);
  port_.set_numeric(true);
  port_.set_width_chars(5);
  port_.set_max_width_chars(5);
  address->append(*field("Port", port_));
  connection_form_.append(*address);
  auto *local_hint = Gtk::make_managed<Gtk::Label>(
      "127.0.0.1 connects to a server on this computer.");
  local_hint->set_xalign(0);
  local_hint->set_wrap(true);
  local_hint->add_css_class("dim-label");
  connection_form_.append(*local_hint);
  username_.set_text("Player");
  username_.set_max_length(31);
  connection_form_.append(*field("Nickname", username_));
  connect_.add_css_class("suggested-action");
  connect_.set_halign(Gtk::Align::END);
  connect_.signal_clicked().connect(
      sigc::mem_fun(*this, &NetplayDialog::on_connect));
  server_.signal_activate().connect(
      sigc::mem_fun(*this, &NetplayDialog::on_connect));
  username_.signal_activate().connect(
      sigc::mem_fun(*this, &NetplayDialog::on_connect));
  server_.signal_changed().connect(
      sigc::mem_fun(*this, &NetplayDialog::update_controls));
  username_.signal_changed().connect(
      sigc::mem_fun(*this, &NetplayDialog::update_controls));
  connection_form_.append(connect_);
  root->append(connection_form_);

  auto *connection_status =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  status_.set_xalign(0);
  status_.set_hexpand(true);
  status_.set_wrap(true);
  connection_status->append(spinner_);
  connection_status->append(status_);
  cancel_.signal_clicked().connect([this] {
    cancel_source_.request_stop();
    progress_ = "Cancelling connection…";
    update_controls();
  });
  disconnect_.signal_clicked().connect(
      sigc::mem_fun(*this, &NetplayDialog::on_disconnect));
  connection_status->append(cancel_);
  connection_status->append(disconnect_);
  root->append(*connection_status);

  feedback_.set_xalign(0);
  feedback_.set_wrap(true);
  feedback_.set_selectable(true);
  feedback_.set_visible(false);
  root->append(feedback_);

  auto *rom_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  rom_.set_xalign(0);
  rom_.set_hexpand(true);
  rom_.set_ellipsize(Pango::EllipsizeMode::END);
  rom_row->append(rom_);
  choose_rom_.signal_clicked().connect(std::move(choose_rom));
  rom_row->append(choose_rom_);
  root->append(*rom_row);

  browser_.append(*heading("Available rooms"));
  games_.set_selection_mode(Gtk::SelectionMode::SINGLE);
  games_.add_css_class("boxed-list");
  auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroll->set_min_content_height(150);
  scroll->set_child(games_);
  room_list_.add(*scroll, "rooms");
  empty_.set_wrap(true);
  empty_.set_margin(24);
  empty_.add_css_class("dim-label");
  room_list_.add(empty_, "empty");
  browser_.append(room_list_);
  auto *room_actions =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  host_.signal_clicked().connect(sigc::mem_fun(*this, &NetplayDialog::on_host));
  join_.signal_clicked().connect(sigc::mem_fun(*this, &NetplayDialog::on_join));
  join_.add_css_class("suggested-action");
  host_.set_hexpand(true);
  host_.set_halign(Gtk::Align::START);
  room_actions->append(host_);
  room_actions->append(join_);
  browser_.append(*room_actions);
  root->append(browser_);
  games_.signal_row_selected().connect([this](Gtk::ListBoxRow *row) {
    int index = row != nullptr ? row->get_index() : -1;
    selected_id_ = index >= 0 && static_cast<size_t>(index) < rooms_.size()
                       ? rooms_[index].id
                       : -1;
    update_controls();
  });

  room_title_.set_xalign(0);
  room_title_.set_wrap(true);
  room_title_.add_css_class("heading");
  players_.set_xalign(0);
  room_hint_.set_xalign(0);
  room_hint_.set_wrap(true);
  room_hint_.add_css_class("dim-label");
  room_.append(room_title_);
  room_.append(players_);
  room_.append(room_hint_);
  auto *match_actions =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  leave_.signal_clicked().connect(
      sigc::mem_fun(*this, &NetplayDialog::on_leave));
  start_.signal_clicked().connect(
      sigc::mem_fun(*this, &NetplayDialog::on_start));
  resume_.signal_clicked().connect(
      sigc::mem_fun(*this, &NetplayDialog::return_to_game));
  start_.add_css_class("suggested-action");
  resume_.add_css_class("suggested-action");
  leave_.set_hexpand(true);
  leave_.set_halign(Gtk::Align::START);
  match_actions->append(leave_);
  match_actions->append(start_);
  match_actions->append(resume_);
  room_.append(*match_actions);
  root->append(room_);

  auto *hint = Gtk::make_managed<Gtk::Label>(
      "Closing this window keeps the connection. Reopen Netplay to manage it.");
  hint->set_xalign(0);
  hint->set_wrap(true);
  hint->add_css_class("dim-label");
  root->append(*hint);

#if defined(NETPLAY)
  thread_.run_exclusive([] {
    std::lock_guard lock(generator::gtkui::NetplaySession::mutex());
    netplay_init();
  });
#else
  phase_ = Phase::UNAVAILABLE;
#endif
  refresh();
  timer_ = Glib::signal_timeout().connect(
      [this] {
        refresh();
        return true;
      },
      200);
}

NetplayDialog::~NetplayDialog()
{
  timer_.disconnect();
  cancel_source_.request_stop();
  if (operation_.valid())
    operation_.wait();
}

bool NetplayDialog::in_room() const
{
  return phase_ == Phase::ROOM || phase_ == Phase::PLAYING;
}

void NetplayDialog::set_feedback(const std::string &message, bool error)
{
  feedback_.set_text(message);
  feedback_.set_visible(!message.empty());
  feedback_.remove_css_class("error");
  if (error)
    feedback_.add_css_class("error");
}

void NetplayDialog::run_operation(std::function<Result(std::stop_token)> fn,
                                  const std::string &progress)
{
  if (busy())
    return;
  set_feedback({});
  progress_ = progress;
  cancel_source_ = std::stop_source{};
  auto token = cancel_source_.get_token();
  operation_ =
      std::async(std::launch::async, [this, fn = std::move(fn), token] {
        Result result{};
        thread_.run_exclusive([&] {
          std::lock_guard lock(generator::gtkui::NetplaySession::mutex());
          result = fn(token);
        });
        return result;
      });
  update_controls();
}

void NetplayDialog::refresh()
{
  if (busy() && operation_.wait_for(std::chrono::seconds(0)) ==
                    std::future_status::ready) {
    auto result = operation_.get();
    connecting_ = false;
    set_feedback(result.message, !result.success);
  }
#if defined(NETPLAY)
  if (!busy()) {
    std::unique_lock lock(generator::gtkui::NetplaySession::mutex(),
                          std::try_to_lock);
    if (lock.owns_lock()) {
      auto state = netplay_get_state();
      phase_ = state == NETPLAY_IN_GAME ? Phase::PLAYING
               : state == NETPLAY_LOBBY
                   ? (netplay_in_room() ? Phase::ROOM : Phase::LOBBY)
               : state == NETPLAY_ERROR ? Phase::ERROR
                                        : Phase::DISCONNECTED;
      host_role_ = netplay_is_host();
      player_count_ = netplay_player_count();
      std::string error =
          state == NETPLAY_ERROR ? last_error("Connection failed") : "";
      if (!error.empty() && error != session_error_)
        set_feedback(error, true);
      session_error_ = std::move(error);
      netplay_game_t games[32];
      int count = netplay_copy_games(games, 32);
      std::vector<Room> rooms;
      for (int i = 0; i < count; ++i)
        rooms.push_back({games[i].id, games[i].name, games[i].owner,
                         games[i].num_players, games[i].max_players,
                         games[i].status});
      if (rooms != rooms_)
        rebuild_rooms(std::move(rooms));
    }
  }
#endif
  update_controls();
  bool playing = phase_ == Phase::PLAYING;
  if (playing && !was_playing_ && !busy())
    return_to_game();
  was_playing_ = playing;
}

void NetplayDialog::rebuild_rooms(std::vector<Room> rooms)
{
  int selection = selected_id_;
  while (auto *row = games_.get_row_at_index(0))
    games_.remove(*row);
  rooms_ = std::move(rooms);
  for (const auto &room : rooms_) {
    auto *row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    row->set_margin(12);
    auto *name = heading(room.name);
    name->set_ellipsize(Pango::EllipsizeMode::END);
    row->append(*name);
    auto *detail = Gtk::make_managed<Gtk::Label>(
        room.owner + " · " + std::to_string(room.players) + "/" +
        std::to_string(room.capacity) + " players · " +
        (room.status == 0 ? "Waiting" : "Playing"));
    detail->set_xalign(0);
    detail->set_ellipsize(Pango::EllipsizeMode::END);
    detail->add_css_class("dim-label");
    row->append(*detail);
    games_.append(*row);
  }
  selected_id_ = -1;
  for (size_t i = 0; i < rooms_.size(); ++i) {
    if (rooms_[i].id == selection) {
      games_.select_row(*games_.get_row_at_index(static_cast<int>(i)));
      break;
    }
  }
}

void NetplayDialog::update_controls()
{
  bool connected = phase_ == Phase::LOBBY || in_room();
  bool available = phase_ != Phase::UNAVAILABLE;
  bool ready = !busy() && available;
  std::string rom = loaded_rom();
  connection_form_.set_visible(!connected);
  connection_form_.set_sensitive(ready);
  connect_.set_sensitive(ready && !server_.get_text().empty() &&
                         !username_.get_text().empty());
  spinner_.set_spinning(busy());
  spinner_.set_visible(busy());
  cancel_.set_visible(busy() && connecting_);
  cancel_.set_sensitive(!cancel_source_.stop_requested());
  disconnect_.set_visible(connected);
  disconnect_.set_sensitive(ready);
  rom_.set_text(rom.empty() ? "No ROM loaded" : "ROM: " + rom);
  rom_.set_tooltip_text(rom);
  choose_rom_.set_sensitive(!busy() && !in_room());
  browser_.set_visible(connected && !in_room());
  browser_.set_sensitive(ready && connected);
  host_.set_sensitive(ready && connected && !rom.empty());
  host_.set_tooltip_text(rom.empty() ? "Choose a ROM before creating a room"
                                     : "Create a room for the loaded ROM");
  auto selected = std::ranges::find(rooms_, selected_id_, &Room::id);
  bool joinable = selected != rooms_.end() && selected->status == 0 &&
                  selected->players < selected->capacity &&
                  selected->name == rom;
  join_.set_sensitive(ready && connected && joinable && !rom.empty());
  join_.set_tooltip_text(selected == rooms_.end() ? "Select a room to join"
                         : selected->name != rom
                             ? "Choose this room's ROM before joining"
                         : !joinable ? "This room is full or already playing"
                                     : "Join the selected room");
  games_.set_sensitive(!busy());
  room_list_.set_visible_child(connected && !rooms_.empty() ? "rooms"
                                                            : "empty");
  empty_.set_text(busy() && connecting_ ? "Connecting to the server…"
                  : connected
                      ? "No rooms available. Create a room for your loaded ROM."
                      : "Connect to a server to browse rooms.");
  room_.set_visible(in_room());
  room_title_.set_text((host_role_ ? "Your room: " : "Room: ") + room_name_);
  players_.set_text("Players: " + std::to_string(player_count_) + " / 2");
  room_hint_.set_text(phase_ == Phase::PLAYING
                          ? "The match is running. Return to the game to play."
                      : host_role_ ? "Start when both players have joined."
                                   : "Waiting for the host to start the game.");
  start_.set_visible(phase_ == Phase::ROOM && host_role_);
  start_.set_sensitive(ready && player_count_ >= 2);
  leave_.set_sensitive(ready);
  resume_.set_visible(phase_ == Phase::PLAYING);
  resume_.set_sensitive(!busy());
  std::string status =
      busy()                         ? progress_
      : phase_ == Phase::UNAVAILABLE ? "Netplay is unavailable in this build."
      : phase_ == Phase::ERROR       ? "Connection error"
      : phase_ == Phase::PLAYING     ? "Playing online"
      : phase_ == Phase::ROOM
          ? (host_role_ ? "Hosting a room" : "Joined a room")
      : connected ? "Connected to " + server_.get_text().raw() + ":" +
                        std::to_string(port_.get_value_as_int())
                  : "Disconnected";
  status_.set_text(status);
  Glib::ustring summary = busy() ? (connecting_ ? "Connecting…" : "Working…")
                          : phase_ == Phase::UNAVAILABLE ? "Unavailable"
                          : phase_ == Phase::ERROR       ? "Error"
                          : phase_ == Phase::PLAYING     ? "Playing"
                          : phase_ == Phase::ROOM        ? "In room"
                          : connected                    ? "Lobby"
                                                         : "Disconnected";
  signal_session_changed.emit(summary, busy(), in_room());
}

void NetplayDialog::on_connect()
{
#if defined(NETPLAY)
  if (busy() || in_room() || phase_ == Phase::LOBBY)
    return;
  std::string server = server_.get_text();
  std::string username = username_.get_text();
  auto blank = [](const std::string &text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
  };
  if (blank(server) || blank(username)) {
    set_feedback("Enter a server address and nickname.", true);
    return;
  }
  if (server.size() >= sizeof(netplay_config_t::server) ||
      username.size() >= sizeof(netplay_config_t::username)) {
    set_feedback("Server or nickname is too long.", true);
    return;
  }
  netplay_config_t cfg{};
  std::snprintf(cfg.server, sizeof(cfg.server), "%s", server.c_str());
  std::snprintf(cfg.username, sizeof(cfg.username), "%s", username.c_str());
  cfg.port = static_cast<uint16_t>(port_.get_value_as_int());
  cfg.conn_type = NETPLAY_CONN_LAN;
  connecting_ = true;
  run_operation(
      [cfg](std::stop_token cancel) {
        int rc = netplay_connect(&cfg, cancel);
        if (cancel.stop_requested()) {
          netplay_disconnect();
          return Result{true, "Connection cancelled."};
        }
        return Result{rc == 0,
                      rc == 0 ? "Connected. Choose a room or create your own."
                              : last_error("Connection failed")};
      },
      "Connecting…");
#endif
}

void NetplayDialog::on_disconnect()
{
#if defined(NETPLAY)
  run_operation(
      [](std::stop_token) {
        netplay_disconnect();
        input_set_netplay_drive(false);
        return Result{true, "Disconnected from the server."};
      },
      "Disconnecting…");
#endif
}

void NetplayDialog::on_host()
{
#if defined(NETPLAY)
  if (busy() || phase_ != Phase::LOBBY || loaded_rom().empty())
    return;
  room_name_ = loaded_rom();
  run_operation(
      [name = room_name_](std::stop_token) {
        netplay_set_expected_rom(name.c_str());
        int rc = netplay_create_game(name.c_str());
        return Result{rc == 0, rc == 0
                                   ? "Room created. Waiting for another player."
                                   : last_error("Could not create room")};
      },
      "Creating room…");
#endif
}

void NetplayDialog::on_join()
{
#if defined(NETPLAY)
  if (busy() || phase_ != Phase::LOBBY || !join_.get_sensitive())
    return;
  room_name_ = loaded_rom();
  run_operation(
      [name = room_name_, id = selected_id_](std::stop_token) {
        netplay_set_expected_rom(name.c_str());
        int rc = netplay_join_game(id);
        return Result{rc == 0, rc == 0
                                   ? "Joined the room. Waiting for the host."
                                   : last_error("Could not join room")};
      },
      "Joining room…");
#endif
}

void NetplayDialog::on_start()
{
#if defined(NETPLAY)
  if (busy() || phase_ != Phase::ROOM || !host_role_ || player_count_ < 2)
    return;
  run_operation(
      [](std::stop_token) {
        int rc = netplay_start_game();
        return Result{rc == 0, rc == 0
                                   ? "Start requested. Waiting for the server."
                                   : last_error("Could not start game")};
      },
      "Starting game…");
#endif
}

void NetplayDialog::on_leave()
{
#if defined(NETPLAY)
  if (busy() || !in_room())
    return;
  run_operation(
      [](std::stop_token) {
        netplay_end_game();
        input_set_netplay_drive(false);
        return Result{true, "Left the room."};
      },
      "Leaving room…");
#endif
}

void NetplayDialog::return_to_game()
{
  hide();
  parent_.present();
}
