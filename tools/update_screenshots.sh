#!/usr/bin/env bash
# Regenerate the five store captures under screenshots/ on the emery emulator.
# Run from the repo root: make screenshots
#
# Everything user-visible is injected, nothing needs a phone:
#   settings + slot assignment -> AppMessage (the face persists them itself)
#   weather payload            -> AppMessage (same frame the JS side would send)
#   steps/sleep/active         -> emu-steps / emu-sleep / emu-active-time
#   battery + BT + 12/24h      -> emu-battery / emu-bt-connection / emu-time-format
#   clock                      -> gdb write to the QEMU RTC register, per shot
#
# Emulator facts this flow is built around (Core Devices QEMU fork, emery):
#   - pebble-tool's post_connect re-sends host time (SetUTC) on EVERY
#     --emulator/pypkjs connection, slamming the watch clock. The RTC device's
#     TIME_LO register write instead computes a persistent offset inside QEMU
#     (hw/misc/pebble_rtc.c), so a write through the QEMU gdb stub pins the
#     clock. Hence: install once over --emulator, then kill pypkjs, pin via
#     gdb, and drive everything afterwards over direct --qemu (skips
#     post_connect, so nothing un-pins).
#   - The displayed time is RTC(UTC) + the emulator's persisted timezone
#     (EEST here), so PIN_UTC is three hours behind the wanted wall time.
#   - pypkjs fetches REAL weather on every WEATHER_REQUEST; killed before the
#     capture loop, so the injected payload stays final.
#   - emu-heart-rate is useless for watchfaces: hrm_manager drops injected
#     samples when no process holds an HRM subscription. No slot config below
#     uses heart rate.
#
# AppMessage's inbox is 256 B, so settings+slots and weather go as two
# messages. Wire key values are read from the build tree, never hardcoded:
# package.json messageKeys reordering stays safe.
set -euo pipefail

SETTLE=3
# Back to the Future: the first DeLorean jump — the face shows 01:21 on
# Saturday Oct 26 1985 (ISO week 43). UTC value behind the persisted +03:00.
PIN_UTC="1985-10-25T22:21:00Z"

# Health values picked to light up status colors without looking contrived.
STEPS=8432 SLEEP_TOTAL=441 SLEEP_RESTFUL=210 ACTIVE_TIME=27

# Values are display-ready, matching each shot's units (the phone reduces
# into watch units; here we play the phone).
WEATHER_IMPERIAL=(WEATHER_TEMP=72 WEATHER_COND=61 WEATHER_AQI=78 WEATHER_UV=8
  WEATHER_HUMIDITY=61 WEATHER_WIND_DIRECTION=238 WEATHER_WIND_SPEED=14
  WEATHER_PCP=65 WEATHER_PRECIP_NOW=1 WEATHER_HIGH=81 WEATHER_LOW=58
  WEATHER_LOW_TOMORROW=55 WEATHER_TEMP_HIGH_TOMORROW=76
  WEATHER_HI_HOUR_TODAY=14 WEATHER_LO_HOUR_TODAY=5
  WEATHER_HI_HOUR_TOMORROW=15 WEATHER_LO_HOUR_TOMORROW=4)
WEATHER_METRIC=(WEATHER_TEMP=22 WEATHER_COND=61 WEATHER_AQI=78 WEATHER_UV=8
  WEATHER_HUMIDITY=61 WEATHER_WIND_DIRECTION=238 WEATHER_WIND_SPEED=6
  WEATHER_PCP=65 WEATHER_PRECIP_NOW=1 WEATHER_HIGH=27 WEATHER_LOW=14
  WEATHER_LOW_TOMORROW=13 WEATHER_TEMP_HIGH_TOMORROW=24
  WEATHER_HI_HOUR_TODAY=14 WEATHER_LO_HOUR_TODAY=5
  WEATHER_HI_HOUR_TOMORROW=15 WEATHER_LO_HOUR_TOMORROW=4)

