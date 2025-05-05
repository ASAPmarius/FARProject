EXEC = server
CC = gcc

#Option de compilation
CFLAGS = -Wall -Wextra -g

SRC = dict.c dict.h globalVariables.h server.c #client.c 

OBJ = $(SRC:.c=.o)

all: $(EXEC)

$(EXEC): $(OBJ) 
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c 
	$(CC) $(CFLAGS) -c $< -o $@

# suppression des ficheir objets .o
clean:
	rm -f *.o

# suppression de l'executable
fclean: clean
	rm -f $(EXEC)

# nettoie tout puis recompile
re: fclean all