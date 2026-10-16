#include "../Header_files/http.h"
#include "../Header_files/server.h"

#include <stdio.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h> 
#include <time.h> 
#include <getopt.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define THREAD_POOL_SIZE 8
#define QUEUE_SIZE 128
#define QUEUE_MASK (QUEUE_SIZE - 1) 

int PORT = 8080;
int client_queue[QUEUE_SIZE];
int g_verbose = 0;
int queue_count = 0;
int queue_head = 0;
int running = 1;
int queue_tail = 0;



pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond  = PTHREAD_COND_INITIALIZER;

const char *doc_root = "../public";
int read_timeout_sec = 12;
char *g_doc_root = NULL;

static int set_nonblocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(void) {

    int server_fd = create_server_socket(PORT);
    if (server_fd < 0) {
        return 1;
    }

    if (set_nonblocking(server_fd) < 0){
        perror("set_nonblocking");
        close(server_fd);
        return 1;
    }

    int epfd = epoll_create1(0);
    if (epfd < 0){
        perror("epoll_create1");
        close(server_fd);
        return 1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) < 0){
        perror("epoll_ctl");
        close(server_fd);
        return 1;
    }

    struct epoll_event events[64];

    while (running) {
        int n = epoll_wait(epfd, events, 64, 1000);
        if (n < 0){
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0;i < n; i++){
            if (events[i].data.fd != server_fd) continue;

            for (;;) {
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            int cfd = accept(server_fd, (struct sockaddr *)&addr, &len);

            if (cfd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; 
                if (errno == EINTR) continue;
                perror("accept");
                break;
            }

            printf("accepted fd=%d from %s\n", cfd, inet_ntoa(addr.sin_addr));
            close(cfd);
            }
        }
    }
    close(server_fd);
    close(epfd);
}