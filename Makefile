CC = gcc
CFLAGS = -std=c99 -g -Wall -Wpedantic -fsanitize=address,undefined -Wextra

mysh: mysh.c mysh.h
	$(CC) $(CFLAGS) mysh.c -o mysh

clean:
	rm -f mysh
