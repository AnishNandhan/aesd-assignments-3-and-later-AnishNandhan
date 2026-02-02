CC = ${CROSS-COMPILE}gcc
CFLAGS = -Wall -g

all: writer

writer: writer.o
	$(CC) $(CFALGS) -o writer writer.o

writer.o: writer.c
	$(CC) $(CFLAGS) -c writer.c

# Reference: https://stackoverflow.com/questions/2145590/what-is-the-purpose-of-phony-in-a-makefile
.PHONY: clean
clean:
	rm -f *.o writer