// os_font.cpp - Dolphin OS built-in debug/error-screen font subsystem
//
// Address: 0x8039c114 | Name: os_font_char_to_glyph  | Date: 2026-09-14
// Address: 0x8039c288 | Name: os_font_lz_decode      | Date: 2026-09-14
// Address: 0x8039c3fc | Name: os_font_detect_encoding | Date: 2026-09-14
// Address: 0x8039c460 | Name: os_font_load_sheet     | Date: 2026-09-14
// Address: 0x8039c784 | Name: os_font_init           | Date: 2026-09-14
// Address: 0x8039c8c4 | Name: os_font_decode_char    | Date: 2026-09-14
// Address: 0x8039ca00 | Name: os_font_get_glyph      | Date: 2026-09-14
//
// The OS ships a compressed font ROM (a Yay0-style "Yay0" archive of a paletted
// 4bpp tiled glyph sheet) used by the low-level console/error screen when no game
// renderer is up. os_font_init detects the encoding (single-byte ANSI vs
// Shift-JIS, chosen from the console region) and decompresses the matching sheet
// into RAM; os_font_decode_char (installed at DAT_806130a8) advances the text
// pointer one code point and picks the sheet; os_font_char_to_glyph maps a code
// point to a glyph index; os_font_get_glyph composites one glyph's 4bpp tile into
// the caller's texture (this is the decoder os_console_draw_text drives).
//
// NON-MATCHING: the sheet-header field offsets, the CMPR/paletted tile-swizzle
// index arithmetic, and the Yay0 LZ back-reference loop are transcribed verbatim
// from the decompiler; C4311/C4312 pointer-width warnings only.

#include "common.h"

extern "C" {
extern int    DAT_800000cc;      // "console has bootrom font override" flag
extern ushort DAT_cc00206e;      // VI display-config register (region/progressive bits)
extern ushort DAT_806113e8;      // cached encoding id (0=ANSI,1=SJIS,...; 0xffff=unknown)
extern void  *DAT_806130a8;      // installed codec vtable (os_font_decode_char)
extern int    DAT_806130a0;      // decompressed ANSI sheet base
extern int    DAT_806130a4;      // decompressed SJIS sheet base
extern undefined1 PTR_caseD_5_80498a28; // ASCII->glyph index table
extern undefined1 DAT_80498be8;          // SJIS->glyph index table
extern int    DAT_8061bfe0;      // test-pixel colour word A
extern int    DAT_8061bfe4;      // test-pixel colour word B

// Font-ROM flash/EXI block reader (returns nonzero on success).
extern uint FUN_8039e9b8(uint dst, int len, int romOffset);
}

// Forward declarations for intra-file references (definitions below).
uint  os_font_char_to_glyph(short mode, uint code);
void  os_font_lz_decode(int src, int dst);
short os_font_detect_encoding(void);
int   os_font_load_sheet(char *buf, short sheet, int outBase);
int   os_font_init(int sheetBuf, char *romBuf);
byte *os_font_decode_char(short mode, byte *text, undefined4 *outSheet, uint *outGlyph,
                          undefined4 param_5, undefined4 sheet);
undefined4 os_font_get_glyph(undefined4 text, int tex, int col, uint orient, uint *outWidth);

// Map an ASCII (mode 0) or Shift-JIS (mode 1) code point to a glyph index into
// the decompressed sheet; 0 when the code has no glyph.
uint os_font_char_to_glyph(short mode, uint code)
{
    bool inHiRange;
    bool valid;
    uint c;
    uint lo;
    int col;

    if (mode == 1) {
        c = code & 0xffff;
        if ((0x1f < c) && (c < 0xe0)) {
            return (uint)*(ushort *)((int)&PTR_caseD_5_80498a28 + c * 2);
        }
        c = code & 0xffff;
        if ((0x889e < c) && (c < 0x9873)) {
            lo = code & 0xff;
            valid = false;
            inHiRange = false;
            if ((0x3f < lo) && (lo < 0xfd)) {
                inHiRange = true;
            }
            if ((inHiRange) && (lo != 0x7f)) {
                valid = true;
            }
            if (!valid) {
                return 0;
            }
            col = lo - 0x40;
            if (0x3f < col) {
                col = lo - 0x41;
            }
            return (((int)c >> 8) + -0x88) * 0xbc + col + 0x2be;
        }
        c = code & 0xffff;
        if ((0x813f < c) && (c < 0x879e)) {
            lo = code & 0xff;
            valid = false;
            inHiRange = false;
            if ((0x3f < lo) && (lo < 0xfd)) {
                inHiRange = true;
            }
            if ((inHiRange) && (lo != 0x7f)) {
                valid = true;
            }
            if (!valid) {
                return 0;
            }
            col = lo - 0x40;
            if (0x3f < col) {
                col = lo - 0x41;
            }
            return (uint)*(ushort *)(&DAT_80498be8 + ((((int)c >> 8) + -0x81) * 0xbc + col) * 2);
        }
    } else {
        c = code & 0xffff;
        if ((0x20 < c) && (c < 0x100)) {
            return c - 0x20;
        }
    }
    return 0;
}

