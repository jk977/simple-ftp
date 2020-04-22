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

    // TODO: bind server, implement server control commands, etc.

    return EXIT_FAILURE;
}

int main(void)
{
    return run_server();
}
