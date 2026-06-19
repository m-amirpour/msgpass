#include "mp_executor.h"
#include "mp_log.h"
#include "mp_portable.h"
#include "os/os_process.h"

/*
 * On Windows, pwd semantics come from GetCurrentDirectory(), not from
 * shelling out to cmd. Using cmd /c cd is fragile because the working
 * directory of cmd.exe may differ from the server process. We implement
 * it directly through the OS API and feed it back as if it were stdout.
 */

#ifdef _WIN32

static int exec_pwd_windows(char **out_data, size_t *out_len)
{
    DWORD needed = GetCurrentDirectoryA(0, NULL);
    if (needed == 0) {
        const char *msg = "pwd: GetCurrentDirectory failed";
        *out_data = malloc(strlen(msg) + 1);
        if (*out_data) strcpy(*out_data, msg);
        *out_len = *out_data ? strlen(msg) : 0;
        return -1;
    }

    /* +2 for '\n' and '\0' */
    char *buf = malloc((size_t)needed + 2);
    if (!buf) {
        *out_data = NULL;
        *out_len  = 0;
        return -1;
    }

    DWORD written = GetCurrentDirectoryA(needed, buf);
    if (written == 0 || written >= needed) {
        free(buf);
        const char *msg = "pwd: GetCurrentDirectory failed";
        *out_data = malloc(strlen(msg) + 1);
        if (*out_data) strcpy(*out_data, msg);
        *out_len = *out_data ? strlen(msg) : 0;
        return -1;
    }

    /* Append newline to match unix pwd output */
    buf[written]     = '\n';
    buf[written + 1] = '\0';

    *out_data = buf;
    *out_len  = (size_t)written + 1;
    return 0;
}

#endif /* _WIN32 */

static void set_error_msg(mp_exec_result_t *res, const char *msg)
{
    free(res->data);
    res->data = NULL;
    res->len  = 0;

    size_t n = strlen(msg);
    res->data = malloc(n + 1);
    if (res->data) {
        memcpy(res->data, msg, n + 1);
        res->len = n;
    }
}

int mp_executor_run(const mp_request_t *req, mp_exec_result_t *result)
{
    result->data = NULL;
    result->len  = 0;

    int rc = 0;

    switch (req->type) {

    case REQUEST_TYPE_LS: {
#ifdef _WIN32
        char *const args[] = { "cmd", "/c", "dir", req->arg, NULL };
#else
        char *const args[] = { "ls", "-la", req->arg, NULL };
#endif
        rc = os_exec_capture(args, EXECUTOR_TIMEOUT_MS,
                             &result->data, &result->len);
        break;
    }

    case REQUEST_TYPE_PWD: {
#ifdef _WIN32
        /*
         * Use GetCurrentDirectory() directly instead of shelling out.
         * cmd /c cd is unreliable because cmd may start in a different
         * directory than the server process.
         */
        rc = exec_pwd_windows(&result->data, &result->len);
#else
        char *const args[] = { "pwd", NULL };
        rc = os_exec_capture(args, EXECUTOR_TIMEOUT_MS,
                             &result->data, &result->len);
#endif
        break;
    }

    case REQUEST_TYPE_CAT: {
#ifdef _WIN32
        char *const args[] = { "cmd", "/c", "type", req->arg, NULL };
#else
        char *const args[] = { "cat", req->arg, NULL };
#endif
        rc = os_exec_capture(args, EXECUTOR_TIMEOUT_MS,
                             &result->data, &result->len);
        break;
    }

    default:
        set_error_msg(result, "Unknown request type");
        return -1;
    }

    if (rc != 0 && !result->data)
        set_error_msg(result, "Command execution failed");

    if (rc == 0 && !result->data) {
        result->data = malloc(1);
        if (result->data) {
            result->data[0] = '\0';
            result->len = 0;
        }
    }

    return rc;
}

void mp_exec_result_free(mp_exec_result_t *result)
{
    if (!result) return;
    free(result->data);
    result->data = NULL;
    result->len  = 0;
}