// THP movie audio: THADPCM decoder.
//
// The GameCube THP container interleaves a JPEG video track (see
// video/thp_jpeg.cpp) with a THADPCM-style audio track. This module decodes one
// audio frame: FUN_803c3890 walks the packed 4-bit nibble stream through a
// tiny bit-reader (FUN_803c3d84 init + FUN_803c3cf4 read), running the standard
// 2-tap ADPCM predictor per sample with a per-block scale/coefficient set, then
// rounds and clamps to signed 16-bit PCM into the output channel buffer(s).
//
// State packed into the caller's 3-int reader (aiStack_44 in the decompiler):
//   [0] byte cursor, [1] nibble/bit position, [2] (byte 8) high nibble of the
//   current control byte, (byte 9) low nibble -> the per-sample coefficient
//   index and scale-shift used by the predictor.
//
// NON-MATCHING: the bit-reader packing, the 64-bit intermediate arithmetic the
// decompiler rendered via __shl2i / __sar2i / CARRY4, and the round-to-nearest
// + 16-bit clamp are transcribed verbatim. Coefficient/history tables live in
// the frame-parameter block param_2 at the raw offsets the decompiler used.
// Batch: batch_1158
// Source module: audio/thp_adpcm.cpp

#include "common.h"

// 64-bit shift runtime helpers (Metrowerks __shl2i / __sar2i). Declared per-file
// to match the generated_decls signature without pulling the whole decl header.
extern ulonglong __shl2i(int hiValue, uint loValue, int shift);
extern ulonglong __sar2i(int hiValue, uint loValue, int shift);

// ---------------------------------------------------------------------------
// thp_adpcm_reader_init - FUN_803c3d84. Seed the nibble reader at byte address
// 'src': record the cursor, set the bit position to 2, and pre-latch the first
// control byte's high (coeff index) and low (scale) nibbles, then advance one
// byte.
// ---------------------------------------------------------------------------
void thp_adpcm_reader_init(int *reader, int src)
{
    *reader = src;
    reader[1] = 2;
    *(byte *)(reader + 2) = *(byte *)(intptr_t)*reader >> 4 & 7;
    *(byte *)((int)reader + 9) = *(byte *)(intptr_t)*reader & 0xf;
    *reader = *reader + 1;
    return;
}

// ---------------------------------------------------------------------------
// thp_adpcm_read_nibble - FUN_803c3cf4. Return the next signed 4-bit sample
// from the nibble stream, re-latching the control byte at each 8-nibble
// boundary and consuming a byte every two nibbles.
// ---------------------------------------------------------------------------
int thp_adpcm_read_nibble(int *reader)
{
    byte bVar1;
    uint uVar2;

    if ((reader[1] & 0xfU) == 0) {
        *(byte *)(reader + 2) = *(byte *)(intptr_t)*reader >> 4 & 7;
        *(byte *)((int)reader + 9) = *(byte *)(intptr_t)*reader & 0xf;
        *reader = *reader + 1;
        reader[1] = reader[1] + 2;
    }
    if ((reader[1] & 1U) == 0) {
        uVar2 = (uint)*(byte *)(intptr_t)*reader << 0x18;
    }
    else {
        bVar1 = *(byte *)(intptr_t)*reader;
        *reader = (int)((byte *)(intptr_t)*reader + 1);
        uVar2 = (uint)bVar1 << 0x1c | (uint)(bVar1 >> 4);
    }
    reader[1] = reader[1] + 1;
    return (int)uVar2 >> 0x1c;
}

