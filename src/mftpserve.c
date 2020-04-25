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
        log_print("Sending ack with port %u", *port);
        Q_FAIL_IF(dprintf(sock, "%c%u\n", RSP_ACK, *port) < 0, EXIT_FAILURE);
    } else {
        log_print("Sending ack");
        Q_FAIL_IF(dprintf(sock, "%c\n", RSP_ACK) < 0, EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

/*
 * send_err: Send an error message to the given socket, in the format
 *           "context: msg".
 *
 *           Returns `EXIT_SUCCESS` or `EXIT_FAILURE`, depending on if the
 *           socket write was successful.
 */

static int send_err(int sock, char const* context, char const* msg)
{
    log_print("Sending error (context=\"%s\", msg=\"%s\")", context, msg);
    Q_FAIL_IF(dprintf(sock, "%c%s: %s\n", RSP_ERR, context, msg) < 0,
              EXIT_FAILURE);
    return EXIT_SUCCESS;
}

/*
 * respond: Send a response to the given socket. If `success` is true, the
 *          response is an acknowledgement. Otherwise, the response is an error
 *          with the contents "context: strerror(errno)".
 *
 *          Returns `EXIT_SUCCESS` or `EXIT_FAILURE`, depending on if the socket
 *          write was successful.
 *
 *          This prints an error message if sending the response fails.
 */

static int respond(int sock, bool success, char const* context)
{
    if (success) {
        FAIL_IF(send_ack(sock, NULL) != EXIT_SUCCESS, "send_ack", EXIT_FAILURE);
    } else {
        FAIL_IF(send_err(sock, context, strerror(errno)) != EXIT_SUCCESS,
                "send_err", EXIT_FAILURE);
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
    Q_FAIL_IF(sock < 0, EXIT_FAILURE);

    struct in_addr const sin_addr = { .s_addr = INADDR_ANY };
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = sin_addr,
    };

    // set up server to start listening for connections (any address)
    Q_FAIL_IF(bind(sock, (struct sockaddr*) &address, sizeof address) < 0,
              EXIT_FAILURE);
    Q_FAIL_IF(listen(sock, CFG_BACKLOG) < 0, EXIT_FAILURE);
    log_print("Listening on port %u", port);

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
    int const data_sock = listen_on(0);
    Q_FAIL_IF(data_sock < 0, -1);

    struct sockaddr_in addr;
    socklen_t addr_size = sizeof addr;

    if (getsockname(data_sock, (struct sockaddr*) &addr, &addr_size) < 0) {
        int const old_errno = errno;
        close(data_sock);
        errno = old_errno;

        return -1;
    }

    in_port_t const port = ntohs(addr.sin_port);
    log_print("Created data connection; listening on port %u", port);

    Q_FAIL_IF(send_ack(client_sock, &port) != EXIT_SUCCESS, -1);
    log_print("Sent ack over control connection");

    char client[CFG_MAXHOST] = {0};
    Q_FAIL_IF(accept(data_sock, (struct sockaddr*) &addr, &addr_size) < 0, -1);
    addr_to_hostname((struct sockaddr*) &addr, addr_size, client, sizeof client);
    log_print("Accepted data client at %s:%u", client, ntohs(addr.sin_port));

    return data_sock;
}

/*
 * server_exit: Send an acknowledgement to the client, then close the 
 *              connection and exit.
 *
 *              Exits with `EXIT_SUCCESS` on successful acknowledgement,
 *              or `EXIT_FAILURE` otherwise.
 *
 *              This prints an error message if the acknowledgement fails.
 */

static void server_exit(int client_sock)
{
    int status = send_ack(client_sock, NULL);

    if (status != EXIT_SUCCESS) {
        ERRMSG("send_ack", strerror(errno));
    }

    close(client_sock);
    cmd_exit(status);
}

/*
 * handle_local_cmd: Run a command that does not require a data connection.
 *
 *                   Returns `EXIT_SUCCESS` if command was successful, or
 *                   `EXIT_FAILURE` otherwise.
 *
 *                   This prints an error message if the command fails.
 */

static int handle_local_cmd(int client_sock, int* data_sock,
        enum cmd_type cmd, char const* arg)
{
    if (cmd == CMD_EXIT) {
        server_exit(client_sock);
        return EXIT_FAILURE;
    } else if (cmd == CMD_RCD) {
        return respond(client_sock, cmd_chdir(arg) == EXIT_SUCCESS, "cmd_chdir");
    } else if (cmd == CMD_DATA) {
        *data_sock = init_data(client_sock);
        return respond(client_sock, *data_sock >= 0, "init_data");
    } else {
        char const* msg = "Invalid command given";
        FAIL_IF(send_err(client_sock, "Server", msg) != EXIT_SUCCESS,
                "send_err", EXIT_FAILURE);
        return EXIT_SUCCESS;
    }
}

/*
 * handle_data_cmd: Run a command that requires a data connection.
 *
 *                  Returns `EXIT_SUCCESS` if command was successful, or
 *                  `EXIT_FAILURE` otherwise.
 *
 *                  This prints an error message if the command fails.
 */

static int handle_data_cmd(int client_sock, int* data_sock,
        enum cmd_type cmd, char const* arg)
{
    // make sure data connection has been created
    if (*data_sock < 0) {
        char const* msg = "Data connection not established.";
        FAIL_IF(send_err(client_sock, "Server", msg) != EXIT_SUCCESS,
                "send_err", EXIT_FAILURE);
        return EXIT_SUCCESS;
    }

    FAIL_IF(send_ack(*data_sock, NULL) != EXIT_SUCCESS, "send_ack",
            EXIT_FAILURE);

    int result;
    char const* context;

    if (cmd == CMD_RLS) {
        context = "cmd_ls";
        result = cmd_ls(*data_sock, NULL);
    } else if (cmd == CMD_GET || cmd == CMD_SHOW) {
        context = "send_path";
        result = send_path(*data_sock, arg);
    } else if (cmd == CMD_PUT) {
        context = "receive_path";
        result = receive_path(basename_of(arg), *data_sock);
    } else {
        log_print("Unexpected command %d; check info table for accuracy", cmd);
        return EXIT_FAILURE;
    }

    if (result != EXIT_SUCCESS) {
        ERRMSG(context, strerror(errno));
    }

    close(*data_sock);
    *data_sock = -1;
    return result;
}

/*
 * process_command: Process the command contained in the given message.
 */

static int process_command(int client_sock, int* data_sock, char const* msg)
{
    log_print("Received command from client: %s", msg);
    enum cmd_type cmd = cmd_get_type(msg[0]);
    char const* arg = &msg[1];

    if (!cmd_needs_data(cmd)) {
        return handle_local_cmd(client_sock, data_sock, cmd, arg);
    } else {
        return handle_data_cmd(client_sock, data_sock, cmd, arg);
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

        if (read_line(client_sock, message, sizeof(message) - 1) > 0) {
            process_command(client_sock, &data_sock, message);
        } else {
            log_print("Failed to receive message from client");
            send_err(client_sock, "read_line", strerror(errno));
        }
    }
}

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
    FAIL_IF(sock < 0, "listen_on", EXIT_FAILURE);

    while (true) {
        struct sockaddr addr;
        socklen_t addr_len = sizeof(struct sockaddr_in);
        int const client_sock = accept(sock, &addr, &addr_len);

        if (client_sock < 0) {
            if (errno != EINTR) {
                // ignore `EINTR` since child termination may interrupt
                // `accept()` with `SIGCHLD`
                ERRMSG("accept", strerror(errno));
            }

            continue;
        }

        char host[CFG_MAXHOST] = {0};
        GAI_FAIL_IF(addr_to_hostname(&addr, addr_len, host, sizeof host),
                    "addr_to_hostname", EXIT_FAILURE);
        printf("Accepted connection from %s\n", host);

        // fork to have the child handle client so the parent can keep listening
        pid_t const child = fork();
        FAIL_IF(child < 0, "fork", EXIT_FAILURE);

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
    FAIL_IF(sigemptyset(&act.sa_mask) < 0, "sigemptyset", EXIT_FAILURE);
    FAIL_IF(sigaction(SIGCHLD, &act, NULL) < 0, "sigaction", EXIT_FAILURE);

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
