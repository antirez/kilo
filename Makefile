all: kilo

kilo: kilo.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o kilo kilo.c -Wall -W -pedantic -std=c99

clean:
	rm kilo
