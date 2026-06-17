#ifdef _WIN32

#include "os/os_socket.h"
#include "mp_log.h"

#define LISTEN_BACKLOG 128

static LONG g_wsa_init = 0;

int os_net_init(void)
{
    if (InterlockedCompareExchange(&g_wsa_init, 1, 0) != 0)
        return 0;
    WSADATA wd;
    int rc = WSAStartup(MAKEWORD(2, 2), &wd);
    if (rc != 0) {
        LOG_ERROR("WSAStartup failed: %d", rc);
        InterlockedExchange(&g_wsa_init, 0);
        return -1;
    }
    return 0;
}

void os_net_cleanup(void)
{
    if (InterlockedCompareExchange(&g_wsa_init, 0, 1) == 1)
        WSACleanup();
}

int os_socket_last_error(void)
{
    return WSAGetLastError();
}

const char *os_socket_strerror(int e)
{
    static _Thread_local char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, (DWORD)e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   buf, sizeof(buf), NULL);
    char *p = buf + strlen(buf);
    while (p > buf && (p[-1] == '\n' || p[-1] == '\r')) *--p = '\0';
    return buf;
}

bool os_socket_would_block(int e)
{
    return e == WSAEWOULDBLOCK;
}

static int apply_common_opts(mp_socket_t fd)
{
    BOOL opt = TRUE;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    /* Make the socket non-inheritable by default */
    SetHandleInformation((HANDLE)fd, HANDLE_FLAG_INHERIT, 0);
    return 0;
}

mp_socket_t os_listen_tcp(int port)
{
    mp_socket_t fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return MP_INVALID_SOCKET;
    apply_common_opts(fd);

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((u_short)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("bind(port=%d): %s", port, os_socket_strerror(WSAGetLastError()));
        closesocket(fd);
        return MP_INVALID_SOCKET;
    }
    if (listen(fd, LISTEN_BACKLOG) == SOCKET_ERROR) {
        closesocket(fd);
        return MP_INVALID_SOCKET;
    }
    LOG_INFO("TCP listener on port %d", port);
    return fd;
}

mp_socket_t os_listen_unix(const char *path)
{
    mp_socket_t fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) {
        LOG_ERROR("AF_UNIX socket failed (requires Windows 10 1803+): %s",
                  os_socket_strerror(WSAGetLastError()));
        return MP_INVALID_SOCKET;
    }
    apply_common_opts(fd);
    DeleteFileA(path);

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(fd);
        return MP_INVALID_SOCKET;
    }
    if (listen(fd, LISTEN_BACKLOG) == SOCKET_ERROR) {
        closesocket(fd);
        return MP_INVALID_SOCKET;
    }
    LOG_INFO("UNIX listener at %s", path);
    return fd;
}

mp_socket_t os_accept(mp_socket_t server_fd)
{
    struct sockaddr_storage addr;
    int addrlen = sizeof(addr);
    mp_socket_t fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
    if (fd == INVALID_SOCKET) return MP_INVALID_SOCKET;
    SetHandleInformation((HANDLE)fd, HANDLE_FLAG_INHERIT, 0);
    return fd;
}

mp_socket_t os_connect_tcp(const char *host, int port)
{
    mp_socket_t fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return MP_INVALID_SOCKET;
    SetHandleInformation((HANDLE)fd, HANDLE_FLAG_INHERIT, 0);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((u_short)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(fd);
        return MP_INVALID_SOCKET;
    }
    return fd;
}

mp_socket_t os_connect_unix(const char *path)
{
    mp_socket_t fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) return MP_INVALID_SOCKET;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(fd);
        return MP_INVALID_SOCKET;
    }
    return fd;
}

int os_set_nonblocking(mp_socket_t fd)
{
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
}

int os_set_cloexec(mp_socket_t fd)
{
    return SetHandleInformation((HANDLE)fd, HANDLE_FLAG_INHERIT, 0) ? 0 : -1;
}

void os_close_socket(mp_socket_t fd)
{
    if (fd != INVALID_SOCKET) closesocket(fd);
}

void os_unlink_socket(const char *path)
{
    if (path && path[0]) DeleteFileA(path);
}

ssize_t os_recv_nonblock(mp_socket_t fd, void *buf, size_t len)
{
    int n = recv(fd, (char *)buf, (int)len, 0);
    if (n > 0)  return n;
    if (n == 0) return 0;
    int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK) return -2;
    return -1;
}

ssize_t os_send_all(mp_socket_t fd, const void *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = send(fd, (const char *)buf + sent, (int)(len - sent), 0);
        if (n == SOCKET_ERROR) return -1;
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

ssize_t os_recv_all(mp_socket_t fd, void *buf, size_t len)
{
    size_t recvd = 0;
    while (recvd < len) {
        int n = recv(fd, (char *)buf + recvd, (int)(len - recvd), 0);
        if (n == SOCKET_ERROR) return -1;
        if (n == 0)            return (ssize_t)recvd;
        recvd += (size_t)n;
    }
    return (ssize_t)recvd;
}

#endif /* _WIN32 */