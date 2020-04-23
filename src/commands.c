#include "commands.h"
#include "logging.h"
#include "util.h"

#include <stdbool.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>

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

void cmd_exit(int status)
{
    log_print("Exiting.");
    exit(status);
}

int cmd_chdir(char const* path)
{
    log_print("Changing directory to %s", path);
    return chdir(path);
}

int cmd_ls(int fd)
{
    log_print("Executing `ls -l`");

    char* cmd[] = { "ls", "-l", NULL };
    int status;

    if (exec_to_fd(fd, &status, cmd) != EXIT_SUCCESS) {
        return -1;
    }

    return status;
}

int cmd_put(int fd, char const* path)
{
    int const in_fd = open(path, O_RDONLY);
    FAIL_IF(in_fd < 0, "open", EXIT_FAILURE);
    return send_file(fd, in_fd);
}
