/*
 * Single-threaded server using select() for I/O multiplexing.
 *
 * On Windows, Winsock's select() works differently from POSIX:
 * - fd_set is an array of SOCKETs, not a bitmask indexed by fd number
 * - FD_SETSIZE is the max number of sockets in the set (default 64)
 * - The nfds parameter to select() is ignored on Windows
 *
 * We handle this transparently since we use the FD_SET/FD_ISSET macros
 * which work correctly on both platforms.
 */

#include "mp_common.h"
#include "mp_client_state.h"
#include "mp_queue.h"
#include "mp_dispatcher.h"
#include "mp_log.h"
#include "mp_portable.h"
#include "os/os_socket.h"

#ifndef _WIN32
#include <sys/select.h>
#endif

typedef struct mp_client_list {
    mp_client_node_t *head;
    int               count;
} mp_client_list_t;

static void client_list_add(mp_client_list_t *list, mp_socket_t fd)
{
    mp_client_node_t *node = calloc(1, sizeof(*node));
    if (!node) {
        LOG_ERROR("could not allocate client node, dropping fd=%d", (int)fd);
        os_close_socket(fd);
        return;
    }
    cs_init(&node->state, fd);
    node->next  = list->head;
    list->head  = node;
    list->count++;
    LOG_INFO("client connected fd=%d (total=%d)", (int)fd, list->count);
}

static void client_list_remove(mp_client_list_t *list, mp_socket_t fd)
{
    mp_client_node_t **pp = &list->head;
    while (*pp) {
        if ((*pp)->state.fd == fd) {
            mp_client_node_t *dead = *pp;
            *pp = dead->next;
            list->count--;
            cs_free(&dead->state);
            os_close_socket(fd);
            free(dead);
            LOG_INFO("client removed fd=%d (total=%d)", (int)fd, list->count);
            return;
        }
        pp = &(*pp)->next;
    }
}

static void client_list_destroy(mp_client_list_t *list)
{
    mp_client_node_t *cur = list->head;
    while (cur) {
        mp_client_node_t *next = cur->next;
        cs_free(&cur->state);
        os_close_socket(cur->state.fd);
        free(cur);
        cur = next;
    }
    list->head  = NULL;
    list->count = 0;
}

void mp_server_st_run(mp_socket_t listen_fd, mp_shutdown_flag_t *shutdown_flag)
{
    mp_client_list_t clients = {NULL, 0};
    mp_queue_t       queue;
    mp_queue_init(&queue);

    LOG_INFO("single-threaded event loop started");

    while (!MP_FLAG_GET(*shutdown_flag)) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(listen_fd, &rset);

        /*
         * On POSIX, nfds must be the highest fd + 1.
         * On Windows, nfds is ignored but we compute it anyway.
         */
        int max_fd = (int)listen_fd;

        for (mp_client_node_t *n = clients.head; n; n = n->next) {
            FD_SET(n->state.fd, &rset);
            if ((int)n->state.fd > max_fd)
                max_fd = (int)n->state.fd;
        }

        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 100000; /* 100ms */

        int sel = select(max_fd + 1, &rset, NULL, NULL, &tv);

        if (sel < 0) {
#ifdef _WIN32
            int e = WSAGetLastError();
            if (e == WSAEINTR) continue;
            LOG_ERROR("select failed: %d (%s)", e, os_socket_strerror(e));
#else
            if (errno == EINTR) continue;
            LOG_ERROR("select: %s", strerror(errno));
#endif
            break;
        }

        if (sel == 0)
            continue;

        /* Accept new connections. Drain all pending accepts. */
        if (FD_ISSET(listen_fd, &rset)) {
            for (;;) {
                mp_socket_t cfd = os_accept(listen_fd);
                if (cfd == MP_INVALID_SOCKET)
                    break;

                if (os_set_nonblocking(cfd) != 0) {
                    LOG_ERROR("failed to set non-blocking on fd=%d", (int)cfd);
                    os_close_socket(cfd);
                    continue;
                }

                client_list_add(&clients, cfd);
            }
        }

        /* Feed each readable client through the state machine. */
        mp_client_node_t *cur = clients.head;
        while (cur) {
            mp_client_node_t *next = cur->next;
            mp_socket_t       cfd  = cur->state.fd;

            if (!FD_ISSET(cfd, &rset)) {
                cur = next;
                continue;
            }

            int client_removed = 0;

            for (;;) {
                cs_feed_result_t res = cs_feed(&cur->state);

                if (res == CS_FEED_READY) {
                    mp_request_t *req = cs_take_request(&cur->state);
                    if (!req) {
                        LOG_ERROR("cs_take_request failed for fd=%d", (int)cfd);
                        client_list_remove(&clients, cfd);
                        client_removed = 1;
                        break;
                    }
                    LOG_DEBUG("queued request type=%u for fd=%d",
                              (unsigned)req->type, (int)cfd);
                    if (mp_queue_enqueue(&queue, cfd, req) != 0) {
                        LOG_ERROR("enqueue failed for fd=%d", (int)cfd);
                        proto_request_free(req);
                    }
                    continue;
                }

                if (res == CS_FEED_NEED_MORE)
                    break;

                if (res == CS_FEED_CLOSED)
                    LOG_INFO("client fd=%d disconnected", (int)cfd);
                else if (res == CS_FEED_PROTO_ERR)
                    LOG_WARN("protocol error from fd=%d", (int)cfd);
                else
                    LOG_WARN("read error from fd=%d", (int)cfd);

                client_list_remove(&clients, cfd);
                client_removed = 1;
                break;
            }

            (void)client_removed;
            cur = next;
        }

        /* Process exactly one queued request per iteration. */
        if (!mp_queue_is_empty(&queue)) {
            mp_socket_t   cfd = MP_INVALID_SOCKET;
            mp_request_t *req = NULL;

            if (mp_queue_dequeue(&queue, &cfd, &req)) {
                LOG_DEBUG("dispatching request type=%u to fd=%d (queue=" MP_FSIZE ")",
                          (unsigned)req->type, (int)cfd, MP_CAST_SIZE(mp_queue_size(&queue)));
                mp_dispatch(cfd, req);
                proto_request_free(req);
            }
        }
    }

    LOG_INFO("event loop shutting down (clients=%d, queue=" MP_FSIZE ")",
             clients.count, MP_CAST_SIZE(mp_queue_size(&queue)));
    mp_queue_drain(&queue);
    client_list_destroy(&clients);
    LOG_INFO("event loop exited cleanly");
}