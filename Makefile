CC = gcc
CFLAGS = -Wall -Wextra -ansi -O3 -Wno-unused-parameter

bin/microshell: src/vector.c src/microshell.c
	mkdir bin -p
	$(CC) $(CFLAGS) -o $@ $^

run: bin/microshell
	PS1="\e[32;1m\u@\h\e[0m[\e[34m\w\e[0m] \P " ./bin/microshell

clean:
	rm bin/*
