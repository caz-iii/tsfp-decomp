// Address: 0x8038a0a4 | Name: msl_qsort_heap      | Date: 2026-09-13
// Address: 0x8038a210 | Name: msl_sscanf          | Date: 2026-09-13
// Address: 0x8038a364 | Name: msl_scan_engine     | Date: 2026-09-13
//
// Metrowerks Standard Library (MSL) scanf family + the qsort heapsort core.
//
// msl_scan_engine is the shared scanf core: a character-source callback
//   src(state, ch, op)   op 0 = fetch next char, op 1 = push char back,
//                        op 2 = at-EOF probe
// drives the parse. It walks the format string, matching literal/whitespace runs
// and %-conversions parsed by the spec parser (FUN_8038ad10), pulling assignment
// targets from the varargs and converting numbers via the runtime scan helpers
// (FUN_8038b670 float, FUN_8038c82c 64-bit int, FUN_8038cc38 32-bit int).
//
// The scan-spec record (written by FUN_8038ad10) is 20 bytes:
//   +0  suppress '*' flag        +1  (pad)
//   +5  conversion char          +6  length modifier (0 int..7 wide)
//   +8  field-width (int)        others reserved
//
// PPC note: the engine's FP accumulators appear in the decompiler as uninitialized
// register pairs (unaff_rNN); they are working temporaries, modelled here as locals.

#include "common.h"

// --- Named cross-module helpers ---------------------------------------------
extern int *aiactor_arg_walker(char *va, int kind);      // varargs fetch (0x803824b0)
extern int  msl_utf8_to_utf16(ushort *out, byte *src, uint avail);  // msl_string.cpp

// --- Per-file externs for raw symbols referenced by this TU -----------------
extern int    FUN_8038a364(void *src, undefined4 state, byte *fmt, char *scanSpec); // scan engine
byte  *FUN_8038ad10(int fmtAtPercent, undefined4 *specOut);          // parse scan conversion (defined below)
extern double FUN_8038b670(uint base, void *src, undefined4 state, int *countOut, int *errOut); // float scan
extern u64    FUN_8038c82c(uint radix, int width, void *src, undefined4 state,
                           int *countOut, int *negOut, int *errOut);  // 64-bit int scan
extern uint   FUN_8038cc38(uint radix, int width, void *src, undefined4 state,
                           int *countOut, int *negOut, int *errOut);  // 32-bit int scan
extern u8     DAT_80497890[];   // ctype-style class table
// The sscanf char-source callback body lives at 0x8038a2d4 (a label inside this
// module in the original); it is provided as a raw symbol.
extern "C" void LAB_8038a2d4(void);

// ---------------------------------------------------------------------------
// msl_qsort_heap (0x8038a0a4) - in-place heapsort of `count` elements of `size`
// bytes at `base`, ordered by the caller's comparator `cmp(a,b)` (<0 a-before-b).
// Sifts down from the midpoint to build the heap, then repeatedly swaps the root
// to the end and re-sifts. Element swaps are byte-wise.
void FUN_8038a0a4(int base, uint count, int size, void *cmpArg)
{
    typedef int (*cmp_t)(int, int);
    cmp_t cmp = (cmp_t)cmpArg;
    u8   tmp;
    int  a;
    int  b;
    int  ia;
    int  ib;
    int  n;
    int  child;
    uint node;
    uint parent;

    if (count < 2) {
        return;
    }
    parent = (count >> 1) + 1;
    a = base + size * (count >> 1);
    b = base + size * (count - 1);
    do {
        if (parent < 2) {
            ia = b + -1;
            ib = a + -1;
            n = size + 1;
            while (n = n + -1, n != 0) {
                tmp = *(u8 *)(ib + 1);
                *(u8 *)(ib + 1) = *(u8 *)(ia + 1);
                ib = ib + 1;
                *(u8 *)(ia + 1) = tmp;
                ia = ia + 1;
            }
            count = count - 1;
            if (count == 1) {
                return;
            }
            b = b - size;
        }
        else {
            a = a - size;
            parent = parent - 1;
        }
        ia = base + size * (parent - 1);
        node = parent;
        while (node << 1 <= count) {
            node = node * 2;
            child = base + size * (node - 1);
            if (node < count) {
                if (cmp(child, child + size) < 0) {
                    node = node + 1;
                    child = child + size;
                }
            }
            if (-1 < cmp(ia, child)) break;
            ib = child + -1;
            ia = ia + -1;
            n = size + 1;
            while (n = n + -1, ia = child, n != 0) {
                tmp = *(u8 *)(ia + 1);         // note: ia was just set to child below-loop
                *(u8 *)(ia + 1) = *(u8 *)(ib + 1);
                ia = ia + 1;
                *(u8 *)(ib + 1) = tmp;
                ib = ib + 1;
            }
        }
    } while (true);
}

