#!/usr/bin/env python3
"""Locate reference-source translation units inside main.dol.

Compiles every C file of a reference library (e.g. doldecomp/dolsdk2004) with
the original Metrowerks flags, then matches each compiled function against the
DOL with relocated fields masked out. For every source file with matches it
reports the .text range, the data ranges it owns (recovered from relocations),
and real names for functions and data objects.

Usage:
  sdk_match.py --ref <ref-root> --lib os [--lib gx ...] --out build/sdk_match.json
"""
import argparse
import json
import re
import struct
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOL = ROOT / "orig/G3FE69/sys/main.dol"
SYMBOLS = ROOT / "config/G3FE69/symbols.txt"
COMPILERS = ROOT / "build/compilers/GC"

# Per-file flag overrides from dolsdk2004's Makefile.
SIGNED_CHAR = re.compile(r"(card/CARDRename|card/CARDOpen|dvd/.*|mtx/mtx|mtx/mtx44|demo/.*)\.c$")
OPT_OVERRIDE = {"exi/EXIBios.c": "-O3,p"}

R_PPC_ADDR32, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA = 1, 4, 5, 6
R_PPC_REL24, R_PPC_REL14, R_PPC_EMB_SDA21 = 10, 11, 109
MASKS = {
    R_PPC_ADDR16_LO: 0x0000FFFF, R_PPC_ADDR16_HI: 0x0000FFFF, R_PPC_ADDR16_HA: 0x0000FFFF,
    R_PPC_REL24: 0x03FFFFFC, R_PPC_REL14: 0x0000FFFC, R_PPC_EMB_SDA21: 0x001FFFFF,
    R_PPC_ADDR32: 0xFFFFFFFF,
}
DATA_SECTIONS = (".data", ".rodata", ".sdata", ".sdata2", ".bss", ".sbss", ".sbss2", ".ctors", ".dtors")


# ---------------------------------------------------------------- DOL / symbols
class Dol:
    def __init__(self, path):
        d = path.read_bytes()
        offs = struct.unpack(">18I", d[0:72])
        addrs = struct.unpack(">18I", d[72:144])
        sizes = struct.unpack(">18I", d[144:216])
        self.segs = [(a, d[o:o + s]) for o, a, s in zip(offs, addrs, sizes) if s]

    def read(self, addr, n):
        for a, b in self.segs:
            if a <= addr and addr + n <= a + len(b):
                return b[addr - a:addr - a + n]
        return None


def load_dol_functions():
    funcs = []
    for line in SYMBOLS.read_text().splitlines():
        m = re.match(r"^(\S+) = \.(init|text):0x([0-9A-F]+); // type:function size:0x([0-9A-F]+)", line)
        if m:
            funcs.append((int(m[3], 16), int(m[4], 16), m[1]))
    return funcs


def sda_bases(dol):
    """r13/r2 from __init_registers (lis/ori pairs for r1, r2, r13)."""
    regs = {}
    for a, b in dol.segs[:2]:
        for i in range(0, len(b) - 8, 4):
            w1, w2 = struct.unpack(">II", b[i:i + 8])
            if w1 >> 26 == 15 and (w1 >> 16) & 31 == 0 and w2 >> 26 == 24:  # lis rX,hi ; ori rX,rX,lo
                rd = (w1 >> 21) & 31
                if rd in (2, 13) and (w2 >> 21) & 31 == rd and (w2 >> 16) & 31 == rd:
                    regs.setdefault(rd, ((w1 & 0xFFFF) << 16) | (w2 & 0xFFFF))
    return regs.get(13), regs.get(2)


