# vidwizard — later effects

Not in 0.4. Same CLI shape as the rest when we get to them: optional
`START-END,START-END,…` (including `10-` / `-20`), sequential with other
flags on one command.

## Wanted

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

## Overlapping effects

Right now flags on one command run as a **sequential** graph (crop, then
grayscale, then speed, …). Whole-clip `--crop --grayscale` already
stacks that way.

Still missing: **overlapping time windows** where two ranged effects
apply **together** on the shared interval, e.g. grayscale *and* crop
from 5s–10s while each also has its own range. Later: compose those
intersections instead of last-writer or “one effect per slice.”

## Notes

- Keep defaults high-quality; extra looks are opt-in flags only.
- Prefer libavfilter (`hflip`, `vflip`, `gblur`, `colorchannelmixer` /
  `pseudocolor` for infra) over a second pipeline.
- Infra: not real thermal — a stylized LUT is enough.
