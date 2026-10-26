/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "socket.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

TEST_CASE("a datagram from a host other than the peer is dropped", "[kaillera]")
{
  REQUIRE(socket_subsystem_init() == 0);

  netplay_socket_t *peer = socket_create();
  netplay_socket_t *client = socket_create();
  netplay_socket_t *stranger = socket_create();
  REQUIRE(peer != nullptr);
  REQUIRE(client != nullptr);
  REQUIRE(stranger != nullptr);
  REQUIRE(socket_bind(peer, 0) == 0);
  REQUIRE(socket_bind(client, 0) == 0);
  REQUIRE(socket_bind(stranger, 0) == 0);

  const uint16_t peer_port = socket_get_local_port(peer);
  const uint16_t client_port = socket_get_local_port(client);
  REQUIRE(peer_port != 0);
  REQUIRE(client_port != 0);
  REQUIRE(socket_connect(peer, "127.0.0.1", client_port) == 0);

  const char bad[] = "BAD!";
  REQUIRE(socket_sendto(stranger, bad, sizeof(bad) - 1, "127.0.0.1",
                        peer_port) > 0);

  bool saw_bad = false;
  for (int attempt = 0; attempt < 5 && !saw_bad; attempt++) {
    char buf[16] = {};
    const int got = socket_recv(peer, buf, sizeof(buf), 50);
    if (got == (int)(sizeof(bad) - 1) &&
        std::memcmp(buf, bad, sizeof(bad) - 1) == 0) {
      saw_bad = true;
    }
  }
  CHECK_FALSE(saw_bad);

  const char good[] = "OK";
  REQUIRE(socket_sendto(client, good, sizeof(good) - 1, "127.0.0.1",
                        peer_port) > 0);

  bool saw_good = false;
  for (int attempt = 0; attempt < 8 && !saw_good; attempt++) {
    char buf[16] = {};
    const int got = socket_recv(peer, buf, sizeof(buf), 100);
    if (got == (int)(sizeof(good) - 1) &&
        std::memcmp(buf, good, sizeof(good) - 1) == 0) {
      saw_good = true;
    }
  }
  CHECK(saw_good);

  socket_destroy(stranger);
  socket_destroy(client);
  socket_destroy(peer);
  socket_subsystem_shutdown();
}
