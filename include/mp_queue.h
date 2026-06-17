#ifndef MP_QUEUE_H
#define MP_QUEUE_H

/*
 * A straightforward FIFO queue backed by a singly-linked list.
 *
 * Ownership rule: enqueue() takes ownership of the request pointer.
 * dequeue() transfers it to the caller. The caller is responsible
 * for calling proto_request_free() after processing.
 *
 * Not thread-safe by design — it lives entirely inside the
 * single-threaded event loop.
 */

#include "mp_common.h"
#include "mp_protocol.h"
#include "os/os_socket.h"

typedef struct mp_queue_node {
    mp_socket_t           client_fd;
    mp_request_t         *request;
    struct mp_queue_node *next;
} mp_queue_node_t;

typedef struct {
    mp_queue_node_t *head;
    mp_queue_node_t *tail;
    size_t           count;
} mp_queue_t;

void          mp_queue_init(mp_queue_t *q);
void          mp_queue_drain(mp_queue_t *q);

MP_NODISCARD int  mp_queue_enqueue(mp_queue_t *q, mp_socket_t fd, mp_request_t *req);
bool              mp_queue_dequeue(mp_queue_t *q, mp_socket_t *fd_out, mp_request_t **req_out);
bool              mp_queue_is_empty(const mp_queue_t *q);
size_t            mp_queue_size(const mp_queue_t *q);

#endif /* MP_QUEUE_H */