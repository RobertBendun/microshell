CC = gcc
CFLAGS = -Wall -Wextra -ansi -O3 -Wno-unused-parameter

bin/microshell: src/vector.c src/microshell.c
	mkdir bin -p
	$(CC) $(CFLAGS) -o $@ $^

run: bin/microshell
	./bin/microshell

clean:
	rm bin/*
