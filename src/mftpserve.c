#include "config.h"
#include "util.h"

#include <stdbool.h>
#include <stdlib.h>

#include <unistd.h>

#include <sys/wait.h>

/*
 * handle_connection: Handle the connection given by the given file descriptor.
 *
 *                    Returns `EXIT_SUCCESS` or `EXIT_FAILURE` on success or
 *                    failure, respectively.
 */

static int handle_connection(int client_sock)
{
    // TODO: implement server commands, etc.

    close(client_sock);
    return EXIT_FAILURE;
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
    // info for a TCP socket
    struct addrinfo const info = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0
    };

    int const sock = make_socket(&info);
    FAIL_IF(sock < 0, "make_socket", EXIT_FAILURE);

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

    while (true) {
        struct sockaddr addr = {0};
        socklen_t addr_len = sizeof(struct sockaddr_in);
        int const client_sock = accept(sock, &addr, &addr_len);

        if (client_sock < 0) {
            ERRMSG("accept", strerror(errno));
            continue;
        }

        // fork to have the child handle client so the parent can keep listening
        pid_t const current_pid = fork();
        FAIL_IF(current_pid < 0, "fork", EXIT_FAILURE);

        if (current_pid == 0) {
            return handle_connection(client_sock);
        }

        // parent doesn't need client
        close(client_sock);

        // clean up process table if any children finished, but don't wait for
        // them to finish
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }

    return EXIT_FAILURE;
}

int main(void)
{
    return run_server();
}
