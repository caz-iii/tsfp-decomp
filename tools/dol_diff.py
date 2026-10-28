#!/usr/bin/env python3
"""Report which split units own the bytes where build/<ver>/main.dol differs from the original."""
import re
import struct
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def segments(path):
    d = path.read_bytes()
    o = struct.unpack(">18I", d[0:72]); a = struct.unpack(">18I", d[72:144]); s = struct.unpack(">18I", d[144:216])
    return {a[i]: d[o[i]:o[i] + s[i]] for i in range(18) if s[i]}, d[216:228]


def units():
    cur, out = None, []
    for line in (ROOT / "config/G3FE69/splits.txt").read_text().splitlines():
        if line and not line[0].isspace() and line.endswith(":") and not line.startswith("Sections"):
            cur = line[:-1]
        m = re.match(r"^\s+(\.\w+)\s+start:0x([0-9A-F]+) end:0x([0-9A-F]+)", line)
        if m and cur:
            out.append((int(m[2], 16), int(m[3], 16), cur, m[1]))
    return out


def main():
    a, ab = segments(ROOT / "orig/G3FE69/sys/main.dol")
    b, bb = segments(ROOT / "build/G3FE69/main.dol")
    if set(a) != set(b) or ab != bb:
        print("segment layout differs:", sorted(map(hex, set(a) ^ set(b))), ab.hex(), bb.hex())
    us = units()
    hits = Counter()
    first = {}
    for base, data in a.items():
        other = b.get(base, b"")
        if len(other) != len(data):
            print(f"segment {base:08X} size {len(data):#x} -> {len(other):#x}")
        for i in range(min(len(data), len(other))):
            if data[i] != other[i]:
                addr = base + i
                owner = next((f"{u[2]} {u[3]}" for u in us if u[0] <= addr < u[1]), "unsplit")
                hits[owner] += 1
                first.setdefault(owner, addr)
    for owner, n in hits.most_common(int(sys.argv[1]) if len(sys.argv) > 1 else 40):
        print(f"{n:7d} bytes  first {first[owner]:08X}  {owner}")


if __name__ == "__main__":
    main()
