#include "util.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>

#include <sys/wait.h>

size_t line_length(char const* str)
{
    if (str == NULL) {
        return 0;
    }

    size_t len = 0;

    while (*str != '\0' && *str != '\n') {
        ++len;
        ++str;
    }

    return len;
}

/*
 * write_str: Write `str` to `fd`, stopping on failure.
 *            Note that this function could be optimized to have less local
 *            variables, but it would sacrifice readability.
 *
 *            Returns the total number of bytes written.
 */

size_t write_str(int fd, char const* str)
{
    size_t remaining = strlen(str);
    size_t total_bytes = 0;
    ssize_t prev_bytes;

    while (remaining > 0 && (prev_bytes = write(fd, str, remaining)) > 0) {
        remaining -= prev_bytes;
        total_bytes += prev_bytes;
        str += prev_bytes;
    }

    return total_bytes;
}

static size_t read_until(int fd, char* buf, size_t max_bytes,
        int (*char_is_end)(int))
{
    size_t remaining = max_bytes;
    ssize_t prev_bytes;

    // read bytes one at a time to check for newlines
    while (remaining > 0 && (prev_bytes = read(fd, buf, 1)) > 0) {
        if (char_is_end != NULL && char_is_end(*buf)) {
            *buf = '\0';
            break;
        }

        --remaining;
        ++buf;
    }

    return max_bytes - remaining;
}

static int is_newline(int c)
{
    return c == '\n';
}

/*
 * read_all: Read at most `max_bytes` from `fd` into `buf`, stopping when
 *           either no bytes are read, `max_bytes` have been read, or an error
 *           occurs.
 *
 *           Returns the total number of bytes read.
 */

size_t read_all(int fd, char* buf, size_t max_bytes)
{
    return read_until(fd, buf, max_bytes, NULL);
}

/*
 * read_line: Read at most `max_bytes` from `fd` into `buf`, stopping when
 *            either no bytes are read, `max_bytes` have been read, a newline
 *            character is encountered, or an error occurs.
 *
 *            Returns the total number of bytes read.
 */

size_t read_line(int fd, char* buf, size_t max_bytes)
{
    return read_until(fd, buf, max_bytes, is_newline);
}

/*
 * send_file: Essentially the same as `sendfile(2)` (specifically, invoking
 *            `sendfile(dest_fd, src_fd, NULL, BUFSIZ)` repeatedly until all
 *            data is transferred) except data flows through
 *            userspace instead of being efficiently transferred in kernel
 *            space.
 *
 *            Returns `EXIT_SUCCESS` or `EXIT_FAILURE` on successful transfer
 *            of data or failure to transfer data, respectively.
 */

int send_file(int dest_fd, int src_fd)
{
    char buf[BUFSIZ] = {0};
    size_t prev_bytes;

    while ((prev_bytes = read_all(src_fd, buf, BUFSIZ)) > 0) {
        size_t const written_bytes = write_str(dest_fd, buf);
        FAIL_IF(written_bytes != prev_bytes, "write_str", EXIT_FAILURE);
        memset(buf, '\0', BUFSIZ);
    }

    return EXIT_SUCCESS;
}

/*
 * exec_to_fd: Forward the command given by `cmd` to `execvp(3)`, writing the
 *             process' output to `fd`.
 *
 *             On success, returns `EXIT_SUCCESS` and stores the command return
 *             status to `*status`. Otherwise, returns `EXIT_FAILURE` without
 *             writing to `*status`.
 */

int exec_to_fd(int fd, int* status, char* const cmd[])
{
    pid_t const child = fork();
    FAIL_IF(child < 0, "fork", EXIT_FAILURE);

    if (child == 0) {
        FAIL_IF(dup2(fd, STDOUT_FILENO) < 0, "dup2", EXIT_FAILURE);
        FAIL_IF(dup2(STDOUT_FILENO, STDERR_FILENO) < 0, "dup2", EXIT_FAILURE);
        FAIL_IF(execvp(cmd[0], cmd) < 0, "execvp", EXIT_FAILURE);
    }

    FAIL_IF(wait(status) < 0, "wait", EXIT_FAILURE);
    return EXIT_SUCCESS;
}

/*
 * make_socket: Create and configure a socket to match assignment description.
 *
 *              Returns the socket file descriptor on success, or -1 on failure
 *              with `errno` set accordingly. Doesn't return `EXIT_FAILURE` on
 *              failure since its value is usually 1, which is a valid file
 *              descriptor.
 */

int make_socket(struct addrinfo const* info)
{
    int sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    FAIL_IF(sock < 0, "socket", -1);

    int const opt_val = 1;
    size_t const opt_size = sizeof(opt_val);

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt_val, opt_size) < 0) {
        // prevent `close()` from changing `errno`
        int const old_errno = errno;
        close(sock);
        errno = old_errno;

        sock = -1;
    }

    return sock;
}

/*
 * addr_to_hostname: Writes the buffer with the human-readable form of the
 *                   given address.
 *
 *                   The return value is the result of `getnameinfo(3)`.
 */

int addr_to_hostname(struct sockaddr const* addr, socklen_t addrlen,
        char* host, socklen_t hostlen)
{
    return getnameinfo(addr, addrlen, host, hostlen, NULL, 0, 0);
}

