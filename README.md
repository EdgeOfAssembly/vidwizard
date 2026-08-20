# vidwizard

<img src="icons/vidwizard.svg" alt="vidwizard" width="128" height="128" align="right">

High-quality command-line video edits, no GUI.

[![version](https://img.shields.io/badge/version-0.4.0--alpha-orange)](https://github.com/EdgeOfAssembly/vidwizard/releases)
[![platform](https://img.shields.io/badge/platform-linux--x86__64-blue)](https://github.com/EdgeOfAssembly/vidwizard)
[![license](https://img.shields.io/badge/license-GPLv3%20%2F%20Commercial-blue)](#license)

> **Alpha (0.4.0-alpha).** Public preview.

Name a file, name the effect, get a result. Quality and every logical
CPU core are the defaults. Several flags on one command run as **one
sequential graph** (crop, then grayscale, then speed, …). Ranges are
optional.

As of **0.4.0-alpha**, vidwizard can:

- Convert to **grayscale**
- **Explode** a clip into lossless PNG frames
- **Cut** one or more time windows into their own files
- **Speed up** (audio stays in pitch)
- **Crop** (whole clip, or a window with the rest black)
- **Reverse**
- **Mute** or silence time windows
- **Zoom** in and out (fill-frame)
- Overlay **text** labels

## Quick start

Sample output: [`examples/catwalk-demo.mp4`](examples/catwalk-demo.mp4)
(zoom in/out, grayscale, a real face crop, speed, reverse, labeled).

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

`vidwizard` with no arguments (or `-h`) prints usage. `-v` prints the
version. `--verbose` writes extra progress to stderr. Options and input
paths may appear in any order. `-o` is a file or a directory. Pass a
directory of videos to process every clip in it.

## Install

### From a GitHub release

[Releases](https://github.com/EdgeOfAssembly/vidwizard/releases) ship:

- a **statically linked** x86_64 Linux binary (no FFmpeg install on the
  target — unpack `vidwizard` onto your `PATH`)
- a source tarball (build as below)

### From source

Needs **g++/gcc** (C++23), **FFmpeg 8** development libraries, and
pkg-config.

```text
git clone https://github.com/EdgeOfAssembly/vidwizard.git
cd vidwizard
make -s V=0 -j$(nproc)
make -s test
sudo make install          # /usr/local/bin/vidwizard and man 1
```

Optimized binary:

```text
make -s V=0 -j$(nproc) release   # bin/release/vidwizard
```

## Flags

| Flag | What |
|------|------|
| `-h`, `--help` | Show usage |
| `-v`, `--version` | Show version |
| `-o`, `--output PATH` | Output file or directory |
| `--grayscale[=RANGES]` | Grayscale → `clip_gray.mp4` |
| `--explode[=RANGES]` | Lossless PNG frames → `clip_01.png` … |
| `--cut RANGES` | Each window as its own video → `clip_cut_01.mp4` |
| `--speed FACTOR[:RANGES]` | Forward speed; audio stays in pitch → `clip_speed.mp4` |
| `--crop GEOM[:RANGES]` | Crop `WxH+X+Y` or `W:H:X:Y` (`WxH` = centred) → `clip_crop.mp4` |
| `--reverse[=RANGES]` | Reverse whole clip or windows in place → `clip_rev.mp4` |
| `--mute[=RANGES]` | Drop all audio, or silence windows → `clip_mute.mp4` |
| `--zoom SPEC` | Fill-frame zoom → `clip_zoom.mp4` |
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

One input plus `-o file` uses that exact name. Several inputs plus
`-o out.mp4` write `out_<input>.mp4`. `-o` as a directory (created if
missing) receives default-named outputs.

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

**`--crop`** keeps a pixel rectangle. `640x360` (or `640:360`) is
centred; `280x200+180+8` is width × height + left + top. A whole-clip
crop changes the output resolution. A ranged crop keeps that rectangle
and pads the rest of the frame black (it is not a zoom).

**`--zoom`** fills the frame. Output size stays the source size. Spec:
`Z0[-Z1][@CX,CY][:RANGE]` with segments split by `;`.
`Z` is 1..8 (1 = no zoom). `CX,CY` are 0..1 (default 0.5, 0.5).
`Z0-Z1` animates over the range; a single `Z` holds that factor.

```text
vidwizard clip.mp4 --crop 640x360
vidwizard clip.mp4 --crop 280x200+180+8:7.5-9.2
vidwizard clip.mp4 --zoom '1-2.35@0.50,0.40:2.0-4.0;2.35-1@0.50,0.40:4.0-5.5'
```

## Text overlays

`--text` is repeatable. Spec is `TEXT[:RANGE][+X+Y]`. Default origin is
`0,0` (top-left), bold white, transparent background. `--text-font`,
`--text-style`, `--text-size`, `--text-color`, and `--text-bg` apply to
every overlay. `--text-bg 000000c0` is a translucent black box.

```text
vidwizard clip.mp4 \
  --text 'Zoom in:2-4' \
  --text 'Crop:7.5-9.2+196+16' \
  --text-bg 000000c0 \
  --text-size 32
```

`--reverse` holds the selected frames in RAM (short clips are fine; a
long 4K reverse can use a lot of memory).

## Build

```text
make -s V=0 -j$(nproc)
make -s test
make -s V=0 -j$(nproc) release   # bin/release/vidwizard
```

Manual page: `man vidwizard` after `make install`, or `man/vidwizard.1`.

Later ideas (mirror, infravision, blur, …): [`TODO.md`](TODO.md).

## License

Dual-licensed:

- **GPLv3** (or later) — see [`LICENSE`](LICENSE) and [`COPYING`](COPYING)
- **Commercial** — contact EdgeOfAssembly at haxbox2000@gmail.com
