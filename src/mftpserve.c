#include "config.h"
#include "util.h"

#include <stdlib.h>

int run_server(void)
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

    // TODO: implement server control commands, etc.

    return EXIT_FAILURE;
}

int main(void)
{
    return run_server();
}
