#ifndef OS_THREAD_H
#define OS_THREAD_H

#include "mp_common.h"

typedef void (*os_thread_fn_t)(void *arg);

/*
 * Spawn a detached thread running fn(arg). The thread owns arg
 * and is responsible for freeing it.
 * Returns 0 on success, -1 on failure.
 */
MP_NODISCARD int os_thread_spawn_detached(os_thread_fn_t fn, void *arg);

#endif /* OS_THREAD_H */