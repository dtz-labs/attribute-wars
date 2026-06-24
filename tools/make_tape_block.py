#!/usr/bin/env python3
"""Wrap a binary file into a single headerless ZX tape data block (flag $FF).

Output (to stdout): [len_lo][len_hi][flag=$FF][data...][checksum], where
len = len(data) + 2 and checksum = XOR over flag and all data bytes. This is
exactly what the ROM LD-BYTES routine ($0556) reads with A=$FF, DE=len(data).

Used by the zx128 build to append the PT3 tune as a trailing tape block that
the running program loads into RAM bank 4 at $C000 (see src/zx128_page.asm
zx128_load_tune).
"""
import sys


def make_block(data: bytes) -> bytes:
    flag = 0xFF
    body = bytes([flag]) + data
    checksum = 0
    for b in body:
        checksum ^= b
    block = body + bytes([checksum])
    length = len(block)  # = len(data) + 2
    return bytes([length & 0xFF, (length >> 8) & 0xFF]) + block


def main(argv: list) -> int:
    if len(argv) != 2:
        sys.stderr.write(f"usage: {argv[0]} <input.bin>\n")
        return 2
    with open(argv[1], "rb") as f:
        data = f.read()
    sys.stdout.buffer.write(make_block(data))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
