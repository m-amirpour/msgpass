#ifdef _WIN32

#include "os/os_process.h"
#include "os/os_time.h"
#include "mp_buf.h"
#include "mp_log.h"
#include "mp_portable.h"

static void set_error(char **out_data, size_t *out_len, const char *msg)
{
    free(*out_data);
    *out_data = NULL;
    size_t n = strlen(msg);
    char *p  = malloc(n + 1);
    if (p) memcpy(p, msg, n + 1);
    *out_data = p;
    *out_len  = p ? n : 0;
}

/*
 * Build a command line string from argv[].
 *
 * The first element (argv[0]) determines the quoting strategy:
 * - If it's "cmd" or "cmd.exe", we use cmd.exe semantics:
 *   cmd /c dir "C:\some path"
 *   Only arguments with spaces get quoted. The /c flag and command
 *   name must NOT be quoted individually.
 *
 * - Otherwise, we quote each argument with double quotes.
 *   This handles most executables correctly.
 */
static char *build_cmdline(char *const argv[])
{
    if (!argv[0]) return NULL;

    /* Check if we're running through cmd.exe */
    int is_cmd = (_stricmp(argv[0], "cmd") == 0 ||
                  _stricmp(argv[0], "cmd.exe") == 0);

    /* First pass: compute total length needed */
    size_t total = 0;
    for (int i = 0; argv[i]; i++) {
        total += strlen(argv[i]) + 3; /* worst case: "arg" + space */
    }

    char *cmd = malloc(total + 1);
    if (!cmd) return NULL;

    char *p = cmd;

    if (is_cmd) {
        /*
         * For cmd.exe, build the line as:
         *   cmd /c somecommand arg1 "arg with spaces"
         *
         * Don't quote /c or the command name. Only quote arguments
         * that contain spaces.
         */
        for (int i = 0; argv[i]; i++) {
            if (i > 0) *p++ = ' ';

            int needs_quotes = (i >= 3 && strchr(argv[i], ' ') != NULL);

            if (needs_quotes) *p++ = '"';
            size_t len = strlen(argv[i]);
            memcpy(p, argv[i], len);
            p += len;
            if (needs_quotes) *p++ = '"';
        }
    } else {
        /*
         * For normal executables, quote every argument.
         * This is the standard Windows convention.
         */
        for (int i = 0; argv[i]; i++) {
            if (i > 0) *p++ = ' ';
            *p++ = '"';
            size_t len = strlen(argv[i]);
            memcpy(p, argv[i], len);
            p += len;
            *p++ = '"';
        }
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
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        set_error(out_data, out_len, "Command failed: pipe error");
        return -1;
    }
    /* Read end must not be inherited by the child */
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    char *cmdline = build_cmdline(argv);
    if (!cmdline) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        set_error(out_data, out_len, "Command failed: memory error");
        return -1;
    }

    LOG_DEBUG("executing: %s", cmdline);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessA(
        NULL,              /* application name (use cmdline) */
        cmdline,           /* command line */
        NULL,              /* process security */
        NULL,              /* thread security */
        TRUE,              /* inherit handles */
        CREATE_NO_WINDOW,  /* don't flash a console window */
        NULL,              /* environment */
        NULL,              /* current directory */
        &si,
        &pi
    );

    free(cmdline);

    /* Close write end in parent immediately so reads will see EOF */
    CloseHandle(hWrite);
    hWrite = NULL;

    if (!ok) {
        DWORD err = GetLastError();
        CloseHandle(hRead);
        LOG_WARN("CreateProcess failed: error %lu", (unsigned long)err);
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            set_error(out_data, out_len, "Command not found");
        else
            set_error(out_data, out_len, "Command failed");
        return -1;
    }

    /* Read child stdout with timeout */
    mp_buf_t output;
    char     rbuf[8192];
    mp_buf_init(&output);

    int64_t deadline = os_monotonic_ms() + timeout_ms;

    for (;;) {
        int64_t remaining = deadline - os_monotonic_ms();
        if (remaining <= 0) {
            LOG_WARN("command timed out, terminating process");
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
                if (mp_buf_append(&output, rbuf, nread) != 0) {
                    LOG_ERROR("buf_append failed during read");
                    break;
                }
        } else {
            /* No data available — wait a bit for process or more data */
            DWORD wait_ms = (DWORD)(remaining > 50 ? 50 : remaining);
            DWORD wres = WaitForSingleObject(pi.hProcess, wait_ms);
            if (wres == WAIT_OBJECT_0) {
                /* Process exited. Drain any remaining output. */
                for (;;) {
                    DWORD nr = 0;
                    if (!PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) || avail == 0)
                        break;
                    DWORD tr = (avail > sizeof(rbuf)) ? (DWORD)sizeof(rbuf) : avail;
                    if (!ReadFile(hRead, rbuf, tr, &nr, NULL) || nr == 0)
                        break;
                    if (mp_buf_append(&output, rbuf, nr) != 0) {
                        LOG_ERROR("buf_append failed during drain");
                        break;
                    }
                }
                break;
            }
        }
    }

    CloseHandle(hRead);

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    LOG_DEBUG("process exited with code %lu, captured " MP_FSIZE " bytes",
              (unsigned long)exit_code, MP_CAST_SIZE(output.len));

    if (exit_code != 0) {
        if (output.len == 0) {
            mp_buf_free(&output);
            set_error(out_data, out_len, "Command failed");
            return -1;
        }
        /* Return the output even on failure — it may contain an error message */
        *out_data = (char *)mp_buf_detach(&output, out_len);
        return -1;
    }

    /* Success */
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