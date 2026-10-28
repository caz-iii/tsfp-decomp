#!/usr/bin/env python3
"""Find the largest set of candidate units that can be linked without breaking
the DOL hash, by group testing.

Candidates are units in config/<ver>/sdk_units.json with "complete" set whose
objdiff report shows 100% code and data. The final "verified" flags are written
back to sdk_units.json, and the bad units are listed in build/sdk_bisect.json.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UNITS = ROOT / "config/G3FE69/sdk_units.json"
NINJA = ROOT / "tools/bin/ninja.exe"


def build_ok():
    try:
        r = subprocess.run([str(NINJA)], cwd=ROOT, capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return False
    return "main.dol: OK" in r.stdout


def set_verified(units, names):
    for u in units:
        u["verified"] = u["file"] in names
    UNITS.write_text(json.dumps(units, indent=1) + "\n")


def main():
    units = json.loads(UNITS.read_text())
    report = json.loads((ROOT / "build/G3FE69/report.json").read_text())
    perfect = {"/".join(x["name"].split("/")[1:]) + ".c" for x in report["units"]
               if x["measures"].get("matched_code_percent", 0) == 100
               and x["measures"].get("matched_data_percent", 100) == 100}
    exclude = set(sys.argv[1:])
    cands = [u["file"] for u in units if u["complete"] and u["file"] in perfect and u["file"] not in exclude]
    good, bad, builds = set(), [], 0

    def check(group):
        nonlocal builds
        set_verified(units, good | set(group))
        builds += 1
        if build_ok():
            good.update(group)
            print(f"  ok   +{len(group):3d}  (linked {len(good)})", flush=True)
        elif len(group) == 1:
            bad.append(group[0])
            print(f"  bad  {group[0]}", flush=True)
        else:
            mid = len(group) // 2
            check(group[:mid])
            check(group[mid:])

    print(f"candidates {len(cands)}", flush=True)
    check(cands)
    set_verified(units, good)
    build_ok()
    (ROOT / "build/sdk_bisect.json").write_text(json.dumps({"linked": sorted(good), "bad": bad}, indent=1))
    print(f"linked {len(good)}  bad {len(bad)}  builds {builds}")


if __name__ == "__main__":
    main()
