# vidwizard

<img src="icons/vidwizard.svg" alt="vidwizard" width="128" height="128" align="right">

**ImageMagick `convert` for video** — high-quality command-line edits, no GUI.

[![version](https://img.shields.io/badge/version-0.4.0--alpha-orange)](https://github.com/EdgeOfAssembly/vidwizard/releases)
[![platform](https://img.shields.io/badge/platform-linux--x86__64-blue)](https://github.com/EdgeOfAssembly/vidwizard)
[![license](https://img.shields.io/badge/license-use%20as%20you%20wish-lightgrey)](#license)

> **Alpha (0.4.0-alpha).** Public preview. Flags are intended to stay
> stable; treat encoder details and edge cases as still moving.

Name a file, name the effect, get a result. Quality and every logical
CPU core are the defaults. Several flags on one command run as **one
sequential graph** (crop, then grayscale, then speed, …). Ranges are
optional.

## Quick start

```text
vidwizard                  # usage (same as -h / --help)
vidwizard -v               # version (never verbose)
vidwizard clip.mp4 --grayscale
```

`-v` / `--version` print the version. Extra progress is `--verbose` on
stderr. Options and inputs may appear in any order. `-o` is one target
(file or directory). Directory inputs expand to common video types;
there is no `--batch` flag.

## Install

### From a GitHub release

[Releases](https://github.com/EdgeOfAssembly/vidwizard/releases) ship:

- a **statically linked** x86_64 Linux binary (no FFmpeg install on the
  target — unpack `vidwizard` onto your `PATH`)
- a source tarball (build as below)

### From source

Needs **g++/gcc** (gnu++23 / gnu23), **FFmpeg 8** development libraries
(`libavfilter`, `libavformat`, `libavcodec`, `libavutil`, `libswscale`,
`libswresample`), and pkg-config. Catch2 is required for `make test`.

```text
git clone https://github.com/EdgeOfAssembly/vidwizard.git
cd vidwizard
make -s V=0 -j$(nproc)
make -s test
sudo make install          # /usr/local/bin/vidwizard and man 1
```

Default `make` is debug + ASan/UBSan. For an optimized binary:

```text
make -s BUILD=release all  # bin/release/vidwizard
```

## Flags

| Flag | What |
|------|------|
| `-h`, `--help` | Usage and exit (also printed with no arguments) |
| `-v`, `--version` | Version and exit — **never** verbose |
| `-o`, `--output PATH` | Single output file or directory (once only) |
| `--grayscale[=RANGES]` | Rec.709 grayscale → `clip_gray.mp4` |
| `--explode[=RANGES]` | Lossless 32-bit RGBA PNG, max zlib → `clip_01.png` … |
| `--cut RANGES` | Each window as its own video → `clip_cut_01.mp4` |
| `--speed FACTOR[:RANGES]` | Forward speed; audio keeps pitch (`atempo`) → `clip_speed.mp4` |
| `--crop GEOM[:RANGES]` | Crop `WxH+X+Y` or `W:H:X:Y` (`WxH` = centred) → `clip_crop.mp4` |
| `--reverse[=RANGES]` | Reverse whole clip or windows in place → `clip_rev.mp4` |
| `--mute[=RANGES]` | Drop all audio, or silence windows → `clip_mute.mp4` |
| `--zoom SPEC` | Ken Burns fill-frame zoom → `clip_zoom.mp4` |
| `--text SPEC` | Overlay, repeatable: `TEXT[:RANGE][+X+Y]` → `clip_text.mp4` |
| `--text-font PATH` | Font file (default DejaVu Sans Bold) |
| `--text-style bold\|regular` | Default `bold` |
| `--text-size N` | Pixel size, 8..512 (default 28) |
| `--text-color RRGGBB[AA]` | Fill (default `FFFFFF`) |
| `--text-bg RRGGBB[AA]` | Solid box; omit for transparent |
| `--jobs N` | Threads (default: all logical cores) |
| `--verbose` | Extra progress on stderr |
| `--log-file PATH` | Also write diagnostics to `PATH` |

Combining operations writes `clip_edit.mp4`. `--speed` keeps
pitch-preserving audio; add `--mute` only if you want the stream
dropped.

`--cut` requires at least one `START-END` range. PNG index width matches
the frame count (20 frames → `01`..`20`; 1000 frames → `0001`..`1000`).

One input plus `-o file` uses that exact name. Several inputs plus a
file path warn and write `outstem_instem.ext`. A directory `-o` (created
if missing) receives default-named outputs.

## Time ranges

`START-END,START-END,…` with seconds, `MM:SS`, or `HH:MM:SS` (fractions
allowed).

| Form | Meaning |
|------|---------|
| `10-20` | From 10s up to 20s |
| `1:00-1:30` | One minute to one minute thirty |
| `10-` / `5:00-` | From that time **to EOF** |
| `-20` | From the start up to 20s |
| *(omit)* | Whole clip |

```text
vidwizard clip.mp4 --grayscale 5:00-
vidwizard clip.mp4 --cut 10-
```

## Crop versus zoom

This distinction matters.

**`--crop`** keeps a pixel rectangle. `640x360` (or `640:360`) is
centred; `280x200+180+8` is an ImageMagick-style origin. A **whole-clip**
crop changes the output resolution. A **ranged** crop keeps that
rectangle and **pads the rest of the frame black** — it is **not** a
zoom. Output size stays the source size.

**`--zoom`** is a Ken Burns **fill-frame** zoom (`zoompan`). The output
stays the source resolution; the picture scales around a normalized
center. Spec: `Z0[-Z1][@CX,CY][:RANGE]` with segments split by `;`.
`Z` is 1..8 (1 = no zoom). `CX,CY` are 0..1 (default 0.5,0.5).
`Z0-Z1` animates over the range; a single `Z` holds that factor.

```text
vidwizard clip.mp4 --crop 640x360
vidwizard clip.mp4 --crop 280x200+180+8:7.5-9.2
vidwizard clip.mp4 --zoom '1-2.35@0.50,0.40:2.0-4.0;2.35-1@0.50,0.40:4.0-5.5'
```

## Text overlays

`--text` is repeatable. Spec is `TEXT[:RANGE][+X+Y]`. Range and position
are parsed from the right so the label may contain colons. Default
origin is `0,0` (top-left), bold white, transparent background.
`--text-font`, `--text-style`, `--text-size`, `--text-color`, and
`--text-bg` apply to every overlay on the command. `--text-bg 000000c0`
is a translucent black box (8 hex digits = alpha).

```text
vidwizard clip.mp4 \
  --text 'Zoom in:2-4' \
  --text 'Crop:7.5-9.2+196+16' \
  --text-bg 000000c0 \
  --text-size 32
```

## Catwalk demo

Sample output: [`examples/catwalk-demo.mp4`](examples/catwalk-demo.mp4).

```text
vidwizard catwalk.mp4 -o cat_demo.mp4 \
  --zoom='1-2.35@0.50,0.40:2.0-4.0;2.35-1@0.50,0.40:4.0-5.5' \
  --grayscale=5.5-7.5 \
  --crop=280x200+180+8:7.5-9.2 \
  --speed=2.2:14-16 \
  --reverse=16- \
  --text 'Zoom in:2-4' \
  --text 'Zoom out:4-5.5' \
  --text 'Grayscale:5.5-7.5' \
  --text 'Crop:7.5-9.2+196+16' \
  --text 'Speed:14-16' \
  --text 'Reverse:16-' \
  --text-bg 000000c0 \
  --text-size 32
```

`--reverse` holds the selected frames in RAM (short clips are fine; a
long 4K reverse can use a lot of memory).

## Build

```text
make -s V=0 -j$(nproc)
make -s test
```

`make test` (alias `make tests`) runs Catch2 plus generated fixtures.
`make verify` runs CBMC on the time-range parser after tests.
`make -s BUILD=release all` builds `bin/release/vidwizard`.

Optional network smoke (yt-dlp):

```text
scripts/fetch-test-clip.sh
./vidwizard testdata/net/netclip.* --grayscale -o testdata/net/gray.mp4
```

Manual page: `man/vidwizard.1` (`man vidwizard` after `make install`).

Later ideas (mirror, infravision, blur, …): [`TODO.md`](TODO.md).

## License

Use as you wish. Public-domain / CC0-style — no paperwork.
