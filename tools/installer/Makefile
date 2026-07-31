CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
CPPFLAGS ?=
LDFLAGS ?= -static
LDLIBS ?= -lncurses -ltinfo

TARGET := installer
OBJS := main.o ui.o sys.o install.o helpers.o
TEST_HELPERS := tests/test_helpers

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(TEST_HELPERS): tests/test_helpers.o helpers.o
	$(CC) $(LDFLAGS) -o $@ tests/test_helpers.o helpers.o

test: $(TEST_HELPERS)
	./$(TEST_HELPERS)

clean:
	rm -f $(TARGET) $(OBJS) tests/test_helpers.o $(TEST_HELPERS)

%.o: %.c installer.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c installer.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. -c -o $@ $<
