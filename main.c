#include "server.h"
#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h> 
#include <time.h>   

#define PORT 8080

int main(void) {

    signal(SIGPIPE, SIG_IGN);

    int server_fd = create_server_socket(PORT);
    if (server_fd < 0) {
        return 1;
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int clientfd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (clientfd < 0) {
            perror("accept");
            continue;
        }

        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;

        if (setsockopt(clientfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("socksockopt SO_RCVTIMEO");
            close(clientfd);
            continue;
        }

        printf("Connection accepted from %s\n", inet_ntoa(client_addr.sin_addr));

        int *client_ptr = malloc(sizeof(int));
        if(!client_ptr) {
            perror("malloc");
            close(clientfd);
            continue;
        }

        *client_ptr = clientfd;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_thread, client_ptr) != 0){
            perror("pthread_create");
            free(client_ptr);
            close(clientfd);
            continue;
        }

        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}