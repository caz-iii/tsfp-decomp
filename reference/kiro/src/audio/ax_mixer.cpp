// AX voice mixer / channel-volume computation.
//
// Sits on top of the AX voice VPB layer: given a voice's pan / span / volume
// / distance-attenuation parameters it computes the per-output-channel mix
// volumes and pushes them into the voice's VPB mixer fields (voice+0x144
// dirty-bit mask, voice+0x14a.. the L/R/Ls/Rs/... channel volume shorts),
// then raises the mix dirty bits 0x212 so ax_voice_sync_to_dsp flushes them.
//
// Channel attenuations are looked up in a dB->linear table DAT_8049ec30 indexed
// by an integer dB offset (clamped to [-0x387, 0x3c); below => 0 silence, at/
// above => 0xff64). The mixer keeps a 64-voice parameter cache at DAT_8060cf00
// (0x60 bytes / 0x18 ints per voice) and a parallel computed-volume table (0x30
// shorts per voice). The output layout depends on the mixer mode DAT_806133e8
// (0 = stereo, 3 = Dolby Pro Logic II / surround, 1/2 = intermediate).
//
// Address: 0x803aef4c..0x803affbc | Module: audio/ax_mixer.cpp | Date: 2026-09-15

#include "common.h"

// ---- OS helpers -----------------------------------------------------------
extern void os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);

// ---- profiling scratch ring (shared with ax_frame.cpp) --------------------
extern int  DAT_806133d0;   // scratch ring base
extern int  DAT_806133d4;   // ring length
extern uint DAT_806133d8;   // ring cursor
extern int  DAT_806133dc;   // ring enabled flag

// ---- mixer state ----------------------------------------------------------
extern int  DAT_806133e0;
extern int  DAT_806133e4;
extern int  DAT_806133e8;   // mixer mode (0 stereo / 3 DPL2 / intermediate)

// ---- mixer parameter + computed-volume tables (64 voices) -----------------
// The mixer param block is a flat int table indexed by idx*0x18; the computed
// per-channel volume block is a flat u16 table indexed by idx*0x30. Each named
// offset is a distinct field within one voice's slot (decompiler-style flat
// symbols), so all are declared per-file.
extern int  DAT_8060cf00;   // [+0x00] voice ptr
extern int  DAT_8060cf04;   // [+0x04] control flags
extern int  DAT_8060cf08;   // [+0x08] base dB
extern int  DAT_8060cf0c;   // [+0x0c] param p4
extern int  DAT_8060cf10;   // [+0x10] param p5
extern int  DAT_8060cf14;   // [+0x14] param p6
extern int  DAT_8060cf18;   // [+0x18] param p7
extern int  DAT_8060cf1c;   // [+0x1c] param p8
extern int  DAT_8060cf20;   // [+0x20] pan table entry
extern int  DAT_8060cf24;   // [+0x24]
extern int  DAT_8060cf28;   // [+0x28]
extern int  DAT_8060cf2c;   // [+0x2c]
extern int  DAT_8060cf30;   // [+0x30]
extern int  DAT_8060cf34;   // [+0x34]
extern u16  DAT_8060cf38;   // computed-volume block base (0x30 shorts/voice)
extern u16  DAT_8060cf3c;
extern u16  DAT_8060cf40;
extern u16  DAT_8060cf44;
extern u16  DAT_8060cf48;
extern u16  DAT_8060cf4c;
extern u16  DAT_8060cf50;
extern u16  DAT_8060cf54;
extern u16  DAT_8060cf58;
extern u16  DAT_8060cf5c;

// ---- dB->linear pan/attenuation lookup tables (audio ROM) -----------------
extern short DAT_8049ec30;  // dB offset -> linear volume (stride 2, biased)
extern int   DAT_8049ecac;  // stereo pan table (stride 4)
extern short DAT_8049eeac;  // surround pan table A (stride 2)
extern short DAT_8049efac;  // surround pan table B (stride 2)

// Return the next profiling-scratch destination in a 0x38-byte ring (or 0 when
// profiling is disabled), advancing the cursor modulo the ring length.
int ax_profile_scratch_next(void)
{
    int slot;

    if (DAT_806133dc != 0) {
        slot = DAT_806133d8 * 0x38;
        DAT_806133d8 = (DAT_806133d8 + 1U) - ((DAT_806133d8 + 1U) / DAT_806133d4) * DAT_806133d4;
        return DAT_806133d0 + slot;
    }
    return 0;
}

