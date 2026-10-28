// AX voice parameter-block (VPB) -> DSP sync layer.
//
// The AX synth keeps a DSP-facing parameter block per voice at DAT_805fb700
// (32 voices, 0xf4-byte / 0x7a-short stride, mirrored to the DSP over the
// command list). Each per-frame pass walks the priority-sorted voice list,
// accumulates an estimated DSP cycle cost, and either flushes each voice's
// dirty VPB fields to the shared block (ax_voice_sync_to_dsp) or, once the
// cost budget DAT_806133c0 is exceeded, drops the voice. A parallel AUX-send
// accumulator (DAT_80613398..DAT_806133b8) sums the nine send levels across
// all voices and steps them toward the DSP each frame (ax_aux_send_step).
//
// Address: 0x803ad930..0x803ae814 | Module: audio/ax_voice_sync.cpp | Date: 2026-09-15

#include "common.h"

// ---- OS / cache helpers ---------------------------------------------------
extern void os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);
extern void os_dc_flush_range(u32 addr, u32 len);
extern void os_dc_invalidate_range(u32 addr, u32 len);

// ---- named AX helpers (shared headers) ------------------------------------
extern int   ax_cmd_list_get_size(void);       // 0x803aca4c
extern void* ax_voice_by_index(int idx);       // 0x803ac054
extern void  ax_voice_stack_push(int voice);   // 0x803ac268

// ---- user frame callback / studio state -----------------------------------
extern int   DAT_80613378;   // AX user per-frame callback

// ---- AUX-send level accumulators (one per DSP AUX bus, 9 total) -----------
extern int   DAT_80613398;
extern int   DAT_8061339c;
extern int   DAT_806133a0;
extern int   DAT_806133a4;
extern int   DAT_806133a8;
extern int   DAT_806133ac;
extern int   DAT_806133b0;
extern int   DAT_806133b4;
extern int   DAT_806133b8;

// ---- per-frame sync counters / budget -------------------------------------
extern int   DAT_806133c0;   // DSP cycle-cost budget
extern uint  DAT_806133c4;   // accumulated cost this frame
extern int   DAT_806133c8;   // count of voices synced this frame

// ---- AUX-send DSP staging block (flushed as 0x36 bytes) -------------------
extern int   DAT_805fb6c0;   // AUX-send send-0 value (int)
extern short DAT_805fb6c4;   // AUX-send send-0 step (negated)
extern int   DAT_805fb6c6;
extern short DAT_805fb6ca;
extern int   DAT_805fb6cc;
extern short DAT_805fb6d0;
extern int   DAT_805fb6d2;
extern short DAT_805fb6d6;
extern int   DAT_805fb6d8;
extern short DAT_805fb6dc;
extern int   DAT_805fb6de;
extern short DAT_805fb6e2;
extern int   DAT_805fb6e4;
extern short DAT_805fb6e8;
extern int   DAT_805fb6ea;
extern short DAT_805fb6ee;
extern int   DAT_805fb6f0;
extern short DAT_805fb6f4;

// ---- per-voice DSP parameter block (VPB) shared block ---------------------
// Base of the 32-voice DSP parameter table; 0xf4 bytes / 0x7a shorts / 0x3d
// ints per voice. Referenced by byte/short/int offset throughout.
extern u8    DAT_805fb700;
extern u8    DAT_805fb704;
extern u16   DAT_805fb708;
extern u16   DAT_805fb70c;
extern u16   DAT_805fb710;
extern u16   DAT_805fb714;
extern u16   DAT_805fb718;
extern u16   DAT_805fb71c;
extern u16   DAT_805fb720;
extern u16   DAT_805fb724;
extern u16   DAT_805fb728;
extern u16   DAT_805fb72c;
extern u16   DAT_805fb730;
extern u16   DAT_805fb734;
extern u16   DAT_805fb738;
extern u16   DAT_805fb73a;
extern u16   DAT_805fb73c;
extern u8    DAT_805fb740;
extern u8    DAT_805fb742;
extern u8    DAT_805fb744;
extern u8    DAT_805fb746;
extern u8    DAT_805fb748;
extern u8    DAT_805fb74a;
extern u8    DAT_805fb74c;
extern u8    DAT_805fb750;
extern u8    DAT_805fb752;
extern u8    DAT_805fb754;
extern u8    DAT_805fb756;
extern u8    DAT_805fb758;
extern u8    DAT_805fb75a;
extern u8    DAT_805fb75c;
extern u8    DAT_805fb75e;
extern u8    DAT_805fb760;
extern u8    DAT_805fb762;
extern u8    DAT_805fb764;
extern u8    DAT_805fb766;
extern u8    DAT_805fb768;
extern u8    DAT_805fb76a;
extern u8    DAT_805fb76c;
extern u8    DAT_805fb76e;
extern u8    DAT_805fb770;
extern u8    DAT_805fb772;
extern u8    DAT_805fb774;
extern u8    DAT_805fb776;
extern u8    DAT_805fb778;
extern u8    DAT_805fb77a;
extern u8    DAT_805fb77c;
extern u8    DAT_805fb77e;
extern u8    DAT_805fb780;
extern u8    DAT_805fb782;
extern u8    DAT_805fb784;
extern u8    DAT_805fb786;
extern u8    DAT_805fb788;
extern u8    DAT_805fb78a;
extern u8    DAT_805fb78c;
extern u8    DAT_805fb78e;
extern u8    DAT_805fb790;
extern u8    DAT_805fb792;
extern u8    DAT_805fb794;
extern u8    DAT_805fb796;
extern u8    DAT_805fb798;
extern u8    DAT_805fb79a;
extern u8    DAT_805fb79c;
extern u8    DAT_805fb79e;
extern u8    DAT_805fb7a0;
extern u8    DAT_805fb7a2;
extern u8    DAT_805fb7a4;
extern u8    DAT_805fb7a6;
extern u8    DAT_805fb7a8;
extern u8    DAT_805fb7aa;
extern u8    DAT_805fb7ac;
extern u8    DAT_805fb7ae;
extern u8    DAT_805fb7b0;
extern u8    DAT_805fb7b2;
extern u8    DAT_805fb7b4;
extern u8    DAT_805fb7b6;
extern u8    DAT_805fb7b8;
extern u8    DAT_805fb7ba;
extern u8    DAT_805fb7bc;
extern u8    DAT_805fb7be;
extern u8    DAT_805fb7c0;
extern u8    DAT_805fb7c4;
extern u8    DAT_805fb7c8;
extern u8    DAT_805fb7cc;

