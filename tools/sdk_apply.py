#!/usr/bin/env python3
"""Apply build/sdk_match.json: splits, symbol names, sources, and unit list.

Everything this writes is regenerated on every run:
  - config/G3FE69/splits.txt   every dolphin/ unit
  - config/G3FE69/symbols.txt  names for matched functions/data (fn_/lbl_ only)
  - config/G3FE69/sdk_units.json  read by configure.py
  - src/dolphin/<lib>/..., include/ (reference headers), when --copy-src

Usage: sdk_apply.py --ref <ref-root> [--copy-src] [--only os,vi]
"""
import argparse
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CFG = ROOT / "config/G3FE69"
SECTION_ORDER = [".text", ".ctors", ".dtors", ".rodata", ".data", ".bss", ".sdata", ".sbss", ".sdata2", ".sbss2"]
SIGNED_CHAR = re.compile(r"(card/CARDRename|card/CARDOpen|dvd/.*|mtx/mtx|mtx/mtx44|demo/.*)\.c$")
OPT_OVERRIDE = {"exi/EXIBios.c": "-O3,p"}
TEXT_PAD = 0x40  # max alignment padding between adjacent units


def load_data_objects():
    """section -> sorted [(addr, end)] of every object dtk knows."""
    objs = {}
    for line in (CFG / "symbols.txt").read_text().splitlines():
        m = re.match(r"^\S+ = (\.\w+):0x([0-9A-F]+); // type:object size:0x([0-9A-F]+)", line)
        if m:
            a = int(m[2], 16)
            objs.setdefault(m[1], []).append((a, a + int(m[3], 16)))
    return {k: sorted(v) for k, v in objs.items()}


def object_end(objs, sec, addr):
    for a, e in objs.get(sec, []):
        if a == addr:
            return e
    return addr + 4


def select_units(results, only):
    units = [r for r in results if r.get("matched") and "error" not in r
             and (not only or r["file"].split("/")[0] in only or r["file"] in only)]
    accepted = []
    for u in sorted(units, key=lambda r: -r["matched"]):
        lo, hi = u["text"]
        if all(hi <= a["text"][0] or lo >= a["text"][1] for a in accepted):
            accepted.append(u)
    return sorted(accepted, key=lambda r: r["text"][0])


def add_defined_globals(units):
    """A unit owns every global it defines, even if only other units use it."""
    where = {}
    for u in units:
        for nm, addr in u.get("externs", {}).items():
            where.setdefault(nm, addr)
        for n in u["data_names"]:
            where.setdefault(n["name"], n["addr"])
    # Globals nobody else references in matched code (e.g. used only by game
    # code): place them by offset from a located symbol in the same section,
    # accepting only if dtk has an object of exactly that size there.
    objs = {(a, e - a) for v in load_data_objects().values() for a, e in v}
    for u in units:
        located = [d for d in u.get("defined", []) if d["name"] in where]
        for d in u.get("defined", []):
            if not d["global"] or d["name"] in where:
                continue
            for k in located:
                if k["section"] == d["section"]:
                    guess = where[k["name"]] + d["value"] - k["value"]
                    if (guess, d["size"]) in objs:
                        where[d["name"]] = guess
                        u["data_names"].append({"name": d["name"], "section": d["section"],
                                                "addr": guess, "size": d["size"]})
                    break
    for u in units:
        for d in u.get("defined", []):
            if d["global"] and d["name"] in where:
                u["data"].setdefault(d["section"], [])
                if where[d["name"]] not in u["data"][d["section"]]:
                    u["data"][d["section"]] = sorted(u["data"][d["section"]] + [where[d["name"]]])


def partition_data(units, objs):
    """Each unit owns a data section from its first referenced object up to the
    next unit's first object, if the two units are adjacent in link order.
    Otherwise it ends after its last referenced object."""
    for i, u in enumerate(units):
        u["ranges"] = {".text": list(u["text"])}
        # Drop stray references: keep the densest window no larger than the
        # unit's own compiled section.
        for sec, refs in list(u["data"].items()):
            size = u.get("sizes", {}).get(sec)
            if size and refs and refs[-1] - refs[0] >= size:
                best = max(((r, [x for x in refs if r <= x < r + size]) for r in refs), key=lambda t: len(t[1]))
                u["data"][sec] = best[1]
    for sec in {s for u in units for s in u["data"]}:
        users = [(i, min(u["data"][sec]), max(u["data"][sec])) for i, u in enumerate(units) if sec in u["data"]]
        for k, (i, start, last) in enumerate(users):
            end = object_end(objs, sec, last)
            if k + 1 < len(users):
                j, nstart, _ = users[k + 1]
                adjacent = all(units[t + 1]["text"][0] - units[t]["text"][1] <= TEXT_PAD for t in range(i, j))
                if nstart < start:
                    units[i].setdefault("warnings", []).append(f"{sec} order inverted vs {units[j]['file']}")
                elif adjacent:
                    end = nstart
                else:
                    end = min(max(end, start), nstart)
            cap = start + units[i].get("sizes", {}).get(sec, end - start)
            end = max(min(end, cap), object_end(objs, sec, last))
            limit = users[k + 1][1] if k + 1 < len(users) else 1 << 32
            for oa, oe in objs.get(sec, []):  # never end inside an object
                if oa < end < oe:
                    end = oe if oe <= limit else oa
                    break
            # Absorb trailing alignment padding up to the next object.
            nxt = min([oa for oa, _ in objs.get(sec, []) if oa >= end] + [limit])
            if nxt - end < 16:
                end = nxt
            units[i]["ranges"][sec] = [start, end]
    # Residual overlaps: trim the earlier range; if a range starts inside an
    # earlier one, drop it and keep the unit out of the link.
    for sec in {s for u in units for s in u["ranges"]} - {".text"}:
        owned = sorted((u["ranges"][sec][0], i) for i, u in enumerate(units) if sec in u["ranges"])
        for (a0, i), (b0, j) in zip(owned, owned[1:]):
            ri, rj = units[i]["ranges"], units[j]["ranges"]
            if sec in ri and sec in rj and ri[sec][1] > rj[sec][0]:
                if ri[sec][0] < rj[sec][0]:
                    ri[sec][1] = rj[sec][0]
                else:
                    del rj[sec]
                    units[j]["no_link"] = True
                units[i].setdefault("warnings", []).append(f"{sec} overlap with {units[j]['file']} trimmed")


