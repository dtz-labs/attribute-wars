#!/bin/sh
# Timex TC2048 -> beeper SFX (no AY).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"; TAP="$ROOT/build/game.tap"
ZDIR="/Applications/ZEsarUX.app/Contents/MacOS"
[ -f "$TAP" ] || { echo "build first" >&2; exit 1; }
cd "$ZDIR"
exec ./zesarux --noconfigfile --machine TC2048 --enabletimexvideo --joystickemulated Kempston --nosplash --verbose 0 "$TAP"
