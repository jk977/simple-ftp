#ifndef COMMANDS_H_
#define COMMANDS_H_

#include <stdbool.h>
#include <stddef.h>

enum cmd_type {
    CMD_EXIT = 0,
    CMD_CD = 1,
    CMD_RCD = 2,
    CMD_LS = 3,
    CMD_RLS = 4,
    CMD_GET = 5,
    CMD_SHOW = 6,
    CMD_PUT = 7,
    CMD_DATA = 8,
    CMD_INVALID,
};

enum rsp_code {
    RSP_ACK = 'A',
    RSP_ERR = 'E',
};

enum cmd_type cmd_get_type(char code);
int cmd_get_ctl(enum cmd_type cmd);
bool cmd_is_remote(enum cmd_type cmd);
bool cmd_needs_data(enum cmd_type cmd);

enum cmd_type cmd_parse(char const* msg, char const** p_arg);

int cmd_exec_msg(int fd, char const* msg, char* rsp, size_t rsp_len);

void cmd_exit(int status);
int cmd_chdir(char const* path);
int cmd_ls(int fd, int* status);

#endif // COMMANDS_H_
