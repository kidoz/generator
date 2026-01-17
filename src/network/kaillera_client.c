/* kaillera_client.c - Kaillera client implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kaillera_client.h"
#include "kaillera_protocol.h"
#include "socket.h"

/* Connection timeout in milliseconds */
#define CONNECT_TIMEOUT_MS 5000

/* Packet receive timeout for blocking operations */
#define RECV_TIMEOUT_MS 100

/* Game data sync timeout */
#define GAME_DATA_TIMEOUT_MS 1000

/* Keepalive interval */
#define KEEPALIVE_INTERVAL_MS 5000

/* Maximum ACK packets for ping calculation */
#define MAX_ACK_SAMPLES 10

/* Hello message for initial handshake */
#define KAILLERA_HELLO "HELLO" KAILLERA_VERSION

/* Client structure */
struct kaillera_client {
  netplay_socket_t *sock;
  kaillera_state_t state;
  kaillera_callbacks_t callbacks;

  /* Connection info */
  char server_host[256];
  uint16_t server_port;
  uint16_t game_port;       /* Port for game data after handshake */

  /* User info */
  char username[32];
  char emulator[128];
  uint8_t connection_type;
  uint32_t user_id;
  int player_number;        /* 0-based player number in game */
  int num_players;          /* Total players in game */

  /* Game state */
  uint32_t current_game_id;
  bool is_host;

  /* Protocol state */
  uint16_t send_seq;        /* Outgoing sequence number */
  uint16_t recv_seq;        /* Expected incoming sequence number */
  uint16_t frame_count;     /* Frame counter for game data */

  /* Ping calculation */
  int ping_samples[MAX_ACK_SAMPLES];
  int ping_sample_count;
  int ping_ms;

  /* Timing */
  uint64_t last_keepalive;
  uint64_t connect_start_time;

  /* Error handling */
  char error_msg[256];
};

/* Get current time in milliseconds */
static uint64_t get_time_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* Send a packet to the server */
static int client_send_packet(kaillera_client_t *client, const kaillera_packet_t *pkt)
{
  uint8_t buf[KAILLERA_MAX_PACKET_SIZE];
  int len = kaillera_packet_serialize(pkt, buf, sizeof(buf));
  if (len < 0)
    return -1;

  return socket_send(client->sock, buf, (size_t)len);
}

/* Send a single message */
static int client_send_message(kaillera_client_t *client, kaillera_msg_type_t type,
                               const void *payload, uint16_t payload_len)
{
  kaillera_packet_t pkt;
  kaillera_packet_init(&pkt);

  if (kaillera_packet_add_message(&pkt, client->send_seq++, type,
                                  payload, payload_len) < 0)
    return -1;

  return client_send_packet(client, &pkt);
}

