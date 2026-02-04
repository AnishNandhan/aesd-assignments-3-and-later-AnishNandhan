CC = ${CROSS_COMPILE}gcc
CFLAGS = -g -Wall -Werror

default: all

all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.o

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c

.PHONY: clean
clean:
	rm -f *.o aesdsocket