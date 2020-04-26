#include "util.h"
#include "logging.h"

#include <stdbool.h>
#include <stdlib.h>

#include <ctype.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/wait.h>

/*
 * is_newline: Predicate for newline characters.
 */

int is_newline(int c)
{
    return c == '\n';
}

/*
 * is_not_newline: Predicate for non-newline characters.
 */

int is_not_newline(int c)
{
    return !is_newline(c);
}

/*
 * is_not_space: Predicate for non-space characters.
 */

int is_not_space(int c)
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
 * is_readable: Returns true if file is readable by the user, otherwise false.
 *              On failure, error and errno are set accordingly.
 *
 *              Note: access() returns -1 and sets errno to EACCES if the file
 *                    isn't readable. This function doesn't consider an
 *                    unreadable file to be an error, so a special check is done
 *                    to see if errno is EACCES in addition to checking if
 *                    access() returns 0.
 *
 *                    This function also is not concerned with whether or not
 *                    the file exists. It only checks if the file exists and is
 *                    readable.
 */

static bool is_readable(char const* path, bool* error)
{
    int const status = access(path, R_OK);
    *error = (status < 0) && (errno != EACCES);
    return status == 0;
}

/*
 * is_reg: Check if `path` is a regular file. `*error` is set to `true` if
 *         an error occurs, or `false` otherwise.
 */

bool is_reg(char const* path, bool* error)
{
    struct stat buf;
    *error = false;

    if (lstat(path, &buf) < 0) {
        *error = true;
        return false;
    }

    return S_ISREG(buf.st_mode);
}

/*
 * is_readable_reg: Check if `path` is a readable regular file. `*error` is set
 *                  to `true` if an error occurs, or `false` otherwise.
 */

bool is_readable_reg(char const* path, bool* error)
{
    bool const arg_is_readable = is_readable(path, error);
    Q_FAIL_IF(*error, false);
    return arg_is_readable && is_reg(path, error);
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
    size_t const opt_size = sizeof opt_val;

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
    GAI_FAIL_IF(getaddrinfo(host, port, &hints, &info), NULL);
    return info;
}