// Fill a mixer slot's pan-attenuation table from its pan indices (slot+0x14 =
// left/right index, slot+0x18 = front/back index). In DPL2 mode (mode 3) the
// six entries come from the two surround tables; otherwise from the stereo pan
// table. slot points at a mixer parameter block.
void ax_mixer_slot_pan_table(int slot)
{
    int lr;
    int fb;
    int lrx;

    lr = *(int*)(slot + 0x14);
    fb = *(int*)(slot + 0x18);
    if (DAT_806133e8 == 3) {
        lrx = (0x7f - lr) * 2;
        *(int*)(slot + 0x20) = (int)*(short*)(&DAT_8049eeac + lr * 2);
        *(int*)(slot + 0x24) = (int)*(short*)(&DAT_8049eeac + lrx);
        *(int*)(slot + 0x28) = (int)*(short*)(&DAT_8049eeac + (0x7f - fb) * 2);
        *(int*)(slot + 0x2c) = (int)*(short*)(&DAT_8049eeac + fb * 2);
        *(int*)(slot + 0x30) = (int)*(short*)(&DAT_8049efac + lrx);
        *(int*)(slot + 0x34) = (int)*(short*)(&DAT_8049efac + lr * 2);
        return;
    }
    *(u32*)(slot + 0x20) = *(u32*)(&DAT_8049ecac + lr * 4);
    *(u32*)(slot + 0x24) = *(u32*)(&DAT_8049ecac + (0x7f - lr) * 4);
    *(u32*)(slot + 0x28) = *(u32*)(&DAT_8049ecac + (0x7f - fb) * 4);
    *(u32*)(slot + 0x2c) = *(u32*)(&DAT_8049ecac + fb * 4);
}

// Reset all 64 mixer parameter slots to defaults (unity volume 0x50000000,
// centre pan 0x40/0x7f, full-range distance) and rebuild each pan table; then
// clear the mixer accumulators and set the mixer mode to 1 (stereo).
void ax_mixer_reset(void)
{
    int   i;
    int*  slot;

    slot = &DAT_8060cf00;
    i = 0;
    do {
        slot[1] = 0x50000000;
        slot[2] = 0;
        slot[3] = 0xfffffc40;
        slot[4] = 0xfffffc40;
        slot[7] = 0;
        slot[5] = 0x40;
        slot[6] = 0x7f;
        *(u16*)(slot + 0x17) = 0;
        *(u16*)(slot + 0x16) = 0;
        *(u16*)(slot + 0x15) = 0;
        *(u16*)(slot + 0x14) = 0;
        *(u16*)(slot + 0x13) = 0;
        *(u16*)(slot + 0x12) = 0;
        *(u16*)(slot + 0x11) = 0;
        *(u16*)(slot + 0x10) = 0;
        *(u16*)(slot + 0xf) = 0;
        *(u16*)(slot + 0xe) = 0;
        ax_mixer_slot_pan_table((int)slot);
        i = i + 1;
        slot = slot + 0x18;
    } while (i < 0x40);
    DAT_806133e0 = 0;
    DAT_806133e4 = 0;
    DAT_806133e8 = 1;
}

// Set the mixer output mode (0 stereo / 3 DPL2 / intermediate).
void ax_mixer_set_mode(int mode)
{
    DAT_806133e8 = mode;
}

