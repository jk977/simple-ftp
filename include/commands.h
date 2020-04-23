#ifndef COMMANDS_H_
#define COMMANDS_H_

#include <stddef.h>

int cmd_exec(int fd, char const* msg, char* rsp, size_t rsp_len);

#endif // COMMANDS_H_
