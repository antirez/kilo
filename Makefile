CFLAGS = -g -Wall -W -pedantic -std=c99

all: kilo.o main.o
	$(CC) $(CFLAGS) kilo.o main.o -o kilo

kilo.o: kilo.c
	$(CC) -c $(CFLAGS) kilo.c 

main.o: main.c
	$(CC) -c $(CFLAGS) main.c

clean:
	rm -f *.o kilo
