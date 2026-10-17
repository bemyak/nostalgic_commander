# Ideas

Unvetted brainstorm space — nothing here is decided, let alone promised.
When an idea gets the nod it graduates to [TODOs.md](TODOs.md); when it's
rejected it gets deleted (git history remembers). Proposals welcome — see
[CONTRIBUTING.md](CONTRIBUTING.md) for what a good pitch includes.

## Complications

- Distance Walked
- UTC Offset (e.g. -07:00 or +08:00)
- Wind, future-looking variant: the shipped WIND window is the "now" reading
  (`↗ 12k`). A next-12h max-gust variant was floated and dropped; revisit
  only if someone asks.

## Rendering

- CRT effect at subpixel resolution: use the panel's own R/G/B elements to make
  the chromatic aberration continuous instead of three banded zones, and to draw
  the aperture grille the effect currently has no way to show. Blocked on one
  hardware fact (the colour-filter geometry, which PebbleOS does not state) and
  invisible in the emulator. Design:
  [docs/superpowers/specs/2026-08-22-crt-subpixel-design.md](docs/superpowers/specs/2026-08-22-crt-subpixel-design.md).

## Interactions

> **Platform note (June 2026):** touchscreen interactions are off the table
> for watchfaces. PebbleOS reserves the touch sensor for watchapps — a
> watchface's `touch_service_subscribe()` silently no-ops (see
> `fw/applib/touch_service.c` in
> [coredevices/PebbleOS](https://github.com/coredevices/PebbleOS): "Touch is
> reserved for watchapps; watchfaces must not consume it"). Note that
> `touch_service_is_enabled()` still returns true on watchfaces, so don't
> trust it. Accelerometer tap (`accel_tap_service_subscribe`) remains the
> only gesture input available to watchfaces.
