#include "commands.h"
#include "config.h"
#include "logging.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>

#include <sys/stat.h>

#define FAIL_IF_SERV_ERR(rsp, ret)      \
    do {                                \
        char const* _rsp = rsp;         \
        if (_rsp[0] == RSP_ERR) {       \
            print_server_error(_rsp);   \
            return ret;                 \
        }                               \
    } while (0)

static char const* program;

static void usage(FILE* stream)
{
    fprintf(stream, "Usage:\n");
    fprintf(stream, "\t%s -h\n", program);
    fprintf(stream, "\t%s [-d] HOSTNAME\n\n", program);

    fprintf(stream, "Options:\n");
    fprintf(stream, "\t-h\tShow this help message and exit.\n");
    fprintf(stream, "\t-d\tEnable debug output.\n");
}

static int print_server_error(char const* msg)
{
    return fprintf(stderr, "Server error: %s\n", &msg[1]);
}

static struct addrinfo* get_info(char const* host, char const* port)
{
    // hints for a TCP socket
    struct addrinfo const hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0
    };

    struct addrinfo* info = NULL;
    log_print("Connecting to %s:%s", host, port);

    // get the actual info needed to connect client to the server
    GAI_FAIL_IF(getaddrinfo(host, port, &hints, &info), "getaddrinfo", NULL);
    return info;
}

static int connect_to(char const* host, char const* port)
{
    struct addrinfo* info = get_info(host, port);

    int const sock = make_socket(info);
    FAIL_IF(sock < 0, "make_socket", EXIT_FAILURE);
    log_print("Created socket with file descriptor %d", sock);

    // copy the required data from the info struct and free it
    struct sockaddr const dest_addr = *info->ai_addr;
    socklen_t const dest_addrlen = info->ai_addrlen;
    freeaddrinfo(info);

    // use the provided info to connect to the given destination
    FAIL_IF(connect(sock, &dest_addr, dest_addrlen) < 0, "connect",
            EXIT_FAILURE);
    log_print("Successfully connected to %s:%s", host, port);

    return sock;
}

static int init_data(int server_sock, char const* host)
{
    char const data_ctl = cmd_get_ctl(CMD_DATA);
    Q_FAIL_IF(dprintf(server_sock, "%c\n", data_ctl) < 0, -1);

    char response[CFG_MAXLINE] = {0};
    Q_FAIL_IF(read_line(server_sock, response, CFG_MAXLINE - 1) < 0, -1);
    FAIL_IF_SERV_ERR(response, -1);

    char const* data_port = response + 1;
    return connect_to(host, data_port);
}

static int send_cmd(int server_sock, enum cmd_type cmd, char const* arg)
{
    char const code = cmd_get_ctl(cmd);

    if (arg != NULL) {
        Q_FAIL_IF(dprintf(server_sock, "%c%s\n", code, arg) < 0, EXIT_FAILURE);
    } else {
        Q_FAIL_IF(dprintf(server_sock, "%c\n", code) < 0, EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

static int handle_local_cmd(enum cmd_type cmd, char const* arg)
{
    if (cmd == CMD_LS) {
        return cmd_ls(STDOUT_FILENO);
    } else if (cmd == CMD_CD) {
        return cmd_chdir(arg);
    } else {
        log_print("Unexpected command (cmd=%d)", cmd);
        return EXIT_FAILURE;
    }
}

static int handle_remote_cmd(int server_sock, enum cmd_type cmd,
        char const* arg)
{
    FAIL_IF(send_cmd(server_sock, cmd, arg) != EXIT_SUCCESS, "send_cmd",
            EXIT_FAILURE);

    // get server response
    char response[CFG_MAXLINE] = {0};
    FAIL_IF(read_line(server_sock, response, CFG_MAXLINE - 1) < 0, "read_line",
            EXIT_FAILURE);

    if (cmd == CMD_EXIT) {
        cmd_exit(EXIT_SUCCESS);
    }

    FAIL_IF_SERV_ERR(response, EXIT_FAILURE);
    return EXIT_SUCCESS;
}

static int handle_data_cmd(int server_sock, char const* host,
        enum cmd_type cmd, char const* arg)
{
    int const data_sock = init_data(server_sock, host);
    Q_FAIL_IF(data_sock < 0, EXIT_FAILURE);
    Q_FAIL_IF(send_cmd(server_sock, cmd, arg) != EXIT_SUCCESS, EXIT_FAILURE);

    char response[CFG_MAXLINE] = {0};
    Q_FAIL_IF(read_line(server_sock, response, CFG_MAXLINE - 1) < 0, 
              EXIT_FAILURE);

    if (cmd == CMD_GET) {
        int const dest_fd = open(basename_of(arg), O_CREAT | O_EXCL);
        Q_FAIL_IF(dest_fd < 0, EXIT_FAILURE);

        if (send_file(dest_fd, data_sock) < 0) {
            perror("send_file");
            close(dest_fd);
            return EXIT_FAILURE;
        }

        close(dest_fd);
    } else if (cmd == CMD_SHOW) {
        Q_FAIL_IF(send_file(STDOUT_FILENO, data_sock) < 0, EXIT_FAILURE);
    } else if (cmd == CMD_PUT) {
        int const src_fd = open(arg, O_RDONLY);
        Q_FAIL_IF(src_fd < 0, EXIT_FAILURE);

        if (send_file(data_sock, src_fd) < 0) {
            int const old_errno = errno;
            close(src_fd);
            errno = old_errno;

            return EXIT_FAILURE;
        }

        close(src_fd);
    }

    return EXIT_SUCCESS;
}

static int run_command(int server_sock, char const* host, char const* msg)
{
    char const* arg;
    enum cmd_type cmd = cmd_parse(msg, &arg);

    if (cmd == CMD_INVALID) {
        printf("Unrecognized command: \"%s\"\n", msg);
        return EXIT_FAILURE;
    }

    if (!cmd_is_remote(cmd)) {
        return handle_local_cmd(cmd, arg);
    } else if (!cmd_needs_data(cmd)) {
        return handle_remote_cmd(server_sock, cmd, arg);
    } else {
        return handle_data_cmd(server_sock, host, cmd, arg);
    }
}

static int client_run(char const* hostname)
{
    int const sock = connect_to(hostname, AS_STR(CFG_PORT));
    FAIL_IF(sock < 0, "connect_to", EXIT_FAILURE);

    while (true) {
        printf(CFG_PROMPT);
        fflush(stdout);

        char buf[CFG_MAXLINE] = {0};
        FAIL_IF(fgets(buf, CFG_MAXLINE, stdin) == NULL, "fgets", EXIT_FAILURE);
        size_t const buf_len = strlen(buf);

        if (buf[buf_len - 1] == '\n') {
            buf[buf_len - 1] = '\0';
        }

        int const status = run_command(sock, hostname, buf);
        log_print("Command status: %d", status);
    }
}

int main(int argc, char** argv)
{
    program = argv[0];

    int opt;

    while ((opt = getopt(argc, argv, "dh")) != -1) {
        switch (opt) {
        case 'd':
            log_set_debug(true);
            break;
        case 'h':
            usage(stdout);
            return EXIT_SUCCESS;
        case '?':
        default:
            usage(stderr);
            return EXIT_FAILURE;
        }
    }

    if (optind != argc - 1) {
        usage(stderr);
        return EXIT_FAILURE;
    }

    char const* hostname = argv[optind];
    return client_run(hostname);
}