def write_splits(units):
    """Replace every dolphin/ unit in splits.txt (dtk rewrites this file, so
    blocks are found by path rather than by comment markers)."""
    path = CFG / "splits.txt"
    kept, skip = [], False
    for line in path.read_text().splitlines():
        if line and not line[0].isspace():
            skip = line.startswith("dolphin/") or line.startswith("# --- ")
        if not skip:
            kept.append(line)
    lines = []
    for u in units:
        lines.append(f"dolphin/{u['file']}:")
        for sec in SECTION_ORDER:
            if sec in u["ranges"]:
                a, b = u["ranges"][sec]
                lines.append(f"\t{sec:<11} start:0x{a:08X} end:0x{b:08X}")
        lines.append("")
    path.write_text("\n".join(kept).rstrip("\n") + "\n\n" + "\n".join(lines))


def rename_symbols(units):
    path = CFG / "symbols.txt"
    lines = path.read_text().splitlines()
    want = {}
    for u in units:
        for n in u["names"]:
            want[n["addr"]] = n["name"]
        for n in u["data_names"]:
            want.setdefault(n["addr"], n["name"])
    forced = {}
    for u in units:
        for nm, addr in u.get("externs", {}).items():
            forced.setdefault(addr, nm)
    want.update(forced)
    used = {l.split(" = ", 1)[0] for l in lines}
    count = 0
    for i, l in enumerate(lines):
        m = re.match(r"^(\S+) = (\.\w+):0x([0-9A-F]+);(.*)$", l)
        if not m:
            continue
        new = want.get(int(m[3], 16))
        if not new or new == m[1] or new in used:
            continue
        # Only replace placeholder or Kiro/Ghidra-derived names, never dtk signature names.
        if m[1].startswith(("fn_", "lbl_")) or not m[1].startswith("_") or int(m[3], 16) in forced:
            used.discard(m[1])
            used.add(new)
            lines[i] = f"{new} = {m[2]}:0x{m[3]};{m[4]}"
            count += 1
    for u in units:
        for lb in u.get("labels", []):
            if lb["name"] not in used:
                lines.append(f"{lb['name']} = .text:0x{lb['addr']:08X}; // type:label scope:global")
                used.add(lb["name"])
                count += 1
    path.write_text("\n".join(lines) + "\n")
    return count


def copy_sources(ref, units):
    for u in units:
        lib = u["file"].split("/")[0]
        dst = ROOT / "src/dolphin" / lib
        dst.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ref / "src" / u["file"], dst / Path(u["file"]).name)
        for h in (ref / "src" / lib).glob("*.h"):
            shutil.copy2(h, dst / h.name)
    for item in (ref / "include").iterdir():
        target = ROOT / "include" / item.name
        if item.is_dir():
            shutil.copytree(item, target, dirs_exist_ok=True)
        else:
            shutil.copy2(item, target)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True, type=Path)
    ap.add_argument("--copy-src", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--verify", action="store_true",
                    help="mark units that objdiff reports 100%% code+data as verified (linked)")
    ap.add_argument("--exclude", default="", help="comma list of units never to link")
    a = ap.parse_args()
    results = json.loads((ROOT / "build/sdk_match.json").read_text())["results"]
    units = select_units(results, set(filter(None, a.only.split(","))))
    add_defined_globals(units)
    partition_data(units, load_data_objects())
    write_splits(units)
    renamed = rename_symbols(units)
    if a.copy_src:
        copy_sources(a.ref, units)
    verified = set()
    report = ROOT / "build/G3FE69/report.json"
    if a.verify and report.exists():
        for x in json.loads(report.read_text())["units"]:
            m = x["measures"]
            if m.get("matched_code_percent", 0) == 100 and m.get("matched_data_percent", 100) == 100:
                verified.add(x["name"].split("/", 1)[1] + ".c")
    excluded = set(filter(None, a.exclude.split(",")))
    out = []
    for u in units:
        extra = ["-char", "signed" if SIGNED_CHAR.search(u["file"]) else "unsigned"]
        out.append({"lib": u["file"].split("/")[0], "file": f"dolphin/{u['file']}",
                    "complete": not u["unmatched_dol"], "opt": OPT_OVERRIDE.get(u["file"], "-O4,p"),
                    "extra_cflags": extra, "warnings": u.get("warnings", []),
                    "verified": f"dolphin/{u['file']}" in verified and u["file"] not in excluded
                    and not u.get("no_link")})
    (CFG / "sdk_units.json").write_text(json.dumps(out, indent=1) + "\n")
    print(f"units {len(units)}  complete {sum(o['complete'] for o in out)}  "
          f"verified {sum(o['verified'] for o in out)}  renamed {renamed}")
    for o in out:
        if o["warnings"]:
            print("  warn", o["file"], o["warnings"])


if __name__ == "__main__":
    main()
