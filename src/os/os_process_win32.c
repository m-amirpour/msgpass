#ifdef _WIN32

#include "os/os_process.h"
#include "os/os_time.h"
#include "mp_buf.h"
#include "mp_log.h"

static void set_error(char **out_data, size_t *out_len, const char *msg)
{
    free(*out_data);
    size_t n = strlen(msg);
    char *p  = malloc(n + 1);
    if (p) memcpy(p, msg, n + 1);
    *out_data = p;
    *out_len  = p ? n : 0;
}

static char *build_cmdline(char *const argv[])
{
    size_t total = 0;
    for (int i = 0; argv[i]; i++)
        total += strlen(argv[i]) + 3; /* '"arg" ' */

    char *cmd = malloc(total + 1);
    if (!cmd) return NULL;

    char *p = cmd;
    for (int i = 0; argv[i]; i++) {
        if (i > 0) *p++ = ' ';
        *p++ = '"';
        size_t l = strlen(argv[i]);
        memcpy(p, argv[i], l);
        p += l;
        *p++ = '"';
    }
    *p = '\0';
    return cmd;
}

int os_exec_capture(char *const argv[],
                    int         timeout_ms,
                    char      **out_data,
                    size_t     *out_len)
{
    *out_data = NULL;
    *out_len  = 0;

    HANDLE hRead = NULL, hWrite = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        set_error(out_data, out_len, "Command failed: pipe error");
        return -1;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    char *cmdline = build_cmdline(argv);
    if (!cmdline) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        set_error(out_data, out_len, "Command failed: memory error");
        return -1;
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;   /* stderr goes to the same pipe */

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdline);
    CloseHandle(hWrite);

    if (!ok) {
        CloseHandle(hRead);
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND)
            set_error(out_data, out_len, "Command not found");
        else
            set_error(out_data, out_len, "Command failed");
        return -1;
    }

    mp_buf_t output;
    char     rbuf[8192];
    mp_buf_init(&output);

    int64_t deadline = os_monotonic_ms() + timeout_ms;

    for (;;) {
        int64_t remaining = deadline - os_monotonic_ms();
        if (remaining <= 0) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(hRead);
            mp_buf_free(&output);
            set_error(out_data, out_len, "Command timeout");
            return -1;
        }

        DWORD avail = 0;
        if (!PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL))
            break;

        if (avail > 0) {
            DWORD toread = (avail > sizeof(rbuf)) ? (DWORD)sizeof(rbuf) : avail;
            DWORD nread  = 0;
            if (ReadFile(hRead, rbuf, toread, &nread, NULL) && nread > 0)
                if (!mp_buf_append(&output, rbuf, nread)) {
                    return -1;
                }
        } else {
            DWORD wait_ms = (DWORD)(remaining > 50 ? 50 : remaining);
            if (WaitForSingleObject(pi.hProcess, wait_ms) == WAIT_OBJECT_0) {
                /* +++ FIX: Drain remaining data using plain ReadFile until pipe closes +++ */
                for (;;) {
                    DWORD nr = 0;
                    if (!ReadFile(hRead, rbuf, sizeof(rbuf), &nr, NULL) || nr == 0)
                        break;
                    if (!mp_buf_append(&output, rbuf, nr)) {
                        return -1;
                    }
                }
                break;
            }
        }
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    if (exit_code != 0) {
        if (output.len == 0) {
            mp_buf_free(&output);
            set_error(out_data, out_len, "Command failed");
            return -1;
        }
        *out_data = (char *)mp_buf_detach(&output, out_len);
        return -1;   /* returns captured error message in out_data */
    }

    if (output.len == 0) {
        mp_buf_free(&output);
        char *empty = malloc(1);
        if (empty) *empty = '\0';
        *out_data = empty;
        *out_len  = 0;
    } else {
        *out_data = (char *)mp_buf_detach(&output, out_len);
    }
    return 0;
}

#endif /* _WIN32 */