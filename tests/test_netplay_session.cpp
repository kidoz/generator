/* SPDX-License-Identifier: GPL-2.0-or-later */
/* One client against a loopback Kaillera peer. Proves the handshake, the
 * ROM-name gate, that a joiner cannot start, and that a host's frame
 * exchange returns both pads with the local pad in its own slot. */

#include "kaillera_protocol.h"
#include "netplay.h"
#include "socket.h"
#include "support_netplay_server.hpp"

#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <future>

using generator::test::LoopbackServer;

TEST_CASE("controller bits survive a round trip", "[netplay]")
{
  netplay_input_t in{};
  in.up = 1;
  in.a = 1;
  in.mode = 1;
  uint16_t bits = netplay_serialize_input(&in);
  CHECK(bits == ((1u << 0) | (1u << 4) | (1u << 11)));
  netplay_input_t out{};
  netplay_deserialize_input(bits, &out);
  CHECK(out.up == 1);
  CHECK(out.down == 0);
  CHECK(out.a == 1);
  CHECK(out.start == 0);
  CHECK(out.mode == 1);
}

TEST_CASE("a joiner cannot start, and a mismatched ROM cannot join",
          "[netplay]")
{
  LoopbackServer server;
  REQUIRE(server.start(2, 4, 1));
  REQUIRE(netplay_init() == 0);

  netplay_config_t cfg{};
  std::snprintf(cfg.server, sizeof(cfg.server), "127.0.0.1");
  cfg.port = server.port();
  std::snprintf(cfg.username, sizeof(cfg.username), "Player");
  cfg.conn_type = NETPLAY_CONN_LAN;
  REQUIRE(netplay_connect(&cfg) == 0);
  CHECK(netplay_get_state() == NETPLAY_LOBBY);

  netplay_game_t games[4];
  int listed = 0;
  auto list_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (listed == 0 && std::chrono::steady_clock::now() < list_deadline) {
    netplay_poll();
    listed = netplay_copy_games(games, 4);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(listed == 1);
  CHECK(std::string(games[0].name) == "Demo");
  CHECK(games[0].id == 7);
  CHECK(games[0].num_players == 2);
  CHECK(games[0].max_players == 4);
  CHECK(games[0].status == 1);
  CHECK_FALSE(netplay_in_room());

  netplay_set_expected_rom("Other");
  CHECK(netplay_join_game(7) == -1);
  CHECK(std::string(netplay_get_error()) ==
        "Loaded ROM does not match the room");

  netplay_set_expected_rom("Demo");
  REQUIRE(netplay_join_game(7) == 0);
  CHECK(netplay_in_room());
  CHECK(netplay_start_game() == -1);
  CHECK(std::string(netplay_get_error()) == "Only the host can start the game");

  netplay_disconnect();
  netplay_shutdown();
  server.stop();
}

TEST_CASE("the host's frame sync returns both pads", "[netplay]")
{
  LoopbackServer server;
  REQUIRE(server.start());
  REQUIRE(netplay_init() == 0);

  netplay_config_t cfg{};
  std::snprintf(cfg.server, sizeof(cfg.server), "127.0.0.1");
  cfg.port = server.port();
  std::snprintf(cfg.username, sizeof(cfg.username), "Host");
  cfg.conn_type = NETPLAY_CONN_LAN;
  REQUIRE(netplay_connect(&cfg) == 0);

  netplay_set_expected_rom("Demo");
  REQUIRE(netplay_create_game("Demo") == 0);
  CHECK(netplay_in_room());
  CHECK(netplay_is_host() == 1);
  REQUIRE(netplay_start_game() == 0);

  bool playing = false;
  auto start_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (!playing && std::chrono::steady_clock::now() < start_deadline) {
    netplay_poll();
    playing = netplay_get_state() == NETPLAY_IN_GAME;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(playing);
  CHECK(netplay_player_count() == 2);

  netplay_input_t local{};
  local.a = 1;
  netplay_input_t players[2]{};
  REQUIRE(netplay_sync_frame(&local, players, 2) == 0);
  CHECK(players[0].a == 1);
  CHECK(players[0].left == 0);
  CHECK(players[1].left == 1);
  CHECK(players[1].a == 0);

  netplay_end_game();
  CHECK_FALSE(netplay_in_room());

  netplay_disconnect();
  netplay_shutdown();
  server.stop();
}

TEST_CASE("connection cancellation interrupts the handshake wait", "[netplay]")
{
  REQUIRE(netplay_init() == 0);
  auto *silent = socket_create();
  REQUIRE(silent != nullptr);
  REQUIRE(socket_bind(silent, 0) == 0);
  netplay_config_t cfg{};
  std::snprintf(cfg.server, sizeof(cfg.server), "127.0.0.1");
  std::snprintf(cfg.username, sizeof(cfg.username), "Player");
  cfg.port = socket_get_local_port(silent);
  cfg.conn_type = NETPLAY_CONN_LAN;
  std::stop_source cancel;
  auto connection = std::async(std::launch::async, [&] {
    return netplay_connect(&cfg, cancel.get_token());
  });
  uint8_t hello[64];
  // A received HELLO proves cancellation happens during the receive wait.
  CHECK(socket_recv(silent, hello, sizeof(hello), 1000) > 0);
  cancel.request_stop();
  CHECK(connection.wait_for(std::chrono::seconds(1)) ==
        std::future_status::ready);
  CHECK(connection.get() == -1);
  CHECK(std::string(netplay_get_error()) == "Connection cancelled");
  netplay_disconnect();
  netplay_shutdown();
  socket_destroy(silent);
}
