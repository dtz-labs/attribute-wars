#!/bin/sh
# Timex TC2068 -> AY music+SFX at 0xF5/0xF6 (ROM-detected).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"; TAP="$ROOT/build/game.tap"
ZDIR="/Applications/ZEsarUX.app/Contents/MacOS"
[ -f "$TAP" ] || { echo "build first" >&2; exit 1; }
cd "$ZDIR"
exec ./zesarux --noconfigfile --machine TC2068 --enabletimexvideo --joystickemulated Kempston --nosplash --verbose 0 "$TAP"
