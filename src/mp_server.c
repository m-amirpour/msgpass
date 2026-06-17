/*
 * Server entry point. Parses options, sets up the listening socket,
 * installs signal handlers, and hands off to the appropriate engine.
 */

#include "mp_common.h"
#include "mp_log.h"
#include "os/os_socket.h"

/* Forward declarations from the two server engines. */
void mp_server_st_run(mp_socket_t listen_fd, mp_shutdown_flag_t *shutdown_flag);
void mp_server_mt_run(mp_socket_t listen_fd, mp_shutdown_flag_t *shutdown_flag);

/* ------------------------------------------------------------------ */

static mp_shutdown_flag_t g_shutdown = 0;
static char               g_unix_path[256];

/* ------------------------------------------------------------------ */

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
    struct sigaction sa = {0};
    sa.sa_handler = sig_handler;
    /* We deliberately do not set SA_RESTART so that select() returns EINTR
     * and the loop can check the shutdown flag without waiting. */
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct sigaction sa_ign = {0};
    sa_ign.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa_ign, NULL);
}

#endif

/* ------------------------------------------------------------------ */

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -s <path>   listen on UNIX domain socket (default: /tmp/msgpass.sock)\n"
        "  -p <port>   listen on TCP port\n"
        "  -t          multi-threaded mode (default: single-threaded)\n"
        "  -v          verbose / debug logging\n"
        "  -h          show this help\n",
        name);
}

int main(int argc, char *argv[])
{
    if (os_net_init() != 0) return 1;

    int  use_tcp    = 0;
    int  use_mt     = 0;
    int  tcp_port   = 9000;
    char sock_path[256] = "/tmp/msgpass.sock";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i+1 < argc) {
            strncpy(sock_path, argv[++i], sizeof(sock_path) - 1);
        } else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) {
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
        snprintf(g_unix_path, sizeof(g_unix_path), "%s", sock_path);
        listen_fd = os_listen_unix(sock_path);
    }

    if (listen_fd == MP_INVALID_SOCKET) {
        LOG_ERROR("failed to create listening socket");
        os_net_cleanup();
        return 1;
    }

    /*
     * Single-threaded mode needs a non-blocking listening socket
     * so accept() doesn't block inside the select() loop. In MT mode
     * we block in accept() on purpose.
     */
    if (!use_mt) {
        os_set_nonblocking(listen_fd);
    }

    LOG_INFO("server ready: transport=%s mode=%s",
             use_tcp ? "TCP" : "UNIX",
             use_mt  ? "multi-threaded" : "single-threaded");

    if (use_mt) {
        mp_server_mt_run(listen_fd, &g_shutdown);
    } else {
        mp_server_st_run(listen_fd, &g_shutdown);
    }

    os_close_socket(listen_fd);
    if (!use_tcp && g_unix_path[0])
        os_unlink_socket(g_unix_path);

    LOG_INFO("server shut down cleanly");
    os_net_cleanup();
    return 0;
}