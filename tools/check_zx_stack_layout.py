#!/usr/bin/env python3
"""Check whether a z88dk +zx map leaves enough stack space above BSS."""

from __future__ import annotations

import re
import sys
from pathlib import Path


MIN_STACK_GAP = 512
REQUIRED = ("__BSS_END_tail", "__register_sp")


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

    bss_end = symbols["__BSS_END_tail"]
    sp = symbols["__register_sp"]
    gap = sp - bss_end
    if gap < MIN_STACK_GAP:
        print("ZX stack layout: NOT SAFE")
        print(f"  - only {gap} bytes between __BSS_END_tail and SP; need >= {MIN_STACK_GAP}")
        print(f"  __BSS_END_tail={fmt(bss_end)}")
        print(f"  __register_sp={fmt(sp)}")
        return 1

    print("ZX stack layout: safe")
    print(f"  __BSS_END_tail={fmt(bss_end)}")
    print(f"  __register_sp={fmt(sp)}")
    print(f"  stack_gap={gap} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
