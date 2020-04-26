#include "commands.h"
#include "config.h"
#include "io.h"
#include "logging.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <unistd.h>

#include <sys/wait.h>

/*
 * FAIL_IF_SERV_ERR: Print the server error message and return `ret` if `rsp` is
 *                   a server error response.
 */

#define FAIL_IF_SERV_ERR(rsp, ret)      \
    do {                                \
        char const* _rsp = rsp;         \
        if (_rsp[0] == RSP_ERR) {       \
            print_server_error(_rsp);   \
            return ret;                 \
        }                               \
    } while (0)

// the file name of the program
static char const* program;

/*
 * usage: Print program usage to the given file stream.
 */

static void usage(FILE* stream)
{
    fprintf(stream, "Usage:\n");
    fprintf(stream, "\t%s -h\n", program);
    fprintf(stream, "\t%s [-d] HOSTNAME\n\n", program);

    fprintf(stream, "Options:\n");
    fprintf(stream, "\t-h\tShow this help message and exit.\n");
    fprintf(stream, "\t-d\tEnable debug output.\n");
}

/*
 * print_server_error: Print the error given by the server response.
 */

static int print_server_error(char const* msg)
{
    return fprintf(stderr, "Server error: %s\n", &msg[1]);
}

/*
 * send_command: Send `cmd` to `server_sock`.
 *
 *               Returns `EXIT_SUCCESS` or `EXIT_FAILURE` on success or failure,
 *               respectively.
 */

static int send_command(int sock, struct command cmd)
{
    char const code = cmd_get_ctl(cmd.type);

    if (cmd.arg != NULL) {
        Q_FAIL_IF(dprintf(sock, "%c%s\n", code, cmd.arg) < 0, EXIT_FAILURE);
        log_print("Sent command to fd %d: %c%s", sock, code, cmd.arg);
    } else {
        Q_FAIL_IF(dprintf(sock, "%c\n", code) < 0, EXIT_FAILURE);
        log_print("Sent command to fd %d: %c", sock, code);
    }

    return EXIT_SUCCESS;
}

/*
 * msg_is_eof: Predicate for EOF server responses.
 */

static bool msg_is_eof(char const* msg)
{
    return msg[0] == '\0';
}

/*
 * get_response: Receive a response of max length `rsp_len - 1` from `sock` and
 *               store into `rsp`.
 *
 *               Returns the number of bytes stored in `rsp` on success, or -1
 *               on failure.
 */

static ssize_t get_response(int sock, char* rsp, size_t rsp_len)
{
    ssize_t const result = read_line(sock, rsp, rsp_len);
    Q_FAIL_IF(result < 0, -1);

    if (msg_is_eof(rsp)) {
        log_print("Received response from fd %d: EOF", sock);
    } else {
        log_print("Received response from fd %d: \"%s\" (%zd bytes)",
                  sock, rsp, result);
    }

    return result;
}

/*
 * connect_to: Connect to the given host and port.
 *
 *             Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or failure,
 *             respectively.
 */

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
    log_print("Successfully connected to %s:%s (fd %d)", host, port, sock);

    return sock;
}

/*
 * init_data: Initialize a data connection with the server at `host`,
 *            communicating through `server_sock`.
 *
 *            Returns the socket to send and receive data through on success,
 *            or -1 on failure.
 */

static int init_data(int server_sock, char const* host)
{
    struct command const data_cmd = { .type = CMD_DATA, .arg = NULL };
    FAIL_IF(send_command(server_sock, data_cmd) < 0, -1);

    char rsp[CFG_MAXLINE] = {0};
    FAIL_IF(get_response(server_sock, rsp, sizeof rsp) < 0, -1);

    if (msg_is_eof(rsp)) {
        ERRMSG("Unexpected EOF received");
        return -1;
    } else if (rsp[1] == '\0') {
        ERRMSG("Expected a port number from server");
        return -1;
    }

    FAIL_IF_SERV_ERR(rsp, -1);

    char const* data_port = &rsp[1];
    int const data_sock = connect_to(host, data_port);

    log_print("Initialized data connection at fd %d", data_sock);
    return data_sock;
}

/*
 * local_ls: Execute `cmd_ls()` locally, paging its output.
 *
 *           Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or failure,
 *           respectively.
 */

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

/*
 * handle_local_cmd: Execute `cmd` locally, printing an error message on
 *                   failure.
 *
 *                   Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or
 *                   failure, respectively.
 */

static int handle_local_cmd(struct command cmd)
{
    int result;

    switch (cmd.type) {
    case CMD_LS:
        result = local_ls();
        break;
    case CMD_CD:
        result = cmd_chdir(cmd.arg);
        break;
    default:
        fprintf(stderr, "Unexpected command %d; info table error?", cmd.type);
        return EXIT_FAILURE;
    }

    FAIL_IF(result != EXIT_SUCCESS, EXIT_FAILURE);
    return EXIT_SUCCESS;
}

/*
 * handle_remote_cmd: Send `cmd` to `server_sock` to be executed, printing an
 *                    error message on failure.
 *
 *                    Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or
 *                    failure, respectively.
 */

