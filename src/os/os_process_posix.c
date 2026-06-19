#ifndef _WIN32

#include "os/os_process.h"
#include "os/os_time.h"
#include "mp_buf.h"
#include "mp_log.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void set_error(char **out, size_t *len, const char *msg)
{
    free(*out);
    *out = NULL;
    size_t n = strlen(msg);
    char *p  = malloc(n + 1);
    if (p) memcpy(p, msg, n + 1);
    *out = p;
    *len = p ? n : 0;
}

int os_exec_capture(char *const argv[],
                    int         timeout_ms,
                    char      **out_data,
                    size_t     *out_len)
{
    *out_data = NULL;
    *out_len  = 0;

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        LOG_ERROR("pipe: %s", strerror(errno));
        set_error(out_data, out_len, "Command failed: pipe error");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        set_error(out_data, out_len, "Command failed: fork error");
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(126);
        close(pipefd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);

    mp_buf_t  output;
    char      rbuf[8192];
    int       timed_out = 0;
    int64_t   deadline  = os_monotonic_ms() + timeout_ms;

    mp_buf_init(&output);

    for (;;) {
        int64_t remaining = deadline - os_monotonic_ms();
        if (remaining <= 0) {
            timed_out = 1;
            break;
        }

        struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
        int pr = poll(&pfd, 1, (int)remaining);

        if (pr < 0) {
            if (errno == EINTR) continue;
            LOG_WARN("poll: %s", strerror(errno));
            break;
        }
        if (pr == 0) {
            timed_out = 1;
            break;
        }

        if (pfd.revents & POLLIN) {
            ssize_t nr = read(pipefd[0], rbuf, sizeof(rbuf));
            if (nr < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (nr == 0) break;
            if (mp_buf_append(&output, rbuf, (size_t)nr) != 0) {
                LOG_ERROR("buf_append failed during read");
                break;
            }
        }

        if (pfd.revents & (POLLHUP | POLLERR)) {
            for (;;) {
                ssize_t nr = read(pipefd[0], rbuf, sizeof(rbuf));
                if (nr <= 0) break;
                if (mp_buf_append(&output, rbuf, (size_t)nr) != 0) {
                    LOG_ERROR("buf_append failed during drain");
                    break;
                }
            }
            break;
        }
    }

    close(pipefd[0]);

    if (timed_out) {
        LOG_WARN("command timed out, killing pid %d", (int)pid);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        mp_buf_free(&output);
        set_error(out_data, out_len, "Command timeout");
        return -1;
    }

    int wstatus = 0;
    if (waitpid(pid, &wstatus, 0) < 0) {
        LOG_ERROR("waitpid: %s", strerror(errno));
        mp_buf_free(&output);
        set_error(out_data, out_len, "Command failed: waitpid error");
        return -1;
    }

    if (WIFEXITED(wstatus)) {
        int code = WEXITSTATUS(wstatus);
        if (code == 127) {
            mp_buf_free(&output);
            set_error(out_data, out_len, "Command not found");
            return -1;
        }
        if (code == 126) {
            mp_buf_free(&output);
            set_error(out_data, out_len, "Permission denied");
            return -1;
        }
        if (code != 0) {
            LOG_WARN("command exited with status %d", code);
            if (output.len == 0) {
                mp_buf_free(&output);
                set_error(out_data, out_len, "Command failed");
                return -1;
            }
            *out_data = (char *)mp_buf_detach(&output, out_len);
            return -1;
        }
    } else if (WIFSIGNALED(wstatus)) {
        LOG_WARN("command killed by signal %d", WTERMSIG(wstatus));
        mp_buf_free(&output);
        set_error(out_data, out_len, "Command killed by signal");
        return -1;
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

#endif /* !_WIN32 */