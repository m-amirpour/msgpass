#include "mp_client_state.h"
#include "mp_log.h"
#include "mp_portable.h"

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
    /*
     * Reset the state machine for the next request on the same connection.
     * We must preserve the fd — everything else goes back to zero.
     */
    mp_socket_t saved_fd = cs->fd;
    free(cs->arg_buf);
    memset(cs, 0, sizeof(*cs));
    cs->fd    = saved_fd;
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

    LOG_DEBUG("fd=%d raw header bytes: %02x %02x %02x %02x %02x %02x",
              (int)cs->fd,
              cs->hdr_buf[0], cs->hdr_buf[1],
              cs->hdr_buf[2], cs->hdr_buf[3],
              cs->hdr_buf[4], cs->hdr_buf[5]);

    LOG_DEBUG("fd=%d parsed: req_len=%u req_type=%u req_arg_len=%u",
              (int)cs->fd,
              (unsigned)cs->req_len,
              (unsigned)cs->req_type,
              (unsigned)cs->req_arg_len);

    const char *err = NULL;
    if (proto_validate_header(cs->req_len, cs->req_type, cs->req_arg_len, &err) != 0) {
        LOG_WARN("fd=%d protocol error: %s (req_len=%u type=%u arg_len=%u)",
                 (int)cs->fd, err ? err : "unknown",
                 (unsigned)cs->req_len,
                 (unsigned)cs->req_type,
                 (unsigned)cs->req_arg_len);
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
    cs_reset(cs);
    return req;
}