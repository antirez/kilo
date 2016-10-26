all: kilo

OBJS=kilo.o function.o process_keypress.o

INCLUDE=function.h kilo.h
CFLAGS=-Wextra -pedantic -std=c11

kilo: $(OBJS)
	$(CC) -o kilo $(OBJS)

clean:
	rm kilo $(OBJS)
