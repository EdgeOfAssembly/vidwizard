# vidwizard — later work

Not in 0.4.0-alpha. Same CLI shape when we add flags: optional
`START-END,START-END,…` (including `10-` / `-20`).

## Before 1.0 (priority)

1. **Playlist vs `--overlap`** (below): default independent looks;
   markers for cumulative stack. Golden frames for both.
2. **`--dry-run`** (print outputs / graph, encode nothing) and clearer progress.
3. Stream-copy `--cut` / whole `--mute` when pixels need not change.
4. `--reverse` without holding the whole window in RAM, or a hard refuse.
5. Cheap looks: `--mirror`, `--blur`, `--fade` — not stabilize/shake/infra yet.
6. Profile a 1080p clip (`make -s V=0 -j$(nproc) profile`) before optimizing.

Do not grow a NLE. 1.0 should feel finished, not full.

Novices who do not want markers can **run vidwizard twice**: save
`--grayscale` to a file, then `--crop` that file. That *is* cumulative
(second pass sees the first encode). Cost: another lossy re-encode.
`--overlap` is the same idea in **one** decode/encode.

### Tests

Parsers/CLI are in decent shape. Still missing **picture proof**:
playlist gray-then-colour-crop (demo), `--overlap` gray∩crop, zoom∩text,
speed∩reverse, `--text` at 0,0 vs a ranged crop (label got cut off),
stereo `--speed`, open-ended `10-` on a real duration. Small
`testdata/golden/` clips beat another pile of unit tests.

### Performance

Always re-encode today (libx264). `--cut`/`--mute` could often copy.
`zoompan` is not free at 4K. Explode already threads PNG encode; decode
stays serial. Do not micro-optimize `parse_time.c`.

### Code

Keep C parsers (CBMC). Turn `filter_spec.cpp` into **ops + time slices →
one graph emitter**. Leave `transcode.cpp` as “talk to libav.”

---

## Playlist (default) vs cumulative overlap (opt-in)

### What 0.4 actually does (and should keep as baseline)

Default is a **playlist of independent looks on the source**, not a
stack that poisons later stages.

Proof: the catwalk demo `--grayscale=5.5-7.5` then `--crop=…:7.5-9.2`.
The face crop is **still colour**. Gray did not stay in the feed. That
is correct for the baseline: each ranged effect is its own playlist
entry, applied to the **original** picture for that window only.

So we do **not** “already stack” in the sense that matters. Sequential
here means **one look after another in time**, each non-destructive
w.r.t. later windows.

(Caveat: two **whole-clip** flags with **no** ranges, e.g. `--crop --grayscale`,
go through one filter chain today and **do** compose. That is ambiguous
under a playlist reading — both want the entire duration. Call that out
in 1.0: either “no-range multi-effect is one combined look” or require
`--overlap` / ranges.)

### What overlap is for

**Cumulative / destructive:** earlier effects stay in the feed; later
(or simultaneous) effects see that output. Gray then crop in the **same**
window → cropped grayscale. Zoom then text → label on the zoomed
picture (and it stays if the group continues).

That is **not** the default. It needs an explicit **mode marker** so the
program knows the user switched from playlist-independent to stacking.

### Markers (needed)

Left-to-right mode switches (order matters **only** around markers):

| Marker | Mode for the following effect flags |
|--------|-------------------------------------|
| (default) / `--sequential` | Playlist: each effect is independent on the source for its range. |
| `--overlap` | Cumulative: following effects stack on the same feed; they share timing (see below). |

Worked example (locked):

```text
vidwizard in.mp4 --grayscale=0-5 --overlap --crop 160x120 --reverse --sequential --mirror=10-
```

| Time | Picture |
|------|---------|
| 0–5s | Grayscale on the **original** (playlist). |
| 5–10s | `--overlap`: **crop and reverse stacked** on one feed. No range on those flags → group **starts when grayscale ends (5s)** and **runs until the next sequential item (10s)**. |
| 10s–EOF | `--sequential --mirror=10-`: mirror on the **original**. Crop/reverse **do not stick**. They are nullified. |

`--sequential` **ends** the overlap group. The next playlist item is a
fresh look at the source, same as the catwalk gray-then-colour-crop.
If crop/reverse kept going under the mirror, `--sequential` would mean
nothing.

To keep a stack into later time, **omit `--sequential`**. Effects after
`--overlap` stay on that feed; a later `--mirror=10-` **adds** mirror on
top of crop+reverse from 10s onward (crop/reverse still stick).

```text
vidwizard in.mp4 --grayscale=0-5 --overlap --crop 160x120 --reverse --mirror=10-
```

| Time | Picture |
|------|---------|
| 0–5s | Grayscale on original (still a playlist item: it is **before** `--overlap`). |
| 5–10s | Crop + reverse stacked. |
| 10s–EOF | Crop + reverse **still on**, plus mirror. |

That is the “drop `--sequential`” story: the overlap group never ended.

**Caveat:** grayscale in that command is *not* in the overlap group, so
it still ends at 5s (colour crop+reverse, then mirrored). For
**cropped-grayscale + mirror** on one feed, put gray **inside** the
group:

```text
vidwizard in.mp4 --overlap --grayscale=0-5 --crop 160x120 --reverse --mirror=10-
```

No `--sequential` → nothing nullifies the stack; mirror at 10s is extra
on the same cumulative picture.

Same command may mix playlist entries and overlap groups. That is the
point.

### Timing rules (locked)

- Playlist items: range on the flag, or whole clip if omitted (see
  whole-clip caveat above).
- `--overlap` group with **no** ranges on its flags: **starts at the
  previous item’s end**, **ends at the next `--sequential` item’s
  start** (or EOF if none). That matches the example (5s–10s).
- `--overlap` plus an explicit range on a flag: that range wins for
  that flag (still stacked with the rest of the group on the
  intersection — specify this in implementation tests).
- `--sequential` clears stack state. Following effects are independent
  on the source; they **nullify** the previous overlap feed.
- Same-window stacking (gray *and* crop *during* 0–5s) is a different
  request: put `--overlap` **before** those flags and give them the
  **same** range, e.g. `--overlap --grayscale=0-5 --crop 160x120`.
  Do not overload the example’s “after gray, until mirror” meaning.

### Why this is still hard (honest)

- Markers make **flag order** matter next to `--overlap` / `--sequential`.
  Rest of the CLI can stay order-independent.
- Implementation: playlist slices each get `source → that effect`;
  overlap groups get `source → effect1 → effect2 → …` for that slice,
  then concat. Golden frames: demo-style gray then colour crop
  (independent); `--overlap` crop+reverse 5–10 then colour mirror
  (stack does not leak past `--sequential`).

### Not this (rejected as default)

Intersection-without-a-marker (`--grayscale=0-10 --crop=…:5-15` auto
stacks on 5–10s) would surprise the playlist model. Overlap is **opt-in**.

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
