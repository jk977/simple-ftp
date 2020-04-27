#include "commands.h"
#include "config.h"
#include "io.h"
#include "logging.h"
#include "util.h"

#include <assert.h>
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
    assert(stream != NULL);

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
    assert(msg != NULL);
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
    assert(cmd.type != CMD_INVALID);
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
    assert(msg != NULL);
    return msg[0] == '\0';
}

/*
 * get_response: Receive a response of max length `rsp_len - 1` from `sock` and
 *               store into `rsp`.
 *
 *               Returns the number of bytes stored in `rsp` on success, or -1
 *               on failure.
 */

static ssize_t get_response(int sock, char* rsp, ssize_t rsp_len)
{
    assert(sock >= 0);
    assert(rsp != NULL);
    assert(rsp_len > 0);

    ssize_t const read_bytes = read_line(sock, rsp, rsp_len);
    Q_FAIL_IF(read_bytes < 0, -1);

    if (msg_is_eof(rsp)) {
        log_print("Received response from fd %d: EOF", sock);
    } else {
        log_print("Received response from fd %d: \"%s\" (%zd bytes)",
                  sock, rsp, read_bytes);
    }

    return read_bytes;
}

/*
 * connect_to: Connect to the given host and port.
 *
 *             Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or failure,
 *             respectively.
 */

