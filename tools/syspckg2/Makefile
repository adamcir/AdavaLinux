CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
CPPFLAGS ?= -D_GNU_SOURCE
LDFLAGS ?=

TARGET := syspckg2

.PHONY: all clean test

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<

test: $(TARGET)
	sh tests/cli_test.sh

clean:
	rm -f $(TARGET)
