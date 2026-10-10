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
#define MAX_BODY_SIZE (1024 * 1024)

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
                printf("\n[fd=%d] Header read timeout hit. Closing connection.\n", clientfd);

                const char *timeout_res = 
                    "HTTP/1.1 408 Request Timeout\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 20\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "408 Request Timeout\n";
            
                send_all(clientfd, timeout_res, strlen(timeout_res));
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

        char *header_end = strstr(buffer, "\r\n\r\n");
        if (!header_end){
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

        size_t header_len = (header_end + 4) - buffer;
        size_t already_have = total_read - header_len;

        long content_length = 0;
        char *cl = strcasestr(buffer, "Content-Length:");
        if (cl) {
            char *colon = strchr(cl, ':');
            if (colon){
            content_length = strtol(colon + 1, NULL, 10);
            if (content_length < 0) content_length = 0;
            }
        }

        if (content_length > MAX_BODY_SIZE){
            const char *bad = 
                "HTTP/1.1 413 Content Too Large\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 21\r\n"
                "Connection: close\r\n"
                "\r\n"
                "413 Content Too Large\n";
            send_all(clientfd, bad, strlen(bad));
            break; 
        }

        char *content = malloc(content_length + 1);
        if (!content){
            break;
        }

        if (already_have > 0){
            memcpy( content, header_end + 4, already_have);
        }

        size_t total_body_read = already_have;

        while (total_body_read < (size_t) content_length)
        {
            size_t missing_bytes = content_length - total_body_read;
            ssize_t bytes_read = recv(clientfd, content + total_body_read, missing_bytes, 0);

            if (bytes_read < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                printf("\n[fd=%d] Body read timeout hit. Closing connection.\n", clientfd);

                const char *timeout_res = 
                    "HTTP/1.1 408 Request Timeout\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 20\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "408 Request Timeout\n";
            
                send_all(clientfd, timeout_res, strlen(timeout_res));

            } else {
                perror("recv");
            }
            free(content);
            return;
            }
            if (bytes_read == 0) {
                printf("\n[fd=%d] disconnected\n", clientfd);
                free(content);
                return;
            }

            total_body_read += bytes_read;

        }
        content[total_body_read] = '\0';

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
            free(content);
            break;
        }

        if (strcmp(method, "GET") == 0){
            if (serve_static_file(clientfd, doc_root, path, keep_alive)){
                 printf("\n[fd=%d] has been served static resource at path: %s \n", clientfd, path);

                 if (!keep_alive) break;

                 free(content);

                 continue;
            } else {
                printf("\n[fd=%d] Static file not found. Falling back to build_response.\n", clientfd);
            }

            
        } else if (strcmp(method, "POST") == 0) {
            if (strcmp(path, "/echo") == 0){
                char Echo_headers[BUFFER_SIZE];

                int header_length = snprintf(Echo_headers, sizeof(Echo_headers),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %ld\r\n"
                    "Connection: %s\r\n"
                    "\r\n", 
                    content_length, 
                    keep_alive ? "keep-alive" : "close"
                );

                if (send_all(clientfd, Echo_headers, header_length) < 0){
                    free(content);
                    break;
                }

                if (content_length > 0){
                    if (send_all(clientfd, content, content_length) < 0){
                        free(content);
                        break;
                    }
                }

                printf("\n[fd=%d] Echoed %ld body bytes back to client\n", clientfd, content_length);

                free(content);

                if (!keep_alive) break;

                continue;
            } else {
                const char *bad = 
                    "HTTP/1.1 404 NOT FOUND\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 13\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "404 NOT FOUND\n";
                send_all(clientfd, bad, strlen(bad));
                free(content);
                break;
            }
        } else {
                const char *bad = 
                    "HTTP/1.1 405 Method Not Allowed\r\n"
                    "Allow: GET, POST\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 23\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "405 Method Not Allowed\n";
                send_all(clientfd, bad, strlen(bad));
                free(content);
                break;
        }

        printf("\n[fd=%d] %s %s → handled via fallback\n", clientfd, method, path);
        
        build_response(method, path, keep_alive, response, sizeof(response), &response_len);

        if (response_len < 0 || response_len >= (int)sizeof(response)) {
            fprintf(stderr, "Response buffer too small\n");
            free(content);
            break;
        }

       
        if (send_all(clientfd, response, response_len) < 0) {
            printf("\n[fd=%d] %s %s → encountered errot while sending response\n", clientfd, method, path);
            free(content);
            break;
        }

        free(content);

        if (!keep_alive) {
            break;
        }
    }
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
                if (!url_path || url_path[0] != '/') return -1;
                if (strstr(url_path, "..") != NULL) return -1 ;

                const char *final_url = (strcmp(url_path, "/") == 0) ? "/index.html" : url_path;
                
                char candidate[PATH_MAX];
                if (snprintf(candidate, sizeof(candidate), "%s%s", doc_root, final_url) >= (int)sizeof(candidate)){
                    return -1;
                }

                char root_real[PATH_MAX];
                char file_real[PATH_MAX];

                if (!realpath(doc_root, root_real)){
                    return -1;
                }

                if (!realpath(candidate, file_real)){
                    return -1;
                }

                size_t root_len = strlen(root_real);

                if (strncmp(file_real, root_real, root_len) != 0){
                    return -1;
                }

                if (file_real[root_len] !=  '\0' && file_real[root_len] != '/'){
                    return -1;
                }

                if (strlen(file_real) >= out_size) return -1;
                strcpy(out, file_real);
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
    long file_size;

    fseek(file, 0, SEEK_END);
    if ((file_size = ftell(file)) < 0){
        return 0;
    }
    fseek(file, 0, SEEK_SET);

    const char *content_type = get_content_type(safe_path);

    const char *connection_header = keep_alive ? "keep_value" : "close";

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