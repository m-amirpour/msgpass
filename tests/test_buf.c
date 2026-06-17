#include "test_runner.h"
#include "mp_buf.h"

TEST(init_and_free)
{
    mp_buf_t b;
    mp_buf_init(&b);
    ASSERT_NULL(b.data);
    ASSERT_EQ(b.len, (size_t)0);
    ASSERT_EQ(b.cap, (size_t)0);
    mp_buf_free(&b);
    ASSERT_NULL(b.data);
    TEST_OK();
}

TEST(append_grows)
{
    mp_buf_t b;
    mp_buf_init(&b);

    ASSERT_EQ(mp_buf_append(&b, "hello", 5), 0);
    ASSERT_EQ(mp_buf_append(&b, " world", 6), 0);
    ASSERT_EQ(b.len, (size_t)11);
    ASSERT_EQ(memcmp(b.data, "hello world", 11), 0);

    mp_buf_free(&b);
    TEST_OK();
}

TEST(reset_keeps_capacity)
{
    mp_buf_t b;
    mp_buf_init(&b);
    mp_buf_append(&b, "some data", 9);

    size_t cap = b.cap;
    mp_buf_reset(&b);
    ASSERT_EQ(b.len, (size_t)0);
    ASSERT_EQ(b.cap, cap);
    ASSERT_NOTNULL(b.data);

    mp_buf_free(&b);
    TEST_OK();
}

TEST(detach_transfers_ownership)
{
    mp_buf_t b;
    mp_buf_init(&b);
    mp_buf_append(&b, "abc", 3);

    size_t   out_len = 0;
    uint8_t *p       = mp_buf_detach(&b, &out_len);

    ASSERT_NOTNULL(p);
    ASSERT_EQ(out_len, (size_t)3);
    ASSERT_NULL(b.data);
    ASSERT_EQ(b.len, (size_t)0);
    ASSERT_EQ(memcmp(p, "abc", 3), 0);

    free(p);
    TEST_OK();
}

TEST(large_append)
{
    mp_buf_t b;
    mp_buf_init(&b);

    for (int i = 0; i < 100000; i++) {
        uint8_t byte = (uint8_t)(i & 0xFF);
        ASSERT_EQ(mp_buf_append(&b, &byte, 1), 0);
    }

    ASSERT_EQ(b.len, (size_t)100000);
    for (int i = 0; i < 100000; i++) {
        ASSERT_EQ(b.data[i], (uint8_t)(i & 0xFF));
    }

    mp_buf_free(&b);
    TEST_OK();
}

int main(void)
{
    printf("=== Buffer Tests ===\n");
    RUN(init_and_free);
    RUN(append_grows);
    RUN(reset_keeps_capacity);
    RUN(detach_transfers_ownership);
    RUN(large_append);
    TEST_SUMMARY();
    TEST_RETURN();
}