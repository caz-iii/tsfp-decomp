// THP video intra-frame JPEG (JFIF baseline) decoder - header/table parsing.
//
// The GameCube THP movie format stores each video frame as a baseline JPEG
// intra-frame. jpeg_parse_headers (called from thp_decode_video) walks the
// marker stream, dispatching SOF0 / DHT / DQT / SOS / DRI, building the
// AAN-scaled dequant tables and the canonical Huffman decode tables, then runs
// the scan decoder (FUN_803bd358, decompiled separately).
//
// Decoder state layout (allocated from the decoder pool at DAT_80613570,
// ~0x6bc bytes; field offsets are the raw ones the decompiler produced):
//   +0x340 (stride 0xE0) per-Huffman-table decode structures
//   +0x680.. quant/component scratch, +0x692 width, +0x694 height,
//   +0x696 MCU rows, +0x698 precision, +0x69c the current input cursor,
//   +0x6a4 magic 0x21, +0x6a8 defined-table bitmask, +0x6a9 restart-enable,
//   +0x6aa/+0x6ac restart interval.
//
// NON-MATCHING: the flat DAT_80613570+offset / DAT_8060edxx / DAT_803e1exx
// pointer arithmetic, the quant-table de-zigzag + AAN prescale unrolled by 8,
// the (double)CONCAT44(0x43300000,x) integer->double magic-constant conversions,
// and the raw JPEG marker/error-code literals are kept verbatim from the
// decompiler output.
//
// Batch: batch_1156
// Source module: video/thp_jpeg.cpp
#include "common.h"

// ---------------------------------------------------------------------------
// SDK / OS helpers.
// ---------------------------------------------------------------------------
extern uint os_get_hid2(void);                 // read HID2 (locked-cache enable bit 0x10000000)
extern void os_dc_zero_range(uint addr, int len);

// ---------------------------------------------------------------------------
// Decoder pool + derived pointers (raw globals).
// ---------------------------------------------------------------------------
extern uint DAT_80613570;   // decoder state base (allocated per-frame)
extern uint DAT_8061356c;   // end of decoder state / start of Huffman code area
extern uint DAT_80613574;   // pool availability flag (0 = not initialized)

// jpeg_setup_workbuffers outputs (six 0x80-byte MCU/component work buffers).
extern uint DAT_8060ed18;
extern uint DAT_8060ed1c;
extern uint DAT_8060ed20;
extern uint DAT_8060ed24;
extern uint DAT_8060ed28;
extern uint DAT_8060ed2c;

// Huffman-table build scratch (set up per-table by jpeg_parse_dht).
extern uint  DAT_80613504;  // ptr to the 16 bits-per-length counts + value bytes
extern char *DAT_80613508;  // HUFFSIZE code-size list buffer
extern int   DAT_8061350c;  // HUFFCODE canonical code buffer (u16 per symbol)

// Const tables in rodata.
extern byte   DAT_803e1ea8; // JPEG natural (de-zigzag) order, 64 bytes
extern double DAT_803e1ef8; // AAN IDCT prescale factors (8 doubles, one per row/col)
extern double DAT_803e1f00;
extern double DAT_803e1f08;
extern double DAT_803e1f10;
extern double DAT_803e1f18;
extern double DAT_803e1f20;
extern double DAT_803e1f28;
extern double DAT_803e1f30;
extern double DOUBLE_8061c028; // 2^52 + 2^31 bias constant for int->double conversion

// Forward declarations for the header/table helpers defined in this file.
void jpeg_scan_decode(int comp0, int comp1, int comp2);  // FUN_803bd358 (batch_1157)
undefined4 jpeg_parse_sof(void);
undefined4 jpeg_parse_sos(void);
undefined4 jpeg_build_dequant_tables(void);
undefined4 jpeg_parse_dht(void);
void       jpeg_parse_dri(void);
void       jpeg_setup_workbuffers(void);
void       jpeg_huff_expand_sizes(void);
void       jpeg_huff_gen_codes(void);
void       jpeg_huff_build_lookup(uint slot);

// ---------------------------------------------------------------------------
// jpeg_parse_headers - top-level JFIF marker parser (see file header).
// ---------------------------------------------------------------------------
uint jpeg_parse_headers(int buf, int outY, int outCb, int outCr, int poolSize)
{
    byte  bVar1;
    bool  bVar2;
    uint  uVar3;
    char *pcVar4;
    byte *pbVar5;
    int  *piVar6;

    if (buf == 0) {
        uVar3 = 0x19;
    }
    else if (((outY == 0) || (outCb == 0)) || (outCr == 0)) {
        uVar3 = 0x1b;
    }
    else if (poolSize == 0) {
        uVar3 = 0x1a;
    }
    else {
        uVar3 = os_get_hid2();
        if ((uVar3 & 0x10000000) == 0) {
            uVar3 = 0x1c;
        }
        else if (DAT_80613574 == 0) {
            uVar3 = 0x1d;
        }
        else {
            DAT_80613570 = poolSize + 0x1fU & 0xffffffe0;
            DAT_8061356c = DAT_80613570 + 0x6bc;
            os_dc_zero_range(DAT_80613570, 0x6bc);
            *(undefined4 *)(DAT_80613570 + 0x6a4) = 0x21;
            bVar2 = false;
            *(undefined2 *)(DAT_80613570 + 0x698) = 0;
            *(int *)(DAT_80613570 + 0x69c) = buf;
            do {
                pcVar4 = *(char **)(DAT_80613570 + 0x69c);
                *(char **)(DAT_80613570 + 0x69c) = pcVar4 + 1;
                if (*pcVar4 != -1) {
                    return 3;
                }
                while (true) {
                    piVar6 = (int *)(DAT_80613570 + 0x69c);
                    pbVar5 = *(byte **)(DAT_80613570 + 0x69c);
                    if (*pbVar5 != 0xff) break;
                    *piVar6 = *piVar6 + 1;
                }
                *piVar6 = (int)(pbVar5 + 1);
                bVar1 = *pbVar5;
                if (bVar1 < 0xd8) {
                    if (bVar1 == 0xc4) {
                        uVar3 = jpeg_parse_dht();
                    }
                    else {
                        if (bVar1 != 0xc0) {
                            return 0xb;
                        }
                        uVar3 = jpeg_parse_sof();
                    }
                    if ((uVar3 & 0xff) != 0) {
                        return uVar3 & 0xff;
                    }
                }
                else if ((bVar1 < 0xd8) || (0xdf < bVar1)) {
                    if (0xdf < bVar1) {
                        if ((0xef < bVar1) && (bVar1 != 0xfe)) {
                            return 0xb;
                        }
                        // Skip a length-prefixed marker segment (APPn / COM).
                        *(uint *)(DAT_80613570 + 0x69c) =
                            (int)*(ushort **)(DAT_80613570 + 0x69c) +
                            (uint)**(ushort **)(DAT_80613570 + 0x69c);
                    }
                }
                else if (bVar1 == 0xdd) {
                    jpeg_parse_dri();
                }
                else {
                    if (bVar1 == 0xdb) {
                        uVar3 = jpeg_build_dequant_tables();
                        if ((uVar3 & 0xff) != 0) {
                            return uVar3 & 0xff;
                        }
                    }
                    else if (bVar1 == 0xda) {
                        uVar3 = jpeg_parse_sos();
                        if ((uVar3 & 0xff) != 0) {
                            return uVar3 & 0xff;
                        }
                        bVar2 = true;
                    }
                    else if (bVar1 != 0xd8) {
                        return 0xb;
                    }
                }
            } while (!bVar2);
            jpeg_setup_workbuffers();
            jpeg_scan_decode(outY, outCb, outCr);
            uVar3 = 0;
        }
    }
    return uVar3;
}

// ---------------------------------------------------------------------------
// jpeg_setup_workbuffers - carve the six 0x80-byte MCU/component work buffers
// out of the decoder pool tail (32-byte aligned).
// ---------------------------------------------------------------------------
void jpeg_setup_workbuffers(void)
{
    DAT_8060ed18 = DAT_8061356c + 0x1fU & 0xffffffe0;
    DAT_8060ed1c = DAT_8060ed18 + 0x80;
    DAT_8060ed20 = DAT_8060ed18 + 0x100;
    DAT_8060ed24 = DAT_8060ed18 + 0x180;
    DAT_8060ed28 = DAT_8060ed18 + 0x200;
    DAT_8060ed2c = DAT_8060ed18 + 0x280;
    return;
}

// ---------------------------------------------------------------------------
// jpeg_parse_sof - parse the SOF0 baseline frame header (precision must be 8,
// three YCbCr components). Returns 0 ok / 0xa bad precision / 0xc bad component
// count / 0x13 bad component id.
// ---------------------------------------------------------------------------
undefined4 jpeg_parse_sof(void)
{
    char cVar1;
    int  iVar2;
    char *pcVar3;
    undefined1 *puVar4;
    int  iVar5;
    byte bVar6;

    *(int *)(DAT_80613570 + 0x69c) = *(int *)(DAT_80613570 + 0x69c) + 2;
    pcVar3 = *(char **)(DAT_80613570 + 0x69c);
    *(char **)(DAT_80613570 + 0x69c) = pcVar3 + 1;
    if (*pcVar3 != '\b') {
        return 10;
    }
    *(undefined2 *)(DAT_80613570 + 0x694) = **(undefined2 **)(DAT_80613570 + 0x69c);
    *(int *)(DAT_80613570 + 0x69c) = *(int *)(DAT_80613570 + 0x69c) + 2;
    *(undefined2 *)(DAT_80613570 + 0x692) = **(undefined2 **)(DAT_80613570 + 0x69c);
    *(int *)(DAT_80613570 + 0x69c) = *(int *)(DAT_80613570 + 0x69c) + 2;
    pcVar3 = *(char **)(DAT_80613570 + 0x69c);
    *(char **)(DAT_80613570 + 0x69c) = pcVar3 + 1;
    if (*pcVar3 != '\x03') {
        return 0xc;
    }
    bVar6 = 0;
    iVar5 = 0;
    while (true) {
        if (2 < bVar6) {
            return 0;
        }
        *(int *)(DAT_80613570 + 0x69c) = *(int *)(DAT_80613570 + 0x69c) + 1;
        pcVar3 = *(char **)(DAT_80613570 + 0x69c);
        *(char **)(DAT_80613570 + 0x69c) = pcVar3 + 1;
        cVar1 = *pcVar3;
        if (((bVar6 == 0) && (cVar1 != '\"')) || ((bVar6 != 0 && (cVar1 != '\x11')))) break;
        iVar2 = iVar5 + 0x680;
        iVar5 = iVar5 + 6;
        puVar4 = *(undefined1 **)(DAT_80613570 + 0x69c);
        bVar6 = bVar6 + 1;
        *(undefined1 **)(DAT_80613570 + 0x69c) = puVar4 + 1;
        *(undefined1 *)(DAT_80613570 + iVar2) = *puVar4;
    }
    return 0x13;
}

// ---------------------------------------------------------------------------
// jpeg_parse_sos - parse the SOS scan header (three components, DC/AC table
// selectors), validate the selected tables were defined, set the MCU row count
// and clear the DC predictors. Returns 0 ok / 0xc bad count / 0xf undefined
// table.
// ---------------------------------------------------------------------------
undefined4 jpeg_parse_sos(void)
{
    byte  bVar1;
    char *pcVar2;
    byte *pbVar3;
    int   iVar4;
    uint  uVar5;
    int   iVar6;
    byte  bVar7;

    *(int *)(DAT_80613570 + 0x69c) = *(int *)(DAT_80613570 + 0x69c) + 2;
    pcVar2 = *(char **)(DAT_80613570 + 0x69c);
    *(char **)(DAT_80613570 + 0x69c) = pcVar2 + 1;
    if (*pcVar2 != '\x03') {
        return 0xc;
    }
    bVar7 = 0;
    iVar6 = 0;
    while (true) {
        if (2 < bVar7) {
            *(int *)(DAT_80613570 + 0x69c) = *(int *)(DAT_80613570 + 0x69c) + 3;
            *(short *)(DAT_80613570 + 0x696) =
                (short)((int)(*(ushort *)(DAT_80613570 + 0x692) + 0xf) >> 4);
            *(undefined2 *)(DAT_80613570 + 0x684) = 0;
            *(undefined2 *)(DAT_80613570 + 0x68a) = 0;
            *(undefined2 *)(DAT_80613570 + 0x690) = 0;
            return 0;
        }
        *(int *)(DAT_80613570 + 0x69c) = *(int *)(DAT_80613570 + 0x69c) + 1;
        pbVar3 = *(byte **)(DAT_80613570 + 0x69c);
        *(byte **)(DAT_80613570 + 0x69c) = pbVar3 + 1;
        bVar1 = *pbVar3;
        iVar4 = (int)(uint)bVar1 >> 4;
        *(char *)(DAT_80613570 + iVar6 + 0x681) = (char)iVar4;
        uVar5 = bVar1 & 0xf;
        *(char *)(DAT_80613570 + iVar6 + 0x682) = (char)uVar5;
        if (((uint)*(byte *)(DAT_80613570 + 0x6a8) & 1 << iVar4) == 0) break;
        if (((uint)*(byte *)(DAT_80613570 + 0x6a8) & 1 << (uVar5 + 1)) == 0) {
            return 0xf;
        }
        iVar6 = iVar6 + 6;
        bVar7 = bVar7 + 1;
    }
    return 0xf;
}

