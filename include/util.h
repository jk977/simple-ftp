#ifndef UTIL_H_
#define UTIL_H_

#include <stddef.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>

#include <netdb.h>

#include <sys/types.h>
#include <sys/socket.h>

#define ERRMSG(cause, msg) \
    fprintf(stderr, "%s -> %s: %s\n", __func__, cause, msg)

/*
 * FAIL_IF: Logs error message associated with `errno` with context information
 *          and returns `ret` if `cond` is truthy.
 *
 *          This greatly reduces boilerplate code without sacrificing the safety
 *          provided by checking for errors.
 */

#define FAIL_IF(cond, cause, ret)           \
    do {                                    \
        if (cond) {                         \
            ERRMSG(cause, strerror(errno)); \
            return ret;                     \
        }                                   \
    } while (0)

/*
 * Q_FAIL_IF: Does the same as `FAIL_IF` but doesn't output anything.
 */

#define Q_FAIL_IF(cond, ret)    \
    do {                        \
        if (cond) {             \
            return ret;         \
        }                       \
    } while (0)

/*
 * GAI_FAIL_IF: Does the same as `FAIL_IF`, but for `getaddrinfo(3)` and related
 *              functions that use `gai_strerror(3)`.
 */

#define GAI_FAIL_IF(code, cause, ret)           \
    do {                                        \
        int const _code = code;                 \
        if (_code != 0) {                       \
            ERRMSG(cause, gai_strerror(_code)); \
            return ret;                         \
        }                                       \
    } while (0)

#define STR(val)      #val       /* stringify the value passed */
#define AS_STR(macro) STR(macro) /* expand + stringify the value/macro passed */

size_t word_length(char const* str);
size_t space_length(char const* str);
size_t line_length(char const* str);

ssize_t write_str(int fd, char const* str);
ssize_t read_line(int fd, char* buf, size_t max_bytes);

int exec_to_fd(int fd, int* status, char* const cmd[]);
int send_file(int dest_fd, int src_fd);
char const* basename_of(char const* path);

int send_path(int dest_fd, char const* src_path);
int receive_path(char const* dest_path, int src_fd);

int make_socket(struct addrinfo const* info);
int addr_to_hostname(struct sockaddr const* addr, socklen_t addrlen,
        char* host, socklen_t hostlen);

#endif // UTIL_H_
