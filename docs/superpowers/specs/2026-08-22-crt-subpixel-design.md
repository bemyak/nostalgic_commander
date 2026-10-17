# CRT subpixel rendering — design

Date: 2026-08-22. Status: **unapproved proposal, nothing built.** It came out of
a read-only review of the CRT pass (`src/c/crt.c`, working copy of "Strike reads
from a captured framebuffer snapshot"); the review's bug findings are a separate
matter and are not repeated here.

One fact gates half the design and is not in any source: the panel's
colour-filter geometry. The aperture grille depends on it; the chromatic
aberration work does not. See [Go/no-go](#gono-go-the-filter-geometry) before
costing the grille.

## Why

The pass quantizes every stage to whole logical pixels. Two consequences,
both measured during the review:

- The chromatic-aberration stage has three hard zones (0/1/2 px shift), so the
  fringe switches on at a visible ring. Real misconvergence is continuous. The
  vignette dodged the same artifact with ordered dithering; the CA never got it.
- The tube's mask structure — shadow mask or aperture grille — is absent, and
  from logical pixels on a 200 px-wide screen it cannot be drawn at all: a 1 px
  pattern is the content, not a texture over it.

The panel's own R/G/B elements form a grid three times finer than the
framebuffer. Addressed directly, they answer both points.

## What the panel gives us

From [coredevices/PebbleOS](https://github.com/coredevices/PebbleOS), verified
by reading the driver:

- Emery's physical board is `obelix` (`tools/waf/pebble_test.py:422` maps board
  `obelix` → `CONFIG_PLATFORM_EMERY`), 200x228, `PBL_COLOR`
  (`fw/board/displays/display_obelix.h:12-17`).
- The panel is driven over the SoC's LCDC in a JDI parallel interface
  (`fw/board/boards/board_obelix.c:60-67`), pinmuxed as six discrete lines,
  `r1,r2,g1,g2,b1,b2` (`board_obelix.c:90-121`) — two bits per element, which is
  where the 64 colours come from.
- **The app's byte reaches those lines unmodified.** `display_update()` converts
  the row from 222 to the LCDC's 332 packing by moving bit positions only
  (`drivers/display/sf32lb/display_jdi.c:439-444`), and the driver has no
  gamma table, no LUT, no spatial or temporal dither. Writing `R=3,G=0,B=0`
  drives the red pair and nothing else. This is the precondition the idea
  normally dies on; here it holds.
- Orientation is row-major, vertically inverted, no transpose
  (`display_obelix.h:6-9`), and the driver's horizontal byte-swap path is
  inactive on production hardware (`display_jdi.c:446-454`, gated on
  `DISPLAY_ORIENTATION_ROTATED_180`, which is 0 for obelix). So framebuffer x
  maps straight to panel column, and the vertical mirror cannot flip element
  order.
- PebbleOS has no subpixel antialiasing anywhere. On colour platforms "AA" is a
  whole-pixel coverage blend of the packed byte (`gtypes.c:684-704`), and the
  1-bit dither path is `#if !PBL_COLOR` (`graphics_private_raw.c:32`). Nothing
  in the OS competes with us for element-level control.

Two things were **inference, not source**: that the elements are a horizontal
stripe, and that a partially-lit element's optical centroid sits at the element
centre. Both are now measured on hardware — see the Result below. The panel does
divide the element's area to make levels, as memory-in-pixel panels usually do,
but it does so symmetrically, so the centroid does not move with level.

## Go/no-go: the filter geometry

No source names the panel part or states the filter layout — round boards name
their Sharp panel, obelix names nothing. Settle it on hardware, not from a
datasheet hunt. The whole experiment is one sitting:

1. A throwaway static face (not in this repo), everything on one screen, black
   background, no interaction: a white column, one black column, then a 1 px
   pure-R column — repeated for G and B — plus each channel as 1 px columns at
   levels 1, 2 and 3. Write the x positions down; the photograph is unreadable
   without an index.
2. **The white anchor is the trick.** A lone red column gives a stripe with no
   reference — nothing says which element inside the pixel lit. The anchor lights
   all three of its elements, so you count dark element-widths from its right
   edge to the red stripe: **3 = R leftmost (RGB), 5 = R rightmost (BGR)**,
   anything else means it is not a horizontal stripe and the grille is dead. At
   ~0.126 mm pixel pitch that is a 126 µm versus 210 µm gap.
3. Rig for a reflective panel, not an emissive one: diffuse external light at
   ~45°, shoot straight on, watch on a stand, front light **off** — its grazing
   illumination hides element structure. An element is ~42 µm, so this needs
   about 1:1 magnification: a USB microscope, or a phone behind a 10x loupe. A
   bare phone macro mode sits at the limit and will probably waste the trip.
4. Same framing, levels 1/2/3. This is the only part of the experiment that says
   whether a partially-lit element lights a fixed sub-area (centroid at the
   element centre, as the model assumes) or a level-dependent one (centroid
   moves, and the 1/3 px cancellation is approximate). Step 2 cannot answer it.
5. While set up: one frame at 1/1000 s and one at 1/30 s, which shows whether the
   panel adds frame-rate control the driver does not. (Not needed in the end —
   see the Result.)

### Result — 2026-08-22, hardware

Probe at `~/projects/crt-subpixel-probe`, built and run on the watch. Read by
eye through a 60x loupe; no camera, so everything below is an observer
judgement at that magnification, not a measurement.

- **Order: RGB, red leftmost.** The white anchor column reads red at its left
  edge and blue at its right, matching the probe's painted layout. Lens CA ruled
  out by the observer. This is outcome 1 — the design applies as written, and the
  grille is unblocked rather than voided.
- **Pitch** as modelled: ~42 µm elements, ~126 µm logical pixel.
- **Levels are area fill, not dimming.** At code 1 the stripe shows sparse dots,
  at 2 larger and denser ones, at 3 it is solid. So the element's levels come
  from filling more of its area — which makes code values approximately linear in
  emitted light, and removes the need for a gamma round trip in stage 1.
- **Centroid is level-stable.** The dot fill stays symmetric about the stripe
  centre at every level, so the 1/3 px cancellation holds.
- **Sub-element lanes are not resolvable at 60x**; each lit channel reads as one
  filled rectangle per stripe. The design needs order and centroid, not lane
  structure, so this blocks nothing.
- **Frame-rate control: ruled out, without the shutter probe.** Stable dot
  structure visible to the eye means the division is spatial. A temporal scheme
  would integrate to a uniformly dim stripe over the eye's ~1/20 s, not to
  countable dots. With the driver already known to have no FRC, the shutter
  comparison would add nothing.

Unmeasured and left open: the exact dot-area ratios against 1:2:3, and any
asymmetry below what 60x by eye resolves.

Three outcomes:

1. **RGB stripe** — design applies as written.
2. **BGR stripe** — same design, every sign constant flips.
3. **Not a stripe** (RGBW quad, diagonal mosaic, anything 2D) — the grille and
   the 1/3 px baseline are both void. The weighted fringe below survives intact:
   it moves a pixel's centroid by arithmetic and does not depend on element
   geometry, only on the calibration constant. So the CA improvement is not
   actually gated on this photograph; the grille is.

**None of this is observable in the emulator.** `board_qemu_emery.c:36-47` is a
synthetic 200x228 framebuffer with no display driver, so QEMU renders logical
pixels and the entire point of the design is invisible there. Screenshots and
`make visual-check` can prove nothing about the effect; they can only prove
nothing else moved. Judging it requires the watch.

## The model in element units

Take an RGB stripe. Logical pixel `x` covers `[x, x+1)`, so its elements sit at
`x+1/6` (R), `x+1/2` (G), `x+5/6` (B) — 1/3 px apart. Content column `c` is at
`c+1/2`.

Today's stage 1 writes, on the left half, `R[x]=src[x+s]`, `G[x]=src[x]`,
`B[x]=src[x-s]`. Against the geometry above that puts the red raster at
`-(s+1/3)` px and the blue at `+(s+1/3)` px, so the R↔B separation is
`2s + 2/3`:

| `s` | separation |
| --- | ---------- |
| 0   | 0.67 px    |
| 1   | 2.67 px    |
| 2   | 4.67 px    |

Two facts fall out. The rungs are 2 px apart, which is the banding. And the
centre of the screen, where the model says convergence is perfect, carries 2/3
px of fringe that nobody asked for.

The quantum is not reachable by integer sampling alone — each channel's grid is
still 1 px. Two mechanisms can reach it, and the choice between them is the
whole design.

**Dithering the column shift along x does not work here.** Mixing `s` and `s+1`
in a 2:1 pattern gives a mean shift of `s+1/3`, but the eye does not integrate
*position* the way it integrates intensity. Take a 1 px white line at column `c`
in a region targeting 1.33 px: the destination pixel that picks it up satisfies
`x + s(x) = c`, and with `s` alternating between 1 and 2 by `x mod 3` both
`x = c-1` and `x = c-2` can satisfy it — so the line's red ghost appears twice,
at full level, as a 2 px fringe whose thickness depends on where the feature
lands modulo 3. On smooth content the mean is what you see; on 1-2 px features
it duplicates or drops them. This face is nothing but 1-2 px features: 8x16
glyph stems and 2 px frame strokes.

**Weighting energy across neighbours does work.** Instead of copying one
neighbour's channel wholesale, take the fractional sample:
`R[x] = (2*src[x+s].r + src[x+s+1].r) / 3` for a 1/3 px offset. This is subpixel
antialiasing in the ClearType sense, and at four levels the weights land exactly
for the case that dominates — a 0↔3 transition yields levels 2 and 1, a real
centroid shift with no duplication and no periodic pattern. Off that case the
error is under one level and it is smooth. Exactness depends on the sampled
*values* (`2a+b` divisible by 3), not on `s`, so an integer offset added on top
— the strike's `ca_boost` — leaves the recipe intact.

Zero separation at the centre comes out of the same arithmetic: blend
`src[x-1].r` and `src[x].r` at 1/3, 2/3 to cancel the element's built-in offset.
This depends on the element's optical centroid staying put as its level changes,
which the hardware probe confirms — the dot fill that makes the levels is
symmetric about the stripe centre. The bound on that is an eyeball at 60x, so a
sub-resolution asymmetry is not excluded; what is excluded is gross migration,
and anything below that bound is far below what a 42 µm element can express.

Weighting is allowed here for a reason that does not extend to stage 3: the
fringe is a chroma perturbation and may dim, whereas a warped white pixel may
not. That is why stage 1 gets fractional sampling and the curvature does not.

What still limits the result: the *displacement* becomes continuous, but the
fringe *intensity* is still four levels, so the zone transition softens rather
than disappears. And the grille below spends one of those levels on the same
pixels — see Risks.

## Stage by stage

1. **Horizontal CA — adopt.** Express the zone table as a target displacement in
   thirds and hit it with the weighted sample above. Zone boundaries stay in the
   existing squared-radius form; the rung values, their count, and the sampling
   arithmetic change. Note this stage no longer just permutes bits — it computes
   a weighted average per channel, which is the one place in the pass that reads
   two source pixels per channel.
2. **Vertical CA — no change.** The element structure is horizontal, so vertical
   misconvergence gains nothing from it. If finer vertical steps are ever wanted
   they come from dithering along y, independently of this design.
3. **Curvature — do not use subpixel.** A *common* fractional shift of all three
   channels needs energy spread between neighbouring elements, which needs
   intensity headroom; with four levels that exists only in mid-tones, and a
   saturated white pixel cannot move at all without dimming. The pass also runs
   after compositing, so it can only permute and attenuate quantized values —
   it cannot reconstruct the pre-quantization image to resample it. Keep the
   warp integer.

   Do **not** dither the inset along y to soften the stair-steps. The failure
   mode is not the same as stage 1's — a dithered row is still resampled
   monotonically, so nothing is duplicated or dropped — but it is the same class
   of harm: adjacent rows disagree by 1 px, so a vertical stem crossing the
   dithered band gets a comb edge instead of a single kink. On a face made of
   8x16 stems and 2 px strokes, one static step reads better than a moving comb.

   The steps are real, and worth recording so this is not re-litigated:
   `inset` changes value at `dy = 227*sqrt(k/5)`, i.e. eight rows
   (y ≈ 12, 26, 42, 63, 164, 185, 201, 215). Adjacent insets differ by about
   690 in `mul16`, so `sx` moves by 1 px wherever `|dx| >= 95` — columns
   `x <= 52` and `x >= 147`, at depths where the vignette is still at full 256.
   So it is a visible 1 px discontinuity across the outer quarter of those
   rows, not an edge-only artifact.

   The fix is the one the physics review already named: the warp is rank-1 (a
   y-dependent horizontal gain with no x-nonlinearity). Give it the proper
   radial term and the step boundary becomes a curve in x instead of a
   horizontal seam, which is what a curved surface looks like — no geometry
   dithering needed. That is a separate change from this design.
4. **Aperture grille — new, optional.** Attenuate channel `x mod 3` by one level
   in the stage 3 output loop, indexed by **destination** x, skipping black.
   That is the mask at native pitch. It belongs in screen space because the mask
   is a property of the tube, not of the picture — unlike the vignette, which
   the current code deliberately warps with the content. On white the luminance
   cost is about 11% (one level of one channel of three) with a 3 px period.

Explicitly not doing, so it is not re-proposed: **scanlines** (the stripe is on
the x axis, scanlines need vertical resolution, and there is no rotated mount to
exploit — `display_obelix.h:6-9`); **subpixel text** (the 8x16 VGA cells are
grid-aligned by design, and subpixel positioning would muddy the stems and fight
the 8 px column grid); **a gamma round trip before weighting** — normally
subpixel filtering has to weight in linear light, but the probe found the panel
makes its levels by filling more of the element's area, which is linear in
emitted light by construction. Code values are therefore approximately
proportional to output and stage 1 can weight them directly. The dot areas were
not measured against an exact 1:2:3 ratio, so "approximately" is doing real work
here; check it if the fringe comes out visibly wrong in brightness.

## Cost

Stage 1 is the one that gets more expensive: 5 source reads per pixel where it
does 3 today (R and B take two horizontal taps each, G stays at one — it is the
reference and has no offset), plus a weighted average per shifted channel,
against today's two compares and three masked ORs. The vertical CA does not
multiply this: `ry`/`by` select which ring row R and B read from, so both of a
channel's horizontal taps come from the same row and the row memcpys are
unchanged. Stage 3 gains one lookup and one subtract per pixel. The pass is
three loops over 45,600 px today and already runs on every render, with the
strike asking for a frame every 90 ms — and the review's LUT comments record
that this pass has been the difference between 3-4 fps and usable before.
Hardware timing is the thing to watch, not the loop count.

## Tests

- Element ladder: target displacement is monotone in radius and mirror-symmetric
  about both centrelines.
- No feature duplication: a 1 px white line at every column position, at every
  rung, produces exactly one red edge and one blue edge — the regression test
  against reintroducing shift dithering.
- Zero point: at the centre the modelled separation is 0, not 2/3 px.
- Strike stacking: `ca_boost` is in whole pixels, not thirds — pin it, because
  reading it as thirds silently cuts the strike's channel splay to a third of
  its amplitude. At the maximum (`s` = 2 + boost 3 = 5) both taps of a channel
  clamp to the same edge column, which degenerates the weighted sample to a
  plain copy; pin that too, since it is the only place the weights collapse.
- Grille: on a uniform white field every 3 px column group has exactly one
  attenuated channel; black stays black; the pattern is invariant under
  `flash_phase` — that last one is the regression test for keeping the mask in
  screen space.
- `test_crt_ca_onset_should_cut_at_the_dead_zone` and
  `test_crt_pure_geometry_should_match_the_spec` both assert whole-pixel shifts
  and have to be rewritten in thirds. Breaking change to the test surface, not
  an incidental edit.
- `test/visual/baseline.png` changes (the grille moves logical pixels), so the
  baseline must be regenerated and `make visual-check` fails until it is.

## Risks

At four levels the grille may read as colour dirt rather than as structure.
Worse, the grille and the weighted fringe compete for the same levels: the
grille spends one of three on every pixel, and the fringe's intermediate
positions are expressed in exactly those levels. Doing both may be worse than
doing either, so they land as separate changes, fringe first. The vignette's
Bayer dither adds a third claim on the same pixels near the rim. A reflective
MIP stack diffuses element structure, so the visible payoff is smaller than the
same trick on an emissive panel. Every one of these is a hardware judgement —
the emulator cannot answer any of them.
