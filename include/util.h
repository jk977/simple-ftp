#ifndef UTIL_H_
#define UTIL_H_

#include <stddef.h>
#include <stdio.h>

#include <errno.h>

/*
 * FAIL_IF: Logs error message associated with `errno` with context information
 *          and returns `ret` if `cond` is truthy.
 *
 *          This greatly reduces boilerplate code without sacrificing the safety
 *          provided by checking for errors.
 */

#define FAIL_IF(cond, cause, ret)                                       \
    do {                                                                \
        if (cond) {                                                     \
            char const* msg = strerror(errno);                          \
            fprintf(stderr, "%s -> %s: %s\n", __func__, cause, msg);    \
            return ret;                                                 \
        }                                                               \
    } while (0)

size_t write_str(int fd, char const* str);
size_t read_line(int fd, char* buf, size_t max_bytes);

int exec_to_fd(int fd, int* status, char* const cmd[]);
int send_file(int dest_fd, int src_fd);

#endif // UTIL_H_
