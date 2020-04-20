#ifndef PROTOCOL_H_
#define PROTOCOL_H_

enum response_type {
    RE_ERROR = 'E',
    RE_ACK = 'A',
};

enum command_type {
    CMD_DATA = 'D',
    CMD_CHDIR = 'C',
    CMD_LIST = 'L',
    CMD_GET = 'G',
    CMD_PUT = 'P',
    CMD_QUIT = 'Q',
    CMD_INVALID = '\0',
};

struct response {
    enum response_type type;
    char* info;
};

struct command {
    enum command_type type;
    char* arg;
};

int proto_read_command(int fd, struct command* cmd);
int proto_write_command(int fd, struct command const* cmd);

int proto_read_response(int fd, struct response* rsp);
int proto_write_response(int fd, struct response const* rsp);

#endif // PROTOCOL_H_
