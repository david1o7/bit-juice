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
#include <limits.h>

#define BUFFER_SIZE 4096

static int serve_static_file(int clientfd, const char *doc_root, const char *url_path, int keep_alive);

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
        int large_request = 0;
        size_t total_read = 0;

        while (1){
        if (total_read >= BUFFER_SIZE - 1){
            const char *bad = 
                "HTTP/1.1 413 Content Too Large\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 21\r\n"
                "Connection: close\r\n"
                "\r\n"
                "413 Content Too Large\n";
            send_all(clientfd, bad, strlen(bad));
            large_request = 1;
            break;
        }        

        ssize_t bytes_read = recv(clientfd, buffer + total_read,  BUFFER_SIZE - 1 - total_read, 0);

        if (bytes_read < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // Do nothing  
            } else {
            perror("recv");
            }
            return;
        }
        if (bytes_read == 0) {
            printf("\n[fd=%d] disconnected\n", clientfd);
            return;
        }

        total_read += bytes_read;
        buffer[total_read] = '\0';

        if (strstr(buffer, "\r\n\r\n") != NULL){
            break;
        }

        }

        if (large_request){
            break;
        }
        if (strcasestr(buffer, "Connection: close") != NULL) {
            keep_alive = 0;
        }
        
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

        if (strcmp(method, "GET") == 0){
            if (serve_static_file(clientfd, doc_root, path, keep_alive)){
                 printf("\n[fd=%d] has been server static resource at path: %s \n", clientfd, path);

                 if (!keep_alive) break;

                 continue;
            }
        }

        printf("\n[fd=%d] %s %s → handled\n", clientfd, method, path);
        
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

    printf("\nDone attending to client on socket %d. Time taken: %.4f seconds.\n", clientfd, elapsed_time);

    close(clientfd);

    return NULL;
}

static int ends_with(const char *s, const char *suffix){
    if (!s || !suffix) return 0;
    
    size_t str_len = strlen(s);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len){
        return 0;
    }

    return strcmp(s + (str_len - suffix_len), suffix) == 0 ? 1 : 0;
}

static const char *get_content_type(const char *path){
    if (ends_with(path, ".html") || ends_with(path, ".htm")){
        return "text/html; charset=UTF-8";
    }
    if (ends_with(path, ".css")){
        return "text/css";
    }
    if (ends_with(path, ".js")) {
        return "application/javascript";
    }
    if (ends_with(path, ".png")) {
        return "image/png";
    }
    if (ends_with(path, ".jpg") || ends_with(path, ".jpeg")) {
        return "image/jpeg";
    }
    if (ends_with(path, ".gif")) {
        return "image/gif";
    }
    if (ends_with(path, ".json")) {
        return "application/json";
    }

    return "application/octet-stream";
}

static int build_file_path(const char *doc_root, const char *url_path,
                            char *out, size_t out_size){
        if (url_path[0] != '/'){
            return -1;
        }

        if (strstr(url_path, "..") != NULL) {
            return -1;
        }
        const char *final_url = url_path;
        if (strcmp(url_path, "/") == 0) {
            final_url = "/index.html";
        }

        int bytes_written = snprintf(out, out_size, "%s%s", doc_root, final_url);

        if (bytes_written >= (int)out_size) {
            return -1;
        }

        return 0; 

}

static int serve_static_file(int clientfd, const char *doc_root, const char *url_path, int keep_alive){
    char safe_path[PATH_MAX];

    if ((build_file_path(doc_root, url_path, safe_path, sizeof(safe_path))) != 0){
        return 0;
    }

    FILE *file = fopen(safe_path, "rb");
    if (file == NULL){
            return 0;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    const char *content_type = get_content_type(safe_path);

    const char *connection_header = keep_alive ? "keep-value" : "close";

    char header_buffer[1024];
    int header_len = snprintf(header_buffer, sizeof(header_buffer), 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: %s\r\n"
        "\r\n",
        content_type, file_size, connection_header
        );

    send_all(clientfd, header_buffer, header_len);

    char file_buffer[4096];
    size_t bytes_read;

    while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0){
        send_all(clientfd, file_buffer, bytes_read);
    }

    fclose(file);
    return 1;
}