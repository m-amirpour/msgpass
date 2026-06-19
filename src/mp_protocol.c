#include "mp_protocol.h"
#include "mp_log.h"

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

    /* uint16_t max is 65535 by definition so upper bound always holds */
    if (req_arg_len != (uint16_t)(req_len - PROTO_HDR_SIZE)) {
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
    size_t total = (size_t)PROTO_HDR_SIZE + arg_len;
    if (total > MP_MAX_FRAME_LEN) {
        LOG_ERROR("request too large: " MP_FSIZE " bytes", MP_CAST_SIZE(total));
        return -1;
    }

    uint8_t hdr[PROTO_HDR_SIZE];
    uint16_t wl = htons_uint16((uint16_t)total);
    uint16_t wt = htons_uint16(type);
    uint16_t wa = htons_uint16(arg_len);

    /* Write explicit bytes in network order (big-endian) to avoid any
       memcpy-from-uint16_t ambiguity across platforms/ABIs. */
    hdr[0] = (uint8_t)((wl >> 8) & 0xFF);
    hdr[1] = (uint8_t)(wl & 0xFF);
    hdr[2] = (uint8_t)((wt >> 8) & 0xFF);
    hdr[3] = (uint8_t)(wt & 0xFF);
    hdr[4] = (uint8_t)((wa >> 8) & 0xFF);
    hdr[5] = (uint8_t)(wa & 0xFF);

    if (mp_buf_append(out, hdr, PROTO_HDR_SIZE) != 0) return -1;
    if (arg_len > 0 && arg) {
        if (mp_buf_append(out, arg, arg_len) != 0) return -1;
    }
    return 0;
}

int proto_encode_response(const char *data,
                          size_t      data_len,
                          mp_buf_t   *out)
{
    if (data_len > PROTO_MAX_RESP_DATA)
        data_len = PROTO_MAX_RESP_DATA;

    size_t total = PROTO_RESP_HDR_SIZE + data_len;
    uint16_t wl = htons_uint16((uint16_t)total);

    uint8_t hdr[PROTO_RESP_HDR_SIZE];
    hdr[0] = (uint8_t)((wl >> 8) & 0xFF);
    hdr[1] = (uint8_t)(wl & 0xFF);

    if (mp_buf_append(out, hdr, PROTO_RESP_HDR_SIZE) != 0) return -1;
    if (data_len > 0 && data) {
        if (mp_buf_append(out, data, data_len) != 0) return -1;
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