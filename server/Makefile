PWD := $(shell pwd)

CC ?= ${CROSS_COMPILE}gcc
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -lpthread -lrt
INCLUDES ?= -I$(PWD)/../include
TARGET ?= aesdsocket

OBJECTS += aesdsocket.o

default: all

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

$(OBJECTS): aesdsocket.c
	$(CC) $(CFLAGS) $(INCLUDES) -c aesdsocket.c

.PHONY: clean
clean:
	rm -f *.o $(TARGET)