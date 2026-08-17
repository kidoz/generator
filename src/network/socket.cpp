/* socket.cpp - Cross-platform UDP socket implementation (POSIX) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "socket.h"

/* Socket structure */
struct netplay_socket {
  int fd;
  struct sockaddr_in remote_addr;
  bool has_remote;
  uint16_t local_port;
};

/* Thread-local error buffer */
static thread_local char socket_error_buf[256];

int socket_subsystem_init(void)
{
  /* POSIX doesn't require initialization */
  return 0;
}

void socket_subsystem_shutdown(void)
{
  /* POSIX doesn't require cleanup */
}

netplay_socket_t *socket_create(void)
{
  netplay_socket_t *sock =
      static_cast<netplay_socket_t *>(calloc(1, sizeof(netplay_socket_t)));
  if (sock == nullptr) {
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Failed to allocate socket structure");
    return nullptr;
  }

  sock->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock->fd < 0) {
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Failed to create socket: %s", strerror(errno));
    free(sock);
    return nullptr;
  }

  /* Enable address reuse */
  int opt = 1;
  setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  return sock;
}

void socket_destroy(netplay_socket_t *sock)
{
  if (sock == nullptr)
    return;

  if (sock->fd >= 0) {
    close(sock->fd);
  }
  free(sock);
}

int socket_bind(netplay_socket_t *sock, uint16_t port)
{
  if (sock == nullptr || sock->fd < 0)
    return -1;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Failed to bind to port %d: %s", port, strerror(errno));
    return -1;
  }

  /* Get the actual bound port (in case port was 0) */
  socklen_t addrlen = sizeof(addr);
  if (getsockname(sock->fd, (struct sockaddr *)&addr, &addrlen) == 0) {
    sock->local_port = ntohs(addr.sin_port);
  }

  return 0;
}

int socket_connect(netplay_socket_t *sock, const char *host, uint16_t port)
{
  if (sock == nullptr || sock->fd < 0 || host == nullptr)
    return -1;

  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%d", port);

  int err = getaddrinfo(host, port_str, &hints, &res);
  if (err != 0) {
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Failed to resolve host '%s': %s", host, gai_strerror(err));
    return -1;
  }

  /* Copy the first result */
  memcpy(&sock->remote_addr, res->ai_addr, sizeof(sock->remote_addr));
  sock->has_remote = true;

  freeaddrinfo(res);
  return 0;
}

int socket_send(netplay_socket_t *sock, const void *data, size_t len)
{
  if (sock == nullptr || sock->fd < 0 || !sock->has_remote)
    return -1;

  ssize_t sent = sendto(sock->fd, data, len, 0,
                        (struct sockaddr *)&sock->remote_addr,
                        sizeof(sock->remote_addr));
  if (sent < 0) {
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Send failed: %s", strerror(errno));
    return -1;
  }

  return (int)sent;
}

int socket_sendto(netplay_socket_t *sock, const void *data, size_t len,
                  const char *host, uint16_t port)
{
  if (sock == nullptr || sock->fd < 0 || host == nullptr)
    return -1;

  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%d", port);

  int err = getaddrinfo(host, port_str, &hints, &res);
  if (err != 0) {
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Failed to resolve host '%s': %s", host, gai_strerror(err));
    return -1;
  }

  ssize_t sent = sendto(sock->fd, data, len, 0, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);

  if (sent < 0) {
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Send failed: %s", strerror(errno));
    return -1;
  }

  return (int)sent;
}

int socket_recv(netplay_socket_t *sock, void *buf, size_t maxlen, int timeout_ms)
{
  if (sock == nullptr || sock->fd < 0)
    return -1;

  /* Use select for timeout */
  if (timeout_ms >= 0) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock->fd, &readfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(sock->fd + 1, &readfds, nullptr, nullptr, &tv);
    if (ret < 0) {
      snprintf(socket_error_buf, sizeof(socket_error_buf),
               "Select failed: %s", strerror(errno));
      return -1;
    }
    if (ret == 0) {
      /* Timeout */
      return 0;
    }
  }

  ssize_t received = recvfrom(sock->fd, buf, maxlen, 0, nullptr, nullptr);
  if (received < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Receive failed: %s", strerror(errno));
    return -1;
  }

  return (int)received;
}

int socket_recvfrom(netplay_socket_t *sock, void *buf, size_t maxlen,
                    char *src_host, size_t src_host_len, uint16_t *src_port,
                    int timeout_ms)
{
  if (sock == nullptr || sock->fd < 0)
    return -1;

  /* Use select for timeout */
  if (timeout_ms >= 0) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock->fd, &readfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(sock->fd + 1, &readfds, nullptr, nullptr, &tv);
    if (ret < 0) {
      snprintf(socket_error_buf, sizeof(socket_error_buf),
               "Select failed: %s", strerror(errno));
      return -1;
    }
    if (ret == 0) {
      /* Timeout */
      return 0;
    }
  }

  struct sockaddr_in from_addr;
  socklen_t addrlen = sizeof(from_addr);

  ssize_t received = recvfrom(sock->fd, buf, maxlen, 0,
                              (struct sockaddr *)&from_addr, &addrlen);
  if (received < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Receive failed: %s", strerror(errno));
    return -1;
  }

  /* Extract source address if requested */
  if (src_host != nullptr && src_host_len > 0) {
    inet_ntop(AF_INET, &from_addr.sin_addr, src_host, (socklen_t)src_host_len);
  }
  if (src_port != nullptr) {
    *src_port = ntohs(from_addr.sin_port);
  }

  return (int)received;
}

int socket_set_nonblocking(netplay_socket_t *sock, bool nonblocking)
{
  if (sock == nullptr || sock->fd < 0)
    return -1;

  int flags = fcntl(sock->fd, F_GETFL, 0);
  if (flags < 0) {
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Failed to get socket flags: %s", strerror(errno));
    return -1;
  }

  if (nonblocking) {
    flags |= O_NONBLOCK;
  } else {
    flags &= ~O_NONBLOCK;
  }

  if (fcntl(sock->fd, F_SETFL, flags) < 0) {
    snprintf(socket_error_buf, sizeof(socket_error_buf),
             "Failed to set socket flags: %s", strerror(errno));
    return -1;
  }

  return 0;
}

uint16_t socket_get_local_port(netplay_socket_t *sock)
{
  if (sock == nullptr)
    return 0;
  return sock->local_port;
}

const char *socket_get_error(void)
{
  return socket_error_buf;
}
