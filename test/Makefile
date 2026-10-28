CC = gcc
CFLAGS = -Wall -Wextra -Werror -I. -I./unity/src -g -DTEST_ENV \
         -fsanitize=address,undefined -fno-sanitize-recover=all \
         -fno-omit-frame-pointer
TARGET = test_watchface
SRCS = test_watchface.c pebble_mock.c unity/src/unity.c

# Hosts that build the app as one translation unit by #including src/c/*.c.
# Every such file must include every module, and the TU-include gate below
# checks each of them: a module missing from test_watchface.c ships firmware-
# wide with zero host coverage, and one missing from a tool breaks its link —
# which `make test` would not otherwise notice, since it never builds them.
TU_HOSTS = test_watchface.c vignette_ramp.c

# State globals the reset-mirror lint must not find by name in
# reset_all_state(), each with its reason:
#   s_wind_arrows — const-content lookup table, never reassigned; nothing to reset
RESET_LINT_EXEMPT = s_wind_arrows

all: $(TARGET)

.PHONY: all test ramp clean

# test_watchface.c #includes the app sources directly, so they are build
# inputs too.
$(TARGET): $(SRCS) $(wildcard ../src/c/*.c ../src/c/*.h) pebble.h
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

# The vignette measuring tool (test/vignette_ramp.c). Same host build as the
# suite, minus the sanitizers — it is an instrument, not a test. Unity comes
# along because pebble_mock.c asserts through it.
vignette_ramp: vignette_ramp.c pebble_mock.c unity/src/unity.c \
               $(wildcard ../src/c/*.c ../src/c/*.h) pebble.h
	$(CC) -Wall -Wextra -Werror -I. -I./unity/src -g -DTEST_ENV \
	      vignette_ramp.c pebble_mock.c unity/src/unity.c -o vignette_ramp

ramp: vignette_ramp
	./vignette_ramp

clean:
	rm -f $(TARGET) vignette_ramp

# vignette_ramp builds first so API drift under it fails here. Three guards
# before the binary runs: the registered-runner set (every defined
# test is registered, and the reverse),
# the TU-include gate, and the reset-mirror lint (every writable s_* state global defined
# at file scope in src/c must be restored by reset_all_state(), or it leaks
# between tests). The lint's name-grep can't see the three indirect mechanisms,
# so they resolve by hand: the wire-table sentinel walk (only the globals
# targeted by messaging.c's s_weather_fields and s_settings_fields),
# set_slots() (s_complication_slots), and reset_ui_snapshot() (s_shown_ui).
test: $(TARGET) vignette_ramp
	@defined=$$(mktemp); runner=$$(mktemp); \
	sed -n 's/^void \(test_[a-zA-Z0-9_]*\)(void).*/\1/p' test_watchface.c | sort > $$defined; \
	tr -d '\n' < test_watchface.c | grep -oE 'RUN_TEST\( *test_[a-zA-Z0-9_]*' \
	  | sed 's/RUN_TEST( *//' | sort > $$runner; \
	drift=$$(comm -3 $$defined $$runner | sed 's/^\t/  registered but not defined: /; t; s/^/  defined but not registered: /'); \
	rm -f $$defined $$runner; \
	if [ -n "$$drift" ]; then echo "RUN_TEST drift:"; echo "$$drift"; exit 1; fi
	@srcs=$$(ls ../src/c/*.c | sed 's#.*/##' | sort); \
	for host in $(TU_HOSTS); do \
	  incs=$$(sed -n 's|^#include "\.\./src/c/\([a-z_]*\.c\)".*|\1|p' $$host | sort); \
	  src_only=$$(printf '%s\n' "$$srcs" | grep -Fvx -- "$$incs" || true); \
	  inc_only=$$(printf '%s\n' "$$incs" | grep -Fvx -- "$$srcs" || true); \
	  if [ -n "$$src_only$$inc_only" ]; then \
	    echo "TU-include gate: src/c modules and $$host #includes differ"; \
	    for f in $$src_only; do echo "  only in src/c: $$f"; done; \
	    for f in $$inc_only; do echo "  only in $$host: $$f"; done; \
	    exit 1; \
	  fi; \
	done
	@body=$$(sed -n '/^static void reset_all_state(void) {/,/^}/p' test_watchface.c | sed 's|//.*||'); \
	if echo "$$body" | grep -q 'set_slots('; then body="$$body s_complication_slots"; fi; \
	if echo "$$body" | grep -q 'reset_ui_snapshot('; then body="$$body s_shown_ui"; fi; \
	wire=$$(sed -n '/^static const MessageField s_\(weather\|settings\)_fields\[\] = {/,/^};/p' ../src/c/messaging.c \
	    | grep -oE '&s_[a-z0-9_]+' | tr -d '&' | sort -u | tr '\n' ' '); \
	missing=""; \
	for name in $$(awk -f reset_lint.awk ../src/c/*.c | sort -u); do \
	  case " $$wire " in *" $$name "*) continue;; esac; \
	  case " $(RESET_LINT_EXEMPT) " in *" $$name "*) continue;; esac; \
	  echo "$$body" | grep -qw "$$name" || missing="$$missing $$name"; \
	done; \
	if [ -n "$$missing" ]; then \
	  echo "reset-mirror lint: state globals reset_all_state() does not restore:$$missing"; \
	  exit 1; \
	fi
	./$(TARGET)
