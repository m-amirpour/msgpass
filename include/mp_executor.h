#ifndef MP_EXECUTOR_H
#define MP_EXECUTOR_H

/*
 * The executor turns a decoded request into command output.
 * It knows nothing about sockets or protocols — it just runs
 * a command and gives you the stdout.
 */

#include "mp_common.h"
#include "mp_protocol.h"

/* Timeout for any spawned child, in milliseconds. */
#define EXECUTOR_TIMEOUT_MS 5000

typedef struct {
    char  *data;   /* heap-allocated; caller must free */
    size_t len;
} mp_exec_result_t;

/*
 * Execute the request and fill result. Always sets result->data
 * to something (possibly an error message), so the caller can
 * always call mp_exec_result_free() regardless of return value.
 */
MP_NODISCARD int mp_executor_run(const mp_request_t *req, mp_exec_result_t *result);

void mp_exec_result_free(mp_exec_result_t *result);

#endif /* MP_EXECUTOR_H */