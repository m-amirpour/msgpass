#ifndef MP_BUF_H
#define MP_BUF_H

/*
 * A simple growable byte buffer. The pattern here is borrowed from
 * the classic "strbuf" design — the buffer owns its memory and you
 * call mp_buf_free when you're done.
 *
 * mp_buf_detach() is the escape hatch: it hands off ownership to
 * the caller and resets the buffer to an empty state.
 */

#include "mp_common.h"

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} mp_buf_t;

void     mp_buf_init(mp_buf_t *b);
void     mp_buf_free(mp_buf_t *b);
void     mp_buf_reset(mp_buf_t *b);

MP_NODISCARD int mp_buf_reserve(mp_buf_t *b, size_t extra);
MP_NODISCARD int mp_buf_append(mp_buf_t *b, const void *data, size_t len);

/* Transfer ownership to caller. Caller must free() the returned pointer. */
uint8_t *mp_buf_detach(mp_buf_t *b, size_t *out_len);

#endif /* MP_BUF_H */