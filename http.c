#define _GNU_SOURCE
#include "http.h"
#include "server.h"

#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define BUFFER_SIZE 4096

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
            if (errno == EAGAIN || errno == EWOULDBLOCK){
                printf("Client timed out\n");
            } else {
            perror("recv");
            break;
            }
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

void *client_thread(void *arg) {
    int clientfd = *(int *)arg;
    free(arg);

    struct timeval start_time, finish_time;

    gettimeofday(&start_time, NULL);

    handle_client(clientfd);

    gettimeofday(&finish_time, NULL);

    double start_secs = start_time.tv_sec + (start_time.tv_usec / 1000000.0);
    double end_secs = finish_time.tv_sec + (finish_time.tv_usec / 1000000.0);
    double elapsed_time = end_secs - start_secs;

    printf("Done attending to client on socket %d. Time taken: %.4f seconds.\n", clientfd, elapsed_time);

    close(clientfd);

    return NULL;
}