#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>

#define PORT 8080
#define BUFFER_SIZE 4096

int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 10) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int send_all(int fd, const char *data, size_t len) {
    size_t total_sent = 0;

    while (total_sent < len) {
        ssize_t n = send(fd, data + total_sent, len - total_sent, 0);
        if (n < 0) {
            perror("send");
            return -1;
        }
        total_sent += n;
    }
    return 0;
}

int parse_request(const char *buffer, char *method, char *path, char *version) {
    int matched = sscanf(buffer, "%15s %255s %15s", method, path, version);
    return matched == 3;
}

void build_response(const char *method, const char *path,
                    int keep_alive,
                    char *response, size_t response_size,
                    int *response_len) {

    const char *status_line;
    const char *body;

    if (strcmp(method, "GET") != 0) {
        status_line = "HTTP/1.1 405 Method Not Allowed";
        body = "Method Not Allowed.\n";
    } else if (strcmp(path, "/") == 0) {
        status_line = "HTTP/1.1 200 OK";
        body = "Howdy partner, welcome to my http server.\n";
    } else if (strcmp(path, "/about") == 0) {
        status_line = "HTTP/1.1 200 OK";
        body = "Howdy partner, welcome to my about page.\nP.S i am a programmer.\n";
    } else {
        status_line = "HTTP/1.1 404 Not Found";
        body = "404 Not Found\n";
    }

    size_t body_len = strlen(body);

    *response_len = snprintf(response, response_size,
        "%s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n"
        "%s",
        status_line,
        body_len,
        keep_alive ? "keep-alive" : "close",
        body);
}

void handle_client(int clientfd) {
    char buffer[BUFFER_SIZE];
    char method[16];
    char path[256];
    char version[16];
    char response[BUFFER_SIZE];
    int response_len;

    while (1) {
        int keep_alive = 1;

        ssize_t bytes_read = recv(clientfd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read < 0) {
            perror("recv");
            break;
        }
        if (bytes_read == 0) {
            printf("Client disconnected\n");
            break;
        }

        buffer[bytes_read] = '\0';

        
        if (strcasestr(buffer, "Connection: close") != NULL) {
            keep_alive = 0;
        }

        printf("--- Raw request ---\n%s\n---\n", buffer);

        
        if (!parse_request(buffer, method, path, version)) {
            const char *bad = 
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 15\r\n"
                "Connection: close\r\n"
                "\r\n"
                "400 Bad Request\n";
            send_all(clientfd, bad, strlen(bad));
            break;
        }

        printf("Method: %s | Path: %s | Version: %s\n", method, path, version);

       
        build_response(method, path, keep_alive, response, sizeof(response), &response_len);

        if (response_len < 0 || response_len >= (int)sizeof(response)) {
            fprintf(stderr, "Response buffer too small\n");
            break;
        }

       
        if (send_all(clientfd, response, response_len) < 0) {
            break;
        }

        if (!keep_alive) {
            break;
        }
    }
}

void *client_thread(void *args){
    int clientfd = *(int *)args;
    free(args);

    handle_client(clientfd);
    close(clientfd);

    return NULL;
}


int main(void) {
    int server_fd = create_server_socket(PORT);
    if (server_fd < 0) {
        return 1;
    }

    struct sigaction kill_zombie_cp;
    memset(&kill_zombie_cp, 0, sizeof(kill_zombie_cp));
    kill_zombie_cp.sa_handler = sigchild_handler;
    sigemptyset(&kill_zombie_cp.sa_mask);
    kill_zombie_cp.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &kill_zombie_cp, NULL) == -1){
        perror("sigaction");
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