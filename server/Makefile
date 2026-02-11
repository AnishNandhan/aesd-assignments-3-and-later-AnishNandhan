CC ?= ${CROSS_COMPILE}gcc
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -lpthread -lrt
INLCUDE ?=
TARGET ?= aesdsocket

OBJECTS += aesdsocket.o

default: all

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

$(OBJECTS): aesdsocket.c
	$(CC) $(CFLAGS) $(INLCUDES) -c aesdsocket.c

.PHONY: clean
clean:
	rm -f *.o $(TARGET)