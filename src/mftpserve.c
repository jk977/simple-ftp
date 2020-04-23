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

static int send_ack(int sock, int port)
{
    size_t const ack_len = 32;
    char ack[ack_len];
    memset(ack, '\0', sizeof(ack));

    if (port > 0) {
        snprintf(ack, ack_len, "A%d\n", port);
    } else {
        strcat(ack, "A\n");
    }

    if (write_str(sock, ack) != strlen(ack)) {
        return EXIT_FAILURE;
    } else {
        return EXIT_SUCCESS;
    }
}

static int send_err(int sock, char const* msg)
{
    size_t const err_len = strlen(msg) + 3;
    char err[err_len];
    memset(err, '\0', sizeof(err));

    err[0] = 'E';
    strcat(err, msg);
    strcat(err, "\n");

    if (write_str(sock, err) != err_len - 1) {
        return EXIT_FAILURE;
    } else {
        return EXIT_SUCCESS;
    }
}

static int init_data_sock(int* data_sock)
{
    *data_sock = make_socket(NULL);

    if (*data_sock < 0) {
        return -1;
    }

    struct in_addr const sin_addr = { .s_addr = INADDR_ANY };
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = 0,
        .sin_addr = sin_addr,
    };

    socklen_t addr_size = sizeof(address);

    if (bind(*data_sock, (struct sockaddr*) &address, addr_size) < 0) {
        close(*data_sock);
        *data_sock = -1;
        return -1;
    }

    if (getsockname(*data_sock, (struct sockaddr*) &address, &addr_size) < 0) {
        close(*data_sock);
        *data_sock = -1;
        return -1;
    }

    return ntohs(address.sin_port);
}

static int process_command(int client_sock, char const* cmd, int* data_sock)
{
    (void) data_sock;
    log_print("Received command from client: %s", cmd);

    char code = cmd[0];
    char const* arg = cmd + 1;

    if (code == cmd_get_ctl(CMD_EXIT)) {
        send_ack(client_sock, -1);
        cmd_exit(EXIT_SUCCESS);
    } else if (code == cmd_get_ctl(CMD_RCD)) {
        send_ack(client_sock, -1);
        return cmd_chdir(arg);
    } else if (code == cmd_get_ctl(CMD_DATA)) {
        int const port = init_data_sock(data_sock);

        if (port <= 0) {
            return send_err(client_sock, "Failed to open data connection.");
        } else {
            log_print("Opened data connection on port %d", port);
            return send_ack(client_sock, port);
        }
    }

    if (code == cmd_get_ctl(CMD_RLS)) {
        send_ack(client_sock, -1);
        log_print("rls command.");
    } else if (code == cmd_get_ctl(CMD_GET)) {
        send_ack(client_sock, -1);
        log_print("get command.");
    } else if (code == cmd_get_ctl(CMD_SHOW)) {
        send_ack(client_sock, -1);
        log_print("show command.");
    } else if (code == cmd_get_ctl(CMD_PUT)) {
        send_ack(client_sock, -1);
        log_print("put command.");
    } else {
        send_err(client_sock, "Invalid command given");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
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
            process_command(client_sock, message, &data_sock);
        } else {
            log_print("Failed to receive message from client");
            send_err(client_sock, strerror(errno));
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
    int const sock = make_socket(NULL);
    FAIL_IF(sock < 0, "make_socket", EXIT_FAILURE);
    log_print("Created socket at file descriptor %d", sock);

    struct in_addr const sin_addr = { .s_addr = INADDR_ANY };
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(CFG_PORT),
        .sin_addr = sin_addr,
    };

    // set up server to start listening for connections (any address)
    FAIL_IF(bind(sock, (struct sockaddr*) &address, sizeof(address)) < 0,
            "bind", EXIT_FAILURE);
    FAIL_IF(listen(sock, CFG_BACKLOG) < 0,
            "listen", EXIT_FAILURE);
    log_print("Listening on port %d", CFG_PORT);

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