// ---------------------------------------------------------------------------
// thp_adpcm_decode_frame - FUN_803c3890. Decode one channel (or two, when
// param_3 != 1) of an audio frame.
//   param_1 : output PCM buffer (short*)
//   param_2 : frame-parameter block (sample count at [1]; per-block scale/
//             coeff nibbles + history at the offsets below)
//   param_3 : channel mode (1 = single interleave stride, else 2)
// Returns the number of samples produced (param_2[1]).
// ---------------------------------------------------------------------------
int thp_adpcm_decode_frame(undefined2 *param_1, int *param_2, int param_3)
{
    uint uVar1;
    int iVar2;
    int iVar3;
    int iVar4;
    uint uVar5;
    uint uVar6;
    int iVar7;
    uint uVar8;
    int iVar9;
    undefined2 *puVar10;
    ulonglong uVar11;
    undefined8 uVar12;
    int aiStack_44[3];
    byte local_3c;
    byte local_3b;

    if ((param_1 == (undefined2 *)0x0) || (param_2 == (int *)0x0)) {
        iVar2 = 0;
    }
    else {
        iVar2 = *param_2;
        if (param_3 == 1) {
            iVar9 = 1;
            puVar10 = param_1 + param_2[1];
        }
        else {
            puVar10 = param_1 + 1;
            iVar9 = 2;
        }
        if (*param_2 == 0) {
            thp_adpcm_reader_init(aiStack_44, (int)(param_2 + 0x14));
            local_3c = *(byte *)(aiStack_44 + 2);
            local_3b = *(byte *)((int)aiStack_44 + 9);
            iVar2 = (int)*(short *)((int)param_2 + 0x4a);
            iVar7 = (int)*(short *)(param_2 + 0x12);
            for (uVar8 = 0; uVar8 < (uint)param_2[1]; uVar8 = uVar8 + 1) {
                iVar3 = thp_adpcm_read_nibble(aiStack_44);
                local_3c = *(byte *)(aiStack_44 + 2);
                local_3b = *(byte *)((int)aiStack_44 + 9);
                uVar1 = (iVar3 << (uint)local_3b) * 0x800;
                uVar6 = (int)*(short *)((int)param_2 + (uint)local_3c * 4 + 10) * (int)(short)iVar2;
                uVar5 = (int)*(short *)(param_2 + local_3c + 2) * (int)(short)iVar7;
                uVar11 = __shl2i(((int)uVar6 >> 0x1f) + ((int)uVar5 >> 0x1f) + (uint)CARRY4(uVar6, uVar5) +
                                 ((int)(uVar1 | (uint)(iVar3 << (uint)local_3b) >> 0x15) >> 0x1f) +
                                 (uint)CARRY4(uVar6 + uVar5, uVar1), uVar6 + uVar5 + uVar1, 5);
                uVar1 = (uint)uVar11 & 0xffff;
                if (uVar1 < 0x8001) {
                    if ((uVar1 == 0x8000) && ((uVar11 & 0x10000) != 0)) {
                        uVar11 = uVar11 + 0x10000;
                    }
                }
                else {
                    uVar11 = uVar11 + 0x10000;
                }
                if (0x80000000 < (uint)(0x7fffffff < (uint)uVar11) + ((uint)(uVar11 >> 0x20) ^ 0x80000000)) {
                    uVar11 = 0x7fffffff;
                }
                if (((uint)(uVar11 >> 0x20) ^ 0x80000000) < ((uint)uVar11 < 0x80000000) + 0x7fffffff) {
                    uVar11 = 0xffffffff80000000;
                }
                uVar12 = __sar2i((int)(uVar11 >> 0x20), (uint)uVar11, 0x10);
                *puVar10 = (short)uVar12;
                *param_1 = (short)uVar12;
                puVar10 = puVar10 + iVar9;
                param_1 = param_1 + iVar9;
                iVar2 = iVar7;
                iVar7 = (int)uVar12;
            }
        }
        else {
            thp_adpcm_reader_init(aiStack_44, (int)(param_2 + 0x14));
            local_3c = *(byte *)(aiStack_44 + 2);
            local_3b = *(byte *)((int)aiStack_44 + 9);
            iVar7 = (int)*(short *)((int)param_2 + 0x4a);
            iVar3 = (int)*(short *)(param_2 + 0x12);
            for (uVar8 = 0; uVar8 < (uint)param_2[1]; uVar8 = uVar8 + 1) {
                iVar4 = thp_adpcm_read_nibble(aiStack_44);
                local_3c = *(byte *)(aiStack_44 + 2);
                local_3b = *(byte *)((int)aiStack_44 + 9);
                uVar1 = (iVar4 << (uint)local_3b) * 0x800;
                uVar6 = (int)*(short *)((int)param_2 + (uint)local_3c * 4 + 10) * (int)(short)iVar7;
                uVar5 = (int)*(short *)(param_2 + local_3c + 2) * (int)(short)iVar3;
                uVar11 = __shl2i(((int)uVar6 >> 0x1f) + ((int)uVar5 >> 0x1f) + (uint)CARRY4(uVar6, uVar5) +
                                 ((int)(uVar1 | (uint)(iVar4 << (uint)local_3b) >> 0x15) >> 0x1f) +
                                 (uint)CARRY4(uVar6 + uVar5, uVar1), uVar6 + uVar5 + uVar1, 5);
                uVar1 = (uint)uVar11 & 0xffff;
                if (uVar1 < 0x8001) {
                    if ((uVar1 == 0x8000) && ((uVar11 & 0x10000) != 0)) {
                        uVar11 = uVar11 + 0x10000;
                    }
                }
                else {
                    uVar11 = uVar11 + 0x10000;
                }
                if (0x80000000 < (uint)(0x7fffffff < (uint)uVar11) + ((uint)(uVar11 >> 0x20) ^ 0x80000000)) {
                    uVar11 = 0x7fffffff;
                }
                if (((uint)(uVar11 >> 0x20) ^ 0x80000000) < ((uint)uVar11 < 0x80000000) + 0x7fffffff) {
                    uVar11 = 0xffffffff80000000;
                }
                uVar12 = __sar2i((int)(uVar11 >> 0x20), (uint)uVar11, 0x10);
                *puVar10 = (short)uVar12;
                puVar10 = puVar10 + iVar9;
                iVar7 = iVar3;
                iVar3 = (int)uVar12;
            }
            thp_adpcm_reader_init(aiStack_44, (int)param_2 + iVar2 + 0x50);
            local_3c = *(byte *)(aiStack_44 + 2);
            local_3b = *(byte *)((int)aiStack_44 + 9);
            iVar2 = (int)*(short *)((int)param_2 + 0x4e);
            iVar7 = (int)*(short *)(param_2 + 0x13);
            for (uVar8 = 0; uVar8 < (uint)param_2[1]; uVar8 = uVar8 + 1) {
                iVar3 = thp_adpcm_read_nibble(aiStack_44);
                local_3c = *(byte *)(aiStack_44 + 2);
                local_3b = *(byte *)((int)aiStack_44 + 9);
                uVar1 = (iVar3 << (uint)local_3b) * 0x800;
                uVar6 = (int)*(short *)((int)param_2 + (uint)local_3c * 4 + 0x2a) * (int)(short)iVar2;
                uVar5 = (int)*(short *)(param_2 + local_3c + 10) * (int)(short)iVar7;
                uVar11 = __shl2i(((int)uVar6 >> 0x1f) + ((int)uVar5 >> 0x1f) + (uint)CARRY4(uVar6, uVar5) +
                                 ((int)(uVar1 | (uint)(iVar3 << (uint)local_3b) >> 0x15) >> 0x1f) +
                                 (uint)CARRY4(uVar6 + uVar5, uVar1), uVar6 + uVar5 + uVar1, 5);
                uVar1 = (uint)uVar11 & 0xffff;
                if (uVar1 < 0x8001) {
                    if ((uVar1 == 0x8000) && ((uVar11 & 0x10000) != 0)) {
                        uVar11 = uVar11 + 0x10000;
                    }
                }
                else {
                    uVar11 = uVar11 + 0x10000;
                }
                if (0x80000000 < (uint)(0x7fffffff < (uint)uVar11) + ((uint)(uVar11 >> 0x20) ^ 0x80000000)) {
                    uVar11 = 0x7fffffff;
                }
                if (((uint)(uVar11 >> 0x20) ^ 0x80000000) < ((uint)uVar11 < 0x80000000) + 0x7fffffff) {
                    uVar11 = 0xffffffff80000000;
                }
                uVar12 = __sar2i((int)(uVar11 >> 0x20), (uint)uVar11, 0x10);
                *param_1 = (short)uVar12;
                param_1 = param_1 + iVar9;
                iVar2 = iVar7;
                iVar7 = (int)uVar12;
            }
        }
        iVar2 = param_2[1];
    }
    return iVar2;
}
