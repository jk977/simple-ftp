#ifndef LOGGING_H_
#define LOGGING_H_

#include <stdbool.h>

void log_set_debug(bool status);
void log_print(char const* fmt, ...);

#endif // LOGGING_H_
