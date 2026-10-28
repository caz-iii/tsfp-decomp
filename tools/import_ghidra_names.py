#!/usr/bin/env python3
"""Merge Ghidra function names into config/<ver>/symbols.txt.

Input: the GhidraMCP plugin's /list_functions output ("name at addr" per line).
Only dtk placeholder names (fn_XXXXXXXX) are replaced; signature-detected names
from dtk are kept. Generic Ghidra placeholders are skipped.
"""
import re
import sys
from pathlib import Path

GENERIC = re.compile(
    r"^(FUN_|thunk_FUN_|leaf_|stub_[0-9a-f]+$|LAB_|sub_|fn_|.*_[0-9a-f]{4,8}$)"
)
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def main(ghidra_list: str, symbols_path: str) -> None:
    names = {}
    for line in Path(ghidra_list).read_text(encoding="utf-8").splitlines():
        m = re.match(r"^(\S+) at ([0-9a-fA-F]{8})$", line.strip())
        if m and IDENT.match(m[1]) and not GENERIC.match(m[1]):
            names[int(m[2], 16)] = m[1]

    lines = Path(symbols_path).read_text(encoding="utf-8").splitlines()
    used = {l.split(" = ", 1)[0] for l in lines if " = " in l}
    out, renamed = [], 0
    for line in lines:
        m = re.match(r"^(fn_[0-9A-F]{8}) = (\.\w+):0x([0-9A-F]{8});(.*)$", line)
        if m and "type:function" in line:
            new = names.get(int(m[3], 16))
            if new:
                if new in used:
                    new = f"{new}_{m[3]}"
                if new not in used:
                    used.add(new)
                    line = f"{new} = {m[2]}:0x{m[3]};{m[4]}"
                    renamed += 1
        out.append(line)
    Path(symbols_path).write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"ghidra names usable: {len(names)}  renamed: {renamed}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
