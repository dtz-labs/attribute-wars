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

echo "ALL HOST TESTS PASSED"
