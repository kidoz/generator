/* kaillera_client.h - Kaillera client implementation */

#ifndef KAILLERA_CLIENT_H
#define KAILLERA_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "kaillera_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Client state */
typedef enum {
  KAILLERA_STATE_DISCONNECTED = 0,
  KAILLERA_STATE_CONNECTING,
  KAILLERA_STATE_CONNECTED,
  KAILLERA_STATE_IN_GAME,
  KAILLERA_STATE_PLAYING,
  KAILLERA_STATE_ERROR
} kaillera_state_t;

/* Kaillera client callbacks */
typedef struct {
  /* Connection events */
  void (*on_connect)(void *user_data);
  void (*on_disconnect)(const char *reason, void *user_data);
  void (*on_error)(const char *error, void *user_data);

  /* Server messages */
  void (*on_user_join)(const char *username, uint16_t ping,
                       uint8_t conn_type, void *user_data);
  void (*on_user_quit)(const char *username, void *user_data);
  void (*on_chat)(const char *username, const char *message, void *user_data);

  /* Game events */
  void (*on_game_created)(uint32_t game_id, const char *name,
                          const char *owner, void *user_data);
  void (*on_game_closed)(uint32_t game_id, void *user_data);
  void (*on_player_join)(int player_num, const char *username, void *user_data);
  void (*on_player_leave)(int player_num, const char *username, void *user_data);
  void (*on_game_start)(int num_players, void *user_data);
  void (*on_game_drop)(void *user_data);

  /* Input sync */
  void (*on_game_data)(const kaillera_game_data_t *data, void *user_data);

  void *user_data;
} kaillera_callbacks_t;

/* Forward declaration */
typedef struct kaillera_client kaillera_client_t;

/*
 * Create a new Kaillera client.
 * Returns nullptr on failure.
 */
kaillera_client_t *kaillera_client_create(void);

/*
 * Destroy a Kaillera client.
 */
void kaillera_client_destroy(kaillera_client_t *client);

/*
 * Set callbacks for client events.
 */
void kaillera_client_set_callbacks(kaillera_client_t *client,
                                   const kaillera_callbacks_t *callbacks);

/*
 * Connect to a Kaillera server.
 * Returns 0 on success (connection initiated), -1 on failure.
 */
int kaillera_client_connect(kaillera_client_t *client,
                            const char *host, uint16_t port,
                            const char *username, const char *emulator,
                            uint8_t connection_type);

/*
 * Disconnect from the server.
 */
void kaillera_client_disconnect(kaillera_client_t *client);

/*
 * Get the current client state.
 */
kaillera_state_t kaillera_client_get_state(kaillera_client_t *client);

/*
 * Get the last error message.
 */
const char *kaillera_client_get_error(kaillera_client_t *client);

/*
 * Poll for incoming packets.
 * Should be called regularly (non-blocking).
 */
void kaillera_client_poll(kaillera_client_t *client);

/*
 * Send a chat message.
 * Returns 0 on success, -1 on failure.
 */
int kaillera_client_chat(kaillera_client_t *client, const char *message);

/*
 * Create a new game room.
 * Returns 0 on success, -1 on failure.
 * Game ID is received via on_game_created callback.
 */
int kaillera_client_create_game(kaillera_client_t *client, const char *game_name);

/*
 * Join an existing game room.
 * Returns 0 on success, -1 on failure.
 */
int kaillera_client_join_game(kaillera_client_t *client, uint32_t game_id);

/*
 * Leave the current game room.
 */
void kaillera_client_leave_game(kaillera_client_t *client);

/*
 * Start the game (host only).
 * Returns 0 on success, -1 on failure.
 */
int kaillera_client_start_game(kaillera_client_t *client);

/*
 * Signal ready to play.
 */
int kaillera_client_ready(kaillera_client_t *client);

/*
 * Send game data (player input).
 * This is the core input sync function - it sends local input and
 * blocks until it receives all players' inputs for the frame.
 *
 * Returns the total number of bytes of input data received (all players),
 * or -1 on error/disconnect.
 */
int kaillera_client_modify_play_values(kaillera_client_t *client,
                                       void *input, int size);

/*
 * End the current game.
 */
void kaillera_client_drop_game(kaillera_client_t *client);

/*
 * Get the current ping to the server in milliseconds.
 * Returns -1 if not connected.
 */
int kaillera_client_get_ping(kaillera_client_t *client);

/*
 * Get the local player number (0-based).
 * Returns -1 if not in a game.
 */
int kaillera_client_get_player_number(kaillera_client_t *client);

/*
 * Get the number of players in the current game.
 * Returns 0 if not in a game.
 */
int kaillera_client_get_num_players(kaillera_client_t *client);

#ifdef __cplusplus
}
#endif

#endif /* KAILLERA_CLIENT_H */
