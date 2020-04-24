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

static int send_err(int sock, char const* context, char const* msg)
{
    log_print("Sending error (context=\"%s\", msg=\"%s\")", context, msg);
    Q_FAIL_IF(dprintf(sock, "%c%s: %s\n", RSP_ERR, context, msg) < 0,
              EXIT_FAILURE);
    return EXIT_SUCCESS;
}

static int respond(int client_sock, bool success, char const* context)
{
    if (success) {
        FAIL_IF(send_ack(client_sock, NULL) != EXIT_SUCCESS, "send_ack",
                EXIT_FAILURE);
    } else {
        FAIL_IF(send_err(client_sock, context, strerror(errno)) != EXIT_SUCCESS,
                "send_err", EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

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
    Q_FAIL_IF(bind(sock, (struct sockaddr*) &address, sizeof(address)) < 0,
              EXIT_FAILURE);
    Q_FAIL_IF(listen(sock, CFG_BACKLOG) < 0,
              EXIT_FAILURE);
    log_print("Listening on port %u", port);

    return sock;
}

static int init_data(int client_sock)
{
    int const data_sock = listen_on(0);
    Q_FAIL_IF(data_sock < 0, -1);

    struct sockaddr_in address;
    socklen_t addr_size = sizeof(address);

    if (getsockname(data_sock, (struct sockaddr*) &address, &addr_size) < 0) {
        int const old_errno = errno;
        close(data_sock);
        errno = old_errno;

        return -1;
    }

    in_port_t const port = ntohs(address.sin_port);
    log_print("Created data connection; listening on port %u", port);

    Q_FAIL_IF(send_ack(client_sock, &port) != EXIT_SUCCESS, -1);
    log_print("Sent ack over control connection");

    char client_host[CFG_MAXHOST] = {0};
    Q_FAIL_IF(accept(data_sock, (struct sockaddr*) &address, &addr_size) < 0, -1);
    addr_to_hostname((struct sockaddr*) &address, addr_size,
                     client_host, sizeof(client_host));
    log_print("Accepted data client at %s:%u", client_host, address.sin_port);

    return data_sock;
}

static void server_exit(int client_sock)
{
    if (send_ack(client_sock, NULL) != EXIT_SUCCESS) {
        ERRMSG("send_ack", strerror(errno));
        cmd_exit(EXIT_FAILURE);
    } else {
        cmd_exit(EXIT_SUCCESS);
    }
}

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
        send_err(client_sock, "Server", "Invalid command given");
        return EXIT_SUCCESS;
    }
}

static int handle_data_cmd(int client_sock, int* data_sock,
        enum cmd_type cmd, char const* arg)
{
    // make sure data connection has been created
    if (*data_sock < 0) {
        send_err(client_sock, "Server", "Data connection not established.");
        return EXIT_SUCCESS;
    }

    (void) arg;
    bool success = false;
    char const* context = NULL;

    if (cmd == CMD_RLS) {
        int status = -1;
        success = cmd_ls(*data_sock, &status) == EXIT_SUCCESS;
        context = "cmd_ls";
        log_print("Exit status (ls): %d", status);
    } else if (cmd == CMD_GET) {
        context = "get";
    } else if (cmd == CMD_SHOW) {
        context = "show";
    } else if (cmd == CMD_PUT) {
        context = "put";
    } else {
        context = "Unknown command";
        log_print("Unexpected command %d; check info table for accuracy", cmd);
    }

    close(*data_sock);
    *data_sock = -1;
    return respond(client_sock, success, context);
}

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

        if (read_line(client_sock, message, CFG_MAXLINE - 1) > 0) {
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
        struct sockaddr addr = {0};
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

        char hostname[CFG_MAXHOST] = {0};
        GAI_FAIL_IF(addr_to_hostname(&addr, addr_len, hostname, CFG_MAXHOST),
                    "addr_to_hostname", EXIT_FAILURE);

        printf("Accepted connection from %s\n", hostname);

        // fork to have the child handle client so the parent can keep listening
        pid_t const current_pid = fork();
        FAIL_IF(current_pid < 0, "fork", EXIT_FAILURE);

        if (current_pid == 0) {
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
