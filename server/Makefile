# Makefile for aesdsocket
# `make`                                       -> native build (gcc)
# `make CROSS_COMPILE=aarch64-none-linux-gnu-` -> cross build for the target
# `-g -O0` so the binary is debuggable

CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
CFLAGS  := -Wall -Wextra -pedantic -g -O0 -D_POSIX_C_SOURCE=200809L
LDFLAGS := -pthread

TARGETS := aesdsocket

.PHONY: all default clean
all: $(TARGETS)
default: $(TARGETS)

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)
