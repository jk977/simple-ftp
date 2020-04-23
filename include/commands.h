#ifndef COMMANDS_H_
#define COMMANDS_H_

enum command {
    CMD_INVALID = -1,
    CMD_EXIT = 0,
    CMD_CD = 1,
    CMD_RCD = 2,
    CMD_LS = 3,
    CMD_RLS = 4,
    CMD_GET = 5,
    CMD_SHOW = 6,
    CMD_PUT = 7,
};

enum command cmd_parse(char const* msg, char const** p_arg);

#endif // COMMANDS_H_
