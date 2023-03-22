CC = gcc -g -Wall -W -ansi -pedantic -std=c99 -o

all: kilo

kilo: kilo.c
	$(CC) kilo kilo.c

clean:
	rm -f kilo
