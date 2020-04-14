#ifndef LOGGING_H_
#define LOGGING_H_

#include <stdbool.h>

void log_debug(char const* fmt, ...);
void log_set_debug(bool status);

#endif // LOGGING_H_