// ADPCM / large VPB scratch staged from the studio parameter blocks.
extern u8    DAT_80600400;   // per-voice 0x100-byte ADPCM coefficient block

// ---- DSP cost lookup tables (audio ROM data) ------------------------------
extern int   DAT_8049ab00;   // per-src-type cost table (stride 4)
extern int   DAT_8049ab14;   // low-nibble mix cost table (stride 4)
extern int   DAT_8049ab54;   // wide mix cost table (stride 4, indexed *0x7c)

// Set the AX user per-frame callback under an interrupt lock; returns prior.
int ax_set_user_frame_callback(int callback)
{
    int prev = DAT_80613378;
    os_disable_interrupts();
    DAT_80613378 = callback;
    os_get_interrupts_enabled();
    return prev;
}

// Address of the AUX-send DSP staging block.
int* ax_aux_send_params(void)
{
    return &DAT_805fb6c0;
}

// Step each of the nine AUX-send accumulators toward the DSP: quantize by
// 0xa0 (clamped to +/-0x14 per frame), stage the current value + negated step
// into the DSP block, then flush the 0x36-byte staging block.
void ax_aux_send_step(void)
{
    int step;

    step = DAT_80613398 / 0xa0 + (DAT_80613398 >> 0x1f);
    step = step - (step >> 0x1f);
    if (step == 0) {
        DAT_80613398 = 0;
        DAT_805fb6c0 = 0;
        DAT_805fb6c4 = 0;
    } else {
        if (0x14 < step) step = 0x14;
        if (step < -0x14) step = -0x14;
        DAT_805fb6c0 = DAT_80613398;
        DAT_805fb6c4 = -(short)step;
        DAT_80613398 = DAT_80613398 + step * -0xa0;
    }
    step = DAT_8061339c / 0xa0 + (DAT_8061339c >> 0x1f);
    step = step - (step >> 0x1f);
    if (step == 0) {
        DAT_8061339c = 0;
        DAT_805fb6c6 = 0;
        DAT_805fb6ca = 0;
    } else {
        if (0x14 < step) step = 0x14;
        if (step < -0x14) step = -0x14;
        DAT_805fb6c6 = DAT_8061339c;
        DAT_805fb6ca = -(short)step;
        DAT_8061339c = DAT_8061339c + step * -0xa0;
    }
    step = DAT_806133a0 / 0xa0 + (DAT_806133a0 >> 0x1f);
    step = step - (step >> 0x1f);
    if (step == 0) {
        DAT_806133a0 = 0;
        DAT_805fb6cc = 0;
        DAT_805fb6d0 = 0;
    } else {
        if (0x14 < step) step = 0x14;
        if (step < -0x14) step = -0x14;
        DAT_805fb6cc = DAT_806133a0;
        DAT_805fb6d0 = -(short)step;
        DAT_806133a0 = DAT_806133a0 + step * -0xa0;
    }
    step = DAT_806133a4 / 0xa0 + (DAT_806133a4 >> 0x1f);
    step = step - (step >> 0x1f);
    if (step == 0) {
        DAT_806133a4 = 0;
        DAT_805fb6d2 = 0;
        DAT_805fb6d6 = 0;
    } else {
        if (0x14 < step) step = 0x14;
        if (step < -0x14) step = -0x14;
        DAT_805fb6d2 = DAT_806133a4;
        DAT_805fb6d6 = -(short)step;
        DAT_806133a4 = DAT_806133a4 + step * -0xa0;
    }
    step = DAT_806133a8 / 0xa0 + (DAT_806133a8 >> 0x1f);
    step = step - (step >> 0x1f);
    if (step == 0) {
        DAT_806133a8 = 0;
        DAT_805fb6d8 = 0;
        DAT_805fb6dc = 0;
    } else {
        if (0x14 < step) step = 0x14;
        if (step < -0x14) step = -0x14;
        DAT_805fb6d8 = DAT_806133a8;
        DAT_805fb6dc = -(short)step;
        DAT_806133a8 = DAT_806133a8 + step * -0xa0;
    }
    step = DAT_806133ac / 0xa0 + (DAT_806133ac >> 0x1f);
    step = step - (step >> 0x1f);
    if (step == 0) {
        DAT_806133ac = 0;
        DAT_805fb6de = 0;
        DAT_805fb6e2 = 0;
    } else {
        if (0x14 < step) step = 0x14;
        if (step < -0x14) step = -0x14;
        DAT_805fb6de = DAT_806133ac;
        DAT_805fb6e2 = -(short)step;
        DAT_806133ac = DAT_806133ac + step * -0xa0;
    }
    step = DAT_806133b0 / 0xa0 + (DAT_806133b0 >> 0x1f);
    step = step - (step >> 0x1f);
    if (step == 0) {
        DAT_806133b0 = 0;
        DAT_805fb6e4 = 0;
        DAT_805fb6e8 = 0;
    } else {
        if (0x14 < step) step = 0x14;
        if (step < -0x14) step = -0x14;
        DAT_805fb6e4 = DAT_806133b0;
        DAT_805fb6e8 = -(short)step;
        DAT_806133b0 = DAT_806133b0 + step * -0xa0;
    }
    step = DAT_806133b4 / 0xa0 + (DAT_806133b4 >> 0x1f);
    step = step - (step >> 0x1f);
    if (step == 0) {
        DAT_806133b4 = 0;
        DAT_805fb6ea = 0;
        DAT_805fb6ee = 0;
    } else {
        if (0x14 < step) step = 0x14;
        if (step < -0x14) step = -0x14;
        DAT_805fb6ea = DAT_806133b4;
        DAT_805fb6ee = -(short)step;
        DAT_806133b4 = DAT_806133b4 + step * -0xa0;
    }
    step = DAT_806133b8 / 0xa0 + (DAT_806133b8 >> 0x1f);
    step = step - (step >> 0x1f);
    if (step == 0) {
        DAT_806133b8 = 0;
        DAT_805fb6f0 = 0;
        DAT_805fb6f4 = 0;
    } else {
        if (0x14 < step) step = 0x14;
        if (step < -0x14) step = -0x14;
        DAT_805fb6f0 = DAT_806133b8;
        DAT_805fb6f4 = -(short)step;
        DAT_806133b8 = DAT_806133b8 + step * -0xa0;
    }
    os_dc_flush_range(0x805fb6c0, 0x36);
}

