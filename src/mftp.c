#include "commands.h"
#include "config.h"
#include "logging.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>

#include <ctype.h>
#include <unistd.h>

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

char const* get_cmd_arg(char const* cmd)
{
    cmd += word_length(cmd);
    cmd += space_length(cmd);
    return cmd;
}

int run_command(int sock, char const* msg)
{
    char response[BUFSIZ] = {0};
    
    if (cmd_exec(sock, msg, response, BUFSIZ) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    log_print("Server response: %s", response);
    return EXIT_SUCCESS;
}

int client_run(char const* hostname)
{
    // hints for a TCP socket
    struct addrinfo const hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0
    };

    char const* service = AS_STR(CFG_PORT);
    struct addrinfo* info = NULL;
    log_print("Connecting to %s:%s", hostname, service);

    // get the actual info needed to connect client to the server
    GAI_FAIL_IF(getaddrinfo(hostname, service, &hints, &info), "getaddrinfo",
            EXIT_FAILURE);

    int const sock = make_socket(info);
    FAIL_IF(sock < 0, "make_socket", EXIT_FAILURE);
    log_print("Created socket with file descriptor %d", sock);

    // copy the required data from the info struct and free it
    struct sockaddr const dest_addr = *info->ai_addr;
    socklen_t const dest_addrlen = info->ai_addrlen;
    freeaddrinfo(info);

    // use the provided info to connect to the server
    FAIL_IF(connect(sock, &dest_addr, dest_addrlen) < 0, "connect",
            EXIT_FAILURE);
    printf("Successfully connected to %s:%s\n", hostname, service);

    while (true) {
        printf(CFG_PROMPT);
        fflush(stdout);

        char buf[CFG_MAXLINE] = {0};
        FAIL_IF(fgets(buf, CFG_MAXLINE, stdin) == NULL, "fgets", EXIT_FAILURE);
        size_t const buf_len = strlen(buf);

        if (buf[buf_len - 1] == '\n') {
            buf[buf_len - 1] = '\0';
        }

        int const status = run_command(sock, buf);
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
