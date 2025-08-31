# Makefile for compiling the C programs (Ubuntu)
CC      := gcc
CFLAGS  := -Wall -Wextra -pedantic -O2 -D_POSIX_C_SOURCE=200809L

# Target executables
TARGETS := aesdsocket 

.PHONY: all clean
all: $(TARGETS)
default: $(TARGETS)

# aesdsocket 
aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $< -pthread -lrt

clean:
	rm -f $(TARGETS)

