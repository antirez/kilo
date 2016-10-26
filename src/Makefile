all: kilo

CFLAGS=-Wextra -pedantic -std=c11

OBJS=kilo.o function.o process_keypress.o init.o syntax_highlighter.o colon.o trie.o
INCLUDE=function.h kilo.h process_keypress.h init.h syntax_highlighter.h colon.h trie.h

kilo: $(OBJS)
	$(CC) -o kilo $(OBJS)

clean:
	rm kilo $(OBJS)
