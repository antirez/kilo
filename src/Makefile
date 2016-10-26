all: kilo

SRC=kilo.c function.c
INCLUDE=function.h kilo.h
CFLAGS=-Wextra -pedantic -std=c11
OBJS=kilo.o function.o

kilo: $(OBJS)
	$(CC) -o kilo $(OBJS)

clean:
	rm kilo $(OBJS)
