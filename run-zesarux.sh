#!/bin/sh
# run-zesarux.sh -- launch the game on ZEsarUX as a Timex TC2048.
#
# Usage:  ./run-zesarux.sh        (build build/game.tap first)
#
# Flags chosen during M1 bring-up:
#   --machine TC2048        the actual Timex Computer 2048 model
#   --enabletimexvideo      enable the SCLD Timex video modes (page-flip)
#   --joystickemulated Kempston  drive the Kempston port (idle = 0x00) so it
#                           doesn't float and jam movement; lets a real pad work
#   --nosplash              disable splash texts -- THIS kills the constant
#                           "Setting Timex Video Mode" overlay our page-flip
#                           triggers every frame (--verbose alone does not)
#   --verbose 0             quieter console logging
#   --noconfigfile          ignore the user's saved config for a clean launch
#
# Controls: move 5/6/7/8 (cursor) or a Kempston pad; fire 0 or Q W E/A D/Z X C.
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
TAP="$ROOT/build/game.tap"
ZDIR="/Applications/ZEsarUX.app/Contents/MacOS"

[ -f "$TAP" ] || { echo "Tap not found: $TAP  (build it first)" >&2; exit 1; }
[ -x "$ZDIR/zesarux" ] || { echo "ZEsarUX binary not found at $ZDIR/zesarux" >&2; exit 1; }

# ZEsarUX must run from its own dir so it locates its bundled ROMs; the tape
# path is absolute, so it survives the cd.
cd "$ZDIR"
exec ./zesarux \
    --noconfigfile \
    --machine TC2048 \
    --enabletimexvideo \
    --joystickemulated Kempston \
    --nosplash \
    --verbose 0 \
    "$TAP"
