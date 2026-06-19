#include "mp_dispatcher.h"
#include "mp_executor.h"
#include "mp_buf.h"
#include "mp_log.h"
#include "mp_portable.h"
#include "os/os_socket.h"

int mp_dispatch(mp_socket_t fd, const mp_request_t *req)
{
    LOG_INFO("dispatching type=%u arg='%s' fd=%d",
             (unsigned)req->type, req->arg ? req->arg : "", (int)fd);

    mp_exec_result_t result;
    memset(&result, 0, sizeof(result));

    /* mp_executor_run is MP_NODISCARD — capture the return value */
    int exec_rc = mp_executor_run(req, &result);
    if (exec_rc != 0) {
        LOG_WARN("command execution failed (rc=%d) for fd=%d", exec_rc, (int)fd);
    }

    mp_buf_t resp;
    mp_buf_init(&resp);

    int rc = 0;
    if (proto_encode_response(result.data, result.len, &resp) != 0) {
        LOG_ERROR("proto_encode_response failed");
        rc = -1;
        goto done;
    }

    LOG_DEBUG("sending " MP_FSIZE " byte response to fd=%d",
              MP_CAST_SIZE(resp.len), (int)fd);

    ssize_t sent = os_send_all(fd, resp.data, resp.len);
    if (sent != (ssize_t)resp.len) {
        LOG_WARN("send failed for fd=%d", (int)fd);
        rc = -1;
    } else {
        LOG_INFO("sent " MP_FSIZE " byte response to fd=%d",
                 MP_CAST_SIZE(resp.len), (int)fd);
    }

    done:
        mp_exec_result_free(&result);
    mp_buf_free(&resp);
    return rc;
}