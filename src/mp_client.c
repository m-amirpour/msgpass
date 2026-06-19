/*
 * Command-line client.
 *
 * Modes:
 *   Single request via argv:
 *     msgpass_client -p 8080 PWD
 *     msgpass_client -s /tmp/msgpass.sock LS /home
 *
 *   Batch mode from stdin (one command per line):
 *     echo "LS /tmp" | msgpass_client -p 8080
 *     msgpass_client -p 8080 < commands.txt
 *
 * Stdin line format: COMMAND [ARG]
 */

#include "mp_common.h"
#include "mp_protocol.h"
#include "mp_buf.h"
#include "mp_log.h"
#include "mp_portable.h"
#include "os/os_socket.h"

#ifdef _WIN32
#  include <io.h>      /* _isatty, _fileno */
#endif

typedef enum {
    CONN_UNIX = 0,
    CONN_TCP  = 1
} conn_type_t;

typedef struct {
    conn_type_t  conn_type;
    int          tcp_port;
    char         sock_path[256];
} conn_opts_t;

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s [opts] COMMAND [ARG]     single request\n"
        "  %s [opts]                   read commands from stdin\n"
        "\n"
        "Options:\n"
        "  -s <path>   connect via UNIX socket (default: /tmp/msgpass.sock)\n"
        "  -p <port>   connect via TCP (127.0.0.1)\n"
        "  -v          verbose logging\n"
        "\n"
        "Commands:\n"
        "  LS <dir>    list directory\n"
        "  PWD         print working directory\n"
        "  CAT <file>  print file contents\n"
        "\n"
        "Stdin format (one per line):\n"
        "  LS /some/path\n"
        "  PWD\n"
        "  CAT /etc/hostname\n",
        name, name);
}

static int parse_command_str(const char *cmd_str,
                              uint16_t   *req_type_out)
{
    if (strcmp(cmd_str, "LS") == 0)       { *req_type_out = REQUEST_TYPE_LS;  return 0; }
    if (strcmp(cmd_str, "PWD") == 0)      { *req_type_out = REQUEST_TYPE_PWD; return 0; }
    if (strcmp(cmd_str, "CAT") == 0)      { *req_type_out = REQUEST_TYPE_CAT; return 0; }
    return -1;
}

static mp_socket_t do_connect(const conn_opts_t *opts)
{
    if (opts->conn_type == CONN_TCP)
        return os_connect_tcp("127.0.0.1", opts->tcp_port);
    return os_connect_unix(opts->sock_path);
}

/*
 * send_one_request() — Connect, send a single request, receive and print
 * the response, then close. Returns 0 on success.
 */
static int send_one_request(const conn_opts_t *opts,
                             uint16_t           req_type,
                             const char        *arg_str)
{
    const char *arg_for_encode = arg_str ? arg_str : "";
    uint16_t    arg_len        = arg_str ? (uint16_t)strlen(arg_str) : 0;

    if (arg_str && strlen(arg_str) > PROTO_MAX_ARG_LEN) {
        fprintf(stderr, "Error: argument too long\n");
        return -1;
    }

    mp_socket_t fd = do_connect(opts);
    if (fd == MP_INVALID_SOCKET) {
        LOG_ERROR("connection failed");
        return -1;
    }

    /* Encode request */
    mp_buf_t req_buf;
    mp_buf_init(&req_buf);

    if (proto_encode_request(req_type, arg_for_encode, arg_len, &req_buf) != 0) {
        LOG_ERROR("encode request failed");
        mp_buf_free(&req_buf);
        os_close_socket(fd);
        return -1;
    }

    LOG_DEBUG("sending %u byte request: type=%u arg='%s'",
              (unsigned)req_buf.len, (unsigned)req_type,
              arg_str ? arg_str : "");

    ssize_t sent = os_send_all(fd, req_buf.data, req_buf.len);
    mp_buf_free(&req_buf);

    if (sent < 0) {
        LOG_ERROR("send failed");
        os_close_socket(fd);
        return -1;
    }

    /* Read response header */
    uint8_t resp_hdr[MP_RESP_HDR_LEN];
    ssize_t nr = os_recv_all(fd, resp_hdr, sizeof(resp_hdr));
    if (nr != (ssize_t)sizeof(resp_hdr)) {
        LOG_ERROR("failed to read response header (got " MP_FSIZED ")", MP_CAST_SSIZE(nr));
        os_close_socket(fd);
        return -1;
    }

    uint16_t wire_resp_len;
    memcpy(&wire_resp_len, resp_hdr, 2);
    uint16_t resp_total = ntohs_uint16(wire_resp_len);

    if (resp_total < MP_RESP_HDR_LEN) {
        LOG_ERROR("malformed response length: %u", (unsigned)resp_total);
        os_close_socket(fd);
        return -1;
    }

    uint16_t data_len = (uint16_t)(resp_total - MP_RESP_HDR_LEN);

    /* Read and print response data */
    int ret = 0;
    if (data_len > 0) {
        uint8_t *data = malloc(data_len);
        if (!data) {
            LOG_ERROR("malloc failed");
            os_close_socket(fd);
            return -1;
        }

        nr = os_recv_all(fd, data, data_len);
        if (nr > 0) {
            fwrite(data, 1, (size_t)nr, stdout);
            fflush(stdout);
        }
        if (nr != (ssize_t)data_len) {
            LOG_WARN("short response: got " MP_FSIZED " of %u",
                     MP_CAST_SSIZE(nr), (unsigned)data_len);
            ret = -1;
        }
        free(data);
    }

    os_close_socket(fd);
    return ret;
}

