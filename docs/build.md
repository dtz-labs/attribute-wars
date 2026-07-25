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
make timex        # build Timex TC2048/TC2068 TAP -> build/aw-*-timex.tap
make zx128        # build ZX Spectrum 128K TAP -> build/aw-*-zx128k.tap
make zx48         # build ZX Spectrum 48K TAP -> build/aw-*-zx48k.tap
make TARGET=zx128 # same as make zx128
```

## Run In ZEsarUX

```sh
make run-tc2048   # Timex TC2048
make run-tc2068   # Timex TC2068 / TS2068 AY path
make run-zx128    # ZX Spectrum 128K / +2-style build
make run-zx48     # ZX Spectrum 48K build
```

### Emulator Configuration

The `run*` targets start ZEsarUX with `--configfile tools/zesarux.rc` instead of
the global `~/.zesaruxrc`, so runs are reproducible and never overwrite your own
settings. That file holds the emulated Kempston joystick plus the host
joystick/pad mapping, and the emulator is told not to save it on exit. The
startup splash and welcome logo are suppressed.

Remap the pad in a normal ZEsarUX session (which writes `~/.zesaruxrc`), then
run `make zesarux-config` to copy the mapping back into `tools/zesarux.rc`.
Extra one-off flags can still be passed via `ZESARUX_FLAGS`, for example
`make run-zx128 ZESARUX_FLAGS="--zoom 3"`.

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

- `build/aw-*-zx48k.tap` - ZX Spectrum 48K build
- `build/aw-*-zx128k.tap` - ZX Spectrum 128K / +2 build
- `build/aw-*-timex.tap` - Timex TC2048/TC2068/TS2068 build

All TAP files include the loading screen generated from `assets/loading.png`.
