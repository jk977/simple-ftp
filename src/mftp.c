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
    (void) sock;

    char const* arg;
    enum command cmd = cmd_parse(msg, &arg);

    switch (cmd) {
    case CMD_EXIT:
        cmd_exit(EXIT_SUCCESS);
        return EXIT_FAILURE;
    case CMD_CD:
        return cmd_chdir(arg);
    case CMD_RCD:
        log_print("rcd (%s) command.", arg);
        break;
    case CMD_LS:
        return cmd_ls(STDOUT_FILENO);
    case CMD_RLS:
        log_print("rls command.");
        break;
    case CMD_GET:
        log_print("get (%s) command.", arg);
        break;
    case CMD_SHOW:
        log_print("show (%s) command.", arg);
        break;
    case CMD_PUT:
        log_print("put (%s) command.", arg);
        break;
    case CMD_INVALID:
        log_print("invalid command.");
        return EXIT_FAILURE;
    }

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

        run_command(sock, buf);
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