// ---------------------------------------------------------------------------
// msl_scan_engine (0x8038a364) - the scanf core (see the header block). Returns
// the number of successful assignments, or -1 on input failure before any match.
int FUN_8038a364(void *srcArg, undefined4 state, byte *fmt, char *scanSpec)
{
    typedef int (*src_t)(undefined4, int, int);
    src_t src = (src_t)srcArg;
    bool   ok;
    byte   fc;
    double dAcc = 0.0;
    float  fAcc = 0.0f;
    uint   uv;
    int   *arg;
    int    rc;
    double dTmp = 0.0;
    double dOut;
    int    matched = 0;   // successful assignments (iVar9)
    int    seen = 0;      // conversions attempted (iVar8)
    int    consumed = 0;  // input chars consumed (fVar10 in original, kept as int)
    // scan-spec record filled by FUN_8038ad10 (40-byte record; tail is the %[ scanset bitmap)
    u8     spec[40];
    int    specWidth;     // record width field (spec+8)
    int    negFlag;       // scratch out from number scanners (local_70)
    int    countOut;      // chars produced by a scanner (local_6c)
    int    errOut;        // error flag from scanners (iStack_74)
    u8     suppress;      // spec+0 (local_68)
    u8     lenmod;        // spec+6 (local_66)
    u8     conv;          // spec+5 (local_65)
    uint   width;         // spec+8 (local_64)
    byte   inChar[4];
    byte   charClass[32]; // %[ scanset bitmap (abStack_60)
    (void)charClass; (void)dTmp;

    while (true) {
        fc = *fmt;
        if (fc == 0) {
        at_eof:
            rc = src(state, 0, 2);
            if ((rc != 0) && (seen == 0)) matched = -1;
            return matched;
        }
        if ((DAT_80497890[fc] & 6) == 0) {           // not whitespace
            if (fc == 0x25) {                        // conversion
                fmt = FUN_8038ad10((int)fmt, (undefined4 *)spec);
                // FUN_8038ad10 writes a 20-byte scan-spec record at `spec`.
                // Documented fields: [0]=suppress, [5]=conv, [6]=lenmod, [8]=width.
                suppress = ((u8 *)spec)[0];
                conv     = ((u8 *)spec)[5];
                lenmod   = ((u8 *)spec)[6];
                width = *(uint *)((int)spec + 8);
                specWidth = (int)width;
                (void)specWidth; (void)arg;

                // The scanf conversion machinery (numeric/string/set parsing) is
                // delegated to the runtime scanners; the assignment target pointer
                // comes from the varargs.
                {
                    int *target = (suppress == 0 && conv != 0x25)
                                      ? (int *)*aiactor_arg_walker(scanSpec, 1)
                                      : (int *)0;
                    if ((conv != 0x6e) && (rc = src(state, 0, 2), rc != 0)) goto at_eof;

                    if (conv == 'd' || conv == 'i' || conv == 'u' ||
                        conv == 'o' || conv == 'x' || conv == 'X' || conv == 'p') {
                        uv = (conv == 'd') ? 10u
                           : (conv == 'i') ? 0u
                           : (conv == 'o') ? 8u
                           : (conv == 'u') ? 10u : 0x10u;
                        if (lenmod == 4) {
                            u64 v = FUN_8038c82c(uv, (int)width, srcArg, state,
                                                 &countOut, &negFlag, &errOut);
                            if (countOut == 0) goto at_eof;
                            consumed += countOut;
                            if (negFlag != 0) v = (u64)(-(s64)v);
                            if (target != (int *)0) { *(u64 *)target = v; matched++; }
                        }
                        else {
                            uint v = FUN_8038cc38(uv, (int)width, srcArg, state,
                                                  &countOut, &negFlag, &errOut);
                            if (countOut == 0) goto at_eof;
                            consumed += countOut;
                            if (negFlag != 0) v = (uint)(-(int)v);
                            if (target != (int *)0) {
                                if (lenmod == 2)      *(short *)target = (short)v;
                                else if (lenmod == 1) *(char *)target = (char)v;
                                else                  *target = (int)v;
                                matched++;
                            }
                        }
                        seen++;
                        continue;
                    }
                    if (conv == 'e' || conv == 'E' || conv == 'f' || conv == 'F' ||
                        conv == 'g' || conv == 'G' || conv == 'a' || conv == 'A') {
                        dOut = FUN_8038b670(width, srcArg, state, &countOut, &errOut);
                        if (countOut == 0) goto at_eof;
                        consumed += countOut;
                        if (target != (int *)0) {
                            if (lenmod == 5 || lenmod == 6) *(double *)target = dOut;
                            else if (lenmod == 0)           *(float *)target = (float)dOut;
                            else                            *(double *)target = dOut;
                            matched++;
                        }
                        seen++;
                        continue;
                    }
                    if (conv == 'n') {                 // store count so far
                        if (target != (int *)0) {
                            if (lenmod == 2)      *(short *)target = (short)consumed;
                            else if (lenmod == 1) *(char *)target = (char)consumed;
                            else if (lenmod == 4) { ((int *)target)[1] = consumed; *target = consumed >> 0x1f; }
                            else                  *target = consumed;
                        }
                        continue;
                    }
                    if (conv == 'c' || conv == 's' || conv == 0x5b) {  // c / s / [set]
                        // character / string capture: consume up to width chars
                        int produced = 0;
                        uint remaining = (conv == 'c') ? (width == 0 ? 1u : width) : width;
                        if (conv != 'c') {
                            // skip leading whitespace for %s
                            if (conv == 's') {
                                do { inChar[0] = (byte)src(state, 0, 0); }
                                while ((DAT_80497890[inChar[0]] & 6) != 0 && (consumed++, 1));
                                src(state, (int)(char)inChar[0], 1);
                            }
                        }
                        for (; remaining != 0; remaining--) {
                            int ci = src(state, 0, 0);
                            inChar[0] = (byte)ci;
                            if (ci == -1) break;
                            if (conv == 's' && (DAT_80497890[inChar[0]] & 6) != 0) break;
                            if (target != (int *)0) {
                                if (lenmod == 7) { msl_utf8_to_utf16((ushort *)target, inChar, 1); target = (int *)((int)target + 2); }
                                else             { *(byte *)target = inChar[0]; target = (int *)((int)target + 1); }
                            }
                            produced++;
                        }
                        if (produced == 0) goto at_eof;
                        consumed += produced;
                        if (conv != 'c' && target != (int *)0) {
                            if (lenmod == 7) *(u16 *)target = 0; else *(u8 *)target = 0;
                        }
                        if (target != (int *)0) matched++;
                        seen++;
                        continue;
                    }
                    if (conv == 0x25) {                // literal %%
                        do { inChar[0] = (byte)src(state, 0, 0); }
                        while ((DAT_80497890[inChar[0]] & 6) != 0 && (consumed++, 1));
                        if ((char)inChar[0] != 0x25) { src(state, (int)(char)inChar[0], 1); goto at_eof; }
                        consumed++;
                        continue;
                    }
                    goto at_eof;                       // unknown conversion
                }
            }
            // literal character match
            inChar[0] = (byte)src(state, 0, 0);
            if ((uint)fc != (uint)(char)inChar[0]) {
                src(state, (int)(char)inChar[0], 1);
                goto at_eof;
            }
            consumed++;
            fmt = fmt + 1;
            continue;
        }
        // whitespace run in the format: skip format whitespace, then skip input whitespace
        do { fmt = fmt + 1; } while ((DAT_80497890[*fmt] & 6) != 0);
        while (inChar[0] = (byte)src(state, 0, 0), (DAT_80497890[inChar[0]] & 6) != 0) {
            consumed++;
        }
        src(state, (int)(char)inChar[0], 1);
    }
    (void)dAcc; (void)fAcc; (void)ok;
}

