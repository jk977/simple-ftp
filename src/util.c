#include "util.h"
#include "logging.h"

#include <stdlib.h>

#include <ctype.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

/*
 * is_newline: Predicate for newline characters.
 */

static int is_newline(int c)
{
    return c == '\n';
}

/*
 * is_not_newline: Predicate for non-newline characters.
 */

static int is_not_newline(int c)
{
    return !is_newline(c);
}

/*
 * is_not_space: Predicate for non-space characters.
 */

static int is_not_space(int c)
{
    return !isspace(c);
}

/*
 * count_chars: Return the number of characters at the beginning of `str` that
 *              pass the predicate `test_char()`.
 */

static size_t count_chars(char const* str, int (*test_char)(int))
{
    if (str == NULL) {
        return 0;
    }

    size_t len = 0;

    while (test_char(*str) && *str != '\0') {
        ++len;
        ++str;
    }

    return len;
}

/*
 * word_length: Returns the number of characters in the first word of `str`.
 */

size_t word_length(char const* str)
{
    return count_chars(str, is_not_space);
}

/*
 * space_length: Returns the number of spaces at the beginning of `str`.
 */

size_t space_length(char const* str)
{
    return count_chars(str, isspace);
}

/*
 * space_length: Returns the number of characters in the first line of `str`.
 */

size_t line_length(char const* str)
{
    return count_chars(str, is_not_newline);
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

static ssize_t read_all(int fd, char* buf, size_t max_bytes)
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
    log_print("Sending fd %d contents to fd %d", src_fd, dest_fd);

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
 * page_fd: Output the contents of `fd`, paged with `more -20`.
 *
 *          Returns `EXIT_SUCCESS` on successful paging, or `EXIT_FAILURE`
 *          otherwise.
 */

int page_fd(int fd)
{
    int pipes[2];
    Q_FAIL_IF(pipe(pipes) < 0, EXIT_FAILURE);

    pid_t const child = fork();
    Q_FAIL_IF(child < 0, EXIT_FAILURE);

    if (child == 0) {
        log_print("Running `more -20` from child %u", getpid());
        close(pipes[1]);

        char* const cmd[] = { "more", "-20", NULL };
        Q_FAIL_IF(dup2(pipes[0], STDIN_FILENO) < 0, EXIT_FAILURE);
        Q_FAIL_IF(execvp(cmd[0], cmd) < 0, EXIT_FAILURE);
    }

    close(pipes[0]);

    log_print("Sending fd %d to child %u", fd, child);
    int const status = send_file(pipes[1], fd);
    close(pipes[1]);

    Q_FAIL_IF(wait(NULL) < 0, EXIT_FAILURE);
    return status;
}

/*
 * basename_of: Wrapper for `basename(3)` that doesn't mutate its argument
 *              regardless of the usage of GNU C.
 *
 *              Returns a pointer to the base of the path, contained within
 *              the string `path`. Thus, if `path` goes out of scope or is
 *              freed by the caller, the return value is invalidated.
 */

char const* basename_of(char const* path)
{
    size_t const path_len = strlen(path);
    char path_copy[path_len + 1];
    strcpy(path_copy, path);

    char const* base = basename(path_copy);

    if (strcmp(path_copy, base) == 0) {
        // `path` doesn't contain any slashes
        return path;
    }

    size_t const base_len = strlen(base);
    size_t const path_offset = path_len - base_len;

    char const* path_base = path + path_offset;
    return path_base;
}

/*
 * send_path: Put the contents of the file at `src_path` into the file
 *            descriptor `dest_fd`.
 *
 *            Returns `EXIT_FAILURE` if `src_path` can't be opened (or
 *            already exists), or if there was an error while writing the
 *            file. Otherwise, returns `EXIT_SUCCESS`.
 */

int send_path(int dest_fd, char const* src_path)
{
    log_print("Sending %s contents to fd %d", src_path, dest_fd);

    int const src_fd = open(src_path, O_RDONLY);
    Q_FAIL_IF(src_fd < 0, EXIT_FAILURE);
    log_print("Opened %s at fd %d", src_path, src_fd);

    int const result = send_file(dest_fd, src_fd);
    close(src_fd);
    return result;
}

/*
 * receive_path: Put the contents of the file descriptor `src_fd` into the
 *               file at `dest_path`.
 *
 *               Returns `EXIT_FAILURE` if `dest_path` can't be opened (or
 *               already exists), or if there was an error while writing the
 *               file. Otherwise, returns `EXIT_SUCCESS`.
 */

int receive_path(char const* dest_path, int src_fd, unsigned int mode)
{
    log_print("Sending fd %d contents to %s with mode %o",
              src_fd, dest_path, mode);

    int const dest_fd = open(dest_path, O_CREAT | O_EXCL, mode);
    Q_FAIL_IF(dest_fd < 0, EXIT_FAILURE);
    log_print("Opened %s at fd %d", dest_path, dest_fd);

    int const result = send_file(dest_fd, src_fd);
    close(dest_fd);
    return result;
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

/*
 * get_info: Get info about the given host and port, printing the error message
 *           associated with the `getaddrinfo(3)` return value on failure.
 *
 *           Assumes that the host and port uses TCP.
 *
 *           Returns a pointer that must be freed with `freeaddrinfo(3)`, or
 *           `NULL` on failure.
 */

struct addrinfo* get_info(char const* host, char const* port)
{
    // hints for a TCP socket
    struct addrinfo const hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0
    };

    // get the actual info needed to connect client to the server
    struct addrinfo* info = NULL;
    GAI_FAIL_IF(getaddrinfo(host, port, &hints, &info), "getaddrinfo", NULL);
    return info;
}

