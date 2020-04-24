#include "util.h"

#include <stdlib.h>

#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/wait.h>

static int is_newline(int c)
{
    return c == '\n';
}

static int is_nonspace(int c)
{
    return !isspace(c);
}

static size_t count_chars(char const* str, int (*reject_char)(int))
{
    if (str == NULL) {
        return 0;
    }

    size_t len = 0;

    while (!reject_char(*str) && *str != '\0') {
        ++len;
        ++str;
    }

    return len;
}

size_t word_length(char const* str)
{
    return count_chars(str, isspace);
}

size_t space_length(char const* str)
{
    return count_chars(str, is_nonspace);
}

size_t line_length(char const* str)
{
    return count_chars(str, is_newline);
}

/*
 * write_str: Write `str` to `fd`, stopping on failure. `str` must be at most
 *            `SSIZE_MAX` bytes long, excluding the null-terminating byte.
 *
 *            Returns the total number of bytes written.
 */

ssize_t write_str(int fd, char const* str)
{
    size_t remaining = strlen(str);
    size_t total_bytes = 0;
    ssize_t prev_bytes;

    while (remaining > 0 && (prev_bytes = write(fd, str, remaining)) != 0) {
        if (prev_bytes < 0) {
            return prev_bytes;
        }

        remaining -= prev_bytes;
        total_bytes += prev_bytes;
        str += prev_bytes;
    }

    return total_bytes;
}

/*
 * read_until: Read from `fd` until either an EOF is reached, a byte is read
 *             that returns nonzero from `char_is_end()`, `max_bytes` have been
 *             read, or an error occurs. `str` must be at most `SSIZE_MAX` bytes
 *             long, excluding the null-terminating byte.
 *
 *             Returns the number of bytes read on success, or -1 on failure
 *             with `errno` set.
 */

static ssize_t read_until(int fd, char* buf, size_t max_bytes,
        int (*char_is_end)(int))
{
    size_t remaining = max_bytes;
    ssize_t prev_bytes;

    // read bytes one at a time to check for end
    while (remaining > 0 && (prev_bytes = read(fd, buf, 1)) != 0) {
        if (prev_bytes < 0) {
            return -1;
        } else if (char_is_end != NULL && char_is_end(*buf)) {
            *buf = '\0';
            break;
        }

        --remaining;
        ++buf;
    }

    return max_bytes - remaining;
}

/*
 * read_all: Read at most `max_bytes` from `fd` into `buf`, stopping when
 *           either no bytes are read, `max_bytes` have been read, or an error
 *           occurs. Wraps `read_until()`.
 *
 *           Returns the total number of bytes read.
 */

ssize_t read_all(int fd, char* buf, size_t max_bytes)
{
    return read_until(fd, buf, max_bytes, NULL);
}

/*
 * read_line: Read at most `max_bytes` from `fd` into `buf`, stopping when
 *            either no bytes are read, `max_bytes` have been read, a newline
 *            character is encountered, or an error occurs. Wraps
 *            `read_until()`.
 *
 *            Returns the total number of bytes read.
 */

ssize_t read_line(int fd, char* buf, size_t max_bytes)
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

    while ((prev_bytes = read_all(src_fd, buf, BUFSIZ - 1)) > 0) {
        size_t const written_bytes = write_str(dest_fd, buf);
        Q_FAIL_IF(written_bytes != prev_bytes, EXIT_FAILURE);
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
    Q_FAIL_IF(child < 0, EXIT_FAILURE);

    if (child == 0) {
        Q_FAIL_IF(dup2(fd, STDOUT_FILENO) < 0, EXIT_FAILURE);
        Q_FAIL_IF(dup2(STDOUT_FILENO, STDERR_FILENO) < 0, EXIT_FAILURE);
        Q_FAIL_IF(execvp(cmd[0], cmd) < 0, EXIT_FAILURE);
    }

    Q_FAIL_IF(wait(status) < 0, EXIT_FAILURE);
    return EXIT_SUCCESS;
}

/*
 * make_socket: Create and configure a socket with the given info. If `p_info`
 *              is `NULL`, defaults to a TCP socket.
 *
 *              Returns the socket file descriptor on success, or -1 on failure
 *              with `errno` set accordingly. Doesn't return `EXIT_FAILURE` on
 *              failure since its value is usually 1, which is a valid file
 *              descriptor.
 */

int make_socket(struct addrinfo const* p_info)
{
    struct addrinfo info;

    if (p_info != NULL) {
        info = *p_info;
    } else {
        // default to a TCP socket
        info = (struct addrinfo) {
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = 0
        };
    }

    int const sock = socket(info.ai_family, info.ai_socktype, info.ai_protocol);
    Q_FAIL_IF(sock < 0, -1);

    int const opt_val = 1;
    size_t const opt_size = sizeof(opt_val);

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt_val, opt_size) < 0) {
        // prevent `close()` from changing `errno`
        int const old_errno = errno;
        close(sock);
        errno = old_errno;
        return -1;
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

