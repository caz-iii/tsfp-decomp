// AX voice initialization + per-parameter public setter API.
//
// This is the write side of the AX voice-parameter-block (VPB) system: the
// pool/VPB reset path (ax_voice_pool_init) that builds the 64-voice pool at
// DAT_80604400 (0x22c-byte / 0x8b-int stride) each wired to its VPB slice in
// DAT_805fb700 (0xf4-byte / 0x3d-int stride), its ARAM staging block at
// DAT_805ff400 (0x40-byte stride) and ADPCM coefficient block at DAT_80600400
// (0x100-byte stride); and the AXSetVoice* setters that write voice fields and
// raise the matching dirty bit in the group mask at voice+0x1c. The dirty bits
// here are the exact bits consumed by ax_voice_sync_to_dsp (audio/ax_voice_sync.cpp)
// when the block is flushed to the DSP each frame.
//
// Address: 0x803ae814..0x803aed0c | Module: audio/ax_voice_setup.cpp | Date: 2026-09-15

#include "common.h"

// ---- OS / cache helpers ---------------------------------------------------
extern void os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);
extern void os_dc_flush_range(u32 addr, u32 len);

// ---- named AX helpers (shared headers) ------------------------------------
extern void ax_voice_freelist_push(void* voice);   // 0x803ac248

// ---- runtime float helpers / constants ------------------------------------
extern unsigned int rt_double_to_uint(double v);
extern float FLOAT_8061c020;                        // sample-rate fixed-point scale

// ---- VPB / pool base blocks -----------------------------------------------
extern u32   DAT_805fb700;     // VPB image base (0x3d ints / 0xf4 bytes per voice)
extern u32   DAT_805ff400;     // per-voice ARAM staging (0x10 ints / 0x40 bytes)
extern u8    DAT_80600400;     // per-voice ADPCM coefficient block (0x100 bytes)
extern u32   DAT_80604400;     // AX voice-struct pool (0x8b ints / 0x22c bytes)

// ---- per-frame cost budget ------------------------------------------------
extern int   DAT_800000f8;     // OS bus clock / timing constant
extern int   DAT_806133c0;     // DSP cycle-cost budget
extern uint  DAT_806133c4;     // accumulated cost this frame

// Reset one AX voice's mutable VPB-source fields to their idle defaults.
// voice points at the AX voice struct.
void ax_voice_reset_fields(int voice)
{
    *(u16*)(voice + 0x146) = 0;
    *(u16*)(voice + 0x16e) = 0;
    *(u32*)(voice + 0x1c) = 0x2000a4;   // default dirty group mask
    *(u32*)(voice + 0x28) = 0;
    *(u32*)(voice + 0x24) = 0;
    *(int*)(voice + 0x30) = voice + 0x34;
    *(u16*)(voice + 0x184) = 0;
    *(u16*)(voice + 0x182) = 0;
    *(u16*)(voice + 0x180) = 0;
    *(u16*)(voice + 0x17e) = 0;
    *(u16*)(voice + 0x17c) = 0;
    *(u16*)(voice + 0x1f2) = 0;
}