// Zero all nine AUX-send accumulators.
void ax_aux_send_reset(void)
{
    DAT_806133b8 = 0;
    DAT_806133b4 = 0;
    DAT_806133b0 = 0;
    DAT_806133ac = 0;
    DAT_806133a8 = 0;
    DAT_806133a4 = 0;
    DAT_806133a0 = 0;
    DAT_8061339c = 0;
    DAT_80613398 = 0;
}

// Empty per-voice sync hook.
void ax_voice_sync_nop(void)
{
}

// Accumulate a voice's nine AUX-send deltas (VPB fields +0x52..+0x62) into the
// global send accumulators. vpb points at the voice's shared parameter block.
void ax_aux_accumulate(int vpb)
{
    DAT_80613398 = DAT_80613398 + *(short*)(vpb + 0x52);
    DAT_806133a4 = DAT_806133a4 + *(short*)(vpb + 0x54);
    DAT_806133b0 = DAT_806133b0 + *(short*)(vpb + 0x56);
    DAT_8061339c = DAT_8061339c + *(short*)(vpb + 0x58);
    DAT_806133a8 = DAT_806133a8 + *(short*)(vpb + 0x5a);
    DAT_806133b4 = DAT_806133b4 + *(short*)(vpb + 0x5c);
    DAT_806133a0 = DAT_806133a0 + *(short*)(vpb + 0x5e);
    DAT_806133ac = DAT_806133ac + *(short*)(vpb + 0x60);
    DAT_806133b8 = DAT_806133b8 + *(short*)(vpb + 0x62);
}

// Number of voices synced to the DSP this frame.
int ax_get_synced_voice_count(void)
{
    return DAT_806133c8;
}

