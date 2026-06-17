#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name)                                                       \
    static void test_##name(void);                                       \
    static void run_##name(void) {                                       \
        g_tests_run++;                                                   \
        printf("  %-50s", #name);                                        \
        fflush(stdout);                                                  \
        test_##name();                                                   \
    }                                                                    \
    static void test_##name(void)

#define ASSERT(cond)                                                     \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("FAIL\n    assertion failed: %s\n    at %s:%d\n",    \
                   #cond, __FILE__, __LINE__);                           \
            g_tests_failed++;                                            \
            return;                                                      \
        }                                                                \
    } while (0)

#define ASSERT_EQ(a, b)   ASSERT((a) == (b))
#define ASSERT_NE(a, b)   ASSERT((a) != (b))
#define ASSERT_NULL(p)    ASSERT((p) == NULL)
#define ASSERT_NOTNULL(p) ASSERT((p) != NULL)
#define ASSERT_STREQ(a,b) ASSERT(strcmp((a),(b)) == 0)

#define TEST_OK() do { printf("ok\n"); g_tests_passed++; } while (0)

#define RUN(name)   run_##name()

#define TEST_SUMMARY()                                                   \
    do {                                                                 \
        printf("\n%d/%d tests passed", g_tests_passed, g_tests_run);    \
        if (g_tests_failed) printf(" (%d FAILED)", g_tests_failed);     \
        printf("\n");                                                    \
    } while (0)

#define TEST_RETURN() return g_tests_failed ? 1 : 0

#endif /* TEST_RUNNER_H */