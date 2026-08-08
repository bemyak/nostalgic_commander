CC = gcc
CFLAGS = -Wall -Wextra -Werror -I. -I./unity/src -g -DTEST_ENV \
         -fsanitize=address,undefined -fno-omit-frame-pointer
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
