#ifndef MP_CLIENT_STATE_H
#define MP_CLIENT_STATE_H

/*
 * Per-client state machine for the single-threaded event loop.
 *
 * Accumulates bytes from non-blocking reads and signals when a
 * complete, validated request has been assembled.
 *
 * The state machine moves through:
 *   CS_READ_HEADER -> CS_READ_ARG -> CS_READY
 *
 * After the caller extracts the request via cs_take_request(),
 * the state machine resets to CS_READ_HEADER for the next request
 * on the same connection.
 */

#include "mp_common.h"
#include "mp_protocol.h"
#include "os/os_socket.h"

typedef enum {
    CS_READ_HEADER   = 0,
    CS_READ_ARG      = 1,
    CS_READY         = 2
} client_state_phase_t;

typedef enum {
    CS_FEED_NEED_MORE  =  0,
    CS_FEED_READY      =  1,
    CS_FEED_CLOSED     = -1,
    CS_FEED_ERROR      = -2,
    CS_FEED_PROTO_ERR  = -3
} cs_feed_result_t;

typedef struct {
    mp_socket_t          fd;
    client_state_phase_t phase;

    /* Header accumulation buffer — always exactly 6 bytes */
    uint8_t  hdr_buf[MP_REQ_HDR_LEN];
    uint16_t hdr_bytes;

    /* Parsed header fields (host byte order) */
    uint16_t req_len;
    uint16_t req_type;
    uint16_t req_arg_len;

    /* Argument accumulation */
    char    *arg_buf;
    uint16_t arg_bytes;
} mp_client_state_t;

/* Linked list node for use by the event loop */
typedef struct mp_client_node {
    mp_client_state_t      state;
    struct mp_client_node *next;
} mp_client_node_t;

void             cs_init(mp_client_state_t *cs, mp_socket_t fd);
void             cs_free(mp_client_state_t *cs);
void             cs_reset(mp_client_state_t *cs);
cs_feed_result_t cs_feed(mp_client_state_t *cs);
mp_request_t    *cs_take_request(mp_client_state_t *cs);

#endif /* MP_CLIENT_STATE_H */