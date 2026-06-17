#include "os/os_time.h"

#ifdef _WIN32

int64_t os_monotonic_ms(void)
{
    return (int64_t)GetTickCount64();
}

#else

#define _POSIX_C_SOURCE 200809L
#include <time.h>

int64_t os_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#endif