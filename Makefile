CC = gcc
CFLAGS = -Wall -Wextra -ansi -O3 -Wno-unused-parameter -pedantic -Wshadow

FILES = vector StringView allocators
OBJECTS = $(addprefix bin/,$(addsuffix .o,$(FILES)))

bin/%.o: src/%.c
	mkdir bin -p
	$(CC) -c $(CFLAGS) -o $@ $^

bin/microshell: $(OBJECTS) src/microshell.c
	mkdir bin -p
	$(CC) $(CFLAGS) -o $@ $^ -lrt

run: bin/microshell
	./bin/microshell

clean:
	rm bin/*

build: bin/microshell
