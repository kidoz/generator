/* socket.h - Cross-platform UDP socket abstraction */

#ifndef NETPLAY_SOCKET_H
#define NETPLAY_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Opaque socket type */
typedef struct netplay_socket netplay_socket_t;

/*
 * Initialize the socket subsystem.
 * Must be called before creating sockets.
 * Returns 0 on success, -1 on failure.
 */
int socket_subsystem_init(void);

/*
 * Shutdown the socket subsystem.
 */
void socket_subsystem_shutdown(void);

/*
 * Create a new UDP socket.
 * Returns nullptr on failure.
 */
netplay_socket_t *socket_create(void);

/*
 * Destroy a socket and free resources.
 */
void socket_destroy(netplay_socket_t *sock);

/*
 * Bind the socket to a local port.
 * Use port 0 for automatic port assignment.
 * Returns 0 on success, -1 on failure.
 */
int socket_bind(netplay_socket_t *sock, uint16_t port);

/*
 * Set the remote address for the socket.
 * Subsequent send() calls will go to this address.
 * Returns 0 on success, -1 on failure (e.g., DNS resolution failed).
 */
int socket_connect(netplay_socket_t *sock, const char *host, uint16_t port);

/*
 * Send data to the connected remote address.
 * Returns number of bytes sent, or -1 on error.
 */
int socket_send(netplay_socket_t *sock, const void *data, size_t len);

/*
 * Send data to a specific address (not the connected one).
 * Returns number of bytes sent, or -1 on error.
 */
int socket_sendto(netplay_socket_t *sock, const void *data, size_t len,
                  const char *host, uint16_t port);

/*
 * Receive data from the socket.
 * Blocks up to timeout_ms milliseconds (0 = non-blocking, -1 = infinite).
 * Returns number of bytes received, 0 on timeout, or -1 on error.
 */
int socket_recv(netplay_socket_t *sock, void *buf, size_t maxlen, int timeout_ms);

/*
 * Receive data and get the sender's address.
 * Blocks up to timeout_ms milliseconds (0 = non-blocking, -1 = infinite).
 * If src_host is not nullptr, it must point to a buffer of at least 64 bytes.
 * Returns number of bytes received, 0 on timeout, or -1 on error.
 */
int socket_recvfrom(netplay_socket_t *sock, void *buf, size_t maxlen,
                    char *src_host, size_t src_host_len, uint16_t *src_port,
                    int timeout_ms);

/*
 * Set the socket to non-blocking mode.
 * Returns 0 on success, -1 on failure.
 */
int socket_set_nonblocking(netplay_socket_t *sock, bool nonblocking);

/*
 * Get the local port the socket is bound to.
 * Returns 0 if not bound.
 */
uint16_t socket_get_local_port(netplay_socket_t *sock);

/*
 * Get the last socket error message.
 */
const char *socket_get_error(void);

#endif /* NETPLAY_SOCKET_H */