/* Process a received message */
static void client_process_message(kaillera_client_t *client,
                                   const kaillera_message_t *msg)
{
  switch (msg->type) {
  case KAILLERA_MSG_ACK:
    /* ACK for ping calculation */
    if (client->state == KAILLERA_STATE_CONNECTING) {
      uint64_t now = get_time_ms();
      int ping = (int)(now - client->connect_start_time);
      if (client->ping_sample_count < MAX_ACK_SAMPLES) {
        client->ping_samples[client->ping_sample_count++] = ping;
      }
      /* Calculate average ping */
      int total = 0;
      for (int i = 0; i < client->ping_sample_count; i++) {
        total += client->ping_samples[i];
      }
      client->ping_ms = total / client->ping_sample_count;
    }
    break;

  case KAILLERA_MSG_SERVER_STATUS:
    /* Server status received - we're connected */
    if (client->state == KAILLERA_STATE_CONNECTING) {
      client->state = KAILLERA_STATE_CONNECTED;
      if (client->callbacks.on_connect) {
        client->callbacks.on_connect(client->callbacks.user_data);
      }
    }
    /* Parse user and game lists (not implemented here for brevity) */
    break;

  case KAILLERA_MSG_USER_JOIN:
    if (msg->payload != nullptr && client->callbacks.on_user_join) {
      /* Parse: [null-term name][2B ping][1B conn_type] */
      char name[32];
      const uint8_t *p = msg->payload;
      const uint8_t *end = msg->payload + msg->length;
      p = kaillera_read_string(p, end, name, sizeof(name));
      if (p != nullptr && p + 3 <= end) {
        uint16_t ping = p[0] | ((uint16_t)p[1] << 8);
        uint8_t conn = p[2];
        client->callbacks.on_user_join(name, ping, conn,
                                       client->callbacks.user_data);
      }
    }
    break;

  case KAILLERA_MSG_USER_QUIT:
    if (msg->payload != nullptr && client->callbacks.on_user_quit) {
      char name[32];
      kaillera_read_string(msg->payload, msg->payload + msg->length,
                          name, sizeof(name));
      client->callbacks.on_user_quit(name, client->callbacks.user_data);
    }
    break;

  case KAILLERA_MSG_GLOBAL_CHAT:
  case KAILLERA_MSG_GAME_CHAT:
    if (msg->payload != nullptr && client->callbacks.on_chat) {
      /* Parse: [null-term username][null-term message] */
      char username[32], message[256];
      const uint8_t *p = msg->payload;
      const uint8_t *end = msg->payload + msg->length;
      p = kaillera_read_string(p, end, username, sizeof(username));
      if (p != nullptr) {
        kaillera_read_string(p, end, message, sizeof(message));
        client->callbacks.on_chat(username, message,
                                  client->callbacks.user_data);
      }
    }
    break;

  case KAILLERA_MSG_GAME_STATUS:
    if (msg->payload != nullptr && client->callbacks.on_game_created) {
      /* Parse: [4B game_id][null-term name][null-term emu][null-term owner]
                [1B players][1B max][1B status] */
      const uint8_t *p = msg->payload;
      const uint8_t *end = msg->payload + msg->length;

      if (p + 4 <= end) {
        uint32_t game_id = p[0] | ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        p += 4;

        char name[128], owner[32];
        p = kaillera_read_string(p, end, name, sizeof(name));
        if (p != nullptr) {
          p = kaillera_read_string(p, end, (char[64]){0}, 64); /* emulator */
          if (p != nullptr) {
            kaillera_read_string(p, end, owner, sizeof(owner));
            client->callbacks.on_game_created(game_id, name, owner,
                                              client->callbacks.user_data);
          }
        }
      }
    }
    break;

  case KAILLERA_MSG_PLAYER_JOIN:
    if (msg->payload != nullptr) {
      /* Parse: [4B game_id][1B player_num][null-term name][2B ping][1B conn] */
      const uint8_t *p = msg->payload;
      const uint8_t *end = msg->payload + msg->length;

      if (p + 5 <= end) {
        /* uint32_t game_id = p[0] | ... */
        p += 4;
        int player_num = *p++;

        char name[32];
        p = kaillera_read_string(p, end, name, sizeof(name));

        if (client->callbacks.on_player_join) {
          client->callbacks.on_player_join(player_num, name,
                                           client->callbacks.user_data);
        }

        /* If this is us, record our player number */
        if (strcmp(name, client->username) == 0) {
          client->player_number = player_num;
        }
        client->num_players++;
      }
    }
    break;

  case KAILLERA_MSG_PLAYER_LEAVE:
    if (msg->payload != nullptr) {
      const uint8_t *p = msg->payload;
      const uint8_t *end = msg->payload + msg->length;

      if (p + 5 <= end) {
        p += 4; /* game_id */
        int player_num = *p++;

        char name[32];
        kaillera_read_string(p, end, name, sizeof(name));

        if (client->callbacks.on_player_leave) {
          client->callbacks.on_player_leave(player_num, name,
                                            client->callbacks.user_data);
        }
        client->num_players--;
      }
    }
    break;

  case KAILLERA_MSG_START_GAME:
    client->state = KAILLERA_STATE_PLAYING;
    client->frame_count = 0;
    if (client->callbacks.on_game_start) {
      client->callbacks.on_game_start(client->num_players,
                                      client->callbacks.user_data);
    }
    break;

  case KAILLERA_MSG_DROP_GAME:
    if (client->state == KAILLERA_STATE_PLAYING) {
      client->state = KAILLERA_STATE_CONNECTED;
      if (client->callbacks.on_game_drop) {
        client->callbacks.on_game_drop(client->callbacks.user_data);
      }
    }
    break;

  case KAILLERA_MSG_CONNECTION_REJECTED:
    snprintf(client->error_msg, sizeof(client->error_msg),
             "Connection rejected by server");
    client->state = KAILLERA_STATE_ERROR;
    if (client->callbacks.on_error) {
      client->callbacks.on_error(client->error_msg,
                                 client->callbacks.user_data);
    }
    break;

  default:
    /* Unknown message type */
    break;
  }
}

