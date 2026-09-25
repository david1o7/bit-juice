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

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
    if (clientfd < 0){
        perror("accept");
        close(sockfd);
        return 1;
    }

    char *client_ip = inet_ntoa(client_addr.sin_addr);
    printf("connection accepted from IP: %s\n", client_ip);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = recv(clientfd, buffer, sizeof(buffer), 0);

    if (bytes_read < 0){
        perror("recv");
        goto cleanup;
    }

    if (bytes_read == 0){
        printf("Client disconnected!\n");
        goto cleanup;
    }

    const char *body = "Hello, world!\n";
    size_t body_len = strlen(body);

    char response[BUFFER_SIZE];
    int response_len = snprintf(response, sizeof(response), 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        body_len, body);
    if (response_len < 0 || response_len >= (int)sizeof(response)){
        fprintf(stderr, "Buffer is too small/encoding error\n");
        goto cleanup;
    }

    ssize_t bytes_sent = 0;
    while (bytes_sent < response_len){
        ssize_t n = send(clientfd, response + bytes_sent, response_len - bytes_sent, 0);

        if (n < 0){
            perror("send");
            break;
            }
            bytes_sent += n;
    } 

    cleanup:
        close(clientfd);
        close(sockfd);

    return 0;
}