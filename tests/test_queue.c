#include "test_runner.h"
#include "mp_queue.h"

static mp_request_t *make_req(uint16_t type, const char *arg)
{
    uint16_t alen = arg ? (uint16_t)strlen(arg) : 0;
    return proto_request_alloc(type, arg, alen);
}

TEST(queue_init_empty)
{
    mp_queue_t q;
    mp_queue_init(&q);
    ASSERT(mp_queue_is_empty(&q));
    ASSERT_EQ(mp_queue_size(&q), (size_t)0);
    TEST_OK();
}

TEST(queue_enqueue_dequeue_single)
{
    mp_queue_t q;
    mp_queue_init(&q);

    mp_request_t *req = make_req(REQUEST_TYPE_PWD, NULL);
    ASSERT_NOTNULL(req);

    ASSERT_EQ(mp_queue_enqueue(&q, (mp_socket_t)42, req), 0);
    ASSERT(!mp_queue_is_empty(&q));
    ASSERT_EQ(mp_queue_size(&q), (size_t)1);

    mp_socket_t   fd  = 0;
    mp_request_t *out = NULL;
    ASSERT(mp_queue_dequeue(&q, &fd, &out));
    ASSERT_EQ((int)fd, 42);
    ASSERT(out == req);
    ASSERT(mp_queue_is_empty(&q));

    proto_request_free(out);
    TEST_OK();
}

TEST(queue_fifo_ordering)
{
    mp_queue_t q;
    mp_queue_init(&q);

    for (int i = 0; i < 5; i++) {
        mp_request_t *r = make_req(REQUEST_TYPE_LS, "/tmp");
        ASSERT_NOTNULL(r);
        ASSERT_EQ(mp_queue_enqueue(&q, (mp_socket_t)i, r), 0);
    }

    ASSERT_EQ(mp_queue_size(&q), (size_t)5);

    for (int i = 0; i < 5; i++) {
        mp_socket_t   fd  = 0;
        mp_request_t *req = NULL;
        ASSERT(mp_queue_dequeue(&q, &fd, &req));
        ASSERT_EQ((int)fd, i);
        proto_request_free(req);
    }

    ASSERT(mp_queue_is_empty(&q));
    TEST_OK();
}

TEST(queue_dequeue_empty_returns_false)
{
    mp_queue_t q;
    mp_queue_init(&q);

    mp_socket_t   fd  = 0;
    mp_request_t *req = NULL;
    ASSERT(!mp_queue_dequeue(&q, &fd, &req));
    TEST_OK();
}

TEST(queue_drain_releases_all)
{
    mp_queue_t q;
    mp_queue_init(&q);

    for (int i = 0; i < 10; i++) {
        mp_request_t *r = make_req(REQUEST_TYPE_CAT, "/etc/passwd");
        ASSERT_EQ(mp_queue_enqueue(&q, (mp_socket_t)i, r), 0);
    }

    ASSERT_EQ(mp_queue_size(&q), (size_t)10);
    mp_queue_drain(&q);
    ASSERT(mp_queue_is_empty(&q));
    TEST_OK();
}

TEST(queue_interleaved_ops)
{
    mp_queue_t q;
    mp_queue_init(&q);

    /* Interleave enqueue/dequeue to stress the head/tail tracking. */
    for (int round = 0; round < 4; round++) {
        for (int i = 0; i < 3; i++) {
            mp_request_t *r = make_req(REQUEST_TYPE_PWD, NULL);
            ASSERT_EQ(mp_queue_enqueue(&q, (mp_socket_t)(round * 3 + i), r), 0);
        }
        for (int i = 0; i < 3; i++) {
            mp_socket_t   fd;
            mp_request_t *req;
            ASSERT(mp_queue_dequeue(&q, &fd, &req));
            ASSERT_EQ((int)fd, round * 3 + i);
            proto_request_free(req);
        }
    }

    ASSERT(mp_queue_is_empty(&q));
    TEST_OK();
}

int main(void)
{
    printf("=== Queue Tests ===\n");
    RUN(queue_init_empty);
    RUN(queue_enqueue_dequeue_single);
    RUN(queue_fifo_ordering);
    RUN(queue_dequeue_empty_returns_false);
    RUN(queue_drain_releases_all);
    RUN(queue_interleaved_ops);
    TEST_SUMMARY();
    TEST_RETURN();
}