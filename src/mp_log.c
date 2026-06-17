#include "mp_log.h"

#include <stdarg.h>
#include <time.h>

static mp_log_level_t g_min_level = MP_LOG_INFO;

#ifdef _WIN32
static CRITICAL_SECTION g_log_lock;
static LONG             g_lock_init = 0;

static void ensure_lock_init(void)
{
    if (InterlockedCompareExchange(&g_lock_init, 1, 0) == 0)
        InitializeCriticalSection(&g_log_lock);
}
#endif

static const char *level_name(mp_log_level_t l)
{
    switch (l) {
    case MP_LOG_DEBUG: return "DEBUG";
    case MP_LOG_INFO:  return "INFO ";
    case MP_LOG_WARN:  return "WARN ";
    case MP_LOG_ERROR: return "ERROR";
    }
    return "?????";
}

void mp_log_set_level(mp_log_level_t level) { g_min_level = level; }
mp_log_level_t mp_log_get_level(void) { return g_min_level; }

void mp_log_write(mp_log_level_t level,
                  const char    *file,
                  int            line,
                  const char    *func,
                  const char    *fmt, ...)
{
    if (level < g_min_level)
        return;

    va_list ap;
    char    tsbuf[64];

#ifdef _WIN32
    ensure_lock_init();
    EnterCriticalSection(&g_log_lock);

    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(tsbuf, sizeof(tsbuf),
         "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
         (unsigned)st.wYear,
         (unsigned)st.wMonth,
         (unsigned)st.wDay,
         (unsigned)st.wHour,
         (unsigned)st.wMinute,
         (unsigned)st.wSecond,
         (unsigned)st.wMilliseconds);

#else
    struct timespec ts;
    struct tm       tm_info;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_info);
    strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%dT%H:%M:%S", &tm_info);

    char tsbuf2[40];
    snprintf(tsbuf2, sizeof(tsbuf2), "%s.%03ld", tsbuf, ts.tv_nsec / 1000000L);

    flockfile(stderr);
    fprintf(stderr, "%s [%s] %s:%d %s(): ", tsbuf2, level_name(level), file, line, func);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    funlockfile(stderr);
    return;
#endif

    fprintf(stderr, "%s [%s] %s:%d %s(): ", tsbuf, level_name(level), file, line, func);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);

#ifdef _WIN32
    LeaveCriticalSection(&g_log_lock);
#endif
}