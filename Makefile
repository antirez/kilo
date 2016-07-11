all: kilo

kilo: kilo.c
	$(CC) -o kilo kilo.c -Wall -Wextra -Werror -pedantic -std=c99 -Os

clean:
	rm kilo
