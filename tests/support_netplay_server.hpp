/* SPDX-License-Identifier: GPL-2.0-or-later */
/* In-process Kaillera peer for session and GTK integration tests. */

#pragma once

#include "kaillera_protocol.h"
#include "socket.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <string>

namespace generator::test {

inline void write_le32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

inline int send_message(netplay_socket_t *sock, const sockaddr_in &to,
                        uint16_t *seq, kaillera_msg_type_t type,
                        const void *payload, uint16_t payload_len)
{
  kaillera_packet_t pkt;
  kaillera_packet_init(&pkt);
  if (kaillera_packet_add_message(&pkt, (*seq)++, type, payload, payload_len) !=
      0)
    return -1;
  uint8_t buf[512];
  int n = kaillera_packet_serialize(&pkt, buf, sizeof(buf));
  if (n < 0)
    return -1;
  char host[32];
  inet_ntop(AF_INET, &to.sin_addr, host, sizeof(host));
  return socket_sendto(sock, buf, (size_t)n, host, ntohs(to.sin_port));
}

class LoopbackServer {
public:
  ~LoopbackServer()
  {
    stop();
  }

  bool start(int players = 1, int capacity = 2, int status = 0,
             const char *name = "Demo")
  {
    if (socket_subsystem_init() != 0)
      return false;
    sock_ = socket_create();
    if (sock_ == nullptr || socket_bind(sock_, 0) != 0)
      return false;
    room_.store(players | (capacity << 8) | (status << 16));
    name_ = name;
    port_ = socket_get_local_port(sock_);
    stop_.store(false);
    thread_ = std::thread([this] { loop(); });
    return port_ != 0;
  }

  void stop()
  {
    stop_.store(true);
    if (thread_.joinable())
      thread_.join();
    socket_destroy(sock_);
    sock_ = nullptr;
  }

  uint16_t port() const
  {
    return port_;
  }

  void update_room(int players, int capacity, int status)
  {
    room_.store(players | (capacity << 8) | (status << 16));
    room_changed_.store(true);
  }

private:
  void send_room(const sockaddr_in &from)
  {
    uint8_t body[160];
    size_t at = 0;
    write_le32(body + at, 7);
    at += 4;
    auto put = [&](const char *s) {
      size_t n = std::strlen(s) + 1;
      std::memcpy(body + at, s, n);
      at += n;
    };
    put(name_.c_str());
    put("Generator");
    put("other");
    int room = room_.load();
    body[at++] = room & 0xff;
    body[at++] = (room >> 8) & 0xff;
    body[at++] = (room >> 16) & 0xff;
    send_message(sock_, from, &seq_, KAILLERA_MSG_GAME_STATUS, body,
                 (uint16_t)at);
  }

  void reply_hello(const sockaddr_in &from)
  {
    char text[32];
    std::snprintf(text, sizeof(text), "HELLOD00D%u", port_);
    char host[32];
    inet_ntop(AF_INET, &from.sin_addr, host, sizeof(host));
    socket_sendto(sock_, text, std::strlen(text), host, ntohs(from.sin_port));
  }

  void handle_packet(const uint8_t *data, int len, const sockaddr_in &from)
  {
    kaillera_packet_t pkt;
    if (kaillera_packet_parse(&pkt, data, (size_t)len) != 0)
      return;
    int count = kaillera_packet_message_count(&pkt);
    for (int i = 0; i < count; i++) {
      kaillera_message_t msg;
      if (kaillera_packet_get_message(&pkt, i, &msg) != 0)
        continue;
      if (msg.type == KAILLERA_MSG_LOGIN) {
        send_message(sock_, from, &seq_, KAILLERA_MSG_SERVER_STATUS, nullptr,
                     0);
        peer_ = from;
        send_room(from);
      } else if (msg.type == KAILLERA_MSG_CREATE_GAME) {
        uint8_t body[14]{};
        write_le32(body, 7);
        body[4] = 1;
        std::memcpy(body + 5, "other", 6);
        body[13] = 1;
        send_message(sock_, from, &seq_, KAILLERA_MSG_PLAYER_JOIN, body,
                     sizeof(body));
      } else if (msg.type == KAILLERA_MSG_JOIN_GAME && msg.payload != nullptr &&
                 msg.length >= 4) {
        uint8_t body[64];
        std::memcpy(body, msg.payload, 4);
        body[4] = 1; /* player number */
        const char *name = "Player";
        std::memcpy(body + 5, name, 7);
        body[12] = 0;
        body[13] = 0;
        body[14] = 1;
        send_message(sock_, from, &seq_, KAILLERA_MSG_PLAYER_JOIN, body, 15);
      } else if (msg.type == KAILLERA_MSG_START_GAME) {
        uint8_t body[3] = {0, 0, 2};
        send_message(sock_, from, &seq_, KAILLERA_MSG_START_GAME, body, 3);
      } else if (msg.type == KAILLERA_MSG_GAME_DATA && msg.payload != nullptr) {
        kaillera_game_data_t gd;
        if (kaillera_parse_game_data(msg.payload, msg.length, &gd) != 0)
          continue;
        uint8_t raw[8];
        raw[0] = (uint8_t)(gd.frame_count & 0xff);
        raw[1] = (uint8_t)(gd.frame_count >> 8);
        raw[2] = 2;
        raw[3] = 2;
        if (gd.data_size >= 2) {
          raw[4] = gd.player_data[0];
          raw[5] = gd.player_data[1];
        } else {
          raw[4] = raw[5] = 0;
        }
        raw[6] = 0x04; /* remote: left */
        raw[7] = 0x00;
        send_message(sock_, from, &seq_, KAILLERA_MSG_GAME_DATA, raw, 8);
      }
    }
  }

  void loop()
  {
    while (!stop_.load()) {
      if (peer_.sin_port != 0 && room_changed_.exchange(false))
        send_room(peer_);
      uint8_t buf[1024];
      char host[64];
      uint16_t port = 0;
      int n = socket_recvfrom(sock_, buf, sizeof(buf), host, sizeof(host),
                              &port, 50);
      if (n <= 0)
        continue;
      sockaddr_in from{};
      from.sin_family = AF_INET;
      from.sin_port = htons(port);
      inet_pton(AF_INET, host, &from.sin_addr);
      if (n >= 5 && std::memcmp(buf, "HELLO", 5) == 0)
        reply_hello(from);
      else
        handle_packet(buf, n, from);
    }
  }

  netplay_socket_t *sock_ = nullptr;
  uint16_t port_ = 0;
  uint16_t seq_ = 1;
  sockaddr_in peer_{};
  std::string name_;
  std::atomic<int> room_{0};
  std::atomic<bool> room_changed_{false};
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

}  // namespace generator::test