/* Poll for and process incoming packets */
static void client_receive_packets(kaillera_client_t *client, int timeout_ms)
{
  uint8_t buf[KAILLERA_MAX_PACKET_SIZE];

  int len = socket_recv(client->sock, buf, sizeof(buf), timeout_ms);
  if (len <= 0)
    return;

  kaillera_packet_t pkt;
  if (kaillera_packet_parse(&pkt, buf, (size_t)len) < 0)
    return;

  int msg_count = kaillera_packet_message_count(&pkt);
  for (int i = 0; i < msg_count; i++) {
    kaillera_message_t msg;
    if (kaillera_packet_get_message(&pkt, i, &msg) == 0) {
      client_process_message(client, &msg);
    }
  }
}

kaillera_client_t *kaillera_client_create(void)
{
  kaillera_client_t *client = calloc(1, sizeof(kaillera_client_t));
  if (client == nullptr)
    return nullptr;

  client->state = KAILLERA_STATE_DISCONNECTED;
  client->player_number = -1;
  client->ping_ms = -1;

  return client;
}

void kaillera_client_destroy(kaillera_client_t *client)
{
  if (client == nullptr)
    return;

  kaillera_client_disconnect(client);
  free(client);
}

void kaillera_client_set_callbacks(kaillera_client_t *client,
                                   const kaillera_callbacks_t *callbacks)
{
  if (client == nullptr)
    return;

  if (callbacks != nullptr) {
    client->callbacks = *callbacks;
  } else {
    memset(&client->callbacks, 0, sizeof(client->callbacks));
  }
}