# Wire key ids from the build tree into an assoc map (never hardcoded —
# package.json messageKeys reordering stays safe).
declare -A KEY
while read -r line; do
  name=${line#uint32_t MESSAGE_KEY_}; name=${name%% *}
  val=${line##*= }; val=${val%;}
  KEY[$name]=$val
done < <(grep '^uint32_t MESSAGE_KEY_' build/src/message_keys.auto.c)

# Resolve the key id and emit "id=value" for each name=value pair on stdin.
resolve() {
  local pair
  for pair; do
    echo "${KEY[${pair%%=*}]}=${pair#*=}"
  done
}

peb() { pebble "$@"; }

# Direct-qemu variant: skips the tool's post_connect SetUTC.
q() { pebble "$1" --qemu "$QEMU_TARGET" "${@:2}"; }

# QEMU gdb/serial ports and target string for the running instance.
fill_qemu_handles() {
  local args
  args=$(ps -o args= -C qemu-pebble | head -1)
  [[ -n $args ]] || { echo "qemu-pebble not running" >&2; exit 1; }
  QEMU_TARGET="localhost:$(grep -oP 'serial tcp::\K\d+' <<<"$args" | head -1)"
  GDB_PORT=$(grep -oP 'gdb tcp::\K\d+' <<<"$args")
}

# Write the pinned epoch into the RTC register. QEMU records it as an offset
# and the clock keeps ticking; done per shot so the minute digit never rolls.
pin_clock() {
  gdb -batch \
    -ex "set architecture arm" \
    -ex "target remote :$GDB_PORT" \
    -ex "set {unsigned int}0x40005000 = $PIN_EPOCH" >/dev/null
}

inject_health() {
  q emu-steps "$STEPS"
  q emu-sleep --restful "$SLEEP_RESTFUL" "$SLEEP_TOTAL"
  q emu-active-time "$ACTIVE_TIME"
}

# Args: filename theme units date_fmt short_date_fmt dow window weather_set
#        battery_pct charging_flag slots...
shoot() {
  local file=$1 theme=$2 units=$3 date_fmt=$4 short_fmt=$5 dow=$6 window=$7
  local weather_set=$8 pct=$9 charging=${10}; shift 10

  local settings=(SETTINGS_THEME=$theme SETTINGS_UNITS=$units
    SETTINGS_DATE_FORMAT=$date_fmt SETTINGS_SHORT_DATE_FORMAT=$short_fmt
    SETTINGS_DOW_POSITION=$dow SETTINGS_WEATHER_WINDOW=$window
    SETTINGS_DISCONNECT_VIBE=0)
  # Slot args are listed in key order 1,2,6,3,4,5 (top row, center, bottom row).
  local order=(1 2 6 3 4 5)
  local i slot
  i=0
  for slot in "$@"; do
    settings+=("SLOT_${order[$i]}=$slot")
    i=$((i + 1))
  done

  local wname="WEATHER_${weather_set}[@]"
  peb send-app-message --qemu "$QEMU_TARGET" --uint $(resolve "${settings[@]}")
  sleep 1
  peb send-app-message --qemu "$QEMU_TARGET" --uint $(resolve "${!wname}")
  inject_health
  if [[ $charging == charging ]]; then
    q emu-battery --percent "$pct" --charging
  else
    q emu-battery --percent "$pct"
  fi

  pin_clock
  sleep $SETTLE
  # Raw framebuffer, no colour-correction: the face's claim is exact EGA
  # colors, so the store captures show the real palette, not the corrected
  # hardware rendition.
  q screenshot --no-open --no-correction "screenshots/$file"
  echo "captured screenshots/$file"
}

peb build

PIN_EPOCH=$(date -u -d "$PIN_UTC" +%s)

# A clean, single emulator: `pebble kill` consults its state file and can miss
# strays, so the process table is the source of truth.
peb kill || true
pkill -x qemu-pebble || true
sleep 1
peb install --emulator emery
sleep 6 # cold boot: let the first real render land, per visual-check

# pypkjs ends here: it would push real weather and nothing more is needed
# from the phone side.
pkill -f pypkjs || true
fill_qemu_handles

pin_clock
q emu-time-format --format 24h
q emu-bt-connection --connected yes

# SLOT_1 top-left .. SLOT_6 center (see config.js: 1,2 then 6 then 3,4,5) —
# shot slot args follow that key order: 1 2 6 3 4 5. Each value is a
# ComplicationDataSource id, position-constrained by config.js; 20 = hidden.
shoot 00_minimal.png 2 1 0 0 0 12 METRIC 85 no 20 20 23 20 20 20
shoot 01_time_system.png 2 1 0 0 0 12 METRIC 62 no 21 22 25 33 31 9
shoot 02_weather.png 2 0 0 0 0 12 IMPERIAL 62 no 30 18 27 26 34 28
shoot 03_theme_turbo.png 1 0 2 0 0 12 IMPERIAL 85 no 5 2 23 1 21 0
shoot 04_theme_dark.png 3 1 0 1 1 12 METRIC 85 no 10 0 24 2 21 33
shoot 05_theme_navigator.png 4 1 1 1 2 12 METRIC 85 no 5 32 27 1 10 34
