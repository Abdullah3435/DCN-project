#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h> // For mkdir

#define SERVER_IP "127.0.0.1"
#define PORT 9090
#define BUFFER_SIZE 4096

pthread_mutex_t connection_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t connection_cond = PTHREAD_COND_INITIALIZER;
int ready_threads = 0; // Tracks the number of threads ready to proceed used in thread sync

typedef struct {
    int thread_id;
    long start_offset;
    long end_offset;
    const char *temp_file_name;
} ThreadArgs;

// XOR checksum
unsigned char calculate_checksum(const char *data, size_t length) {
    unsigned char checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum ^= data[i]; // Xorring
    }
    return checksum;
}

// thread func to receive each chunk separately
void *receive_file_chunk(void *args) {
    ThreadArgs *thread_args = (ThreadArgs *)args;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    FILE *temp_file;
    int sock;
    struct sockaddr_in server_addr;

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connection failed");
        close(sock);
        pthread_exit(NULL);
    }

    //     // Signal the main thread after connection
    pthread_mutex_lock(&connection_mutex);
    ready_threads++;
    pthread_cond_signal(&connection_cond);
    pthread_mutex_unlock(&connection_mutex);

    // Open temporary file for this thread
    temp_file = fopen(thread_args->temp_file_name, "wb");
    if (!temp_file) {
        perror("Temporary file creation failed");
        close(sock);
        pthread_exit(NULL);
    }

    // Receive file chunk
    while ((bytes_received = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        unsigned char received_checksum;

        // Receive checksum
        if (recv(sock, &received_checksum, sizeof(received_checksum), 0) <= 0) {
            perror("Checksum receive failed");
            break;
        }

        // Calculate and validate checksum
        unsigned char calculated_checksum = calculate_checksum(buffer, bytes_received);
        if (calculated_checksum != received_checksum) {
            printf("Checksum mismatch for thread %d\n", thread_args->thread_id);
            
            // Send negative acknowledgment
            send(sock, "Checksum mismatch", 18, 0);
            continue; // Skip writing the buffer to file and simply continue loop and receive again
        }
        //printf("valid check sum is sent");
        // Send acknowledgment
        send(sock, "Checksum has been validated", 27, 0);

        fwrite(buffer, 1, bytes_received, temp_file);
    }
    fclose(temp_file);
    close(sock);

    if (bytes_received == -1) {
        perror("Receive failed");
    } else {
        printf("Thread %d: Received chunk successfully\n", thread_args->thread_id);
    }

    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    int num_threads;
    char file_name[256];
    char file_path[512]; // Path to save the downloaded file
    FILE *output_file;
    pthread_t *threads;
    ThreadArgs *thread_args;
    long file_size, chunk_size;

    // Handle command-line arguments
    if (argc >= 3) {
        strncpy(file_name, argv[1], sizeof(file_name) - 1);
        file_name[sizeof(file_name) - 1] = '\0'; // Ensure null-termination
        num_threads = atoi(argv[2]);
    } else {
        // Prompt for input if not provided via command line
        printf("Enter file name to download: ");
        scanf("%s", file_name);

        printf("Enter number of threads to use: ");
        scanf("%d", &num_threads);
    }

    // Create the Clients_Downloads directory if it doesn't exist
    struct stat st = {0};
    if (stat("Clients_Downloads", &st) == -1) {
        mkdir("Clients_Downloads", 0755);
    }

    // Create initial socket to request file
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = {0};

    if (sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connection to server failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Send file name and number of threads
    send(sock, file_name, sizeof(file_name), 0);
    send(sock, &num_threads, sizeof(num_threads), 0);

    // Receive file size
    recv(sock, &file_size, sizeof(file_size), 0);
    printf("File size: %ld bytes\n", file_size);

    close(sock);

    // Calculate chunk size
    chunk_size = file_size / num_threads;

    // Allocate memory for threads and thread arguments
    threads = malloc(num_threads * sizeof(pthread_t));
    thread_args = malloc(num_threads * sizeof(ThreadArgs));

    for (int i = 0; i < num_threads; i++) {
        char temp_file_name[64];
        snprintf(temp_file_name, sizeof(temp_file_name), "chunk_%d.tmp", i);

        thread_args[i].thread_id = i;
        thread_args[i].start_offset = i * chunk_size;
        thread_args[i].end_offset = (i == num_threads - 1) ? file_size : thread_args[i].start_offset + chunk_size;
        thread_args[i].temp_file_name = strdup(temp_file_name);

        pthread_create(&threads[i], NULL, receive_file_chunk, &thread_args[i]);
        // Wait for the thread to signal readiness
        pthread_mutex_lock(&connection_mutex); // lock used for synchronization
        while (ready_threads <= i) {
            pthread_cond_wait(&connection_cond, &connection_mutex);
        }
        pthread_mutex_unlock(&connection_mutex);
    }

    // Wait for threads to complete
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // Combine temporary files into final output
    snprintf(file_path, sizeof(file_path), "Clients_Downloads/%s", file_name);
    output_file = fopen(file_path, "wb");
    if (!output_file) {
        perror("Failed to create output file");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_threads; i++) {
        FILE *temp_file = fopen(thread_args[i].temp_file_name, "rb");
        if (!temp_file) {
            perror("Temporary file open failed");
            continue;
        }

        char buffer[BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, temp_file)) > 0) {
            fwrite(buffer, 1, bytes_read, output_file);
        }

        fclose(temp_file);
        remove(thread_args[i].temp_file_name); // Delete temporary file
        free((void *)thread_args[i].temp_file_name);
    }

    fclose(output_file);

    printf("File downloaded and assembled successfully as '%s'\n", file_path);

    free(threads);
    free(thread_args);
    return 0;
}
