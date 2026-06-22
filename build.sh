#!/bin/sh
# build.sh -- compile the Timex TC2048 twin-stick shooter to build/game.tap.
#
# Flags fixed empirically in M1 (see docs spec §10): +zx target (TC2048 runs
# Spectrum software; SCLD reached via OUT 0xFF), sdcc_iy clib, default ORG
# 0x8000 (no -zorg). The asm blitter (src/blit.asm) is assembled alongside the
# C sources. -iquote<abs>/include keeps our headers off the system path so they
# don't shadow z88dk's <input.h> etc.
set -e

export PATH="$HOME/Programowanie/z88dk/bin:$PATH"
export ZCCCFG="$HOME/Programowanie/z88dk/lib/config"

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
mkdir -p build

zcc +zx -SO3 -clib=sdcc_iy -iquote"$ROOT/include" \
    src/main.c src/scld.c src/sprite.c src/sprites.c src/player.c \
    src/bullet.c src/enemy.c src/collision.c src/geometry.c src/input.c \
    src/rng.c src/score.c src/blit.asm src/enemy_update.asm src/collide.asm \
    -o build/game -create-app

ls -l build/game.tap