// Build/reset the 64-voice AX voice pool. Zeroes the VPB image (DAT_805fb700),
// the ARAM staging block (DAT_805ff400) and the ADPCM coefficient block
// (DAT_80604400), then for each of the 64 voices wires its VPB / ARAM / ADPCM
// pointers, resets its default fields, links VPB slices head-to-tail (last
// voice terminates the chain), and pushes the voice onto the free list. Also
// seeds the per-frame DSP cost budget DAT_806133c0. NON-MATCHING: the raw
// pointer arithmetic over the three parallel blocks is preserved verbatim,
// with the high/low 16-bit address halves stored as the DSP expects.
void ax_voice_pool_init(void)
{
    u16   hiVpb;
    u16   hiAram;
    u16   hiAdpcm;
    u32*  vpbslice;
    uint  i;
    u8*   adpcm;
    u32*  aram;
    u32*  voice;
    int   n;

    vpbslice = (u32*)&DAT_805fb700;
    DAT_806133c0 = DAT_800000f8 / 400;
    DAT_806133c4 = 0;

    // Zero the VPB image (0x3d00 bytes).
    n = 0x1e8;
    voice = vpbslice;
    do {
        voice[0] = 0; voice[1] = 0; voice[2] = 0; voice[3] = 0;
        voice[4] = 0; voice[5] = 0; voice[6] = 0; voice[7] = 0;
        voice = voice + 8;
        n = n + -1;
    } while (n != 0);

    // Zero the ARAM staging block (0x1000 bytes).
    n = 0x80;
    voice = &DAT_805ff400;
    do {
        voice[0] = 0; voice[1] = 0; voice[2] = 0; voice[3] = 0;
        voice[4] = 0; voice[5] = 0; voice[6] = 0; voice[7] = 0;
        voice = voice + 8;
        n = n + -1;
    } while (n != 0);

    // Zero the voice-struct pool (0x8b8c bytes).
    n = 0x458;
    voice = &DAT_80604400;
    do {
        voice[0] = 0; voice[1] = 0; voice[2] = 0; voice[3] = 0;
        voice[4] = 0; voice[5] = 0; voice[6] = 0; voice[7] = 0;
        voice = voice + 8;
        n = n + -1;
    } while (n != 0);

    aram = &DAT_805ff400;
    adpcm = &DAT_80600400;
    i = 0;
    voice = &DAT_80604400;
    do {
        voice[6] = i;
        voice[0xc] = (u32)(voice + 0xd);
        voice[0x4d] = (u32)aram;
        *(u16*)((int)voice + 0x146) = 0;
        *(u16*)((int)voice + 0x16e) = 0;
        voice[7] = 0x2000a4;
        voice[10] = 0;
        voice[9] = 0;
        voice[0xc] = (u32)(voice + 0xd);
        *(u16*)(voice + 0x61) = 0;
        *(u16*)((int)voice + 0x182) = 0;
        *(u16*)(voice + 0x60) = 0;
        *(u16*)((int)voice + 0x17e) = 0;
        *(u16*)(voice + 0x5f) = 0;
        *(u16*)((int)voice + 0x1f2) = 0;
        if (i == 0x3f) {
            *(u16*)((int)vpbslice + 2) = 0;
            *(u16*)vpbslice = 0;
            *(u16*)((int)voice + 0x13a) = 0;
            *(u16*)(voice + 0x4e) = 0;
        } else {
            voice[0x4e] = (u32)(vpbslice + 0x3d);
            *vpbslice = (u32)(vpbslice + 0x3d);
        }
        hiVpb = (u16)((uint)vpbslice >> 0x10);
        *(u16*)(voice + 0x4f) = hiVpb;
        hiAram = (u16)((uint)aram >> 0x10);
        hiAdpcm = (u16)((uint)adpcm >> 0x10);
        *(short*)((int)voice + 0x13e) = (short)(u32)vpbslice;
        *(u16*)(vpbslice + 1) = hiVpb;
        *(short*)((int)vpbslice + 6) = (short)(u32)vpbslice;
        *(u16*)(voice + 0x5c) = hiAram;
        *(short*)((int)voice + 0x172) = (short)(u32)aram;
        *(u16*)(vpbslice + 0xe) = hiAram;
        *(short*)((int)vpbslice + 0x3a) = (short)(u32)aram;
        *(u16*)((int)voice + 0x186) = hiAdpcm;
        *(short*)(voice + 0x62) = (short)(u32)adpcm;
        *(u16*)((int)vpbslice + 0x4e) = hiAdpcm;
        *(short*)(vpbslice + 0x14) = (short)(u32)adpcm;
        voice[3] = 1;
        ax_voice_freelist_push(voice);
        i = i + 1;
        vpbslice = vpbslice + 0x3d;
        aram = aram + 0x10;
        adpcm = adpcm + 0x100;
        voice = voice + 0x8b;
    } while (i < 0x40);

    os_dc_flush_range(0x805fb700, 0x3d00);
}

// Empty voice hook.
void ax_voice_nop(void)
{
}

// AXSetVoiceState: set the running state (voice+0x146) and raise dirty bit 4.
// A zero state also latches the stop request (voice+0x20 = 1).
void ax_voice_set_state(int voice, short state)
{
    os_disable_interrupts();
    *(short*)(voice + 0x146) = state;
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 4;
    if (state == 0) {
        *(u32*)(voice + 0x20) = 1;
    }
    os_get_interrupts_enabled();
}

// AXSetVoiceType: set the voice type (voice+0x148) and raise dirty bit 8.
void ax_voice_set_type(int voice, u16 type)
{
    os_disable_interrupts();
    *(u16*)(voice + 0x148) = type;
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 8;
    os_get_interrupts_enabled();
}

// AXSetVoiceAddr: set the sample address block (4 words at voice+0x1a6) and,
// for ADPCM formats (fmt 10 / 0x19 in the second short), pre-clear the ADPCM
// running state at voice+0x1b6.. with the format's initial predictor word.
// Clears the sub-field dirty bits (mask 0x1e000) then raises 0x21000.
void ax_voice_set_addr(int voice, u32* addr)
{
    ushort fmt;

    os_disable_interrupts();
    *(u32*)(voice + 0x1a6) = addr[0];
    *(u32*)(voice + 0x1aa) = addr[1];
    *(u32*)(voice + 0x1ae) = addr[2];
    *(u32*)(voice + 0x1b2) = addr[3];
    fmt = *(ushort*)((int)addr + 2);
    if (fmt == 10) {
        *(u32*)(voice + 0x1b6) = 0;
        *(u32*)(voice + 0x1ba) = 0;
        *(u32*)(voice + 0x1be) = 0;
        *(u32*)(voice + 0x1c2) = 0;
        *(u32*)(voice + 0x1c6) = 0;
        *(u32*)(voice + 0x1ca) = 0;
        *(u32*)(voice + 0x1ce) = 0;
        *(u32*)(voice + 0x1d2) = 0;
        *(u32*)(voice + 0x1d6) = 0x8000000;
        *(u32*)(voice + 0x1da) = 0;
    } else if ((9 < fmt) && (fmt == 0x19)) {
        *(u32*)(voice + 0x1b6) = 0;
        *(u32*)(voice + 0x1ba) = 0;
        *(u32*)(voice + 0x1be) = 0;
        *(u32*)(voice + 0x1c2) = 0;
        *(u32*)(voice + 0x1c6) = 0;
        *(u32*)(voice + 0x1ca) = 0;
        *(u32*)(voice + 0x1ce) = 0;
        *(u32*)(voice + 0x1d2) = 0;
        *(u32*)(voice + 0x1d6) = 0x1000000;
        *(u32*)(voice + 0x1da) = 0;
    }
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) & 0xfffe1fff;
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x21000;
    os_get_interrupts_enabled();
}

