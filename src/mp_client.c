/*
 * Command-line client. Connects, sends one request, prints the response.
 */

#include "mp_common.h"
#include "mp_protocol.h"
#include "mp_buf.h"
#include "mp_log.h"
#include "os/os_socket.h"
#include <string.h>
#include <ctype.h>

/* Remove leading whitespace and a UTF-8 BOM (EF BB BF) if present */
static void clean_response(char *str, size_t *len)
{
    if (!str || !len || *len == 0) return;

    /* Skip leading whitespace (space, tab, \r, \n) */
    size_t start = 0;
    while (start < *len && isspace((unsigned char)str[start])) {
        start++;
    }

    /* If there's a BOM after skipping whitespace, remove it */
    if (*len - start >= 3 &&
        (unsigned char)str[start] == 0xEF &&
        (unsigned char)str[start+1] == 0xBB &&
        (unsigned char)str[start+2] == 0xBF) {
        start += 3;  /* skip the BOM */
    }

    /* Now copy the cleaned part to the beginning */
    if (start > 0) {
        size_t remaining = *len - start;
        memmove(str, str + start, remaining);
        *len = remaining;
        str[*len] = '\0';
    }
}

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [options] COMMAND [ARG]\n"
        "  -s <path>   connect via UNIX socket\n"
        "  -p <port>   connect via TCP (127.0.0.1)\n"
        "  -v          verbose logging\n"
        "Commands: LS <dir>, PWD, CAT <file>\n"
        "Example:  %s -s /tmp/msgpass.sock LS /tmp\n",
        name, name);
}

int main(int argc, char *argv[])
{
    if (os_net_init() != 0) return 1;

#ifndef _WIN32
    {
        struct sigaction sa = {0};
        sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sa, NULL);
    }
#endif

    int  use_tcp  = 0;
    int  tcp_port = 9000;
    char sock_path[256] = "/tmp/msgpass.sock";
    int  cmd_idx  = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i+1 < argc) {
            strncpy(sock_path, argv[++i], sizeof(sock_path) - 1);
        } else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) {
            tcp_port = atoi(argv[++i]);
            use_tcp  = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            mp_log_set_level(MP_LOG_DEBUG);
        } else if (argv[i][0] != '-') {
            cmd_idx = i;
            break;
        }
    }

    if (cmd_idx < 0) {
        usage(argv[0]);
        os_net_cleanup();
        return 1;
    }

    const char *cmd_str = argv[cmd_idx];
    const char *arg_str = (cmd_idx + 1 < argc) ? argv[cmd_idx + 1] : NULL;

    uint16_t req_type;
    if (strcmp(cmd_str, "LS")  == 0) req_type = REQUEST_TYPE_LS;
    else if (strcmp(cmd_str, "PWD") == 0) req_type = REQUEST_TYPE_PWD;
    else if (strcmp(cmd_str, "CAT") == 0) req_type = REQUEST_TYPE_CAT;
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd_str);
        usage(argv[0]);
        os_net_cleanup();
        return 1;
    }

    if ((req_type == REQUEST_TYPE_LS || req_type == REQUEST_TYPE_CAT) && !arg_str) {
        fprintf(stderr, "%s requires an argument\n", cmd_str);
        os_net_cleanup();
        return 1;
    }
    if (req_type == REQUEST_TYPE_PWD)
        arg_str = NULL;

    uint16_t arg_len = arg_str ? (uint16_t)strlen(arg_str) : 0;

    /* Connect */
    mp_socket_t fd;
    if (use_tcp) {
        fd = os_connect_tcp("127.0.0.1", tcp_port);
    } else {
        fd = os_connect_unix(sock_path);
    }
    if (fd == MP_INVALID_SOCKET) {
        os_net_cleanup();
        return 1;
    }

    /* Build and send request */
    mp_buf_t req_buf;
    mp_buf_init(&req_buf);

    if (proto_encode_request(req_type, arg_str, arg_len, &req_buf) != 0) {
        LOG_ERROR("failed to encode request");
        mp_buf_free(&req_buf);
        os_close_socket(fd);
        os_net_cleanup();
        return 1;
    }

    if (os_send_all(fd, req_buf.data, req_buf.len) != (ssize_t)req_buf.len) {
        LOG_ERROR("failed to send request");
        mp_buf_free(&req_buf);
        os_close_socket(fd);
        os_net_cleanup();
        return 1;
    }
    mp_buf_free(&req_buf);

    /* Read response header */
    uint8_t resp_hdr[MP_RESP_HDR_LEN];
    ssize_t nr = os_recv_all(fd, resp_hdr, sizeof(resp_hdr));
    if (nr != (ssize_t)sizeof(resp_hdr)) {
        LOG_ERROR("failed to read response header (got %lld)", (long long)nr);
        os_close_socket(fd);
        os_net_cleanup();
        return 1;
    }

    uint16_t wire_resp_len;
    memcpy(&wire_resp_len, resp_hdr, 2);
    uint16_t resp_total = ntohs_uint16(wire_resp_len);

    if (resp_total < MP_RESP_HDR_LEN) {
        LOG_ERROR("malformed response length: %u", resp_total);
        os_close_socket(fd);
        os_net_cleanup();
        return 1;
    }

    uint16_t data_len = (uint16_t)(resp_total - MP_RESP_HDR_LEN);

    /* Read and print response data */
    int ret = 0;
    if (data_len > 0) {
        uint8_t *data = malloc(data_len);
        if (!data) {
            LOG_ERROR("malloc failed");
            os_close_socket(fd);
            os_net_cleanup();
            return 1;
        }

        nr = os_recv_all(fd, data, data_len);
        if (nr != (ssize_t)data_len) {
            LOG_WARN("short response data: got %lld of %u", (long long)nr, data_len);
            ret = 1;
        } else {
            char *str = (char *)data;
            size_t len = data_len;

            /* Clean the response: remove leading whitespace and BOM */
            clean_response(str, &len);

            /* Print the cleaned response */
            if (len > 0) {
                fwrite(str, 1, len, stdout);
                /* Add a newline if the response doesn't already end with one */
                if (str[len-1] != '\n') {
                    fwrite("\n", 1, 1, stdout);
                }
            } else {
                /* Empty response – print a newline */
                fwrite("\n", 1, 1, stdout);
            }
            fflush(stdout);
        }
        free(data);
    } else {
        /* No data – print a newline */
        fwrite("\n", 1, 1, stdout);
    }

    os_close_socket(fd);
    os_net_cleanup();
    return ret;
}