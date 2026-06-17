#include "test_runner.h"
#include "mp_executor.h"

#ifndef _WIN32

TEST(exec_echo)
{
    mp_request_t *req = proto_request_alloc(REQUEST_TYPE_LS, "/tmp", 4);
    ASSERT_NOTNULL(req);

    mp_exec_result_t result = {0};
    int rc = mp_executor_run(req, &result);

    ASSERT_EQ(rc, 0);
    ASSERT_NOTNULL(result.data);
    ASSERT(result.len > 0);

    proto_request_free(req);
    mp_exec_result_free(&result);
    TEST_OK();
}

TEST(exec_pwd)
{
    mp_request_t *req = proto_request_alloc(REQUEST_TYPE_PWD, "", 0);
    ASSERT_NOTNULL(req);

    mp_exec_result_t result = {0};
    int rc = mp_executor_run(req, &result);

    ASSERT_EQ(rc, 0);
    ASSERT_NOTNULL(result.data);
    ASSERT(result.len > 0);

    proto_request_free(req);
    mp_exec_result_free(&result);
    TEST_OK();
}

TEST(exec_cat_missing_file)
{
    mp_request_t *req = proto_request_alloc(REQUEST_TYPE_CAT,
                                            "/no/such/file/exists_xyz", 24);
    ASSERT_NOTNULL(req);

    mp_exec_result_t result = {0};
    int rc = mp_executor_run(req, &result);

    /* cat fails on missing file; we should get -1 and an error message */
    ASSERT_NE(rc, 0);
    ASSERT_NOTNULL(result.data);

    proto_request_free(req);
    mp_exec_result_free(&result);
    TEST_OK();
}

#else /* Windows */

TEST(exec_ls_win)
{
    mp_request_t *req = proto_request_alloc(REQUEST_TYPE_LS, "C:\\", 3);
    ASSERT_NOTNULL(req);

    mp_exec_result_t result = {0};
    mp_executor_run(req, &result);
    ASSERT_NOTNULL(result.data);

    proto_request_free(req);
    mp_exec_result_free(&result);
    TEST_OK();
}

#endif

int main(void)
{
    printf("=== Executor Tests ===\n");
#ifndef _WIN32
    RUN(exec_echo);
    RUN(exec_pwd);
    RUN(exec_cat_missing_file);
#else
    RUN(exec_ls_win);
#endif
    TEST_SUMMARY();
    TEST_RETURN();
}