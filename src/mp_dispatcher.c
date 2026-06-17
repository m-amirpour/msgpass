#include "mp_dispatcher.h"
#include "mp_executor.h"
#include "mp_buf.h"
#include "mp_log.h"
#include "os/os_socket.h"

int mp_dispatch(mp_socket_t fd, const mp_request_t *req)
{
    LOG_INFO("dispatching type=%u arg='%s' fd=%d",
             (unsigned)req->type, req->arg ? req->arg : "", (int)fd);

    mp_exec_result_t result = {0};
    int exec_rc = mp_executor_run(req, &result);

    if (exec_rc != 0 && result.data == NULL) {
        LOG_ERROR("executor failed with no output for request");
        return -1;
    }

    if (exec_rc != 0 && result.data != NULL) {
        LOG_WARN("executor failed, but sending captured error output (len=%u)",
                 (unsigned)result.len);
    }

    mp_buf_t resp;
    mp_buf_init(&resp);

    int rc = 0;
    if (proto_encode_response(result.data, result.len, &resp) != 0) {
        LOG_ERROR("proto_encode_response failed");
        rc = -1;
        goto done;
    }

    uint16_t total_len = (uint16_t)(resp.len + 2);
    uint16_t wire_len  = htons(total_len);

    if (os_send_all(fd, &wire_len, sizeof(wire_len)) != sizeof(wire_len)) {
        LOG_WARN("failed to send response header fd=%d", (int)fd);
        rc = -1;
        goto done;
    }

    if (os_send_all(fd, resp.data, resp.len) != (ssize_t)resp.len) {
        LOG_WARN("send failed for fd=%d", (int)fd);
        rc = -1;
    } else {
        LOG_INFO("sent %u byte response to fd=%d", total_len, (int)fd);
    }

    done:
        mp_exec_result_free(&result);
    mp_buf_free(&resp);
    return rc;
}