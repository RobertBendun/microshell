CC = gcc
CFLAGS = -Wall -Wextra -ansi -O3

bin/microshell: src/vector.c src/microshell.c
	mkdir bin 2> /dev/null
	$(CC) $(CFLAGS) -o $@ $^

run: bin/microshell
	./bin/microshell

clean:
	rm bin/*
