#!/bin/sh
# ZX Spectrum 48K -> single-buffer, visibly flickery graphics, beeper default.
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
TAP="$ROOT/build/game-zx48.tap"
ZDIR="/Applications/ZEsarUX.app/Contents/MacOS"

[ -f "$TAP" ] || { echo "build first: ./build-zx48.sh" >&2; exit 1; }
[ -x "$ZDIR/zesarux" ] || { echo "ZEsarUX binary not found at $ZDIR/zesarux" >&2; exit 1; }

cd "$ZDIR"
exec ./zesarux --noconfigfile --machine 48k --joystickemulated Kempston \
    --nosplash --verbose 0 "$TAP"
