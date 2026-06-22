#!/bin/sh
# Build and run the host (native) unit tests for the pure-logic modules.
# These compile with the macOS system compiler -- no Z80 toolchain, no
# emulator -- so red/green/refactor is instant. Hardware-touching code
# (video/SCLD, input ports) is verified separately in Fuse / z88dk-ticks.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-cc}"
CFLAGS="-std=c99 -Wall -Wextra -Werror -I$ROOT/include"
OUT="$ROOT/build/host"
mkdir -p "$OUT"

# One executable per test_*.c, linked against the matching pure-logic sources.
$CC $CFLAGS "$ROOT/test/test_geometry.c" "$ROOT/src/geometry.c" -o "$OUT/test_geometry"
"$OUT/test_geometry"

$CC $CFLAGS "$ROOT/test/test_input.c" "$ROOT/src/input.c" "$ROOT/src/geometry.c" -o "$OUT/test_input"
"$OUT/test_input"

$CC $CFLAGS "$ROOT/test/test_player.c" "$ROOT/src/player.c" "$ROOT/src/geometry.c" "$ROOT/src/input.c" -o "$OUT/test_player"
"$OUT/test_player"

$CC $CFLAGS "$ROOT/test/test_bullet.c" "$ROOT/src/bullet.c" -o "$OUT/test_bullet"
"$OUT/test_bullet"

# scld_scanline is a pure inline in the header (the hardware lives in scld.c,
# which is target-only), so this test needs no .c source linked.
$CC $CFLAGS "$ROOT/test/test_scld.c" -o "$OUT/test_scld"
"$OUT/test_scld"

$CC $CFLAGS "$ROOT/test/test_collision.c" "$ROOT/src/collision.c" -o "$OUT/test_collision"
"$OUT/test_collision"

$CC $CFLAGS "$ROOT/test/test_rng.c" "$ROOT/src/rng.c" -o "$OUT/test_rng"
"$OUT/test_rng"

$CC $CFLAGS "$ROOT/test/test_enemy.c" "$ROOT/src/enemy.c" "$ROOT/src/rng.c" -o "$OUT/test_enemy"
"$OUT/test_enemy"

$CC $CFLAGS "$ROOT/test/test_score.c" "$ROOT/src/score.c" -o "$OUT/test_score"
"$OUT/test_score"

$CC $CFLAGS "$ROOT/test/test_bgpat.c" "$ROOT/src/bgpat.c" -o "$OUT/test_bgpat"
"$OUT/test_bgpat"

$CC $CFLAGS "$ROOT/test/test_fxtab.c" "$ROOT/src/fxtab.c" -o "$OUT/test_fxtab"
"$OUT/test_fxtab"

echo "ALL HOST TESTS PASSED"
