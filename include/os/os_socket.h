#ifndef OS_SOCKET_H
#define OS_SOCKET_H

/*
 * Socket abstraction layer. Every function here has identical
 * semantics on both platforms — callers never need an #ifdef.
 */

#include "mp_common.h"

/* Lifecycle */
int  os_net_init(void);
void os_net_cleanup(void);

/* Socket error helpers */
int         os_socket_last_error(void);
const char *os_socket_strerror(int err);
bool        os_socket_would_block(int err);

/* Server-side */
mp_socket_t os_listen_tcp(int port);
mp_socket_t os_listen_unix(const char *path);

/* Accept a connection. Returns MP_INVALID_SOCKET on EAGAIN or error. */
mp_socket_t os_accept(mp_socket_t server_fd);

/* Client-side */
mp_socket_t os_connect_tcp(const char *host, int port);
mp_socket_t os_connect_unix(const char *path);

/* Options */
int os_set_nonblocking(mp_socket_t fd);
int os_set_cloexec(mp_socket_t fd);

/* Cleanup */
void os_close_socket(mp_socket_t fd);
void os_unlink_socket(const char *path);

/*
 * Non-blocking receive. Returns:
 *   > 0  bytes read
 *     0  connection closed
 *    -1  error
 *    -2  would block (EAGAIN/EWOULDBLOCK)
 */
ssize_t os_recv_nonblock(mp_socket_t fd, void *buf, size_t len);

/*
 * Reliable send — loops until all bytes are written or an error occurs.
 * Returns the number of bytes sent; anything less than len is an error.
 */
ssize_t os_send_all(mp_socket_t fd, const void *buf, size_t len);

/*
 * Reliable recv — blocks until exactly len bytes are received or EOF/error.
 * Returns bytes read; short count means EOF or error.
 */
ssize_t os_recv_all(mp_socket_t fd, void *buf, size_t len);

#endif /* OS_SOCKET_H */