# Adapt CC/CFLAGS to match the rest of user/bin in the OS1/NEXS tree
# (cross-compiler, -ffreestanding flags, link script, etc).
CC ?= cc
CFLAGS ?= -Wall -Wextra -std=c99 -I../../include/api

all: kilo.elf

kilo.elf: kilo.c
	$(CC) $(CFLAGS) -o kilo.elf kilo.c

clean:
	rm -f kilo.elf