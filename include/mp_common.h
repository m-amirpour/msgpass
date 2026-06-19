#ifndef MP_COMMON_H
#define MP_COMMON_H

/*
 * Central header that every translation unit includes first.
 * Establishes platform detection, common types, and the few
 * cross-platform primitives that don't belong anywhere else.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "mp_portable.h" /* portable printing macros and helpers */

#ifdef _WIN32
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <afunix.h>

   typedef SOCKET          mp_socket_t;
   typedef int             mp_socklen_t;
   typedef SSIZE_T         ssize_t;

#  define MP_INVALID_SOCKET INVALID_SOCKET
#  define MP_SOCKET_ERROR   SOCKET_ERROR
#  define mp_close_socket   closesocket
#  define mp_sleep_ms(ms)   Sleep(ms)

#else
   /* Linux / POSIX – only define if not already defined */
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE
#  endif

#  include <unistd.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <sys/wait.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <poll.h>
#  include <sys/select.h>
#  include <pthread.h>

   typedef int             mp_socket_t;
   typedef socklen_t       mp_socklen_t;

#  define MP_INVALID_SOCKET (-1)
#  define MP_SOCKET_ERROR   (-1)
#  define mp_close_socket   close
#  define mp_sleep_ms(ms)   usleep((ms) * 1000)
#endif

/*
 * Attribute macros — I use these sparingly. They're only worth
 * adding when they catch real bugs at the call site.
 */
#if defined(__GNUC__) || defined(__clang__)
#  define MP_NODISCARD    __attribute__((warn_unused_result))
#  define MP_PRINTF(f, a) __attribute__((format(printf, f, a)))
#  define MP_NORETURN     __attribute__((noreturn))
#else
#  define MP_NODISCARD
#  define MP_PRINTF(f, a)
#  define MP_NORETURN
#endif

/* Shutdown flag. Written only by a signal handler or ctrl handler,
 * read from the event loop. Volatile is sufficient here. */
#ifdef _WIN32
typedef volatile LONG       mp_shutdown_flag_t;
#  define MP_FLAG_SET(f)    InterlockedExchange(&(f), 1)
#  define MP_FLAG_GET(f)    InterlockedCompareExchange(&(f), 0, 0)
#else
typedef volatile sig_atomic_t  mp_shutdown_flag_t;
#  define MP_FLAG_SET(f)       ((f) = 1)
#  define MP_FLAG_GET(f)       (f)
#endif

#define MP_UNUSED(x) ((void)(x))

/* Bounds everyone needs to know about. */
#define MP_MAX_FRAME_LEN    65535u
#define MP_REQ_HDR_LEN      6u
#define MP_RESP_HDR_LEN     2u

#endif /* MP_COMMON_H */