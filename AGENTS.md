# vidwizard

C++23 command-line video editor using FFmpeg 8 libraries (`libav*`).

## Build

```text
make -s V=0 -j$(nproc)          # debug + ASan/UBSan (default)
make -s test                    # Catch2 + fixtures (alias: make tests)
make -s verify                  # CBMC on parse_time.c (after test)
make -s V=0 -j$(nproc) release  # bin/release/vidwizard
make static                     # fully static musl binary via /mnt/alpine
```

Compiler is **g++/gcc** only (`gnu++23` / `gnu23`).

## CLI rules

Follow workstation `cli-design`: no-args → usage, `-h`/`--help`, `-v`/`--version`
(not verbose), order-independent operands, `-o` one target, directory inputs
expand without `--batch`. Default `--jobs` is all logical cores.

## Layout

| Path | Role |
|------|------|
| `include/vidwizard/` | Public headers (Doxygen) |
| `src/` | Implementation |
| `tests/` | Catch2 unit + integration |
| `formal/` | CBMC harness |
| `man/vidwizard.1` | Manual page |
