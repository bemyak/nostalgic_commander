CC = gcc
CFLAGS = -Wall -Wextra -I. -I./unity/src -g -DTEST_ENV
TARGET = test_watchface
SRCS = test_watchface.c pebble_mock.c unity/src/unity.c

all: $(TARGET)

# test_watchface.c #includes the app sources directly, so they are build
# inputs too.
$(TARGET): $(SRCS) $(wildcard ../src/c/*.c ../src/c/*.h) pebble.h
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

test: $(TARGET)
	./$(TARGET)
