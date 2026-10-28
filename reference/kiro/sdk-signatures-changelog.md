# SDK Signatures Changelog

## Format

Each entry follows this format:
- **Date:** ISO date
- **Version:** Schema version number
- **Changes:** What was added or modified
- **Verified by:** Who confirmed the patterns match

---

## Version 1 — Initial Release

- **Date:** 2026-07-14
- **Entries:** 26 signatures
- **Modules covered:** gx (5), os (5), dvd (4), mtx (3), ai (2), dsp (1), pad (2), vi (2), card (2)
- **Notes:** Initial placeholder patterns. Most patterns are approximations based on common GameCube SDK function prologues. Only OSCreateThread confirmed matched in the live binary. Real patterns should be extracted from confirmed matches as they are identified manually.
- **Match results:** 1/26 matched (OSCreateThread @ 0x80039ca8). GXEnd skipped (degenerate `blr` pattern).

---

## Future Entries

_Add new entries above this line as signatures are added or refined._

### Template:
```
## Version N — <Description>

- **Date:** YYYY-MM-DD
- **Changes:** 
  - Added: <function names>
  - Modified: <function names with reason>
  - Removed: <function names with reason>
- **Verified by:** <who confirmed the match>
- **Match results:** X/Y matched in live binary
```
