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

/*
 * cmd_get_ctl: Return the control character for `cmd`, or -1 if the command has
 *              no control character.
 */

int cmd_get_ctl(enum cmd_type cmd)
{
    if (!cmd_is_remote(cmd)) {
        return -1;
    } else {
        return info_table[cmd].ctl;
    }
}

/*
 * cmd_get_type: Return the `enum cmd_type` equivalent of the control character,
 *               or `CMD_INVALID` if `ctl` does not correspond to a command.
 */

enum cmd_type cmd_get_type(char ctl)
{
    if (ctl == cmd_get_ctl(CMD_EXIT)) {
        return CMD_EXIT;
    } else if (ctl == cmd_get_ctl(CMD_CD)) {
        return CMD_CD;
    } else if (ctl == cmd_get_ctl(CMD_RCD)) {
        return CMD_RCD;
    } else if (ctl == cmd_get_ctl(CMD_LS)) {
        return CMD_LS;
    } else if (ctl == cmd_get_ctl(CMD_RLS)) {
        return CMD_RLS;
    } else if (ctl == cmd_get_ctl(CMD_GET)) {
        return CMD_GET;
    } else if (ctl == cmd_get_ctl(CMD_SHOW)) {
        return CMD_SHOW;
    } else if (ctl == cmd_get_ctl(CMD_PUT)) {
        return CMD_PUT;
    } else if (ctl == cmd_get_ctl(CMD_DATA)) {
        return CMD_DATA;
    } else {
        return CMD_INVALID;
    }
}

/*
 * cmd_is_remote: Return whether or not the command is executed on the server.
 */

bool cmd_is_remote(enum cmd_type cmd)
{
    if (cmd == CMD_INVALID) {
        return false;
    } else {
        return info_table[cmd].is_remote;
    }
}

/*
 * cmd_needs_data: Return whether or not the command requires a data connection.
 */

bool cmd_needs_data(enum cmd_type cmd)
{
    if (!cmd_is_remote(cmd)) {
        return false;
    } else {
        return info_table[cmd].needs_data;
    }
}

/*
 * cmd_parse: Parse the user-supplied command in `msg`.
 *
 *            Returns a `struct command` containing the command and argument
 *            provided. If `msg` is an invalid command, the return value will
 *            contain CMD_INVALID. On success, the `.arg` member of the return
 *            value will have the same lifetime as `msg`, and should not be
 *            freed.
 */

struct command cmd_parse(char const* msg)
{
    struct command result = {
        .type = CMD_INVALID,
        .arg = NULL,
    };

    // get pointer to beginning of command argument
    size_t const cmd_len = word_length(msg);
    char const* arg = msg + cmd_len;
    arg += space_length(arg);

    if (arg[0] != '\0') {
        result.arg = arg;
    }

    if (strncmp(msg, "exit", cmd_len) == 0) {
        result.type = CMD_EXIT;
    } else if (strncmp(msg, "cd", cmd_len) == 0) {
        result.type = CMD_CD;
    } else if (strncmp(msg, "rcd", cmd_len) == 0) {
        result.type = CMD_RCD;
    } else if (strncmp(msg, "ls", cmd_len) == 0) {
        result.type = CMD_LS;
    } else if (strncmp(msg, "rls", cmd_len) == 0) {
        result.type = CMD_RLS;
    } else if (strncmp(msg, "get", cmd_len) == 0) {
        result.type = CMD_GET;
    } else if (strncmp(msg, "show", cmd_len) == 0) {
        result.type = CMD_SHOW;
    } else if (strncmp(msg, "put", cmd_len) == 0) {
        result.type = CMD_PUT;
    }

    bool const has_arg = (arg[0] != '\0');
    bool const needs_arg = info_table[result.type].has_arg;

    if (result.type != CMD_INVALID && needs_arg != has_arg) {
        result.type = CMD_INVALID;
    }

    return result;
}

/*
 * cmd_exit: Exit the process with `status`. Used instead of calling `exit(3)`
 *           directly to allow other commands to be added to the exit routine.
 */

void cmd_exit(int status)
{
    log_print("Exiting.");
    exit(status);
}

/*
 * cmd_chdir: Change directories to `path`.
 *
 *            Returns `EXIT_SUCCESS` or `EXIT_FAILURE` on success or failure,
 *            respectively.
 */

int cmd_chdir(char const* path)
{
    log_print("Changing directory to %s", path);
    Q_FAIL_IF(chdir(path) < 0, EXIT_FAILURE);
    return EXIT_SUCCESS;
}

/*
 * cmd_ls: Run `ls -l`, sending output to `fd`.
 *
 *         Returns -1 if `exec_to_fd()` fails, or the exit status of the `ls`
 *         command on successful execution.
 */

int cmd_ls(int fd)
{
    int status;
    char* const cmd[] = { "ls", "-l", NULL };
    log_print("Executing `ls -l`");
    Q_FAIL_IF(exec_to_fd(fd, &status, cmd) != EXIT_SUCCESS, -1);
    return status;
}
