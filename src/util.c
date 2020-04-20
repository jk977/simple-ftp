#include "util.h"

#include <string.h>
#include <unistd.h>

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

/*
 * read_line: Read at most `max_bytes` from `fd` into `buf`, stopping when
 *            either no bytes are read, `max_bytes` have been read, a newline
 *            character is encountered, or an error occurs.
 *
 *            Returns the total number of bytes read.
 */

size_t read_line(int fd, char* buf, size_t max_bytes)
{
    size_t remaining = max_bytes;
    ssize_t prev_bytes;

    // read bytes one at a time to check for newlines
    while (remaining > 0 && (prev_bytes = read(fd, buf, 1)) > 0) {
        --remaining;
        ++buf;

        if (buf[-1] == '\n') {
            break;
        }
    }

    return max_bytes - remaining;
}
