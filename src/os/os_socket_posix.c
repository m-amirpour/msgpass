#ifndef _WIN32

#define _DEFAULT_SOURCE

#include "os/os_socket.h"
#include "mp_log.h"

#include <netdb.h>
#include <netinet/tcp.h>


int os_net_init(void)    { return 0; }
void os_net_cleanup(void) {}

int os_socket_last_error(void)          { return errno; }
const char *os_socket_strerror(int e)   { return strerror(e); }
bool os_socket_would_block(int e)       { return e == EAGAIN || e == EWOULDBLOCK; }

static int apply_common_opts(mp_socket_t fd)
{
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    return os_set_cloexec(fd);
}

mp_socket_t os_listen_tcp(int port)
{
    mp_socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("socket: %s", strerror(errno));
        return MP_INVALID_SOCKET;
    }
    apply_common_opts(fd);

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind(port=%d): %s", port, strerror(errno));
        close(fd);
        return MP_INVALID_SOCKET;
    }
    if (listen(fd, LISTEN_BACKLOG) < 0) {
        LOG_ERROR("listen: %s", strerror(errno));
        close(fd);
        return MP_INVALID_SOCKET;
    }

    LOG_INFO("TCP listener on port %d (fd=%d)", port, fd);
    return fd;
}

mp_socket_t os_listen_unix(const char *path)
{
    mp_socket_t fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("socket(AF_UNIX): %s", strerror(errno));
        return MP_INVALID_SOCKET;
    }
    apply_common_opts(fd);
    unlink(path);

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        LOG_ERROR("UNIX path too long: %s", path);
        close(fd);
        return MP_INVALID_SOCKET;
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind(%s): %s", path, strerror(errno));
        close(fd);
        return MP_INVALID_SOCKET;
    }
    if (listen(fd, LISTEN_BACKLOG) < 0) {
        LOG_ERROR("listen: %s", strerror(errno));
        close(fd);
        unlink(path);
        return MP_INVALID_SOCKET;
    }

    LOG_INFO("UNIX listener at %s (fd=%d)", path, fd);
    return fd;
}

mp_socket_t os_accept(mp_socket_t server_fd)
{
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    mp_socket_t fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
    if (fd < 0) return MP_INVALID_SOCKET;
    os_set_cloexec(fd);
    return fd;
}

mp_socket_t os_connect_tcp(const char *host, int port)
{
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints = {0};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, portstr, &hints, &res);
    if (gai != 0) {
        LOG_ERROR("getaddrinfo(%s:%d): %s", host, port, gai_strerror(gai));
        return MP_INVALID_SOCKET;
    }

    mp_socket_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return MP_INVALID_SOCKET;
    }
    os_set_cloexec(fd);

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        LOG_ERROR("connect(%s:%d): %s", host, port, strerror(errno));
        close(fd);
        freeaddrinfo(res);
        return MP_INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return fd;
}

mp_socket_t os_connect_unix(const char *path)
{
    mp_socket_t fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("socket(AF_UNIX): %s", strerror(errno));
        return MP_INVALID_SOCKET;
    }
    os_set_cloexec(fd);

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("connect(%s): %s", path, strerror(errno));
        close(fd);
        return MP_INVALID_SOCKET;
    }
    return fd;
}

int os_set_nonblocking(mp_socket_t fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int os_set_cloexec(mp_socket_t fd)
{
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void os_close_socket(mp_socket_t fd)
{
    if (fd != MP_INVALID_SOCKET) close(fd);
}

void os_unlink_socket(const char *path)
{
    if (path && path[0]) {
        unlink(path);
        LOG_INFO("removed socket file: %s", path);
    }
}

ssize_t os_recv_nonblock(mp_socket_t fd, void *buf, size_t len)
{
    for (;;) {
        ssize_t n = recv(fd, buf, len, MSG_DONTWAIT);
        if (n > 0)  return n;
        if (n == 0) return 0;
        if (errno == EINTR)                      continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
        return -1;
    }
}

ssize_t os_send_all(mp_socket_t fd, const void *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, (const char *)buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

ssize_t os_recv_all(mp_socket_t fd, void *buf, size_t len)
{
    size_t recvd = 0;
    while (recvd < len) {
        ssize_t n = recv(fd, (char *)buf + recvd, len - recvd, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return (ssize_t)recvd;
        recvd += (size_t)n;
    }
    return (ssize_t)recvd;
}

#endif /* !_WIN32 */