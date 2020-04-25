#include "logging.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include <inttypes.h>
#include <time.h>

static bool enable_debug = false;

void log_print(char const* fmt, ...)
{
    if (!enable_debug) {
        return;
    }

    // timestamp the output
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    printf("%"PRIdMAX".%"PRIdMAX" - ", tp.tv_sec, tp.tv_nsec);

    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    printf("\n");
}

void log_set_debug(bool status)
{
    enable_debug = status;
}
