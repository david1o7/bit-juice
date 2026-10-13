#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1"

int send_test_request(const char *request_payload, char *response_buffer, size_t buffer_size){
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0){
        perror("Socket");
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("Connect");
        close(sock);
        return -1;
    }

    send(sock, request_payload, strlen(request_payload), 0);

    shutdown(sock, SHUT_WR); 

    memset(response_buffer, 0, buffer_size);
    
    size_t total_received = 0;
    while (total_received < buffer_size - 1) {
        ssize_t bytes = recv(sock, response_buffer + total_received, buffer_size - 1 - total_received, 0);
        if (bytes <= 0) {
            break;
        }
        total_received += bytes;
    }

    close(sock);
    return 0;
}

int main() {
    char response[1024];
    int passed_tests = 0;
    int total_tests = 0;

    printf("--- RUNNING HTTP SERVER SECURITY TESTS ---\n\n");

    total_tests++;
    const char *req1 = "GET /../about.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    printf("Test 1: Sending path traversal request...\n");
    
    if (send_test_request(req1, response, sizeof(response)) == 0) {
        
        if (strstr(response, "HTTP/1.1 400") != NULL) {
            passed_tests++;
        } else {
            printf("[ FAIL ] Server did not block traversal! Response: %.50s...\n", response);
        }
    }
    printf("\n");

    total_tests++;
    const char *req9 = "GET /../about.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    printf("Test 9: Sending path traversal request...\n");
    
    if (send_test_request(req9, response, sizeof(response)) == 0) {
        
        if (strcasestr(response, "connection: close") != NULL) {
            passed_tests++;
        } else {
            printf("[ FAIL ] Server does not close connection immediately on a bad client traversal Response: %.50s...\n", response);
        }
    }
    printf("\n");

    total_tests++;
    const char *req2 = "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    printf("Test 2: Sending valid index file request...\n");
    
    if (send_test_request(req2, response, sizeof(response)) == 0) {
       
        if (strstr(response, "HTTP/1.1 200") != NULL) {
            passed_tests++;
        } else {
            printf("[ FAIL ] Server choked on a normal path. Response: %.50s...\n", response);
        }
    }
    printf("\n");

    printf("--- RUNNING HTTP SERVER PATH TRAVERSAL TESTS ---\n\n");

    total_tests++;
    const char *req3 = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    printf("Test 3: Sending / to test if it directs it to the /index file...\n");
    
    if (send_test_request(req3, response, sizeof(response)) == 0) {
       
        if (strstr(response, "HTTP/1.1 200") != NULL) {
            passed_tests++;
        } else {
            printf("[ FAIL ] Server didnt resolve to the /index.html file. Response: %.50s...\n", response);
        }
    }
    printf("\n");

    total_tests++;
    const char *req4 = "GET /hah HTTP/1.1\r\nHost: localhost\r\n\r\n";
    printf("Test 4: Sending /hah to test if its sends 404...\n");
    
    if (send_test_request(req4, response, sizeof(response)) == 0) {
       
        if (strstr(response, "HTTP/1.1 404") != NULL) {
            passed_tests++;
        } else {
            printf("[ FAIL ] Server didnt notice a wrong path. Response: %.50s...\n", response);
        }
    }
    printf("\n");

    total_tests++;
    const char *req5 = "POST /hah HTTP/1.1\r\nHost: localhost\r\n\r\n";
    printf("Test 5: Sending POST /hah to test if its sends 404...\n");
    
    if (send_test_request(req5, response, sizeof(response)) == 0) {
       
        if (strstr(response, "HTTP/1.1 404") != NULL) {
            passed_tests++;
        } else {
            printf("[ FAIL ] Server didnt observe that the Post endpoint isnt supported. Response: %.50s...\n", response);
        }
    }
    printf("\n");

    total_tests++;
    const char *req6 = "PATCH / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    printf("Test 6: Sending / to test if its sends 405...\n");
    
    if (send_test_request(req6, response, sizeof(response)) == 0) {
       
        if (strstr(response, "HTTP/1.1 405") != NULL) {
            passed_tests++;
        } else {
            printf("[ FAIL ] Server didnt notice that its not a method it supports: %.50s...\n", response);
        }
    }
    printf("\n");

    total_tests++;
    const char *req7 = 
    "POST /echo HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "Content-Length: 26\r\n"
    "\r\n"
    "Howdy, this is a test body";
    printf("Test 7: Sending POST /echo to test if its echos my response body...\n");
    
    if (send_test_request(req7, response, sizeof(response)) == 0) {
       
        if (strstr(response, "HTTP/1.1 200") != NULL && strstr(response, "Howdy,") != NULL){
            passed_tests++;
        } else {
            printf("[ FAIL ] Server didnt echo back the response body: %.50s...\n", response);
        }
    }
    printf("\n");

    total_tests++;
    const char *req8 = "GET /about.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    printf("Test 8: Sending valid index file request...\n");
    
    if (send_test_request(req8, response, sizeof(response)) == 0) {
       
        if (strstr(response, "HTTP/1.1 200") != NULL) {
            passed_tests++;
        } else {
            printf("[ FAIL ] Server choked on a normal path. Response: %.50s...\n", response);
        }
    }
    printf("\n");

    total_tests++;
    printf("Test 10: Sending a 5000+ byte path to test 413 Content Too Large...\n");
    char massive_req[6000];

    strcpy(massive_req, "GET /");

    size_t prefix_len = strlen(massive_req);
    memset(massive_req + prefix_len, 'A', 5000); 
    massive_req[prefix_len + 5000] = '\0'; 

    strcat(massive_req, " HTTP/1.1\r\nHost: localhost\r\n\r\n");

    if (send_test_request(massive_req, response, sizeof(response)) == 0) {
        if (strstr(response, "HTTP/1.1 413") != NULL || strstr(response, "413") != NULL) {
            printf("[ PASS ] Server correctly caught the overflow and returned 413 Content Too Large.\n");
            passed_tests++;
        } else {
            printf("[ FAIL ] Server did not return 413. Response: %.50s...\n", response);
        }
    }
    printf("\n");

    printf("--- TEST RESULTS ---\n");
    printf("Passed: %d / %d tests.\n", passed_tests, total_tests);

    return (passed_tests == total_tests) ? 0 : 1;
}