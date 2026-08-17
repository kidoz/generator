/* kaillera_protocol.h - Kaillera protocol packet definitions */

#ifndef KAILLERA_PROTOCOL_H
#define KAILLERA_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Kaillera protocol version */
#define KAILLERA_VERSION "0.83"

/* Default Kaillera port */
#define KAILLERA_DEFAULT_PORT 27888

/* Maximum packet size */
#define KAILLERA_MAX_PACKET_SIZE 4096

/* Maximum message payload size */
#define KAILLERA_MAX_MESSAGE_SIZE 1024

/* Maximum players per game */
#define KAILLERA_MAX_PLAYERS 8

/* Kaillera message types */
typedef enum {
  KAILLERA_MSG_USER_QUIT = 0x01,
  KAILLERA_MSG_USER_JOIN = 0x02,
  KAILLERA_MSG_LOGIN = 0x03,
  KAILLERA_MSG_SERVER_STATUS = 0x04,
  KAILLERA_MSG_ACK = 0x05,
  KAILLERA_MSG_GLOBAL_CHAT = 0x07,
  KAILLERA_MSG_GAME_CHAT = 0x08,
  KAILLERA_MSG_KEEPALIVE = 0x09,
  KAILLERA_MSG_CREATE_GAME = 0x0A,
  KAILLERA_MSG_GAME_STATUS = 0x0B,
  KAILLERA_MSG_JOIN_GAME = 0x0C,
  KAILLERA_MSG_PLAYER_JOIN = 0x0D,
  KAILLERA_MSG_LEAVE_GAME = 0x0E,
  KAILLERA_MSG_PLAYER_LEAVE = 0x0F,
  KAILLERA_MSG_KICK = 0x10,
  KAILLERA_MSG_START_GAME = 0x11,
  KAILLERA_MSG_GAME_DATA = 0x12,
  KAILLERA_MSG_GAME_CACHE = 0x13,
  KAILLERA_MSG_DROP_GAME = 0x14,
  KAILLERA_MSG_READY = 0x15,
  KAILLERA_MSG_PLAYER_READY = 0x16,
  KAILLERA_MSG_CONNECTION_REJECTED = 0x17
} kaillera_msg_type_t;

/* Server status flags */
typedef enum {
  KAILLERA_SERVER_IDLE = 0,
  KAILLERA_SERVER_WAITING = 1,
  KAILLERA_SERVER_PLAYING = 2
} kaillera_server_status_t;

/* Kaillera message header */
typedef struct {
  uint16_t sequence;           /* Sequence number */
  uint16_t length;             /* Payload length */
  uint8_t type;                /* Message type */
} kaillera_msg_header_t;

/* Kaillera packet (contains multiple messages) */
typedef struct {
  uint8_t message_count;       /* Number of messages in packet */
  uint8_t data[KAILLERA_MAX_PACKET_SIZE - 1]; /* Message data */
  size_t data_len;             /* Current data length */
} kaillera_packet_t;

/* Parsed message structure */
typedef struct {
  kaillera_msg_type_t type;
  uint16_t sequence;
  uint16_t length;
  const uint8_t *payload;      /* Points into packet data */
} kaillera_message_t;

/* Login request data */
typedef struct {
  char username[32];
  char emulator[128];
  uint8_t connection_type;     /* 1=LAN, 2=Excellent, ..., 6=Bad */
} kaillera_login_t;

/* User info from server */
typedef struct {
  uint32_t user_id;
  char username[32];
  uint16_t ping;
  uint8_t connection_type;
  uint8_t status;              /* 0=idle, 1=playing */
} kaillera_user_info_t;

/* Game info from server */
typedef struct {
  uint32_t game_id;
  char name[128];
  char emulator[64];
  char owner[32];
  uint8_t num_players;
  uint8_t max_players;
  uint8_t status;              /* 0=waiting, 1=playing */
} kaillera_game_info_t;

/* Game data packet (input exchange) */
typedef struct {
  uint16_t frame_count;        /* Frame number (wraps at 65535) */
  uint8_t player_count;        /* Number of players */
  uint8_t data_size;           /* Size of each player's data */
  uint8_t player_data[KAILLERA_MAX_PLAYERS * 16]; /* Input data */
} kaillera_game_data_t;

/*
 * Initialize a packet for building.
 */
void kaillera_packet_init(kaillera_packet_t *pkt);

/*
 * Add a message to a packet.
 * Returns 0 on success, -1 if packet is full.
 */
int kaillera_packet_add_message(kaillera_packet_t *pkt, uint16_t seq,
                                kaillera_msg_type_t type,
                                const void *payload, uint16_t payload_len);

/*
 * Serialize a packet to a byte buffer.
 * Returns the number of bytes written, or -1 on error.
 */
int kaillera_packet_serialize(const kaillera_packet_t *pkt,
                              uint8_t *buf, size_t buflen);

/*
 * Parse a received packet.
 * Returns 0 on success, -1 on parse error.
 */
int kaillera_packet_parse(kaillera_packet_t *pkt,
                          const uint8_t *data, size_t len);

/*
 * Get the number of messages in a parsed packet.
 */
int kaillera_packet_message_count(const kaillera_packet_t *pkt);

/*
 * Get a message from a parsed packet by index.
 * Returns 0 on success, -1 if index out of range.
 */
int kaillera_packet_get_message(const kaillera_packet_t *pkt, int index,
                                kaillera_message_t *msg);

/*
 * Serialize login data.
 * Returns payload length.
 */
int kaillera_serialize_login(const kaillera_login_t *login,
                             uint8_t *buf, size_t buflen);

/*
 * Parse user info from server status message.
 * Returns number of users parsed, or -1 on error.
 */
int kaillera_parse_server_status_users(const uint8_t *data, size_t len,
                                       kaillera_user_info_t *users,
                                       int max_users);

/*
 * Parse game info from server status message.
 * Returns number of games parsed, or -1 on error.
 */
int kaillera_parse_server_status_games(const uint8_t *data, size_t len,
                                       kaillera_game_info_t *games,
                                       int max_games);

/*
 * Serialize game data (input) packet.
 * Returns payload length.
 */
int kaillera_serialize_game_data(const kaillera_game_data_t *gd,
                                 uint8_t *buf, size_t buflen);

/*
 * Parse game data (input) packet.
 * Returns 0 on success, -1 on error.
 */
int kaillera_parse_game_data(const uint8_t *data, size_t len,
                             kaillera_game_data_t *gd);

/*
 * Serialize a null-terminated string with length prefix.
 * Returns bytes written.
 */
int kaillera_write_string(uint8_t *buf, size_t buflen, const char *str);

/*
 * Read a null-terminated string from buffer.
 * Returns pointer past the string, or nullptr on error.
 */
const uint8_t *kaillera_read_string(const uint8_t *data, const uint8_t *end,
                                    char *str, size_t maxlen);

#endif /* KAILLERA_PROTOCOL_H */
