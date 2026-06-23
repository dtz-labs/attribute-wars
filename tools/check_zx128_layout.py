#!/usr/bin/env python3
"""Check whether a z88dk +zx map is safe for ZX Spectrum 128K page flipping.

The 128K shadow screen maps RAM page 7 into 0xC000-0xFFFF while the game draws
to the hidden buffer. Therefore no resident code, data, BSS, interrupt state, or
stack may live in that window. This script is intentionally conservative: it is
a gate to run before enabling a 0x7FFD shadow-screen kernel.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


PAGING_WINDOW = 0xC000
REQUIRED = (
    "__CODE_END_tail",
    "__DATA_END_tail",
    "__BSS_END_tail",
    "__register_sp",
)


def parse_symbols(path: Path) -> dict[str, int]:
    symbols: dict[str, int] = {}
    pattern = re.compile(r"^([A-Za-z0-9_]+)\s*=\s*\$([0-9A-Fa-f]+)\b")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            symbols[match.group(1)] = int(match.group(2), 16)
    return symbols


def fmt(value: int) -> str:
    return f"${value:04X}"


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"usage: {Path(argv[0]).name} build/game.map", file=sys.stderr)
        return 2

    map_path = Path(argv[1])
    if not map_path.exists():
        print(f"error: map file not found: {map_path}", file=sys.stderr)
        return 2

    symbols = parse_symbols(map_path)
    missing = [name for name in REQUIRED if name not in symbols]
    if missing:
        print(f"error: missing map symbols: {', '.join(missing)}", file=sys.stderr)
        return 2

    failures: list[str] = []
    for name in ("__CODE_END_tail", "__DATA_END_tail", "__BSS_END_tail"):
        value = symbols[name]
        if value > PAGING_WINDOW:
            failures.append(f"{name}={fmt(value)} overlaps the 128K paging window")

    sp = symbols["__register_sp"]
    if sp > PAGING_WINDOW:
        failures.append(f"__register_sp={fmt(sp)} starts inside the 128K paging window")

    if failures:
        print("ZX128 page-flip layout: NOT SAFE")
        for failure in failures:
            print(f"  - {failure}")
        print("Required: resident code/data/BSS end <= $C000 and SP <= $C000.")
        return 1

    print("ZX128 page-flip layout: safe")
    for name in REQUIRED:
        print(f"  {name}={fmt(symbols[name])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
