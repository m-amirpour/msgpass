#include "mp_buf.h"
#include "mp_log.h"

#define INITIAL_CAP 4096u

void mp_buf_init(mp_buf_t *b)
{
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

void mp_buf_free(mp_buf_t *b)
{
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

void mp_buf_reset(mp_buf_t *b)
{
    b->len = 0;
}

int mp_buf_reserve(mp_buf_t *b, size_t extra)
{
    size_t needed = b->len + extra;
    if (needed <= b->cap)
        return 0;

    size_t new_cap = b->cap ? b->cap : INITIAL_CAP;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = needed;
            break;
        }
        new_cap *= 2;
    }

    uint8_t *p = realloc(b->data, new_cap);
    if (!p) {
        LOG_ERROR("realloc(" MP_FSIZE ") failed", MP_CAST_SIZE(new_cap));
        return -1;
    }
    b->data = p;
    b->cap  = new_cap;
    return 0;
}

int mp_buf_append(mp_buf_t *b, const void *data, size_t len)
{
    if (!len) return 0;
    if (mp_buf_reserve(b, len) != 0) return -1;
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 0;
}

uint8_t *mp_buf_detach(mp_buf_t *b, size_t *out_len)
{
    uint8_t *p = b->data;
    if (out_len) *out_len = b->len;
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
    return p;
}