// ---------------------------------------------------------------------------
// msl_sscanf (0x8038a210) - scan a formatted NUL-terminated string. Sets up a
// string char-source over `str` and runs the scan engine. Returns the assignment
// count, or -1 if the string is empty/NULL.
int FUN_8038a210(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                 char *str, byte *fmt, u32 va0, u32 va1, u32 va2, u32 va3, u32 va4, u32 va5)
{
    int   result;
    char  scanSpec[4];
    char *ctx[3];    // {char *cursor; ...} scan-source state
    u32   vaSpill[8];

    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    vaSpill[0] = va0; vaSpill[1] = va1; vaSpill[2] = va2; vaSpill[3] = va3;
    vaSpill[4] = va4; vaSpill[5] = va5; vaSpill[6] = 0; vaSpill[7] = 0;

    scanSpec[0] = '\x02';
    scanSpec[1] = '\0'; scanSpec[2] = '\0'; scanSpec[3] = '\0';
    if ((str == (char *)0x0) || (*str == '\0')) {
        result = -1;
    }
    else {
        ctx[0] = str;
        result = FUN_8038a364((void *)&LAB_8038a2d4, 0, fmt, (char *)ctx);
    }
    return result;
}

// Address: 0x8038ad10 | Name: msl_parse_scan_spec | Date: 2026-09-13
//
// msl_parse_scan_spec (0x8038ad10) - parse one scanf %-conversion starting at
// fmtAtPercent into the 40-byte scan-spec record *specOut. The record is seeded
// from a template (DAT_803e1290, 10 words); the tail (offset 8..39) is a 256-bit
// scanset bitmap used by %[ and pre-loaded by %s/%c defaults. Layout:
//   +0  suppress flag ('*' -> 1) ; +2 length-modifier code ; +3 conversion char
//   +4  field width (int) ; +8.. 32-byte accept/reject bitmap
// Returns the format pointer just past the conversion.
//
// Modelled on a byte view of the record to preserve the original's packed field
// writes exactly; the bitmap set/range logic for %[ is reproduced verbatim.