/*
 * run_stdin_mode() — Read commands from stdin, one per line.
 * Format: COMMAND [ARG]
 */
static int run_stdin_mode(const conn_opts_t *opts)
{
    char    line[1024];
    int     errors = 0;

    while (fgets(line, sizeof(line), stdin)) {
        /* Strip trailing newline and carriage return */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue;

        /* Tokenise: first token is command, rest is argument */
        char *saveptr = NULL;
#ifdef _WIN32
        char *cmd_tok = strtok_s(line, " \t", &saveptr);
        char *arg_tok = strtok_s(NULL,  "",    &saveptr);
#else
        char *cmd_tok = strtok_r(line, " \t", &saveptr);
        char *arg_tok = strtok_r(NULL,  "",    &saveptr);
#endif
        if (!cmd_tok) continue;

        /* Strip leading whitespace from arg */
        if (arg_tok) {
            while (*arg_tok == ' ' || *arg_tok == '\t') arg_tok++;
            if (*arg_tok == '\0') arg_tok = NULL;
        }

        uint16_t req_type;
        if (parse_command_str(cmd_tok, &req_type) != 0) {
            fprintf(stderr, "stdin: unknown command: %s\n", cmd_tok);
            errors++;
            continue;
        }

        if ((req_type == REQUEST_TYPE_LS || req_type == REQUEST_TYPE_CAT) && !arg_tok) {
            fprintf(stderr, "stdin: %s requires an argument\n", cmd_tok);
            errors++;
            continue;
        }

        if (req_type == REQUEST_TYPE_PWD)
            arg_tok = NULL;

        if (send_one_request(opts, req_type, arg_tok) != 0)
            errors++;
    }

    return errors == 0 ? 0 : -1;
}

int main(int argc, char *argv[])
{
    if (os_net_init() != 0) {
        fprintf(stderr, "Network init failed\n");
        return 1;
    }

#ifndef _WIN32
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sa, NULL);
    }
#endif

    conn_opts_t opts;
    opts.conn_type = CONN_UNIX;
    opts.tcp_port  = 9000;
    strncpy(opts.sock_path, "/tmp/msgpass.sock", sizeof(opts.sock_path) - 1);
    opts.sock_path[sizeof(opts.sock_path) - 1] = '\0';

    int cmd_idx = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(opts.sock_path, argv[++i], sizeof(opts.sock_path) - 1);
            opts.sock_path[sizeof(opts.sock_path) - 1] = '\0';
            opts.conn_type = CONN_UNIX;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            opts.tcp_port  = atoi(argv[++i]);
            opts.conn_type = CONN_TCP;
        } else if (strcmp(argv[i], "-v") == 0) {
            mp_log_set_level(MP_LOG_DEBUG);
        } else if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            os_net_cleanup();
            return 0;
        } else if (argv[i][0] != '-') {
            cmd_idx = i;
            break;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            os_net_cleanup();
            return 1;
        }
    }

    int ret = 0;

    if (cmd_idx < 0) {
        /*
         * No command on argv. If stdin is not a terminal, read commands
         * from it. Otherwise print usage.
         */
#ifdef _WIN32
        int stdin_is_pipe = !_isatty(_fileno(stdin));
#else
        int stdin_is_pipe = !isatty(fileno(stdin));
#endif
        if (stdin_is_pipe) {
            ret = run_stdin_mode(&opts);
        } else {
            usage(argv[0]);
            os_net_cleanup();
            return 1;
        }
    } else {
        /* Single command from argv */
        const char *cmd_str = argv[cmd_idx];
        const char *arg_str = (cmd_idx + 1 < argc) ? argv[cmd_idx + 1] : NULL;

        uint16_t req_type;
        if (parse_command_str(cmd_str, &req_type) != 0) {
            fprintf(stderr, "Unknown command: %s\n", cmd_str);
            usage(argv[0]);
            os_net_cleanup();
            return 1;
        }

        if ((req_type == REQUEST_TYPE_LS || req_type == REQUEST_TYPE_CAT) && !arg_str) {
            fprintf(stderr, "Error: %s requires an argument\n", cmd_str);
            os_net_cleanup();
            return 1;
        }

        if (req_type == REQUEST_TYPE_PWD)
            arg_str = NULL;

        ret = send_one_request(&opts, req_type, arg_str);
    }

    os_net_cleanup();
    return ret == 0 ? 0 : 1;
}