# tsfp-decomp

A 1:1 byte-matching decompilation of TimeSplitters: Future Perfect (GameCube, NTSC-U, `G3FE69`), part of the OpenMonkey project.

## About

The build reproduces the retail `main.dol` exactly (SHA-1 `791b22e44038676ef1935d9f78344a4dfcd2cd20`). Code that isn't decompiled yet is assembled from [decomp-toolkit](https://github.com/encounter/decomp-toolkit) splits. Translation units move to C/C++ as [objdiff](https://github.com/encounter/objdiff) confirms they match. The project is based on [dtk-template](https://github.com/encounter/dtk-template).

Matched code also feeds [tsfp-recomp](../tsfp-recomp), the PC port.

`reference/kiro/` holds an earlier, non-matching Ghidra pseudo-C pass. It is a reading aid and is not built.

## Building

### Prerequisites

- Python 3.8+
- Windows (native), or Linux/macOS with [wibo](https://github.com/decompals/wibo) or Wine for the Metrowerks compilers

`configure.py` downloads decomp-toolkit, objdiff-cli, binutils, and the CodeWarrior compiler bundle into `build/` on first run. `tools/bin/ninja.exe` is vendored for Windows.

### Setup

1. Place your own copy of the game's `sys/` folder (at least `main.dol`) in `orig/G3FE69/sys/`. You can also put a disc image in `orig/G3FE69/` and dtk will extract it.
2. Configure and build:
   ```
   python configure.py --ninja tools/bin/ninja.exe
   tools/bin/ninja.exe
   ```
   A successful build ends with `build/G3FE69/main.dol: OK` and a progress report.

## Layout

| Path | Purpose |
|---|---|
| `config/G3FE69/config.yml` | dtk project config |
| `config/G3FE69/symbols.txt` | Symbol names, sizes, and types |
| `config/G3FE69/splits.txt` | Translation unit boundaries |
| `configure.py` | Libraries, objects, compiler versions, and flags |
| `src/`, `include/` | Decompiled source |
| `build/G3FE69/asm/` | Generated disassembly (read-only reference) |

Workflow details are in [../CLAUDE.md](../CLAUDE.md).

## License

Distributed under the MIT License. See `LICENSE` for more information. No game data or binaries are included.
