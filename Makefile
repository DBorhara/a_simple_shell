CC = gcc
CFLAGS = -std=c99 -g -Wall -fsanitize=address -fsanitize=undefined -Wextra

mysh: mysh.c
	$(CC) $(CFLAGS) mysh.c -o mysh