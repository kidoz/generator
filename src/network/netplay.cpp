/* netplay.cpp - High-level netplay API implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>
#include <string>
#include <vector>

#include "netplay.h"
#include "kaillera_client.h"
#include "kaillera_protocol.h"
#include "socket.h"

/* Emulator headers for input access. */
#include "generator.h"

/* Emulator name sent to Kaillera servers */
#define NETPLAY_EMULATOR_NAME "Generator " VERSION

/* Internal state */
static struct {
  bool initialized;
  netplay_state_t state;
  netplay_config_t config;
  kaillera_client_t *client;
  netplay_callbacks_t callbacks;
  char error_msg[256];
  char expected_rom[128];
} netplay_ctx;

static std::mutex g_games_mu;
static std::vector<netplay_game_t> g_games;

/* Forward declarations for Kaillera callbacks */
static void on_kaillera_connect(void *user_data);
static void on_kaillera_disconnect(const char *reason, void *user_data);
static void on_kaillera_error(const char *error, void *user_data);
static void on_kaillera_chat(const char *username, const char *message,
                             void *user_data);
static void on_kaillera_player_leave(int player_num, const char *username,
                                     void *user_data);
static void on_kaillera_game_start(int num_players, void *user_data);
static void on_kaillera_game_drop(void *user_data);
static void on_kaillera_game_created(uint32_t game_id, const char *name,
                                     const char *owner, int players,
                                     int max_players, int status,
                                     void *user_data);

int netplay_init(void)
{
  if (netplay_ctx.initialized)
    return 0;

  memset(&netplay_ctx, 0, sizeof(netplay_ctx));

  /* Initialize socket subsystem */
  if (socket_subsystem_init() < 0) {
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg),
             "Failed to initialize socket subsystem");
    return -1;
  }

  netplay_ctx.state = NETPLAY_DISCONNECTED;
  netplay_ctx.initialized = true;
  {
    std::lock_guard<std::mutex> lock(g_games_mu);
    g_games.clear();
  }

  return 0;
}

void netplay_shutdown(void)
{
  if (!netplay_ctx.initialized)
    return;

  netplay_disconnect();
  socket_subsystem_shutdown();

  memset(&netplay_ctx, 0, sizeof(netplay_ctx));
}

void netplay_set_callbacks(const netplay_callbacks_t *callbacks)
{
  if (callbacks != nullptr) {
    netplay_ctx.callbacks = *callbacks;
  } else {
    memset(&netplay_ctx.callbacks, 0, sizeof(netplay_ctx.callbacks));
  }
}

int netplay_connect(const netplay_config_t *config, std::stop_token cancel)
{
  if (!netplay_ctx.initialized || config == nullptr)
    return -1;

  if (netplay_ctx.state != NETPLAY_DISCONNECTED) {
    netplay_disconnect();
  }

  /* Store configuration */
  netplay_ctx.config = *config;

  /* Set default port if not specified */
  if (netplay_ctx.config.port == 0) {
    netplay_ctx.config.port = KAILLERA_DEFAULT_PORT;
  }

  /* Create Kaillera client */
  netplay_ctx.client = kaillera_client_create();
  if (netplay_ctx.client == nullptr) {
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg),
             "Failed to create Kaillera client");
    netplay_ctx.state = NETPLAY_ERROR;
    return -1;
  }

  /* Set up Kaillera callbacks */
  kaillera_callbacks_t k_callbacks = {
      .on_connect = on_kaillera_connect,
      .on_disconnect = on_kaillera_disconnect,
      .on_error = on_kaillera_error,
      .on_user_join = nullptr,
      .on_user_quit = nullptr,
      .on_chat = on_kaillera_chat,
      .on_game_created = on_kaillera_game_created,
      .on_game_closed = nullptr,
      .on_player_join = nullptr,
      .on_player_leave = on_kaillera_player_leave,
      .on_game_start = on_kaillera_game_start,
      .on_game_drop = on_kaillera_game_drop,
      .on_game_data = nullptr,
      .user_data = nullptr};
  kaillera_client_set_callbacks(netplay_ctx.client, &k_callbacks);

  /* Initiate connection */
  netplay_ctx.state = NETPLAY_CONNECTING;

  int result = kaillera_client_connect(
      netplay_ctx.client, config->server, config->port, config->username,
      NETPLAY_EMULATOR_NAME, (uint8_t)config->conn_type, cancel);

  if (result < 0) {
    const char *err = kaillera_client_get_error(netplay_ctx.client);
    std::string message = err ? err : "Connection failed";
    // Destroy invokes on_disconnect; preserve the actual failure afterward.
    kaillera_client_destroy(netplay_ctx.client);
    netplay_ctx.client = nullptr;
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg), "%s",
             message.c_str());
    netplay_ctx.state = NETPLAY_ERROR;
    return -1;
  }

  netplay_ctx.state = NETPLAY_LOBBY;
  return 0;
}

