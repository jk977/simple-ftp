#include "commands.h"
#include "logging.h"
#include "util.h"

#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>

struct cmd_info {
    bool has_arg: 1;        // flag for required command argument
    bool is_remote: 1;      // flag for command that communicates with server
    bool needs_data: 1;     // flag for command that receives data from server
    char ctl;               // character used for command in control message
};

// table associating index (as an `enum cmd_type`) with command info
struct cmd_info const info_table[] = {
    { .has_arg = false, .is_remote = true,  .needs_data = false, .ctl = 'Q' },
    { .has_arg = true,  .is_remote = false, .needs_data = false             },
    { .has_arg = true,  .is_remote = true,  .needs_data = false, .ctl = 'C' },
    { .has_arg = false, .is_remote = false, .needs_data = false             },
    { .has_arg = false, .is_remote = true,  .needs_data = true,  .ctl = 'L' },
    { .has_arg = true,  .is_remote = true,  .needs_data = true,  .ctl = 'G' },
    { .has_arg = true,  .is_remote = true,  .needs_data = true,  .ctl = 'S' },
    { .has_arg = true,  .is_remote = true,  .needs_data = true,  .ctl = 'P' },
    { .has_arg = false, .is_remote = true,  .needs_data = false, .ctl = 'D' },
};

int cmd_get_ctl(enum cmd_type cmd)
{
    if (!cmd_is_remote(cmd)) {
        return -1;
    } else {
        return info_table[cmd].ctl;
    }
}

enum cmd_type cmd_get_type(char code)
{
    if (code == cmd_get_ctl(CMD_EXIT)) {
        return CMD_EXIT;
    } else if (code == cmd_get_ctl(CMD_CD)) {
        return CMD_CD;
    } else if (code == cmd_get_ctl(CMD_RCD)) {
        return CMD_RCD;
    } else if (code == cmd_get_ctl(CMD_LS)) {
        return CMD_LS;
    } else if (code == cmd_get_ctl(CMD_RLS)) {
        return CMD_RLS;
    } else if (code == cmd_get_ctl(CMD_GET)) {
        return CMD_GET;
    } else if (code == cmd_get_ctl(CMD_SHOW)) {
        return CMD_SHOW;
    } else if (code == cmd_get_ctl(CMD_PUT)) {
        return CMD_PUT;
    } else if (code == cmd_get_ctl(CMD_DATA)) {
        return CMD_DATA;
    } else {
        return CMD_INVALID;
    }
}

bool cmd_is_remote(enum cmd_type cmd)
{
    if (cmd == CMD_INVALID) {
        return false;
    } else {
        return info_table[cmd].is_remote;
    }
}

bool cmd_needs_data(enum cmd_type cmd)
{
    if (!cmd_is_remote(cmd)) {
        return false;
    } else {
        return info_table[cmd].needs_data;
    }
}

enum cmd_type cmd_parse(char const* msg, char const** p_arg)
{
    enum cmd_type result = CMD_INVALID;
    *p_arg = NULL;

    // get pointer to beginning of command argument
    size_t const cmd_len = word_length(msg);
    char const* arg = msg + cmd_len;
    arg += space_length(arg);

    if (arg[0] != '\0') {
        *p_arg = arg;
    }

    if (strncmp(msg, "exit", cmd_len) == 0) {
        result = CMD_EXIT;
    } else if (strncmp(msg, "cd", cmd_len) == 0) {
        result = CMD_CD;
    } else if (strncmp(msg, "rcd", cmd_len) == 0) {
        result = CMD_RCD;
    } else if (strncmp(msg, "ls", cmd_len) == 0) {
        result = CMD_LS;
    } else if (strncmp(msg, "rls", cmd_len) == 0) {
        result = CMD_RLS;
    } else if (strncmp(msg, "get", cmd_len) == 0) {
        result = CMD_GET;
    } else if (strncmp(msg, "show", cmd_len) == 0) {
        result = CMD_SHOW;
    } else if (strncmp(msg, "put", cmd_len) == 0) {
        result = CMD_PUT;
    }

    bool const has_arg = (arg[0] != '\0');

    if (result != CMD_INVALID && info_table[result].has_arg != has_arg) {
        result = CMD_INVALID;
    }

    return result;
}

void cmd_exit(int status)
{
    log_print("Exiting.");
    exit(status);
}

int cmd_chdir(char const* path)
{
    log_print("Changing directory to %s", path);
    Q_FAIL_IF(chdir(path) < 0, EXIT_FAILURE);
    return EXIT_SUCCESS;
}

int cmd_ls(int fd)
{
    int status;
    char* cmd[] = { "ls", "-l", NULL };
    log_print("Executing `ls -l`");
    Q_FAIL_IF(exec_to_fd(fd, &status, cmd) != EXIT_SUCCESS, -1);
    return status;
}
