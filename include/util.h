#ifndef UTIL_H_
#define UTIL_H_

#include <stddef.h>

size_t write_str(int fd, char const* str);
size_t read_line(int fd, char* buf, size_t max_bytes);

#endif // UTIL_H_
