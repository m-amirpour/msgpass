#ifndef _WIN32

#define _POSIX_C_SOURCE 200809L

#include "os/os_thread.h"
#include "mp_log.h"

#include <pthread.h>

typedef struct {
    os_thread_fn_t fn;
    void          *arg;
} trampoline_t;

static void *thread_entry(void *ctx)
{
    trampoline_t t = *(trampoline_t *)ctx;
    free(ctx);
    t.fn(t.arg);
    return NULL;
}

int os_thread_spawn_detached(os_thread_fn_t fn, void *arg)
{
    trampoline_t *t = malloc(sizeof(*t));
    if (!t) {
        LOG_ERROR("malloc trampoline: %s", strerror(errno));
        return -1;
    }
    t->fn  = fn;
    t->arg = arg;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_t tid;
    int rc = pthread_create(&tid, &attr, thread_entry, t);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        LOG_ERROR("pthread_create: %s", strerror(rc));
        free(t);
        return -1;
    }
    return 0;
}

#endif /* !_WIN32 */