#!/bin/sh
# ZX Spectrum 128K -> shadow-screen page flipping, Sinclair 1/2 twin-stick.
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
TAP="$ROOT/build/game-zx128.tap"
ZDIR="/Applications/ZEsarUX.app/Contents/MacOS"

[ -f "$TAP" ] || { echo "build first: ./build-zx128.sh" >&2; exit 1; }
[ -x "$ZDIR/zesarux" ] || { echo "ZEsarUX binary not found at $ZDIR/zesarux" >&2; exit 1; }

cd "$ZDIR"
exec ./zesarux --noconfigfile --machine 128k --joystickemulated Kempston \
    --nosplash --verbose 0 "$TAP"
