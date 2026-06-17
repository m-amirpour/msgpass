#include "mp_executor.h"
#include "mp_log.h"
#include "os/os_process.h"
#include "mp_buf.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static void set_error_msg(mp_exec_result_t *res, const char *msg)
{
    size_t n = strlen(msg);
    res->data = malloc(n + 1);
    if (res->data) {
        memcpy(res->data, msg, n + 1);
        res->len = n;
    } else {
        res->len = 0;
    }
}

#ifdef _WIN32
/* Windows native directory listing (uses FindFirstFile) */
static char *list_directory(const char *path, size_t *out_len)
{
    char pattern[MAX_PATH + 2];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        *out_len = 0;
        return strdup("No files or access denied");
    }

    mp_buf_t buf;
    mp_buf_init(&buf);
    int ok = 1;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (mp_buf_append(&buf, "[DIR] ", 6) != 0) { ok = 0; break; }
        } else {
            if (mp_buf_append(&buf, "      ", 6) != 0) { ok = 0; break; }
        }
        if (mp_buf_append(&buf, fd.cFileName, strlen(fd.cFileName)) != 0) {
            ok = 0; break;
        }
        if (mp_buf_append(&buf, "\r\n", 2) != 0) {
            ok = 0; break;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);

    if (!ok) {
        mp_buf_free(&buf);
        *out_len = 0;
        return strdup("Error building output");
    }

    char *data = (char *)mp_buf_detach(&buf, out_len);
    if (!data) {
        *out_len = 0;
        return strdup("");
    }
    return data;
}

/* Cross‑platform file reader using standard C I/O (works on Windows too) */
static char *read_file_content(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        *out_len = 0;
        return strdup("File not found or access denied");
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    if (size < 0) {
        fclose(fp);
        *out_len = 0;
        return strdup("Failed to read file size");
    }

    char *data = malloc((size_t)size + 1);
    if (!data) {
        fclose(fp);
        *out_len = 0;
        return strdup("Memory allocation failed");
    }

    size_t bytes_read = fread(data, 1, (size_t)size, fp);
    fclose(fp);

    if (bytes_read != (size_t)size) {
        free(data);
        *out_len = 0;
        return strdup("Failed to read file");
    }

    data[size] = '\0';

    /* Strip UTF-8 BOM (EF BB BF) if present */
    if (size >= 3 &&
        (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        memmove(data, data + 3, size - 3);
        size -= 3;
        data[size] = '\0';
    }

    *out_len = (size_t)size;
    return data;
}
#endif /* _WIN32 */

int mp_executor_run(const mp_request_t *req, mp_exec_result_t *result)
{
    result->data = NULL;
    result->len  = 0;

    int rc = 0;

    switch (req->type) {
        case REQUEST_TYPE_LS: {
#ifdef _WIN32
            char *listing = list_directory(req->arg, &result->len);
            if (listing) {
                result->data = listing;
                rc = 0;
            } else {
                set_error_msg(result, "Failed to list directory");
                rc = -1;
            }
#else
            char *const args[] = { "ls", "-la", req->arg, NULL };
            char *const *argv = args;
            rc = os_exec_capture(argv, EXECUTOR_TIMEOUT_MS,
                                 &result->data, &result->len);
#endif
            break;
        }

        case REQUEST_TYPE_PWD: {
#ifdef _WIN32
            char cwd[4096];
            if (_getcwd(cwd, sizeof(cwd)) != NULL) {
                size_t len = strlen(cwd);
                result->data = malloc(len + 1);
                if (result->data) {
                    memcpy(result->data, cwd, len + 1);
                    result->len = len;
                    rc = 0;
                } else {
                    set_error_msg(result, "Memory allocation failed");
                    rc = -1;
                }
            } else {
                set_error_msg(result, "Failed to get current directory");
                rc = -1;
            }
#else
            char *const args[] = { "pwd", NULL };
            char *const *argv = args;
            rc = os_exec_capture(argv, EXECUTOR_TIMEOUT_MS,
                                 &result->data, &result->len);
#endif
            break;
        }

        case REQUEST_TYPE_CAT: {
#ifdef _WIN32
            char *content = read_file_content(req->arg, &result->len);
            if (content) {
                result->data = content;
                /* Check if it's an error string */
                if (strncmp(content, "File not found", 14) == 0 ||
                    strncmp(content, "Failed to", 9) == 0 ||
                    strncmp(content, "Memory allocation", 17) == 0 ||
                    strncmp(content, "Error", 5) == 0) {
                    rc = -1;   /* still send the error message */
                } else {
                    rc = 0;    /* success */
                }
            } else {
                set_error_msg(result, "Unknown read error");
                rc = -1;
            }
#else
            char *const args[] = { "cat", req->arg, NULL };
            char *const *argv = args;
            rc = os_exec_capture(argv, EXECUTOR_TIMEOUT_MS,
                                 &result->data, &result->len);
#endif
            break;
        }

        default:
            set_error_msg(result, "Unknown request type");
            return -1;
    }

    /* If execution failed and we have no data, fallback to generic error */
    if (rc != 0 && result->data == NULL) {
        set_error_msg(result, "Command execution failed");
        return -1;
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