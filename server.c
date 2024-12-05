#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 9090
#define BUFFER_SIZE 4096

typedef struct {
    int client_sock;
    char file_name[256];
    long start_offset;
    long end_offset;
} ThreadArgs;

// simple checksum using XOR
unsigned char calculate_checksum(const char *data, size_t length) {
    unsigned char checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

// multi-threaded function with proper lock to synchronize initially to let each thread connect to its parallel server thread
void *send_file_chunk(void *args) {
    ThreadArgs *thread_args = (ThreadArgs *)args;
    int sock = thread_args->client_sock;
    long start = thread_args->start_offset;
    long end = thread_args->end_offset;
    char buffer[BUFFER_SIZE];
    size_t bytes_read;

    // Opening file separately in each thread having separate FD to avoid shared file pointer issues
    FILE *file = fopen(thread_args->file_name, "rb");
    if (!file) {
        perror("File open failed");
        close(sock);
        free(thread_args);
        return NULL;
    }

    fseek(file, start, SEEK_SET);
    
    // Continually read data from file into the buffer for sending
    while (start < end && (bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (start + bytes_read > end) {
            bytes_read = end - start;
        }

        // Calculate checksum
        unsigned char checksum = calculate_checksum(buffer, bytes_read);

        while (1) { // Loop to handle resending in case of failed acknowledgment
            // Send data
            if (send(sock, buffer, bytes_read, 0) == -1) {
                perror("Send failed");
                break;
            }

            // Send checksum
            if (send(sock, &checksum, sizeof(checksum), 0) == -1) {
                perror("Checksum send failed");
                break;
            }

            // Wait for acknowledgment
            char ack_message[32];
            if (recv(sock, ack_message, sizeof(ack_message), 0) > 0 && strcmp(ack_message, "Checksum has been validated") == 0) {
                // If acknowledgment is valid, proceed
                //printf("checksum is valid\n");
                break;
            }

            // If acknowledgment fails, retry sending the data chunk
            printf("Resending data chunk due to acknowledgment failure\n");
        }

        start += bytes_read; // Update start only when acknowledgment is valid
    }

    fclose(file); // Close the file after the thread is done
    close(sock);
    free(thread_args);
    return NULL;
}

int main() {
    int server_fd, new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    pthread_t tid;
    char file_name[256];
    int num_threads;

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // setting up standard TCP args for the socket and address calculation
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind socket to address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 5) == -1) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    // main indefinite loop to continually send files again and again
    while (1) {
        printf("Server is running...\n");

        // Accept initial client connection
        new_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len); // Blocking
        if (new_sock < 0) {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        // Receive file name and thread count from client
        recv(new_sock, file_name, sizeof(file_name), 0);
        recv(new_sock, &num_threads, sizeof(num_threads), 0);

        printf("Client requested file: %s with %d threads\n", file_name, num_threads);

        // Open the file to calculate file size
        FILE *file = fopen(file_name, "rb");
        if (!file) {
            perror("File open failed");
            close(new_sock);
            continue;
        }

        // Calculate chunk size
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        long chunk_size = file_size / num_threads;
        fclose(file); // Close the file after getting its size

        // Send file size to the client
        send(new_sock, &file_size, sizeof(file_size), 0);
        printf("File size sent to client: %ld bytes\n", file_size);

        for (int i = 0; i < num_threads; i++) {
            int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (client_sock < 0) {
                perror("Accept failed");
                break;
            }

            long start_offset = i * chunk_size;
            long end_offset = (i == num_threads - 1) ? file_size : start_offset + chunk_size;

            ThreadArgs *thread_args = malloc(sizeof(ThreadArgs));
            thread_args->client_sock = client_sock;
            strncpy(thread_args->file_name, file_name, sizeof(thread_args->file_name));
            thread_args->start_offset = start_offset;
            thread_args->end_offset = end_offset;

            //handover the chunk handling to a separate thread
            pthread_create(&tid, NULL, send_file_chunk, thread_args);
            pthread_detach(tid);// detach the threads as we dont want to block now and want the threads to work independently
        }
    }

    close(server_fd);
    return 0;
}
