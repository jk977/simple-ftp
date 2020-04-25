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
#include <sys/wait.h>

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

    // get the actual info needed to connect client to the server
    struct addrinfo* info = NULL;
    GAI_FAIL_IF(getaddrinfo(host, port, &hints, &info), "getaddrinfo", NULL);
    return info;
}

static int send_command(int server_sock, struct command cmd)
{
    char const code = cmd_get_ctl(cmd.type);

    if (cmd.arg != NULL) {
        Q_FAIL_IF(dprintf(server_sock, "%c%s\n", code, cmd.arg) < 0,
                  EXIT_FAILURE);
        log_print("Sent command: %c%s", code, cmd.arg);
    } else {
        Q_FAIL_IF(dprintf(server_sock, "%c\n", code) < 0, EXIT_FAILURE);
        log_print("Sent command: %c", code);
    }

    return EXIT_SUCCESS;
}

static bool msg_is_eof(char const* msg)
{
    return msg[0] == '\0';
}

static ssize_t get_response(int sock, char* rsp, size_t rsp_len)
{
    log_print("Reading response from socket %d", sock);
    ssize_t const result = read_line(sock, rsp, rsp_len - 1);
    Q_FAIL_IF(result < 0, -1);

    if (msg_is_eof(rsp)) {
        log_print("Received server response: EOF");
    } else {
        log_print("Received server response: \"%s\" (%zd bytes)", rsp, result);
    }

    return result;
}

static int connect_to(char const* host, char const* port)
{
    struct addrinfo* info = get_info(host, port);

    int const sock = make_socket(info);
    Q_FAIL_IF(sock < 0, EXIT_FAILURE);

    // copy the required data from the info struct and free it
    struct sockaddr const dest_addr = *info->ai_addr;
    socklen_t const dest_addrlen = info->ai_addrlen;
    freeaddrinfo(info);

    // use the provided info to connect to the given destination
    Q_FAIL_IF(connect(sock, &dest_addr, dest_addrlen) < 0, EXIT_FAILURE);
    log_print("Successfully connected to %s:%s", host, port);

    return sock;
}

static int init_data(int server_sock, char const* host)
{
    struct command const data_cmd = { .type = CMD_DATA, .arg = NULL };
    FAIL_IF(send_command(server_sock, data_cmd) < 0, "send_command", -1);

    char rsp[CFG_MAXLINE] = {0};
    FAIL_IF(get_response(server_sock, rsp, sizeof rsp) < 0, "get_response", -1);

    if (msg_is_eof(rsp)) {
        ERRMSG("get_response", "Unexpected EOF received.");
        return -1;
    } else if (rsp[1] == '\0') {
        ERRMSG("get_response", "Expected a port number");
        return -1;
    }

    FAIL_IF_SERV_ERR(rsp, -1);

    char const* data_port = &rsp[1];
    return connect_to(host, data_port);
}

static int local_ls(void)
{
    int pipes[2];
    Q_FAIL_IF(pipe(pipes) < 0, EXIT_FAILURE);

    pid_t const child = fork();
    Q_FAIL_IF(child < 0, EXIT_FAILURE);

    if (child == 0) {
        close(pipes[1]);
        exit(page_fd(pipes[0]));
    }

    close(pipes[0]);
    Q_FAIL_IF(cmd_ls(pipes[1]) != EXIT_SUCCESS, EXIT_FAILURE);
    close(pipes[1]);

    int status;
    Q_FAIL_IF(wait(&status) < 0, EXIT_FAILURE);
    return status;
}

static int handle_local_cmd(struct command cmd)
{
    int result;
    char const* context;
    log_print("Handling local command (pid=%u)", getpid());

    switch (cmd.type) {
    case CMD_LS:
        result = local_ls();
        context = "local_ls";
        break;
    case CMD_CD:
        result = cmd_chdir(cmd.arg);
        context = "cmd_chdir";
        break;
    default:
        log_print("Unexpected command %d; info table error?", cmd.type);
        return EXIT_FAILURE;
    }

    FAIL_IF(result != EXIT_SUCCESS, context, EXIT_FAILURE);
    log_print("Finished local command (pid=%u)", getpid());
    return EXIT_SUCCESS;
}

static int handle_remote_cmd(int server_sock, struct command cmd)
{
    FAIL_IF(send_command(server_sock, cmd) != EXIT_SUCCESS, "send_command",
            EXIT_FAILURE);

    char rsp[CFG_MAXLINE] = {0};
    FAIL_IF(get_response(server_sock, rsp, sizeof rsp) < 0, "get_response",
            EXIT_FAILURE);

    if (msg_is_eof(rsp)) {
        ERRMSG("get_response", "Unexpected EOF received.");
        return EXIT_FAILURE;
    }

    if (cmd.type == CMD_EXIT) {
        cmd_exit(EXIT_SUCCESS);
    }

    FAIL_IF_SERV_ERR(rsp, EXIT_FAILURE);
    return EXIT_SUCCESS;
}

static int handle_data_cmd(int server_sock, char const* host, struct command cmd)
{
    int const data_sock = init_data(server_sock, host);
    Q_FAIL_IF(data_sock < 0, EXIT_FAILURE);
    FAIL_IF(send_command(server_sock, cmd) != EXIT_SUCCESS, "send_command",
            EXIT_FAILURE);

    char const* context;
    int result;

    switch (cmd.type) {
    case CMD_RLS:
    case CMD_SHOW:
        context = "page_fd";
        result = page_fd(data_sock);
        break;
    case CMD_GET:
        context = "receive_path";
        result = receive_path(basename_of(cmd.arg), data_sock);
        break;
    case CMD_PUT:
        context = "send_path";
        result = send_path(data_sock, cmd.arg);
        break;
    default:
        log_print("Unexpected command %d; check info table for accuracy", cmd);
        return EXIT_FAILURE;
    }

    if (result != EXIT_SUCCESS) {
        ERRMSG(context, strerror(errno));
    }

    close(data_sock);
    return result;
}

static int run_command(int server_sock, char const* host, char const* msg)
{
    struct command cmd = cmd_parse(msg);

    if (cmd.type == CMD_INVALID) {
        printf("Unrecognized command: \"%s\"\n", msg);
        return EXIT_FAILURE;
    }

    if (!cmd_is_remote(cmd.type)) {
        return handle_local_cmd(cmd);
    } else if (!cmd_needs_data(cmd.type)) {
        return handle_remote_cmd(server_sock, cmd);
    } else {
        return handle_data_cmd(server_sock, host, cmd);
    }
}

static int client_run(char const* hostname)
{
    int const server_sock = connect_to(hostname, AS_STR(CFG_PORT));
    FAIL_IF(server_sock < 0, "connect_to", EXIT_FAILURE);

    while (true) {
        printf(CFG_PROMPT);
        fflush(stdout);

        char buf[CFG_MAXLINE] = {0};
        FAIL_IF(fgets(buf, sizeof(buf), stdin) == NULL, "fgets", EXIT_FAILURE);
        size_t const buf_len = strlen(buf);

        if (buf[buf_len - 1] == '\n') {
            buf[buf_len - 1] = '\0';
        }

        int const status = run_command(server_sock, hostname, buf);
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
