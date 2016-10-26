all:
	$(MAKE) -C src && cp src/kilo kilo

clean:
	$(MAKE) -C src clean && rm kilo