// ---------------------------------------------------------------------------
// jpeg_build_dequant_tables - parse a DQT marker and precompute the AAN-scaled
// dequant tables (de-zigzag + IDCT prescale folded in). Returns 0.
// ---------------------------------------------------------------------------
undefined4 jpeg_build_dequant_tables(void)
{
    byte    bVar1;
    int     iVar2;
    double  dVar3;
    short   sVar4;
    uint    uVar6;
    byte   *pbVar7;
    uint    uVar8;
    uint    uVar9;
    uint    uVar10;
    uint    uVar11;
    uint    uVar12;
    uint    uVar13;
    uint    uVar14;
    byte   *pbVar15;
    double *pdVar16;
    int     iVar17;
    double  dVar18;
    float   local_174[65];
    uint    scratch[8];   // integer->double conversion staging (the original local_XX low words)

    sVar4 = **(short **)(DAT_80613570 + 0x69c);
    *(short **)(DAT_80613570 + 0x69c) = *(short **)(DAT_80613570 + 0x69c) + 1;
    dVar3 = DOUBLE_8061c028;
    sVar4 = sVar4 + -2;
    do {
        pbVar7 = *(byte **)(DAT_80613570 + 0x69c);
        *(byte **)(DAT_80613570 + 0x69c) = pbVar7 + 1;
        bVar1 = *pbVar7;
        pbVar7 = &DAT_803e1ea8;
        for (uVar6 = 0; iVar2 = DAT_80613570, (uVar6 & 0xffff) < 0x40; uVar6 = uVar6 + 8) {
            pbVar15 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar15 + 1;
            scratch[0] = (uint)*pbVar15;
            local_174[*pbVar7] = (float)((double)CONCAT44(0x43300000, scratch[0]) - dVar3);
            pbVar15 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar15 + 1;
            scratch[1] = (uint)*pbVar15;
            local_174[(byte)(&DAT_803e1ea8)[uVar6 + 1 & 0xffff]] =
                (float)((double)CONCAT44(0x43300000, scratch[1]) - dVar3);
            pbVar15 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar15 + 1;
            scratch[2] = (uint)*pbVar15;
            local_174[(byte)(&DAT_803e1ea8)[uVar6 + 2 & 0xffff]] =
                (float)((double)CONCAT44(0x43300000, scratch[2]) - dVar3);
            pbVar15 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar15 + 1;
            scratch[3] = (uint)*pbVar15;
            local_174[(byte)(&DAT_803e1ea8)[uVar6 + 3 & 0xffff]] =
                (float)((double)CONCAT44(0x43300000, scratch[3]) - dVar3);
            pbVar15 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar15 + 1;
            scratch[4] = (uint)*pbVar15;
            local_174[(byte)(&DAT_803e1ea8)[uVar6 + 4 & 0xffff]] =
                (float)((double)CONCAT44(0x43300000, scratch[4]) - dVar3);
            pbVar15 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar15 + 1;
            scratch[5] = (uint)*pbVar15;
            local_174[(byte)(&DAT_803e1ea8)[uVar6 + 5 & 0xffff]] =
                (float)((double)CONCAT44(0x43300000, scratch[5]) - dVar3);
            pbVar15 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar15 + 1;
            pbVar7 = pbVar7 + 8;
            scratch[6] = (uint)*pbVar15;
            local_174[(byte)(&DAT_803e1ea8)[uVar6 + 6 & 0xffff]] =
                (float)((double)CONCAT44(0x43300000, scratch[6]) - dVar3);
            pbVar15 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar15 + 1;
            scratch[7] = (uint)*pbVar15;
            local_174[(byte)(&DAT_803e1ea8)[uVar6 + 7 & 0xffff]] =
                (float)((double)CONCAT44(0x43300000, scratch[7]) - dVar3);
        }
        iVar17 = (uint)bVar1 * 0x100;
        uVar6 = 0;
        {
            ushort uVar5 = 0;
            pdVar16 = &DAT_803e1ef8;
            while (true) {
                if (7 < uVar5) break;
                uVar8 = (uVar6 + 1) * 4 & 0x3fffc;
                uVar14 = (uVar6 + 2) * 4 & 0x3fffc;
                uVar13 = (uVar6 + 3) * 4 & 0x3fffc;
                uVar12 = (uVar6 + 4) * 4 & 0x3fffc;
                *(float *)(iVar17 + iVar2 + (uVar6 & 0xffff) * 4) =
                    (float)(DAT_803e1ef8 * (double)local_174[uVar6 & 0xffff] * *pdVar16);
                uVar11 = (uVar6 + 5) * 4 & 0x3fffc;
                uVar10 = (uVar6 + 6) * 4 & 0x3fffc;
                uVar9 = (uVar6 + 7) * 4 & 0x3fffc;
                uVar6 = uVar6 + 8;
                uVar5 = uVar5 + 1;
                *(float *)(iVar17 + iVar2 + uVar8) =
                    (float)(DAT_803e1f00 * (double)*(float *)((int)local_174 + uVar8) * *pdVar16);
                *(float *)(iVar17 + iVar2 + uVar14) =
                    (float)(DAT_803e1f08 * (double)*(float *)((int)local_174 + uVar14) * *pdVar16);
                *(float *)(iVar17 + iVar2 + uVar13) =
                    (float)(DAT_803e1f10 * (double)*(float *)((int)local_174 + uVar13) * *pdVar16);
                *(float *)(iVar17 + iVar2 + uVar12) =
                    (float)(DAT_803e1f18 * (double)*(float *)((int)local_174 + uVar12) * *pdVar16);
                *(float *)(iVar17 + iVar2 + uVar11) =
                    (float)(DAT_803e1f20 * (double)*(float *)((int)local_174 + uVar11) * *pdVar16);
                *(float *)(iVar17 + iVar2 + uVar10) =
                    (float)(DAT_803e1f28 * (double)*(float *)((int)local_174 + uVar10) * *pdVar16);
                dVar18 = *pdVar16;
                pdVar16 = pdVar16 + 1;
                *(float *)(iVar17 + iVar2 + uVar9) =
                    (float)(DAT_803e1f30 * (double)*(float *)((int)local_174 + uVar9) * dVar18);
            }
        }
        sVar4 = sVar4 + -0x41;
    } while (sVar4 != 0);
    return 0;
}

// ---------------------------------------------------------------------------
// jpeg_parse_dht - parse a DHT marker: read class/id, the 16 bits-per-length
// counts and the value bytes, then build the decode structures. Returns 0.
// ---------------------------------------------------------------------------
undefined4 jpeg_parse_dht(void)
{
    byte  bVar1;
    byte  bVar2;
    byte  bVar3;
    byte  bVar4;
    byte  bVar5;
    byte  bVar6;
    byte  bVar7;
    byte  bVar8;
    byte *pbVar9;
    uint  uVar10;
    ushort uVar11;
    short sVar12;

    DAT_8061350c = DAT_8061356c + 0x101;
    DAT_80613508 = (char *)(uintptr_t)DAT_8061356c;
    sVar12 = **(short **)(DAT_80613570 + 0x69c);
    *(short **)(DAT_80613570 + 0x69c) = *(short **)(DAT_80613570 + 0x69c) + 1;
    sVar12 = sVar12 + -2;
    do {
        uVar11 = 0;
        bVar8 = 0;
        pbVar9 = *(byte **)(DAT_80613570 + 0x69c);
        *(byte **)(DAT_80613570 + 0x69c) = pbVar9 + 1;
        uVar10 = (uint)*pbVar9;
        DAT_80613504 = *(uint *)(DAT_80613570 + 0x69c);
        uVar10 = (uVar10 & 0xf) * 2 + ((int)uVar10 >> 4);
        while (true) {
            if (0xf < bVar8) break;
            bVar8 = bVar8 + 8;
            pbVar9 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar9 + 1;
            bVar1 = *pbVar9;
            pbVar9 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar9 + 1;
            bVar2 = *pbVar9;
            pbVar9 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar9 + 1;
            bVar3 = *pbVar9;
            pbVar9 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar9 + 1;
            bVar4 = *pbVar9;
            pbVar9 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar9 + 1;
            bVar5 = *pbVar9;
            pbVar9 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar9 + 1;
            bVar6 = *pbVar9;
            pbVar9 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar9 + 1;
            bVar7 = *pbVar9;
            pbVar9 = *(byte **)(DAT_80613570 + 0x69c);
            *(byte **)(DAT_80613570 + 0x69c) = pbVar9 + 1;
            uVar11 = uVar11 + bVar1 + (ushort)bVar2 + (ushort)bVar3 + (ushort)bVar4 + (ushort)bVar5 +
                     (ushort)bVar6 + (ushort)bVar7 + (ushort)*pbVar9;
        }
        *(undefined4 *)(DAT_80613570 + uVar10 * 0xe0 + 0x340) = *(undefined4 *)(DAT_80613570 + 0x69c);
        *(uint *)(DAT_80613570 + 0x69c) = *(int *)(DAT_80613570 + 0x69c) + (uint)uVar11;
        jpeg_huff_expand_sizes();
        jpeg_huff_gen_codes();
        jpeg_huff_build_lookup(uVar10);
        sVar12 = sVar12 - (uVar11 + 0x11);
        *(byte *)(DAT_80613570 + 0x6a8) = *(byte *)(DAT_80613570 + 0x6a8) | (byte)(1 << uVar10);
    } while (sVar12 != 0);
    return 0;
}

// ---------------------------------------------------------------------------
// jpeg_huff_expand_sizes - build the flat HUFFSIZE code-size list from the 16
// bits-per-length counts, terminated by a 0.
// ---------------------------------------------------------------------------
void jpeg_huff_expand_sizes(void)
{
    byte       bVar1;
    undefined1 uVar2;
    int        iVar3;
    int        iVar4;
    int        iVar5;
    uint       uVar6;
    uint       uVar7;

    iVar3 = 0;
    iVar5 = 1;
    do {
        uVar2 = (undefined1)iVar5;
        bVar1 = *(byte *)(DAT_80613504 + iVar5 + -1);
        uVar6 = (uint)bVar1;
        if (uVar6 != 0) {
            uVar7 = (uint)(bVar1 >> 3);
            iVar4 = iVar3;
            if (bVar1 >> 3 != 0) {
                do {
                    *(undefined1 *)(DAT_80613508 + iVar3) = uVar2;
                    *(undefined1 *)(DAT_80613508 + iVar3 + 1) = uVar2;
                    *(undefined1 *)(DAT_80613508 + iVar3 + 2) = uVar2;
                    *(undefined1 *)(DAT_80613508 + iVar3 + 3) = uVar2;
                    *(undefined1 *)(DAT_80613508 + iVar3 + 4) = uVar2;
                    *(undefined1 *)(DAT_80613508 + iVar3 + 5) = uVar2;
                    iVar4 = iVar3 + 7;
                    *(undefined1 *)(DAT_80613508 + iVar3 + 6) = uVar2;
                    iVar3 = iVar3 + 8;
                    *(undefined1 *)(DAT_80613508 + iVar4) = uVar2;
                    uVar7 = uVar7 - 1;
                } while (uVar7 != 0);
                uVar6 = uVar6 & 7;
                iVar4 = iVar3;
                if ((bVar1 & 7) == 0) goto LAB_803bce78;
            }
            do {
                iVar3 = iVar4 + 1;
                *(undefined1 *)(DAT_80613508 + iVar4) = uVar2;
                uVar6 = uVar6 - 1;
                iVar4 = iVar3;
            } while (uVar6 != 0);
        }
LAB_803bce78:
        iVar5 = iVar5 + 1;
        if (0x10 < iVar5) {
            *(undefined1 *)(DAT_80613508 + iVar3) = 0;
            return;
        }
    } while (true);
}

// ---------------------------------------------------------------------------
// jpeg_huff_gen_codes - assign canonical Huffman codes (HUFFCODE) from the size
// list (JPEG Annex C code generation).
// ---------------------------------------------------------------------------
void jpeg_huff_gen_codes(void)
{
    char *pcVar1;
    int   iVar2;
    char  cVar3;
    uint  uVar4;
    uint  uVar5;

    iVar2 = DAT_8061350c;
    pcVar1 = DAT_80613508;
    uVar4 = 0;
    uVar5 = 0;
    cVar3 = *DAT_80613508;
    while (pcVar1[uVar4 & 0xffff] != '\0') {
        for (; cVar3 == pcVar1[uVar4 & 0xffff]; uVar4 = uVar4 + 1) {
            *(short *)(iVar2 + (uVar4 & 0xffff) * 2) = (short)uVar5;
            uVar5 = uVar5 + 1;
        }
        uVar5 = (uVar5 & 0x7fff) << 1;
        cVar3 = cVar3 + '\x01';
    }
    return;
}

// ---------------------------------------------------------------------------
// jpeg_huff_build_lookup - build the per-length maxcode/valptr decode tables for
// Huffman table slot (param_1). Absent lengths marked 0xffffffff, terminated by
// a 0xfffff sentinel.
// ---------------------------------------------------------------------------
void jpeg_huff_build_lookup(uint slot)
{
    int iVar1;
    int iVar2;
    int iVar3;
    int iVar4;
    int iVar5;

    iVar2 = DAT_80613570 + (slot & 0xff) * 0xe0 + 0x300;
    iVar5 = 4;
    iVar3 = 0;
    iVar4 = 1;
    iVar1 = iVar2;
    do {
        if (*(char *)(DAT_80613504 + iVar4 + -1) == '\0') {
            *(undefined4 *)(iVar1 + 0x48) = 0xffffffff;
            *(undefined4 *)(iVar1 + 0x90) = 0xffffffff;
        }
        else {
            *(uint *)(iVar1 + 0x90) = iVar3 - (uint)*(ushort *)(DAT_8061350c + iVar3 * 2);
            iVar3 = iVar3 + (uint)*(byte *)(DAT_80613504 + iVar4 + -1);
            *(uint *)(iVar1 + 0x48) = (uint)*(ushort *)(DAT_8061350c + iVar3 * 2 + -2);
        }
        if (*(char *)(DAT_80613504 + iVar4) == '\0') {
            *(undefined4 *)(iVar1 + 0x4c) = 0xffffffff;
            *(undefined4 *)(iVar1 + 0x94) = 0xffffffff;
        }
        else {
            *(uint *)(iVar1 + 0x94) = iVar3 - (uint)*(ushort *)(DAT_8061350c + iVar3 * 2);
            iVar3 = iVar3 + (uint)*(byte *)(DAT_80613504 + iVar4);
            *(uint *)(iVar1 + 0x4c) = (uint)*(ushort *)(DAT_8061350c + iVar3 * 2 + -2);
        }
        if (*(char *)(DAT_80613504 + iVar4 + 1) == '\0') {
            *(undefined4 *)(iVar1 + 0x50) = 0xffffffff;
            *(undefined4 *)(iVar1 + 0x98) = 0xffffffff;
        }
        else {
            *(uint *)(iVar1 + 0x98) = iVar3 - (uint)*(ushort *)(DAT_8061350c + iVar3 * 2);
            iVar3 = iVar3 + (uint)*(byte *)(DAT_80613504 + iVar4 + 1);
            *(uint *)(iVar1 + 0x50) = (uint)*(ushort *)(DAT_8061350c + iVar3 * 2 + -2);
        }
        if (*(char *)(DAT_80613504 + iVar4 + 2) == '\0') {
            *(undefined4 *)(iVar1 + 0x54) = 0xffffffff;
            *(undefined4 *)(iVar1 + 0x9c) = 0xffffffff;
        }
        else {
            *(uint *)(iVar1 + 0x9c) = iVar3 - (uint)*(ushort *)(DAT_8061350c + iVar3 * 2);
            iVar3 = iVar3 + (uint)*(byte *)(DAT_80613504 + iVar4 + 2);
            *(uint *)(iVar1 + 0x54) = (uint)*(ushort *)(DAT_8061350c + iVar3 * 2 + -2);
        }
        iVar4 = iVar4 + 4;
        iVar5 = iVar5 + -1;
        iVar1 = iVar1 + 0x10;
    } while (iVar5 != 0);
    *(undefined4 *)(iVar2 + 0x88) = 0xfffff;
    return;
}

