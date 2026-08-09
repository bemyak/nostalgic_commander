# Top-level developer entry points. The Pebble SDK build itself is `pebble
# build` (see README); this Makefile wraps formatting and the host test suite.

FORMAT_SRCS = $(wildcard src/c/*.c src/c/*.h) \
              $(wildcard src/pkjs/*.js) \
              $(wildcard test/pkjs/*.js) \
              test/test_watchface.c test/pebble_mock.c test/pebble.h

.PHONY: format format-check test test-js build visual-check visual-baseline

format:
	clang-format -i $(FORMAT_SRCS)

format-check:
	clang-format --dry-run -Werror $(FORMAT_SRCS)

test: format-check test-js
	$(MAKE) -C test test

# Phone-side contract tests; plain node, no framework.
test-js:
	node --test test/pkjs/*.test.js

# Requires the pebble tool on PATH (SDK auto-resolves under ~/.pebble-sdk).
build: format-check
	pebble build

# --- Visual gate ------------------------------------------------------------
# Pixel-gates the rendered face against a committed capture: build, launch on
# the emery emulator, screenshot immediately (before any weather fetch can
# land), mask the moving regions on both images, require the rest identical.
VISUAL_DIR = test/visual
VISUAL_BASELINE = $(VISUAL_DIR)/baseline.png
VISUAL_SHOT = /tmp/nc_visual_current.png
VISUAL_BASE_MASKED = /tmp/nc_visual_baseline_masked.png
VISUAL_DIFF = /tmp/nc_visual_diff.png

# Mask rects ("x0,y0 x1,y1", inclusive) — exactly the regions that move
# between runs, derived by diffing consecutive captures; everything else is
# gated pixel-for-pixel.
#   CLOCK:   the clock digits (layout.h CLOCK_RECT) — minute/hour roll
#   DATE:    the centre-slot date text — wall-date roll (daily)
#   WEATHER: the top-left slot — the live fetch may land right after launch
VISUAL_MASK_CLOCK = 12,49 187,112
VISUAL_MASK_DATE = 10,144 189,175
VISUAL_MASK_WEATHER = 10,10 98,41
VISUAL_MASK_ARGS = -fill black -draw "rectangle $(VISUAL_MASK_CLOCK)" \
                   -fill black -draw "rectangle $(VISUAL_MASK_DATE)" \
                   -fill black -draw "rectangle $(VISUAL_MASK_WEATHER)"

# IM7 renames the front-end to `magick`; IM6 (ubuntu-latest's apt imagemagick)
# only ships `convert`. `compare` exists under both, so only the two masking
# invocations go through this.
MAGICK := $(shell command -v magick || command -v convert || echo NO-IMAGEMAGICK)

$(VISUAL_BASELINE):
	@echo "no $(VISUAL_BASELINE) yet — regenerate it with 'make visual-baseline'"; exit 1

visual-baseline:
	pebble build
	pebble install --emulator emery
	@mkdir -p $(VISUAL_DIR)
	pebble screenshot --emulator emery --no-open $(VISUAL_BASELINE)

visual-check: $(VISUAL_BASELINE)
	pebble build
	pebble install --emulator emery
	@# Cold boots: the emulator takes a moment between install and a real first
	@# render, so the first screenshot can catch a half-brought-up face. Settle,
	@# then up to three screenshot+compare attempts, passing on the first AE==0 —
	@# a real rendering regression persists across retries; only boot noise moves.
	sleep 5
	pebble screenshot --emulator emery --no-open $(VISUAL_SHOT)
	$(MAGICK) $(VISUAL_BASELINE) $(VISUAL_MASK_ARGS) $(VISUAL_BASE_MASKED)
	$(MAGICK) $(VISUAL_SHOT) $(VISUAL_MASK_ARGS) $(VISUAL_SHOT)
	@for attempt in 1 2 3; do \
		ae=$$(compare -metric AE $(VISUAL_BASE_MASKED) $(VISUAL_SHOT) $(VISUAL_DIFF) 2>&1 || true); \
		ae_px=$${ae%% *}; \
		echo "visual-check (attempt $$attempt): $$ae_px pixels differ outside the masks"; \
		[ "$$ae_px" = "0" ] && exit 0; \
		if [ $$attempt -lt 3 ]; then \
			sleep 3; \
			pebble screenshot --emulator emery --no-open $(VISUAL_SHOT); \
			$(MAGICK) $(VISUAL_SHOT) $(VISUAL_MASK_ARGS) $(VISUAL_SHOT); \
		fi; \
	done; \
	echo "visual-check FAILED (raw metric: $$ae; diff image: $(VISUAL_DIFF))"; \
	exit 1