int kaillera_client_connect(kaillera_client_t *client,
                            const char *host, uint16_t port,
                            const char *username, const char *emulator,
                            uint8_t connection_type)
{
  if (client == nullptr || host == nullptr || username == nullptr)
    return -1;

  if (client->state != KAILLERA_STATE_DISCONNECTED) {
    kaillera_client_disconnect(client);
  }

  /* Store connection info */
  snprintf(client->server_host, sizeof(client->server_host), "%s", host);
  client->server_port = port;
  snprintf(client->username, sizeof(client->username), "%s", username);
  snprintf(client->emulator, sizeof(client->emulator), "%s",
           emulator ? emulator : "Generator");
  client->connection_type = connection_type;

  /* Create socket */
  client->sock = socket_create();
  if (client->sock == nullptr) {
    snprintf(client->error_msg, sizeof(client->error_msg),
             "Failed to create socket: %s", socket_get_error());
    client->state = KAILLERA_STATE_ERROR;
    return -1;
  }

  /* Bind to any available port */
  if (socket_bind(client->sock, 0) < 0) {
    snprintf(client->error_msg, sizeof(client->error_msg),
             "Failed to bind socket: %s", socket_get_error());
    socket_destroy(client->sock);
    client->sock = nullptr;
    client->state = KAILLERA_STATE_ERROR;
    return -1;
  }

  /* Set remote address */
  if (socket_connect(client->sock, host, port) < 0) {
    snprintf(client->error_msg, sizeof(client->error_msg),
             "Failed to resolve server: %s", socket_get_error());
    socket_destroy(client->sock);
    client->sock = nullptr;
    client->state = KAILLERA_STATE_ERROR;
    return -1;
  }

  /* Send HELLO */
  client->state = KAILLERA_STATE_CONNECTING;
  client->connect_start_time = get_time_ms();

  if (socket_send(client->sock, KAILLERA_HELLO, strlen(KAILLERA_HELLO)) < 0) {
    snprintf(client->error_msg, sizeof(client->error_msg),
             "Failed to send hello: %s", socket_get_error());
    socket_destroy(client->sock);
    client->sock = nullptr;
    client->state = KAILLERA_STATE_ERROR;
    return -1;
  }

  /* Wait for HELLO response */
  uint8_t buf[64];
  int len = socket_recv(client->sock, buf, sizeof(buf), CONNECT_TIMEOUT_MS);
  if (len <= 0) {
    snprintf(client->error_msg, sizeof(client->error_msg),
             "Connection timeout - no response from server");
    socket_destroy(client->sock);
    client->sock = nullptr;
    client->state = KAILLERA_STATE_ERROR;
    return -1;
  }

  /* Check response */
  if (len >= 3 && memcmp(buf, "TOO", 3) == 0) {
    snprintf(client->error_msg, sizeof(client->error_msg),
             "Server is full");
    socket_destroy(client->sock);
    client->sock = nullptr;
    client->state = KAILLERA_STATE_ERROR;
    return -1;
  }

  if (len < 10 || memcmp(buf, "HELLOD00D", 9) != 0) {
    snprintf(client->error_msg, sizeof(client->error_msg),
             "Invalid server response");
    socket_destroy(client->sock);
    client->sock = nullptr;
    client->state = KAILLERA_STATE_ERROR;
    return -1;
  }

  /* Parse game port from response */
  /* Format: HELLOD00D<4-digit port> */
  char port_str[8] = {0};
  memcpy(port_str, buf + 9, len - 9 < 7 ? len - 9 : 6);
  client->game_port = (uint16_t)atoi(port_str);

  if (client->game_port == 0) {
    client->game_port = port; /* Use same port if not specified */
  }

  /* Reconnect to game port if different */
  if (client->game_port != port) {
    if (socket_connect(client->sock, host, client->game_port) < 0) {
      snprintf(client->error_msg, sizeof(client->error_msg),
               "Failed to connect to game port: %s", socket_get_error());
      socket_destroy(client->sock);
      client->sock = nullptr;
      client->state = KAILLERA_STATE_ERROR;
      return -1;
    }
  }

  /* Send login packet */
  kaillera_login_t login;
  snprintf(login.username, sizeof(login.username), "%s", client->username);
  snprintf(login.emulator, sizeof(login.emulator), "%s", client->emulator);
  login.connection_type = client->connection_type;

  uint8_t login_buf[256];
  int login_len = kaillera_serialize_login(&login, login_buf, sizeof(login_buf));
  if (login_len < 0 ||
      client_send_message(client, KAILLERA_MSG_LOGIN, login_buf,
                          (uint16_t)login_len) < 0) {
    snprintf(client->error_msg, sizeof(client->error_msg),
             "Failed to send login");
    socket_destroy(client->sock);
    client->sock = nullptr;
    client->state = KAILLERA_STATE_ERROR;
    return -1;
  }

  /* Wait for server status (indicates successful login) */
  uint64_t start = get_time_ms();
  while (client->state == KAILLERA_STATE_CONNECTING) {
    if (get_time_ms() - start > CONNECT_TIMEOUT_MS) {
      snprintf(client->error_msg, sizeof(client->error_msg),
               "Login timeout");
      socket_destroy(client->sock);
      client->sock = nullptr;
      client->state = KAILLERA_STATE_ERROR;
      return -1;
    }
    client_receive_packets(client, RECV_TIMEOUT_MS);
  }

  if (client->state == KAILLERA_STATE_ERROR) {
    socket_destroy(client->sock);
    client->sock = nullptr;
    return -1;
  }

  client->last_keepalive = get_time_ms();
  return 0;
}

void kaillera_client_disconnect(kaillera_client_t *client)
{
  if (client == nullptr)
    return;

  if (client->state != KAILLERA_STATE_DISCONNECTED && client->sock != nullptr) {
    /* Send quit message */
    client_send_message(client, KAILLERA_MSG_USER_QUIT, nullptr, 0);
  }

  if (client->sock != nullptr) {
    socket_destroy(client->sock);
    client->sock = nullptr;
  }

  client->state = KAILLERA_STATE_DISCONNECTED;
  client->player_number = -1;
  client->num_players = 0;
  client->current_game_id = 0;

  if (client->callbacks.on_disconnect) {
    client->callbacks.on_disconnect("Disconnected", client->callbacks.user_data);
  }
}

