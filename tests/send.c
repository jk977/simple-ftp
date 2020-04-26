#include "commands.h"
#include "config.h"
#include "logging.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <unistd.h>

#include <sys/wait.h>

// the file name of the program
static char const* program;

/*
 * usage: Print program usage to the given file stream.
 */

static void usage(FILE* stream)
{
    fprintf(stream, "Usage:\n");
    fprintf(stream, "\t%s -h\n", program);
    fprintf(stream, "\t%s [-d] HOSTNAME\n\n", program);

    fprintf(stream, "Options:\n");
    fprintf(stream, "\t-h\tShow this help message and exit.\n");
    fprintf(stream, "\t-d\tEnable debug output.\n");
}

/*
 * msg_is_eof: Predicate for EOF server responses.
 */

static bool msg_is_eof(char const* msg)
{
    return msg[0] == '\0';
}

/*
 * get_response: Receive a response of max length `rsp_len - 1` from `sock` and
 *               store into `rsp`.
 *
 *               Returns the number of bytes stored in `rsp` on success, or -1
 *               on failure.
 */

static ssize_t get_response(int sock, char* rsp, size_t rsp_len)
{
    ssize_t const result = read_line(sock, rsp, rsp_len);
    Q_FAIL_IF(result < 0, -1);

    if (msg_is_eof(rsp)) {
        log_print("Received response from fd %d: EOF", sock);
    } else {
        log_print("Received response from fd %d: \"%s\" (%zd bytes)",
                  sock, rsp, result);
    }

    return result;
}

/*
 * connect_to: Connect to the given host and port.
 *
 *             Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or failure,
 *             respectively.
 */

static int connect_to(char const* host, char const* port)
{
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
 * client_run: Run the client, connecting to the server at `hostname`.
 *
 *             Returns `EXIT_FAILURE` or `EXIT_SUCCESS` on success or failure,
 *             respectively.
 */

static int client_run(char const* hostname)
{
    int const server_sock = connect_to(hostname, AS_STR(CFG_PORT));
    FAIL_IF(server_sock < 0, EXIT_FAILURE);

    while (true) {
        printf(CFG_PROMPT);
        fflush(stdout);

        char buf[CFG_MAXLINE] = {0};
        FAIL_IF(fgets(buf, sizeof buf, stdin) == NULL, EXIT_FAILURE);
        size_t const buf_len = strlen(buf);

        if (buf[buf_len - 1] == '\n') {
            buf[buf_len - 1] = '\0';
        }

        log_print("Sending \"%s\" to server", buf);
        FAIL_IF(dprintf(server_sock, "%s\n", buf) < 0, EXIT_FAILURE);

        memset(buf, '\0', sizeof buf);
        FAIL_IF(get_response(server_sock, buf, sizeof buf) < 0, EXIT_FAILURE);
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