void netplay_disconnect(void)
{
  if (!netplay_ctx.initialized)
    return;

  if (netplay_ctx.client != nullptr) {
    kaillera_client_disconnect(netplay_ctx.client);
    kaillera_client_destroy(netplay_ctx.client);
    netplay_ctx.client = nullptr;
  }

  netplay_ctx.state = NETPLAY_DISCONNECTED;
  netplay_ctx.error_msg[0] = '\0';
  {
    std::lock_guard<std::mutex> lock(g_games_mu);
    g_games.clear();
  }
}

netplay_state_t netplay_get_state(void)
{
  return netplay_ctx.state;
}

const char *netplay_get_error(void)
{
  if (netplay_ctx.error_msg[0] != '\0')
    return netplay_ctx.error_msg;
  return nullptr;
}

int netplay_create_game(const char *rom_name)
{
  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr)
    return -1;

  if (netplay_ctx.state != NETPLAY_LOBBY)
    return -1;

  if (kaillera_client_create_game(netplay_ctx.client, rom_name) < 0) {
    const char *err = kaillera_client_get_error(netplay_ctx.client);
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg), "%s",
             err ? err : "Failed to create game");
    return -1;
  }

  /* Set local player to player 0 (host) */
  netplay_ctx.config.local_player = 0;

  return 0;
}

void netplay_set_expected_rom(const char *rom_name)
{
  if (rom_name == nullptr) {
    netplay_ctx.expected_rom[0] = '\0';
    return;
  }
  snprintf(netplay_ctx.expected_rom, sizeof(netplay_ctx.expected_rom), "%s",
           rom_name);
}

int netplay_copy_games(netplay_game_t *out, int max_out)
{
  std::lock_guard<std::mutex> lock(g_games_mu);
  if (out == nullptr || max_out <= 0)
    return (int)g_games.size();
  int n = (int)g_games.size();
  if (n > max_out)
    n = max_out;
  for (int i = 0; i < n; i++)
    out[i] = g_games[(size_t)i];
  return n;
}

int netplay_player_count(void)
{
  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr)
    return 0;
  int n = kaillera_client_get_num_players(netplay_ctx.client);
  return n > 0 ? n : 0;
}

int netplay_is_host(void)
{
  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr)
    return 0;
  kaillera_state_t kstate = kaillera_client_get_state(netplay_ctx.client);
  if (kstate != KAILLERA_STATE_IN_GAME && kstate != KAILLERA_STATE_PLAYING)
    return 0;
  return kaillera_client_get_player_number(netplay_ctx.client) == 0;
}

int netplay_in_room(void)
{
  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr)
    return 0;
  auto state = kaillera_client_get_state(netplay_ctx.client);
  return state == KAILLERA_STATE_IN_GAME || state == KAILLERA_STATE_PLAYING;
}

int netplay_join_game(int game_id)
{
  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr)
    return -1;

  if (netplay_ctx.state != NETPLAY_LOBBY)
    return -1;

  bool listed = false;
  {
    std::lock_guard<std::mutex> lock(g_games_mu);
    for (const netplay_game_t &game : g_games) {
      if (game.id != game_id)
        continue;
      listed = true;
      if (netplay_ctx.expected_rom[0] != '\0' &&
          strcmp(game.name, netplay_ctx.expected_rom) != 0) {
        snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg),
                 "Loaded ROM does not match the room");
        return -1;
      }
    }
  }
  if (!listed) {
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg),
             "Game is not in the server list");
    return -1;
  }

  if (kaillera_client_join_game(netplay_ctx.client, (uint32_t)game_id) < 0) {
    const char *err = kaillera_client_get_error(netplay_ctx.client);
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg), "%s",
             err ? err : "Failed to join game");
    return -1;
  }

  return 0;
}

int netplay_start_game(void)
{
  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr)
    return -1;

  if (kaillera_client_get_state(netplay_ctx.client) != KAILLERA_STATE_IN_GAME)
    return -1;

  if (kaillera_client_get_player_number(netplay_ctx.client) != 0) {
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg),
             "Only the host can start the game");
    return -1;
  }

  kaillera_client_ready(netplay_ctx.client);

  if (kaillera_client_start_game(netplay_ctx.client) < 0) {
    const char *err = kaillera_client_get_error(netplay_ctx.client);
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg), "%s",
             err ? err : "Failed to start the game");
    return -1;
  }

  return 0;
}

