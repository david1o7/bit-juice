#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>

extern const char *doc_root; 
int create_server_socket(int port);
int send_all(int fd, const char *data, size_t len);

#endif