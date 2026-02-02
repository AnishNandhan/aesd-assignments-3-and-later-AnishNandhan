all: writer

writer: writer.o
	$(CROSS_COMPILE)gcc -o writer writer.o

writer.o: writer.c
	$(CROSS_COMPILE)gcc -c writer.c

# Reference: https://stackoverflow.com/questions/2145590/what-is-the-purpose-of-phony-in-a-makefile
.PHONY: clean
clean:
	rm -f *.o writer