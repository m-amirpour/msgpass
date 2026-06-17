#include "test_runner.h"
#include "mp_protocol.h"
#include "mp_buf.h"

TEST(htons_ntohs_roundtrip)
{
    uint16_t values[] = { 0, 1, 0xFF, 0x100, 0x1234, 0x7FFF, 0xFFFF };
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
        ASSERT_EQ(ntohs_uint16(htons_uint16(values[i])), values[i]);
    }
    TEST_OK();
}

TEST(validate_header_min_len)
{
    const char *err = NULL;
    ASSERT_EQ(proto_validate_header(5, REQUEST_TYPE_PWD, 0, &err), -1);
    ASSERT_NOTNULL(err);
    TEST_OK();
}

TEST(validate_header_arg_len_mismatch)
{
    const char *err = NULL;
    ASSERT_EQ(proto_validate_header(10, REQUEST_TYPE_LS, 3, &err), -1);
    TEST_OK();
}

TEST(validate_header_unknown_type)
{
    const char *err = NULL;
    ASSERT_EQ(proto_validate_header(6, 99, 0, &err), -1);
    TEST_OK();
}

TEST(validate_header_pwd_ok)
{
    const char *err = NULL;
    ASSERT_EQ(proto_validate_header(6, REQUEST_TYPE_PWD, 0, &err), 0);
    TEST_OK();
}

TEST(validate_header_ls_with_arg)
{
    const char *err = NULL;
    ASSERT_EQ(proto_validate_header(11, REQUEST_TYPE_LS, 5, &err), 0);
    TEST_OK();
}

TEST(encode_decode_pwd)
{
    mp_buf_t buf;
    mp_buf_init(&buf);

    ASSERT_EQ(proto_encode_request(REQUEST_TYPE_PWD, NULL, 0, &buf), 0);
    ASSERT_EQ(buf.len, (size_t)6);

    uint16_t wl, wt, wa;
    memcpy(&wl, buf.data + 0, 2);
    memcpy(&wt, buf.data + 2, 2);
    memcpy(&wa, buf.data + 4, 2);
    ASSERT_EQ(ntohs_uint16(wl), 6);
    ASSERT_EQ(ntohs_uint16(wt), REQUEST_TYPE_PWD);
    ASSERT_EQ(ntohs_uint16(wa), 0);

    mp_buf_free(&buf);
    TEST_OK();
}

TEST(encode_decode_ls_with_arg)
{
    mp_buf_t buf;
    mp_buf_init(&buf);

    const char *arg = "/home/user";
    uint16_t    alen = (uint16_t)strlen(arg);

    ASSERT_EQ(proto_encode_request(REQUEST_TYPE_LS, arg, alen, &buf), 0);
    ASSERT_EQ(buf.len, (size_t)(6 + alen));
    ASSERT_EQ(memcmp(buf.data + 6, arg, alen), 0);

    mp_buf_free(&buf);
    TEST_OK();
}

TEST(encode_response_empty)
{
    mp_buf_t buf;
    mp_buf_init(&buf);

    ASSERT_EQ(proto_encode_response(NULL, 0, &buf), 0);
    ASSERT_EQ(buf.len, (size_t)2);

    uint16_t wl;
    memcpy(&wl, buf.data, 2);
    ASSERT_EQ(ntohs_uint16(wl), 2);

    mp_buf_free(&buf);
    TEST_OK();
}

TEST(encode_response_with_data)
{
    mp_buf_t buf;
    mp_buf_init(&buf);

    const char *data = "hello world";
    size_t      dlen = strlen(data);

    ASSERT_EQ(proto_encode_response(data, dlen, &buf), 0);
    ASSERT_EQ(buf.len, 2 + dlen);
    ASSERT_EQ(memcmp(buf.data + 2, data, dlen), 0);

    mp_buf_free(&buf);
    TEST_OK();
}

TEST(request_alloc_and_free)
{
    mp_request_t *req = proto_request_alloc(REQUEST_TYPE_CAT,
                                            "/etc/passwd", 11);
    ASSERT_NOTNULL(req);
    ASSERT_EQ(req->type, REQUEST_TYPE_CAT);
    ASSERT_EQ(req->arg_len, 11);
    ASSERT_STREQ(req->arg, "/etc/passwd");
    proto_request_free(req);
    proto_request_free(NULL);  /* should not crash */
    TEST_OK();
}

int main(void)
{
    printf("=== Protocol Tests ===\n");
    RUN(htons_ntohs_roundtrip);
    RUN(validate_header_min_len);
    RUN(validate_header_arg_len_mismatch);
    RUN(validate_header_unknown_type);
    RUN(validate_header_pwd_ok);
    RUN(validate_header_ls_with_arg);
    RUN(encode_decode_pwd);
    RUN(encode_decode_ls_with_arg);
    RUN(encode_response_empty);
    RUN(encode_response_with_data);
    RUN(request_alloc_and_free);
    TEST_SUMMARY();
    TEST_RETURN();
}