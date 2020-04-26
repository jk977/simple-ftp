#ifndef IO_H_
#define IO_H_

#include <unistd.h>

ssize_t write_str(int fd, char const* str);
ssize_t read_line(int fd, char* buf, size_t max_bytes);

int exec_to_fd(int fd, int* status, char* const cmd[]);

int send_file(int dest_fd, int src_fd);
int page_fd(int fd);

int send_path(int dest_fd, char const* src_path);
int receive_path(char const* dest_path, int src_fd, unsigned int mode);

#endif // IO_H_