void netplay_end_game(void)
{
  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr)
    return;

  kaillera_state_t kstate = kaillera_client_get_state(netplay_ctx.client);

  if (kstate == KAILLERA_STATE_PLAYING) {
    kaillera_client_drop_game(netplay_ctx.client);
  } else if (kstate == KAILLERA_STATE_IN_GAME) {
    kaillera_client_leave_game(netplay_ctx.client);
  }

  netplay_ctx.state = NETPLAY_LOBBY;
}

int netplay_chat_send(const char *text)
{
  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr ||
      text == nullptr)
    return -1;

  return kaillera_client_chat(netplay_ctx.client, text);
}

int netplay_request_gamelist(void)
{
  /* The server pushes GAME_STATUS; there is no list-request message.
   * Hand the cache to the callback so a UI refresh still has a call. */
  if (!netplay_ctx.initialized)
    return -1;
  if (netplay_ctx.callbacks.on_gamelist) {
    std::vector<netplay_game_t> copy;
    {
      std::lock_guard<std::mutex> lock(g_games_mu);
      copy = g_games;
    }
    netplay_ctx.callbacks.on_gamelist(copy.empty() ? nullptr : copy.data(),
                                      (int)copy.size(),
                                      netplay_ctx.callbacks.user_data);
  }
  return 0;
}

int netplay_sync_frame(const netplay_input_t *local, netplay_input_t *players,
                       int max_players)
{
  if (local == nullptr || players == nullptr || max_players <= 0)
    return -1;

  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr)
    return -1;

  if (netplay_ctx.state != NETPLAY_IN_GAME)
    return -1;

  int num_players = kaillera_client_get_num_players(netplay_ctx.client);
  if (num_players < 1 || num_players > KAILLERA_MAX_PLAYERS) {
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg),
             "Invalid player count");
    netplay_ctx.state = NETPLAY_ERROR;
    return -1;
  }

  /* The wire slot is this client's player number, not always index 0. */
  int local_player = kaillera_client_get_player_number(netplay_ctx.client);
  if (local_player < 0 || local_player >= num_players)
    local_player = netplay_ctx.config.local_player;
  if (local_player < 0 || local_player >= num_players) {
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg),
             "Local player index is not in this match");
    return -1;
  }
  netplay_ctx.config.local_player = local_player;

  uint16_t inputs[KAILLERA_MAX_PLAYERS] = {};
  inputs[local_player] = netplay_serialize_input(local);

  int result = kaillera_client_modify_play_values(
      netplay_ctx.client, inputs, num_players * (int)sizeof(uint16_t));

  if (result < 0) {
    /* Connection lost or error */
    const char *err = kaillera_client_get_error(netplay_ctx.client);
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg), "%s",
             err ? err : "Connection lost");
    netplay_ctx.state = NETPLAY_ERROR;
    return -1;
  }

  /* Hand every player's input back to the caller */
  for (int i = 0; i < num_players && i < max_players; i++) {
    netplay_deserialize_input(inputs[i], &players[i]);
  }

  return 0;
}

void netplay_poll(void)
{
  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr)
    return;

  kaillera_client_poll(netplay_ctx.client);

  /* Update our state based on Kaillera client state */
  kaillera_state_t kstate = kaillera_client_get_state(netplay_ctx.client);

  switch (kstate) {
  case KAILLERA_STATE_DISCONNECTED:
    netplay_ctx.state = NETPLAY_DISCONNECTED;
    break;
  case KAILLERA_STATE_CONNECTING:
    netplay_ctx.state = NETPLAY_CONNECTING;
    break;
  case KAILLERA_STATE_CONNECTED:
    netplay_ctx.state = NETPLAY_LOBBY;
    break;
  case KAILLERA_STATE_IN_GAME:
    /* Still in game lobby, waiting to start */
    break;
  case KAILLERA_STATE_PLAYING:
    netplay_ctx.state = NETPLAY_IN_GAME;
    break;
  case KAILLERA_STATE_ERROR:
    netplay_ctx.state = NETPLAY_ERROR;
    break;
  }
}

int netplay_get_ping(void)
{
  if (!netplay_ctx.initialized || netplay_ctx.client == nullptr)
    return -1;

  return kaillera_client_get_ping(netplay_ctx.client);
}

int netplay_get_local_player(void)
{
  if (!netplay_ctx.initialized)
    return -1;

  if (netplay_ctx.state != NETPLAY_IN_GAME)
    return -1;

  return netplay_ctx.config.local_player;
}

