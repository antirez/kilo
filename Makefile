all: kilo

kilo: kilo.c
	mkdir build
	$(CC) -o ./build/kilo kilo.c -Wall -W -pedantic -std=c99

clean:
	rm -rf build
