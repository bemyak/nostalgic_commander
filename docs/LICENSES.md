# Upstream dependency license audit

Audited 2026-06-10. Every upstream dependency of this project, how it is used,
and its license.

## Code dependencies

| Dependency | How it's used | License | Notes |
|---|---|---|---|
| [@rebble/clay](https://github.com/pebble-dev/clay) ^1.0.3 | npm dependency, bundled into the PebbleKit JS config page | MIT | Zero transitive runtime dependencies — the entire npm tree is this one package. |
| [PebbleOS / Pebble SDK](https://github.com/coredevices/PebbleOS) | Build-time SDK (headers, system fonts, `wscript` build rules); the watchface links against firmware APIs on-device | Apache 2.0 | Open-sourced by Google in January 2025, now maintained by Core Devices / Rebble. |
| [Unity test framework](https://github.com/ThrowTheSwitch/Unity) | Test-only; git submodule at `test/unity/` (pinned at `bbf8f37`) | MIT | Verified from `test/unity/LICENSE.txt`. Submodule contents are distributed under their own license, not this project's. |

`test/pebble.h` and `test/pebble_mock.c` are hand-written mocks original to
this project, not copies of SDK code.

## Bundled assets

| Asset | How it's used | License | Notes |
|---|---|---|---|
| [Px437 IBM VGA 8x16](https://int10h.org/oldschool-pc-fonts/) by VileR | Bundled TTF (`resources/fonts/`); every glyph the face renders comes from it, baked per build into two sizes | [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) | Modified: four diagonal arrows (U+2196–2199) added as new glyphs, drawn on the font's own 8x16 grid; every original glyph is untouched. The baked font resources inside the pbw are derivatives of this asset and stay under CC BY-SA 4.0. Attribution per the license: this file's table plus the README License section. |

The system fonts PebbleOS draws outside the watchface app area are firmware
assets (Apache 2.0), not bundled here.

## Data / service dependencies

| Service | How it's used | License / terms |
|---|---|---|
| [Open-Meteo Weather API](https://open-meteo.com/en/licence) | `src/pkjs/index.js` fetches current weather + UV index | API data is CC BY 4.0. Free tier is **non-commercial use only** (up to 10,000 calls/day) with attribution required. |
| [Open-Meteo Air Quality API](https://open-meteo.com/en/licence) | `src/pkjs/index.js` fetches US AQI | Same terms as above. |

Open-Meteo's CC BY 4.0 attribution: include "Weather data by
[Open-Meteo.com](https://open-meteo.com/)" where people encounter the work.
Today that's the README (the settings page deliberately carries no
about/credits section). **If Nostalgic Commander ever ships to the Rebble store, put the
attribution in the store listing description** — end users don't see the
README.

## Compatibility conclusion

All code dependencies are permissive (MIT / Apache 2.0), so this project is
free to choose any license it wants, including a noncommercial one — there is
no copyleft (GPL/AGPL) code anywhere in the tree that would force a particular
outbound license. The Open-Meteo free tier being non-commercial-only actually
*reinforces* a noncommercial project license: a commercial fork would violate
the API terms as well.

This project is licensed under the
[PolyForm Noncommercial License 1.0.0](../LICENSE.md) — see the README for the
rationale.
