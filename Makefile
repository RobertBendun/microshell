CC = gcc
CFLAGS = -Wall -Wextra -ansi -O3 -Wno-unused-parameter -Wno-overlength-strings -pedantic -Wshadow

microshell: microshell.c
	mkdir bin -p
	$(CC) $(CFLAGS) -o $@ $^ -lrt

run: microshell
	./microshell

clean:
	rm microshell

build: microshell
