#!/bin/sh
# measure.sh -- build the non-shipped T-state harness (src/measure_main.c) and
# print the marker addresses. Mirrors build.sh's flags; links only the sources
# the harness exercises. Output: build/measure_CODE.bin + build/measure.map.
set -e
export PATH="$HOME/Programowanie/z88dk/bin:$PATH"
export ZCCCFG="$HOME/Programowanie/z88dk/lib/config"
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
mkdir -p build
zcc +zx -SO3 -clib=sdcc_iy -startup=31 -m -iquote"$ROOT/include" \
    src/measure_main.c src/scld.c src/sprite.c src/sprites.c \
    src/enemy.c src/bullet.c src/collision.c src/rng.c src/music.c \
    src/blit.asm src/enemy_update.asm src/collide.asm \
    src/music_ay.asm src/pt3prom.asm src/tune.asm \
    -o build/measure -create-app >/dev/null
echo "marker addresses (.map):"
grep -E '^_mark[0-9A-C]\b' build/measure.map | awk '{printf "  %-8s %s\n",$1,$3}'
