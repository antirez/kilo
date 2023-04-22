CC = gcc -g -Wall -W -ansi -pedantic -std=c99 -pthread -o
C+ = g++ -g -Wall -pthread -o

all: clean kilo server

kilo: kilo.c
	$(CC) kilo kilo.c

server: server.cpp
	$(C+) server server.cpp

clean:
	rm -f kilo kilo.exe server server.exe
