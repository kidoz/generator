/* netplay.h - High-level netplay API for Kaillera integration */

#ifndef NETPLAY_H
#define NETPLAY_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Button state exchanged over the wire.
 *
 * Netplay owns this struct rather than borrowing the machine's: it does not
 * reach into the emulator. The caller reads its own local pad, hands it to
 * netplay_sync_frame(), and drives the core with the remote pads that come
 * back through EmulatorCore::set_input.
 */
typedef struct {
  uint8_t up, down, left, right;
  uint8_t a, b, c, start;
  uint8_t x, y, z, mode;
} netplay_input_t;

/* Netplay connection state */
typedef enum {
  NETPLAY_DISCONNECTED = 0,
  NETPLAY_CONNECTING,
  NETPLAY_LOBBY,
  NETPLAY_IN_GAME,
  NETPLAY_ERROR
} netplay_state_t;

/* Connection type (affects input delay) */
typedef enum {
  NETPLAY_CONN_LAN = 1,       /* LAN - minimal delay */
  NETPLAY_CONN_EXCELLENT = 2, /* Excellent connection */
  NETPLAY_CONN_GOOD = 3,      /* Good connection */
  NETPLAY_CONN_AVERAGE = 4,   /* Average connection */
  NETPLAY_CONN_LOW = 5,       /* Low bandwidth */
  NETPLAY_CONN_BAD = 6        /* Bad connection - maximum delay */
} netplay_conn_type_t;

/* Netplay configuration */
typedef struct {
  char server[256];              /* Server hostname or IP */
  uint16_t port;                 /* Server port (default 27888) */
  char username[32];             /* Player username */
  netplay_conn_type_t conn_type; /* Connection quality */
  int local_player;              /* Local player index (0 or 1) */
} netplay_config_t;

/* Game room information */
typedef struct {
  int id;          /* Game ID */
  char name[128];  /* Game name (ROM name) */
  char owner[32];  /* Room creator username */
  int num_players; /* Current player count */
  int max_players; /* Maximum players (usually 2) */
  int status;      /* 0=waiting, 1=playing */
} netplay_game_t;

/* Chat callback - called when chat message received */
typedef void (*netplay_chat_callback_t)(const char *nick, const char *text,
                                        void *user_data);

/* Player drop callback - called when a player disconnects */
typedef void (*netplay_drop_callback_t)(const char *nick, int player_num,
                                        void *user_data);

/* Game list callback - called when game list is updated */
typedef void (*netplay_gamelist_callback_t)(const netplay_game_t *games,
                                            int count, void *user_data);

/* Error callback - called on connection errors */
typedef void (*netplay_error_callback_t)(const char *error_msg,
                                         void *user_data);

/* Callback structure */
typedef struct {
  netplay_chat_callback_t on_chat;
  netplay_drop_callback_t on_player_drop;
  netplay_gamelist_callback_t on_gamelist;
  netplay_error_callback_t on_error;
  void *user_data;
} netplay_callbacks_t;

/*
 * Initialize the netplay subsystem.
 * Must be called before any other netplay functions.
 * Returns 0 on success, -1 on failure.
 */
int netplay_init(void);

/*
 * Shutdown the netplay subsystem.
 * Disconnects if connected and frees all resources.
 */
void netplay_shutdown(void);

/*
 * Set callbacks for netplay events.
 * Can be called at any time to update callbacks.
 */
void netplay_set_callbacks(const netplay_callbacks_t *callbacks);

/*
 * Connect to a Kaillera server.
 * This is an asynchronous operation. Use netplay_get_state() to check status.
 * Returns 0 on success (connection initiated), -1 on failure.
 */
int netplay_connect(const netplay_config_t *config);

/*
 * Disconnect from the current server.
 * Safe to call even if not connected.
 */
void netplay_disconnect(void);

/*
 * Get the current netplay state.
 */
netplay_state_t netplay_get_state(void);

/*
 * Get the last error message.
 * Returns nullptr if no error.
 */
const char *netplay_get_error(void);

/*
 * Create a new game room on the server.
 * Must be in NETPLAY_LOBBY state.
 * Returns game ID on success, -1 on failure.
 */
int netplay_create_game(const char *rom_name);

/*
 * Join an existing game room.
 * Must be in NETPLAY_LOBBY state.
 * Returns 0 on success, -1 on failure.
 */
int netplay_join_game(int game_id);

/*
 * Start the game (host only).
 * Must be in NETPLAY_LOBBY state and be the game creator.
 * Returns 0 on success, -1 on failure.
 */
int netplay_start_game(void);

/*
 * End the current game and return to lobby.
 */
void netplay_end_game(void);

/*
 * Send a chat message.
 * Returns 0 on success, -1 on failure.
 */
int netplay_chat_send(const char *text);

/*
 * Request the game list from the server.
 * Results delivered via on_gamelist callback.
 * Returns 0 on success, -1 on failure.
 */
int netplay_request_gamelist(void);

/*
 * Synchronize frame with remote player(s).
 * This function BLOCKS until all players' inputs are received for the frame.
 * Must be called once per frame when in NETPLAY_IN_GAME state.
 *
 * The function:
 * 1. Serializes the caller's local player input
 * 2. Sends to server and waits for all player inputs
 * 3. Deserializes every player's input into `players`
 *
 * `players` receives one entry per player, indexed by player number, and
 * must have room for `max_players`; the local player's own entry is echoed
 * back unchanged. Returns 0 on success, -1 on connection lost or error.
 */
int netplay_sync_frame(const netplay_input_t *local, netplay_input_t *players,
                       int max_players);

/*
 * Poll for incoming packets and process callbacks.
 * Should be called regularly when connected (e.g., in main loop).
 * Non-blocking.
 */
void netplay_poll(void);

/*
 * Get the current ping to the server in milliseconds.
 * Returns -1 if not connected.
 */
int netplay_get_ping(void);

/*
 * Get local player index (0 or 1).
 * Returns -1 if not in a game.
 */
int netplay_get_local_player(void);

/*
 * Input serialization/deserialization utilities.
 * These are used internally but exposed for testing.
 */

/*
 * Serialize controller input to a 16-bit value.
 * Format: up|down|left|right|a|b|c|start|x|y|z|mode (bits 0-11)
 */
uint16_t netplay_serialize_input(const netplay_input_t *keys);

/*
 * Deserialize a 16-bit value to controller input.
 */
void netplay_deserialize_input(uint16_t data, netplay_input_t *keys);

#endif /* NETPLAY_H */
