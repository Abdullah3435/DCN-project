#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define PORT 9090
#define BUFFER_SIZE 4096

void initialize_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl() {
    EVP_cleanup();
}

SSL_CTX *create_context() {
    const SSL_METHOD *method = SSLv23_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    return ctx;
}

void configure_context(SSL_CTX *ctx) {
    if (SSL_CTX_use_certificate_file(ctx, "server.crt", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, "server.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
}

int main() {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    // SSL dependencies initialization private key and certificates are loaded
    initialize_openssl();
    SSL_CTX *ctx = create_context();
    configure_context(ctx);


    // socket initialization
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // binding address to the socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // start listening and accepting connection on PORT 9090
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }

    SSL *ssl = SSL_new(ctx); // creates a new SSL object used during the session keys, certificates all other important info
    SSL_set_fd(ssl, client_fd); // binds teh ssl object to the client socket

    // Handshaking Phase

    /*
    starts the TLS handshake with the client. During this phase:
    The server and client exchange cryptographic keys.
    The server sends its certificate to the client for authentication.
    After a successful handshake, both sides agree on the encryption method and establish a secure session.
    accept is blocking until a safe connection has been established to the server
    */

    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
    } else {
        printf("Client connected using TLS.\n");

        // Receive requested file name
        SSL_read(ssl, buffer, sizeof(buffer));
        printf("Client requested file: %s\n", buffer);

        // Open and send the file
        FILE *file = fopen(buffer, "rb");
        if (!file) {
            perror("File not found");
            SSL_write(ssl, "ERROR: File not found", 21);
        } else {
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                SSL_write(ssl, buffer, bytes_read);
            }
            printf("File transfer completed.\n");
            fclose(file);
        }
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client_fd);
    close(server_fd);
    SSL_CTX_free(ctx);
    cleanup_openssl();
    return 0;
}
