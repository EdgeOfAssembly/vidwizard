# vidwizard

Command-line video editor for x86_64 Linux. High-quality defaults, no GUI.

```text
vidwizard clip.mp4 --grayscale
vidwizard clip.mp4 --explode
vidwizard clip.mp4 --cut 10-20,1:00-1:30
vidwizard clip.mp4 --speed 2.75
vidwizard clip.mp4 --crop 640x360
vidwizard clip.mp4 --reverse
vidwizard clip.mp4 --mute
```

No arguments prints usage (same as `-h`). Version is `-v` / `--version` (0.3).
`--verbose` is extra stderr, never `-v`.

Several flags on one command run **in one sequential graph** (crop, then
grayscale, then speed, …). Ranges are optional.

## What it does

| Flag | Default output |
|------|----------------|
| `--grayscale[=RANGES]` | `clip_gray.mp4` Rec.709 luma |
| `--explode[=RANGES]` | `clip_01.png` … lossless 32-bit RGBA, max zlib |
| `--cut RANGES` | `clip_cut_01.mp4` per window |
| `--speed FACTOR[:RANGES]` | `clip_speed.mp4` |
| `--crop GEOM[:RANGES]` | `clip_crop.mp4` (`WxH+X+Y` or `W:H:X:Y`) |
| `--reverse[=RANGES]` | `clip_rev.mp4` |
| `--mute[=RANGES]` | `clip_mute.mp4` (drop audio, or silence windows) |
| `--zoom SPEC` | `clip_zoom.mp4` — `Z0[-Z1][@CX,CY][:RANGE];…` |

Combine operations (no ranges required):

```text
vidwizard clip.mp4 --zoom 1-2.2@0.5,0.4:2-4;2.2-1:4-6 --grayscale 6-8
vidwizard clip.mp4 --crop 640x360 --grayscale --speed 2
```

writes `clip_edit.mp4`. `--speed` keeps pitch-preserving audio; add `--mute`
only if you want the stream dropped.

Time ranges: `START-END,START-END,…` with seconds, `MM:SS`, or `HH:MM:SS`.
Open end: `10-` or `5:00-` (to EOF). Open start: `-20` (from 0).

```text
vidwizard clip.mp4 --grayscale 5:00-
vidwizard clip.mp4 --cut 10-
```

Threads: every logical core unless `--jobs N`.

`--reverse` holds the selected frames in RAM (short clips are fine; a long
4K reverse can use a lot of memory).

## Build

Needs FFmpeg 8 development libraries (`libavfilter` and friends), g++ with
C++23, and pkg-config.

```text
make -s V=0 -j$(nproc)
make -s test
sudo make install    # /usr/local/bin/vidwizard
```

Optional network smoke (yt-dlp):

```text
scripts/fetch-test-clip.sh
./vidwizard testdata/net/netclip.* --grayscale -o testdata/net/gray.mp4
```

## License

Use as you wish in this workspace.
