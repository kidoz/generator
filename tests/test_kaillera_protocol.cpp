/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "kaillera_protocol.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

TEST_CASE("a truncated Kaillera packet is rejected", "[kaillera]")
{
  const uint8_t data[] = {1, 0x00, 0x00};
  kaillera_packet_t packet;
  CHECK(kaillera_packet_parse(&packet, data, sizeof(data)) < 0);
}

TEST_CASE("a Kaillera message length past the buffer is rejected", "[kaillera]")
{
  /* count=1, seq=0, length=50, type=keepalive, no payload bytes. */
  const uint8_t data[] = {1, 0x00, 0x00, 50, 0x00, KAILLERA_MSG_KEEPALIVE};
  kaillera_packet_t packet;
  CHECK(kaillera_packet_parse(&packet, data, sizeof(data)) < 0);
}

TEST_CASE("game data rejects more players than the protocol allows",
          "[kaillera]")
{
  const uint8_t data[] = {0x00, 0x00, 9, 1, 0x00};
  kaillera_game_data_t game;
  CHECK(kaillera_parse_game_data(data, sizeof(data), &game) < 0);
}

TEST_CASE("a Kaillera message round-trips through the packet", "[kaillera]")
{
  kaillera_packet_t packet;
  kaillera_packet_init(&packet);
  const uint8_t payload[] = {1, 2, 3};
  REQUIRE(kaillera_packet_add_message(&packet, 7, KAILLERA_MSG_KEEPALIVE,
                                      payload, sizeof(payload)) == 0);

  uint8_t bytes[64];
  const int size = kaillera_packet_serialize(&packet, bytes, sizeof(bytes));
  REQUIRE(size > 0);

  kaillera_packet_t parsed;
  REQUIRE(kaillera_packet_parse(&parsed, bytes, (size_t)size) == 0);
  kaillera_message_t message;
  REQUIRE(kaillera_packet_get_message(&parsed, 0, &message) == 0);
  CHECK(message.sequence == 7);
  CHECK(message.type == KAILLERA_MSG_KEEPALIVE);
  CHECK(message.length == sizeof(payload));
  REQUIRE(message.payload != nullptr);
  CHECK(std::memcmp(message.payload, payload, sizeof(payload)) == 0);
}
