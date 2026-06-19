/*
 * Single-threaded server using select() for I/O multiplexing.
 *
 * The basic loop is:
 *   1. Build fd_set from all tracked client fds + the listening socket.
 *   2. Call select() with a short timeout so we can check the shutdown flag.
 *   3. Accept any new connections.
 *   4. Feed each readable client through its state machine.
 *      When a complete request arrives, push it onto the FIFO queue.
 *   5. Dequeue exactly one request and dispatch it.
 *
 * Step 5 is deliberately one-at-a-time. This keeps the latency predictable
 * and avoids starving the I/O phase when commands take a while to run.
 */

#include "mp_common.h"
#include "mp_client_state.h"
#include "mp_queue.h"
#include "mp_dispatcher.h"
#include "mp_log.h"
#include "os/os_socket.h"

#ifndef _WIN32
#include <sys/select.h>
#endif

/* A simple linked list of active client connections. */
typedef struct mp_client_list {
    mp_client_node_t *head;
    int               count;
} mp_client_list_t;

static void client_list_add(mp_client_list_t *list, mp_socket_t fd)
{
    mp_client_node_t *node = calloc(1, sizeof(*node));
    if (!node) {
        LOG_ERROR("could not allocate client node, closing fd=%d", (int)fd);
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

        int max_fd = (int)listen_fd;

        for (mp_client_node_t *n = clients.head; n; n = n->next) {
            FD_SET(n->state.fd, &rset);
            if ((int)n->state.fd > max_fd)
                max_fd = (int)n->state.fd;
        }

        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
        int sel = select(max_fd + 1, &rset, NULL, NULL, &tv);

        if (sel < 0) {
#ifdef _WIN32
            int e = WSAGetLastError();
            if (e == WSAEINTR) continue;
            LOG_ERROR("select failed: %d", e);
#else
            if (errno == EINTR) continue;
            LOG_ERROR("select: %s", strerror(errno));
#endif
            break;
        }

        /* Accept any waiting connections first. */
        if (sel > 0 && FD_ISSET(listen_fd, &rset)) {
            mp_socket_t cfd = os_accept(listen_fd);
            if (cfd != MP_INVALID_SOCKET) {
                os_set_nonblocking(cfd);
                client_list_add(&clients, cfd);
            }
        }

        /* Feed each readable client through its state machine. */
        mp_client_node_t *cur = clients.head;
        while (cur) {
            mp_client_node_t *next = cur->next;
            mp_socket_t       cfd  = cur->state.fd;

            /* Only read when select() reported the socket as readable. */
            if (sel <= 0 || !FD_ISSET(cfd, &rset)) {
                cur = next;
                continue;
            }

            /*
             * Keep feeding until the socket has no more data right now
             * or until a complete request is available. A single recv()
             * can return multiple requests' worth of data in theory.
             */
            for (;;) {
                cs_feed_result_t res = cs_feed(&cur->state);

                if (res == CS_FEED_READY) {
                    mp_request_t *req = cs_take_request(&cur->state);
                    if (!req) {
                        LOG_ERROR("cs_take_request failed for fd=%d", (int)cfd);
                        client_list_remove(&clients, cfd);
                        cur = NULL;
                        break;
                    }
                    LOG_DEBUG("queued request type=%u for fd=%d", req->type, (int)cfd);
                    if (mp_queue_enqueue(&queue, cfd, req) != 0)
                        proto_request_free(req);
                    /* Check for more data immediately. */
                    continue;
                }

                if (res == CS_FEED_NEED_MORE) break;

                if (res == CS_FEED_CLOSED || res == CS_FEED_ERROR || res == CS_FEED_PROTO_ERR) {
                    client_list_remove(&clients, cfd);
                    cur = NULL;
                    break;
                }
            }

            cur = (cur == NULL) ? NULL : next;
        }

        /* Process exactly one request per loop iteration. */
        if (!mp_queue_is_empty(&queue)) {
            mp_socket_t   cfd = MP_INVALID_SOCKET;
            mp_request_t *req = NULL;

            if (mp_queue_dequeue(&queue, &cfd, &req)) {
                LOG_DEBUG("dispatching queued request, queue depth now " MP_FSIZE,
                          MP_CAST_SIZE(mp_queue_size(&queue)));
                mp_dispatch(cfd, req);
                proto_request_free(req);
            }
        }
    }

    LOG_INFO("event loop shutting down");
    mp_queue_drain(&queue);
    client_list_destroy(&clients);
}