/* Input serialization - 16-bit format for Genesis 6-button controller */
uint16_t netplay_serialize_input(const netplay_input_t *keys)
{
  if (keys == nullptr)
    return 0;

  uint16_t data = 0;

  if (keys->up)
    data |= (1 << 0);
  if (keys->down)
    data |= (1 << 1);
  if (keys->left)
    data |= (1 << 2);
  if (keys->right)
    data |= (1 << 3);
  if (keys->a)
    data |= (1 << 4);
  if (keys->b)
    data |= (1 << 5);
  if (keys->c)
    data |= (1 << 6);
  if (keys->start)
    data |= (1 << 7);
  if (keys->x)
    data |= (1 << 8);
  if (keys->y)
    data |= (1 << 9);
  if (keys->z)
    data |= (1 << 10);
  if (keys->mode)
    data |= (1 << 11);

  return data;
}

void netplay_deserialize_input(uint16_t data, netplay_input_t *keys)
{
  if (keys == nullptr)
    return;

  keys->up = (data & (1 << 0)) ? 1 : 0;
  keys->down = (data & (1 << 1)) ? 1 : 0;
  keys->left = (data & (1 << 2)) ? 1 : 0;
  keys->right = (data & (1 << 3)) ? 1 : 0;
  keys->a = (data & (1 << 4)) ? 1 : 0;
  keys->b = (data & (1 << 5)) ? 1 : 0;
  keys->c = (data & (1 << 6)) ? 1 : 0;
  keys->start = (data & (1 << 7)) ? 1 : 0;
  keys->x = (data & (1 << 8)) ? 1 : 0;
  keys->y = (data & (1 << 9)) ? 1 : 0;
  keys->z = (data & (1 << 10)) ? 1 : 0;
  keys->mode = (data & (1 << 11)) ? 1 : 0;
}

/* Kaillera callback implementations */

static void on_kaillera_connect(void *user_data)
{
  (void)user_data;
  netplay_ctx.state = NETPLAY_LOBBY;
}

static void on_kaillera_disconnect(const char *reason, void *user_data)
{
  (void)user_data;
  if (reason != nullptr) {
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg), "%s",
             reason);
  }
  netplay_ctx.state = NETPLAY_DISCONNECTED;
}

static void on_kaillera_error(const char *error, void *user_data)
{
  (void)user_data;
  if (error != nullptr) {
    snprintf(netplay_ctx.error_msg, sizeof(netplay_ctx.error_msg), "%s", error);
  }
  netplay_ctx.state = NETPLAY_ERROR;

  if (netplay_ctx.callbacks.on_error) {
    netplay_ctx.callbacks.on_error(error, netplay_ctx.callbacks.user_data);
  }
}

static void on_kaillera_chat(const char *username, const char *message,
                             void *user_data)
{
  (void)user_data;
  if (netplay_ctx.callbacks.on_chat) {
    netplay_ctx.callbacks.on_chat(username, message,
                                  netplay_ctx.callbacks.user_data);
  }
}

static void on_kaillera_player_leave(int player_num, const char *username,
                                     void *user_data)
{
  (void)user_data;
  if (netplay_ctx.callbacks.on_player_drop) {
    netplay_ctx.callbacks.on_player_drop(username, player_num,
                                         netplay_ctx.callbacks.user_data);
  }
}

static void on_kaillera_game_created(uint32_t game_id, const char *name,
                                     const char *owner, int players,
                                     int max_players, int status,
                                     void *user_data)
{
  (void)user_data;
  netplay_game_t game{};
  game.id = (int)game_id;
  if (name != nullptr)
    snprintf(game.name, sizeof(game.name), "%s", name);
  if (owner != nullptr)
    snprintf(game.owner, sizeof(game.owner), "%s", owner);
  game.max_players = max_players;
  game.num_players = players;
  game.status = status;

  std::vector<netplay_game_t> copy;
  {
    std::lock_guard<std::mutex> lock(g_games_mu);
    bool replaced = false;
    for (netplay_game_t &existing : g_games) {
      if (existing.id == game.id) {
        existing = game;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      if (g_games.size() >= 32)
        g_games.erase(g_games.begin());
      g_games.push_back(game);
    }
    copy = g_games;
  }
  if (netplay_ctx.callbacks.on_gamelist) {
    netplay_ctx.callbacks.on_gamelist(copy.data(), (int)copy.size(),
                                      netplay_ctx.callbacks.user_data);
  }
}

static void on_kaillera_game_start(int num_players, void *user_data)
{
  (void)user_data;
  (void)num_players;

  /* Update local player number from Kaillera client */
  int player_num = kaillera_client_get_player_number(netplay_ctx.client);
  if (player_num >= 0) {
    netplay_ctx.config.local_player = player_num;
  }

  netplay_ctx.state = NETPLAY_IN_GAME;
}

static void on_kaillera_game_drop(void *user_data)
{
  (void)user_data;
  netplay_ctx.state = NETPLAY_LOBBY;
}
