# Top-level developer entry points. The Pebble SDK build itself is `pebble
# build` (see README); this Makefile wraps formatting and the host test suite.

FORMAT_SRCS = $(wildcard src/c/*.c src/c/*.h) \
              $(wildcard src/pkjs/*.js) \
              $(wildcard test/pkjs/*.js) \
              test/test_watchface.c test/pebble_mock.c test/pebble.h \
              test/vignette_ramp.c

.PHONY: format format-check test test-js build screenshots vignette-ramp pkjs-font

format:
	clang-format -i $(FORMAT_SRCS)

format-check:
	clang-format --dry-run -Werror $(FORMAT_SRCS)

test: format-check test-js
	$(MAKE) -C test test

# Prints what the vignette actually renders on each theme — the instrument for
# tuning crt.h's bands and the falloff LUTs, which interact through a 4-level
# panel in ways the source does not show. See test/vignette_ramp.c.
vignette-ramp:
	$(MAKE) -C test ramp

# Phone-side contract tests; plain node, no framework.
test-js:
	node --test test/pkjs/*.test.js

# Requires the pebble tool on PATH (SDK auto-resolves under ~/.pebble-sdk).
build: format-check
	pebble build

# Store captures: five scripted emulator screenshots under screenshots/.
screenshots:
	tools/update_screenshots.sh

# Regenerates the base64 VGA font the config page skin embeds.
pkjs-font:
	node -e "const fs=require('fs');fs.writeFileSync('src/pkjs/vga_font.json',JSON.stringify({ttf:fs.readFileSync('resources/fonts/Px437_IBM_VGA_8x16.ttf').toString('base64')})+'\\n')"
