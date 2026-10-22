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

volatile sig_atomic_t running = 1;
volatile int localfd = -1;

int PORT = 8080;
int client_queue[QUEUE_SIZE];
int g_verbose = 0;
int queue_count = 0;
int queue_head = 0;
int queue_tail = 0;

void handle_sig(int val){
    (void) val;
    running = 0;

    if (localfd != -1){
        shutdown(localfd, SHUT_RDWR);
    }
}

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

    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);
    signal(SIGPIPE, SIG_IGN);

    int server_fd = create_server_socket(PORT);
    if (server_fd < 0) {
        return 1;
    }

    localfd = server_fd;

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
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) < 0){
        perror("epoll_ctl");
        close(server_fd);
        return 1;
    }

    printf("server listening on port %d...\n", PORT);

    struct epoll_event events[64];

    while (running) {
        int n = epoll_wait(epfd, events, 64, 1000);
        if (n < 0){
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0;i < n; i++){
            int fd = events[i].data.fd ;
            uint32_t what = events[i].events;


            if (fd == server_fd){
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

                if (set_nonblocking(cfd) < 0){
                    perror("cfd_nonblocking");
                    close(cfd);
                    continue;
                }

                struct epoll_event cfd_ev;
                memset(&cfd_ev, 0, sizeof(cfd_ev));
                cfd_ev.events = EPOLLIN | EPOLLRDHUP;
                cfd_ev.data.fd = cfd;

                if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cfd_ev) < 0){
                    perror("epoll_ctl ADD client");
                    close(cfd);
                    continue;
                }

                printf("accepted [fd=%d] (watching for read)\n", cfd);

                }
                continue;
            }
            
            if (what & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                printf("[fd=%d] hangup/error\n", fd);
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
                continue;
            }

            if (what & EPOLLIN){
                char buffer[4096];
                ssize_t r = recv(fd, buffer, sizeof(buffer) - 1, 0);

                if (r < 0){
                    if (errno == EAGAIN || errno == EWOULDBLOCK){
                        continue;
                    }
                    perror("recv");
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }

                if (r == 0){
                    printf("[fd= %d] client closed\n", fd);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }

                buffer[r] = '\0';
                printf("[fd=%d] got %zd bytes:\n%s\n", fd, r, buffer);

                char resp[256];
                char *body = "Hello from bitjuice via epoll";

                int s = snprintf(resp, sizeof(resp), 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "%s\n",
                    strlen(body), body
                );

                if (s < 0){
                    fprintf(stderr, "Snprintf encountered an error");
                    printf("[fd= %d] client closed\n", fd);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }

                size_t len = strlen(resp);
                size_t sent = 0;

                while (sent < len){
                    ssize_t n = send(fd, resp + sent, len - sent, 0);
                    if (n < 0){
                        if (errno == EAGAIN || errno == EWOULDBLOCK){
                            printf("[fd=%d] socket not ready to write yet\n", fd);
                            break;
                        }
                        if (errno == EPIPE || errno == ECONNRESET){
                            break;
                        }
                        perror("send");
                        break;
                    }
                    sent += (size_t)n;
                }

                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
            }
        }
    }
    printf("Shutting down\n");
    close(epfd);
    close(server_fd);
    return 0;
}