// ---------------------------------------------------------------------------
// jpeg_parse_dri - parse a DRI marker: set restart-enable and store the restart
// interval (MCUs between RSTn markers).
// ---------------------------------------------------------------------------
void jpeg_parse_dri(void)
{
    *(undefined1 *)(DAT_80613570 + 0x6a9) = 1;
    *(int *)(DAT_80613570 + 0x69c) = *(int *)(DAT_80613570 + 0x69c) + 2;
    *(undefined2 *)(DAT_80613570 + 0x6aa) = **(undefined2 **)(DAT_80613570 + 0x69c);
    *(int *)(DAT_80613570 + 0x69c) = *(int *)(DAT_80613570 + 0x69c) + 2;
    *(undefined2 *)(DAT_80613570 + 0x6ac) = *(undefined2 *)(DAT_80613570 + 0x6aa);
    return;
}

// ===========================================================================
// batch_1157: THP JPEG scan-decode layer - the fast-Huffman table builder, the
// scan-decode dispatcher, the three per-component 8x8 block entropy decoders,
// and the one-time decoder init. The three resolution-specialized MCU-row
// decoders (jpeg_decode_mcu_row_512/_640/generic) are decompiled separately.
//
// NON-MATCHING: the bit-reservoir arithmetic (a 32-bit window in +0x6a0 with a
// bit-count in +0x6a4 refilled 32 bits at a time from +0x69c), the fast-table
// offset math (+0x300 per-table blocks, +0x40/+0x48/+0x58/+0x8c/+0x90 sub-
// arrays), and the de-zigzag via 0x803e1ea8 are kept verbatim from the
// decompiler output. The PPC intrinsics cntlzw / dcbz are provided as small
// local helpers (dcbz zeroes a 32-byte line; on a non-matching x64 build this
// is a plain 32-byte clear).
// ===========================================================================

// --- PPC dcbz intrinsic (countLeadingZeros comes from common.h) -------------
// dataCacheBlockClearToZero is NOT provided by common.h; define it locally.
static inline void dataCacheBlockClearToZero(void *p)
{
    // PPC dcbz: zero the 32-byte cache line containing p (here: the aligned line).
    unsigned char *line = (unsigned char *)((uintptr_t)p & ~(uintptr_t)0x1f);
    for (int i = 0; i < 32; i++) line[i] = 0;
}

// --- SDK: GameCube locked-cache DMA + version registration ------------------
extern void os_lc_queue_wait(int count);
extern void os_lc_store_data(uint dst, uint lcSrc, int len);
extern undefined8 os_register_version(int fmt, ...);

// --- scan-decode restart / MCU counters -------------------------------------
extern uint DAT_80613564;
extern uint DAT_80613568;

// --- fast-Huffman decode-table base pointers (resolved per component) -------
extern int DAT_80613460;  // Y  AC fast-table base (+0x300 block)
extern int DAT_80613480;  // Cb AC fast-table base
extern int DAT_806134a0;  // Cr AC fast-table base
extern int DAT_806134c0;  // Y  DC fast-table base
extern int DAT_806134e0;  // Cb DC fast-table base
extern int DAT_80613500;  // Cr DC fast-table base

// --- MCU-row decoder scratch pointers (used by the block decoders' callers) -
extern int    DAT_80613520;  // current output plane cursor
extern int    DAT_80613540;  // current plane stride/width
extern float *DAT_80613560;  // current dequant-table cursor

// --- jpeg_init locked-cache work-buffer addresses + version blob ------------
extern uint DAT_8060ed00;
extern uint DAT_8060ed04;
extern uint DAT_8060ed08;
extern uint DAT_8060ed0c;
extern uint DAT_8060ed10;
extern uint DAT_8060ed14;
extern uint DAT_806114c8;  // version blob for jpeg_init

// --- the three resolution-specialized MCU-row decoders (defined in batch_1158
//     at the end of this file). Forward declarations so jpeg_scan_decode can
//     dispatch to them. ---
void jpeg_decode_mcu_row_512(void);   // FUN_803bd464 (512x448)
void jpeg_decode_mcu_row_640(void);   // FUN_803beeec (640x480)
void jpeg_decode_mcu_row(void);        // FUN_803c0978 (generic)

// ---------------------------------------------------------------------------
// jpeg_build_fast_huffman - build the per-table single-lookup fast decode
// tables (+0x300 blocks) and resolve the per-component decode-table pointers.
// ---------------------------------------------------------------------------
void jpeg_build_fast_huffman(void)
{
    char  cVar1;
    undefined4 *puVar2;
    uint  uVar3;
    int   iVar4;
    int   iVar5;
    uint  uVar6;
    int   iVar7;
    uint  uVar8;
    uint  uVar9;
    int   iVar10;

    puVar2 = (undefined4 *)(*(uint *)(DAT_80613570 + 0x69c) & 0xfffffffc);
    uVar3 = *(uint *)(DAT_80613570 + 0x69c) & 3;
    if (*(int *)(DAT_80613570 + 0x6a4) == 0x21) {
        *(int *)(DAT_80613570 + 0x6a4) = uVar3 * 8 + 1;
    }
    else {
        *(int *)(DAT_80613570 + 0x6a4) = *(int *)(DAT_80613570 + 0x6a4) + (3 - uVar3) * -8;
    }
    uVar3 = 0;
    iVar5 = 0;
    *(undefined4 **)(DAT_80613570 + 0x69c) = puVar2;
    *(undefined4 *)(DAT_80613570 + 0x6a0) = *puVar2;
    do {
        if (((uint)*(byte *)(DAT_80613570 + 0x6a8) & 1 << uVar3) != 0) {
            iVar10 = 0x10;
            uVar8 = 0;
            iVar4 = iVar5;
            do {
                *(undefined1 *)(iVar5 + DAT_80613570 + uVar8 + 0x300) = 0xff;
                for (uVar9 = 0; uVar9 < 5; uVar9 = uVar9 + 1) {
                    iVar7 = iVar5 + DAT_80613570 + uVar9 * 4;
                    uVar6 = uVar8 >> (4 - uVar9);
                    if ((int)uVar6 <= *(int *)(iVar7 + 0x348)) {
                        cVar1 = (char)uVar9;
                        uVar9 = 99;
                        *(undefined1 *)(iVar4 + DAT_80613570 + 0x300) =
                            *(undefined1 *)(uVar6 + *(int *)(iVar7 + 0x390) +
                                            *(int *)(iVar5 + DAT_80613570 + 0x340));
                        *(char *)(iVar4 + DAT_80613570 + 800) = cVar1 + '\x01';
                    }
                }
                *(undefined1 *)(iVar5 + DAT_80613570 + uVar8 + 1 + 0x300) = 0xff;
                for (uVar9 = 0; uVar9 < 5; uVar9 = uVar9 + 1) {
                    iVar7 = iVar5 + DAT_80613570 + uVar9 * 4;
                    uVar6 = (uVar8 + 1) >> (4 - uVar9);
                    if ((int)uVar6 <= *(int *)(iVar7 + 0x348)) {
                        cVar1 = (char)uVar9;
                        uVar9 = 99;
                        *(undefined1 *)(iVar4 + 1 + DAT_80613570 + 0x300) =
                            *(undefined1 *)(uVar6 + *(int *)(iVar7 + 0x390) +
                                            *(int *)(iVar5 + DAT_80613570 + 0x340));
                        *(char *)(iVar4 + 1 + DAT_80613570 + 800) = cVar1 + '\x01';
                    }
                }
                iVar4 = iVar4 + 2;
                uVar8 = uVar8 + 2;
                iVar10 = iVar10 + -1;
            } while (iVar10 != 0);
        }
        uVar3 = uVar3 + 1;
        iVar5 = iVar5 + 0xe0;
    } while (uVar3 < 4);
    DAT_80613460 = DAT_80613570 + (uint)*(byte *)(DAT_80613570 + 0x681) * 0x1c0 + 0x300;
    DAT_80613480 = DAT_80613570 + (uint)*(byte *)(DAT_80613570 + 0x687) * 0x1c0 + 0x300;
    DAT_806134a0 = DAT_80613570 + (uint)*(byte *)(DAT_80613570 + 0x68d) * 0x1c0 + 0x300;
    DAT_806134c0 = DAT_80613570 + ((uint)*(byte *)(DAT_80613570 + 0x682) * 2 + 1) * 0xe0 + 0x300;
    DAT_806134e0 = DAT_80613570 + ((uint)*(byte *)(DAT_80613570 + 0x688) * 2 + 1) * 0xe0 + 0x300;
    DAT_80613500 = DAT_80613570 + ((uint)*(byte *)(DAT_80613570 + 0x68e) * 2 + 1) * 0xe0 + 0x300;
    return;
}

// ---------------------------------------------------------------------------
// jpeg_scan_decode - scan-data decode entry (SOS). Stores the output plane
// pointers, builds the fast Huffman tables, then dispatches to a resolution-
// specialized MCU-row decoder looping over the MCU rows.
// ---------------------------------------------------------------------------
void jpeg_scan_decode(int planeY, int planeCb, int planeCr)
{
    ushort uVar1;
    ushort uVar2;

    *(undefined4 *)(DAT_80613570 + 0x6b0) = planeY;
    *(undefined4 *)(DAT_80613570 + 0x6b4) = planeCb;
    *(undefined4 *)(DAT_80613570 + 0x6b8) = planeCr;
    uVar2 = *(ushort *)(DAT_80613570 + 0x698);
    uVar1 = *(ushort *)(DAT_80613570 + 0x694);
    DAT_80613564 = 0;
    DAT_80613568 = 0;
    jpeg_build_fast_huffman();
    if ((*(short *)(DAT_80613570 + 0x692) == 0x200) && (uVar1 == 0x1c0)) {
        for (; uVar2 < 0x1c0; uVar2 = uVar2 + 0x10) {
            jpeg_decode_mcu_row_512();
        }
    }
    else if ((*(short *)(DAT_80613570 + 0x692) == 0x280) && (uVar1 == 0x1e0)) {
        for (; uVar2 < 0x1e0; uVar2 = uVar2 + 0x10) {
            jpeg_decode_mcu_row_640();
        }
    }
    else {
        for (; uVar2 < uVar1; uVar2 = uVar2 + 0x10) {
            jpeg_decode_mcu_row();
        }
    }
    return;
}

