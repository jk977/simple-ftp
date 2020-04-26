#include "logging.h"

#include <stdarg.h>
#include <stdio.h>

#include <inttypes.h>
#include <time.h>

static bool enable_debug = false;

/*
 * log_print: Logs the given message with `printf(3)` formatting if
 *            logging is enabled. If logging is disabled, this is a no-op.
 */

void log_print(char const* fmt, ...)
{
    if (!enable_debug) {
        return;
    }

    // timestamp the output in nanoseconds for debugging purposes
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    printf("%" PRIdMAX ".%09ld - ", (intmax_t) tp.tv_sec, tp.tv_nsec);

    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    printf("\n");
}

/*
 * log_set_debug: Enable debugging if `status` is true, or disable it if
 *                `status` is false.
 */

void log_set_debug(bool status)
{
    enable_debug = status;
}
