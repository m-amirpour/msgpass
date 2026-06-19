#include "mp_queue.h"
#include "mp_log.h"
#include "mp_portable.h"

void mp_queue_init(mp_queue_t *q)
{
    q->head  = NULL;
    q->tail  = NULL;
    q->count = 0;
}

void mp_queue_drain(mp_queue_t *q)
{
    mp_socket_t  fd;
    mp_request_t *req;

    while (mp_queue_dequeue(q, &fd, &req)) {
        LOG_DEBUG("draining request type=%u", (unsigned)req->type);
        proto_request_free(req);
    }
}

int mp_queue_enqueue(mp_queue_t *q, mp_socket_t fd, mp_request_t *req)
{
    mp_queue_node_t *node = malloc(sizeof(*node));
    if (!node) {
        LOG_ERROR("queue node malloc failed");
        return -1;
    }

    node->client_fd = fd;
    node->request   = req;
    node->next      = NULL;

    if (q->tail) {
        q->tail->next = node;
    } else {
        q->head = node;
    }
    q->tail = node;
    q->count++;
    return 0;
}

bool mp_queue_dequeue(mp_queue_t *q, mp_socket_t *fd_out, mp_request_t **req_out)
{
    if (!q->head) return false;

    mp_queue_node_t *node = q->head;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    q->count--;

    *fd_out  = node->client_fd;
    *req_out = node->request;
    free(node);
    return true;
}

bool   mp_queue_is_empty(const mp_queue_t *q) { return q->head == NULL; }
size_t mp_queue_size(const mp_queue_t *q)     { return q->count; }