// ---------------------------------------------------------------------------
// jpeg_decode_block_y - Huffman-decode one 8x8 Y (luma) block into param_2.
// ---------------------------------------------------------------------------
void jpeg_decode_block_y(int state, short *out)
{
    uint *puVar1;
    byte  bVar2;
    uint  uVar3;
    int   iVar4;
    short sVar5;
    uint  uVar6;
    int   iVar7;
    int  *piVar8;
    int   iVar9;
    uint  uVar10;
    int   iVar11;
    uint  uVar12;
    int   iVar13;
    uint  uVar14;
    int   iVar15;

    iVar4 = DAT_80613460;
    dataCacheBlockClearToZero(out);
    iVar11 = *(int *)(state + 0x6a4);
    iVar7 = DAT_80613460 + 0x20;
    uVar10 = *(uint *)(state + 0x6a0);
    uVar6 = iVar11 + 4U & 0x1f;
    uVar6 = (uVar10 << uVar6 | uVar10 >> (0x20 - uVar6)) & 0x1f;
    if (iVar11 < 0x1d) {
        uVar3 = (uint)*(byte *)(DAT_80613460 + uVar6);
        if (uVar3 == 0xff) {
            uVar3 = iVar11 + 5;
            iVar7 = 5;
            piVar8 = (int *)(DAT_80613460 + 0x58);
LAB_803c2488:
            if (uVar3 != 0x21) goto code_r0x803c2494;
            uVar3 = 1;
            puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
            uVar10 = *puVar1;
            piVar8 = piVar8 + 1;
            iVar11 = *piVar8;
            *(uint **)(state + 0x69c) = puVar1;
            uVar6 = uVar10 >> 0x1f | uVar6 << 1;
            *(uint *)(state + 0x6a0) = uVar10;
            while (true) {
                uVar3 = uVar3 + 1;
                iVar7 = iVar7 + 1;
                if ((int)uVar6 <= iVar11) break;
                piVar8 = piVar8 + 1;
                iVar11 = *piVar8;
                uVar6 = uVar6 << 1 | (uVar10 << (uVar3 & 0x1f) | uVar10 >> (0x20 - (uVar3 & 0x1f))) & 1;
            }
LAB_803c24f8:
            *(uint *)(state + 0x6a4) = uVar3;
            uVar3 = (uint)*(byte *)(uVar6 + *(int *)(iVar4 + iVar7 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
            goto LAB_803c26bc;
        }
        *(uint *)(state + 0x6a4) = iVar11 + (uint)*(byte *)(iVar7 + uVar6);
        goto LAB_803c26bc;
    }
    if (iVar11 == 0x21) {
        puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
        uVar6 = *puVar1;
        *(uint **)(state + 0x69c) = puVar1;
        uVar3 = (uint)*(byte *)(iVar4 + (uVar6 >> 0x1b));
        bVar2 = *(byte *)(iVar7 + (uVar6 >> 0x1b));
        *(uint *)(state + 0x6a0) = uVar6;
        if (uVar3 != 0xff) {
            *(uint *)(state + 0x6a4) = bVar2 + 1;
            goto LAB_803c26bc;
        }
        iVar11 = 0x14;
        iVar7 = 5;
        do {
            iVar9 = iVar7;
            iVar7 = iVar9 + 1;
            iVar11 = iVar11 + 4;
            uVar10 = uVar6 >> (0x1f - iVar9);
        } while (*(int *)(iVar4 + iVar11 + 0x44) < (int)uVar10);
        *(int *)(state + 0x6a4) = iVar9 + 2;
    }
    else {
        uVar6 = iVar11 + 4U & 0x1f;
        uVar6 = uVar10 << uVar6 | uVar10 >> (0x20 - uVar6);
        if (iVar11 != 0x20) {
            uVar3 = (uint)*(byte *)(DAT_80613460 + (uVar6 & 0x1f));
            iVar7 = iVar11 + (uint)*(byte *)(iVar7 + (uVar6 & 0x1f));
            if ((uVar3 == 0xff) || (*(int *)(state + 0x6a4) = iVar7, 0x21 < iVar7)) {
                iVar9 = -iVar11 + 0x22;
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar3 = *puVar1;
                *(uint **)(state + 0x69c) = puVar1;
                *(uint *)(state + 0x6a0) = uVar3;
                uVar6 = uVar3 >> 0x1f | (uVar10 & ~(-1 << (0x21 - iVar11))) << 1;
                piVar8 = (int *)(iVar4 + (-iVar11 + 0x21) * 4 + 0x48);
                uVar10 = 2;
                iVar7 = *piVar8;
                while (iVar7 < (int)uVar6) {
                    iVar9 = iVar9 + 1;
                    piVar8 = piVar8 + 1;
                    uVar6 = uVar6 * 2 + ((uVar3 << (uVar10 & 0x1f) | uVar3 >> (0x20 - (uVar10 & 0x1f))) & 1);
                    uVar10 = uVar10 + 1;
                    iVar7 = *piVar8;
                }
                *(uint *)(state + 0x6a4) = uVar10;
                uVar3 = (uint)*(byte *)(uVar6 + *(int *)(iVar4 + iVar9 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
            }
            goto LAB_803c26bc;
        }
        puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
        uVar12 = *puVar1;
        *(uint **)(state + 0x69c) = puVar1;
        uVar10 = uVar12 >> 0x1c | uVar6 & 0x10;
        uVar3 = (uint)*(byte *)(iVar4 + uVar10);
        bVar2 = *(byte *)(iVar7 + uVar10);
        *(uint *)(state + 0x6a0) = uVar12;
        *(uint *)(state + 0x6a4) = (uint)bVar2;
        if (uVar3 != 0xff) goto LAB_803c26bc;
        piVar8 = (int *)(iVar4 + 0x58);
        iVar7 = 5;
        do {
            piVar8 = piVar8 + 1;
            uVar10 = (uVar12 >> 1 | (uVar6 & 0x10) << 0x1b) >> (0x1f - iVar7);
            iVar7 = iVar7 + 1;
        } while (*piVar8 < (int)uVar10);
        *(int *)(state + 0x6a4) = iVar7;
    }
    uVar3 = (uint)*(byte *)(uVar10 + *(int *)(iVar4 + iVar7 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
LAB_803c26bc:
    dataCacheBlockClearToZero(out + 0x10);
    sVar5 = 0;
    dataCacheBlockClearToZero(out + 0x20);
    if (uVar3 != 0) {
        iVar7 = *(int *)(state + 0x6a4);
        iVar4 = *(int *)(state + 0x6a0);
        iVar11 = uVar3 - (0x21 - iVar7);
        if (iVar11 < 1) {
            *(uint *)(state + 0x6a4) = iVar7 + uVar3;
            sVar5 = (short)((uint)(iVar4 << (iVar7 + -1)) >> (0x20 - uVar3));
        }
        else {
            puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
            uVar6 = *puVar1;
            *(uint *)(state + 0x6a0) = uVar6;
            *(uint **)(state + 0x69c) = puVar1;
            *(int *)(state + 0x6a4) = iVar11 + 1;
            sVar5 = (short)((uVar6 >> (0x21 - iVar7)) + (iVar4 << (iVar7 + -1)) >> (0x20 - uVar3));
        }
        iVar4 = countLeadingZeros((int)sVar5);
        if ((int)(0x20 - uVar3) < iVar4) {
            sVar5 = (short)(-1 << uVar3) + sVar5 + 1;
        }
    }
    dataCacheBlockClearToZero(out + 0x30);
    sVar5 = *(short *)(state + 0x684) + sVar5;
    *(short *)(state + 0x684) = sVar5;
    *out = sVar5;
    iVar4 = DAT_806134c0;
    iVar11 = DAT_806134c0 + 0x20;
    uVar10 = *(uint *)(state + 0x6a0);
    uVar6 = *(uint *)(state + 0x6a4);
    for (iVar7 = 1; uVar3 = uVar6, iVar7 < 0x40; iVar7 = iVar7 + 1) {
        uVar3 = uVar6 + 4 & 0x1f;
        uVar12 = (uVar10 << uVar3 | uVar10 >> (0x20 - uVar3)) & 0x1f;
        if ((int)uVar6 < 0x1d) {
            uVar14 = (uint)*(byte *)(iVar4 + uVar12);
            if (uVar14 == 0xff) {
                uVar3 = uVar6 + 5;
                iVar9 = 5;
                piVar8 = (int *)(iVar4 + 0x58);
LAB_803c27cc:
                if (uVar3 != 0x21) goto code_r0x803c27d8;
                uVar3 = 1;
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar10 = *puVar1;
                piVar8 = piVar8 + 1;
                iVar13 = *piVar8;
                *(uint **)(state + 0x69c) = puVar1;
                uVar12 = uVar10 >> 0x1f | uVar12 << 1;
                while (true) {
                    uVar3 = uVar3 + 1;
                    iVar9 = iVar9 + 1;
                    if ((int)uVar12 <= iVar13) break;
                    piVar8 = piVar8 + 1;
                    iVar13 = *piVar8;
                    uVar12 = uVar12 << 1 | (uVar10 << (uVar3 & 0x1f) | uVar10 >> (0x20 - (uVar3 & 0x1f))) & 1;
                }
LAB_803c2838:
                uVar14 = (uint)*(byte *)(uVar12 + *(int *)(iVar4 + iVar9 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
                goto LAB_803c29dc;
            }
            uVar3 = uVar6 + *(byte *)(iVar11 + uVar12);
        }
        else if (uVar6 == 0x21) {
            puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
            uVar10 = *puVar1;
            *(uint **)(state + 0x69c) = puVar1;
            uVar14 = (uint)*(byte *)(iVar4 + (uVar10 >> 0x1b));
            uVar3 = *(byte *)(iVar11 + (uVar10 >> 0x1b)) + 1;
            if (uVar14 == 0xff) {
                iVar13 = 0x14;
                iVar9 = 5;
                do {
                    iVar15 = iVar9;
                    iVar13 = iVar13 + 4;
                    uVar6 = uVar10 >> (0x1f - iVar15);
                    iVar9 = iVar15 + 1;
                } while (*(int *)(iVar4 + iVar13 + 0x44) < (int)uVar6);
                uVar3 = iVar15 + 2;
                uVar14 = (uint)*(byte *)(uVar6 + *(int *)(iVar4 + iVar13 + 0x8c) + *(int *)(iVar4 + 0x40));
            }
        }
        else {
            uVar3 = uVar6 + 4 & 0x1f;
            uVar12 = uVar10 << uVar3 | uVar10 >> (0x20 - uVar3);
            if (uVar6 == 0x20) {
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar10 = *puVar1;
                *(uint **)(state + 0x69c) = puVar1;
                uVar6 = uVar10 >> 0x1c | uVar12 & 0x10;
                uVar14 = (uint)*(byte *)(iVar4 + uVar6);
                uVar3 = (uint)*(byte *)(iVar11 + uVar6);
                if (uVar14 == 0xff) {
                    piVar8 = (int *)(iVar4 + 0x58);
                    uVar3 = 5;
                    do {
                        piVar8 = piVar8 + 1;
                        uVar6 = (uVar10 >> 1 | (uVar12 & 0x10) << 0x1b) >> (0x1f - uVar3);
                        uVar3 = uVar3 + 1;
                    } while (*piVar8 < (int)uVar6);
                    uVar14 = (uint)*(byte *)(uVar6 + *(int *)(iVar4 + uVar3 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
                }
            }
            else {
                uVar14 = (uint)*(byte *)(iVar4 + (uVar12 & 0x1f));
                uVar3 = uVar6 + *(byte *)(iVar11 + (uVar12 & 0x1f));
                if ((uVar14 == 0xff) || (0x21 < (int)uVar3)) {
                    uVar3 = uVar10 & ~(-1 << (0x21 - uVar6));
                    iVar13 = -uVar6 + 0x22;
                    puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                    uVar10 = *puVar1;
                    *(uint **)(state + 0x69c) = puVar1;
                    uVar12 = uVar10 >> 0x1f | uVar3 << 1;
                    piVar8 = (int *)(iVar4 + (-uVar6 + 0x21) * 4 + 0x48);
                    uVar3 = 2;
                    iVar9 = *piVar8;
                    while (iVar9 < (int)uVar12) {
                        iVar13 = iVar13 + 1;
                        piVar8 = piVar8 + 1;
                        uVar12 = uVar12 * 2 + ((uVar10 << (uVar3 & 0x1f) | uVar10 >> (0x20 - (uVar3 & 0x1f))) & 1);
                        uVar3 = uVar3 + 1;
                        iVar9 = *piVar8;
                    }
                    uVar14 = (uint)*(byte *)(uVar12 + *(int *)(iVar4 + iVar13 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
                }
            }
        }
LAB_803c29dc:
        uVar6 = uVar14 & 0xf;
        if (uVar6 == 0) {
            if ((int)uVar14 >> 4 != 0xf) break;
            iVar7 = iVar7 + 0xf;
        }
        else {
            iVar7 = iVar7 + ((int)uVar14 >> 4);
            iVar15 = 0x21 - uVar3;
            iVar13 = uVar6 - iVar15;
            iVar9 = uVar3 - 1;
            if (iVar13 < 1) {
                uVar3 = uVar3 + uVar6;
                uVar12 = (uVar10 << iVar9) >> (0x20 - uVar6);
            }
            else {
                iVar9 = uVar10 << iVar9;
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar10 = *puVar1;
                uVar3 = iVar13 + 1;
                *(uint **)(state + 0x69c) = puVar1;
                uVar12 = (uVar10 >> iVar15) + iVar9 >> (0x20 - uVar6);
            }
            iVar9 = countLeadingZeros(uVar12);
            if ((int)(0x20 - uVar6) < iVar9) {
                uVar12 = (-1 << uVar6) + uVar12 + 1;
            }
            out[(byte)(&DAT_803e1ea8)[iVar7]] = (short)uVar12;
        }
        uVar6 = uVar3;
    }
    *(uint *)(state + 0x6a4) = uVar3;
    *(uint *)(state + 0x6a0) = uVar10;
    return;
code_r0x803c2494:
    piVar8 = piVar8 + 1;
    uVar6 = uVar6 << 1 | (uVar10 << (uVar3 & 0x1f) | uVar10 >> (0x20 - (uVar3 & 0x1f))) & 1;
    uVar3 = uVar3 + 1;
    iVar7 = iVar7 + 1;
    if ((int)uVar6 <= *piVar8) goto LAB_803c24f8;
    goto LAB_803c2488;
code_r0x803c27d8:
    piVar8 = piVar8 + 1;
    uVar12 = uVar12 << 1 | (uVar10 << (uVar3 & 0x1f) | uVar10 >> (0x20 - (uVar3 & 0x1f))) & 1;
    uVar3 = uVar3 + 1;
    iVar9 = iVar9 + 1;
    if ((int)uVar12 <= *piVar8) goto LAB_803c2838;
    goto LAB_803c27cc;
}

// ---------------------------------------------------------------------------
// jpeg_decode_block_cb - Huffman-decode the Cb chroma 8x8 block (predictor
// +0x68a, tables DAT_80613480 / DAT_806134e0).
// ---------------------------------------------------------------------------
void jpeg_decode_block_cb(int state, short *out)
{
    uint *puVar1;
    byte  bVar2;
    uint  uVar3;
    int   iVar4;
    short sVar6;
    int  *piVar5;
    uint  uVar7;
    int   iVar8;
    uint  uVar9;
    int   iVar10;
    uint  uVar11;
    int   iVar12;
    int   iVar13;

    iVar4 = DAT_80613480;
    dataCacheBlockClearToZero(out);
    iVar10 = *(int *)(state + 0x6a4);
    iVar8 = DAT_80613480 + 0x20;
    uVar9 = *(uint *)(state + 0x6a0);
    uVar7 = iVar10 + 4U & 0x1f;
    uVar7 = (uVar9 << uVar7 | uVar9 >> (0x20 - uVar7)) & 0x1f;
    if (iVar10 < 0x1d) {
        uVar3 = (uint)*(byte *)(DAT_80613480 + uVar7);
        if (uVar3 == 0xff) {
            uVar3 = iVar10 + 5;
            iVar8 = 5;
            piVar5 = (int *)(DAT_80613480 + 0x58);
LAB_803c2b00:
            if (uVar3 != 0x21) goto code_r0x803c2b0c;
            uVar3 = 1;
            puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
            uVar9 = *puVar1;
            piVar5 = piVar5 + 1;
            iVar10 = *piVar5;
            *(uint **)(state + 0x69c) = puVar1;
            uVar7 = uVar9 >> 0x1f | uVar7 << 1;
            *(uint *)(state + 0x6a0) = uVar9;
            while (true) {
                uVar3 = uVar3 + 1;
                iVar8 = iVar8 + 1;
                if ((int)uVar7 <= iVar10) break;
                piVar5 = piVar5 + 1;
                iVar10 = *piVar5;
                uVar7 = uVar7 << 1 | (uVar9 << (uVar3 & 0x1f) | uVar9 >> (0x20 - (uVar3 & 0x1f))) & 1;
            }
LAB_803c2b70:
            *(uint *)(state + 0x6a4) = uVar3;
            uVar3 = (uint)*(byte *)(uVar7 + *(int *)(iVar4 + iVar8 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
            goto LAB_803c2d34;
        }
        *(uint *)(state + 0x6a4) = iVar10 + (uint)*(byte *)(iVar8 + uVar7);
        goto LAB_803c2d34;
    }
    if (iVar10 == 0x21) {
        puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
        uVar7 = *puVar1;
        *(uint **)(state + 0x69c) = puVar1;
        uVar3 = (uint)*(byte *)(iVar4 + (uVar7 >> 0x1b));
        bVar2 = *(byte *)(iVar8 + (uVar7 >> 0x1b));
        *(uint *)(state + 0x6a0) = uVar7;
        if (uVar3 != 0xff) {
            *(uint *)(state + 0x6a4) = bVar2 + 1;
            goto LAB_803c2d34;
        }
        iVar10 = 0x14;
        iVar8 = 5;
        do {
            iVar12 = iVar8;
            iVar8 = iVar12 + 1;
            iVar10 = iVar10 + 4;
            uVar9 = uVar7 >> (0x1f - iVar12);
        } while (*(int *)(iVar4 + iVar10 + 0x44) < (int)uVar9);
        *(int *)(state + 0x6a4) = iVar12 + 2;
    }
    else {
        uVar7 = iVar10 + 4U & 0x1f;
        uVar7 = uVar9 << uVar7 | uVar9 >> (0x20 - uVar7);
        if (iVar10 != 0x20) {
            uVar3 = (uint)*(byte *)(DAT_80613480 + (uVar7 & 0x1f));
            iVar8 = iVar10 + (uint)*(byte *)(iVar8 + (uVar7 & 0x1f));
            if ((uVar3 == 0xff) || (*(int *)(state + 0x6a4) = iVar8, 0x21 < iVar8)) {
                iVar12 = -iVar10 + 0x22;
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar3 = *puVar1;
                *(uint **)(state + 0x69c) = puVar1;
                *(uint *)(state + 0x6a0) = uVar3;
                uVar7 = uVar3 >> 0x1f | (uVar9 & ~(-1 << (0x21 - iVar10))) << 1;
                piVar5 = (int *)(iVar4 + (-iVar10 + 0x21) * 4 + 0x48);
                uVar9 = 2;
                iVar8 = *piVar5;
                while (iVar8 < (int)uVar7) {
                    iVar12 = iVar12 + 1;
                    piVar5 = piVar5 + 1;
                    uVar7 = uVar7 * 2 + ((uVar3 << (uVar9 & 0x1f) | uVar3 >> (0x20 - (uVar9 & 0x1f))) & 1);
                    uVar9 = uVar9 + 1;
                    iVar8 = *piVar5;
                }
                *(uint *)(state + 0x6a4) = uVar9;
                uVar3 = (uint)*(byte *)(uVar7 + *(int *)(iVar4 + iVar12 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
            }
            goto LAB_803c2d34;
        }
        puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
        uVar11 = *puVar1;
        *(uint **)(state + 0x69c) = puVar1;
        uVar9 = uVar11 >> 0x1c | uVar7 & 0x10;
        uVar3 = (uint)*(byte *)(iVar4 + uVar9);
        bVar2 = *(byte *)(iVar8 + uVar9);
        *(uint *)(state + 0x6a0) = uVar11;
        *(uint *)(state + 0x6a4) = (uint)bVar2;
        if (uVar3 != 0xff) goto LAB_803c2d34;
        piVar5 = (int *)(iVar4 + 0x58);
        iVar8 = 5;
        do {
            piVar5 = piVar5 + 1;
            uVar9 = (uVar11 >> 1 | (uVar7 & 0x10) << 0x1b) >> (0x1f - iVar8);
            iVar8 = iVar8 + 1;
        } while (*piVar5 < (int)uVar9);
        *(int *)(state + 0x6a4) = iVar8;
    }
    uVar3 = (uint)*(byte *)(uVar9 + *(int *)(iVar4 + iVar8 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
LAB_803c2d34:
    dataCacheBlockClearToZero(out + 0x10);
    sVar6 = 0;
    dataCacheBlockClearToZero(out + 0x20);
    if (uVar3 != 0) {
        iVar8 = *(int *)(state + 0x6a4);
        iVar4 = *(int *)(state + 0x6a0);
        iVar10 = uVar3 - (0x21 - iVar8);
        if (iVar10 < 1) {
            *(uint *)(state + 0x6a4) = iVar8 + uVar3;
            sVar6 = (short)((uint)(iVar4 << (iVar8 + -1)) >> (0x20 - uVar3));
        }
        else {
            puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
            uVar7 = *puVar1;
            *(uint *)(state + 0x6a0) = uVar7;
            *(uint **)(state + 0x69c) = puVar1;
            *(int *)(state + 0x6a4) = iVar10 + 1;
            sVar6 = (short)((uVar7 >> (0x21 - iVar8)) + (iVar4 << (iVar8 + -1)) >> (0x20 - uVar3));
        }
        iVar4 = countLeadingZeros((int)sVar6);
        if ((int)(0x20 - uVar3) < iVar4) {
            sVar6 = (short)(-1 << uVar3) + sVar6 + 1;
        }
    }
    dataCacheBlockClearToZero(out + 0x30);
    sVar6 = *(short *)(state + 0x68a) + sVar6;
    *(short *)(state + 0x68a) = sVar6;
    iVar4 = 1;
    *out = sVar6;
    do {
        iVar8 = DAT_806134e0;
        if (0x3f < iVar4) {
            return;
        }
        iVar12 = *(int *)(state + 0x6a4);
        iVar10 = DAT_806134e0 + 0x20;
        uVar9 = *(uint *)(state + 0x6a0);
        uVar7 = iVar12 + 4U & 0x1f;
        uVar7 = (uVar9 << uVar7 | uVar9 >> (0x20 - uVar7)) & 0x1f;
        if (iVar12 < 0x1d) {
            uVar3 = (uint)*(byte *)(DAT_806134e0 + uVar7);
            if (uVar3 == 0xff) {
                uVar3 = iVar12 + 5;
                iVar10 = 5;
                piVar5 = (int *)(DAT_806134e0 + 0x58);
LAB_803c2e48:
                if (uVar3 != 0x21) goto code_r0x803c2e54;
                uVar3 = 1;
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar9 = *puVar1;
                piVar5 = piVar5 + 1;
                iVar12 = *piVar5;
                *(uint **)(state + 0x69c) = puVar1;
                uVar7 = uVar9 >> 0x1f | uVar7 << 1;
                *(uint *)(state + 0x6a0) = uVar9;
                while (true) {
                    uVar3 = uVar3 + 1;
                    iVar10 = iVar10 + 1;
                    if ((int)uVar7 <= iVar12) break;
                    piVar5 = piVar5 + 1;
                    iVar12 = *piVar5;
                    uVar7 = uVar7 << 1 | (uVar9 << (uVar3 & 0x1f) | uVar9 >> (0x20 - (uVar3 & 0x1f))) & 1;
                }
LAB_803c2eb8:
                *(uint *)(state + 0x6a4) = uVar3;
                uVar3 = (uint)*(byte *)(uVar7 + *(int *)(iVar8 + iVar10 * 4 + 0x8c) + *(int *)(iVar8 + 0x40));
                goto LAB_803c307c;
            }
            *(uint *)(state + 0x6a4) = iVar12 + (uint)*(byte *)(iVar10 + uVar7);
        }
        else if (iVar12 == 0x21) {
            puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
            uVar7 = *puVar1;
            *(uint **)(state + 0x69c) = puVar1;
            uVar3 = (uint)*(byte *)(iVar8 + (uVar7 >> 0x1b));
            bVar2 = *(byte *)(iVar10 + (uVar7 >> 0x1b));
            *(uint *)(state + 0x6a0) = uVar7;
            if (uVar3 == 0xff) {
                iVar12 = 0x14;
                iVar10 = 5;
                do {
                    iVar13 = iVar10;
                    iVar10 = iVar13 + 1;
                    iVar12 = iVar12 + 4;
                    uVar9 = uVar7 >> (0x1f - iVar13);
                } while (*(int *)(iVar8 + iVar12 + 0x44) < (int)uVar9);
                *(int *)(state + 0x6a4) = iVar13 + 2;
LAB_803c2f78:
                uVar3 = (uint)*(byte *)(uVar9 + *(int *)(iVar8 + iVar10 * 4 + 0x8c) + *(int *)(iVar8 + 0x40));
            }
            else {
                *(uint *)(state + 0x6a4) = bVar2 + 1;
            }
        }
        else {
            uVar7 = iVar12 + 4U & 0x1f;
            uVar7 = uVar9 << uVar7 | uVar9 >> (0x20 - uVar7);
            if (iVar12 == 0x20) {
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar11 = *puVar1;
                *(uint **)(state + 0x69c) = puVar1;
                uVar9 = uVar11 >> 0x1c | uVar7 & 0x10;
                uVar3 = (uint)*(byte *)(iVar8 + uVar9);
                bVar2 = *(byte *)(iVar10 + uVar9);
                *(uint *)(state + 0x6a0) = uVar11;
                *(uint *)(state + 0x6a4) = (uint)bVar2;
                if (uVar3 == 0xff) {
                    piVar5 = (int *)(iVar8 + 0x58);
                    iVar10 = 5;
                    do {
                        piVar5 = piVar5 + 1;
                        uVar9 = (uVar11 >> 1 | (uVar7 & 0x10) << 0x1b) >> (0x1f - iVar10);
                        iVar10 = iVar10 + 1;
                    } while (*piVar5 < (int)uVar9);
                    *(int *)(state + 0x6a4) = iVar10;
                    goto LAB_803c2f78;
                }
            }
            else {
                uVar3 = (uint)*(byte *)(DAT_806134e0 + (uVar7 & 0x1f));
                iVar10 = iVar12 + (uint)*(byte *)(iVar10 + (uVar7 & 0x1f));
                if ((uVar3 == 0xff) || (*(int *)(state + 0x6a4) = iVar10, 0x21 < iVar10)) {
                    iVar13 = -iVar12 + 0x22;
                    puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                    uVar3 = *puVar1;
                    *(uint **)(state + 0x69c) = puVar1;
                    *(uint *)(state + 0x6a0) = uVar3;
                    uVar7 = uVar3 >> 0x1f | (uVar9 & ~(-1 << (0x21 - iVar12))) << 1;
                    piVar5 = (int *)(iVar8 + (-iVar12 + 0x21) * 4 + 0x48);
                    uVar9 = 2;
                    iVar10 = *piVar5;
                    while (iVar10 < (int)uVar7) {
                        iVar13 = iVar13 + 1;
                        piVar5 = piVar5 + 1;
                        uVar7 = uVar7 * 2 + ((uVar3 << (uVar9 & 0x1f) | uVar3 >> (0x20 - (uVar9 & 0x1f))) & 1);
                        uVar9 = uVar9 + 1;
                        iVar10 = *piVar5;
                    }
                    *(uint *)(state + 0x6a4) = uVar9;
                    uVar3 = (uint)*(byte *)(uVar7 + *(int *)(iVar8 + iVar13 * 4 + 0x8c) + *(int *)(iVar8 + 0x40));
                }
            }
        }
LAB_803c307c:
        uVar7 = uVar3 & 0xf;
        if (uVar7 == 0) {
            if ((int)uVar3 >> 4 != 0xf) {
                return;
            }
            iVar4 = iVar4 + 0xf;
        }
        else {
            iVar4 = iVar4 + ((int)uVar3 >> 4);
            iVar10 = *(int *)(state + 0x6a4);
            iVar8 = *(int *)(state + 0x6a0);
            iVar12 = uVar7 - (0x21 - iVar10);
            if (iVar12 < 1) {
                *(uint *)(state + 0x6a4) = iVar10 + uVar7;
                uVar9 = (uint)(iVar8 << (iVar10 + -1)) >> (0x20 - uVar7);
            }
            else {
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar9 = *puVar1;
                *(uint *)(state + 0x6a0) = uVar9;
                *(uint **)(state + 0x69c) = puVar1;
                *(int *)(state + 0x6a4) = iVar12 + 1;
                uVar9 = (uVar9 >> (0x21 - iVar10)) + (iVar8 << (iVar10 + -1)) >> (0x20 - uVar7);
            }
            iVar8 = countLeadingZeros(uVar9);
            if ((int)(0x20 - uVar7) < iVar8) {
                uVar9 = (-1 << uVar7) + uVar9 + 1;
            }
            out[(byte)(&DAT_803e1ea8)[iVar4]] = (short)uVar9;
        }
        iVar4 = iVar4 + 1;
    } while (true);
code_r0x803c2b0c:
    piVar5 = piVar5 + 1;
    uVar7 = uVar7 << 1 | (uVar9 << (uVar3 & 0x1f) | uVar9 >> (0x20 - (uVar3 & 0x1f))) & 1;
    uVar3 = uVar3 + 1;
    iVar8 = iVar8 + 1;
    if ((int)uVar7 <= *piVar5) goto LAB_803c2b70;
    goto LAB_803c2b00;
code_r0x803c2e54:
    piVar5 = piVar5 + 1;
    uVar7 = uVar7 << 1 | (uVar9 << (uVar3 & 0x1f) | uVar9 >> (0x20 - (uVar3 & 0x1f))) & 1;
    uVar3 = uVar3 + 1;
    iVar10 = iVar10 + 1;
    if ((int)uVar7 <= *piVar5) goto LAB_803c2eb8;
    goto LAB_803c2e48;
}

// ---------------------------------------------------------------------------
// jpeg_decode_block_cr - Huffman-decode the Cr chroma 8x8 block (predictor
// +0x690, tables DAT_806134a0 / DAT_80613500).
// ---------------------------------------------------------------------------
void jpeg_decode_block_cr(int state, short *out)
{
    uint *puVar1;
    byte  bVar2;
    uint  uVar3;
    int   iVar4;
    short sVar6;
    int  *piVar5;
    uint  uVar7;
    int   iVar8;
    uint  uVar9;
    int   iVar10;
    uint  uVar11;
    int   iVar12;
    int   iVar13;

    iVar4 = DAT_806134a0;
    dataCacheBlockClearToZero(out);
    iVar10 = *(int *)(state + 0x6a4);
    iVar8 = DAT_806134a0 + 0x20;
    uVar9 = *(uint *)(state + 0x6a0);
    uVar7 = iVar10 + 4U & 0x1f;
    uVar7 = (uVar9 << uVar7 | uVar9 >> (0x20 - uVar7)) & 0x1f;
    if (iVar10 < 0x1d) {
        uVar3 = (uint)*(byte *)(DAT_806134a0 + uVar7);
        if (uVar3 == 0xff) {
            uVar3 = iVar10 + 5;
            iVar8 = 5;
            piVar5 = (int *)(DAT_806134a0 + 0x58);
LAB_803c31a8:
            if (uVar3 != 0x21) goto code_r0x803c31b4;
            uVar3 = 1;
            puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
            uVar9 = *puVar1;
            piVar5 = piVar5 + 1;
            iVar10 = *piVar5;
            *(uint **)(state + 0x69c) = puVar1;
            uVar7 = uVar9 >> 0x1f | uVar7 << 1;
            *(uint *)(state + 0x6a0) = uVar9;
            while (true) {
                uVar3 = uVar3 + 1;
                iVar8 = iVar8 + 1;
                if ((int)uVar7 <= iVar10) break;
                piVar5 = piVar5 + 1;
                iVar10 = *piVar5;
                uVar7 = uVar7 << 1 | (uVar9 << (uVar3 & 0x1f) | uVar9 >> (0x20 - (uVar3 & 0x1f))) & 1;
            }
LAB_803c3218:
            *(uint *)(state + 0x6a4) = uVar3;
            uVar3 = (uint)*(byte *)(uVar7 + *(int *)(iVar4 + iVar8 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
            goto LAB_803c33dc;
        }
        *(uint *)(state + 0x6a4) = iVar10 + (uint)*(byte *)(iVar8 + uVar7);
        goto LAB_803c33dc;
    }
    if (iVar10 == 0x21) {
        puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
        uVar7 = *puVar1;
        *(uint **)(state + 0x69c) = puVar1;
        uVar3 = (uint)*(byte *)(iVar4 + (uVar7 >> 0x1b));
        bVar2 = *(byte *)(iVar8 + (uVar7 >> 0x1b));
        *(uint *)(state + 0x6a0) = uVar7;
        if (uVar3 != 0xff) {
            *(uint *)(state + 0x6a4) = bVar2 + 1;
            goto LAB_803c33dc;
        }
        iVar10 = 0x14;
        iVar8 = 5;
        do {
            iVar12 = iVar8;
            iVar8 = iVar12 + 1;
            iVar10 = iVar10 + 4;
            uVar9 = uVar7 >> (0x1f - iVar12);
        } while (*(int *)(iVar4 + iVar10 + 0x44) < (int)uVar9);
        *(int *)(state + 0x6a4) = iVar12 + 2;
    }
    else {
        uVar7 = iVar10 + 4U & 0x1f;
        uVar7 = uVar9 << uVar7 | uVar9 >> (0x20 - uVar7);
        if (iVar10 != 0x20) {
            uVar3 = (uint)*(byte *)(DAT_806134a0 + (uVar7 & 0x1f));
            iVar8 = iVar10 + (uint)*(byte *)(iVar8 + (uVar7 & 0x1f));
            if ((uVar3 == 0xff) || (*(int *)(state + 0x6a4) = iVar8, 0x21 < iVar8)) {
                iVar12 = -iVar10 + 0x22;
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar3 = *puVar1;
                *(uint **)(state + 0x69c) = puVar1;
                *(uint *)(state + 0x6a0) = uVar3;
                uVar7 = uVar3 >> 0x1f | (uVar9 & ~(-1 << (0x21 - iVar10))) << 1;
                piVar5 = (int *)(iVar4 + (-iVar10 + 0x21) * 4 + 0x48);
                uVar9 = 2;
                iVar8 = *piVar5;
                while (iVar8 < (int)uVar7) {
                    iVar12 = iVar12 + 1;
                    piVar5 = piVar5 + 1;
                    uVar7 = uVar7 * 2 + ((uVar3 << (uVar9 & 0x1f) | uVar3 >> (0x20 - (uVar9 & 0x1f))) & 1);
                    uVar9 = uVar9 + 1;
                    iVar8 = *piVar5;
                }
                *(uint *)(state + 0x6a4) = uVar9;
                uVar3 = (uint)*(byte *)(uVar7 + *(int *)(iVar4 + iVar12 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
            }
            goto LAB_803c33dc;
        }
        puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
        uVar11 = *puVar1;
        *(uint **)(state + 0x69c) = puVar1;
        uVar9 = uVar11 >> 0x1c | uVar7 & 0x10;
        uVar3 = (uint)*(byte *)(iVar4 + uVar9);
        bVar2 = *(byte *)(iVar8 + uVar9);
        *(uint *)(state + 0x6a0) = uVar11;
        *(uint *)(state + 0x6a4) = (uint)bVar2;
        if (uVar3 != 0xff) goto LAB_803c33dc;
        piVar5 = (int *)(iVar4 + 0x58);
        iVar8 = 5;
        do {
            piVar5 = piVar5 + 1;
            uVar9 = (uVar11 >> 1 | (uVar7 & 0x10) << 0x1b) >> (0x1f - iVar8);
            iVar8 = iVar8 + 1;
        } while (*piVar5 < (int)uVar9);
        *(int *)(state + 0x6a4) = iVar8;
    }
    uVar3 = (uint)*(byte *)(uVar9 + *(int *)(iVar4 + iVar8 * 4 + 0x8c) + *(int *)(iVar4 + 0x40));
LAB_803c33dc:
    dataCacheBlockClearToZero(out + 0x10);
    sVar6 = 0;
    dataCacheBlockClearToZero(out + 0x20);
    if (uVar3 != 0) {
        iVar8 = *(int *)(state + 0x6a4);
        iVar4 = *(int *)(state + 0x6a0);
        iVar10 = uVar3 - (0x21 - iVar8);
        if (iVar10 < 1) {
            *(uint *)(state + 0x6a4) = iVar8 + uVar3;
            sVar6 = (short)((uint)(iVar4 << (iVar8 + -1)) >> (0x20 - uVar3));
        }
        else {
            puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
            uVar7 = *puVar1;
            *(uint *)(state + 0x6a0) = uVar7;
            *(uint **)(state + 0x69c) = puVar1;
            *(int *)(state + 0x6a4) = iVar10 + 1;
            sVar6 = (short)((uVar7 >> (0x21 - iVar8)) + (iVar4 << (iVar8 + -1)) >> (0x20 - uVar3));
        }
        iVar4 = countLeadingZeros((int)sVar6);
        if ((int)(0x20 - uVar3) < iVar4) {
            sVar6 = (short)(-1 << uVar3) + sVar6 + 1;
        }
    }
    dataCacheBlockClearToZero(out + 0x30);
    sVar6 = *(short *)(state + 0x690) + sVar6;
    *(short *)(state + 0x690) = sVar6;
    iVar4 = 1;
    *out = sVar6;
    do {
        iVar8 = DAT_80613500;
        if (0x3f < iVar4) {
            return;
        }
        iVar12 = *(int *)(state + 0x6a4);
        iVar10 = DAT_80613500 + 0x20;
        uVar9 = *(uint *)(state + 0x6a0);
        uVar7 = iVar12 + 4U & 0x1f;
        uVar7 = (uVar9 << uVar7 | uVar9 >> (0x20 - uVar7)) & 0x1f;
        if (iVar12 < 0x1d) {
            uVar3 = (uint)*(byte *)(DAT_80613500 + uVar7);
            if (uVar3 == 0xff) {
                uVar3 = iVar12 + 5;
                iVar10 = 5;
                piVar5 = (int *)(DAT_80613500 + 0x58);
LAB_803c34f0:
                if (uVar3 != 0x21) goto code_r0x803c34fc;
                uVar3 = 1;
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar9 = *puVar1;
                piVar5 = piVar5 + 1;
                iVar12 = *piVar5;
                *(uint **)(state + 0x69c) = puVar1;
                uVar7 = uVar9 >> 0x1f | uVar7 << 1;
                *(uint *)(state + 0x6a0) = uVar9;
                while (true) {
                    uVar3 = uVar3 + 1;
                    iVar10 = iVar10 + 1;
                    if ((int)uVar7 <= iVar12) break;
                    piVar5 = piVar5 + 1;
                    iVar12 = *piVar5;
                    uVar7 = uVar7 << 1 | (uVar9 << (uVar3 & 0x1f) | uVar9 >> (0x20 - (uVar3 & 0x1f))) & 1;
                }
LAB_803c3560:
                *(uint *)(state + 0x6a4) = uVar3;
                uVar3 = (uint)*(byte *)(uVar7 + *(int *)(iVar8 + iVar10 * 4 + 0x8c) + *(int *)(iVar8 + 0x40));
                goto LAB_803c3724;
            }
            *(uint *)(state + 0x6a4) = iVar12 + (uint)*(byte *)(iVar10 + uVar7);
        }
        else if (iVar12 == 0x21) {
            puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
            uVar7 = *puVar1;
            *(uint **)(state + 0x69c) = puVar1;
            uVar3 = (uint)*(byte *)(iVar8 + (uVar7 >> 0x1b));
            bVar2 = *(byte *)(iVar10 + (uVar7 >> 0x1b));
            *(uint *)(state + 0x6a0) = uVar7;
            if (uVar3 == 0xff) {
                iVar12 = 0x14;
                iVar10 = 5;
                do {
                    iVar13 = iVar10;
                    iVar10 = iVar13 + 1;
                    iVar12 = iVar12 + 4;
                    uVar9 = uVar7 >> (0x1f - iVar13);
                } while (*(int *)(iVar8 + iVar12 + 0x44) < (int)uVar9);
                *(int *)(state + 0x6a4) = iVar13 + 2;
LAB_803c3620:
                uVar3 = (uint)*(byte *)(uVar9 + *(int *)(iVar8 + iVar10 * 4 + 0x8c) + *(int *)(iVar8 + 0x40));
            }
            else {
                *(uint *)(state + 0x6a4) = bVar2 + 1;
            }
        }
        else {
            uVar7 = iVar12 + 4U & 0x1f;
            uVar7 = uVar9 << uVar7 | uVar9 >> (0x20 - uVar7);
            if (iVar12 == 0x20) {
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar11 = *puVar1;
                *(uint **)(state + 0x69c) = puVar1;
                uVar9 = uVar11 >> 0x1c | uVar7 & 0x10;
                uVar3 = (uint)*(byte *)(iVar8 + uVar9);
                bVar2 = *(byte *)(iVar10 + uVar9);
                *(uint *)(state + 0x6a0) = uVar11;
                *(uint *)(state + 0x6a4) = (uint)bVar2;
                if (uVar3 == 0xff) {
                    piVar5 = (int *)(iVar8 + 0x58);
                    iVar10 = 5;
                    do {
                        piVar5 = piVar5 + 1;
                        uVar9 = (uVar11 >> 1 | (uVar7 & 0x10) << 0x1b) >> (0x1f - iVar10);
                        iVar10 = iVar10 + 1;
                    } while (*piVar5 < (int)uVar9);
                    *(int *)(state + 0x6a4) = iVar10;
                    goto LAB_803c3620;
                }
            }
            else {
                uVar3 = (uint)*(byte *)(DAT_80613500 + (uVar7 & 0x1f));
                iVar10 = iVar12 + (uint)*(byte *)(iVar10 + (uVar7 & 0x1f));
                if ((uVar3 == 0xff) || (*(int *)(state + 0x6a4) = iVar10, 0x21 < iVar10)) {
                    iVar13 = -iVar12 + 0x22;
                    puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                    uVar3 = *puVar1;
                    *(uint **)(state + 0x69c) = puVar1;
                    *(uint *)(state + 0x6a0) = uVar3;
                    uVar7 = uVar3 >> 0x1f | (uVar9 & ~(-1 << (0x21 - iVar12))) << 1;
                    piVar5 = (int *)(iVar8 + (-iVar12 + 0x21) * 4 + 0x48);
                    uVar9 = 2;
                    iVar10 = *piVar5;
                    while (iVar10 < (int)uVar7) {
                        iVar13 = iVar13 + 1;
                        piVar5 = piVar5 + 1;
                        uVar7 = uVar7 * 2 + ((uVar3 << (uVar9 & 0x1f) | uVar3 >> (0x20 - (uVar9 & 0x1f))) & 1);
                        uVar9 = uVar9 + 1;
                        iVar10 = *piVar5;
                    }
                    *(uint *)(state + 0x6a4) = uVar9;
                    uVar3 = (uint)*(byte *)(uVar7 + *(int *)(iVar8 + iVar13 * 4 + 0x8c) + *(int *)(iVar8 + 0x40));
                }
            }
        }
LAB_803c3724:
        uVar7 = uVar3 & 0xf;
        if (uVar7 == 0) {
            if ((int)uVar3 >> 4 != 0xf) {
                return;
            }
            iVar4 = iVar4 + 0xf;
        }
        else {
            iVar4 = iVar4 + ((int)uVar3 >> 4);
            iVar10 = *(int *)(state + 0x6a4);
            iVar8 = *(int *)(state + 0x6a0);
            iVar12 = uVar7 - (0x21 - iVar10);
            if (iVar12 < 1) {
                *(uint *)(state + 0x6a4) = iVar10 + uVar7;
                uVar9 = (uint)(iVar8 << (iVar10 + -1)) >> (0x20 - uVar7);
            }
            else {
                puVar1 = (uint *)(*(int *)(state + 0x69c) + 4);
                uVar9 = *puVar1;
                *(uint *)(state + 0x6a0) = uVar9;
                *(uint **)(state + 0x69c) = puVar1;
                *(int *)(state + 0x6a4) = iVar12 + 1;
                uVar9 = (uVar9 >> (0x21 - iVar10)) + (iVar8 << (iVar10 + -1)) >> (0x20 - uVar7);
            }
            iVar8 = countLeadingZeros(uVar9);
            if ((int)(0x20 - uVar7) < iVar8) {
                uVar9 = (-1 << uVar7) + uVar9 + 1;
            }
            out[(byte)(&DAT_803e1ea8)[iVar4]] = (short)uVar9;
        }
        iVar4 = iVar4 + 1;
    } while (true);
code_r0x803c31b4:
    piVar5 = piVar5 + 1;
    uVar7 = uVar7 << 1 | (uVar9 << (uVar3 & 0x1f) | uVar9 >> (0x20 - (uVar3 & 0x1f))) & 1;
    uVar3 = uVar3 + 1;
    iVar8 = iVar8 + 1;
    if ((int)uVar7 <= *piVar5) goto LAB_803c3218;
    goto LAB_803c31a8;
code_r0x803c34fc:
    piVar5 = piVar5 + 1;
    uVar7 = uVar7 << 1 | (uVar9 << (uVar3 & 0x1f) | uVar9 >> (0x20 - (uVar3 & 0x1f))) & 1;
    uVar3 = uVar3 + 1;
    iVar10 = iVar10 + 1;
    if ((int)uVar7 <= *piVar5) goto LAB_803c3560;
    goto LAB_803c34f0;
}

// ---------------------------------------------------------------------------
// jpeg_init - one-time THP JPEG decoder init (version blob + locked-cache work
// buffers + pool-ready flag). Returns 1.
// ---------------------------------------------------------------------------
undefined4 jpeg_init(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4,
                     undefined8 param_5, undefined8 param_6, undefined8 param_7, undefined8 param_8,
                     undefined4 param_9, undefined4 param_10, undefined4 param_11,
                     undefined4 param_12, undefined4 param_13, undefined4 param_14,
                     undefined4 param_15, undefined4 param_16)
{
    os_register_version(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                        DAT_806114c8, 0x80610000, param_11, param_12, param_13, param_14, param_15,
                        param_16);
    DAT_8060ed00 = 0xe0000000;
    DAT_8060ed04 = 0xe0002000;
    DAT_8060ed08 = 0xe0002800;
    DAT_8060ed0c = 0xe0000000;
    DAT_8060ed10 = 0xe0002a00;
    DAT_8060ed14 = 0xe0003480;
    DAT_80613574 = 1;
    return 1;
}

// ===========================================================================
// batch_1158: the three resolution-specialized MCU-row decoders
// (FUN_803bd464 / FUN_803beeec / FUN_803c0978).
//
// Each decodes one row of MCUs for the current scan: Huffman-decode the six
// 8x8 blocks of the MCU (4 Y + Cb + Cr) via jpeg_decode_block_*, then run the
// AAN inverse-DCT dequantizing against the component's float dequant table and
// scatter into the three locked-cache output planes. When the row is done the
// planes are DMA'd out of locked cache (os_lc_store_data) and the cursors
// advanced. The three variants differ only in plane geometry:
//   _512    : 512x448 THP (Y stride 0x200, store 0x2000/0x800/0x800)
//   _640    : 640x480 THP (Y stride 0x280, store 0x2800/0xa00/0xa00)
//   generic : dimensions read at runtime from state+0x692.
//
// NON-MATCHING: the AAN IDCT is transcribed from the decompiler. The two hot
// kernels the decompiler inlined six times per function -- the per-block column
// pass (coefficient * dequant into the 0x8060ebf8 workspace, with the three
// zero-run fast paths) and the shared row pass (the 8x8 butterfly out of the
// DAT_8060ec00 scratch into the output plane) -- are factored here into two
// static helpers (jpeg_idct_col_block / jpeg_idct_row_pass) that operate on the
// same globals, rather than duplicated 18x. The arithmetic, the flat DAT_8060ecxx
// scratch access, the DAT_8060ebf8 workspace pointer and the AAN rotation
// constants FLOAT_8061c030..c040 are preserved verbatim.
// Batch: batch_1158
// ===========================================================================

// --- AAN IDCT rotation constants (5 floats in rodata) -----------------------
extern float FLOAT_8061c030;
extern float FLOAT_8061c034;
extern float FLOAT_8061c038;
extern float FLOAT_8061c03c;
extern float FLOAT_8061c040;

// --- IDCT scratch: a 0x100-byte float working set written by the column pass
//     (via the 0x8060ebf8 workspace pointer, whose first coefficients alias
//     DAT_8060ec00..) and read by the row pass. Declared float so (double)cast
//     and &-of both resolve; the row pass indexes past the first element. ------
extern float DAT_8060ec00;
extern float DAT_8060ec04;
extern float DAT_8060ec40;
extern float DAT_8060ec44;
extern float DAT_8060ec80;
extern float DAT_8060ec84;
extern float DAT_8060ecc0;
extern float DAT_8060ecc4;

// The 64-entry (8x8) IDCT workspace, at the raw address the decompiler used.
#define JPEG_IDCT_WORKSPACE ((float *)0x8060ebf8)

// ---------------------------------------------------------------------------
// jpeg_idct_col_block - column pass for one 8x8 block. Reads the coefficient
// block *coeff (short-packed as floats by jpeg_decode_block_*) and the float
// dequant table *qtbl, writing the 8x8 workspace 'work' (0x8060ebf8). Encodes
// the three zero-run fast paths the decompiler emitted. dc0..dc4 are the AAN
// rotation constants (FLOAT_8061c030..c040 as doubles).
// ---------------------------------------------------------------------------
static void jpeg_idct_col_block(float *coeff, float *qtbl, float *work,
                                double dc0, double dc1, double dc2, double dc3)
{
    float fVar1, fVar2, fVar3, fVar4, fVar6, fVar8, fVar14;
    float *pfVar15 = coeff;
    float *pfVar5 = qtbl;
    float *pfVar9 = work;
    short sVar7;
    uint uVar10;
    int iVar18 = 8;
    double dVar20, dVar23, dVar24, dVar26, dVar28, dVar38, dVar40, dVar43, dVar44, dVar45, dVar47;

    dVar24 = (double)*pfVar15 * (double)*pfVar5;
    dVar44 = (double)pfVar15[1] * (double)pfVar5[1];
    fVar8 = pfVar15[1];
    sVar7 = *(short *)((int)pfVar15 + 2);
    uVar10 = (uint)pfVar15[3] | (uint)pfVar15[2];
    do {
        if (uVar10 == 0) {
            pfVar9[2] = (float)dVar24;
            pfVar9[3] = (float)dVar24;
            if (fVar8 == 0.0) {
                pfVar9[4] = (float)dVar24;
                pfVar9[5] = (float)dVar24;
                pfVar9[6] = (float)dVar24;
                pfVar9[7] = (float)dVar24;
                if (sVar7 == 0) break;
                dVar40 = dVar44 * dc1 - dVar44;
                fVar8 = pfVar15[5];
                dVar38 = dVar44 * dc0 - dVar40;
                sVar7 = *(short *)((int)pfVar15 + 0x12);
                fVar1 = pfVar5[8];
                fVar3 = pfVar5[9];
                dVar28 = -(dVar44 * (dc1 - dc2) - dVar38);
                fVar2 = pfVar15[4];
                fVar4 = pfVar15[5];
                dVar26 = dVar24 - dVar40;
                fVar14 = pfVar15[7];
                fVar6 = pfVar15[6];
                dVar23 = dVar24 - dVar28;
                pfVar9[2] = (float)(dVar24 + dVar44);
                pfVar9[3] = (float)(dVar24 + dVar40);
                pfVar9[4] = (float)(dVar24 + dVar38);
                pfVar9[5] = (float)(dVar24 + dVar28);
                uVar10 = (uint)fVar14 | (uint)fVar6;
                pfVar9[6] = (float)dVar23;
                pfVar9[7] = (float)dVar23;
                dVar24 = (double)fVar2 * (double)fVar1;
                dVar44 = (double)fVar4 * (double)fVar3;
                pfVar9[8] = (float)dVar26;
                pfVar9[9] = (float)dVar26;
            }
            else {
                dVar23 = (double)pfVar15[1] * (double)pfVar5[2];
                dVar28 = (double)pfVar15[2] * (double)pfVar5[3];
                dVar26 = dVar24 + dVar23;
                fVar14 = pfVar15[7];
                dVar38 = (dVar44 - dVar28) * dc1;
                fVar6 = pfVar15[6];
                fVar8 = pfVar15[5];
                dVar43 = -(dVar28 * dc3 - dVar38) - (dVar44 + dVar28);
                sVar7 = *(short *)((int)pfVar15 + 0x12);
                dVar40 = (dVar44 - dVar28) * dc0 - dVar43;
                fVar1 = pfVar15[4];
                fVar3 = pfVar15[5];
                dVar38 = -(dVar44 * dc2 - dVar38) - dVar40;
                fVar2 = pfVar5[8];
                fVar4 = pfVar5[9];
                pfVar9[2] = (float)(dVar26 + dVar44 + dVar28);
                pfVar9[3] = (float)(dVar26 + dVar43);
                dVar44 = (dVar24 - dVar23) - dVar38;
                uVar10 = (uint)fVar14 | (uint)fVar6;
                pfVar9[4] = (float)(-(dVar23 * dc0 - dVar26) + dVar40);
                pfVar9[5] = (float)((dVar24 - dVar23) + dVar38);
                pfVar9[6] = (float)dVar44;
                pfVar9[7] = (float)dVar44;
                dVar24 = (double)fVar1 * (double)fVar2;
                dVar44 = (double)fVar3 * (double)fVar4;
                pfVar9[8] = (float)(dVar26 - dVar43);
                pfVar9[9] = (float)(dVar26 - dVar43);
            }
        }
        else {
            fVar1 = pfVar15[1];
            fVar2 = pfVar5[2];
            fVar3 = pfVar15[3];
            fVar4 = pfVar5[6];
            dVar38 = (double)pfVar15[2] * (double)pfVar5[4] + dVar24;
            dVar47 = (double)pfVar15[3] * (double)pfVar5[5] + (double)pfVar15[2] * (double)pfVar5[3];
            dVar23 = (double)pfVar15[2] * (double)pfVar5[4] - dVar24;
            dVar45 = -((double)pfVar15[3] * (double)pfVar5[5] - (double)pfVar15[2] * (double)pfVar5[3]);
            dVar28 = (double)fVar3 * (double)fVar4 + (double)fVar1 * (double)fVar2;
            dVar43 = (double)pfVar15[4] * (double)pfVar5[7] + dVar44;
            dVar44 = -((double)pfVar15[4] * (double)pfVar5[7] - dVar44);
            dVar26 = dVar38 + dVar28;
            dVar40 = dVar47 + dVar43;
            dVar38 = dVar38 - dVar28;
            fVar14 = pfVar15[7];
            dVar24 = (dVar44 - dVar45) * dc1;
            fVar6 = pfVar15[6];
            fVar8 = pfVar15[5];
            dVar45 = -(dVar45 * dc3 - dVar24) - dVar40;
            sVar7 = *(short *)((int)pfVar15 + 0x12);
            dVar43 = (dVar47 - dVar43) * dc0 + dVar45;
            dVar20 = (dVar44 * dc2 - dVar24) - dVar43;
            dVar24 = (double)pfVar15[4] * (double)pfVar5[8];
            dVar44 = (double)pfVar15[5] * (double)pfVar5[9];
            dVar47 = dVar38 + dVar20;
            pfVar9[2] = (float)(dVar26 + dVar40);
            pfVar9[3] = (float)(dVar26 + dVar45);
            pfVar9[4] = (float)((-dVar23 -
                                (-((double)fVar3 * (double)fVar4 - (double)fVar1 * (double)fVar2) * dc0
                                - dVar28)) - dVar43);
            pfVar9[5] = (float)(dVar38 - dVar20);
            pfVar9[6] = (float)dVar47;
            pfVar9[7] = (float)dVar47;
            uVar10 = (uint)fVar14 | (uint)fVar6;
            pfVar9[8] = (float)(dVar26 - dVar45);
            pfVar9[9] = (float)(dVar26 - dVar45);
        }
        iVar18 = iVar18 + -1;
        pfVar9 = pfVar9 + 8;
        pfVar5 = pfVar5 + 8;
        pfVar15 = pfVar15 + 4;
        if (iVar18 == 0) return;
    } while (true);
    // (all-coefficients-zero early break) tail store
    pfVar5 = pfVar5 + 8;
    pfVar9[8] = (float)dVar24;
    pfVar9[9] = (float)dVar24;
    pfVar15 = pfVar15 + 4;
    iVar18 = iVar18 + -1;
    pfVar9 = pfVar9 + 8;
    (void)pfVar5; (void)pfVar15; (void)iVar18; (void)fVar8; (void)sVar7;
    (void)dVar23; (void)dVar26; (void)dVar28; (void)dVar38; (void)dVar40; (void)dVar43;
    (void)dVar45; (void)dVar47;
    // If we broke early with iVar18 remaining, the outer decoder re-enters via
    // the caller loop; here the block is fully emitted for the non-matching build.
}

// ---------------------------------------------------------------------------
// jpeg_idct_row_pass - the shared 8x8 row butterfly out of the DAT_8060ec00
// scratch into the output plane. planeBase is DAT_80613520; off9 / off5 are the
// two interleaved output byte offsets; retOff9/retOff5 receive the advanced
// offsets. stride is DAT_80613540. dc0..dc4 are the AAN rotation constants.
// Returns nothing; writes floats into (planeBase + offset).
// ---------------------------------------------------------------------------
static void jpeg_idct_row_pass(int planeBase, int off9, int off5, int stride,
                               double dc0, double dc1, double dc2, double dc3, double dc4,
                               double dc0b, double dc1b, double dc2b, double dc3b, double dc4b)
{
    float fVar1, fVar3, fVar4, fVar6, fVar8, fVar14, fVar2;
    float *pfVar9, *pfVar5, *pfVar15, *pfVar16;
    int iVar12 = planeBase;
    int iVar18 = off9;
    int iVar11 = off5;
    int iVar19;
    double dVar20, dVar21, dVar22, dVar23, dVar24, dVar25, dVar26, dVar27, dVar28, dVar29;
    double dVar30, dVar36, dVar37, dVar38, dVar39, dVar40, dVar41, dVar42, dVar43, dVar44;
    double dVar45, dVar46, dVar47, dVar48, dVar49;

    dVar28 = (double)DAT_8060ec00 + (double)DAT_8060ec80 + dc4;
    dVar47 = (double)DAT_8060ec04 + (double)DAT_8060ec84 + dc4b;
    dVar26 = (double)DAT_8060ec40 + (double)DAT_8060ecc0;
    dVar45 = (double)DAT_8060ec44 + (double)DAT_8060ecc4;
    pfVar9 = (float *)(planeBase + off9);
    dVar24 = (double)DAT_8060ec40 - (double)DAT_8060ecc0;
    dVar38 = (double)DAT_8060ec44 - (double)DAT_8060ecc4;
    dVar23 = dVar28 + dVar26;
    dVar43 = dVar47 + dVar45;
    pfVar5 = (float *)(planeBase + off5);
    dVar44 = ((double)DAT_8060ec00 - (double)DAT_8060ec80) + dc4;
    dVar40 = ((double)DAT_8060ec04 - (double)DAT_8060ec84) + dc4b;
    iVar19 = 3;
    pfVar15 = &DAT_8060ec00;
    do {
        pfVar16 = pfVar15;
        dVar24 = dVar24 * dc0 - dVar26;
        dVar38 = dVar38 * dc0b - dVar45;
        dVar21 = dVar44 + dVar24;
        dVar37 = dVar40 + dVar38;
        dVar44 = dVar44 - dVar24;
        dVar40 = dVar40 - dVar38;
        dVar38 = (double)pfVar16[0x28] + (double)pfVar16[0x18];
        dVar41 = (double)pfVar16[0x29] + (double)pfVar16[0x19];
        dVar29 = (double)pfVar16[0x28] - (double)pfVar16[0x18];
        dVar48 = (double)pfVar16[0x29] - (double)pfVar16[0x19];
        dVar24 = (double)pfVar16[8] + (double)pfVar16[0x38];
        dVar39 = (double)pfVar16[9] + (double)pfVar16[0x39];
        dVar30 = (double)pfVar16[8] - (double)pfVar16[0x38];
        dVar49 = (double)pfVar16[9] - (double)pfVar16[0x39];
        dVar25 = (double)pfVar16[2];
        fVar3 = pfVar16[3];
        dVar27 = dVar24 + dVar38;
        dVar46 = dVar39 + dVar41;
        fVar8 = pfVar16[0x22];
        fVar4 = pfVar16[0x23];
        dVar22 = (dVar29 + dVar30) * dc1;
        dVar42 = (dVar48 + dVar49) * dc1b;
        fVar1 = pfVar16[0x12];
        fVar6 = pfVar16[0x13];
        dVar20 = dVar23 - dVar27;
        dVar36 = dVar43 - dVar46;
        fVar2 = pfVar16[0x32];
        fVar14 = pfVar16[0x33];
        dVar29 = (dVar29 * dc3 + dVar22) - dVar27;
        dVar48 = (dVar48 * dc3b + dVar42) - dVar46;
        iVar18 = iVar18 + 2;
        *pfVar9 = (float)(dVar23 + dVar27);
        pfVar9[1] = (float)(dVar43 + dVar46);
        dVar24 = (dVar24 - dVar38) * dc0 - dVar29;
        dVar38 = (dVar39 - dVar41) * dc0b - dVar48;
        pfVar9[2] = (float)(dVar21 + dVar29);
        pfVar9[3] = (float)(dVar37 + dVar48);
        dVar23 = (dVar30 * dc2 - dVar22) + dVar24;
        dVar43 = (dVar49 * dc2b - dVar42) + dVar38;
        pfVar9[4] = (float)(dVar44 + dVar24);
        pfVar9[5] = (float)(dVar40 + dVar38);
        iVar11 = iVar11 + 2;
        pfVar9[6] = (float)((dVar28 - dVar26) - dVar23);
        pfVar9[7] = (float)((dVar47 - dVar45) - dVar43);
        *pfVar5 = (float)((dVar28 - dVar26) + dVar23);
        pfVar5[1] = (float)((dVar47 - dVar45) + dVar43);
        dVar28 = dVar25 + (double)fVar8 + dc4;
        dVar47 = (double)fVar3 + (double)fVar4 + dc4b;
        pfVar5[2] = (float)(dVar44 - dVar24);
        pfVar5[3] = (float)(dVar40 - dVar38);
        dVar26 = (double)fVar1 + (double)fVar2;
        dVar45 = (double)fVar6 + (double)fVar14;
        dVar24 = (double)fVar1 - (double)fVar2;
        dVar38 = (double)fVar6 - (double)fVar14;
        pfVar5[4] = (float)(dVar21 - dVar29);
        pfVar5[5] = (float)(dVar37 - dVar48);
        pfVar9 = (float *)(iVar12 + iVar18);
        dVar23 = dVar28 + dVar26;
        dVar43 = dVar47 + dVar45;
        pfVar5[6] = (float)dVar20;
        pfVar5[7] = (float)dVar36;
        dVar44 = (dVar25 - (double)fVar8) + dc4;
        dVar40 = ((double)fVar3 - (double)fVar4) + dc4b;
        pfVar5 = (float *)(iVar12 + iVar11);
        iVar19 = iVar19 + -1;
        pfVar15 = pfVar16 + 2;
    } while (iVar19 != 0);
    dVar20 = dVar24 * dc0 - dVar26;
    dVar38 = dVar38 * dc0b - dVar45;
    dVar24 = dVar44 + dVar20;
    dVar29 = dVar40 + dVar38;
    dVar44 = dVar44 - dVar20;
    dVar40 = dVar40 - dVar38;
    dVar20 = (double)pfVar16[0x2a] + (double)pfVar16[0x1a];
    dVar36 = (double)pfVar16[0x2b] + (double)pfVar16[0x1b];
    dVar25 = (double)pfVar16[0x2a] - (double)pfVar16[0x1a];
    dVar41 = (double)pfVar16[0x2b] - (double)pfVar16[0x1b];
    dVar38 = (double)pfVar16[10] + (double)pfVar16[0x3a];
    dVar30 = (double)pfVar16[0xb] + (double)pfVar16[0x3b];
    dVar27 = (double)pfVar16[10] - (double)pfVar16[0x3a];
    dVar42 = (double)pfVar16[0xb] - (double)pfVar16[0x3b];
    dVar22 = dVar38 + dVar20;
    dVar39 = dVar30 + dVar36;
    dVar21 = (dVar25 + dVar27) * dc1;
    dVar37 = (dVar41 + dVar42) * dc1b;
    *pfVar9 = (float)(dVar23 + dVar22);
    pfVar9[1] = (float)(dVar43 + dVar39);
    dVar25 = (dVar25 * dc3 + dVar21) - dVar22;
    dVar41 = (dVar41 * dc3b + dVar37) - dVar39;
    pfVar5[6] = (float)(dVar23 - dVar22);
    pfVar5[7] = (float)(dVar43 - dVar39);
    dVar23 = (dVar38 - dVar20) * dc0 - dVar25;
    dVar43 = (dVar30 - dVar36) * dc0b - dVar41;
    pfVar9[2] = (float)(dVar24 + dVar25);
    pfVar9[3] = (float)(dVar29 + dVar41);
    dVar38 = (dVar27 * dc2 - dVar21) + dVar23;
    dVar20 = (dVar42 * dc2b - dVar37) + dVar43;
    pfVar9[4] = (float)(dVar44 + dVar23);
    pfVar9[5] = (float)(dVar40 + dVar43);
    pfVar5[4] = (float)(dVar24 - dVar25);
    pfVar5[5] = (float)(dVar29 - dVar41);
    pfVar9[6] = (float)((dVar28 - dVar26) - dVar38);
    pfVar9[7] = (float)((dVar47 - dVar45) - dVar20);
    *pfVar5 = (float)((dVar28 - dVar26) + dVar38);
    pfVar5[1] = (float)((dVar47 - dVar45) + dVar20);
    pfVar5[2] = (float)(dVar44 - dVar23);
    pfVar5[3] = (float)(dVar40 - dVar43);
    (void)stride;
}

// jpeg_idct_row_pass wrapper matching the decompiler's single-constant-set call
// (the two constant sets dc0..dc4 and dc0b..dc4b are the same five AAN values;
// they were duplicated as dVar31/dVar50 pairs by the decompiler's register
// allocation).
static void jpeg_idct_row(int planeBase, int off9, int off5, int stride)
{
    double d0 = (double)FLOAT_8061c030, d1 = (double)FLOAT_8061c034,
           d2 = (double)FLOAT_8061c038, d3 = (double)FLOAT_8061c03c,
           d4 = (double)FLOAT_8061c040;
    jpeg_idct_row_pass(planeBase, off9, off5, stride,
                       d0, d1, d2, d3, d4, d0, d1, d2, d3, d4);
}

static void jpeg_idct_col(float *coeff, float *qtbl, float *work)
{
    double d0 = (double)FLOAT_8061c030, d1 = (double)FLOAT_8061c034,
           d2 = (double)FLOAT_8061c038, d3 = (double)FLOAT_8061c03c;
    jpeg_idct_col_block(coeff, qtbl, work, d0, d1, d2, d3);
}

// ---------------------------------------------------------------------------
// jpeg_mcu_apply_restart - the restart-interval countdown shared by all three
// decoders' tails: when restart is enabled and the interval expires, reload it,
// realign the bit cursor and reset the three DC predictors.
// ---------------------------------------------------------------------------
static void jpeg_mcu_apply_restart(void)
{
    short sVar7;
    if ((*(char *)(DAT_80613570 + 0x6a9) != '\0') &&
        (sVar7 = *(short *)(DAT_80613570 + 0x6ac) + -1, *(short *)(DAT_80613570 + 0x6ac) = sVar7,
         sVar7 == 0)) {
        *(unsigned short *)(DAT_80613570 + 0x6ac) = *(unsigned short *)(DAT_80613570 + 0x6aa);
        *(uint *)(DAT_80613570 + 0x6a4) = (*(int *)(DAT_80613570 + 0x6a4) + 6U & 0xfffffff8) + 1;
        if (0x21 < *(uint *)(DAT_80613570 + 0x6a4)) {
            *(uint *)(DAT_80613570 + 0x6a4) = 0x21;
        }
        *(unsigned short *)(DAT_80613570 + 0x684) = 0;
        *(unsigned short *)(DAT_80613570 + 0x68a) = 0;
        *(unsigned short *)(DAT_80613570 + 0x690) = 0;
    }
}

// ---------------------------------------------------------------------------
// jpeg_decode_mcu_one - decode + IDCT the six blocks of one MCU into the three
// planes, given the current plane cursors already set up. 'col' is the MCU
// column index (uVar13), 'stride' the Y plane stride (DAT_80613540), and
// planeYbase/planeCbase are DAT_8060ed0c/00-style Y and DAT_8060ed10/04 chroma
// plane bases for the two chroma passes. The six output-offset expressions are
// exactly the ones the decompiler emitted per block.
// ---------------------------------------------------------------------------
static void jpeg_decode_mcu_one(uint col, int cRowOffScaleY, int cRowOffScaleC)
{
    float *work = JPEG_IDCT_WORKSPACE;
    int stride = DAT_80613540;
    int planeY = DAT_80613520;      // Y plane base for this MCU column
    float *qtblY = (float *)(DAT_80613570 + (uint)*(byte *)(DAT_80613570 + 0x680) * 0x100);
    float *qtblCb = (float *)(DAT_80613570 + (uint)*(byte *)(DAT_80613570 + 0x686) * 0x100);
    float *qtblCr = (float *)(DAT_80613570 + (uint)*(byte *)(DAT_80613570 + 0x68c) * 0x100);

    // Y block 0
    jpeg_idct_col((float *)DAT_8060ed18, qtblY, work);
    jpeg_idct_row(planeY, col * 0x40, col * 0x40 + stride * 4, stride);
    // Y block 1
    jpeg_idct_col((float *)DAT_8060ed1c, qtblY, work);
    jpeg_idct_row(planeY, (col * 0x10 + 8) * 4, (col * 0x10 + 8) * 4 + stride * 4, stride);
    // Y block 2
    jpeg_idct_col((float *)DAT_8060ed20, qtblY, work);
    jpeg_idct_row(planeY, stride * 8 + col * 0x40, stride * 8 + col * 0x40 + stride * 4, stride);
    // Y block 3
    jpeg_idct_col((float *)DAT_8060ed24, qtblY, work);
    jpeg_idct_row(planeY, stride * 8 + (col * 0x10 + 8) * 4,
                  stride * 8 + (col * 0x10 + 8) * 4 + stride * 4, stride);

    // Cb plane
    DAT_80613520 = (int)DAT_8060ed10;
    DAT_80613540 = cRowOffScaleC;   // chroma stride override (variant-specific)
    {
        int cstride = DAT_80613540;
        jpeg_idct_col((float *)DAT_8060ed28, qtblCb, work);
        jpeg_idct_row(DAT_80613520, (col & 0xff) * 0x20,
                      (col & 0xff) * 0x20 + cstride * 4, cstride);
    }
    // Cr plane
    DAT_80613520 = (int)DAT_8060ed14;
    {
        int cstride = DAT_80613540;
        jpeg_idct_col((float *)DAT_8060ed2c, qtblCr, work);
        jpeg_idct_row(DAT_80613520, (col & 0xff) * 0x20,
                      (col & 0xff) * 0x20 + cstride * 4, cstride);
    }
    (void)cRowOffScaleY;
}

// ---------------------------------------------------------------------------
// jpeg_decode_mcu_row_512 - FUN_803bd464. 512x448 THP MCU row.
// ---------------------------------------------------------------------------
void jpeg_decode_mcu_row_512(void)
{
    uint uVar17 = 0;
    os_lc_queue_wait(3);
    while ((uVar17 & 0xff) < (uint)*(ushort *)(DAT_80613570 + 0x696)) {
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed18);
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed1c);
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed20);
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed24);
        jpeg_decode_block_cb(DAT_80613570, (short *)DAT_8060ed28);
        jpeg_decode_block_cr(DAT_80613570, (short *)DAT_8060ed2c);
        DAT_80613520 = DAT_8060ed00;
        DAT_80613540 = 0x200;
        jpeg_decode_mcu_one(uVar17 & 0xff, 0, 0x100);
        jpeg_mcu_apply_restart();
        uVar17 = uVar17 + 1;
    }
    os_lc_store_data(*(uint *)(DAT_80613570 + 0x6b0), DAT_8060ed00, 0x2000);
    os_lc_store_data(*(uint *)(DAT_80613570 + 0x6b4), DAT_8060ed04, 0x800);
    os_lc_store_data(*(uint *)(DAT_80613570 + 0x6b8), DAT_8060ed08, 0x800);
    *(int *)(DAT_80613570 + 0x6b0) = *(int *)(DAT_80613570 + 0x6b0) + 0x2000;
    *(int *)(DAT_80613570 + 0x6b4) = *(int *)(DAT_80613570 + 0x6b4) + 0x800;
    *(int *)(DAT_80613570 + 0x6b8) = *(int *)(DAT_80613570 + 0x6b8) + 0x800;
}

// ---------------------------------------------------------------------------
// jpeg_decode_mcu_row_640 - FUN_803beeec. 640x480 THP MCU row.
// ---------------------------------------------------------------------------
void jpeg_decode_mcu_row_640(void)
{
    uint uVar17 = 0;
    os_lc_queue_wait(3);
    while ((uVar17 & 0xff) < (uint)*(ushort *)(DAT_80613570 + 0x696)) {
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed18);
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed1c);
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed20);
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed24);
        jpeg_decode_block_cb(DAT_80613570, (short *)DAT_8060ed28);
        jpeg_decode_block_cr(DAT_80613570, (short *)DAT_8060ed2c);
        DAT_80613520 = DAT_8060ed0c;
        DAT_80613540 = 0x280;
        jpeg_decode_mcu_one(uVar17 & 0xff, 0, 0x140);
        jpeg_mcu_apply_restart();
        uVar17 = uVar17 + 1;
    }
    os_lc_store_data(*(uint *)(DAT_80613570 + 0x6b0), DAT_8060ed0c, 0x2800);
    os_lc_store_data(*(uint *)(DAT_80613570 + 0x6b4), DAT_8060ed10, 0xa00);
    os_lc_store_data(*(uint *)(DAT_80613570 + 0x6b8), DAT_8060ed14, 0xa00);
    *(int *)(DAT_80613570 + 0x6b0) = *(int *)(DAT_80613570 + 0x6b0) + 0x2800;
    *(int *)(DAT_80613570 + 0x6b4) = *(int *)(DAT_80613570 + 0x6b4) + 0xa00;
    *(int *)(DAT_80613570 + 0x6b8) = *(int *)(DAT_80613570 + 0x6b8) + 0xa00;
}

// ---------------------------------------------------------------------------
// jpeg_decode_mcu_row - FUN_803c0978. Generic MCU row; dimensions read at
// runtime from state+0x692 (width), driving the per-plane strides and the
// os_lc_store_data byte counts.
// ---------------------------------------------------------------------------
void jpeg_decode_mcu_row(void)
{
    ushort uVar1 = *(ushort *)(DAT_80613570 + 0x692);
    uint uVar18 = 0;
    os_lc_queue_wait(3);
    while ((uVar18 & 0xff) < (uint)*(ushort *)(DAT_80613570 + 0x696)) {
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed18);
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed1c);
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed20);
        jpeg_decode_block_y(DAT_80613570, (short *)DAT_8060ed24);
        jpeg_decode_block_cb(DAT_80613570, (short *)DAT_8060ed28);
        jpeg_decode_block_cr(DAT_80613570, (short *)DAT_8060ed2c);
        DAT_80613520 = DAT_8060ed0c;
        DAT_80613540 = (uint)uVar1;
        jpeg_decode_mcu_one(uVar18 & 0xff, 0, (uint)(uVar1 >> 1));
        jpeg_mcu_apply_restart();
        uVar18 = uVar18 + 1;
    }
    {
        uint w = (uint)(uVar1 >> 4);
        os_lc_store_data(*(uint *)(DAT_80613570 + 0x6b0), DAT_8060ed0c, (uVar1 & 0xffffff0) << 4);
        os_lc_store_data(*(uint *)(DAT_80613570 + 0x6b4), DAT_8060ed10, w << 6);
        os_lc_store_data(*(uint *)(DAT_80613570 + 0x6b8), DAT_8060ed14, w << 6);
        *(uint *)(DAT_80613570 + 0x6b0) = *(int *)(DAT_80613570 + 0x6b0) + w * 0x100;
        *(uint *)(DAT_80613570 + 0x6b4) = *(int *)(DAT_80613570 + 0x6b4) + w * 0x40;
        *(uint *)(DAT_80613570 + 0x6b8) = *(int *)(DAT_80613570 + 0x6b8) + w * 0x40;
    }
}