// Flush one voice's dirty VPB fields into the shared DSP parameter block.
// voice points at the AX voice struct. Field +0x18 selects the DSP voice
// index, +0x1c is a bitmask of which parameter groups changed (bit31 => full
// resync). All addresses in the shared block are computed as base + idx*stride
// (0xf4 bytes / 0x7a shorts / 0x3d ints per voice); NON-MATCHING: the raw
// pointer arithmetic on the DAT_805fb700 block is preserved verbatim from the
// decompiler with byte/short/int reinterpret casts.
void ax_voice_sync_to_dsp(int voice)
{
    u32* dst;
    u32  tmp;
    u32* src;
    uint mask;
    int  idx;
    int  off;   // idx * 0xf4 (byte stride)
    u32* end;
    uint n;
    uint m;

    DAT_806133c8 = DAT_806133c8 + 1;
    idx = *(int*)(voice + 0x18);
    mask = *(uint*)(voice + 0x1c);
    off = idx * 0xf4;
    if (mask == 0) {
        *(u16*)(voice + 0x146) = *(u16*)((int)&DAT_805fb70c + off + 2);
        *(u16*)(voice + 0x19c) = *(u16*)(&DAT_805fb764 + off);
        *(u16*)(voice + 0x1b2) = *(u16*)(&DAT_805fb77a + off);
        *(u16*)(voice + 0x1b4) = *(u16*)(&DAT_805fb77c + off);
        return;
    }
    if ((mask & 0x80000000) != 0) {
        // Full resync: copy the entire VPB image (words) then the ADPCM block.
        *(u32*)(&DAT_805fb700 + idx * 0x7a) = *(u32*)(voice + 0x138);
        *(u32*)(&DAT_805fb704 + idx * 0x7a) = *(u32*)(voice + 0x13c);
        (&DAT_805fb708)[idx * 0x3d] = *(u32*)(voice + 0x140);
        (&DAT_805fb70c)[idx * 0x3d] = *(u32*)(voice + 0x144);
        (&DAT_805fb710)[idx * 0x3d] = *(u32*)(voice + 0x148);
        (&DAT_805fb714)[idx * 0x3d] = *(u32*)(voice + 0x14c);
        (&DAT_805fb718)[idx * 0x3d] = *(u32*)(voice + 0x150);
        (&DAT_805fb71c)[idx * 0x3d] = *(u32*)(voice + 0x154);
        (&DAT_805fb720)[idx * 0x3d] = *(u32*)(voice + 0x158);
        (&DAT_805fb724)[idx * 0x3d] = *(u32*)(voice + 0x15c);
        (&DAT_805fb728)[idx * 0x3d] = *(u32*)(voice + 0x160);
        (&DAT_805fb72c)[idx * 0x3d] = *(u32*)(voice + 0x164);
        (&DAT_805fb730)[idx * 0x3d] = *(u32*)(voice + 0x168);
        (&DAT_805fb734)[idx * 0x3d] = *(u32*)(voice + 0x16c);
        *(u32*)(&DAT_805fb738 + idx * 0x7a) = *(u32*)(voice + 0x170);
        (&DAT_805fb73c)[idx * 0x3d] = *(u32*)(voice + 0x174);
        *(u32*)(&DAT_805fb740 + off) = *(u32*)(voice + 0x178);
        *(u32*)(&DAT_805fb744 + off) = *(u32*)(voice + 0x17c);
        *(u32*)(&DAT_805fb748 + off) = *(u32*)(voice + 0x180);
        *(u32*)(&DAT_805fb74c + off) = *(u32*)(voice + 0x184);
        *(u32*)(&DAT_805fb750 + idx * 0x7a) = *(u32*)(voice + 0x188);
        *(u32*)(&DAT_805fb754 + off) = *(u32*)(voice + 0x18c);
        *(u32*)(&DAT_805fb758 + off) = *(u32*)(voice + 400);
        *(u32*)(&DAT_805fb75c + off) = *(u32*)(voice + 0x194);
        *(u32*)(&DAT_805fb760 + off) = *(u32*)(voice + 0x198);
        *(u32*)(&DAT_805fb764 + off) = *(u32*)(voice + 0x19c);
        *(u32*)(&DAT_805fb768 + off) = *(u32*)(voice + 0x1a0);
        *(u32*)(&DAT_805fb76c + off) = *(u32*)(voice + 0x1a4);
        *(u32*)(&DAT_805fb770 + off) = *(u32*)(voice + 0x1a8);
        *(u32*)(&DAT_805fb774 + off) = *(u32*)(voice + 0x1ac);
        *(u32*)(&DAT_805fb778 + off) = *(u32*)(voice + 0x1b0);
        *(u32*)(&DAT_805fb77c + off) = *(u32*)(voice + 0x1b4);
        *(u32*)(&DAT_805fb780 + off) = *(u32*)(voice + 0x1b8);
        *(u32*)(&DAT_805fb784 + off) = *(u32*)(voice + 0x1bc);
        *(u32*)(&DAT_805fb788 + off) = *(u32*)(voice + 0x1c0);
        *(u32*)(&DAT_805fb78c + off) = *(u32*)(voice + 0x1c4);
        *(u32*)(&DAT_805fb790 + off) = *(u32*)(voice + 0x1c8);
        *(u32*)(&DAT_805fb794 + off) = *(u32*)(voice + 0x1cc);
        *(u32*)(&DAT_805fb798 + off) = *(u32*)(voice + 0x1d0);
        *(u32*)(&DAT_805fb79c + off) = *(u32*)(voice + 0x1d4);
        *(u32*)(&DAT_805fb7a0 + off) = *(u32*)(voice + 0x1d8);
        *(u32*)(&DAT_805fb7a4 + off) = *(u32*)(voice + 0x1dc);
        *(u32*)(&DAT_805fb7a8 + off) = *(u32*)(voice + 0x1e0);
        *(u32*)(&DAT_805fb7ac + off) = *(u32*)(voice + 0x1e4);
        *(u32*)(&DAT_805fb7b0 + off) = *(u32*)(voice + 0x1e8);
        *(u32*)(&DAT_805fb7b4 + off) = *(u32*)(voice + 0x1ec);
        *(u32*)(&DAT_805fb7b8 + off) = *(u32*)(voice + 0x1f0);
        *(u32*)(&DAT_805fb7bc + off) = *(u32*)(voice + 500);
        *(u32*)(&DAT_805fb7c0 + off) = *(u32*)(voice + 0x1f8);
        *(u32*)(&DAT_805fb7c4 + off) = *(u32*)(voice + 0x1fc);
        *(u32*)(&DAT_805fb7c8 + off) = *(u32*)(voice + 0x200);
        *(u32*)(&DAT_805fb7cc + off) = *(u32*)(voice + 0x204);
        mask = *(uint*)(voice + 0x28);
        if (mask == 0) return;
        dst = (u32*)(voice + 0x34);
        src = (u32*)(&DAT_80600400 + *(int*)(voice + 0x18) * 0x100);
        n = mask >> 3;
        if (n != 0) {
            do {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
                dst[4] = src[4]; dst[5] = src[5]; dst[6] = src[6]; dst[7] = src[7];
                src = src + 8;
                dst = dst + 8;
                n = n - 1;
            } while (n != 0);
            mask = mask & 7;
            if (mask == 0) return;
        }
        do {
            tmp = *src;
            src = src + 1;
            *dst = tmp;
            dst = dst + 1;
            mask = mask - 1;
        } while (mask != 0);
        return;
    }
    // Partial resync: each bit selects a field group to push.
    if ((mask & 1) != 0) {
        *(u16*)(&DAT_805fb708 + idx * 0x3d) = *(u16*)(voice + 0x140);
        *(u16*)((int)&DAT_805fb708 + off + 2) = *(u16*)(voice + 0x142);
    }
    if ((mask & 2) != 0) {
        *(u16*)(&DAT_805fb70c + idx * 0x3d) = *(u16*)(voice + 0x144);
    }
    if ((mask & 4) == 0) {
        *(u16*)(voice + 0x146) = *(u16*)((int)&DAT_805fb70c + off + 2);
    } else {
        *(u16*)((int)&DAT_805fb70c + off + 2) = *(u16*)(voice + 0x146);
    }
    if ((mask & 8) != 0) {
        *(u16*)(&DAT_805fb710 + idx * 0x3d) = *(u16*)(voice + 0x148);
    }
    if ((mask & 0x10) != 0) {
        *(u16*)((int)&DAT_805fb710 + off + 2) = *(u16*)(voice + 0x14a);
        *(u16*)(&DAT_805fb714 + idx * 0x3d) = *(u16*)(voice + 0x14c);
        *(u16*)((int)&DAT_805fb714 + off + 2) = *(u16*)(voice + 0x14e);
        *(u16*)(&DAT_805fb718 + idx * 0x3d) = *(u16*)(voice + 0x150);
        *(u16*)((int)&DAT_805fb718 + off + 2) = *(u16*)(voice + 0x152);
        *(u16*)(&DAT_805fb71c + idx * 0x3d) = *(u16*)(voice + 0x154);
        *(u16*)((int)&DAT_805fb71c + off + 2) = *(u16*)(voice + 0x156);
        *(u16*)(&DAT_805fb720 + idx * 0x3d) = *(u16*)(voice + 0x158);
        *(u16*)((int)&DAT_805fb720 + off + 2) = *(u16*)(voice + 0x15a);
        *(u16*)(&DAT_805fb724 + idx * 0x3d) = *(u16*)(voice + 0x15c);
        *(u16*)((int)&DAT_805fb724 + off + 2) = *(u16*)(voice + 0x15e);
        *(u16*)(&DAT_805fb728 + idx * 0x3d) = *(u16*)(voice + 0x160);
        *(u16*)((int)&DAT_805fb728 + off + 2) = *(u16*)(voice + 0x162);
        *(u16*)(&DAT_805fb72c + idx * 0x3d) = *(u16*)(voice + 0x164);
        *(u16*)((int)&DAT_805fb72c + off + 2) = *(u16*)(voice + 0x166);
        *(u16*)(&DAT_805fb730 + idx * 0x3d) = *(u16*)(voice + 0x168);
        *(u16*)((int)&DAT_805fb730 + off + 2) = *(u16*)(voice + 0x16a);
        *(u16*)(&DAT_805fb734 + idx * 0x3d) = *(u16*)(voice + 0x16c);
    }
    if ((mask & 0x40) == 0) {
        if ((mask & 0x20) != 0) {
            *(u16*)((int)&DAT_805fb734 + off + 2) = *(u16*)(voice + 0x16e);
            (&DAT_805fb738)[idx * 0x7a] = *(u16*)(voice + 0x170);
            (&DAT_805fb73a)[idx * 0x7a] = *(u16*)(voice + 0x172);
            *(u16*)(&DAT_805fb73c + idx * 0x3d) = *(u16*)(voice + 0x174);
            *(u16*)((int)&DAT_805fb73c + off + 2) = *(u16*)(voice + 0x176);
            *(u16*)(&DAT_805fb740 + off) = *(u16*)(voice + 0x178);
            *(u16*)(&DAT_805fb742 + off) = *(u16*)(voice + 0x17a);
            dst = *(u32**)(voice + 0x134);
            dst[0] = 0; dst[1] = 0; dst[2] = 0; dst[3] = 0;
            dst[4] = 0; dst[5] = 0; dst[6] = 0; dst[7] = 0;
            dst[8] = 0; dst[9] = 0; dst[10] = 0; dst[0xb] = 0;
            dst[0xc] = 0; dst[0xd] = 0; dst[0xe] = 0; dst[0xf] = 0;
        }
    } else {
        *(u16*)(&DAT_805fb740 + off) = *(u16*)(voice + 0x178);
        *(u16*)(&DAT_805fb742 + off) = *(u16*)(voice + 0x17a);
    }
    if ((mask & 0x80) != 0) {
        *(u16*)(&DAT_805fb744 + off) = *(u16*)(voice + 0x17c);
        *(u16*)(&DAT_805fb746 + off) = *(u16*)(voice + 0x17e);
        *(u16*)(&DAT_805fb748 + off) = *(u16*)(voice + 0x180);
        *(u16*)(&DAT_805fb74a + off) = *(u16*)(voice + 0x182);
        *(u16*)(&DAT_805fb74c + off) = *(u16*)(voice + 0x184);
        m = *(uint*)(voice + 0x28);
        if (m != 0) {
            dst = (u32*)(voice + 0x34);
            src = (u32*)(&DAT_80600400 + *(int*)(voice + 0x18) * 0x100);
            n = m >> 3;
            if (n != 0) {
                do {
                    src[0] = dst[0]; src[1] = dst[1]; src[2] = dst[2]; src[3] = dst[3];
                    src[4] = dst[4]; src[5] = dst[5]; src[6] = dst[6]; src[7] = dst[7];
                    dst = dst + 8;
                    src = src + 8;
                    n = n - 1;
                } while (n != 0);
                m = m & 7;
                if (m == 0) goto after_adpcm;
            }
            do {
                tmp = *dst;
                dst = dst + 1;
                *src = tmp;
                src = src + 1;
                m = m - 1;
            } while (m != 0);
        }
    }
after_adpcm:
    if ((mask & 0x100) != 0) {
        *(u16*)(&DAT_805fb752 + off) = *(u16*)(voice + 0x18a);
        *(u16*)(&DAT_805fb754 + off) = *(u16*)(voice + 0x18c);
        *(u16*)(&DAT_805fb756 + off) = *(u16*)(voice + 0x18e);
        *(u16*)(&DAT_805fb758 + off) = *(u16*)(voice + 400);
        *(u16*)(&DAT_805fb75a + off) = *(u16*)(voice + 0x192);
        *(u16*)(&DAT_805fb75c + off) = *(u16*)(voice + 0x194);
        *(u16*)(&DAT_805fb75e + off) = *(u16*)(voice + 0x196);
        *(u16*)(&DAT_805fb760 + off) = *(u16*)(voice + 0x198);
        *(u16*)(&DAT_805fb762 + off) = *(u16*)(voice + 0x19a);
    }
    if ((mask & 0x400) == 0) {
        if ((mask & 0x200) != 0) {
            *(u16*)(&DAT_805fb764 + off) = *(u16*)(voice + 0x19c);
            *(u16*)(&DAT_805fb766 + off) = *(u16*)(voice + 0x19e);
        }
    } else {
        *(u16*)(voice + 0x19c) = *(u16*)(&DAT_805fb764 + off);
        *(u16*)(&DAT_805fb766 + off) = *(u16*)(voice + 0x19e);
    }
    if ((mask & 0x800) != 0) {
        *(u16*)(&DAT_805fb768 + off) = *(u16*)(voice + 0x1a0);
        *(u16*)(&DAT_805fb76a + off) = *(u16*)(voice + 0x1a2);
        *(u16*)(&DAT_805fb76c + off) = *(u16*)(voice + 0x1a4);
    }
    if ((mask & 0x1e000) == 0) {
        if ((mask & 0x1000) == 0) {
            *(u16*)(voice + 0x1b2) = *(u16*)(&DAT_805fb77a + off);
            *(u16*)(voice + 0x1b4) = *(u16*)(&DAT_805fb77c + off);
        } else {
            *(u32*)(&DAT_805fb76e + off) = *(u32*)(voice + 0x1a6);
            *(u32*)(&DAT_805fb772 + off) = *(u32*)(voice + 0x1aa);
            *(u32*)(&DAT_805fb776 + off) = *(u32*)(voice + 0x1ae);
            *(u32*)(&DAT_805fb77a + off) = *(u32*)(voice + 0x1b2);
        }
    } else {
        if ((mask & 0x2000) != 0) {
            *(u16*)(&DAT_805fb76e + off) = *(u16*)(voice + 0x1a6);
        }
        if ((mask & 0x4000) != 0) {
            *(u32*)(&DAT_805fb772 + off) = *(u32*)(voice + 0x1aa);
        }
        if ((mask & 0x8000) != 0) {
            *(u32*)(&DAT_805fb776 + off) = *(u32*)(voice + 0x1ae);
        }
        if ((mask & 0x10000) == 0) {
            *(u32*)(voice + 0x1b2) = *(u32*)(&DAT_805fb77a + off);
        } else {
            *(u32*)(&DAT_805fb77a + off) = *(u32*)(voice + 0x1b2);
        }
    }
    if ((mask & 0x20000) != 0) {
        *(u32*)(&DAT_805fb77e + off) = *(u32*)(voice + 0x1b6);
        *(u32*)(&DAT_805fb782 + off) = *(u32*)(voice + 0x1ba);
        *(u32*)(&DAT_805fb786 + off) = *(u32*)(voice + 0x1be);
        *(u32*)(&DAT_805fb78a + off) = *(u32*)(voice + 0x1c2);
        *(u32*)(&DAT_805fb78e + off) = *(u32*)(voice + 0x1c6);
        *(u32*)(&DAT_805fb792 + off) = *(u32*)(voice + 0x1ca);
        *(u32*)(&DAT_805fb796 + off) = *(u32*)(voice + 0x1ce);
        *(u32*)(&DAT_805fb79a + off) = *(u32*)(voice + 0x1d2);
        *(u32*)(&DAT_805fb79e + off) = *(u32*)(voice + 0x1d6);
        *(u32*)(&DAT_805fb7a2 + off) = *(u32*)(voice + 0x1da);
    }
    if ((mask & 0x80000) == 0) {
        if ((mask & 0x40000) != 0) {
            *(u16*)(&DAT_805fb7a6 + off) = *(u16*)(voice + 0x1de);
            *(u16*)(&DAT_805fb7a8 + off) = *(u16*)(voice + 0x1e0);
            *(u16*)(&DAT_805fb7aa + off) = *(u16*)(voice + 0x1e2);
            *(u16*)(&DAT_805fb7ac + off) = *(u16*)(voice + 0x1e4);
            *(u16*)(&DAT_805fb7ae + off) = *(u16*)(voice + 0x1e6);
            *(u16*)(&DAT_805fb7b0 + off) = *(u16*)(voice + 0x1e8);
            *(u16*)(&DAT_805fb7b2 + off) = *(u16*)(voice + 0x1ea);
        }
    } else {
        *(u16*)(&DAT_805fb7a6 + off) = *(u16*)(voice + 0x1de);
        *(u16*)(&DAT_805fb7a8 + off) = *(u16*)(voice + 0x1e0);
    }
    if ((mask & 0x100000) != 0) {
        *(u16*)(&DAT_805fb7b4 + off) = *(u16*)(voice + 0x1ec);
        *(u16*)(&DAT_805fb7b6 + off) = *(u16*)(voice + 0x1ee);
        *(u16*)(&DAT_805fb7b8 + off) = *(u16*)(voice + 0x1f0);
    }
    if ((mask & 0x400000) == 0) {
        if ((mask & 0x200000) != 0) {
            *(u16*)(&DAT_805fb7ba + off) = *(u16*)(voice + 0x1f2);
            *(u16*)(&DAT_805fb7bc + off) = *(u16*)(voice + 500);
            *(u16*)(&DAT_805fb7be + off) = *(u16*)(voice + 0x1f6);
            *(u16*)(&DAT_805fb7c0 + off) = *(u16*)(voice + 0x1f8);
            return;
        }
        return;
    }
    *(u16*)(&DAT_805fb7be + off) = *(u16*)(voice + 0x1f6);
    *(u16*)(&DAT_805fb7c0 + off) = *(u16*)(voice + 0x1f8);
}

