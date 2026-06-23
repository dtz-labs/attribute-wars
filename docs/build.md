# Build And Run

Attribute Wars is built with z88dk using the `+zx` target, `sdcc_iy` C library,
C sources, and hand-written Z80 assembly.

## Requirements

- z88dk installed at `~/Programowanie/z88dk`, or available on `PATH`
- ZEsarUX for emulator runs on macOS, expected at:
  `/Applications/ZEsarUX.app/Contents/MacOS/zesarux`

## Build Targets

```sh
make              # show available targets
make all          # build every platform TAP in parallel
make timex        # build Timex TC2048/TC2068 TAP -> build/game.tap
make zx128        # build ZX Spectrum 128K TAP -> build/game-zx128.tap
make zx48         # build ZX Spectrum 48K TAP -> build/game-zx48.tap
make TARGET=zx128 # same as make zx128
```

## Run In ZEsarUX

```sh
make run-tc2048   # Timex TC2048
make run-tc2068   # Timex TC2068 / TS2068 AY path
make run-zx128    # ZX Spectrum 128K / +2-style build
make run-zx48     # ZX Spectrum 48K build
```

## Host Tests

```sh
make test
```

This runs the pure-logic host tests with the system C compiler. No emulator or
Z80 toolchain is required for these tests.

## Measurement Harness

```sh
make measure
```

This builds the non-shipped `src/measure_main.c` T-state harness and prints the
marker addresses from `build/measure.map` for `z88dk-ticks`.

## Artifacts

- `build/game.tap` - Timex TC2048/TC2068/TS2068 build
- `build/game-zx128.tap` - ZX Spectrum 128K / +2 build
- `build/game-zx48.tap` - ZX Spectrum 48K build

All TAP files include the loading screen generated from `assets/loading.png`.
