#include "mp_protocol.h"
#include "mp_log.h"
#include "mp_portable.h"

#ifdef _WIN32
#  include <winsock2.h>
#else
#  include <arpa/inet.h>
#endif

uint16_t htons_uint16(uint16_t v) { return htons(v); }
uint16_t ntohs_uint16(uint16_t v) { return ntohs(v); }

int proto_validate_header(uint16_t    req_len,
                          uint16_t    req_type,
                          uint16_t    req_arg_len,
                          const char **err_out)
{
    if (req_len < PROTO_HDR_SIZE) {
        if (err_out) *err_out = "req_len below minimum (6)";
        return -1;
    }

    /*
     * This is the critical check. The arg_len field must be exactly
     * req_len minus the 6-byte header. If this fails, the bytes
     * were either corrupted, misaligned, or encoded incorrectly.
     */
    uint16_t expected_arg_len = (uint16_t)(req_len - PROTO_HDR_SIZE);
    if (req_arg_len != expected_arg_len) {
        if (err_out) *err_out = "req_arg_len inconsistent with req_len";
        return -1;
    }

    switch (req_type) {
    case REQUEST_TYPE_LS:
    case REQUEST_TYPE_PWD:
    case REQUEST_TYPE_CAT:
        break;
    default:
        if (err_out) *err_out = "unknown request type";
        return -1;
    }

    return 0;
}

int proto_encode_request(uint16_t    type,
                         const char *arg,
                         uint16_t    arg_len,
                         mp_buf_t   *out)
{
    if (!out) return -1;

    /*
     * Wire layout:
     *   [REQ_LEN:2][REQ_TYPE:2][REQ_ARG_LEN:2][REQ_ARG:arg_len]
     *
     * REQ_LEN = 6 + arg_len (total frame length including REQ_LEN itself)
     */
    size_t total = (size_t)PROTO_HDR_SIZE + (size_t)arg_len;
    if (total > MP_MAX_FRAME_LEN) {
        LOG_ERROR("request too large: " MP_FSIZE " bytes", MP_CAST_SIZE(total));
        return -1;
    }

    uint16_t wire_total   = htons_uint16((uint16_t)total);
    uint16_t wire_type    = htons_uint16(type);
    uint16_t wire_arg_len = htons_uint16(arg_len);

    /*
     * Write the header as three separate 2-byte fields.
     * Using memcpy into a local buffer first so we get the exact
     * byte layout we expect, regardless of struct packing.
     */
    uint8_t hdr[PROTO_HDR_SIZE];
    memcpy(hdr + 0, &wire_total,   2);
    memcpy(hdr + 2, &wire_type,    2);
    memcpy(hdr + 4, &wire_arg_len, 2);

    if (mp_buf_append(out, hdr, PROTO_HDR_SIZE) != 0)
        return -1;

    if (arg_len > 0 && arg) {
        if (mp_buf_append(out, arg, arg_len) != 0)
            return -1;
    }

    LOG_DEBUG("encoded request: total=%u type=%u arg_len=%u",
              (unsigned)total, (unsigned)type, (unsigned)arg_len);

    return 0;
}

int proto_encode_response(const char *data,
                          size_t      data_len,
                          mp_buf_t   *out)
{
    if (!out) return -1;

    if (data_len > PROTO_MAX_RESP_DATA)
        data_len = PROTO_MAX_RESP_DATA;

    size_t total = (size_t)PROTO_RESP_HDR_SIZE + data_len;

    uint16_t wire_total = htons_uint16((uint16_t)total);
    if (mp_buf_append(out, &wire_total, 2) != 0)
        return -1;

    if (data_len > 0 && data) {
        if (mp_buf_append(out, data, data_len) != 0)
            return -1;
    }

    return 0;
}

mp_request_t *proto_request_alloc(uint16_t    type,
                                   const char *arg,
                                   uint16_t    arg_len)
{
    mp_request_t *req = malloc(sizeof(*req));
    if (!req) return NULL;

    req->arg = malloc((size_t)arg_len + 1);
    if (!req->arg) {
        free(req);
        return NULL;
    }

    if (arg_len > 0 && arg)
        memcpy(req->arg, arg, arg_len);
    req->arg[arg_len] = '\0';

    req->type    = type;
    req->arg_len = arg_len;
    return req;
}

void proto_request_free(mp_request_t *req)
{
    if (!req) return;
    free(req->arg);
    free(req);
}