// Yay0-style LZ decompressor: src is the archive base (header at +4 gives the
// decompressed size, +8 the code-run offset, +0xc the counts offset), dst the
// output buffer.
void os_font_lz_decode(int src, int dst)
{
    byte *pLit;
    uint runLen;
    uint *pMask;
    ushort *pRef;
    undefined1 *pOut;
    int i6;
    int i7;
    int back;
    int outPos;
    int refPos;
    int countPos;
    uint mask;
    int outSize;
    int block;
    uint copies;

    pMask = (uint *)(src + 0x10);
    outPos = 0;
    block = 0;
    outSize = *(int *)(src + 4);
    refPos = *(int *)(src + 8);
    countPos = *(int *)(src + 0xc);
    do {
        if (block == 0) {
            mask = *pMask;
            block = 0x20;
            pMask = pMask + 1;
        }
        if ((mask & 0x80000000) == 0) {
            pRef = (ushort *)(src + refPos);
            refPos = refPos + 2;
            back = outPos - ((*(byte *)pRef & 0xf) << 8 | (uint)(byte)*pRef);
            if ((int)(uint)*pRef >> 0xc == 0) {
                pLit = (byte *)(src + countPos);
                countPos = countPos + 1;
                runLen = *pLit + 0x12;
            } else {
                runLen = ((int)(uint)*pRef >> 0xc) + 2;
            }
            pOut = (undefined1 *)(dst + outPos);
            if (runLen != 0) {
                copies = runLen >> 3;
                if (copies != 0) {
                    do {
                        outPos = outPos + 8;
                        *pOut = *(undefined1 *)(dst + back + -1);
                        pOut[1] = *(undefined1 *)(dst + back);
                        pOut[2] = *(undefined1 *)(dst + back + 1);
                        pOut[3] = *(undefined1 *)(dst + back + 2);
                        pOut[4] = *(undefined1 *)(dst + back + 3);
                        i6 = back + 5;
                        pOut[5] = *(undefined1 *)(dst + back + 4);
                        i7 = back + 6;
                        back = back + 8;
                        pOut[6] = *(undefined1 *)(dst + i6);
                        pOut[7] = *(undefined1 *)(dst + i7);
                        pOut = pOut + 8;
                        copies = copies - 1;
                    } while (copies != 0);
                    runLen = runLen & 7;
                    if (runLen == 0)
                        goto next;
                }
                do {
                    i6 = back + -1;
                    outPos = outPos + 1;
                    back = back + 1;
                    *pOut = *(undefined1 *)(dst + i6);
                    pOut = pOut + 1;
                    runLen = runLen - 1;
                } while (runLen != 0);
            }
        } else {
            *(undefined1 *)(dst + outPos) = *(undefined1 *)(src + countPos);
            countPos = countPos + 1;
            outPos = outPos + 1;
        }
    next:
        mask = mask << 1;
        block = block + -1;
        if (outSize <= outPos) {
            return;
        }
    } while (true);
}

// Detect the font encoding from the console flags/region, cache it, and install
// the codec vtable. Returns the encoding id.
short os_font_detect_encoding(void)
{
    ushort viCfg;

    if (DAT_806113e8 != 0xffff) {
        return (short)DAT_806113e8;
    }
    if (DAT_800000cc == 0) {
        viCfg = DAT_cc00206e;
        DAT_806113e8 = (ushort)((viCfg & 2) != 0);
    } else {
        DAT_806113e8 = 0;
    }
    DAT_806130a8 = (void *)os_font_decode_char;
    return (short)DAT_806113e8;
}

