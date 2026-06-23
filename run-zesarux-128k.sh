#!/bin/sh
# run-zesarux-128k.sh -- ZX Spectrum 128K: native AY at the standard 0xFFFD/0xBFFD
# (the game's scheme 1). Title screen renders + music plays; in-game page-flip
# video is absent (128K has no SCLD) -- a music/detection check, not a video one.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"; TAP="$ROOT/build/game.tap"
ZDIR="/Applications/ZEsarUX.app/Contents/MacOS"
[ -f "$TAP" ] || { echo "build first" >&2; exit 1; }
cd "$ZDIR"
exec ./zesarux --noconfigfile --machine 128k --joystickemulated Kempston \
    --nosplash --verbose 0 "$TAP"