// AXSetVoiceLoop / current-address short setter (voice+0x1a6), dirty bit 0x2000.
void ax_voice_set_current_addr_hi(int voice, u16 val)
{
    os_disable_interrupts();
    *(u16*)(voice + 0x1a6) = val;
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x2000;
    os_get_interrupts_enabled();
}

// Set the loop start address (voice+0x1aa), dirty bit 0x4000.
void ax_voice_set_loop_addr(int voice, u32 val)
{
    os_disable_interrupts();
    *(u32*)(voice + 0x1aa) = val;
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x4000;
    os_get_interrupts_enabled();
}

// Set the loop end address (voice+0x1ae), dirty bit 0x8000.
void ax_voice_set_end_addr(int voice, u32 val)
{
    os_disable_interrupts();
    *(u32*)(voice + 0x1ae) = val;
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x8000;
    os_get_interrupts_enabled();
}

// AXSetVoiceAdpcm: copy the 10-word ADPCM coefficient/state block
// (voice+0x1b6..) and raise dirty bit 0x20000.
void ax_voice_set_adpcm(int voice, u32* coefs)
{
    os_disable_interrupts();
    *(u32*)(voice + 0x1b6) = coefs[0];
    *(u32*)(voice + 0x1ba) = coefs[1];
    *(u32*)(voice + 0x1be) = coefs[2];
    *(u32*)(voice + 0x1c2) = coefs[3];
    *(u32*)(voice + 0x1c6) = coefs[4];
    *(u32*)(voice + 0x1ca) = coefs[5];
    *(u32*)(voice + 0x1ce) = coefs[6];
    *(u32*)(voice + 0x1d2) = coefs[7];
    *(u32*)(voice + 0x1d6) = coefs[8];
    *(u32*)(voice + 0x1da) = coefs[9];
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x20000;
    os_get_interrupts_enabled();
}

// AXSetVoiceSrc-adjacent setter: copy the 7-short sample-rate/interp state block
// (voice+0x1de..+0x1ea), clear the volume dirty bit 0x80000, then raise 0x40000.
void ax_voice_set_src(int voice, u16* src)
{
    os_disable_interrupts();
    *(u16*)(voice + 0x1de) = src[0];
    *(u16*)(voice + 0x1e0) = src[1];
    *(u16*)(voice + 0x1e2) = src[2];
    *(u16*)(voice + 0x1e4) = src[3];
    *(u16*)(voice + 0x1e6) = src[4];
    *(u16*)(voice + 0x1e8) = src[5];
    *(u16*)(voice + 0x1ea) = src[6];
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) & 0xfff7ffff;
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x40000;
    os_get_interrupts_enabled();
}

// AXSetVoiceSrcRatio: set the sample-rate ratio from a float multiplier.
// ratio is quantized against FLOAT_8061c020 (fixed-point 1.0 scale) and clamped
// to 0x40000 (max 4x), stored as a 16.16 value across voice+0x1de/+0x1e0, and
// raises the src-ratio dirty bit 0x80000. NON-MATCHING: the double->uint
// conversion via rt_double_to_uint is preserved verbatim.
void ax_voice_set_src_ratio(double ratio, int voice)
{
    uint fixed;
    uint check;
    double scaled;

    os_disable_interrupts();
    scaled = (double)(float)((double)FLOAT_8061c020 * ratio);
    fixed = rt_double_to_uint(scaled);
    check = rt_double_to_uint(scaled);
    if (0x40000 < check) {
        fixed = 0x40000;
    }
    *(short*)(voice + 0x1de) = (short)(fixed >> 0x10);
    *(short*)(voice + 0x1e0) = (short)fixed;
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x80000;
    os_get_interrupts_enabled();
}

// AXSetVoiceLpf: set the 3-short low-pass-filter coefficient block
// (voice+0x1ec..+0x1f0) and raise dirty bit 0x100000.
void ax_voice_set_lpf(int voice, u16* coefs)
{
    os_disable_interrupts();
    *(u16*)(voice + 0x1ec) = coefs[0];
    *(u16*)(voice + 0x1ee) = coefs[1];
    *(u16*)(voice + 0x1f0) = coefs[2];
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x100000;
    os_get_interrupts_enabled();
}
