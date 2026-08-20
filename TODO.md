# vidwizard — later work

Not in 0.4.0-alpha. Same CLI shape when we add flags: optional
`START-END,START-END,…` (including `10-` / `-20`).

## Before 1.0 (priority)

1. **Overlapping ranges** (below) + a documented compose order + golden frames.
2. **`--dry-run`** (print outputs / graph, encode nothing) and clearer progress.
3. Stream-copy `--cut` / whole `--mute` when pixels need not change.
4. `--reverse` without holding the whole window in RAM, or a hard refuse.
5. Cheap looks: `--mirror`, `--blur`, `--fade` — not stabilize/shake/infra yet.
6. Profile a 1080p clip (`make -s V=0 -j$(nproc) profile`) before optimizing.

Do not grow a NLE. 1.0 should feel finished, not full.

### Tests

Parsers/CLI are in decent shape. Still missing **picture proof**: gray∩crop,
zoom∩text, speed∩reverse, `--text` at 0,0 vs a ranged crop (label got cut
off), stereo `--speed`, open-ended `10-` on a real duration. Small
`testdata/golden/` clips beat another pile of unit tests.

### Performance

Always re-encode today (libx264). `--cut`/`--mute` could often copy.
`zoompan` is not free at 4K. Explode already threads PNG encode; decode
stays serial. Do not micro-optimize `parse_time.c`.

### Code

Keep C parsers (CBMC). Turn `filter_spec.cpp` into **ops + time slices →
one graph emitter**. Leave `transcode.cpp` as “talk to libav.”

---

## Overlapping vs sequential

### Two different clocks (easy to mix up)

| Model | Meaning | vidwizard today |
|--------|---------|-----------------|
| **Timeline** | Every effect has a time on the *source* clock. Two ranges that intersect both apply on the overlap. | This is what `--grayscale=0-10 --crop=…:5-15` *should* mean. |
| **Concat / “after that”** | Sequential *segments*: finish A, then B starts. Duration of A defines when B begins. | Not the current model. Whole-clip `--grayscale` has no “after.” |

`--crop --grayscale` with **no** ranges already **stacks** (crop, then gray)
on the whole clip. That is sequential *filters*, one timeline. The hole is
**ranged** effects that share part of the time axis.

### Meta-markers (`--overlap` / `--sequential`)

Idea:

```text
vidwizard in.mp4 --grayscale --overlap --crop --reverse --sequential --mirror
```

Read as: default sequential grayscale; then a group where crop **and**
reverse start together; then sequential mirror.

**Partly on the right track** — grouping “these flags share a clock” is
real. Hard parts:

- Flag **order becomes significant**. Today options may appear in any
  order. Mode switches (`--overlap` applies to *following* flags) break
  that unless we document “markers are left-to-right.”
- **When does the overlap group start?** “Right after grayscale” only
  works if grayscale is a *segment* with an end. Whole-clip grayscale
  never ends, so “then overlap crop/reverse” has no time to attach to.
- Mixing **both** models on one line (absolute `5:00-` *and* “after the
  previous group”) is the expensive design. Users will write both.

### Easier first step (still 1.0-shaped)

Stay on the **source timeline**. Ranges already say *when*. Overlap =
intersection. Compose order (fixed, documented), e.g.:

crop → zoom → color (gray/…) → text → speed → reverse

```text
vidwizard in.mp4 --grayscale=0-10 --crop=160x120+0+0:5-15
```

- 0–5s: grayscale only
- 5–10s: grayscale **and** crop
- 10–15s: crop only

No new marker required for “both at once.”

Optional sugar later: **`--overlap` = inherit the previous effect’s
window** so the user does not repeat the range:

```text
vidwizard in.mp4 --grayscale=5-10 --overlap --crop 160x120 --text Crop
```

means crop + text also 5–10s, stacked with gray. `--sequential` (or
end of `--overlap` group) goes back to each flag carrying its own
range (or the whole clip).

That is easy for the user *and* for the parser: one bit of state
(inherit window vs not). It does **not** invent “start after previous
segment” times.

### Later (concat segments)

If we ever want “gray the first 5s, *then* crop+reverse the rest” without
absolute times, that is a **second** language (playlist of stacks).
Possible markers: `--then` / `--also`, or explicit ranges only.
Do not ship that in the same breath as intersection-overlap.

### Implementation sketch (intersection)

1. Collect every effect and its range (empty range = whole duration).
2. Sort unique endpoints → slices.
3. For each slice, the set of active effects; emit one filter chain in
   compose order; concat slices.
4. Tests: golden frames for gray∩crop, zoom∩text, speed∩reverse.

---

## Wanted looks (after the timeline is honest)

| Flag (sketch) | What |
|---------------|------|
| `--mirror` / `--mirror=h\|v\|both` | Flip the picture horizontally, vertically, or both. |
| `--infra` / `--infravision` | Heat-vision look: false-color LUT, crush mids, maybe scanlines. |
| `--blur FACTOR[:RANGES]` | Gaussian / box blur; ranged for “go soft then snap back”. |
| `--sharpen` | Unsharp / cas. |
| `--vignette` | Darken edges. |
| `--shake` | Handheld / impact. |
| `--fade in\|out[:RANGE]` | Opacity ramps. |
| `--rotate DEG` | 90/180/270 and small angles. |
| `--stabilize` | Deshake (heavy). |

`--text` and `--zoom` already cover labels and fill-frame zoom.

## Notes

- Keep defaults high-quality; extra looks are opt-in flags only.
- Prefer libavfilter (`hflip`, `vflip`, `gblur`, `colorchannelmixer` /
  `pseudocolor` for infra) over a second pipeline.
- Infra: not real thermal — a stylized LUT is enough.
- `--dry-run`: print output names / graph, encode nothing.
