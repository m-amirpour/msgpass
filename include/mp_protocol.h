#ifndef MP_PROTOCOL_H
#define MP_PROTOCOL_H

/*
 * Everything the protocol parser knows about. This header is the
 * contract between the wire format and the rest of the system.
 *
 * The parser doesn't care whether bytes arrived over TCP or a UNIX
 * socket — that's intentional. Transport is somebody else's problem.
 */

#include "mp_common.h"
#include "mp_buf.h"

/* Wire-level constants */
#define PROTO_HDR_SIZE       MP_REQ_HDR_LEN
#define PROTO_RESP_HDR_SIZE  MP_RESP_HDR_LEN
#define PROTO_MAX_ARG_LEN    ((uint16_t)(MP_MAX_FRAME_LEN - PROTO_HDR_SIZE))
#define PROTO_MAX_RESP_DATA  ((size_t)(MP_MAX_FRAME_LEN - PROTO_RESP_HDR_SIZE))

/* Request types — exactly as specified. */
enum {
    REQUEST_TYPE_LS  = 1,
    REQUEST_TYPE_PWD = 2,
    REQUEST_TYPE_CAT = 3
};

/*
 * Decoded request. The arg field is null-terminated and heap-allocated.
 * Free with proto_request_free().
 */
typedef struct {
    uint16_t type;
    uint16_t arg_len;
    char    *arg;
} mp_request_t;

/* Byte-order helpers the spec explicitly names. */
uint16_t htons_uint16(uint16_t v);
uint16_t ntohs_uint16(uint16_t v);

/* Validation */
MP_NODISCARD int proto_validate_header(uint16_t    req_len,
                                       uint16_t    req_type,
                                       uint16_t    req_arg_len,
                                       const char **err_out);

/* Encoding */
MP_NODISCARD int proto_encode_request(uint16_t     type,
                                      const char  *arg,
                                      uint16_t     arg_len,
                                      mp_buf_t    *out);

MP_NODISCARD int proto_encode_response(const char *data,
                                       size_t      data_len,
                                       mp_buf_t   *out);

/* Request lifecycle */
mp_request_t *proto_request_alloc(uint16_t type, const char *arg, uint16_t arg_len);
void          proto_request_free(mp_request_t *req);

#endif /* MP_PROTOCOL_H */