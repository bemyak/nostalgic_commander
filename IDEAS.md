# Ideas

Unvetted brainstorm space — nothing here is decided, let alone promised.
When an idea gets the nod it graduates to [TODOs.md](TODOs.md); when it's
rejected it gets deleted (git history remembers). Proposals welcome — see
[CONTRIBUTING.md](CONTRIBUTING.md) for what a good pitch includes.

## Complications

- Distance Walked
- UTC Offset (e.g. -07:00 or +08:00)
- Wind (speed + direction). Open questions: direction as 8-way compass
  letters (`NW 12k`) vs degrees (`240°`; ° exists in the font) — arrows are
  out, the font has cardinals only; speed = now vs next-12h max (the face's
  weather is future-looking); unit follows the imperial/metric toggle.
  Data path is the existing Open-Meteo request plus two fields
  (wind_speed_10m, wind_direction_10m). `NW 12k` = 6 cells, fits both slot
  sizes.

## Interactions

> **Platform note (June 2026):** touchscreen interactions are off the table
> for watchfaces. PebbleOS reserves the touch sensor for watchapps — a
> watchface's `touch_service_subscribe()` silently no-ops (see
> `src/fw/applib/touch_service.c` in
> [coredevices/PebbleOS](https://github.com/coredevices/PebbleOS): "Touch is
> reserved for watchapps; watchfaces must not consume it"). Note that
> `touch_service_is_enabled()` still returns true on watchfaces, so don't
> trust it. Accelerometer tap (`accel_tap_service_subscribe`) remains the
> only gesture input available to watchfaces.
