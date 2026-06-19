/*
 * Server entry point. Wires together options, signals, and the
 * chosen server engine.
 */

#include "mp_common.h"
#include "mp_log.h"
#include "os/os_socket.h"

void mp_server_st_run(mp_socket_t listen_fd, mp_shutdown_flag_t *shutdown_flag);
void mp_server_mt_run(mp_socket_t listen_fd, mp_shutdown_flag_t *shutdown_flag);

static mp_shutdown_flag_t g_shutdown = 0;
static char               g_unix_path[256];

#ifdef _WIN32

static BOOL WINAPI ctrl_handler(DWORD ctrl)
{
    if (ctrl == CTRL_C_EVENT || ctrl == CTRL_BREAK_EVENT ||
        ctrl == CTRL_CLOSE_EVENT) {
        MP_FLAG_SET(g_shutdown);
        return TRUE;
    }
    return FALSE;
}

static void setup_signals(void)
{
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
}

#else

static void sig_handler(int sig)
{
    (void)sig;
    MP_FLAG_SET(g_shutdown);
}

static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART so select() gets EINTR */
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct sigaction sa_ign;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);
    sigaction(SIGPIPE, &sa_ign, NULL);
}

#endif

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -s <path>   UNIX domain socket (default: /tmp/msgpass.sock)\n"
        "  -p <port>   TCP port\n"
        "  -t          multi-threaded mode\n"
        "  -v          verbose logging\n"
        "  -h          help\n",
        name);
}

int main(int argc, char *argv[])
{
    if (os_net_init() != 0) {
        fprintf(stderr, "Network init failed\n");
        return 1;
    }

    int  use_tcp  = 0;
    int  use_mt   = 0;
    int  tcp_port = 9000;
    char sock_path[256] = "/tmp/msgpass.sock";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(sock_path, argv[++i], sizeof(sock_path) - 1);
            sock_path[sizeof(sock_path) - 1] = '\0';
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            tcp_port = atoi(argv[++i]);
            use_tcp  = 1;
        } else if (strcmp(argv[i], "-t") == 0) {
            use_mt = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            mp_log_set_level(MP_LOG_DEBUG);
        } else if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            os_net_cleanup();
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            os_net_cleanup();
            return 1;
        }
    }

    setup_signals();

    mp_socket_t listen_fd;
    if (use_tcp) {
        listen_fd = os_listen_tcp(tcp_port);
    } else {
        strncpy(g_unix_path, sock_path, sizeof(g_unix_path) - 1);
        g_unix_path[sizeof(g_unix_path) - 1] = '\0';
        listen_fd = os_listen_unix(sock_path);
    }

    if (listen_fd == MP_INVALID_SOCKET) {
        LOG_ERROR("failed to create listening socket");
        os_net_cleanup();
        return 1;
    }

    /* Single-threaded mode needs non-blocking listen so accept() in the
     * event loop doesn't block. MT mode uses blocking accept(). */
    if (!use_mt) {
        if (os_set_nonblocking(listen_fd) != 0) {
            LOG_ERROR("failed to set non-blocking on listening socket");
            os_close_socket(listen_fd);
            if (!use_tcp) os_unlink_socket(g_unix_path);
            os_net_cleanup();
            return 1;
        }
    }

    LOG_INFO("server ready: transport=%s mode=%s",
             use_tcp ? "TCP" : "UNIX",
             use_mt  ? "multi-threaded" : "single-threaded");

    if (use_mt)
        mp_server_mt_run(listen_fd, &g_shutdown);
    else
        mp_server_st_run(listen_fd, &g_shutdown);

    os_close_socket(listen_fd);
    if (!use_tcp && g_unix_path[0])
        os_unlink_socket(g_unix_path);

    LOG_INFO("server shut down cleanly");
    os_net_cleanup();
    return 0;
}