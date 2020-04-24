#include "commands.h"
#include "logging.h"
#include "util.h"

#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>

struct command {
    enum cmd_type type;
    char const* arg;
};

struct cmd_info {
    bool has_arg;   // flag for required command argument
    bool is_remote; // flag for command that communicates with server
    char ctl;       // character used for command in control message
};

// table associating index (as an `enum cmd_type`) with command info
struct cmd_info const info_table[] = {
    { .has_arg = false, .is_remote = true,  .ctl = 'Q' },
    { .has_arg = true,  .is_remote = false,            },
    { .has_arg = true,  .is_remote = true,  .ctl = 'C' },
    { .has_arg = false, .is_remote = false,            },
    { .has_arg = false, .is_remote = true,  .ctl = 'L' },
    { .has_arg = true,  .is_remote = true,  .ctl = 'G' },
    { .has_arg = true,  .is_remote = true,  .ctl = 'S' },
    { .has_arg = true,  .is_remote = true,  .ctl = 'P' },
    { .has_arg = false, .is_remote = true,  .ctl = 'D' },
};

int cmd_get_ctl(enum cmd_type cmd)
{
    if (cmd == CMD_INVALID || !info_table[cmd].is_remote) {
        return -1;
    } else {
        return info_table[cmd].ctl;
    }
}

static struct command cmd_parse(char const* msg)
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
    enum cmd_type ct = result.type;

    if (ct != CMD_INVALID && info_table[ct].has_arg != has_arg) {
        result.type = CMD_INVALID;
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
    return exec_to_fd(fd, &status, cmd);
}

int cmd_exec_msg(int fd, char const* msg, char* rsp, size_t rsp_len)
{
    struct command const cmd = cmd_parse(msg);
    Q_FAIL_IF(cmd.type == CMD_INVALID, EXIT_FAILURE);

    // handle local commands
    if (cmd.type == CMD_LS) {
        return cmd_ls(STDOUT_FILENO);
    } else if (cmd.type == CMD_CD) {
        return cmd_chdir(cmd.arg);
    }

    struct cmd_info const* info = &info_table[cmd.type];

    // handle commands that require server communication
    if (cmd.arg != NULL) {
        Q_FAIL_IF(dprintf(fd, "%c%s\n", info->ctl, cmd.arg) < 0, EXIT_FAILURE);
    } else {
        Q_FAIL_IF(dprintf(fd, "%c\n", info->ctl) < 0, EXIT_FAILURE);
    }

    Q_FAIL_IF(read_line(fd, rsp, rsp_len) < 0, EXIT_FAILURE);

    if (cmd.type == CMD_EXIT) {
        cmd_exit(EXIT_SUCCESS);
    }

    return EXIT_SUCCESS;
}