kaillera_state_t kaillera_client_get_state(kaillera_client_t *client)
{
  if (client == nullptr)
    return KAILLERA_STATE_DISCONNECTED;
  return client->state;
}

const char *kaillera_client_get_error(kaillera_client_t *client)
{
  if (client == nullptr)
    return "Invalid client";
  return client->error_msg[0] ? client->error_msg : nullptr;
}

void kaillera_client_poll(kaillera_client_t *client)
{
  if (client == nullptr || client->sock == nullptr)
    return;

  if (client->state == KAILLERA_STATE_DISCONNECTED ||
      client->state == KAILLERA_STATE_ERROR)
    return;

  /* Receive and process packets (non-blocking) */
  client_receive_packets(client, 0);

  /* Send keepalive if needed */
  uint64_t now = get_time_ms();
  if (now - client->last_keepalive > KEEPALIVE_INTERVAL_MS) {
    client_send_message(client, KAILLERA_MSG_KEEPALIVE, nullptr, 0);
    client->last_keepalive = now;
  }
}

int kaillera_client_chat(kaillera_client_t *client, const char *message)
{
  if (client == nullptr || message == nullptr)
    return -1;

  if (client->state != KAILLERA_STATE_CONNECTED &&
      client->state != KAILLERA_STATE_IN_GAME &&
      client->state != KAILLERA_STATE_PLAYING)
    return -1;

  /* Format: [null-term username][null-term message] */
  uint8_t buf[512];
  int pos = kaillera_write_string(buf, sizeof(buf), client->username);
  if (pos < 0)
    return -1;
  int msg_len = kaillera_write_string(buf + pos, sizeof(buf) - (size_t)pos, message);
  if (msg_len < 0)
    return -1;

  kaillera_msg_type_t type = (client->current_game_id != 0) ?
    KAILLERA_MSG_GAME_CHAT : KAILLERA_MSG_GLOBAL_CHAT;

  return client_send_message(client, type, buf, (uint16_t)(pos + msg_len));
}

int kaillera_client_create_game(kaillera_client_t *client, const char *game_name)
{
  if (client == nullptr || game_name == nullptr)
    return -1;

  if (client->state != KAILLERA_STATE_CONNECTED)
    return -1;

  /* Format: [null-term game_name][null-term emulator] */
  uint8_t buf[512];
  int pos = kaillera_write_string(buf, sizeof(buf), game_name);
  if (pos < 0)
    return -1;
  int emu_len = kaillera_write_string(buf + pos, sizeof(buf) - (size_t)pos,
                                      client->emulator);
  if (emu_len < 0)
    return -1;

  if (client_send_message(client, KAILLERA_MSG_CREATE_GAME, buf,
                          (uint16_t)(pos + emu_len)) < 0)
    return -1;

  client->state = KAILLERA_STATE_IN_GAME;
  client->is_host = true;
  client->player_number = 0;
  client->num_players = 1;

  return 0;
}

int kaillera_client_join_game(kaillera_client_t *client, uint32_t game_id)
{
  if (client == nullptr)
    return -1;

  if (client->state != KAILLERA_STATE_CONNECTED)
    return -1;

  /* Format: [4B game_id][1B conn_type] */
  uint8_t buf[5];
  buf[0] = (uint8_t)(game_id & 0xFF);
  buf[1] = (uint8_t)((game_id >> 8) & 0xFF);
  buf[2] = (uint8_t)((game_id >> 16) & 0xFF);
  buf[3] = (uint8_t)((game_id >> 24) & 0xFF);
  buf[4] = client->connection_type;

  if (client_send_message(client, KAILLERA_MSG_JOIN_GAME, buf, 5) < 0)
    return -1;

  client->state = KAILLERA_STATE_IN_GAME;
  client->current_game_id = game_id;
  client->is_host = false;

  return 0;
}

void kaillera_client_leave_game(kaillera_client_t *client)
{
  if (client == nullptr)
    return;

  if (client->state != KAILLERA_STATE_IN_GAME &&
      client->state != KAILLERA_STATE_PLAYING)
    return;

  client_send_message(client, KAILLERA_MSG_LEAVE_GAME, nullptr, 0);

  client->state = KAILLERA_STATE_CONNECTED;
  client->current_game_id = 0;
  client->player_number = -1;
  client->num_players = 0;
}