// Per-frame voice-sync pass: walk the priority-sorted voice list (priorities
// 0x1f..1, then the free-standing list at priority 0), accumulate an estimated
// DSP cycle cost from per-voice source/mix cost tables, and either sync each
// voice's VPB to the DSP or, once the running cost exceeds the budget
// DAT_806133c0, silence and free the voice. `frame_base_cost` is the fixed
// per-frame cost seeded by the caller. Finally flushes the VPB, ARAM staging,
// and ADPCM coefficient blocks. NON-MATCHING: raw VPB pointer math preserved.
void ax_frame_sync_voices(int frame_base_cost)
{
    ushort mixcfg;
    int    listcost;
    u32*   voice;
    int    prio;
    uint   cost;

    DAT_806133c8 = 0;
    os_dc_invalidate_range(0x805fb700, 0x3d00);
    os_dc_invalidate_range(0x805ff400, 0x1000);
    listcost = ax_cmd_list_get_size();
    prio = 0x1f;
    cost = listcost + frame_base_cost + 0xaa10;
    do {
        voice = (u32*)ax_voice_by_index(prio);
        while (voice != (u32*)0x0) {
            if (voice[8] != 0) {
                ax_aux_accumulate((int)(&DAT_805fb700 + voice[6] * 0x7a));
            }
            if ((*(short*)((int)voice + 0x146) == 1) || (voice[10] != 0)) {
                if (*(short*)(voice + 0x50) != 2) {
                    cost = cost + *(int*)(&DAT_8049ab00 + (uint)*(ushort*)((int)voice + 0x1de) * 4);
                }
                if (*(short*)((int)voice + 0x1f2) != 0) {
                    cost = cost + 0x22b;
                }
                mixcfg = *(ushort*)(voice + 0x51);
                cost = *(int*)(&DAT_8049ab14 + (mixcfg & 0xf) * 4) +
                       *(int*)(&DAT_8049ab54 + (mixcfg >> 2 & 0x7c)) +
                       *(int*)(&DAT_8049ab54 + (mixcfg >> 7 & 0x7c)) + 0x8c + cost;
                if (cost < DAT_806133c0) {
                    ax_voice_sync_to_dsp((int)voice);
                } else {
                    // Over budget: silence this voice's VPB and recycle it.
                    int voff = voice[6] * 0xf4;
                    if (*(short*)((int)&DAT_805fb70c + voff + 2) == 1) {
                        ax_aux_accumulate((int)(&DAT_805fb700 + voice[6] * 0x7a));
                    }
                    *(u16*)(&DAT_805fb74c + voff) = 0;
                    *(u16*)(&DAT_805fb74a + voff) = 0;
                    *(u16*)(&DAT_805fb748 + voff) = 0;
                    *(u16*)(&DAT_805fb746 + voff) = 0;
                    *(u16*)(&DAT_805fb744 + voff) = 0;
                    *(u16*)((int)&DAT_805fb70c + voff + 2) = 0;
                    *(u16*)((int)voice + 0x146) = 0;
                    ax_voice_stack_push((int)voice);
                }
            } else {
                ax_voice_sync_to_dsp((int)voice);
            }
            voice[7] = 0;
            voice[8] = 0;
            voice[10] = 0;
            voice[9] = 0;
            voice[0xc] = (u32)(voice + 0xd);
            voice = (u32*)*voice;
        }
        prio = prio + -1;
    } while (prio != 0);
    DAT_806133c4 = cost;
    for (voice = (u32*)ax_voice_by_index(0); voice != (u32*)0x0; voice = (u32*)*voice) {
        if (voice[8] != 0) {
            ax_aux_accumulate((int)(&DAT_805fb700 + voice[6] * 0x7a));
        }
        voice[8] = 0;
        *(u16*)(&DAT_805fb74c + voice[6] * 0xf4) = 0;
        *(u16*)(&DAT_805fb74a + voice[6] * 0xf4) = 0;
        *(u16*)(&DAT_805fb748 + voice[6] * 0xf4) = 0;
        *(u16*)(&DAT_805fb746 + voice[6] * 0xf4) = 0;
        *(u16*)(&DAT_805fb744 + voice[6] * 0xf4) = 0;
        *(u16*)((int)&DAT_805fb70c + voice[6] * 0xf4 + 2) = 0;
    }
    os_dc_flush_range(0x805fb700, 0x3d00);
    os_dc_flush_range(0x805ff400, 0x1000);
    os_dc_flush_range(0x80600400, 0x4000);
}

// Base of the 32-voice DSP parameter block (VPB) image.
u16* ax_get_vpb_base(void)
{
    return (u16*)&DAT_805fb700;
}
