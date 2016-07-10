all: kilo

kilo: kilo.c
	$(CC) -o kilo kilo.c -Wall -Wextra -pedantic -std=c99

clean:
	rm kilo
