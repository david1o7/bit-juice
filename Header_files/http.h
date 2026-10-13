#ifndef HTTP_H
#define HTTP_H

#include <sys/types.h>
#include <sys/socket.h>

void *client_thread(void *arg);
void handle_client(int clientfd);

#endif