#!/bin/sh
# run-zesarux-ay.sh -- launch the game on ZEsarUX as a Timex TC2068, the Timex
# machine that has BOTH the SCLD (so the page-flip video works) AND an AY-3-8912
# (so the chiptune music plays). Use this to hear/verify the AY music; the plain
# ./run-zesarux.sh runs the beeper-only TC2048, where the music is correctly
# silent.
#
# The TC2068's AY answers on ports 0x00F5/0x00F6 -- this exercises the TS2068
# port scheme in src/music_ay.asm's runtime detection. (The 128K scheme
# 0xFFFD/0xBFFD is what you get if you instead enable an AY on the TC2048 model
# via ZEsarUX's Audio menu -- a useful second check, but that needs the GUI.)
#
# NOTE: the TC2068 uses the Timex ROM, not the Spectrum-compatible TC2048 ROM.
# If the game does not boot here, fall back to: ./run-zesarux.sh with the AY
# enabled in ZEsarUX (Settings -> Audio -> AY chip), which keeps the SCLD video
# and adds an AY on the 128K port scheme.
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
