#include "mp_client_state.h"
#include "mp_log.h"

void cs_init(mp_client_state_t *cs, mp_socket_t fd)
{
    memset(cs, 0, sizeof(*cs));
    cs->fd    = fd;
    cs->phase = CS_READ_HEADER;
}

void cs_free(mp_client_state_t *cs)
{
    if (!cs) return;
    free(cs->arg_buf);
    cs->arg_buf = NULL;
}

void cs_reset(mp_client_state_t *cs)
{
    free(cs->arg_buf);
    memset(cs, 0, sizeof(*cs));
    /* fd is preserved — the connection is still open */
    cs->phase = CS_READ_HEADER;
}

static int parse_header(mp_client_state_t *cs)
{
    uint16_t wl, wt, wa;
    memcpy(&wl, cs->hdr_buf + 0, 2);
    memcpy(&wt, cs->hdr_buf + 2, 2);
    memcpy(&wa, cs->hdr_buf + 4, 2);

    cs->req_len     = ntohs_uint16(wl);
    cs->req_type    = ntohs_uint16(wt);
    cs->req_arg_len = ntohs_uint16(wa);

    const char *err = NULL;
    if (proto_validate_header(cs->req_len, cs->req_type, cs->req_arg_len, &err) != 0) {
        LOG_WARN("fd=%d protocol error: %s", (int)cs->fd, err ? err : "unknown");
        return -1;
    }
    return 0;
}

cs_feed_result_t cs_feed(mp_client_state_t *cs)
{
    for (;;) {
        switch (cs->phase) {

        case CS_READ_HEADER: {
            size_t want = (size_t)(MP_REQ_HDR_LEN - cs->hdr_bytes);
            ssize_t nr  = os_recv_nonblock(cs->fd,
                                           cs->hdr_buf + cs->hdr_bytes,
                                           want);
            if (nr == 0)  return CS_FEED_CLOSED;
            if (nr == -1) return CS_FEED_ERROR;
            if (nr == -2) return CS_FEED_NEED_MORE;

            cs->hdr_bytes += (uint16_t)nr;
            if (cs->hdr_bytes < MP_REQ_HDR_LEN)
                return CS_FEED_NEED_MORE;

            if (parse_header(cs) != 0)
                return CS_FEED_PROTO_ERR;

            if (cs->req_arg_len == 0) {
                cs->phase = CS_READY;
                return CS_FEED_READY;
            }

            cs->arg_buf = malloc((size_t)cs->req_arg_len + 1);
            if (!cs->arg_buf) {
                LOG_ERROR("malloc arg_buf failed");
                return CS_FEED_ERROR;
            }
            cs->arg_buf[cs->req_arg_len] = '\0';
            cs->arg_bytes = 0;
            cs->phase = CS_READ_ARG;
            /* fall through to CS_READ_ARG on next iteration */
            break;
        }

        case CS_READ_ARG: {
            size_t  want = (size_t)(cs->req_arg_len - cs->arg_bytes);
            ssize_t nr   = os_recv_nonblock(cs->fd,
                                            (uint8_t *)cs->arg_buf + cs->arg_bytes,
                                            want);
            if (nr == 0)  return CS_FEED_CLOSED;
            if (nr == -1) return CS_FEED_ERROR;
            if (nr == -2) return CS_FEED_NEED_MORE;

            cs->arg_bytes += (uint16_t)nr;
            if (cs->arg_bytes < cs->req_arg_len)
                return CS_FEED_NEED_MORE;

            cs->phase = CS_READY;
            return CS_FEED_READY;
        }

        case CS_READY:
            return CS_FEED_READY;
        }
    }
}

mp_request_t *cs_take_request(mp_client_state_t *cs)
{
    mp_request_t *req = proto_request_alloc(cs->req_type,
                                            cs->arg_buf,
                                            cs->req_arg_len);
    /* Reset for the next request on this connection. */
    mp_socket_t saved_fd = cs->fd;
    cs_reset(cs);
    cs->fd = saved_fd;
    return req;
}