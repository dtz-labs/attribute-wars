#!/bin/sh
# build-zx48.sh -- compile the single-buffer ZX Spectrum 48K build.
set -e

if [ -d "$HOME/Programowanie/z88dk/bin" ]; then
    export PATH="$HOME/Programowanie/z88dk/bin:$PATH"
    export ZCCCFG="$HOME/Programowanie/z88dk/lib/config"
fi

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
mkdir -p build

zcc +zx -SO3 -clib=sdcc_iy -startup=31 \
    -DZX48_SINGLE_BUFFER -DZX_SINCLAIR_DUAL_STICK \
    -iquote"$ROOT/include" \
    src/main.c src/scld.c src/sprite.c src/sprites.c src/player.c \
    src/bullet.c src/enemy.c src/collision.c src/geometry.c src/input.c \
    src/rng.c src/score.c src/sfx.c src/hud.c src/music.c \
    src/blit.asm src/enemy_update.asm src/collide.asm src/sfx.asm \
    src/music_ay.asm src/pt3prom.asm src/tune.asm \
    -o build/game-zx48 -create-app

if [ -f assets/loading.scr ]; then
    z88dk-appmake +zx --binfile build/game-zx48_CODE.bin --org 32768 \
        --output build/game-zx48.tap --screen assets/loading.scr \
        --clearaddr 32767 --usraddr 32768
fi

ls -l build/game-zx48.tap
