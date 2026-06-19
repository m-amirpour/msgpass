/*
 * Multi-threaded server. One thread per client connection.
 * Blocking I/O, no shared queue.
 */

#include "mp_common.h"
#include "mp_protocol.h"
#include "mp_dispatcher.h"
#include "mp_log.h"
#include "mp_portable.h"
#include "os/os_socket.h"
#include "os/os_thread.h"

typedef struct {
    mp_socket_t fd;
} mt_client_arg_t;

static void mt_client_worker(void *arg)
{
    mt_client_arg_t *ca = (mt_client_arg_t *)arg;
    mp_socket_t      fd = ca->fd;
    free(ca);

    LOG_INFO("worker started for fd=%d", (int)fd);

    for (;;) {
        uint8_t hdr_buf[MP_REQ_HDR_LEN];
        ssize_t nr = os_recv_all(fd, hdr_buf, sizeof(hdr_buf));

        if (nr == 0) {
            LOG_INFO("fd=%d closed by peer", (int)fd);
            break;
        }
        if (nr != (ssize_t)sizeof(hdr_buf)) {
            LOG_WARN("fd=%d short header (" MP_FSIZED " bytes)", (int)fd, MP_CAST_SSIZE(nr));
            break;
        }

        uint16_t wl, wt, wa;
        memcpy(&wl, hdr_buf + 0, 2);
        memcpy(&wt, hdr_buf + 2, 2);
        memcpy(&wa, hdr_buf + 4, 2);

        uint16_t req_len     = ntohs_uint16(wl);
        uint16_t req_type    = ntohs_uint16(wt);
        uint16_t req_arg_len = ntohs_uint16(wa);

        const char *err = NULL;
        if (proto_validate_header(req_len, req_type, req_arg_len, &err) != 0) {
            LOG_WARN("fd=%d protocol error: %s", (int)fd, err ? err : "unknown");
            break;
        }

        char *arg_buf = NULL;
        if (req_arg_len > 0) {
            arg_buf = malloc((size_t)req_arg_len + 1);
            if (!arg_buf) {
                LOG_ERROR("malloc failed");
                break;
            }
            arg_buf[req_arg_len] = '\0';

            nr = os_recv_all(fd, arg_buf, req_arg_len);
            if (nr != (ssize_t)req_arg_len) {
                LOG_WARN("fd=%d short arg (" MP_FSIZED " bytes)", (int)fd, MP_CAST_SSIZE(nr));
                free(arg_buf);
                break;
            }
        }

        mp_request_t *req = proto_request_alloc(req_type, arg_buf, req_arg_len);
        free(arg_buf);

        if (!req) {
            LOG_ERROR("proto_request_alloc failed");
            break;
        }

        mp_dispatch(fd, req);
        proto_request_free(req);
    }

    os_close_socket(fd);
    LOG_INFO("worker done for fd=%d", (int)fd);
}

void mp_server_mt_run(mp_socket_t listen_fd, mp_shutdown_flag_t *shutdown_flag)
{
    LOG_INFO("multi-threaded server started");

    while (!MP_FLAG_GET(*shutdown_flag)) {
        mp_socket_t cfd = os_accept(listen_fd);
        if (cfd == MP_INVALID_SOCKET) {
#ifdef _WIN32
            mp_sleep_ms(5);
#else
            if (errno == EINTR) continue;
#endif
            continue;
        }

        LOG_INFO("accepted fd=%d", (int)cfd);

        mt_client_arg_t *arg = malloc(sizeof(*arg));
        if (!arg) {
            LOG_ERROR("malloc failed, dropping fd=%d", (int)cfd);
            os_close_socket(cfd);
            continue;
        }
        arg->fd = cfd;

        if (os_thread_spawn_detached(mt_client_worker, arg) != 0) {
            LOG_ERROR("thread spawn failed for fd=%d", (int)cfd);
            free(arg);
            os_close_socket(cfd);
        }
    }

    LOG_INFO("multi-threaded server stopped");
}