#include "server.h"
#include "http.h"

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

#define THREAD_POOL_SIZE 8
#define QUEUE_SIZE 128
#define QUEUE_MASK (QUEUE_SIZE - 1) 
#define PORT 8080

int client_queue[QUEUE_SIZE];
int queue_count = 0;
int queue_head = 0;
int queue_tail = 0;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond  = PTHREAD_COND_INITIALIZER;

volatile int localserverfd = -1;
volatile sig_atomic_t running = 1;
pthread_t workers[THREAD_POOL_SIZE];

void enqueue_client(int clientfd){
    pthread_mutex_lock(&queue_mutex);

    if (queue_count == QUEUE_SIZE){
        const char *bad = 
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 25\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Please try again shortly.\n";

        send_all(clientfd, bad, strlen(bad));
        close(clientfd);
    } else {
        client_queue[queue_tail] = clientfd;
        queue_tail = (queue_tail + 1) & QUEUE_MASK;
        queue_count++;
        pthread_cond_signal(&queue_cond);
    }

    pthread_mutex_unlock(&queue_mutex);
}

void *worker_thread(void *arg){
    (void) arg;

    while(1) {
        pthread_mutex_lock(&queue_mutex);

        while(queue_count == 0 && running){
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }

        if (!running && queue_count == 0) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }

        int clientfd = client_queue[queue_head];
        queue_head = (queue_head + 1) & QUEUE_MASK;
        queue_count--;

        pthread_mutex_unlock(&queue_mutex);

        struct timeval start_time, finish_time;

        gettimeofday(&start_time, NULL);

        handle_client(clientfd);

        gettimeofday(&finish_time, NULL);

        double start_secs = start_time.tv_sec + (start_time.tv_usec / 1000000.0);
        double end_secs = finish_time.tv_sec + (finish_time.tv_usec / 1000000.0);
        double elapsed_time = end_secs - start_secs;

        printf("\n[fd=%d] timeout: %.4f s.\n", clientfd, elapsed_time);

        close(clientfd);
    }

    return NULL;
}

void handle_signal(int sig){
    (void) sig;
    running = 0;

    if (localserverfd != -1){
        shutdown(localserverfd, SHUT_RDWR);
    }
}

int main(void) {

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int server_fd = create_server_socket(PORT);
    if (server_fd < 0) {
        return 1;
    }

    localserverfd = server_fd;

    for (int i = 0; i < THREAD_POOL_SIZE; i++){
        if (pthread_create(&workers[i], NULL, worker_thread, NULL) != 0){
            perror("pthread_create_worker");
            close(server_fd);
            return 1;
        }
    }

    printf("Server listening on port %d...\n", PORT);

    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int clientfd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (clientfd < 0) {
            if (!running || errno == EINTR || errno == EBADF){
            break;
            }
            perror("accept");
            continue;
        }

        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;

        if (setsockopt(clientfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("socksockopt SO_RCVTIMEO");
            close(clientfd);
            continue;
        }

        printf("\n[fd=%d] IP → %s\n", clientfd, inet_ntoa(client_addr.sin_addr));

        enqueue_client(clientfd);

    }
    printf("Shutting down server!.. \n\n");
    close(server_fd);

    pthread_mutex_lock(&queue_mutex);
    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);

    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_join(workers[i], NULL);
    }

    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&queue_cond);

    return 0;
}