#!/bin/sh
# run-zesarux-ay.sh -- launch the game on ZEsarUX as a Timex TC2068 to HEAR the
# AY music. The TC2068 has the SCLD (so the page-flip video works) AND a native
# AY-3-8912; the game's runtime detection finds it by ROM signature (the 2068
# HOME ROM has "Timex" at 0x113D) and drives the AY at 0xF5/0xF6. The plain
# ./run-zesarux.sh runs the beeper-only TC2048, where music is correctly silent.
#
# Notes:
#   - This exercises the full TC2068 path: ROM-signature detection -> 0xF6 AY.
#   - The TC2068 HOME ROM differs from the Spectrum-compatible TC2048 ROM, so the
#     8x8 text font may render differently (a separate concern from the music).
#     If you want guaranteed-correct video while still hearing music, use a ZX
#     128K instead (`--machine 128k`): the title screen renders and the AY at the
#     standard 0xFFFD is detected, but in-game page-flip video is absent there.
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
TAP="$ROOT/build/game.tap"
ZDIR="/Applications/ZEsarUX.app/Contents/MacOS"

[ -f "$TAP" ] || { echo "Tap not found: $TAP  (build it first)" >&2; exit 1; }
[ -x "$ZDIR/zesarux" ] || { echo "ZEsarUX binary not found at $ZDIR/zesarux" >&2; exit 1; }

cd "$ZDIR"
exec ./zesarux \
    --noconfigfile \
    --machine TC2068 \
    --enabletimexvideo \
    --joystickemulated Kempston \
    --nosplash \
    --verbose 0 \
    "$TAP"
