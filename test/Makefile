CC = gcc
CFLAGS = -Wall -Wextra -Werror -I. -I./unity/src -g -DTEST_ENV \
         -fsanitize=address,undefined -fno-omit-frame-pointer
TARGET = test_watchface
SRCS = test_watchface.c pebble_mock.c unity/src/unity.c

# State globals the reset-mirror lint must not find by name in
# reset_all_state(), each with its reason:
#   s_wind_arrows — const-content lookup table, never reassigned; nothing to reset
RESET_LINT_EXEMPT = s_wind_arrows

all: $(TARGET)

# test_watchface.c #includes the app sources directly, so they are build
# inputs too.
$(TARGET): $(SRCS) $(wildcard ../src/c/*.c ../src/c/*.h) pebble.h
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

# Three guards before the binary runs: the registered-runner count (existing),
# the TU-include gate (test_watchface.c builds the app as one translation unit,
# so a module absent from its #include list ships firmware-wide with zero host
# coverage), and the reset-mirror lint (every writable s_* state global defined
# at file scope in src/c must be restored by reset_all_state(), or it leaks
# between tests). The lint's name-grep can't see the three indirect mechanisms,
# so they resolve by hand: the wire-table sentinel walk (any global targeted by
# messaging.c's s_weather_fields), set_slots() (s_complication_slots), and
# reset_ui_snapshot() (s_shown_ui).
test: $(TARGET)
	@defined=$$(grep -c '^void test_' test_watchface.c); \
	runner=$$(grep -c 'RUN_TEST(' test_watchface.c); \
	if [ "$$defined" != "$$runner" ]; then \
	  echo "RUN_TEST drift: $$defined defined tests vs $$runner registered"; exit 1; \
	fi
	@srcs=$$(ls ../src/c/*.c | sed 's#.*/##' | sort); \
	incs=$$(sed -n 's|^#include "\.\./src/c/\([a-z_]*\.c\)".*|\1|p' test_watchface.c | sort); \
	src_only=$$(printf '%s\n' "$$srcs" | grep -Fvx -- "$$incs" || true); \
	inc_only=$$(printf '%s\n' "$$incs" | grep -Fvx -- "$$srcs" || true); \
	if [ -n "$$src_only$$inc_only" ]; then \
	  echo "TU-include gate: src/c modules and test_watchface.c #includes differ"; \
	  for f in $$src_only; do echo "  only in src/c: $$f"; done; \
	  for f in $$inc_only; do echo "  only in test_watchface.c: $$f"; done; \
	  exit 1; \
	fi
	@body=$$(sed -n '/^static void reset_all_state(void) {/,/^}/p' test_watchface.c); \
	if echo "$$body" | grep -q 'set_slots('; then body="$$body s_complication_slots"; fi; \
	if echo "$$body" | grep -q 'reset_ui_snapshot('; then body="$$body s_shown_ui"; fi; \
	wire=$$(grep -hoE '&s_[a-z0-9_]+' ../src/c/messaging.c | tr -d '&' | sort -u | tr '\n' ' '); \
	missing=""; \
	for name in $$(grep -hoE '^(static )?(const )?[A-Za-z_][A-Za-z0-9_]* ?\*? ?s_[a-z0-9_]+ ?(\[[^]]*\])? *[=;]' ../src/c/*.c \
	    | grep -vE '\(|const [A-Za-z_][A-Za-z0-9_]* +s_' \
	    | grep -oE 's_[a-z0-9_]+' | sort -u); do \
	  case " $$wire " in *" $$name "*) continue;; esac; \
	  case " $(RESET_LINT_EXEMPT) " in *" $$name "*) continue;; esac; \
	  echo "$$body" | grep -qw "$$name" || missing="$$missing $$name"; \
	done; \
	if [ -n "$$missing" ]; then \
	  echo "reset-mirror lint: state globals reset_all_state() does not restore:$$missing"; \
	  exit 1; \
	fi
	./$(TARGET)