static int handle_remote_cmd(int server_sock, struct command cmd)
{
    FAIL_IF(send_command(server_sock, cmd) != EXIT_SUCCESS, EXIT_FAILURE);

    char rsp[CFG_MAXLINE] = {0};
    FAIL_IF(get_response(server_sock, rsp, sizeof rsp) < 0, EXIT_FAILURE);

    if (msg_is_eof(rsp)) {
        ERRMSG("Unexpected EOF received.");
        return EXIT_FAILURE;
    }

    if (cmd.type == CMD_EXIT) {
        cmd_exit(EXIT_SUCCESS);
    }

    FAIL_IF_SERV_ERR(rsp, EXIT_FAILURE);
    return EXIT_SUCCESS;
}

/*
 * validate_cmd_arg: Make sure the argument in the given command is acceptable.
 *
 *                   Returns `true` if argument is valid, or `false` otherwise.
 *                   If not valid, prints an error message.
 */

static bool validate_data_cmd_arg(struct command cmd)
{
    if (cmd.type == CMD_PUT) {
        bool error;

        bool const arg_is_readable = is_readable(cmd.arg, &error);
        FAIL_IF(error, false);

        if (!arg_is_readable || !is_reg(cmd.arg, &error)) {
            // either an error occurred or file isn't readable; either way,
            // just ignore the command
            FAIL_IF(error, false);
            ERRMSG("Provided path is not a readable regular file");
            return false;
        }
    }

    return true;
}

/*
 * check_data_response: Get response from server and make sure it's a valid
 *                      response for a data connection.
 *
 *                      Returns `true` if the response was expected, or
 *                      `false` otherwise. If response was unexpected, an
 *                      error message is printed.
 */

static bool check_data_response(int server_sock)
{
    char rsp[CFG_MAXLINE] = {0};

    if (get_response(server_sock, rsp, sizeof rsp) < 0) {
        ERRMSG(strerror(errno));
        return false;
    } else if (msg_is_eof(rsp)) {
        ERRMSG("Unexpected EOF while waiting for server response");
        return false;
    } else if (rsp[0] == RSP_ERR) {
        print_server_error(rsp);
        return false;
    }

    return true;
}

/*
 * handle_data_cmd: Establish a data connection with the server and execute
 *                  `cmd` both locally and remotely, printing an error message
 *                  on failure.
 *
 *                  Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or
 *                  failure, respectively.
 */

static int handle_data_cmd(int server_sock, char const* host,
        struct command cmd)
{
    Q_FAIL_IF(!validate_data_cmd_arg(cmd), EXIT_FAILURE);

    int const data_sock = init_data(server_sock, host);
    Q_FAIL_IF(data_sock < 0, EXIT_FAILURE);

    if (send_command(server_sock, cmd) != EXIT_SUCCESS) {
        ERRMSG(strerror(errno));
        close(data_sock);
        return EXIT_FAILURE;
    }

    // ensure server success before executing local command
    if (!check_data_response(server_sock)) {
        close(data_sock);
        return EXIT_FAILURE;
    }

    int result;

    switch (cmd.type) {
    case CMD_RLS:
    case CMD_SHOW:
        result = page_fd(data_sock);
        break;
    case CMD_GET:
        result = receive_path(basename_of(cmd.arg), data_sock, 0666);
        break;
    case CMD_PUT:
        result = send_path(data_sock, cmd.arg);
        break;
    default:
        fprintf(stderr, "Unexpected command %d; info table error?", cmd.type);
        return EXIT_FAILURE;
    }

    if (result != EXIT_SUCCESS) {
        ERRMSG(strerror(errno));
    }

    close(data_sock);
    return result;
}

/*
 * run_command: Run the command contained in the user input `msg` with the
 *              server at `host`, communicating via `server_sock`.
 *
 *              Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or failure,
 *              respectively.
 */

static int run_command(int server_sock, char const* host, char const* msg)
{
    struct command cmd = cmd_parse(msg);

    if (cmd.type == CMD_INVALID) {
        printf("Unrecognized command: \"%s\"\n", msg);
        return EXIT_FAILURE;
    }

    char const* cmd_name = cmd_get_name(cmd.type);

    if (cmd.arg == NULL) {
        printf("Running \"%s\"\n", cmd_name);
    } else {
        printf("Running \"%s\" with argument \"%s\"\n", cmd_name, cmd.arg);
    }

    if (!cmd_is_remote(cmd.type)) {
        return handle_local_cmd(cmd);
    } else if (!cmd_needs_data(cmd.type)) {
        return handle_remote_cmd(server_sock, cmd);
    } else {
        return handle_data_cmd(server_sock, host, cmd);
    }
}

/*
 * client_run: Run the client, connecting to the server at `hostname`.
 *
 *             Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or failure,
 *             respectively.
 */

static int client_run(char const* hostname)
{
    int const server_sock = connect_to(hostname, AS_STR(CFG_PORT));
    FAIL_IF(server_sock < 0, EXIT_FAILURE);

    while (true) {
        printf(CFG_PROMPT);
        fflush(stdout);

        char buf[CFG_MAXLINE] = {0};
        FAIL_IF(fgets(buf, sizeof buf, stdin) == NULL, EXIT_FAILURE);
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
