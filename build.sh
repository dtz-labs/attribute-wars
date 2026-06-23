#!/bin/sh
# build.sh -- compile the Timex TC2048 twin-stick shooter to build/game.tap.
#
# Flags fixed empirically in M1 (see docs spec §10): +zx target (TC2048 runs
# Spectrum software; SCLD reached via OUT 0xFF), sdcc_iy clib, default ORG
# 0x8000 (no -zorg). The asm blitter (src/blit.asm) is assembled alongside the
# C sources. -iquote<abs>/include keeps our headers off the system path so they
# don't shadow z88dk's <input.h> etc.
set -e

# Local dev points PATH/ZCCCFG at the user's z88dk checkout. In CI we build
# inside the official z88dk Docker image, where zcc is already on PATH and
# ZCCCFG is preset -- so only set these when the local checkout is present.
if [ -d "$HOME/Programowanie/z88dk/bin" ]; then
    export PATH="$HOME/Programowanie/z88dk/bin:$PATH"
    export ZCCCFG="$HOME/Programowanie/z88dk/lib/config"
fi

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
mkdir -p build

zcc +zx -SO3 -clib=sdcc_iy -startup=31 -iquote"$ROOT/include" \
    src/main.c src/scld.c src/sprite.c src/sprites.c src/player.c \
    src/bullet.c src/enemy.c src/collision.c src/geometry.c src/input.c \
    src/rng.c src/score.c src/sfx.c src/hud.c src/music.c \
    src/blit.asm src/enemy_update.asm src/collide.asm src/sfx.asm \
    src/music_ay.asm src/pt3prom.asm src/tune.asm \
    -o build/game -create-app

if [ -f assets/loading.scr ]; then
    z88dk-appmake +zx --binfile build/game_CODE.bin --org 32768 \
        --output build/game.tap --screen assets/loading.scr \
        --clearaddr 32767 --usraddr 32768
fi

ls -l build/game.tap