// Read + decompress one font sheet (sheet==1: SJIS, else ANSI) from the font ROM
// via the flash reader, verify the "Yay0" magic, LZ-decode it, and draw the test
// pixel. Returns the decompressed byte count (0 on bad magic).
int os_font_load_sheet(char *buf, short sheet, int outBase)
{
    int colorB;
    int colorA;
    uint got;
    uint tx;
    int px;
    uint ty;
    int step;
    int remain;
    int romOff;
    int outSize;
    char *p;

    if (sheet == 1) {
        romOff = 0x1aff00;
        remain = 0x4d000;
        p = buf;
        while (0 < remain) {
            step = remain;
            if (0x100 < remain) {
                step = 0x100;
            }
            remain = remain - step;
            do {
                got = FUN_8039e9b8((uint)p, step, romOff);
            } while (got == 0);
            romOff = romOff + step;
            p = p + step;
        }
    } else {
        remain = 0x1fcf00;
        romOff = 0x3000;
        p = buf;
        while (0 < romOff) {
            step = romOff;
            if (0x100 < romOff) {
                step = 0x100;
            }
            romOff = romOff - step;
            do {
                got = FUN_8039e9b8((uint)p, step, remain);
            } while (got == 0);
            remain = remain + step;
            p = p + step;
        }
    }

    if (((*buf == 'Y') && (buf[1] == 'a')) && (buf[2] == 'y')) {
        outSize = *(int *)(buf + 4);
    } else {
        outSize = 0;
    }
    if (outSize == 0) {
        outSize = 0;
    } else {
        os_font_lz_decode((int)buf, outBase);
        colorB = DAT_8061bfe4;
        colorA = DAT_8061bfe0;
        if (sheet == 1) {
            // Draw a single test glyph ('T', 0x54) into the sheet as a sanity mark.
            got = os_font_char_to_glyph(1, 0x54);
            tx = (uint)*(ushort *)(outBase + 0x1a);
            int cells = tx * *(ushort *)(outBase + 0x1c);
            int cell = (int)got / cells;
            int rem = got - cell * cells;
            ty = rem / (int)tx;
            int col = ty * (uint)*(ushort *)(outBase + 0x12);
            uint row = (rem - ty * tx) * (uint)*(ushort *)(outBase + 0x10);
            uint c3 = col + 4;
            uint rmod = row + (((int)row >> 3) + (uint)((int)row < 0 && (row & 7) != 0)) * -8;
            int base = outBase + *(int *)(outBase + 0x24) + ((uint)(cell * *(int *)(outBase + 0x14)) >> 1);
            int rblk = (((int)row >> 3) + (uint)((int)row < 0 && (row & 7) != 0)) * 0x10;
            int rq = ((int)rmod >> 2) + (uint)((int)rmod < 0 && (rmod & 3) != 0);
            int stride = ((int)(uint)*(ushort *)(outBase + 0x1e) >> 3) * 0x20 >> 1;
            short a0 = (short)((uint)colorA >> 0x10);
            short a1 = (short)colorA;
            short b0 = (short)((uint)colorB >> 0x10);
            short b1 = (short)colorB;
            *(short *)(base + stride * (((int)c3 >> 3) + (uint)((int)c3 < 0 && (c3 & 7) != 0)) + rblk
                       + (c3 + (((int)c3 >> 3) + (uint)((int)c3 < 0 && (c3 & 7) != 0)) * -8) * 2 + rq) = a0;
            c3 = col + 5;
            *(short *)(base + stride * (((int)c3 >> 3) + (uint)((int)c3 < 0 && (c3 & 7) != 0)) + rblk
                       + (c3 + (((int)c3 >> 3) + (uint)((int)c3 < 0 && (c3 & 7) != 0)) * -8) * 2 + rq) = a1;
            c3 = col + 6;
            *(short *)(base + stride * (((int)c3 >> 3) + (uint)((int)c3 < 0 && (c3 & 7) != 0)) + rblk
                       + (c3 + (((int)c3 >> 3) + (uint)((int)c3 < 0 && (c3 & 7) != 0)) * -8) * 2 + rq) = b0;
            c3 = col + 7;
            *(short *)(base + stride * (((int)c3 >> 3) + (uint)((int)c3 < 0 && (c3 & 7) != 0)) + rblk
                       + (c3 + (((int)c3 >> 3) + (uint)((int)c3 < 0 && (c3 & 7) != 0)) * -8) * 2 + rq) = b1;
            (void)px;
        }
    }
    return outSize;
}

