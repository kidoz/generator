/* kaillera_protocol.c - Kaillera protocol packet handling */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kaillera_protocol.h"

/* Helper: write 16-bit little-endian */
static inline void write_le16(uint8_t *buf, uint16_t val)
{
  buf[0] = (uint8_t)(val & 0xFF);
  buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

/* Helper: read 16-bit little-endian */
static inline uint16_t read_le16(const uint8_t *buf)
{
  return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/* Helper: write 32-bit little-endian */
static inline void write_le32(uint8_t *buf, uint32_t val)
{
  buf[0] = (uint8_t)(val & 0xFF);
  buf[1] = (uint8_t)((val >> 8) & 0xFF);
  buf[2] = (uint8_t)((val >> 16) & 0xFF);
  buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

/* Helper: read 32-bit little-endian */
static inline uint32_t read_le32(const uint8_t *buf)
{
  return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
         ((uint32_t)buf[3] << 24);
}

void kaillera_packet_init(kaillera_packet_t *pkt)
{
  if (pkt == nullptr)
    return;
  pkt->message_count = 0;
  pkt->data_len = 0;
}

int kaillera_packet_add_message(kaillera_packet_t *pkt, uint16_t seq,
                                kaillera_msg_type_t type, const void *payload,
                                uint16_t payload_len)
{
  if (pkt == nullptr)
    return -1;
  if (pkt->message_count == UINT8_MAX)
    return -1;

  /* Calculate message size: 2 (seq) + 2 (len) + 1 (type) + payload */
  size_t msg_size = 5 + payload_len;

  /* Check if there's room */
  if (pkt->data_len + msg_size > sizeof(pkt->data))
    return -1;

  uint8_t *p = pkt->data + pkt->data_len;

  /* Write sequence number (little-endian) */
  write_le16(p, seq);
  p += 2;

  /* Write length (includes type byte) */
  write_le16(p, payload_len + 1);
  p += 2;

  /* Write message type */
  *p++ = (uint8_t)type;

  /* Write payload */
  if (payload_len > 0 && payload != nullptr) {
    memcpy(p, payload, payload_len);
  }

  pkt->data_len += msg_size;
  pkt->message_count++;

  return 0;
}

int kaillera_packet_serialize(const kaillera_packet_t *pkt, uint8_t *buf,
                              size_t buflen)
{
  if (pkt == nullptr || buf == nullptr)
    return -1;

  /* Packet format: [1 byte count][messages...] */
  size_t total_size = 1 + pkt->data_len;

  if (buflen < total_size)
    return -1;

  buf[0] = pkt->message_count;
  if (pkt->data_len > 0) {
    memcpy(buf + 1, pkt->data, pkt->data_len);
  }

  return (int)total_size;
}

int kaillera_packet_parse(kaillera_packet_t *pkt, const uint8_t *data,
                          size_t len)
{
  if (pkt == nullptr || data == nullptr || len < 1)
    return -1;

  uint8_t message_count = data[0];

  if (len > 1) {
    size_t data_len = len - 1;
    if (data_len > sizeof(pkt->data))
      data_len = sizeof(pkt->data);
    memcpy(pkt->data, data + 1, data_len);
    pkt->data_len = data_len;
  } else {
    pkt->data_len = 0;
  }

  const uint8_t *p = pkt->data;
  const uint8_t *end = pkt->data + pkt->data_len;
  uint8_t parsed_count = 0;
  while (parsed_count < message_count) {
    if ((size_t)(end - p) < 5)
      return -1;

    uint16_t msg_len = read_le16(p + 2);
    if (msg_len == 0)
      return -1;

    size_t payload_len = (size_t)msg_len - 1;
    if ((size_t)(end - p) < 5 + payload_len)
      return -1;

    p += 5 + payload_len;
    parsed_count++;
  }

  if (p != end)
    return -1;

  pkt->message_count = message_count;
  return 0;
}

int kaillera_packet_message_count(const kaillera_packet_t *pkt)
{
  if (pkt == nullptr)
    return 0;
  return pkt->message_count;
}

int kaillera_packet_get_message(const kaillera_packet_t *pkt, int index,
                                kaillera_message_t *msg)
{
  if (pkt == nullptr || msg == nullptr || index < 0 ||
      index >= pkt->message_count)
    return -1;

  const uint8_t *p = pkt->data;
  const uint8_t *end = pkt->data + pkt->data_len;

  /* Walk through messages to find the requested one */
  for (int i = 0; i <= index && p + 5 <= end; i++) {
    uint16_t seq = read_le16(p);
    uint16_t len = read_le16(p + 2);
    uint8_t type = p[4];
    if (len == 0)
      return -1;

    size_t payload_len = (size_t)len - 1;
    if ((size_t)(end - p) < 5 + payload_len)
      return -1;

    if (i == index) {
      msg->sequence = seq;
      msg->length = (uint16_t)payload_len; /* Length includes type byte */
      msg->type = (kaillera_msg_type_t)type;
      msg->payload = payload_len > 0 ? p + 5 : nullptr;
      return 0;
    }

    /* Move to next message */
    p += 5 + payload_len;
  }

  return -1;
}

int kaillera_serialize_login(const kaillera_login_t *login, uint8_t *buf,
                             size_t buflen)
{
  if (login == nullptr || buf == nullptr)
    return -1;

  uint8_t *p = buf;
  const uint8_t *end = buf + buflen;

  /* Write username (null-terminated) */
  size_t name_len = strlen(login->username) + 1;
  if (p + name_len > end)
    return -1;
  memcpy(p, login->username, name_len);
  p += name_len;

  /* Write emulator name (null-terminated) */
  size_t emu_len = strlen(login->emulator) + 1;
  if (p + emu_len > end)
    return -1;
  memcpy(p, login->emulator, emu_len);
  p += emu_len;

  /* Write connection type */
  if (p + 1 > end)
    return -1;
  *p++ = login->connection_type;

  return (int)(p - buf);
}

int kaillera_write_string(uint8_t *buf, size_t buflen, const char *str)
{
  if (buf == nullptr || str == nullptr)
    return -1;

  size_t len = strlen(str) + 1; /* Include null terminator */
  if (len > buflen)
    return -1;

  memcpy(buf, str, len);
  return (int)len;
}

const uint8_t *kaillera_read_string(const uint8_t *data, const uint8_t *end,
                                    char *str, size_t maxlen)
{
  if (data == nullptr || end == nullptr || str == nullptr || maxlen == 0)
    return nullptr;

  const uint8_t *p = data;
  size_t i = 0;

  while (p < end && *p != '\0') {
    if (i + 1 >= maxlen)
      return nullptr;
    str[i++] = (char)*p++;
  }

  if (p >= end)
    return nullptr;

  str[i] = '\0';
  p++;
  return p;
}

int kaillera_parse_server_status_users(const uint8_t *data, size_t len,
                                       kaillera_user_info_t *users,
                                       int max_users)
{
  if (data == nullptr || users == nullptr || max_users <= 0)
    return -1;

  const uint8_t *p = data;
  const uint8_t *end = data + len;
  int count = 0;

  /* Server status format:
   * [4B user_count][4B game_count]
   * For each user: [4B id][null-term name][2B ping][1B conn][1B status]
   */

  if (p + 8 > end)
    return -1;

  uint32_t user_count = read_le32(p);
  /* uint32_t game_count = read_le32(p + 4); */
  p += 8;

  for (uint32_t i = 0; i < user_count && p < end && count < max_users; i++) {
    if (p + 4 > end)
      break;

    users[count].user_id = read_le32(p);
    p += 4;

    p = kaillera_read_string(p, end, users[count].username,
                             sizeof(users[count].username));
    if (p == nullptr)
      break;

    if (p + 4 > end)
      break;

    users[count].ping = read_le16(p);
    p += 2;
    users[count].connection_type = *p++;
    users[count].status = *p++;

    count++;
  }

  return count;
}

int kaillera_parse_server_status_games(const uint8_t *data, size_t len,
                                       kaillera_game_info_t *games,
                                       int max_games)
{
  if (data == nullptr || games == nullptr || max_games <= 0)
    return -1;

  const uint8_t *p = data;
  const uint8_t *end = data + len;

  /* Skip to game data:
   * [4B user_count][4B game_count]
   * [user data...]
   * [game data...]
   */

  if (p + 8 > end)
    return -1;

  uint32_t user_count = read_le32(p);
  uint32_t game_count = read_le32(p + 4);
  p += 8;

  /* Skip user data */
  for (uint32_t i = 0; i < user_count && p < end; i++) {
    p += 4; /* user_id */
    while (p < end && *p != '\0')
      p++; /* name */
    if (p < end)
      p++;  /* null */
    p += 4; /* ping + conn + status */
  }

  /* Parse game data */
  int count = 0;
  for (uint32_t i = 0; i < game_count && p < end && count < max_games; i++) {
    if (p + 4 > end)
      break;

    games[count].game_id = read_le32(p);
    p += 4;

    p = kaillera_read_string(p, end, games[count].name,
                             sizeof(games[count].name));
    if (p == nullptr)
      break;

    p = kaillera_read_string(p, end, games[count].emulator,
                             sizeof(games[count].emulator));
    if (p == nullptr)
      break;

    p = kaillera_read_string(p, end, games[count].owner,
                             sizeof(games[count].owner));
    if (p == nullptr)
      break;

    if (p + 3 > end)
      break;

    games[count].num_players = *p++;
    games[count].max_players = *p++;
    games[count].status = *p++;

    count++;
  }

  return count;
}

int kaillera_serialize_game_data(const kaillera_game_data_t *gd, uint8_t *buf,
                                 size_t buflen)
{
  if (gd == nullptr || buf == nullptr)
    return -1;

  /* Game data format:
   * [2B frame_count][1B player_count][1B data_size][player_data...]
   */
  size_t total_data = (size_t)gd->player_count * gd->data_size;
  size_t total_size = 4 + total_data;

  if (gd->player_count > KAILLERA_MAX_PLAYERS || gd->data_size > 16 ||
      total_data > sizeof(gd->player_data))
    return -1;

  if (buflen < total_size)
    return -1;

  write_le16(buf, gd->frame_count);
  buf[2] = gd->player_count;
  buf[3] = gd->data_size;

  if (total_data > 0) {
    memcpy(buf + 4, gd->player_data, total_data);
  }

  return (int)total_size;
}

int kaillera_parse_game_data(const uint8_t *data, size_t len,
                             kaillera_game_data_t *gd)
{
  if (data == nullptr || gd == nullptr || len < 4)
    return -1;

  gd->frame_count = read_le16(data);
  gd->player_count = data[2];
  gd->data_size = data[3];

  if (gd->player_count > KAILLERA_MAX_PLAYERS || gd->data_size > 16)
    return -1;

  size_t total_data = (size_t)gd->player_count * gd->data_size;

  if (len < 4 + total_data)
    return -1;

  if (total_data > sizeof(gd->player_data))
    return -1;

  if (total_data > 0) {
    memcpy(gd->player_data, data + 4, total_data);
  }

  return 0;
}