# ---------------------------------------------------------------- ELF objects
class Obj:
    def __init__(self, path):
        d = path.read_bytes()
        shoff, = struct.unpack(">I", d[0x20:0x24])
        shentsize, shnum, shstrndx = struct.unpack(">HHH", d[0x2E:0x34])
        sh = [struct.unpack(">10I", d[shoff + i * shentsize:shoff + i * shentsize + 40]) for i in range(shnum)]
        strtab = sh[shstrndx]

        def name(tab, off):
            base = sh[tab][4]
            end = d.index(b"\0", base + off)
            return d[base + off:end].decode("latin1")

        self.sections = []
        for i, s in enumerate(sh):
            self.sections.append({"name": name(shstrndx, s[0]), "type": s[1], "size": s[5],
                                  "data": d[s[4]:s[4] + s[5]] if s[1] != 8 else b""})
        self.symbols = []
        self.relocs = defaultdict(list)  # target section index -> [(off, type, sym, addend)]
        for i, s in enumerate(sh):
            if s[1] == 2:  # SYMTAB
                for j in range(s[5] // 16):
                    nm, val, sz, info, oth, shn = struct.unpack(">IIIBBH", d[s[4] + j * 16:s[4] + j * 16 + 16])
                    self.symbols.append({"name": name(s[6], nm) if nm else "", "value": val, "size": sz,
                                         "bind": info >> 4, "type": info & 15, "shndx": shn})
        for i, s in enumerate(sh):
            if s[1] == 4:  # RELA
                for j in range(s[5] // 12):
                    off, info, add = struct.unpack(">IIi", d[s[4] + j * 12:s[4] + j * 12 + 12])
                    self.relocs[s[7]].append((off, info & 0xFF, info >> 8, add))

    def section_index(self, nm):
        for i, s in enumerate(self.sections):
            if s["name"] == nm:
                return i
        return None


def compile_file(ref, src, out, mw_version):
    rel = src.relative_to(ref / "src").as_posix()
    flags = ["-char", "signed" if SIGNED_CHAR.search(rel) else "unsigned",
             "-nodefaults", "-proc", "gekko", "-fp", "hard", "-Cpp_exceptions", "off", "-enum", "int",
             "-warn", "off", "-requireprotos", "-pragma", "cats off", "-D__GEKKO__", "-DSDK_REVISION=2",
             OPT_OVERRIDE.get(rel, "-O4,p"), "-inline", "auto", "-DRELEASE",
             "-I-", "-Iinclude", "-Iinclude/libc", "-ir", "src"]
    out.parent.mkdir(parents=True, exist_ok=True)
    cc = COMPILERS / mw_version / "mwcceppc.exe"
    r = subprocess.run([str(cc), "-c", *flags, str(src.relative_to(ref)), "-o", str(out)],
                       cwd=ref, capture_output=True, text=True)
    return r.returncode == 0, (r.stdout + r.stderr)[-400:]


# ---------------------------------------------------------------- matching
def masked_function(obj, tidx, fsym):
    """(bytes, mask) for one function, masks relocated fields."""
    text = obj.sections[tidx]["data"]
    start, size = fsym["value"], fsym["size"]
    code = bytearray(text[start:start + size])
    mask = bytearray(b"\xff" * size)
    for off, typ, _, _ in obj.relocs.get(tidx, []):
        if start <= off < start + size:
            w = (off - start) & ~3
            m = MASKS.get(typ, 0xFFFFFFFF)
            for k in range(4):
                if (m >> (24 - 8 * k)) & 0xFF:
                    mask[w + k] &= ~((m >> (24 - 8 * k)) & 0xFF) & 0xFF
    return bytes(code), bytes(mask)


def equal_masked(a, b, mask):
    return all((x ^ y) & m == 0 for x, y, m in zip(a, b, mask))


def match_object(obj, dol, by_size):
    tidx = obj.section_index(".text")
    if tidx is None:
        return []
    funcs = [s for s in obj.symbols if s["type"] == 2 and s["shndx"] == tidx and s["size"]]
    funcs.sort(key=lambda s: s["value"])
    out = []
    for f in funcs:
        code, mask = masked_function(obj, tidx, f)
        cands = [a for a, n in by_size.get(f["size"], []) if equal_masked(dol.read(a, f["size"]), code, mask)]
        out.append({"name": f["name"], "obj_off": f["value"], "size": f["size"], "cands": cands})
    return out


def resolve(matches):
    """Pick one address per function. Unique matches >= 16 bytes are anchors;
    every other function must fall between its neighbouring picks in order."""
    raw = [(i, m["cands"][0]) for i, m in enumerate(matches) if len(m["cands"]) == 1 and m["size"] >= 16]
    if not raw:
        return []
    # Longest chain of anchors whose addresses increase with source order and that
    # sit no farther apart than the object's own code could span.
    span = sum(m["size"] for m in matches) + 0x100
    best = [[r] for r in raw]
    for k in range(len(raw)):
        for j in range(k):
            if raw[j][1] < raw[k][1] <= raw[j][1] + span and len(best[j]) + 1 > len(best[k]):
                best[k] = best[j] + [raw[k]]
    anchor_at = dict(max(best, key=len))
    picked, last = [], None
    for i, m in enumerate(matches):
        nxt = next((anchor_at[j] for j in sorted(anchor_at) if j > i), None)
        if i in anchor_at:
            a = anchor_at[i]
            if last is not None and a < last:
                continue
        else:
            lo = last if last is not None else min(anchor_at.values()) - 0x400
            hi = nxt if nxt is not None else (last or 0) + 0x400
            c = [x for x in m["cands"] if lo <= x < hi]
            if not c:
                continue
            a = c[0]
        picked.append((a, m))
        last = a + m["size"]
    return picked


def data_ranges(obj, picked, tidx, dol_bases, dol):
    """Map referenced data symbols to DOL addresses via the matched code."""
    r13, r2 = dol_bases
    sym_addr = {}
    refs = defaultdict(set)
    externs = {}
    relocs = obj.relocs.get(tidx, [])
    for a, m in picked:
        start = m["obj_off"]
        ha = {}
        for off, typ, si, add in relocs:
            if not (start <= off < start + m["size"]):
                continue
            sym = obj.symbols[si]
            word, = struct.unpack(">I", dol.read(a + ((off - start) & ~3), 4))
            if sym["shndx"] == 0 and sym["name"]:
                # External reference: resolve its DOL address from the matched instruction.
                pc = a + ((off - start) & ~3)
                t = None
                if typ == R_PPC_REL24:
                    t = pc + (((word & 0x03FFFFFC) ^ 0x02000000) - 0x02000000)
                elif typ == R_PPC_EMB_SDA21:
                    b_ = {13: r13, 2: r2}.get((word >> 16) & 31)
                    t = None if b_ is None else b_ + ((word & 0xFFFF) ^ 0x8000) - 0x8000
                elif typ == R_PPC_ADDR16_HA:
                    ha[(si, add)] = word & 0xFFFF
                elif typ == R_PPC_ADDR16_LO and (si, add) in ha:
                    t = (ha[(si, add)] << 16) + ((word & 0xFFFF) ^ 0x8000) - 0x8000
                if t is not None:
                    externs.setdefault(sym["name"], t - add)
                continue
            if sym["shndx"] >= len(obj.sections):
                continue
            secname = obj.sections[sym["shndx"]]["name"]
            if secname not in DATA_SECTIONS:
                continue
            target = None
            if typ == R_PPC_EMB_SDA21:
                base = {13: r13, 2: r2}.get((word >> 16) & 31)
                if base is not None:
                    target = base + ((word & 0xFFFF) ^ 0x8000) - 0x8000
            elif typ == R_PPC_ADDR16_HA:
                ha[(si, add)] = word & 0xFFFF
            elif typ == R_PPC_ADDR16_LO and (si, add) in ha:
                target = (ha[(si, add)] << 16) + ((word & 0xFFFF) ^ 0x8000) - 0x8000
            if target is not None:
                refs[secname].add(target if sym["type"] == 3 else target - add)
                base = target - add - (sym["value"] if sym["type"] != 3 else 0)
                sym_addr.setdefault((secname, sym["name"] or f"@sec{sym['shndx']}", sym["value"], sym["size"]), base)
    bases = defaultdict(set)
    names = []
    for (sec, nm, val, size), base in sym_addr.items():
        bases[sec].add(base)
        if not nm.startswith("@") and size:
            names.append({"name": nm, "section": sec, "addr": base + val, "size": size})
    ranges, conflicts = {}, {}
    for sec, bs in bases.items():
        idx = obj.section_index(sec)
        size = obj.sections[idx]["size"] if idx is not None else 0
        if len(bs) == 1 and size:
            b0 = next(iter(bs))
            ranges[sec] = [b0, b0 + size]
        elif len(bs) > 1:
            conflicts[sec] = sorted(bs)
    # sections that exist in the object but were never referenced from matched code
    missing = [s["name"] for s in obj.sections if s["name"] in DATA_SECTIONS and s["size"] and s["name"] not in ranges]
    return {k: sorted(v) for k, v in refs.items()}, names, conflicts, missing, externs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True, type=Path)
    ap.add_argument("--lib", action="append", required=True)
    ap.add_argument("--mw", default="1.2.5n")
    ap.add_argument("--out", type=Path, default=ROOT / "build/sdk_match.json")
    a = ap.parse_args()

    dol = Dol(DOL)
    bases = sda_bases(dol)
    by_size = defaultdict(list)
    dol_funcs = load_dol_functions()
    for addr, size, nm in dol_funcs:
        by_size[size].append((addr, nm))

    results = []
    for lib in a.lib:
        for src in sorted((a.ref / "src" / lib).glob("*.c")):
            rel = src.relative_to(a.ref / "src").as_posix()
            obj_path = ROOT / "build/sdk_match" / a.mw / (rel[:-2] + ".o")
            ok, log = compile_file(a.ref, src, obj_path, a.mw)
            if not ok:
                results.append({"file": rel, "error": "compile", "log": log})
                continue
            obj = Obj(obj_path)
            matches = match_object(obj, dol, by_size)
            picked = resolve(matches)
            entry = {"file": rel, "functions": len(matches), "matched": len(picked)}
            if picked:
                tidx = obj.section_index(".text")
                entry["text"] = [picked[0][0], picked[-1][0] + picked[-1][1]["size"]]
                entry["names"] = [{"addr": x, "name": m["name"], "size": m["size"]} for x, m in picked]
                entry["data"], entry["data_names"], entry["data_conflicts"], entry["data_missing"], entry["externs"] = data_ranges(obj, picked, tidx, bases, dol)
                # Object facts the apply step needs for data ownership and labels.
                entry["sizes"] = {sc["name"]: sc["size"] for sc in obj.sections if sc["name"] in DATA_SECTIONS}
                entry["defined"] = [{"name": y["name"], "section": obj.sections[y["shndx"]]["name"],
                                     "value": y["value"], "size": y["size"], "global": y["bind"] != 0}
                                    for y in obj.symbols
                                    if y["name"] and 0 < y["shndx"] < len(obj.sections)
                                    and obj.sections[y["shndx"]]["name"] in DATA_SECTIONS and y["type"] in (0, 1)]
                fn_at = {m["obj_off"]: (x, m["size"]) for x, m in picked}
                entry["labels"] = []
                for y in obj.symbols:
                    if y["shndx"] == tidx and y["type"] == 0 and y["name"] and not y["name"].startswith("@"):
                        for fo, (fa, fs) in fn_at.items():
                            if fo <= y["value"] <= fo + fs:
                                entry["labels"].append({"name": y["name"], "addr": fa + y["value"] - fo})
                                break
                got = {x for x, _ in picked}
                lo_, hi_ = entry["text"]
                entry["unmatched_dol"] = [[x, n] for x, sz, n in dol_funcs if lo_ <= x < hi_ and x not in got]
            results.append(entry)
            print(f"{rel:40s} {entry.get('matched', 0):3d}/{entry.get('functions', 0):3d}"
                  + (f"  .text {entry['text'][0]:08X}-{entry['text'][1]:08X}" if picked else "")
                  + (f"  gaps {len(entry['unmatched_dol'])}" if picked else "")
                  + (f"  data {','.join(entry['data'])}" if picked and entry['data'] else "")
                  + (f"  DATA?{entry['data_conflicts'] or ''}{entry['data_missing'] or ''}" if picked and (entry['data_conflicts'] or entry['data_missing']) else "")
                  + ("  COMPILE FAIL" if "error" in entry else ""))
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps({"sda": bases, "results": results}, indent=1))


if __name__ == "__main__":
    main()
