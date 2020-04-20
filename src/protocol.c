#include "protocol.h"
#include "util.h"

#include <stdlib.h>

#include <assert.h>
#include <limits.h>
#include <string.h>

static char* linecpy(char* dest, char const* src)
{
    if (dest == NULL || src == NULL) {
        return NULL;
    }

    while (*dest != '\0' && *src != '\0' && *src != '\n') {
        *dest = *src;
        ++dest;
        ++src;
    }

    *dest = '\0';
    return dest;
}

static enum message_type get_message_type(char c)
{
    switch (c) {
    case RE_ACK:
    case RE_ERROR:
    case CMD_DATA:
    case CMD_CHDIR:
    case CMD_LIST:
    case CMD_GET:
    case CMD_PUT:
    case CMD_QUIT:
        return (enum message_type) c;
    default:
        return MSG_INVALID;
    }
}

static int parse_message(char const* raw, struct message* msg)
{
    assert(raw != NULL);
    assert(raw[0] != '\0');
    assert(msg != NULL);

    char const* info = &raw[1];
    size_t const info_len = strlen(info) + sizeof('\0');

    char const msg_type = get_message_type(raw[0]);

    if (msg_type == MSG_INVALID) {
        return -1;
    }

    msg->type = msg_type;
    msg->info = malloc(info_len);

    memset(msg->info, '\0', info_len);

    if (msg->info == NULL) {
        return -1;
    }

    linecpy(msg->info, info);
    return 0;
}

int pcl_read_message(int fd, struct message* msg)
{
    assert(fd >= 0);
    assert(msg != NULL);

    if (msg == NULL) {
        return -1;
    }

    char buf[PATH_MAX + 1] = {0};
    size_t const line_len = read_line(fd, buf, PATH_MAX + 1);

    if (buf[line_len - 1] != '\n') {
        return -1;
    }

    return parse_message(buf, msg);
}

int pcl_write_message(int fd, struct message const* msg)
{
    assert(fd >= 0);
    assert(msg != NULL);
    assert(msg->type != MSG_INVALID);
    assert(msg->info != NULL);

    size_t const info_len = strlen(msg->info);
    size_t const msg_len = sizeof(msg->type) + info_len + sizeof('\n');

    char buf[msg_len + sizeof('\0')];
    memset(buf, '\0', sizeof(buf));

    buf[0] = msg->type;
    strcat(buf, msg->info);
    strcat(buf, "\n");

    if (write_str(fd, buf) < msg_len) {
        return -1;
    }

    return 0;
}
