CC = gcc
CFLAGS = -Wall -Wextra -g

# Common source files shared between server and client
COMMON_SRC = dict.c globalVariables.c chatroom.c

# Server-specific source files
SERVER_SRC = server.c
SERVER_OBJ = $(SERVER_SRC:.c=.o) $(COMMON_SRC:.c=.o)
SERVER = server

# Client-specific source files
CLIENT_SRC = client.c
CLIENT_OBJ = $(CLIENT_SRC:.c=.o) $(COMMON_SRC:.c=.o)
CLIENT = client

# Default target: build both server and client
all: $(SERVER) $(CLIENT)

# Server executable
$(SERVER): $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# Client executable
$(CLIENT): $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# Pattern rule for object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean object files
clean:
	rm -f *.o

# Clean executables, object files, and data files
fclean: clean
	rm -f $(SERVER) $(CLIENT) users.txt rooms.txt

# Rebuild everything
re: fclean all

.PHONY: all clean fclean re