// AXSetVoiceMix: compute a voice's per-channel mix volumes from its pan / span
// / volume / distance parameters and write them into the VPB mixer fields.
// params: voice = AX voice struct; ctrl bits (param_2 & 7); the remaining args
// are the pan/volume/attenuation dB terms. Stores the incoming params in the
// voice's mixer slot, rebuilds its pan table (ax_mixer_slot_pan_table), then
// for the active mixer mode (surround mode 3, stereo mode 0, intermediate)
// sums the relevant dB terms per output channel and converts through the
// dB->linear table DAT_8049ec30. Finally latches the computed shorts into the
// VPB channel-volume fields (voice+0x14a.., +0x19c) under an interrupt lock,
// building the per-channel active mask voice+0x144 and raising dirty bits 0x212.
// NON-MATCHING: the flat DAT_8060cf00 table pointer arithmetic and the fully
// unrolled per-channel dB-clamp/lookup chains are preserved verbatim.
void ax_voice_set_mix(int voice, uint ctrl, int dbBase, u32 p4, u32 p5,
                      u32 p6, u32 p7, u32 p8)
{
    short cv;
    int   idx;
    u16   attn;
    int   db;
    int   off;      // idx * 0x60 (byte stride into the param block)
    ushort chanMask;

    idx = *(int*)(voice + 0x18);
    off = idx * 0x60;
    (&DAT_8060cf00)[idx * 0x18] = voice;
    (&DAT_8060cf04)[idx * 0x18] = ctrl & 7;
    (&DAT_8060cf08)[idx * 0x18] = dbBase;
    (&DAT_8060cf0c)[idx * 0x18] = p4;
    (&DAT_8060cf10)[idx * 0x18] = p5;
    (&DAT_8060cf14)[idx * 0x18] = p6;
    (&DAT_8060cf18)[idx * 0x18] = p7;
    (&DAT_8060cf1c)[idx * 0x18] = p8;
    ax_mixer_slot_pan_table((int)(&DAT_8060cf00 + idx * 0x18));
    if (((&DAT_8060cf04)[idx * 0x18] & 4) == 0) {
        if (dbBase < -0x387) {
            attn = 0;
        } else if (dbBase < 0x3c) {
            attn = *(u16*)(&DAT_8049ec30 + dbBase * 2);
        } else {
            attn = 0xff64;
        }
        (&DAT_8060cf38)[idx * 0x30] = attn;
    } else {
        (&DAT_8060cf38)[idx * 0x30] = 0;
    }
    chanMask = 0;
    if (DAT_806133e8 == 3) {
        db = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf20)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
        if (db < -0x387) attn = 0; else if (db < 0x3c) attn = *(u16*)(&DAT_8049ec30 + db * 2); else attn = 0xff64;
        (&DAT_8060cf3c)[idx * 0x30] = attn;
        db = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf24)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
        if (db < -0x387) attn = 0; else if (db < 0x3c) attn = *(u16*)(&DAT_8049ec30 + db * 2); else attn = 0xff64;
        (&DAT_8060cf40)[idx * 0x30] = attn;
        db = (&DAT_8060cf1c)[idx * 0x18] + *(int*)(&DAT_8060cf30 + off) + (&DAT_8060cf2c)[idx * 0x18];
        if (db < -0x387) attn = 0; else if (db < 0x3c) attn = *(u16*)(&DAT_8049ec30 + db * 2); else attn = 0xff64;
        (&DAT_8060cf54)[idx * 0x30] = attn;
        db = (&DAT_8060cf1c)[idx * 0x18] + *(int*)(&DAT_8060cf34 + off) + (&DAT_8060cf2c)[idx * 0x18];
        if (db < -0x387) attn = 0; else if (db < 0x3c) attn = *(u16*)(&DAT_8049ec30 + db * 2); else attn = 0xff64;
        (&DAT_8060cf58)[idx * 0x30] = attn;
        if (((&DAT_8060cf04)[idx * 0x18] & 1) == 0) {
            db = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf20)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
            if (db < -0x387) attn = 0; else if (db < 0x3c) attn = *(u16*)(&DAT_8049ec30 + db * 2); else attn = 0xff64;
            (&DAT_8060cf48)[idx * 0x30] = attn;
            db = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf24)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
            if (db < -0x387) attn = 0; else if (db < 0x3c) attn = *(u16*)(&DAT_8049ec30 + db * 2); else attn = 0xff64;
            (&DAT_8060cf4c)[idx * 0x30] = attn;
            db = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf0c)[idx * 0x18] + *(int*)(&DAT_8060cf30 + off) + (&DAT_8060cf2c)[idx * 0x18];
            if (db < -0x387) attn = 0; else if (db < 0x3c) attn = *(u16*)(&DAT_8049ec30 + db * 2); else attn = 0xff64;
            (&DAT_8060cf50)[idx * 0x30] = attn;
            off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf0c)[idx * 0x18] + *(int*)(&DAT_8060cf34 + off) + (&DAT_8060cf2c)[idx * 0x18];
            if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
            (&DAT_8060cf5c)[idx * 0x30] = attn;
        } else {
            db = (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf20)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
            if (db < -0x387) attn = 0; else if (db < 0x3c) attn = *(u16*)(&DAT_8049ec30 + db * 2); else attn = 0xff64;
            (&DAT_8060cf48)[idx * 0x30] = attn;
            db = (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf24)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
            if (db < -0x387) attn = 0; else if (db < 0x3c) attn = *(u16*)(&DAT_8049ec30 + db * 2); else attn = 0xff64;
            (&DAT_8060cf4c)[idx * 0x30] = attn;
            db = (&DAT_8060cf0c)[idx * 0x18] + *(int*)(&DAT_8060cf30 + off) + (&DAT_8060cf2c)[idx * 0x18];
            if (db < -0x387) attn = 0; else if (db < 0x3c) attn = *(u16*)(&DAT_8049ec30 + db * 2); else attn = 0xff64;
            (&DAT_8060cf50)[idx * 0x30] = attn;
            off = (&DAT_8060cf0c)[idx * 0x18] + *(int*)(&DAT_8060cf34 + off) + (&DAT_8060cf2c)[idx * 0x18];
            if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
            (&DAT_8060cf5c)[idx * 0x30] = attn;
        }
        chanMask = 0x4000;
    } else if (DAT_806133e8 < 3) {
        if (DAT_806133e8 == 0) {
            off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
            if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
            (&DAT_8060cf3c)[idx * 0x30] = attn;
            off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
            if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
            (&DAT_8060cf40)[idx * 0x30] = attn;
            off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf2c)[idx * 0x18] + -0x1e;
            if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
            (&DAT_8060cf44)[idx * 0x30] = attn;
            if (((&DAT_8060cf04)[idx * 0x18] & 1) == 0) {
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf48)[idx * 0x30] = attn;
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf4c)[idx * 0x30] = attn;
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf2c)[idx * 0x18] + -0x1e;
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf50)[idx * 0x30] = attn;
            } else {
                off = (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf48)[idx * 0x30] = attn;
                off = (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf4c)[idx * 0x30] = attn;
                off = (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf2c)[idx * 0x18] + -0x1e;
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf50)[idx * 0x30] = attn;
            }
            if (((&DAT_8060cf04)[idx * 0x18] & 2) == 0) {
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf54)[idx * 0x30] = attn;
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf58)[idx * 0x30] = attn;
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf2c)[idx * 0x18] + -0x1e;
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf5c)[idx * 0x30] = attn;
            } else {
                off = (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf54)[idx * 0x30] = attn;
                off = (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf58)[idx * 0x30] = attn;
                off = (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf2c)[idx * 0x18] + -0x1e;
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf5c)[idx * 0x30] = attn;
            }
        } else if (-1 < DAT_806133e8) {
            off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf20)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
            if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
            (&DAT_8060cf3c)[idx * 0x30] = attn;
            off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf24)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
            if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
            (&DAT_8060cf40)[idx * 0x30] = attn;
            off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf2c)[idx * 0x18] + -0x1e;
            if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
            (&DAT_8060cf44)[idx * 0x30] = attn;
            if (((&DAT_8060cf04)[idx * 0x18] & 1) == 0) {
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf20)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf48)[idx * 0x30] = attn;
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf24)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf4c)[idx * 0x30] = attn;
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf2c)[idx * 0x18] + -0x1e;
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf50)[idx * 0x30] = attn;
            } else {
                off = (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf20)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf48)[idx * 0x30] = attn;
                off = (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf24)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf4c)[idx * 0x30] = attn;
                off = (&DAT_8060cf0c)[idx * 0x18] + (&DAT_8060cf2c)[idx * 0x18] + -0x1e;
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf50)[idx * 0x30] = attn;
            }
            if (((&DAT_8060cf04)[idx * 0x18] & 2) == 0) {
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf20)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf54)[idx * 0x30] = attn;
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf24)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf58)[idx * 0x30] = attn;
                off = (&DAT_8060cf1c)[idx * 0x18] + (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf2c)[idx * 0x18] + -0x1e;
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf5c)[idx * 0x30] = attn;
            } else {
                off = (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf20)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf54)[idx * 0x30] = attn;
                off = (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf24)[idx * 0x18] + (&DAT_8060cf28)[idx * 0x18];
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf58)[idx * 0x30] = attn;
                off = (&DAT_8060cf10)[idx * 0x18] + (&DAT_8060cf2c)[idx * 0x18] + -0x1e;
                if (off < -0x387) attn = 0; else if (off < 0x3c) attn = *(u16*)(&DAT_8049ec30 + off * 2); else attn = 0xff64;
                (&DAT_8060cf5c)[idx * 0x30] = attn;
            }
        }
    }
    os_disable_interrupts();
    *(u16*)(voice + 0x19c) = (&DAT_8060cf38)[idx * 0x30];
    *(u16*)(voice + 0x19e) = 0;
    cv = (&DAT_8060cf3c)[idx * 0x30];
    *(short*)(voice + 0x14a) = cv;
    if (cv != 0) chanMask = chanMask | 1;
    *(u16*)(voice + 0x14c) = 0;
    cv = (&DAT_8060cf40)[idx * 0x30];
    *(short*)(voice + 0x14e) = cv;
    if (cv != 0) chanMask = chanMask | 2;
    *(u16*)(voice + 0x150) = 0;
    cv = (&DAT_8060cf48)[idx * 0x30];
    *(short*)(voice + 0x152) = cv;
    if (cv != 0) chanMask = chanMask | 0x10;
    *(u16*)(voice + 0x154) = 0;
    cv = (&DAT_8060cf4c)[idx * 0x30];
    *(short*)(voice + 0x156) = cv;
    if (cv != 0) chanMask = chanMask | 0x20;
    *(u16*)(voice + 0x158) = 0;
    cv = (&DAT_8060cf54)[idx * 0x30];
    *(short*)(voice + 0x15a) = cv;
    if (cv != 0) chanMask = chanMask | 0x200;
    *(u16*)(voice + 0x15c) = 0;
    cv = (&DAT_8060cf58)[idx * 0x30];
    *(short*)(voice + 0x15e) = cv;
    if (cv != 0) chanMask = chanMask | 0x400;
    *(u16*)(voice + 0x160) = 0;
    cv = (&DAT_8060cf5c)[idx * 0x30];
    *(short*)(voice + 0x162) = cv;
    if (cv != 0) chanMask = chanMask | 0x1000;
    *(u16*)(voice + 0x164) = 0;
    cv = (&DAT_8060cf44)[idx * 0x30];
    *(short*)(voice + 0x166) = cv;
    if (cv != 0) chanMask = chanMask | 4;
    *(u16*)(voice + 0x168) = 0;
    cv = (&DAT_8060cf50)[idx * 0x30];
    *(short*)(voice + 0x16a) = cv;
    if (cv != 0) chanMask = chanMask | 0x80;
    *(u16*)(voice + 0x16c) = 0;
    *(ushort*)(voice + 0x144) = chanMask;
    *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x212;
    os_get_interrupts_enabled();
}

