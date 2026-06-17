#ifndef OS_TIME_H
#define OS_TIME_H

#include "mp_common.h"

/* Monotonic millisecond clock. Safe to use for timeout math. */
int64_t os_monotonic_ms(void);

#endif /* OS_TIME_H */