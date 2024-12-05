#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/stat.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9090
#define BUFFER_SIZE 4096

//loads dependencies
void initialize_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

// GArbage collection
void cleanup_openssl() {
    EVP_cleanup();
}

//context creation
SSL_CTX *create_context() {
    const SSL_METHOD *method = SSLv23_client_method(); // using builtin function provided by openssl
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    return ctx;
}

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char file_name[256];
    char file_path[512];

    // Initialize OpenSSL
    initialize_openssl();
    SSL_CTX *ctx = create_context();
 
    // Create normal tcp socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Setup server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    // Connect to server normal connection using tcp 
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        exit(EXIT_FAILURE);
    }

    // Create SSL object
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock); // socket and context is binded for connection starts of handshake as we can now setup context based on the initial tcp connection

    // after the handshake tcp connection is updated to a tls connection
    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
    } else {
        printf("Connected to server using TLS.\n");
        
        // Request file
        printf("Enter file name to download: ");
        scanf("%s", file_name);
        SSL_write(ssl, file_name, strlen(file_name) + 1);

        // Create Client_Downloads directory if it doesn't exist
        struct stat st = {0};
        if (stat("Client_Downloads", &st) == -1) {
            if (mkdir("Client_Downloads", 0700) != 0) {
                perror("Failed to create Client_Downloads directory");
                exit(EXIT_FAILURE);
            }
        }

        // Prepare the path where the file will be saved
        snprintf(file_path, sizeof(file_path), "Tls_Client_Downloads/%s", file_name);

        // Receive file and save it
        FILE *file = fopen(file_path, "wb");
        if (!file) {
            perror("File creation failed");
        } else {
            int bytes_received;
            while ((bytes_received = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
                fwrite(buffer, 1, bytes_received, file);
            }
            printf("File received and saved to %s.\n", file_path);
            fclose(file);
        }
    }

    // Clean up SSL
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
    SSL_CTX_free(ctx);
    cleanup_openssl();

    return 0;
}
