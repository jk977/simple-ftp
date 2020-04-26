#ifndef UTIL_H_
#define UTIL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>

#include <netdb.h>

/*
 * ERRMSG: Prints a formatted error with content `msg`.
 */

#define ERRMSG(msg) fprintf(stderr, "Error: %s\n", msg)

/*
 * FAIL_IF: Logs error message associated with `errno` and returns `ret` if
 *          `cond` is truthy.
 *
 *          This greatly reduces boilerplate code without sacrificing the safety
 *          provided by checking for errors.
 */

#define FAIL_IF(cond, ret)              \
    do {                                \
        if (cond) {                     \
            ERRMSG(strerror(errno));    \
            return ret;                 \
        }                               \
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

#define GAI_FAIL_IF(code, ret)              \
    do {                                    \
        int const _code = code;             \
        if (_code != 0) {                   \
            ERRMSG(gai_strerror(_code));    \
            return ret;                     \
        }                                   \
    } while (0)

#define STR(val)      #val       /* stringify the value passed */
#define AS_STR(macro) STR(macro) /* expand + stringify the value/macro passed */

int is_newline(int c);
int is_not_newline(int c);
int is_not_space(int c);

size_t word_length(char const* str);
size_t space_length(char const* str);
size_t line_length(char const* str);

char const* basename_of(char const* path);
bool is_readable(char const* path, bool* error);
bool is_reg(char const* path, bool* error);

int make_socket(struct addrinfo const* info);
int addr_to_hostname(struct sockaddr const* addr, socklen_t addrlen,
        char* host, socklen_t hostlen);
struct addrinfo* get_info(char const* host, char const* port);

#endif // UTIL_H_