// Clear a voice's mixer slot (drop it from the mixer parameter table).
void ax_mixer_slot_clear(int voice)
{
    (&DAT_8060cf00)[*(int*)(voice + 0x18) * 0x18] = 0;
}

// Set a voice's mixer control flag bit 0x10000000 (external-input routing) and
// store its associated dB base.
void ax_mixer_slot_set_flag(int voice, u32 dbBase)
{
    int idx;

    idx = *(int*)(voice + 0x18);
    (&DAT_8060cf08)[idx * 0x18] = dbBase;
    (&DAT_8060cf04)[idx * 0x18] = (&DAT_8060cf04)[idx * 0x18] | 0x10000000;
}

// ===========================================================================
// batch_1139: mixer slot pan accessors + the per-frame mixer finalize pass.
//
// Address: 0x803affe4 | Name: ax_mixer_slot_get_base_db | Date: 2026-09-14
// Address: 0x803b0000 | Name: ax_mixer_slot_set_pan     | Date: 2026-09-14
// Address: 0x803b006c | Name: ax_mixer_slot_get_pan     | Date: 2026-09-14
// Address: 0x803b0088 | Name: ax_mixer_frame_update     | Date: 2026-09-14
// ===========================================================================

// ---- AI streaming-volume fade (music) -------------------------------------
// ax_mixer_frame_update ramps the AI streaming volume one step/frame toward a
// target using two byte volume-curve tables in the audio ROM; the current step
// is DAT_806133e0 and the target step DAT_806133e4 (both in the mixer-state
// block declared above). The right-channel byte is read from the same curve at
// a -1 offset via the absolute expression the decompiler produced.
extern u8   DAT_8049f0ab;   // AI stream volume curve (down ramp)
extern u8   DAT_8049f0ad;   // AI stream volume curve (up ramp)
extern void ai_set_stream_vol_left(uint vol);
extern void ai_set_stream_vol_right(uint vol);

