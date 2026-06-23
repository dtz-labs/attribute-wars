#!/bin/sh
# build-zx128.sh -- compile the ZX Spectrum 128K shadow-screen build.
set -e

if [ -d "$HOME/Programowanie/z88dk/bin" ]; then
    export PATH="$HOME/Programowanie/z88dk/bin:$PATH"
    export ZCCCFG="$HOME/Programowanie/z88dk/lib/config"
fi

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
mkdir -p build

zcc +zx -SO3 -clib=sdcc_iy -startup=31 \
    -DZX128_PAGE_FLIP -DZX_SINCLAIR_DUAL_STICK -DZX128_NO_MUSIC \
    -Ca-DZX128_PAGE_FLIP \
    -pragma-define:REGISTER_SP=49152 \
    -iquote"$ROOT/include" \
    src/main.c src/scld.c src/sprite.c src/sprites.c src/player.c \
    src/bullet.c src/enemy.c src/collision.c src/geometry.c src/input.c \
    src/rng.c src/score.c src/sfx.c src/hud.c src/music.c \
    src/blit.asm src/enemy_update.asm src/collide.asm src/sfx.asm \
    -o build/game-zx128 -create-app -m

tools/check_zx128_layout.py build/game-zx128.map

if [ -f assets/loading.scr ]; then
    z88dk-appmake +zx --binfile build/game-zx128_CODE.bin --org 32768 \
        --output build/game-zx128.tap --screen assets/loading.scr \
        --clearaddr 32767 --usraddr 32768
fi

ls -l build/game-zx128.tap
