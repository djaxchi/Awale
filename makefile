# Compiler
CC = gcc
CFLAGS = # Enable warnings and debugging information

# Server files
SERVER_SRC = Server2/server2.c Server2/awale.c
SERVER_OBJ = $(SERVER_SRC:.c=.o)
SERVER_BIN = server

# Client files
CLIENT_SRC = Client/client.c
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)
CLIENT_BIN = client1 client2 client3 client4

# All targets
all: $(SERVER_BIN) $(CLIENT_BIN)

# Server target
$(SERVER_BIN): $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Client targets (reuse the same client object files for all clients)
client1: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

client2: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

client3: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

client4: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Object file generation
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(SERVER_OBJ) $(CLIENT_OBJ)

# Clean everything including binaries
dist-clean: clean
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
