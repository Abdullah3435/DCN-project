# Compiler
CC = gcc
CFLAGS = -Wall

# Source Files
SERVER_SRC = server.c
CLIENT_SRC = client.c

# Executables
SERVER_EXEC = server
CLIENT_EXEC = client

# Default target
.PHONY: all
all: build

# Build target to compile both server and client
.PHONY: build
build: $(SERVER_EXEC) $(CLIENT_EXEC)

# Compile the server
$(SERVER_EXEC): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $<

# Compile the client
$(CLIENT_EXEC): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $<

# Clean up compiled files
.PHONY: clean
clean:
	rm -f $(SERVER_EXEC) $(CLIENT_EXEC)