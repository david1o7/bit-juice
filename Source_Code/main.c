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

#define RECV_BUF_SIZE 4096

typedef struct {
    int fd;
    char buf[RECV_BUF_SIZE];
    size_t len;
} connection_t;

volatile sig_atomic_t running = 1;
volatile int localfd = -1;

int PORT = 8080;

void handle_sig(int val){
    (void) val;
    running = 0;

    if (localfd != -1){
        shutdown(localfd, SHUT_RDWR);
    }
}

int parse_req_line(const char *buf, char *method, size_t method_sz, char *path, size_t path_sz){
    if (method_sz < (size_t)15 || path_sz < (size_t)255){
        printf("path and method buffer size too small to hold values!");
        return 0;
    }
    if (sscanf(buf, "%15s %255s", method, path) < 2){
        return 0;
    }
    (void)method_sz;
    (void)path_sz;
    return 1;
}

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
    // TODO: check why this g_doc_root is meant to be freed at server shutdown & fast error quit.

    const char *doc_root = "../public";
    g_doc_root = realpath(doc_root, NULL);

    if (!g_doc_root){
        perror("realpath(doc_root)");
        return 1;
    }

    printf("serving from %s\n", g_doc_root);

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

            if (events[i].data.fd == server_fd){
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

                connection_t *c = calloc(1, sizeof(*c));
                if (!c){
                    close(server_fd);
                    continue;
                }

                c->fd = cfd;
                c->len = 0;

                struct epoll_event cfd_ev;
                memset(&cfd_ev, 0, sizeof(cfd_ev));
                cfd_ev.events = EPOLLIN | EPOLLRDHUP;
                cfd_ev.data.ptr = c;

                if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cfd_ev) < 0){
                    perror("epoll_ctl ADD client");
                    free(c);
                    close(cfd);
                    continue;
                }

                printf("accepted [fd=%d] (watching for read)\n", cfd);

                }
                continue;
            }

            connection_t *c = events[i].data.ptr;
            uint32_t what = events[i].events;
            int fd = c->fd;
            
            if (what & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                printf("[fd=%d] hangup/error\n", fd);
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
                continue;
            }

            if (what & EPOLLIN){
                if (c->len > RECV_BUF_SIZE - 1){
                    printf("[fd= %d] Snprintf error, client closed\n", fd);
                    free(c);
                    close(fd);
                    continue;
                }

                ssize_t r = recv(fd, c->buf + c->len, RECV_BUF_SIZE - 1 - c->len, 0);

                if (r < 0){
                    if (errno == EAGAIN || errno == EWOULDBLOCK){
                        continue;
                    }
                    perror("recv");
                    free(c);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }

                if (r == 0){
                    printf("[fd= %d] client closed\n", fd);
                    free(c);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }

                c->len += (size_t)r;
                c->buf[c->len] = '\0';

                if (strstr(c->buf, "\r\n\r\n") == NULL){
                    continue;
                }

                printf("fd=%d full headers:\n%s\n", fd, c->buf);

                char method[16];
                char path[256];

                const char *status;
                const char *body;

                if (!parse_req_line(c->buf, method, sizeof(method), path, sizeof(path))){
                    status = "HTTP/1.1 400 Bad Request";
                    body = "Bad Request\n";
                    } else if (strcmp(method, "GET") != 0) {
                        status = "HTTP/1.1 405 Method Not Allowed";
                        body = "Method Not Allowed\n";
                    } else if (strcmp(path, "/") == 0 || strcmp(path, "/home") == 0) {
                        status = "HTTP/1.1 200 OK";
                        body = "Home page (epoll)\n";
                    } else if (strcmp(path, "/about") == 0) {
                        status = "HTTP/1.1 200 OK";
                        body = "About page (epoll)\n";
                    } else {
                        status = "HTTP/1.1 404 Not Found";
                        body = "Not Found\n";
                    }

                char resp[1024];

                int s = snprintf(resp, sizeof(resp), 
                    "%s\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "%s\n",
                    status,strlen(body), body
                );

                if (s < 0 || s >= (int)sizeof(resp)){
                    printf("[fd= %d] Snprintf error or response size larger than allocated buffer size\n", fd);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    free(c);
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
                free(c);
            }
        }
    }
    printf("Shutting down\n");
    close(epfd);
    close(server_fd);
    return 0;
}