int kaillera_client_start_game(kaillera_client_t *client)
{
  if (client == nullptr)
    return -1;

  if (client->state != KAILLERA_STATE_IN_GAME || !client->is_host)
    return -1;

  /* Format: [2B frame_delay][1B player_num] */
  uint8_t buf[3];
  buf[0] = 0;  /* Frame delay low byte */
  buf[1] = 0;  /* Frame delay high byte */
  buf[2] = (uint8_t)client->num_players;

  return client_send_message(client, KAILLERA_MSG_START_GAME, buf, 3);
}

int kaillera_client_ready(kaillera_client_t *client)
{
  if (client == nullptr)
    return -1;

  if (client->state != KAILLERA_STATE_IN_GAME)
    return -1;

  return client_send_message(client, KAILLERA_MSG_READY, nullptr, 0);
}

int kaillera_client_modify_play_values(kaillera_client_t *client,
                                       void *input, int size)
{
  if (client == nullptr || input == nullptr || size <= 0)
    return -1;

  if (client->state != KAILLERA_STATE_PLAYING)
    return -1;

  /* Build game data packet */
  kaillera_game_data_t gd;
  gd.frame_count = client->frame_count++;
  gd.player_count = (uint8_t)client->num_players;
  gd.data_size = (uint8_t)size;

  /* Copy our input data */
  if ((size_t)size > sizeof(gd.player_data))
    return -1;
  memcpy(gd.player_data, input, (size_t)size);

  /* Serialize and send */
  uint8_t buf[256];
  int len = kaillera_serialize_game_data(&gd, buf, sizeof(buf));
  if (len < 0)
    return -1;

  if (client_send_message(client, KAILLERA_MSG_GAME_DATA, buf, (uint16_t)len) < 0)
    return -1;

  /* Wait for response with all players' data */
  uint64_t start = get_time_ms();
  bool received = false;
  kaillera_game_data_t response;

  while (!received) {
    if (get_time_ms() - start > GAME_DATA_TIMEOUT_MS) {
      snprintf(client->error_msg, sizeof(client->error_msg),
               "Game data sync timeout");
      return -1;
    }

    uint8_t recv_buf[KAILLERA_MAX_PACKET_SIZE];
    int recv_len = socket_recv(client->sock, recv_buf, sizeof(recv_buf),
                               RECV_TIMEOUT_MS);
    if (recv_len <= 0)
      continue;

    kaillera_packet_t pkt;
    if (kaillera_packet_parse(&pkt, recv_buf, (size_t)recv_len) < 0)
      continue;

    int msg_count = kaillera_packet_message_count(&pkt);
    for (int i = 0; i < msg_count; i++) {
      kaillera_message_t msg;
      if (kaillera_packet_get_message(&pkt, i, &msg) == 0) {
        if (msg.type == KAILLERA_MSG_GAME_DATA && msg.payload != nullptr) {
          if (kaillera_parse_game_data(msg.payload, msg.length, &response) == 0) {
            received = true;
            break;
          }
        } else if (msg.type == KAILLERA_MSG_DROP_GAME) {
          /* Game ended */
          client->state = KAILLERA_STATE_CONNECTED;
          return -1;
        }
      }
    }
  }

  /* Copy all players' data back to caller */
  int total_size = response.player_count * response.data_size;
  memcpy(input, response.player_data, (size_t)total_size);

  return total_size;
}

void kaillera_client_drop_game(kaillera_client_t *client)
{
  if (client == nullptr)
    return;

  if (client->state != KAILLERA_STATE_PLAYING)
    return;

  client_send_message(client, KAILLERA_MSG_DROP_GAME, nullptr, 0);
  client->state = KAILLERA_STATE_CONNECTED;
}

int kaillera_client_get_ping(kaillera_client_t *client)
{
  if (client == nullptr)
    return -1;
  return client->ping_ms;
}

int kaillera_client_get_player_number(kaillera_client_t *client)
{
  if (client == nullptr)
    return -1;
  return client->player_number;
}

int kaillera_client_get_num_players(kaillera_client_t *client)
{
  if (client == nullptr)
    return 0;
  return client->num_players;
}
