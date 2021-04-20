all: kilo

kilo: kilov.c
	$(CC) -o kilov kilov.c -Wall -W -pedantic -std=c99

clean:
	rm kilov
