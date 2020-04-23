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
    bool has_arg: 1;        // flag for required command argument
    bool is_remote: 1;      // flag for command that communicates with server
    bool needs_data: 1;     // flag for command that receives data from server
    char ctl;               // character used for command in control message
};

// table associating index (as an `enum cmd_type`) with command info
struct cmd_info const info_table[] = {
    { .has_arg = false, .is_remote = true,  .needs_data = false, .ctl = 'Q' },
    { .has_arg = true,  .is_remote = false, .needs_data = false },
    { .has_arg = true,  .is_remote = true,  .needs_data = false, .ctl = 'C' },
    { .has_arg = false, .is_remote = false, .needs_data = false },
    { .has_arg = false, .is_remote = true,  .needs_data = true, .ctl = 'L' },
    { .has_arg = true,  .is_remote = true,  .needs_data = true, .ctl = 'G' },
    { .has_arg = true,  .is_remote = true,  .needs_data = true, .ctl = 'S' },
    { .has_arg = true,  .is_remote = true,  .needs_data = true, .ctl = 'P' },
    { .has_arg = false,  .is_remote = true, .needs_data = false, .ctl = 'D' },
};

int cmd_get_ctl(enum cmd_type cmd)
{
    if (cmd == CMD_INVALID) {
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
    FAIL_IF(chdir(path) < 0, "chdir", EXIT_FAILURE);
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

    // handle local commands
    switch (cmd.type) {
    case CMD_INVALID:
        return EXIT_FAILURE;
    case CMD_LS:
        return cmd_ls(STDOUT_FILENO);
    case CMD_CD:
        return cmd_chdir(cmd.arg);
    default:
        break;
    }

    // handle commands that require server communication
    struct cmd_info const* info = &info_table[cmd.type];
    size_t const arg_len = (cmd.arg != NULL) ? strlen(cmd.arg) : 0;
    size_t const ctlbuf_len = arg_len + 3;

    char ctlbuf[ctlbuf_len];
    memset(ctlbuf, '\0', sizeof(ctlbuf));
    ctlbuf[0] = info->ctl;

    if (arg_len > 0) {
        strcat(ctlbuf, cmd.arg);
    }

    strcat(ctlbuf, "\n");
    size_t const written_bytes = write_str(fd, ctlbuf);
    log_print("Wrote %zu bytes to fd", written_bytes);

    if (written_bytes != ctlbuf_len - 1) {
        return EXIT_FAILURE;
    }

    FAIL_IF(read_line(fd, rsp, rsp_len) < 0, "read_line", EXIT_FAILURE);

    if (cmd.type == CMD_EXIT) {
        cmd_exit(EXIT_SUCCESS);
    }

    return EXIT_SUCCESS;
}