// Return a mixer slot's base attenuation (dB) parameter (slot+0x08).
// voice+0x18 holds the slot index; the param block strides 0x18 ints/slot.
undefined4 ax_mixer_slot_get_base_db(int voice)
{
    return (&DAT_8060cf08)[*(int*)(voice + 0x18) * 0x18];
}

// Set a mixer slot's pan (slot+0x14), clamped to [0, 0x7f], rebuild the slot's
// pan table and raise the pan-dirty bit 0x40000000 in the slot control word.
void ax_mixer_slot_set_pan(int voice, int pan)
{
    int idx;

    idx = *(int*)(voice + 0x18);
    if (pan < 0) {
        pan = 0;
    } else if (0x7f < pan) {
        pan = 0x7f;
    }
    (&DAT_8060cf14)[idx * 0x18] = pan;
    ax_mixer_slot_pan_table((int)(&DAT_8060cf00 + idx * 0x18));
    (&DAT_8060cf04)[idx * 0x18] = (&DAT_8060cf04)[idx * 0x18] | 0x40000000;
}

// Return a mixer slot's current pan (slot+0x14).
undefined4 ax_mixer_slot_get_pan(int voice)
{
    return (&DAT_8060cf14)[*(int*)(voice + 0x18) * 0x18];
}

// Per-frame mixer finalize pass. Walks all 64 mixer slots; for each active slot
// (slot[0] = voice ptr != 0) it services the pending pan/volume recompute
// requests (control bits 0x10000000 pending-attn, 0x20000000 attn-ready,
// 0x40000000 pan-dirty, 0x80000000 mix-ready), summing the relevant dB terms
// per output channel through the dB->linear table DAT_8049ec30 (clamped to
// [-0x387, 0x3c); below => silence, at/above => 0xff64) according to the mixer
// mode DAT_806133e8 (3 = DPL2 surround, 0 = stereo, intermediate otherwise).
// The finalized target volumes are written into the VPB voice's channel-volume
// fields (voice+0x14a..) together with per-channel ramp deltas (target-current)
// /0xa0 and an active-channel mask (voice+0x144), then the VPB dirty bits 0x212
// are raised (voice+0x1c). Finally it advances the AI streaming-volume fade one
// step toward its target using the byte volume-curve tables.
// NON-MATCHING: the flat DAT_8060cf00 (int*, 0x18 stride) pointer arithmetic and
// the fully unrolled per-channel dB-clamp/lookup chains are preserved verbatim.
void ax_mixer_frame_update(void)
{
    bool     attnDirty;
    byte*    curve;
    short    cv;
    bool     mixDirty;
    int*     slot;
    int      voice;
    undefined2 attn;
    int      db;
    ushort   chanMask;
    uint     ramp;
    int      i;

    slot = &DAT_8060cf00;
    i = 0x40;
    do {
        voice = *slot;
        if (voice != 0) {
            chanMask = 0;
            attnDirty = (slot[1] & 0x20000000U) != 0;
            if (attnDirty) {
                *(undefined2*)(slot + 0xe) = *(undefined2*)((int)slot + 0x3a);
                slot[1] = slot[1] & 0xdfffffff;
            }
            if ((slot[1] & 0x10000000U) != 0) {
                if ((slot[1] & 4U) == 0) {
                    db = slot[2];
                    if (db < -0x387) {
                        attn = 0;
                    } else if (db < 0x3c) {
                        attn = *(undefined2*)(&DAT_8049ec30 + db * 2);
                    } else {
                        attn = 0xff64;
                    }
                    *(undefined2*)((int)slot + 0x3a) = attn;
                } else {
                    *(undefined2*)((int)slot + 0x3a) = 0;
                }
                attnDirty = true;
                slot[1] = slot[1] & 0xefffffff;
                slot[1] = slot[1] | 0x20000000;
            }
            mixDirty = (slot[1] & 0x80000000U) != 0;
            if (mixDirty) {
                *(undefined2*)(slot + 0xf) = *(undefined2*)((int)slot + 0x3e);
                *(undefined2*)(slot + 0x10) = *(undefined2*)((int)slot + 0x42);
                *(undefined2*)(slot + 0x11) = *(undefined2*)((int)slot + 0x46);
                *(undefined2*)(slot + 0x12) = *(undefined2*)((int)slot + 0x4a);
                *(undefined2*)(slot + 0x13) = *(undefined2*)((int)slot + 0x4e);
                *(undefined2*)(slot + 0x14) = *(undefined2*)((int)slot + 0x52);
                *(undefined2*)(slot + 0x15) = *(undefined2*)((int)slot + 0x56);
                *(undefined2*)(slot + 0x16) = *(undefined2*)((int)slot + 0x5a);
                *(undefined2*)(slot + 0x17) = *(undefined2*)((int)slot + 0x5e);
                slot[1] = slot[1] & 0x7fffffff;
            }
            if ((slot[1] & 0x40000000U) != 0) {
                if (DAT_806133e8 == 3) {
                    db = slot[7] + slot[8] + slot[10];
                    if (db < -0x387) { attn = 0; }
                    else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                    else { attn = 0xff64; }
                    *(undefined2*)((int)slot + 0x3e) = attn;
                    db = slot[7] + slot[9] + slot[10];
                    if (db < -0x387) { attn = 0; }
                    else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                    else { attn = 0xff64; }
                    *(undefined2*)((int)slot + 0x42) = attn;
                    db = slot[7] + slot[0xc] + slot[0xb];
                    if (db < -0x387) { attn = 0; }
                    else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                    else { attn = 0xff64; }
                    *(undefined2*)((int)slot + 0x56) = attn;
                    db = slot[7] + slot[0xd] + slot[0xb];
                    if (db < -0x387) { attn = 0; }
                    else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                    else { attn = 0xff64; }
                    *(undefined2*)((int)slot + 0x5a) = attn;
                    if ((slot[1] & 1U) == 0) {
                        db = slot[7] + slot[3] + slot[8] + slot[10];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x4a) = attn;
                        db = slot[7] + slot[3] + slot[9] + slot[10];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x4e) = attn;
                        db = slot[7] + slot[3] + slot[0xc] + slot[0xb];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x52) = attn;
                        db = slot[7] + slot[3] + slot[0xd] + slot[0xb];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x5e) = attn;
                    } else {
                        db = slot[3] + slot[8] + slot[10];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x4a) = attn;
                        db = slot[3] + slot[9] + slot[10];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x4e) = attn;
                        db = slot[3] + slot[0xc] + slot[0xb];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x52) = attn;
                        db = slot[3] + slot[0xd] + slot[0xb];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x5e) = attn;
                    }
                    chanMask = 0x4000;
                } else if (DAT_806133e8 < 3) {
                    if (DAT_806133e8 == 0) {
                        db = slot[7] + slot[10];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x3e) = attn;
                        db = slot[7] + slot[10];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x42) = attn;
                        db = slot[7] + slot[0xb] + -0x1e;
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x46) = attn;
                        if ((slot[1] & 1U) == 0) {
                            db = slot[7] + slot[3] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x4a) = attn;
                            db = slot[7] + slot[3] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x4e) = attn;
                            db = slot[7] + slot[3] + slot[0xb] + -0x1e;
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x52) = attn;
                        } else {
                            db = slot[3] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x4a) = attn;
                            db = slot[3] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x4e) = attn;
                            db = slot[3] + slot[0xb] + -0x1e;
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x52) = attn;
                        }
                        if ((slot[1] & 2U) == 0) {
                            db = slot[7] + slot[4] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x56) = attn;
                            db = slot[7] + slot[4] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x5a) = attn;
                            db = slot[7] + slot[4] + slot[0xb] + -0x1e;
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x5e) = attn;
                        } else {
                            db = slot[4] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x56) = attn;
                            db = slot[4] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x5a) = attn;
                            db = slot[4] + slot[0xb] + -0x1e;
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x5e) = attn;
                        }
                    } else if (-1 < DAT_806133e8) {
                        db = slot[7] + slot[8] + slot[10];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x3e) = attn;
                        db = slot[7] + slot[9] + slot[10];
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x42) = attn;
                        db = slot[7] + slot[0xb] + -0x1e;
                        if (db < -0x387) { attn = 0; }
                        else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                        else { attn = 0xff64; }
                        *(undefined2*)((int)slot + 0x46) = attn;
                        if ((slot[1] & 1U) == 0) {
                            db = slot[7] + slot[3] + slot[8] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x4a) = attn;
                            db = slot[7] + slot[3] + slot[9] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x4e) = attn;
                            db = slot[7] + slot[3] + slot[0xb] + -0x1e;
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x52) = attn;
                        } else {
                            db = slot[3] + slot[8] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x4a) = attn;
                            db = slot[3] + slot[9] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x4e) = attn;
                            db = slot[3] + slot[0xb] + -0x1e;
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x52) = attn;
                        }
                        if ((slot[1] & 2U) == 0) {
                            db = slot[7] + slot[4] + slot[8] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x56) = attn;
                            db = slot[7] + slot[4] + slot[9] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x5a) = attn;
                            db = slot[7] + slot[4] + slot[0xb] + -0x1e;
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x5e) = attn;
                        } else {
                            db = slot[4] + slot[8] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x56) = attn;
                            db = slot[4] + slot[9] + slot[10];
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x5a) = attn;
                            db = slot[4] + slot[0xb] + -0x1e;
                            if (db < -0x387) { attn = 0; }
                            else if (db < 0x3c) { attn = *(undefined2*)(&DAT_8049ec30 + db * 2); }
                            else { attn = 0xff64; }
                            *(undefined2*)((int)slot + 0x5e) = attn;
                        }
                    }
                }
                mixDirty = true;
                slot[1] = slot[1] & 0xbfffffff;
                slot[1] = slot[1] | 0x80000000;
            }
            if (attnDirty) {
                *(undefined2*)(voice + 0x19c) = *(undefined2*)(slot + 0xe);
                db = (uint)*(ushort*)((int)slot + 0x3a) - (uint)*(ushort*)(slot + 0xe);
                db = db / 0xa0 + (db >> 0x1f);
                *(short*)(voice + 0x19e) = (short)db - (short)(db >> 0x1f);
                *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x200;
            }
            if (mixDirty) {
                cv = *(short*)(slot + 0xf);
                *(short*)(voice + 0x14a) = cv;
                if (cv != 0) { chanMask = chanMask | 1; }
                db = (uint)*(ushort*)((int)slot + 0x3e) - (uint)*(ushort*)(slot + 0xf);
                db = db / 0xa0 + (db >> 0x1f);
                ramp = db - (db >> 0x1f);
                *(short*)(voice + 0x14c) = (short)ramp;
                if ((ramp & 0xffff) != 0) { chanMask = chanMask | 8; }
                cv = *(short*)(slot + 0x10);
                *(short*)(voice + 0x14e) = cv;
                if (cv != 0) { chanMask = chanMask | 2; }
                db = (uint)*(ushort*)((int)slot + 0x42) - (uint)*(ushort*)(slot + 0x10);
                db = db / 0xa0 + (db >> 0x1f);
                ramp = db - (db >> 0x1f);
                *(short*)(voice + 0x150) = (short)ramp;
                if ((ramp & 0xffff) != 0) { chanMask = chanMask | 8; }
                cv = *(short*)(slot + 0x12);
                *(short*)(voice + 0x152) = cv;
                if (cv != 0) { chanMask = chanMask | 0x10; }
                db = (uint)*(ushort*)((int)slot + 0x4a) - (uint)*(ushort*)(slot + 0x12);
                db = db / 0xa0 + (db >> 0x1f);
                ramp = db - (db >> 0x1f);
                *(short*)(voice + 0x154) = (short)ramp;
                if ((ramp & 0xffff) != 0) { chanMask = chanMask | 0x40; }
                cv = *(short*)(slot + 0x13);
                *(short*)(voice + 0x156) = cv;
                if (cv != 0) { chanMask = chanMask | 0x20; }
                db = (uint)*(ushort*)((int)slot + 0x4e) - (uint)*(ushort*)(slot + 0x13);
                db = db / 0xa0 + (db >> 0x1f);
                ramp = db - (db >> 0x1f);
                *(short*)(voice + 0x158) = (short)ramp;
                if ((ramp & 0xffff) != 0) { chanMask = chanMask | 0x40; }
                cv = *(short*)(slot + 0x15);
                *(short*)(voice + 0x15a) = cv;
                if (cv != 0) { chanMask = chanMask | 0x200; }
                db = (uint)*(ushort*)((int)slot + 0x56) - (uint)*(ushort*)(slot + 0x15);
                db = db / 0xa0 + (db >> 0x1f);
                ramp = db - (db >> 0x1f);
                *(short*)(voice + 0x15c) = (short)ramp;
                if ((ramp & 0xffff) != 0) { chanMask = chanMask | 0x800; }
                cv = *(short*)(slot + 0x16);
                *(short*)(voice + 0x15e) = cv;
                if (cv != 0) { chanMask = chanMask | 0x400; }
                db = (uint)*(ushort*)((int)slot + 0x5a) - (uint)*(ushort*)(slot + 0x16);
                db = db / 0xa0 + (db >> 0x1f);
                ramp = db - (db >> 0x1f);
                *(short*)(voice + 0x160) = (short)ramp;
                if ((ramp & 0xffff) != 0) { chanMask = chanMask | 0x800; }
                cv = *(short*)(slot + 0x17);
                *(short*)(voice + 0x162) = cv;
                if (cv != 0) { chanMask = chanMask | 0x1000; }
                db = (uint)*(ushort*)((int)slot + 0x5e) - (uint)*(ushort*)(slot + 0x17);
                db = db / 0xa0 + (db >> 0x1f);
                ramp = db - (db >> 0x1f);
                *(short*)(voice + 0x164) = (short)ramp;
                if ((ramp & 0xffff) != 0) { chanMask = chanMask | 0x2000; }
                cv = *(short*)(slot + 0x11);
                *(short*)(voice + 0x166) = cv;
                if (cv != 0) { chanMask = chanMask | 4; }
                db = (uint)*(ushort*)((int)slot + 0x46) - (uint)*(ushort*)(slot + 0x11);
                db = db / 0xa0 + (db >> 0x1f);
                ramp = db - (db >> 0x1f);
                *(short*)(voice + 0x168) = (short)ramp;
                if ((ramp & 0xffff) != 0) { chanMask = chanMask | 8; }
                cv = *(short*)(slot + 0x14);
                *(short*)(voice + 0x16a) = cv;
                if (cv != 0) { chanMask = chanMask | 0x80; }
                db = (uint)*(ushort*)((int)slot + 0x52) - (uint)*(ushort*)(slot + 0x14);
                db = db / 0xa0 + (db >> 0x1f);
                ramp = db - (db >> 0x1f);
                *(short*)(voice + 0x16c) = (short)ramp;
                if ((ramp & 0xffff) != 0) { chanMask = chanMask | 0x100; }
                *(ushort*)(voice + 0x144) = chanMask;
                *(uint*)(voice + 0x1c) = *(uint*)(voice + 0x1c) | 0x12;
            }
        }
        slot = slot + 0x18;
        i = i + -1;
    } while (i != 0);
    if (DAT_806133e0 < DAT_806133e4) {
        curve = &DAT_8049f0ad + DAT_806133e0;
        DAT_806133e0 = DAT_806133e0 + 1;
        ai_set_stream_vol_left((uint)*curve);
        ai_set_stream_vol_right((uint)*(byte*)(DAT_806133e0 + -0x7fb60f54));
    } else if (DAT_806133e4 < DAT_806133e0) {
        curve = &DAT_8049f0ab + DAT_806133e0;
        DAT_806133e0 = DAT_806133e0 + -1;
        ai_set_stream_vol_left((uint)*curve);
        ai_set_stream_vol_right((uint)*(byte*)(DAT_806133e0 + -0x7fb60f54));
    }
}
