#!/usr/bin/env python3
"""Convert an image to a ZX Spectrum SCREEN$ file.

The output is the standard 6912-byte layout: 6144 bitmap bytes followed by
768 attribute bytes. The converter chooses one Spectrum ink/paper pair per
8x8 cell, matching the hardware's attribute constraint.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


ZX_BASE = [
    (0, 0, 0),        # black
    (0, 0, 205),      # blue
    (205, 0, 0),      # red
    (205, 0, 205),    # magenta
    (0, 205, 0),      # green
    (0, 205, 205),    # cyan
    (205, 205, 0),    # yellow
    (205, 205, 205),  # white
]
ZX_BRIGHT = [
    (0, 0, 0),
    (0, 0, 255),
    (255, 0, 0),
    (255, 0, 255),
    (0, 255, 0),
    (0, 255, 255),
    (255, 255, 0),
    (255, 255, 255),
]


def spectrum_offset(x: int, y: int) -> int:
    return ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | (x >> 3)


def dist2(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    dr = a[0] - b[0]
    dg = a[1] - b[1]
    db = a[2] - b[2]
    return dr * dr + dg * dg + db * db


def fit_image(img: Image.Image, mode: str) -> Image.Image:
    img = img.convert("RGB")
    w, h = img.size
    target = 256 / 192
    src = w / h

    if mode == "crop":
        if src > target:
            nw = int(h * target)
            left = (w - nw) // 2
            img = img.crop((left, 0, left + nw, h))
        else:
            nh = int(w / target)
            top = (h - nh) // 2
            img = img.crop((0, top, w, top + nh))
        return img.resize((256, 192), Image.Resampling.LANCZOS)

    canvas = Image.new("RGB", (256, 192), (0, 0, 0))
    scale = min(256 / w, 192 / h)
    nw = max(1, int(round(w * scale)))
    nh = max(1, int(round(h * scale)))
    resized = img.resize((nw, nh), Image.Resampling.LANCZOS)
    canvas.paste(resized, ((256 - nw) // 2, (192 - nh) // 2))
    return canvas


def best_cell_pair(pixels: list[tuple[int, int, int]]) -> tuple[int, int, int, list[int]]:
    best_err: int | None = None
    best: tuple[int, int, int, list[int]] | None = None

    for bright, palette in enumerate((ZX_BASE, ZX_BRIGHT)):
        for paper in range(8):
            for ink in range(8):
                if paper == ink:
                    continue
                pcol = palette[paper]
                icol = palette[ink]
                bits: list[int] = []
                err = 0
                for px in pixels:
                    pd = dist2(px, pcol)
                    id_ = dist2(px, icol)
                    if id_ < pd:
                        bits.append(1)
                        err += id_
                    else:
                        bits.append(0)
                        err += pd
                    if best_err is not None and err >= best_err:
                        break
                else:
                    if best_err is None or err < best_err:
                        best_err = err
                        best = (bright, paper, ink, bits)

    assert best is not None
    return best


def convert(img: Image.Image) -> tuple[bytes, Image.Image]:
    bitmap = bytearray(6144)
    attrs = bytearray(768)
    preview = Image.new("RGB", (256, 192))
    src = img.load()
    dst = preview.load()

    for cy in range(24):
        for cx in range(32):
            pixels = [
                src[cx * 8 + px, cy * 8 + py]
                for py in range(8)
                for px in range(8)
            ]
            bright, paper, ink, bits = best_cell_pair(pixels)
            palette = ZX_BRIGHT if bright else ZX_BASE
            attrs[cy * 32 + cx] = (bright << 6) | (paper << 3) | ink

            for py in range(8):
                y = cy * 8 + py
                b = 0
                for px in range(8):
                    x = cx * 8 + px
                    bit = bits[py * 8 + px]
                    if bit:
                        b |= 0x80 >> px
                    dst[x, y] = palette[ink if bit else paper]
                bitmap[spectrum_offset(cx * 8, y)] = b

    return bytes(bitmap + attrs), preview


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--preview", type=Path)
    parser.add_argument("--fit", choices=("crop", "contain"), default="crop")
    args = parser.parse_args()

    img = fit_image(Image.open(args.input), args.fit)
    scr, preview = convert(img)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(scr)
    if args.preview:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        preview.save(args.preview)


if __name__ == "__main__":
    main()
