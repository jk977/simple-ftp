#include "commands.h"
#include "util.h"

#include <stdbool.h>

// table associating the command (with index based on `enum command` value)
// with a `bool` that indicates how many arguments are required
bool needs_arg[] = { false, true, true, false, false, true, true, true };

enum command cmd_parse(char const* msg, char const** p_arg)
{
    size_t const cmd_len = word_length(msg);

    // get pointer to beginning of command argument
    char const* arg = msg + cmd_len;
    arg += space_length(arg);

    if (arg[0] == '\0') {
        *p_arg = NULL;
    } else {
        *p_arg = arg;
    }

    enum command cmd = CMD_INVALID;

    if (strncmp(msg, "exit", cmd_len) == 0) {
        cmd = CMD_EXIT;
    } else if (strncmp(msg, "cd", cmd_len) == 0) {
        cmd = CMD_CD;
    } else if (strncmp(msg, "rcd", cmd_len) == 0) {
        cmd = CMD_RCD;
    } else if (strncmp(msg, "ls", cmd_len) == 0) {
        cmd = CMD_LS;
    } else if (strncmp(msg, "rls", cmd_len) == 0) {
        cmd = CMD_RLS;
    } else if (strncmp(msg, "get", cmd_len) == 0) {
        cmd = CMD_GET;
    } else if (strncmp(msg, "show", cmd_len) == 0) {
        cmd = CMD_SHOW;
    } else if (strncmp(msg, "put", cmd_len) == 0) {
        cmd = CMD_PUT;
    }

    bool const has_arg = (arg[0] != '\0');

    if (cmd == CMD_INVALID || needs_arg[cmd] != has_arg) {
        return CMD_INVALID;
    }

    return cmd;
}
