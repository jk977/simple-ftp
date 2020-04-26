#include "commands.h"
#include "config.h"
#include "logging.h"
#include "util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

static char const* program;

/*
 * usage: Print program usage to the given file stream.
 */

static void usage(FILE* stream)
{
    fprintf(stream, "Usage:\n");
    fprintf(stream, "\t%s -h\n", program);
    fprintf(stream, "\t%s [-d]\n\n", program);

    fprintf(stream, "Options:\n");
    fprintf(stream, "\t-h\tShow this help message and exit.\n");
    fprintf(stream, "\t-d\tEnable debug output.\n");
}

/*
 * send_ack: Send an acknowledge message to the given socket.
 *
 *           Returns `EXIT_SUCCESS` or `EXIT_FAILURE`, depending on if the
 *           socket write was successful.
 */

static int send_ack(int sock, in_port_t const* port)
{
    if (port != NULL) {
        log_print("Sending ack to %d with port %u", sock, *port);
        Q_FAIL_IF(dprintf(sock, "%c%u\n", RSP_ACK, *port) < 0, EXIT_FAILURE);
    } else {
        log_print("Sending ack to %d", sock);
        Q_FAIL_IF(dprintf(sock, "%c\n", RSP_ACK) < 0, EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

/*
 * send_err: Send an error message to the given socket.
 *
 *           Returns `EXIT_SUCCESS` or `EXIT_FAILURE`, depending on if the
 *           socket write was successful.
 */

static int send_err(int sock, char const* msg)
{
    log_print("Sending error to fd %d: \"%s\"", sock, msg);
    Q_FAIL_IF(dprintf(sock, "%c%s\n", RSP_ERR, msg) < 0, EXIT_FAILURE);
    return EXIT_SUCCESS;
}

/*
 * respond: Send a response to the given socket. If `success` is true, the
 *          response is an acknowledgement. Otherwise, the response is an error
 *          message containing the string associated with the current value of
 *          `errno`.
 *
 *          Returns `EXIT_SUCCESS` or `EXIT_FAILURE`, depending on if the socket
 *          write was successful.
 */

static int respond(int sock, bool success)
{
    if (success) {
        FAIL_IF(send_ack(sock, NULL) != EXIT_SUCCESS, EXIT_FAILURE);
    } else {
        char const* err = strerror(errno);
        FAIL_IF(send_err(sock, err) != EXIT_SUCCESS, EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

/*
 * listen_on: Create a socket listening on the given port.
 *
 *            Returns a socket file descriptor on success, or -1 otherwise.
 */

static int listen_on(in_port_t port)
{
    int const sock = make_socket(NULL);
    Q_FAIL_IF(sock < 0, -1);

    struct in_addr const sin_addr = { .s_addr = INADDR_ANY };
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = sin_addr,
    };

    // set up server to start listening for connections (any address)
    Q_FAIL_IF(bind(sock, (struct sockaddr*) &address, sizeof address) < 0, -1);
    Q_FAIL_IF(listen(sock, CFG_BACKLOG) < 0, -1);
    log_print("Created socket %d listening on port %u", sock, port);

    return sock;
}

/*
 * init_data: Initialize a data connection for the client, sending an
 *            acknowledgement to the client containing the data port to connect
 *            to.
 *
 *            Returns a new socket to handle the data connection with on
 *            success, or -1 otherwise.
 */

static int init_data(int client_sock)
{
    int const tmp_sock = listen_on(0);
    Q_FAIL_IF(tmp_sock < 0, -1);

    struct sockaddr_in addr;
    socklen_t addr_size = sizeof addr;

    if (getsockname(tmp_sock, (struct sockaddr*) &addr, &addr_size) < 0) {
        int const old_errno = errno;
        close(tmp_sock);
        errno = old_errno;

        return -1;
    }

    in_port_t const port = ntohs(addr.sin_port);
    log_print("Created data connection at fd %d; listening on port %u",
              tmp_sock, port);

    Q_FAIL_IF(send_ack(client_sock, &port) != EXIT_SUCCESS, -1);
    log_print("Sent ack over control connection at fd %d", client_sock);

    char client[CFG_MAXHOST] = {0};
    int const data_sock = accept(tmp_sock, (struct sockaddr*) &addr, &addr_size);
    Q_FAIL_IF(data_sock < 0, -1);
    close(tmp_sock);

    addr_to_hostname((struct sockaddr*) &addr, addr_size, client, sizeof client);
    log_print("Accepted data client at %s:%u (fd %d)",
              client, ntohs(addr.sin_port), data_sock);

    return data_sock;
}

/*
 * server_exit: Send an acknowledgement to the client, then close the 
 *              connection and exit.
 *
 *              Exits with `EXIT_SUCCESS` on successful acknowledgement,
 *              or `EXIT_FAILURE` otherwise.
 */

static void server_exit(int client_sock)
{
    int const status = send_ack(client_sock, NULL);

    if (status != EXIT_SUCCESS) {
        ERRMSG(strerror(errno));
    }

    close(client_sock);
    cmd_exit(status);
}

/*
 * handle_local_cmd: Run a command that does not require a data connection.
 *
 *                   Returns `EXIT_SUCCESS` if command was successful, or
 *                   `EXIT_FAILURE` otherwise.
 */

static int handle_local_cmd(int client_sock, int* data_sock, struct command cmd)
{
    int result;
    char const* err = NULL;

    switch (cmd.type) {
    case CMD_EXIT:
        server_exit(client_sock);

        // this line should never be reached
        result = EXIT_FAILURE;
        err = "Failed to exit process";
        break;
    case CMD_DATA:
        *data_sock = init_data(client_sock);

        if (*data_sock >= 0) {
            // return early to avoid sending multiple acks; although it's a
            // little hack-ish, this is due to the protocol sending the port
            // alongside the ack, requiring special handling of the ack
            return EXIT_SUCCESS;
        }

        result = EXIT_FAILURE;
        err = "Failed to create data socket";
        break;
    case CMD_RCD:
        result = cmd_chdir(cmd.arg);
        err = strerror(errno);
        break;
    default:
        result = EXIT_FAILURE;
        err = "Unrecognized command";
        break;
    }

    if (result != EXIT_SUCCESS) {
        FAIL_IF(send_err(client_sock, err) != EXIT_SUCCESS, EXIT_FAILURE);
    } else {
        FAIL_IF(send_ack(client_sock, NULL) != EXIT_SUCCESS, EXIT_FAILURE);
    }

    return result;
}

/*
 * handle_data_cmd: Run a command that requires a data connection.
 *
 *                  Returns `EXIT_SUCCESS` if command was successful, or
 *                  `EXIT_FAILURE` otherwise.
 *
 *                  This prints an error message if the command fails.
 */

static int handle_data_cmd(int client_sock, int* data_sock, struct command cmd)
{
    if (*data_sock < 0) {
        // fail if data connection has not been created
        char const* err = "Data connection not established";
        int const status = send_err(client_sock, err);
        FAIL_IF(status != EXIT_SUCCESS, EXIT_FAILURE);
        return EXIT_SUCCESS;
    }

    int result;
    int rsp_status;

    switch (cmd.type) {
    case CMD_RLS:
        result = cmd_ls(*data_sock);
        rsp_status = respond(client_sock, result == EXIT_SUCCESS);
        FAIL_IF(rsp_status != EXIT_SUCCESS, EXIT_FAILURE);
        break;
    case CMD_GET:
    case CMD_SHOW:
        result = send_path(*data_sock, cmd.arg);
        rsp_status = respond(client_sock, result == EXIT_SUCCESS);
        FAIL_IF(rsp_status != EXIT_SUCCESS, EXIT_FAILURE);
        break;
    case CMD_PUT:
        FAIL_IF(send_ack(client_sock, NULL) != EXIT_SUCCESS, EXIT_FAILURE);
        result = receive_path(basename_of(cmd.arg), *data_sock, 0666);
        break;
    default:
        fprintf(stderr, "Unexpected command %d; info table error?", cmd.type);
        return EXIT_FAILURE;
    }

    close(*data_sock);
    log_print("Closed data connection at fd %d", *data_sock);
    *data_sock = -1;

    return result;
}

/*
 * process_command: Process the command contained in the given message.
 */

static int process_command(int client_sock, int* data_sock, char const* msg)
{
    log_print("Received command from client: %s", msg);

    struct command cmd = {
        .type = cmd_get_type(msg[0]),
        .arg = &msg[1],
    };

    if (cmd.type == CMD_INVALID) {
        return send_err(client_sock, "Unrecognized command");
    }

    if (!cmd_needs_data(cmd.type)) {
        return handle_local_cmd(client_sock, data_sock, cmd);
    } else {
        return handle_data_cmd(client_sock, data_sock, cmd);
    }
}

/*
 * handle_connection: Handle the connection given by the given file descriptor.
 */

static void handle_connection(int client_sock)
{
    int data_sock = -1;

    while (true) {
        char message[CFG_MAXLINE] = {0};

        if (read_line(client_sock, message, sizeof message) > 0) {
            process_command(client_sock, &data_sock, message);
        } else {
            log_print("Failed to receive message from client");
            send_err(client_sock, strerror(errno));
        }
    }
}

/*
 * handle_sigchld: Clean up process table and output the exit code of child
 *                 processes when they finish.
 */

static void handle_sigchld(int signum)
{
    (void) signum;
    int status;
    pid_t const child = wait(&status);
    log_print("Child %u exited with status %d", child, status);
}

/*
 * run_server: Main method for the server process. Binds the server socket
 *             and sends messages to connected clients.
 *
 *             Returns `EXIT_SUCCESS` or `EXIT_FAILURE` on success or failure,
 *             respectively. Only children processes exit successfully.
 */

static int run_server(void)
{
    int const sock = listen_on(CFG_PORT);
    FAIL_IF(sock < 0, EXIT_FAILURE);

    while (true) {
        log_print("Waiting for client with socket at fd %d", sock);

        struct sockaddr addr;
        socklen_t addr_len = sizeof(struct sockaddr_in);
        int const client_sock = accept(sock, &addr, &addr_len);

        if (client_sock < 0) {
            if (errno != EINTR) {
                // ignore `EINTR` since child termination may interrupt
                // `accept()` with `SIGCHLD`
                ERRMSG(strerror(errno));
            }

            continue;
        }

        char host[CFG_MAXHOST] = {0};
        GAI_FAIL_IF(addr_to_hostname(&addr, addr_len, host, sizeof host),
                    EXIT_FAILURE);
        printf("Accepted connection from %s\n", host);

        // fork to have the child handle client so the parent can keep listening
        pid_t const child = fork();
        FAIL_IF(child < 0, EXIT_FAILURE);

        if (child == 0) {
            handle_connection(client_sock);
        }

        // parent doesn't need client
        close(client_sock);
    }
}

int main(int argc, char** argv)
{
    program = argv[0];

    struct sigaction act = {
        .sa_handler = handle_sigchld,
        .sa_flags = 0,
    };

    // register signal to clean up children when they finish
    FAIL_IF(sigemptyset(&act.sa_mask) < 0, EXIT_FAILURE);
    FAIL_IF(sigaction(SIGCHLD, &act, NULL) < 0, EXIT_FAILURE);

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

    if (optind != argc) {
        usage(stderr);
        return EXIT_FAILURE;
    }

    return run_server();
}
