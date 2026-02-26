PWD       := $(shell pwd)

CC ?= ${CROSS_COMPILE}gcc
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -lpthread -lrt
INLCUDES ?= $(PWD)/../include
TARGET ?= aesdsocket

CFLAGS += -I$(INLCUDES)

OBJECTS += aesdsocket.o

default: all

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

$(OBJECTS): aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c

.PHONY: clean
clean:
	rm -f *.o $(TARGET)