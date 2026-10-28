// os_console.cpp - Dolphin OS debug-console font blitter
//
// Address: 0x8039b484 | Name: os_console_draw_text | Date: 2026-09-14
//
// Renders a NUL-terminated string into a YUYV-style framebuffer using a packed
// 4-bit-per-pixel hex glyph font. Each glyph is decoded via FUN_8039ca00 into a
// 0x18-row bitmap held across three scratch planes, then composited into the
// framebuffer with a foreground colour (colour[0]=luma scale, colour[1..2]=the
// two chroma bytes). Newlines advance the pen by lineAdvance rows; the routine
// stops at the string end or when the next glyph row would run past the bottom
// of the framebuffer. Variable names mirror the decompiler output verbatim to
// keep the non-matching translation faithful.

#include "common.h"

extern "C" {
// Glyph decoder: fills the 0x18-row scratch bitmap (three interleaved planes
// starting at planeBase) for the next glyph in text, returning the advanced
// text pointer and writing the glyph width to *outWidth.
extern int os_font_get_glyph(const char *text, int planeBase, int, int rows, uint *outWidth);
}

// __OSConsoleDrawText-style blitter. param_1=framebuffer base, param_2 packs the
// framebuffer width in its low 16 bits, param_3 packs the height, param_4 points
// at {luma, chromaA, chromaB}, param_5=pen X, param_6=pen Y, param_7=per-line
// row advance, param_8=text.
void os_console_draw_text(int param_1, uint param_2, uint param_3, byte *param_4, uint param_5,
                          int param_6, int param_7, char *param_8)
{
    uint uVar1;
    uint uVar2;
    int iVar3;
    int iVar4;
    uint uVar5;
    int iVar6;
    uint uVar7;
    int iVar8;
    int iVar9;
    uint uVar10;
    uint uVar11;
    int iVar12;
    uint local_164[8];
    undefined4 local_144[8];
    undefined4 local_124[56];
    uint local_44;

    uVar1 = param_2 & 0xffff;
LAB_8039b4cc:
    do {
        if ((int)((param_3 & 0xffff) - 0x18) < param_6) {
            return;
        }
        iVar9 = param_1 + (param_5 + param_6 * uVar1) * 2;
        uVar11 = param_5;
        while (true) {
            if (*param_8 == '\0') {
                return;
            }
            if (*param_8 == '\n') {
                param_6 = param_6 + param_7;
                param_8 = param_8 + 1;
                goto LAB_8039b4cc;
            }
            if ((int)(uVar1 - 0x30) < (int)uVar11)
                break;

            // Clear the three glyph scratch planes (unrolled 8-wide, 3 rows).
            iVar12 = 3;
            iVar4 = 0;
            iVar6 = 0;
            do {
                uVar10 = 0;
                local_164[iVar6] = 0;
                local_144[iVar6] = 0;
                local_124[iVar6] = 0;
                iVar3 = (iVar4 + 1U & 7) + (iVar4 + 1U >> 3) * 0x18;
                local_164[iVar3] = 0;
                local_144[iVar3] = 0;
                local_124[iVar3] = 0;
                iVar3 = (iVar4 + 2U & 7) + (iVar4 + 2U >> 3) * 0x18;
                local_164[iVar3] = 0;
                local_144[iVar3] = 0;
                local_124[iVar3] = 0;
                iVar3 = (iVar4 + 3U & 7) + (iVar4 + 3U >> 3) * 0x18;
                local_164[iVar3] = 0;
                local_144[iVar3] = 0;
                local_124[iVar3] = 0;
                iVar8 = (iVar4 + 4U & 7) + (iVar4 + 4U >> 3) * 0x18;
                local_164[iVar8] = 0;
                local_144[iVar8] = 0;
                iVar3 = (iVar4 + 5U & 7) + (iVar4 + 5U >> 3) * 0x18;
                local_124[iVar8] = 0;
                local_164[iVar3] = 0;
                local_144[iVar3] = 0;
                local_124[iVar3] = 0;
                iVar8 = (iVar4 + 6U & 7) + (iVar4 + 6U >> 3) * 0x18;
                local_164[iVar8] = 0;
                local_144[iVar8] = 0;
                iVar3 = (iVar4 + 7U & 7) + (iVar4 + 7U >> 3) * 0x18;
                local_124[iVar8] = 0;
                local_164[iVar3] = 0;
                iVar6 = iVar6 + 0x18;
                iVar4 = iVar4 + 8;
                local_144[iVar3] = 0;
                local_124[iVar3] = 0;
                iVar12 = iVar12 + -1;
            } while (iVar12 != 0);

            param_8 = (char *)os_font_get_glyph(param_8, (int)local_164, 0, 6, &local_44);

            iVar4 = 0;
            do {
                iVar3 = 0xc;
                iVar12 = (uVar10 & 7) + (uVar10 >> 3) * 0x18;
                iVar6 = iVar4 << 1;
                uVar7 = 0;
                uVar5 = uVar11;
                do {
                    uVar2 = local_164[iVar12 + (uVar7 & 0xfffffff8)] >> (7 - (uVar7 & 7)) * 4 & 0xf;
                    if (uVar2 != 0) {
                        *(char *)(iVar9 + iVar6) =
                            (char)(((uint)*param_4 * uVar2 * 0xef) / 0xef1) + '\x10';
                        if ((uVar5 & 1) == 0) {
                            *(byte *)(iVar9 + iVar6 + -1) = param_4[2];
                            *(byte *)(iVar9 + iVar6 + 1) = param_4[1];
                        } else {
                            *(byte *)(iVar9 + iVar6 + -1) = param_4[1];
                            *(byte *)(iVar9 + iVar6 + 1) = param_4[2];
                        }
                    }
                    uVar2 = local_164[iVar12 + (uVar7 + 1 & 0xfffffff8)] >> (7 - (uVar7 + 1 & 7)) * 4
                            & 0xf;
                    iVar8 = iVar6 + 2;
                    if (uVar2 != 0) {
                        *(char *)(iVar9 + iVar8) =
                            (char)(((uint)*param_4 * uVar2 * 0xef) / 0xef1) + '\x10';
                        if ((uVar5 + 1 & 1) == 0) {
                            *(byte *)(iVar9 + iVar8 + -1) = param_4[2];
                            *(byte *)(iVar9 + iVar8 + 1) = param_4[1];
                        } else {
                            *(byte *)(iVar9 + iVar8 + -1) = param_4[1];
                            *(byte *)(iVar9 + iVar8 + 1) = param_4[2];
                        }
                    }
                    iVar6 = iVar6 + 4;
                    uVar5 = uVar5 + 2;
                    uVar7 = uVar7 + 2;
                    iVar3 = iVar3 + -1;
                } while (iVar3 != 0);
                uVar10 = uVar10 + 1;
                iVar4 = iVar4 + uVar1;
            } while (uVar10 < 0x18);

            iVar9 = iVar9 + local_44 * 2;
            uVar11 = uVar11 + local_44;
        }
        param_6 = param_6 + param_7;
    } while (true);
}
