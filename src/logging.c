#include "logging.h"

#include <stdarg.h>
#include <stdio.h>

static bool enable_debug = false;

void log_debug(char const* fmt, ...)
{
    if (enable_debug) {
        va_list ap;

        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);

        printf("\n");
    }
}

void log_set_debug(bool status)
{
    enable_debug = status;
}