static int connect_to(char const* host, char const* port)
{
    assert(host != NULL);
    assert(port != NULL);

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
 * init_data_sock: Initialize a data socket with the server at `host`,
 *                 communicating through `server_sock`.
 *
 *                 Returns the socket to send and receive data through on
 *                 success, or -1 on failure.
 */

static int init_data_sock(int server_sock, char const* host)
{
    assert(server_sock >= 0);
    assert(host != NULL);

    struct command const data_cmd = { .type = CMD_DATA, .arg = NULL };
    FAIL_IF(send_command(server_sock, data_cmd) < 0, -1);

    char rsp[CFG_MAXLINE + 1] = {0};
    FAIL_IF(get_response(server_sock, rsp, sizeof rsp) < 0, -1);

    if (msg_is_eof(rsp)) {
        ERRMSG("%s", "Unexpected EOF received");
        return -1;
    } else if (rsp[1] == '\0') {
        ERRMSG("%s", "Expected a port number from server");
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
    FAIL_IF(pipe(pipes) < 0, EXIT_FAILURE);

    // spawn child process to page output
    pid_t const child = fork();
    FAIL_IF(child < 0, EXIT_FAILURE);

    if (child == 0) {
        close(pipes[1]);
        exit(page_fd(pipes[0]));
    }

    // pipe the `ls` command into child process
    close(pipes[0]);
    int const ls_result = cmd_ls(pipes[1]);
    close(pipes[1]);

    // don't care about the exit status of the paging process
    FAIL_IF(wait(NULL) < 0, EXIT_FAILURE);

    if (ls_result != EXIT_SUCCESS) {
        ERRMSG("%s", "Command `ls` failed");
    }

    return ls_result;
}

/*
 * local_chdir: Execute `cmd_chdir(path)` locally, printing error messages if
 *              relevant.
 *
 *              Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or failure,
 *              respectively.
 */

static int local_chdir(char const* path)
{
    FAIL_IF(cmd_chdir(path) != EXIT_SUCCESS, EXIT_FAILURE);
    return EXIT_SUCCESS;
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
    switch (cmd.type) {
    case CMD_LS:
        return local_ls();
    case CMD_CD:
        return local_chdir(cmd.arg);
    default:
        fprintf(stderr, "Unexpected command %d; info table error?", cmd.type);
        return EXIT_FAILURE;
    }
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
    assert(server_sock >= 0);
    FAIL_IF(send_command(server_sock, cmd) != EXIT_SUCCESS, EXIT_FAILURE);

    char rsp[CFG_MAXLINE + 1] = {0};
    FAIL_IF(get_response(server_sock, rsp, sizeof rsp) < 0, EXIT_FAILURE);

    if (msg_is_eof(rsp)) {
        ERRMSG("%s", "Unexpected EOF received.");
        return EXIT_FAILURE;
    }

    if (cmd.type == CMD_EXIT) {
        cmd_exit(EXIT_SUCCESS);
    }

    FAIL_IF_SERV_ERR(rsp, EXIT_FAILURE);
    return EXIT_SUCCESS;
}

/*
 * test_put_path: Test if `path` can be used in the put command.
 *
 *                Returns `true` if `path` is a readable regular file, or
 *                `false` otherwise. If not valid, prints an error message.
 */

static bool test_put_path(char const* path)
{
    assert(path != NULL);

    bool error;
    bool const arg_is_valid = is_readable_reg(path, &error);
    FAIL_IF(error, false);

    if (!arg_is_valid) {
        ERRMSG("Path \"%s\" is not a readable regular file", path);
    }

    return arg_is_valid;
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
    assert(server_sock >= 0);

    char rsp[CFG_MAXLINE + 1] = {0};

    if (get_response(server_sock, rsp, sizeof rsp) < 0) {
        ERRMSG("%s", strerror(errno));
        return false;
    } else if (msg_is_eof(rsp)) {
        ERRMSG("%s", "Unexpected EOF while waiting for server response");
        return false;
    } else if (rsp[0] == RSP_ERR) {
        print_server_error(rsp);
        return false;
    }

    return true;
}

/*
 * setup_data_conn: Prepare the data connection to execute the local part of the
 *                  given command, consisting of 3 parts:
 *
 *                      1. Initialize data socket
 *                      2. Send the command to the server
 *                      3. Check for server acknowledgement
 *
 *                 Returns the data socket to use for the command on success,
 *                 or -1 on error. If an error occurs, a relevant error message
 *                 is printed.
 */

static int setup_data_conn(int server_sock, char const* host, struct command cmd)
{
    if (cmd.type == CMD_PUT) {
        Q_FAIL_IF(!test_put_path(cmd.arg), -1);
    }

    int const data_sock = init_data_sock(server_sock, host);
    Q_FAIL_IF(data_sock < 0, -1);

    if (send_command(server_sock, cmd) != -1) {
        ERRMSG("%s", strerror(errno));
        close(data_sock);
        return -1;
    }

    if (!check_data_response(server_sock)) {
        close(data_sock);
        return -1;
    }

    return data_sock;
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
    assert(server_sock >= 0);
    assert(host != NULL);

    int const data_sock = setup_data_conn(server_sock, host, cmd);
    Q_FAIL_IF(data_sock < 0, EXIT_FAILURE);

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
        ERRMSG("Unexpected command %d; info table error?", cmd.type);
        return EXIT_FAILURE;
    }

    if (result != EXIT_SUCCESS) {
        ERRMSG("%s", strerror(errno));
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
    assert(server_sock >= 0);
    assert(host != NULL);
    assert(msg != NULL);

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
 *             Returns `EXIT_FAILURE` on failure. Otherwise, this function
 *             does not return.
 */

static int client_run(char const* host)
{
    assert(host != NULL);

    int const server_sock = connect_to(host, AS_STR(CFG_PORT));
    FAIL_IF(server_sock < 0, EXIT_FAILURE);

    while (true) {
        printf(CFG_PROMPT);
        fflush(stdout);

        char buf[CFG_MAXLINE + 1] = {0};
        ssize_t const read_bytes = read_line(STDIN_FILENO, buf, sizeof buf);
        FAIL_IF(read_bytes < 0, EXIT_FAILURE);

        if (buf[0] == '\0') {
            log_print("Empty user input received; skipping");
            continue;
        }

        int const status = run_command(server_sock, host, buf);
        char const* status_str = (status == EXIT_SUCCESS) ?
            "successfully" :
            "unsuccessfully";

        printf("Command finished %s (status = %d)\n", status_str, status);
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

    char const* host = argv[optind];
    return client_run(host);
}
