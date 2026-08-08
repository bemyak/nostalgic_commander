# Top-level developer entry points. The Pebble SDK build itself is `pebble
# build` (see README); this Makefile wraps formatting and the host test suite.

FORMAT_SRCS = $(wildcard src/c/*.c src/c/*.h) \
              src/pkjs/index.js src/pkjs/weather.js \
              test/pkjs/weather.test.js \
              test/test_watchface.c test/pebble_mock.c test/pebble.h

.PHONY: format format-check test test-js build

format:
	clang-format -i $(FORMAT_SRCS)

format-check:
	clang-format --dry-run -Werror $(FORMAT_SRCS)

test: format-check test-js
	$(MAKE) -C test test

# Phone-side contract tests; plain node, no framework.
test-js:
	node --test test/pkjs/*.test.js

# Requires the pebble-env virtualenv to be active.
build: format-check
	pebble build