// OSGetFontTexture-style init: detect encoding, then decompress the ANSI and/or
// SJIS sheets into the caller-provided buffer. Returns total decompressed bytes.
int os_font_init(int sheetBuf, char *romBuf)
{
    ushort viCfg;
    int n;
    int n2;

    if (DAT_806113e8 == 0xffff) {
        if (DAT_800000cc == 0) {
            viCfg = DAT_cc00206e;
            DAT_806113e8 = (ushort)((viCfg & 2) != 0);
        } else {
            DAT_806113e8 = 0;
        }
        DAT_806130a8 = (void *)os_font_decode_char;
    }
    if (DAT_806113e8 != 2) {
        if (DAT_806113e8 < 2) {
            if (DAT_806113e8 != 0) {
                DAT_806130a4 = sheetBuf;
                return os_font_load_sheet(romBuf, 1, sheetBuf);
            }
            DAT_806130a0 = sheetBuf;
            return os_font_load_sheet(romBuf, 0, sheetBuf);
        }
        if (DAT_806113e8 < 6) {
            DAT_806130a0 = sheetBuf;
            n = os_font_load_sheet(romBuf, 0, sheetBuf);
            if (n == 0) {
                return 0;
            }
            DAT_806130a4 = DAT_806130a0 + n;
            n2 = os_font_load_sheet(romBuf, 1, DAT_806130a4);
            return n + n2;
        }
    }
    return 0;
}

// Codec (installed at DAT_806130a8): read one code point from text (single-byte
// ANSI or double-byte SJIS), select the active sheet into *outSheet, resolve the
// glyph index into *outGlyph, and return the advanced text pointer.
byte *os_font_decode_char(short mode, byte *text, undefined4 *outSheet, uint *outGlyph,
                          undefined4 param_5, undefined4 sheet)
{
    byte hi;
    byte lo;
    bool leadValid;
    bool inRange;
    uint code;
    byte *next;

    code = 0;
    next = text;
    if (mode == 1) {
        hi = *text;
        code = (uint)hi;
        sheet = (undefined4)DAT_806130a4;
        if (code != 0) {
            leadValid = true;
            inRange = false;
            next = text + 1;
            if ((0x80 < hi) && (hi < 0xa0)) {
                inRange = true;
            }
            if (!inRange) {
                inRange = false;
                if ((0xdf < hi) && (hi < 0xfd)) {
                    inRange = true;
                }
                if (!inRange) {
                    leadValid = false;
                }
            }
            if (leadValid) {
                lo = *next;
                leadValid = false;
                inRange = false;
                if ((0x3f < lo) && (lo < 0xfd)) {
                    inRange = true;
                }
                if ((inRange) && (lo != 0x7f)) {
                    leadValid = true;
                }
                if (leadValid) {
                    lo = *next;
                    next = text + 2;
                    code = (uint)(ushort)((hi << 8) | lo);
                }
            }
        }
    } else if ((mode == 0) && (code = (uint)*text, sheet = (undefined4)DAT_806130a0, code != 0)) {
        next = text + 1;
    }
    *outSheet = sheet;
    code = os_font_char_to_glyph(mode, code);
    *outGlyph = code;
    return next;
}

