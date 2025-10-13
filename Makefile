all: tino

tino: tino.c
	$(CC) -o tino tino.c -Wall -W -pedantic -std=c99

clean:
	rm tino
