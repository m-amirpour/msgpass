#ifndef MP_PORTABLE_H
#define MP_PORTABLE_H

/* Portable printf format macros and small helpers.
 *
 * Usage:
 *   LOG_INFO("len=" MP_FSIZE, MP_CAST_SIZE(len));
 *   LOG_WARN("nr=" MP_FSIZED, MP_CAST_SSIZE(nr));
 *
 * Implementation uses <inttypes.h> macros and provides conservative
 * fallbacks for older MSVC toolchains when inttypes.h isn't available.
 */

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#if defined(_MSC_VER) && !defined(__INTTYPES_H__) && !defined(__inttypes_h)
  /* Very small fallback set for MSVC when <inttypes.h> is missing.
     Map common macros to MSVC format specifiers. */
  #ifndef PRId64
    #define PRId64 "I64d"
  #endif
  #ifndef PRIu64
    #define PRIu64 "I64u"
  #endif
  #ifndef PRIdMAX
    #define PRIdMAX PRId64
  #endif
  #ifndef PRIuMAX
    #define PRIuMAX PRIu64
  #endif
  #ifndef PRIdPTR
    #if INTPTR_MAX == INT64_MAX
      #define PRIdPTR PRId64
    #else
      #define PRIdPTR "d"
    #endif
  #endif
  #ifndef PRIuPTR
    #if UINTPTR_MAX == UINT64_MAX
      #define PRIuPTR PRIu64
    #else
      #define PRIuPTR "u"
    #endif
  #endif
#endif /* _MSC_VER fallback */

/* Prefer using <inttypes.h> macros when available */
#include <inttypes.h>

/* Helpers to print size_t / ssize_t portably. We cast to
 * uintmax_t/intmax_t and use PRIuMAX/PRIdMAX to avoid architecture surprises.
 */
#define MP_CAST_SIZE(x)  ((uintmax_t)(x))
#define MP_CAST_SSIZE(x) ((intmax_t)(x))
#define MP_FSIZE "%" PRIuMAX
#define MP_FSIZED "%" PRIdMAX

#endif /* MP_PORTABLE_H */