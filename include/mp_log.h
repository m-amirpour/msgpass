#ifndef MP_LOG_H
#define MP_LOG_H

#include "mp_common.h"

typedef enum {
    MP_LOG_DEBUG = 0,
    MP_LOG_INFO,
    MP_LOG_WARN,
    MP_LOG_ERROR
} mp_log_level_t;

void mp_log_set_level(mp_log_level_t level);
mp_log_level_t mp_log_get_level(void);

void mp_log_write(mp_log_level_t level,
                  const char    *file,
                  int            line,
                  const char    *func,
                  const char    *fmt, ...) MP_PRINTF(5, 6);

#define LOG_DEBUG(...) mp_log_write(MP_LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)  mp_log_write(MP_LOG_INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...)  mp_log_write(MP_LOG_WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...) mp_log_write(MP_LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif /* MP_LOG_H */