#ifdef _WIN32

#include "os/os_thread.h"
#include "mp_log.h"

typedef struct {
    os_thread_fn_t fn;
    void          *arg;
} trampoline_t;

static DWORD WINAPI thread_entry(LPVOID ctx)
{
    trampoline_t t = *(trampoline_t *)ctx;
    free(ctx);
    t.fn(t.arg);
    return 0;
}

int os_thread_spawn_detached(os_thread_fn_t fn, void *arg)
{
    trampoline_t *t = malloc(sizeof(*t));
    if (!t) return -1;
    t->fn  = fn;
    t->arg = arg;

    HANDLE h = CreateThread(NULL, 0, thread_entry, t, 0, NULL);
    if (!h) {
        LOG_ERROR("CreateThread failed: %lu", GetLastError());
        free(t);
        return -1;
    }
    CloseHandle(h);
    return 0;
}

#endif /* _WIN32 */