#define _GNU_SOURCE
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <stdlib.h>

#define PORT 8080
#define BUFFER_SIZE 4096

int main(void) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0){
        fprintf(stderr, "Socket wasn't created!\n");
        return 1;
    }

    struct sockaddr_in socket_addr;
    memset(&socket_addr, 0, sizeof(socket_addr));
    socket_addr.sin_family = AF_INET;
    socket_addr.sin_port = htons(PORT);
    socket_addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("setsocketopt(SO_REUSEADDR) failed");
        close(sockfd);
        return 1;
    }

    if (bind(sockfd, (struct sockaddr *)&socket_addr, sizeof(socket_addr)) < 0){
        perror("bind");
        close(sockfd);
        return 1;
    }

    if (listen(sockfd, 1) < 0){
        perror("listen");
        close(sockfd);
        return 1;
    }

    printf("Server listening on port %d...\n", PORT);

    while(1){

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
    if (clientfd < 0){
        perror("accept");
        continue;
    }

    char *client_ip = inet_ntoa(client_addr.sin_addr);
    printf("connection accepted from IP: %s\n", client_ip);

    while(1) {

    int keep_alive = 1;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = recv(clientfd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read < 0){
        perror("recv");
        break;
    }

    if (bytes_read == 0){
        printf("Client disconnected!\n");
        break;
    }

    buffer[bytes_read]= '\0';

    if (strcasestr(buffer, "Connection: close") != NULL){
        keep_alive = 0;
    }

    printf("-Raw client request-\n%s\n---\n", buffer);

    char method[16] = {0};
    char path[256] = {0};
    char version[16] = {0};

    int matched = sscanf(buffer, "%15s %255s %15s", method, path, version);

    if (matched != 3){
        const char *fail = 
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 15\r\n"
        "Connection: close\r\n"
        "\r\n"
        "400 Bad Request\n";

        send(clientfd, fail, strlen(fail), 0);

        break;
    }

    printf("Method: %s\nPath: %s\nVersion: %s\n", method, path, version);

    const char *status_line;
    const char *body;

    if (strcmp(method, "GET") != 0){
        status_line = "HTTP/1.1 405 Method Not Allowed";
        body = "Method Not Allowed.\n";
    } else if(strcmp(path, "/") == 0) {
        status_line = "HTTP/1.1 200 OK";
        body = "Howdy partner, welcome to my http server.\n";
    } else if(strcmp(path, "/about") == 0){
        status_line = "HTTP/1.1 200 OK";
        body = "Howdy partner, welcome to my about page.\nP.S i am a programmer.\n";
    } else {
        status_line = "HTTP/1.1 404 Not Found";
        body = "404 Not Found\n.";
    }

    size_t body_len = strlen(body);

    char response[BUFFER_SIZE];
    int response_len = snprintf(response, sizeof(response), 
        "%s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n"
        "%s",
        status_line, body_len,(keep_alive)? "keep-alive" : "close", body);

    if (response_len < 0 || response_len >= (int)sizeof(response)){
        fprintf(stderr, "Buffer is too small/encoding error\n");
        break;
    }

    int send_failed = 0;
    ssize_t bytes_sent = 0;
    while (bytes_sent < response_len){
        ssize_t n = send(clientfd, response + bytes_sent, response_len - bytes_sent, 0);

        if (n < 0){
            perror("send");
            send_failed = 1;
            break;
            }
            bytes_sent += n;
    }

    if(send_failed){
        break;
    }

    if (!keep_alive){
        break;
    }

    }

    close(clientfd);
    printf("Client %s disconnected waiting for next client...\n", client_ip);

}

    close(sockfd);
    return 0;
}