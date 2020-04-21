#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#if ! defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#error protocol.h requires _POSIX_C_SOURCE >= 200809L
#endif

enum message_type {
    RE_ACK = 'A',
    RE_ERROR = 'E',
    CMD_DATA = 'D',
    CMD_CHDIR = 'C',
    CMD_LIST = 'L',
    CMD_GET = 'G',
    CMD_PUT = 'P',
    CMD_QUIT = 'Q',
    MSG_INVALID = '\0',
};

struct message {
    enum message_type type;
    char* info;
};

int pcl_read_message(int fd, struct message* cmd);
int pcl_write_message(int fd, struct message const* cmd);

#endif // PROTOCOL_H_
