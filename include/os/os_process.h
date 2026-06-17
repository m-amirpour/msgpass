#ifndef OS_PROCESS_H
#define OS_PROCESS_H

/*
 * Child process execution with stdout capture and wall-clock timeout.
 *
 * argv is NULL-terminated as usual. On return, *out_data is heap-allocated
 * (caller must free). The function always sets *out_data to something —
 * even on failure, it's a descriptive error string.
 */

#include "mp_common.h"

MP_NODISCARD int os_exec_capture(char *const argv[],
                                 int         timeout_ms,
                                 char      **out_data,
                                 size_t     *out_len);

#endif /* OS_PROCESS_H */