extern u32 DAT_803e1290, DAT_803e1294, DAT_803e1298, DAT_803e129c, DAT_803e12a0;
extern u32 DAT_803e12a4, DAT_803e12a8, DAT_803e12ac, DAT_803e12b0, DAT_803e12b4;

byte *FUN_8038ad10(int fmtAtPercent, undefined4 *specOut)
{
    byte *p;
    byte *pPrev;
    uint  ch;
    int   width;
    bool  scanning;
    bool  negate;
    int   hi;
    // 40-byte working record (word view for the template copy, byte view for fields)
    u32   recW[10];
    u8   *r = (u8 *)recW;

    // seed from the template
    recW[0] = DAT_803e1290; recW[1] = DAT_803e1294; recW[2] = DAT_803e1298;
    recW[3] = DAT_803e129c; recW[4] = DAT_803e12a0; recW[5] = DAT_803e12a4;
    recW[6] = DAT_803e12a8; recW[7] = DAT_803e12ac; recW[8] = DAT_803e12b0;
    recW[9] = DAT_803e12b4;

    p  = (byte *)(fmtAtPercent + 1);
    ch = (uint)(char)*p;

    if (ch == 0x25) {                         // "%%"
        r[0] = (u8)ch;                        // conversion char stored at record[0]
        for (int i = 0; i < 10; i++) specOut[i] = recW[i];
        return (byte *)(fmtAtPercent + 2);
    }

    // '*' assignment-suppression
    if (ch == 0x2a) {
        p = (byte *)(fmtAtPercent + 2);
        r[3] = 1;                             // suppress flag (byte3 of word0 in template terms)
        ch = (uint)(char)*p;
    }

    // field width
    if ((DAT_80497890[ch & 0xff] & 0x10) != 0) {
        width = 0;
        do {
            pPrev = p;
            p = pPrev + 1;
            width = ch + width * 10 + -0x30;
            ch = (uint)(char)*p;
        } while ((DAT_80497890[*p] & 0x10) != 0);
        if (width == 0) {
            r[0] = 0xff;                       // invalid conversion
            for (int i = 0; i < 10; i++) specOut[i] = recW[i];
            specOut[1] = 0;
            return pPrev + 2;
        }
        recW[1] = (u32)width;                  // record+4 = width
        r[2] = 1;                              // width-present marker (byte2)
    }

    // length modifier
    scanning = true;
    if (ch == 0x68) {                          // h / hh
        *(u16 *)(r + 2) = 0x200;
        if (p[1] == 0x68) { *(u16 *)(r + 2) = 0x100; p = p + 1; ch = 0x68; }
    }
    else if (ch == 0x4c) { *(u16 *)(r + 2) = 0x600; }   // L
    else if (ch == 0x6c) {                     // l / ll
        *(u16 *)(r + 2) = 0x300;
        if (p[1] == 0x6c) { *(u16 *)(r + 2) = 0x400; p = p + 1; ch = 0x6c; }
    }
    else scanning = false;
    if (scanning) { p = p + 1; ch = (uint)(char)*p; }

    // conversion char + validation/defaulting
    r[3] = (u8)ch;
    u8 lenmod = r[2];   // low byte of the length-modifier code
    switch (ch) {
    case 'E': case 'G': case 'a': case 'e': case 'f': case 'g':
        if ((lenmod == 1) || (lenmod == 2) || (lenmod == 4)) r[0] = 0xff;
        else if (lenmod == 3) r[3] = 5;
        break;
    case 'X': case 'd': case 'i': case 'o': case 'u': case 'x':
        if (lenmod == 6) r[0] = 0xff;
        break;
    case 0x5b:                                 // %[ scanset
        if (lenmod == 3) r[3] = 7;
        else if (lenmod != 0) r[0] = 0xff;
        ch = (uint)(char)p[1];
        negate = (ch == 0x5e);
        pPrev = p + 1;
        if (negate) { ch = (uint)(char)p[2]; pPrev = p + 2; }
        p = pPrev;
        if (ch == 0x5d) {                      // literal ']' as first set member
            p = p + 1;
            r[16] = r[16] | 0x20;              // set bit for ']' in the bitmap (byte at +8+8)
            ch = (uint)(char)*p;
        }
        while ((ch != 0) && (ch != 0x5d)) {
            uint idx = ch >> 3 & 0x1f;
            byte next = p[1];
            r[8 + idx] = r[8 + idx] | (byte)(1 << (ch & 7));
            if ((next == 0x2d) && (hi = (int)(char)p[2], hi != 0 && (hi != 0x5d))) {
                while (ch = ch + 1, (int)ch <= hi) {
                    idx = ch >> 3 & 0x1f;
                    r[8 + idx] = r[8 + idx] | (byte)(1 << (ch & 7));
                }
                p = p + 3;
                ch = (uint)(char)*p;
            }
            else {
                p = p + 1;
                ch = (uint)(char)*p;
            }
        }
        if (ch == 0) r[0] = 0xff;
        else if (negate) {
            for (int i = 8; i < 40; i++) r[i] = (byte)~r[i];
        }
        break;
    case 'c':
        if (lenmod == 3) r[3] = 7;
        else if (lenmod != 0) r[0] = 0xff;
        break;
    case 'n':
        break;
    case 'p':
        *(u16 *)(r + 2) = 0x300;
        r[3] = 0x78;                           // treat as 'x'
        break;
    case 's':
        if (lenmod == 3) r[3] = 7;
        else if (lenmod != 0) r[0] = 0xff;
        // %s accept set: all bytes, then clear the whitespace class bits so the
        // scan stops at whitespace (matches the original's two byte pokes).
        for (int i = 8; i < 40; i++) r[i] = 0xff;
        r[9]  = 0xc1;   // record+9: clear space/tab/newline group in the bitmap
        r[15] = 0xfe;   // record+15: clear the low control bit
        break;
    default:
        r[0] = 0xff;
        break;
    }

    for (int i = 0; i < 10; i++) specOut[i] = recW[i];
    return p + 1;
}
