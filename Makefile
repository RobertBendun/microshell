CC = gcc
CFLAGS = -Wall -Wextra -ansi -O3 -Wno-unused-parameter -pedantic -Wshadow

bin/microshell: src/vector.c src/microshell.c src/StringView.c
	mkdir bin -p
	$(CC) $(CFLAGS) -o $@ $^

run: bin/microshell
	./bin/microshell

clean:
	rm bin/*
