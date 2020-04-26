#include "io.h"
#include "logging.h"
#include "util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <sys/wait.h>

/*
 * write_str: Write `str` to `fd`, stopping on failure. `str` must be at most
 *            `SSIZE_MAX` bytes long, excluding the null-terminating byte.
 *
 *            Returns the total number of bytes written, or -1 on failure with
 *            `errno` set.
 */

ssize_t write_str(int fd, char const* str)
{
    size_t remaining = strlen(str);
    ssize_t total_bytes = 0;
    ssize_t prev_bytes;

    while (remaining > 0 && (prev_bytes = write(fd, str, remaining)) != 0) {
        if (prev_bytes < 0) {
            return -1;
        }

        remaining -= prev_bytes;
        total_bytes += prev_bytes;
        str += prev_bytes;
    }

    log_print("Wrote %zd bytes to fd %d", total_bytes, fd);
    return total_bytes;
}

/*
 * read_until: Read from `fd` until either an EOF is reached, a byte is read
 *             that returns nonzero from `char_is_end()`, `buf_size - 1` bytes
 *             have been read, or an error occurs. `str` must be at most
 *             `SSIZE_MAX` bytes long, excluding the null-terminating byte.
 *
 *             Returns the number of bytes read on success, or -1 on failure
 *             with `errno` set.
 */

static ssize_t read_until(int fd, char* buf, size_t buf_size,
        int (*char_is_end)(int))
{
    size_t remaining = buf_size - 1;
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

    ssize_t const total_bytes = buf_size - remaining - 1;
    log_print("Read %zd bytes from fd %d", total_bytes, fd);
    return total_bytes;
}

/*
 * read_all: Read at most `buf_size - 1` bytes from `fd` into `buf`, stopping
 *           when either no bytes are read, `max_bytes` have been read, or an
 *           error occurs. Wraps `read_until()`.
 *
 *           Returns the total number of bytes read.
 */

static ssize_t read_all(int fd, char* buf, size_t buf_size)
{
    return read_until(fd, buf, buf_size, NULL);
}

/*
 * read_line: Read at most `buf_size - 1` bytes from `fd` into `buf`, stopping
 *            when either no bytes are read, `max_bytes` have been read, a
 *            newline character is encountered, or an error occurs. Wraps
 *            `read_until()`.
 *
 *            Returns the total number of bytes read.
 */

ssize_t read_line(int fd, char* buf, size_t buf_size)
{
    return read_until(fd, buf, buf_size, is_newline);
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
 *            space. This is used over `sendfile(2)` due to the latter being
 *            prohibited by the instructor.
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
 * page_fd: Output the contents of `fd` to `stdout`, paged with `more -20`.
 *          To do this, a pipe is set up, a child process is spawned, and the
 *          child redirects `stdin` to the read end of the pipe. Once this
 *          is set up, the child can execute `more -20` while the parent
 *          feeds the file descriptor into the write end of the pipe.
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
        // child doesn't need write end of pipe
        close(pipes[1]);
        Q_FAIL_IF(dup2(pipes[0], STDIN_FILENO) < 0, EXIT_FAILURE);

        log_print("Running `more -20` from child %u", getpid());
        char* const cmd[] = { "more", "-20", NULL };
        Q_FAIL_IF(execvp(cmd[0], cmd) < 0, EXIT_FAILURE);
    }

    // parent doesn't need read end of pipe
    close(pipes[0]);

    log_print("Sending fd %d to child %u", fd, child);
    int const status = send_file(pipes[1], fd);
    close(pipes[1]);

    Q_FAIL_IF(wait(NULL) < 0, EXIT_FAILURE);
    return status;
}

/*
 * send_path: Put the contents of the file at `src_path` into the file
 *            descriptor `dest_fd`.
 *
 *            Returns `EXIT_FAILURE` if `src_path` can't be opened (or
 *            already exists), or if there was an error while writing the
 *            file. Otherwise, returns `EXIT_SUCCESS`.
 *
 *            In the case that `src_path` is not a regular file, in addition
 *            to returning an error code, `errno` is also set to `ENOTSUP`.
 */

int send_path(int dest_fd, char const* src_path)
{
    bool error;
    bool path_is_reg = is_reg(src_path, &error);
    Q_FAIL_IF(error, EXIT_FAILURE);

    if (!path_is_reg) {
        errno = ENOTSUP;
        return EXIT_FAILURE;
    }

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
 *
 *               In the case that `dest_path` is not a regular file, in addition
 *               to returning an error code, `errno` is also set to `ENOTSUP`.
 */

int receive_path(char const* dest_path, int src_fd, unsigned int mode)
{
    log_print("Sending fd %d contents to %s with mode %o",
              src_fd, dest_path, mode);

    int const dest_fd = open(dest_path, O_WRONLY | O_CREAT | O_EXCL, mode);
    Q_FAIL_IF(dest_fd < 0, EXIT_FAILURE);
    log_print("Opened %s at fd %d", dest_path, dest_fd);

    int const result = send_file(dest_fd, src_fd);
    close(dest_fd);
    return result;
}