// Composite one glyph's 4bpp CMPR tile from the active sheet into the caller's
// texture at (param_3, param_2) with orientation param_4; writes the advance
// width to *outWidth. Returns the codec's sheet handle. This is the decoder
// os_console_draw_text drives.
undefined4 os_font_get_glyph(undefined4 text, int tex, int col, uint orient, uint *outWidth)
{
    ushort viCfg;
    uint packedOrient;
    undefined4 sheetHandle;
    uint tilesW;
    int dataOff;
    int cells;
    int stride;
    ushort cellH;
    ushort cellW;
    int cellRow;
    int inCell;
    int cellCol;
    uint gx;
    int px;
    uint gy;
    int py;
    uint colByte;
    int sheet;
    int glyph;
    byte nib;

    if (DAT_806113e8 == 0xffff) {
        if (DAT_800000cc == 0) {
            viCfg = DAT_cc00206e;
            DAT_806113e8 = (ushort)((viCfg & 2) != 0);
        } else {
            DAT_806113e8 = 0;
        }
        DAT_806130a8 = (void *)os_font_decode_char;
    }
    sheetHandle = ((undefined4(*)(ushort, undefined4, int *, int *))DAT_806130a8)(
        DAT_806113e8, text, &sheet, &glyph);
    packedOrient = orient << 2 | orient >> 0x1e;
    tilesW = (uint)*(ushort *)(sheet + 0x1a);
    dataOff = *(int *)(sheet + 0x24);
    cells = tilesW * *(ushort *)(sheet + 0x1c);
    stride = *(int *)(sheet + 0x14);
    cellH = *(ushort *)(sheet + 0x12);
    cellW = *(ushort *)(sheet + 0x10);
    cellRow = glyph / cells;
    inCell = glyph - cellRow * cells;
    cellCol = inCell / (int)tilesW;

    for (gy = 0; (int)gy < (int)(uint)*(ushort *)(sheet + 0x12); gy = gy + 1) {
        uint srcRow = cellCol * (uint)cellH + gy;
        for (py = 0; py < (int)(uint)*(ushort *)(sheet + 0x10); py = py + 1) {
            uint srcCol = (inCell - cellCol * tilesW) * (uint)cellW + py;
            gx = col + py;
            uint sc8 = srcCol + (((int)srcCol >> 3) + (uint)((int)srcCol < 0 && (srcCol & 7) != 0)) * -8;
            uint gx8 = gx + (((int)gx >> 3) + (uint)((int)gx < 0 && (gx & 7) != 0)) * -8;
            byte *pDst = (byte *)(tex + (((int)gy >> 3) + (uint)((int)gy < 0 && (gy & 7) != 0))
                                        * (((int)packedOrient >> 3)
                                           + (uint)((int)packedOrient < 0 && (orient << 2 & 4) != 0)) * 0x20
                                  + (((int)gx >> 3) + (uint)((int)gx < 0 && (gx & 7) != 0)) * 0x20
                                  + (gy + (((int)gy >> 3) + (uint)((int)gy < 0 && (gy & 7) != 0)) * -8) * 4
                                  + ((int)gx8 >> 1) + (uint)((int)gx8 < 0 && (gx8 & 1) != 0));
            if (gx == (((int)gx >> 1) + (uint)((int)gx < 0 && (gx & 1) != 0)) * 2) {
                nib = 0xf0;
            } else {
                nib = 0xf;
            }
            *pDst = *pDst |
                    *(byte *)(sheet + 0x2c +
                              ((int)(uint)*(byte *)(sheet + dataOff + ((uint)(cellRow * stride) >> 1)
                                                    + (((int)(uint)*(ushort *)(sheet + 0x1e) >> 3) * 0x20 >> 1)
                                                          * (((int)srcRow >> 3)
                                                             + (uint)((int)srcRow < 0 && (srcRow & 7) != 0))
                                                    + (((int)srcCol >> 3)
                                                       + (uint)((int)srcCol < 0 && (srcCol & 7) != 0)) * 0x10
                                                    + (srcRow + (((int)srcRow >> 3)
                                                                 + (uint)((int)srcRow < 0 && (srcRow & 7) != 0)) * -8) * 2
                                                    + ((int)sc8 >> 2)
                                                    + (uint)((int)sc8 < 0 && (sc8 & 3) != 0))
                               >> ((srcCol + (((int)srcCol >> 2)
                                              + (uint)((int)srcCol < 0 && (srcCol & 3) != 0)) * -4) * -2 + 6
                                   & 0x3f)
                               & 3U)) & nib;
        }
    }
    if (outWidth != (uint *)0x0) {
        *outWidth = (uint)*(byte *)(glyph + sheet + (uint)*(ushort *)(sheet + 0x22));
    }
    (void)colByte;
    return sheetHandle;
}
