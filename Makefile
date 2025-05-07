EXEC = server
CC = gcc

# Compilation options
CFLAGS = -Wall -Wextra -g

SRC = dict.c globalVariables.c server.c

OBJ = $(SRC:.c=.o)

all: $(EXEC)

$(EXEC): $(OBJ) 
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c 
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o

fclean: clean
	rm -f $(EXEC)

re: fclean all
