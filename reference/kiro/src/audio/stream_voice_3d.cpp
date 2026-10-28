// stream_voice_3d.cpp
// Streaming-voice / 3D-sound layer: voice-record freelist (0x805a3b68, 0x44 stride),
// 3D volume/pan compute, per-frame volume ramp, intrusive-list unlink.
// Batch: batch_482
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Freelist over the 0x44-stride voice-record pool at DAT_805a3b68.
// -0x7fa5c498 == 0x805a3b68 (the freelist head sentinel is passed by address value).
extern int  freelist_peek_head(void *pList);
extern void freelist_push(int list, void *pNode);

// Audio helpers (defined elsewhere in the tree).
extern int    audio_linear_to_centibels(int linear);
extern float  sound_compute_3d_volume(double posZ, double posY, double posX, float *pDir,
                                       uint flags, int *pOutPan, float *pOutVec);
extern double sound_compute_effective_volume(int gateFlag);
extern double math_sqrt(double x);
extern void   mtx4_transform_vector_alias(float *pMtx, float *pVec);

// Spawn-frame guard pair (returns record ptr in hi word, gate in lo word).
extern unsigned long long FUN_803826e8(void);
extern void FUN_80382734(void);

// Player-count query.
extern int FUN_801c1714(void);

// Low-level voice mixer set / update.
extern void FUN_803affbc(int voice, int centibels);
extern void FUN_8018b3ac(void);

// Voice shutdown chain.
extern void FUN_8018b5ec(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8);
extern void FUN_801a225c(void);
extern void FUN_803b0088(void);

// Data
extern int   DAT_8060fa8c;   // base of 0x14-stride sound-descriptor table
extern uint  DAT_805015c0;   // global flags (bit 0x20000000 skips nearest-player search)
extern int   DAT_80611d78;   // player pool base (0x141c stride)
extern u16   DAT_806108e0;   // per-frame volume-ramp step

extern float  FLOAT_80616790; // time->index scale
extern float  FLOAT_80616794; // time->index bias
extern float  FLOAT_80616798; // default pan component
extern float  FLOAT_8061679c; // initial nearest-distance seed
extern float  FLOAT_806167a0; // audibility threshold
extern float  FLOAT_806167a4; // min-distance floor for pan normalize
extern double DOUBLE_806167a8; // magic-bias constant (unsigned->double correction)
extern double DOUBLE_806167b0; // ramp reference (u16->double bias)
}

// Address of the voice-record freelist head.
static void *const kVoiceFreeList = (void *)0x805a3b68;

// ---------------------------------------------------------------------------
// Address: 0x801875ac | Name: stream_time_to_index | Date: 2026-09-06
// Scale/bias a time value into an integer table index.
int stream_time_to_index(double time)
{
    return (int)(FLOAT_80616790 * (float)((double)FLOAT_80616794 + time));
}

// ---------------------------------------------------------------------------
// Address: 0x801875d4 | Name: stream_voice_compute_3d | Date: 2026-09-06
// Compute 3D volume + stereo pan for an active streaming voice, writing
// { volume, panL, panR } into pOut[0..2]. Optionally searches for the nearest
// player to orient the pan. Returns the raw 3D volume (used by the caller for
// the low-pass/pitch falloff term).
double stream_voice_compute_3d(u32 unused0, u32 unused1, uint *pOut)
{
    (void)unused0;
    (void)unused1;

    unsigned long long spawn = FUN_803826e8();
    int rec = (int)((unsigned long long)spawn >> 0x20);
    int active = (int)spawn;

    float panVec = FLOAT_80616798;   // default pan.x
    float panOther = FLOAT_8061679c; // seed for nearest-distance compare

    float *pDir = (active == 0) ? (float *)0 : (float *)(rec + 0x1c);

    int outPan;
    float outVec[2];
    float vol3d = sound_compute_3d_volume(
        (double)*(float *)(rec + 0x28),
        (double)*(float *)(rec + 0x18),
        (double)*(float *)(rec + 0x14),
        pDir,
        (uint)*(byte *)(DAT_8060fa8c + (uint)*(u16 *)(rec + 0x38) * 0x14 + 5),
        &outPan, outVec);

    double eff = sound_compute_effective_volume(*(u16 *)(rec + 0x3a) & 0x200);

    // Convert accumulated pan magnitude (as biased double) to a scaled volume.
    uint biased = (uint)outPan ^ 0x80000000u;
    double panMag = (double)CONCAT44(0x43300000, biased) - DOUBLE_806167a8;
    uint volInt = (uint)((double)(float)panMag * eff);
    pOut[0] = volInt;

    float finalX = panVec;
    float finalY = panVec;

    if ((active != 0) &&
        ((*(u16 *)(rec + 0x3a) & 0x10) != 0) &&
        ((double)FLOAT_806167a0 < (double)vol3d))
    {
        int nearest = 0;
        double bestDist = (double)panOther;

        if ((DAT_805015c0 & 0x20000000) == 0) {
            int idx = 0;
            int off = 0;
            int count = FUN_801c1714();
            for (idx = 0; idx < count; idx = idx + 1) {
                int ent = *(int *)(DAT_80611d78 + off + 8);
                if (ent != 0) {
                    float dy = pDir[1] - *(float *)(ent + 0x280);
                    float dx = *pDir     - *(float *)(ent + 0x27c);
                    float dz = pDir[2]   - *(float *)(ent + 0x284);
                    double d2 = (double)(dz * dz + dx * dx + dy * dy);
                    if (d2 < bestDist) {
                        nearest = ent;
                        bestDist = d2;
                    }
                }
                off = off + 0x141c;
                count = FUN_801c1714();
            }
        }

        double len = math_sqrt((double)(outVec[0] * outVec[0] + outVec[1] * outVec[1]));

        if ((nearest != 0) && (*(float **)(nearest + 0x208) != (float *)0)) {
            mtx4_transform_vector_alias(*(float **)(nearest + 0x208), outVec);
        }

        if (len <= (double)FLOAT_806167a4) {
            finalX = FLOAT_80616798;
            finalY = FLOAT_80616798;
        } else {
            finalX = (float)((double)outVec[1] / len);
            finalY = (float)((double)outVec[0] / len);
        }
    }

    pOut[1] = (uint)finalY;
    pOut[2] = (uint)finalX;

    FUN_80382734();
    return (double)vol3d;
}

// ---------------------------------------------------------------------------
// Address: 0x801877f8 | Name: stream_list_unlink | Date: 2026-09-06
// Unlink a node from an intrusive doubly-linked list. node[0]=next, node[1]=prev.
void stream_list_unlink(int *node)
{
    *(int *)node[1] = *node;
    *(int *)(*node + 4) = node[1];
    node[1] = 0;
    *node = 0;
}

// ---------------------------------------------------------------------------
// Address: 0x80187820 | Name: stream_voice_nop | Date: 2026-09-06
// Empty routine (placeholder / stubbed callback).
void stream_voice_nop(void)
{
}

// ---------------------------------------------------------------------------
// Address: 0x80187824 | Name: stream_voice_shutdown | Date: 2026-09-06
// Voice shutdown dispatch: forward args to the mixer teardown then run the
// two follow-up cleanup passes.
void stream_voice_shutdown(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                           unsigned long long a4, unsigned long long a5, unsigned long long a6,
                           unsigned long long a7, unsigned long long a8)
{
    FUN_8018b5ec(a1, a2, a3, a4, a5, a6, a7, a8);
    FUN_801a225c();
    FUN_803b0088();
}

// ---------------------------------------------------------------------------
// Address: 0x8018784c | Name: stream_voice_slot_addr | Date: 2026-09-06
// Voice-record address for a slot index (base + index * 0x44).
int stream_voice_slot_addr(int index)
{
    int base = freelist_peek_head(kVoiceFreeList);
    return base + index * 0x44;
}

// ---------------------------------------------------------------------------
// Address: 0x80187888 | Name: stream_voice_slot_index | Date: 2026-09-06
// Slot index for a voice-record address ((addr - base) / 0x44).
int stream_voice_slot_index(int addr)
{
    int base = freelist_peek_head(kVoiceFreeList);
    return (addr - base) / 0x44;
}

// ---------------------------------------------------------------------------
// Address: 0x801878c8 | Name: stream_voice_free | Date: 2026-09-06
// Return a voice record to the freelist (clears rec[2] first).
void stream_voice_free(u32 *rec)
{
    rec[2] = 0;
    freelist_push((int)0x805a3b68, rec);
}

// ---------------------------------------------------------------------------
// Address: 0x80187900 | Name: stream_voice_free_by_index | Date: 2026-09-06
// Free a voice record identified by slot index.
void stream_voice_free_by_index(int index)
{
    u32 *rec = (u32 *)stream_voice_slot_addr(index);
    stream_voice_free(rec);
}

// ---------------------------------------------------------------------------
// Address: 0x80187924 | Name: stream_voice_volume_ramp | Date: 2026-09-06
// Per-frame volume envelope: ramp the fade-in counter (+0x34 toward +0x30) or,
// when at peak, ramp the fade-out counter (+0x36 toward +0x32). Recompute the
// centibel level and push it to the mixer (voice at +8) or update via FUN_8018b3ac.
void stream_voice_volume_ramp(int voice, uint linear, int mode)
{
    double refB = DOUBLE_806167b0;

    if (*(u16 *)(voice + 0x34) < *(u16 *)(voice + 0x30)) {
        *(u16 *)(voice + 0x34) = *(u16 *)(voice + 0x34) + (short)DAT_806108e0;
        if (*(u16 *)(voice + 0x30) < *(u16 *)(voice + 0x34)) {
            *(u16 *)(voice + 0x34) = *(u16 *)(voice + 0x30);
        }
        double lin = (double)CONCAT44(0x43300000, linear ^ 0x80000000u);
        double cur = (double)CONCAT44(0x43300000, (uint)*(u16 *)(voice + 0x34)) - refB;
        double tot = (double)CONCAT44(0x43300000, (uint)*(u16 *)(voice + 0x30)) - refB;
        int cb = audio_linear_to_centibels(
            (int)((float)(lin - DOUBLE_806167a8) * ((float)cur / (float)tot)));
        if (mode == 0) {
            FUN_803affbc(*(int *)(voice + 8), cb);
        } else {
            FUN_8018b3ac();
        }
    } else {
        u16 fadeOut = *(u16 *)(voice + 0x36);
        if ((fadeOut < *(u16 *)(voice + 0x32)) && (fadeOut != 0)) {
            *(u16 *)(voice + 0x36) = fadeOut - (short)DAT_806108e0;
            double lin = (double)CONCAT44(0x43300000, linear ^ 0x80000000u);
            double cur = (double)CONCAT44(0x43300000, (uint)*(u16 *)(voice + 0x36)) - refB;
            double tot = (double)CONCAT44(0x43300000, (uint)*(u16 *)(voice + 0x32)) - refB;
            int cb = audio_linear_to_centibels(
                (int)((float)(lin - DOUBLE_806167a8) * ((float)cur / (float)tot)));
            if (mode == 0) {
                FUN_803affbc(*(int *)(voice + 8), cb);
            } else {
                FUN_8018b3ac();
            }
        }
    }
}

// ===========================================================================
// Batch: batch_483 — streaming-voice update / spawn core
// ===========================================================================

// ---- Additional raw symbols referenced by the batch_483 functions ----
extern "C" {
// Spawn-frame guard pairs used by the update/spawn/query entry points.
extern unsigned long long FUN_803826e4(void);  // per-voice update frame
extern void FUN_80382730(void);
extern unsigned long long FUN_803826c8(void);   // spawn frame
extern void FUN_80382714(void);
extern unsigned long long FUN_803826f0(void);   // nearest-query frame: {extra, posPtr} pair
extern void FUN_8038273c(void);

// Voice-record helpers already defined in this TU (batch_482).
// (stream_voice_slot_addr, stream_voice_slot_index, stream_voice_free,
//  stream_voice_free_by_index, stream_list_unlink, stream_time_to_index,
//  stream_voice_volume_ramp, stream_voice_compute_3d resolve locally.)

// Mixer / voice back-end.
extern void FUN_803aff9c(int voice);
extern int  FUN_803affe4(int voice);
extern int  FUN_803b006c(int voice);
extern void FUN_803b0000(int voice, int index);
extern void FUN_803ac51c(int *voice, int value);
extern void FUN_803ac328(int *voice);
extern void FUN_803aea60(int voice, int flag);
extern void FUN_803aee48(double gain, int voice);
extern int *FUN_803ac3a8(uint prio, int list, int slot);
extern void FUN_803af120(int voice, int a2, int cb, int a4, int a5, int pitch, int a7, int a8);

// Sync guards.
extern void FUN_8039cca0(void);
extern void FUN_8039ccc8(void);

// Hardware-voice (uVar14 != 0) path helpers.
extern int  FUN_8018b35c(int channel);
extern void FUN_8018b3ac(void);
extern void FUN_8018b448(int channel, int index);
extern void FUN_8018b2d8(int channel);
extern void FUN_8018b5b4(int channel);
extern void FUN_8018b5d0(int channel);
extern int  FUN_8018b19c(double a1, double a2, double a3, unsigned long long a4, unsigned long long a5,
                         unsigned long long a6, unsigned long long a7, unsigned long long a8,
                         int a9, unsigned int a10, unsigned int a11, unsigned int a12,
                         unsigned int a13, int a14, int a15, unsigned int a16);

// Freelist pop over the voice-record pool.
extern void *freelist_pop(int list);

// Envelope / index helpers.
extern uint FUN_80187e80(int v);      // fade-out ticks from byte
extern uint FUN_80187ea8(int v);      // fade-in ticks from byte
extern void FUN_80187ec8(int *list, int *node);  // intrusive list insert
extern void FUN_80187ee4(unsigned int a1, int voice, uint dupFlag);  // voice init helper
extern int  FUN_8039cca0_unused(void);

// Resource / pack lookups.
extern int pack_get_entry_id_at_index(int pack, int index);
extern int DAT_80611880;

// Data referenced by batch_483.
extern int    DAT_80612484;   // per-descriptor 0x44-stride sample table base
extern int    DAT_80611008;   // nearest-player search enable
extern int    DAT_80611010;   // master volume scalar
extern int    DAT_80612480;   // last hardware-voice handle
extern float  FLOAT_806167b8;  // pitch base
extern float  FLOAT_806167bc;  // pitch divisor
extern float  FLOAT_806167c4;  // sample-rate divisor
extern float  FLOAT_806167cc;  // spawn pitch scale
extern float  FLOAT_806167d0;  // spawn volume scale
extern int    DAT_805a3b78;    // active-voice list head (SFX)
extern int    DAT_805a3bbc;    // active-voice list head (3D)
}

// ---------------------------------------------------------------------------
// Address: 0x801880f0 | Name: stream_voice_alloc | Date: 2026-09-06
// Pop a free voice record; return its slot index or -1 when the pool is empty.
int stream_voice_alloc(void)
{
    u32 *node = (u32 *)freelist_pop((int)0x805a3b68);
    if (node == (u32 *)0) {
        return -1;
    }
    return stream_voice_slot_index((int)node);
}

// ---------------------------------------------------------------------------
// Address: 0x8018812c | Name: stream_nearest_player_dist_a | Date: 2026-09-06
// Return the squared distance to the nearest live player from the current
// spawn-frame position, seeded with FLOAT_8061679c. (Duplicate body of the
// 0x8018821c variant; kept as separate entry points in the original.)
double stream_nearest_player_dist_a(void)
{
    float *pos = (float *)(uintptr_t)(u32)FUN_803826f0();
    double best = (double)FLOAT_8061679c;

    if ((DAT_805015c0 & 0x20000000) == 0) {
        int off = 0;
        int count = FUN_801c1714();
        for (int i = 0; i < count; i = i + 1) {
            int ent = *(int *)(DAT_80611d78 + off + 8);
            if (ent != 0) {
                float dy = pos[1] - *(float *)(ent + 0x280);
                float dx = *pos   - *(float *)(ent + 0x27c);
                float dz = pos[2] - *(float *)(ent + 0x284);
                double d2 = (double)(dz * dz + dx * dx + dy * dy);
                if (d2 < best) {
                    best = d2;
                }
            }
            off = off + 0x141c;
            count = FUN_801c1714();
        }
    }
    FUN_8038273c();
    return best;
}

// ---------------------------------------------------------------------------
// Address: 0x8018821c | Name: stream_nearest_player_dist_b | Date: 2026-09-06
// Second nearest-player squared-distance query (identical body to the _a
// variant; separate address in the original binary).
double stream_nearest_player_dist_b(void)
{
    float *pos = (float *)(uintptr_t)(u32)FUN_803826f0();
    double best = (double)FLOAT_8061679c;

    if ((DAT_805015c0 & 0x20000000) == 0) {
        int off = 0;
        int count = FUN_801c1714();
        for (int i = 0; i < count; i = i + 1) {
            int ent = *(int *)(DAT_80611d78 + off + 8);
            if (ent != 0) {
                float dy = pos[1] - *(float *)(ent + 0x280);
                float dx = *pos   - *(float *)(ent + 0x27c);
                float dz = pos[2] - *(float *)(ent + 0x284);
                double d2 = (double)(dz * dz + dx * dx + dy * dy);
                if (d2 < best) {
                    best = d2;
                }
            }
            off = off + 0x141c;
            count = FUN_801c1714();
        }
    }
    FUN_8038273c();
    return best;
}

// ---------------------------------------------------------------------------
// Address: 0x801882e8 | Name: stream_voice_release_slot | Date: 2026-09-06
// Release the mixer voice bound to the record at slot (rec[0x14]-1), if valid.
void stream_voice_release_slot(int rec)
{
    uint slot = (uint)(*(int *)(rec + 0x14) - 1);
    if (slot < 0x24) {
        int addr = stream_voice_slot_addr((int)slot);
        if (*(int *)(addr + 8) != 0) {
            FUN_803aff9c(*(int *)(addr + 8));
        }
        *(int *)(addr + 8) = 0;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018833c | Name: stream_voice_spawn | Date: 2026-09-06
// Spawn a new streaming voice from a sound-descriptor: run the 3D cull, alloc a
// voice slot, populate the 0x44-stride record (pitch/pan/volume envelope), link
// it onto the SFX or 3D active list, and start the mixer voice (software path)
// or a hardware voice (FUN_8018b19c).
void stream_voice_spawn(u32 arg0, u32 arg1, int pcmAddr, uint fadeIn, int startMode,
                        uint linear, u32 arg6, u32 arg7)
{
    (void)arg0;
    (void)arg1;
    (void)arg6;
    (void)arg7;

    unsigned long long frame = FUN_803826c8();
    int *src = (int *)((unsigned long long)frame >> 0x20);

    byte b1 = *(byte *)(src + 3);
    byte b2 = *(byte *)((int)src + 0xd);
    uint flags = *(uint *)(DAT_8060fa8c + *src * 0x14 + 8);

    if ((DAT_80611008 == 0) || ((DAT_805015c0 & 0x20000000) != 0)) {
        FUN_80382714();
        return;
    }
    if ((src[4] & 0x80) != 0) {
        flags |= 0x400;
    }

    int panPair;
    float panVec[4];
    if (*(char *)(src + 10) != '\0') {
        sound_compute_3d_volume(
            (double)(float)src[0xb], (double)(float)src[2], (double)(float)src[6],
            (float *)(src + 7), (uint)*(byte *)(DAT_8060fa8c + *src * 0x14 + 5), &panPair, panVec);
    }

    if ((*(char *)(src + 10) != '\0') && ((flags & 0x500) == 0) &&
        ((double)((float)src[0xb] * (float)src[0xb]) < stream_nearest_player_dist_b())) {
        FUN_80382714();
        return;
    }

    uint dup = flags & 0x10;
    if (dup != 0) {
        fadeIn = fadeIn + 1;
    }

    int slot = stream_voice_alloc();
    if (slot == -1) {
        FUN_80382714();
        return;
    }

    int *rec = (int *)stream_voice_slot_addr(slot);
    uint hw = flags & 0x240;
    rec[0xb] = src[5];

    if ((*(char *)(src + 10) != '\0') && ((flags & 0x500) == 0)) {
        fadeIn = (uint)stream_nearest_player_dist_a();
    }

    if (hw == 0) {
        int *voice = FUN_803ac3a8(fadeIn, (int)0x80188708, slot + 1);
        rec[2] = (int)voice;
        if (rec[2] == 0) {
            stream_voice_free_by_index(slot);
            FUN_80382714();
            return;
        }
    }

    if (hw == 0) {
        FUN_80187ee4(pcmAddr, rec[2], (uint)(dup != 0));
    }

    uint fadeOutTicks;
    u16 outTicks;
    if ((b1 == 0) && (b2 == 0)) {
        fadeOutTicks = 0;
        outTicks = 0;
    } else {
        fadeOutTicks = FUN_80187ea8((uint)b1) & 0xffff;
        outTicks = (u16)FUN_80187e80((uint)b2);
    }

    double bias = DOUBLE_806167a8;
    uint linBiased = linear ^ 0x80000000u;
    rec[10] = src[0xb];
    rec[6] = src[2];
    rec[5] = src[6];
    rec[3] = (int)frame;
    *(short *)(rec + 0xe) = (short)*src;
    rec[4] = (int)flags;
    *(short *)((int)rec + 0x3a) = (short)src[4];
    *(u16 *)(rec + 0xd) = 0;
    *(short *)(rec + 0xc) = (short)fadeOutTicks;
    *(u16 *)((int)rec + 0x36) = outTicks;
    *(u16 *)((int)rec + 0x32) = outTicks;

    double masterLo = (double)(CONCAT44(0x43300000, (uint)(DAT_80611010 * 0x3fff)) ^ 0x80000000);
    int vol = (int)(((float)((double)CONCAT44(0x43300000, linBiased) - bias) / FLOAT_806167cc) *
                    (float)(masterLo - bias));
    *(short *)(rec + 0xf) = (short)(vol >> 4);
    rec[7] = src[7];
    rec[8] = src[8];
    rec[9] = src[9];

    uint volOut;
    stream_voice_compute_3d((u32)(uintptr_t)rec, (u32)*(byte *)(src + 10), &volOut);
    int pitchIdx = stream_time_to_index((double)(float)rec[0xf]);

    if (*(char *)(src + 10) == '\0') {
        if (startMode == 1) {
            pitchIdx = 0;
        } else if (startMode == 2) {
            pitchIdx = 0x7f;
        }
        FUN_80187ec8(&DAT_805a3bbc, rec);
        if (fadeOutTicks != 0) {
            double m = (double)(CONCAT44(0x43300000, (uint)DAT_80611010) ^ 0x80000000);
            volOut = (uint)((float)((double)(float)rec[6] * (double)(float)(m - DOUBLE_806167a8)) *
                            FLOAT_806167d0);
        }
    } else {
        FUN_80187ec8(&DAT_805a3b78, rec);
    }

    if (hw == 0) {
        int cb = audio_linear_to_centibels((int)volOut);
        FUN_803af120(rec[2], 0, cb, (int)0xfffffc78, (int)0xfffffc78, pitchIdx, 0x7f, 0);
        *(byte *)(rec + 0x10) = 2;
        *(byte *)((int)rec + 0x3e) = 0;
        *(byte *)((int)rec + 0x3f) = 0;
    } else {
        int cb = audio_linear_to_centibels((int)volOut);
        int entryId = pack_get_entry_id_at_index(
            DAT_80611880, *(int *)(DAT_8060fa8c + *src * 0x14 + 0x10));
        uint clz = countLeadingZeros((uint)(0x240 - hw));
        DAT_80612480 = FUN_8018b19c(
            (double)(float)src[5], bias, (double)(float)src[6], 0, 0, 0, 0, 0,
            0, (unsigned int)entryId, (uint)(dup != 0), clz >> 5,
            volOut, pitchIdx, cb, arg7);
        if (DAT_80612480 < 0) {
            stream_list_unlink(rec);
            stream_voice_free_by_index(slot);
        } else {
            *(char *)((int)rec + 0x41) = (char)DAT_80612480;
            *(byte *)((int)rec + 0x3e) = 2;
            *(byte *)((int)rec + 0x3f) = 0;
        }
    }
    FUN_80382714();
}

// ---------------------------------------------------------------------------
// Address: 0x80187ab8 | Name: stream_voice_update | Date: 2026-09-06
// Per-voice per-frame update: advance state machine (+0x3e), recompute 3D
// volume/pan/pitch and push to the mixer (software) or hardware voice, drive the
// fade-in/out ramp, and retire the voice (unlink + free) when its state expires.
void stream_voice_update(void)
{
    unsigned long long frame = FUN_803826e4();
    int *v = (int *)((unsigned long long)frame >> 0x20);
    int active = (int)frame;

    byte state = *(byte *)((int)v + 0x3e);
    uint hw = (uint)v[4] & 0x240;

    if (state == 2) {
        bool alive;
        if (hw == 0) {
            if ((*(short *)(v[2] + 0x146) == 0) ||
                (((*(short *)((int)v + 0x32) != 0) && (*(short *)((int)v + 0x36) == 0)) &&
                 (*(char *)((int)v + 0x3f) == '\0'))) {
                alive = false;
            } else {
                alive = true;
            }
        } else {
            alive = FUN_8018b35c((int)*(char *)((int)v + 0x41)) != 0;
        }

        if (alive) {
            uint linear = 0xffffffff;
            if ((active != 0) && ((v[4] & 0x500) != 0)) {
                uint volPair;
                double vol3d = (double)stream_voice_compute_3d((u32)(uintptr_t)v, 1, &volPair);
                int cb = audio_linear_to_centibels((int)volPair);
                int pitchIdx = stream_time_to_index((double)(float)volPair);
                FUN_8039cca0();
                if (hw == 0) {
                    int curCb = FUN_803affe4(v[2]);
                    if (cb != curCb) {
                        FUN_803affbc(v[2], cb);
                    }
                    int curPitch = FUN_803b006c(v[2]);
                    if (pitchIdx != curPitch) {
                        FUN_803b0000(v[2], pitchIdx);
                    }
                } else {
                    FUN_8018b3ac();
                    if (hw != 0x240) {
                        FUN_8018b448((int)*(char *)((int)v + 0x41), pitchIdx);
                    }
                }
                if (((v[4] & 0x10) == 0) && (hw == 0)) {
                    int pv = (int)(FLOAT_806167b8 - (float)(vol3d / (double)FLOAT_806167bc));
                    if (pv < 5) {
                        pv = 5;
                    }
                    FUN_803ac51c((int *)v[2], pv);
                }
                FUN_8039ccc8();
                linear = volPair;
            }

            if (((*(u16 *)(v + 0xc) != 0) && (*(u16 *)(v + 0xd) < *(u16 *)(v + 0xc))) ||
                ((*(u16 *)((int)v + 0x32) != 0) && (*(u16 *)((int)v + 0x36) < *(u16 *)((int)v + 0x32)))) {
                FUN_8039cca0();
                if (linear == 0xffffffff) {
                    linear = (uint)*(short *)(v + 0xf);
                }
                stream_voice_volume_ramp((int)v, linear, (int)hw);
                FUN_8039ccc8();
            }
            FUN_80382730();
            return;
        }
    } else if (state < 2) {
        if (state == 0) {
            *(char *)(v + 0x10) = *(char *)(v + 0x10) + -1;
            if (*(char *)(v + 0x10) == '\0') {
                *(byte *)((int)v + 0x3e) = 1;
            }
        } else if (*(char *)((int)v + 0x3f) == '\0') {
            FUN_8039cca0();
            FUN_803aea60(v[2], 1);
            FUN_8039ccc8();
            *(byte *)((int)v + 0x3e) = 2;
        }
        FUN_80382730();
        return;
    } else if (state > 3) {
        FUN_80382730();
        return;
    }

    // Retire path (state==2 not alive, or state==3).
    if (hw == 0) {
        FUN_8039cca0();
        FUN_803aff9c(v[2]);
        FUN_803ac328((int *)v[2]);
        FUN_8039ccc8();
    } else {
        FUN_8018b2d8((int)*(char *)((int)v + 0x41));
    }
    stream_list_unlink(v);
    stream_voice_free((u32 *)v);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// Address: 0x80188754 | Name: stream_voice_spawn_by_desc | Date: 2026-09-06
// Validate a sound-descriptor index (0..0x1648, non-negative sample offset) and
// spawn a voice for it. Returns 0 on success, -1 on invalid index / negative
// spawn result (sign-extension of FUN_8018833c's implicit result).
int stream_voice_spawn_by_desc(u32 arg0, u32 arg1, int descIndex)
{
    if ((descIndex < 0) || (0x1648 < descIndex)) {
        return -1;
    }
    int desc = DAT_8060fa8c + descIndex * 0x14;
    if (*(int *)(desc + 0x10) < 0) {
        return -1;
    }
    // Spawn; FUN_8018833c returns void in the port, so the original's
    // sign-extended result is not observable — treat a successful call as 0.
    stream_voice_spawn(arg0, arg1, DAT_80612484 + *(int *)(desc + 0xc) * 0x44,
                       0xd, 3, (uint)*(byte *)(desc + 5), 0, 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Address: 0x801887cc | Name: stream_voice_request_stop | Date: 2026-09-06
// Request a voice stop: software voices transition to retire state 3; hardware
// voices tear down immediately (unlink + free).
void stream_voice_request_stop(int *v)
{
    if ((v[4] & 0x240) == 0) {
        *(byte *)((int)v + 0x3e) = 3;
    } else {
        FUN_8018b2d8((int)*(char *)((int)v + 0x41));
        stream_list_unlink(v);
        stream_voice_free((u32 *)v);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80188828 | Name: stream_voice_mute | Date: 2026-09-06
// Mute a voice to the default pan gain (software) or route to the hardware mute
// path; marks the record's mute flag (+0x3f = 1).
void stream_voice_mute(int rec)
{
    if ((*(uint *)(rec + 0x10) & 0x240) == 0) {
        FUN_8039cca0();
        if (*(int *)(rec + 8) != 0) {
            FUN_803aee48((double)FLOAT_80616798, *(int *)(rec + 8));
        }
        FUN_8039ccc8();
        *(byte *)(rec + 0x3f) = 1;
    } else {
        FUN_8018b5b4((int)*(char *)(rec + 0x41));
    }
}

// ---------------------------------------------------------------------------
// Address: 0x801888a4 | Name: stream_voice_unmute | Date: 2026-09-06
// Restore a voice's gain from its descriptor's sample-rate scale (software) or
// hardware unmute path; clears the record's mute flag (+0x3f = 0).
void stream_voice_unmute(int rec)
{
    if ((*(uint *)(rec + 0x10) & 0x240) == 0) {
        if (*(int *)(rec + 8) != 0) {
            int sampAddr = DAT_80612484 +
                *(int *)(DAT_8060fa8c + (uint)*(u16 *)(rec + 0x38) * 0x14 + 0xc) * 0x44 + 4;
            double scale = (double)((float)((double)CONCAT44(0x43300000, *(u32 *)sampAddr) -
                                            DOUBLE_806167b0) / FLOAT_806167c4);
            FUN_8039cca0();
            FUN_803aee48((double)(float)((double)*(float *)(rec + 0x2c) * scale),
                         *(int *)(rec + 8));
            FUN_8039ccc8();
        }
        *(byte *)(rec + 0x3f) = 0;
    } else {
        FUN_8018b5d0((int)*(char *)(rec + 0x41));
    }
}

// ===========================================================================
// Batch: batch_484 — streaming-voice lookup / control API + asset stream loader
// ===========================================================================

// ---- Additional raw symbols referenced by the batch_484 functions ----
extern "C" {
// Asset streaming / scratch allocation (sound-bank loader path).
extern int  asset_get_size(unsigned long long f1, double a2, double a3, unsigned long long a4,
                           unsigned long long a5, unsigned long long a6, unsigned long long a7,
                           unsigned long long a8, void *pName);
extern void asset_stream_read(unsigned long long f1, double a2, double a3, unsigned long long a4,
                              unsigned long long a5, unsigned long long a6, unsigned long long a7,
                              unsigned long long a8, void *pName, int fileCtx, int chunkSize,
                              void *pCursor, void (*cb)(u32, u32, uint *),
                              u32 a14, u32 a15, u32 a16);
extern uint occ_scratch_alloc(uint size, int tag);
extern void occ_query_run(int a1, int a2, uint size, int a4);
extern void leaf_31e8(uint ptr, uint a2, int a3);
extern void memset_wrapper(int dst, int val, int len);
extern int  DAT_8060f940;   // active DVD/stream file context for sound-bank reads

}

// ---------------------------------------------------------------------------
// Address: 0x8018897c | Name: stream_voice_loader_nop | Date: 2026-09-06
// Empty routine (stubbed loader hook).
void stream_voice_loader_nop(void)
{
}

// ---------------------------------------------------------------------------
// Address: 0x80188980 | Name: stream_asset_read_chunk_cb | Date: 2026-09-06
// asset_stream_read completion callback: flush the previous scratch chunk (if
// any) via leaf_31e8, then run an occlusion query over the freshly read chunk
// and advance the cursor. The spawn-frame pair supplies {chunkSize, cursor}.
void stream_asset_read_chunk_cb(u32 arg0, u32 arg1, uint *pState)
{
    (void)arg0;
    (void)arg1;

    unsigned long long frame = FUN_803826f0();
    uint hi = (uint)((unsigned long long)frame >> 0x20);
    int lo = (int)frame;

    if (*pState != 0) {
        leaf_31e8(*pState, hi, 0x60);
        *pState = 0;
        hi = hi + 0x60;
        lo = lo - 0x60;
    }

    uint aligned = ((uint)lo + 0x1f) & 0xffffffe0u;
    occ_query_run((int)hi, (int)pState[1], aligned, 1);
    pState[1] = pState[1] + aligned;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Address: 0x80188a10 | Name: stream_voice_find_by_id | Date: 2026-09-06
// Search the 3D active list (DAT_805a3bbc) then the SFX active list
// (DAT_805a3b78) for the record whose id (rec[3]) matches. Writes *pIsSfx = 0
// for a 3D match, 1 for an SFX match. Returns the record ptr, or NULL.
int *stream_voice_find_by_id(int id, int *pIsSfx)
{
    int *p;
    for (p = (int *)DAT_805a3bbc; (int *)p != (int *)&DAT_805a3bbc; p = (int *)*p) {
        if (p[3] == id) {
            *pIsSfx = 0;
            return p;
        }
    }
    for (p = (int *)DAT_805a3b78; (int *)p != (int *)&DAT_805a3b78; p = (int *)*p) {
        if (p[3] == id) {
            *pIsSfx = 1;
            return p;
        }
    }
    return (int *)0;
}

// ---------------------------------------------------------------------------
// Address: 0x80188a80 | Name: stream_voice_lookup | Date: 2026-09-06
// Convenience wrapper: find a voice record by id, discarding the SFX/3D flag.
int *stream_voice_lookup(int id)
{
    int isSfx;
    return stream_voice_find_by_id(id, &isSfx);
}

// ---------------------------------------------------------------------------
// Address: 0x80188aa4 | Name: stream_sound_bank_load | Date: 2026-09-06
// Load and decode a streamed sound-bank entry into the record at outRec. Query
// the asset size, allocate a scratch region, stream it in through
// stream_asset_read_chunk_cb, then parse the header: 0x7f7f magic selects the
// raw layout; otherwise ADPCM with an optional loop block and a 16-entry
// coefficient table copied into the record. Writes *pSize with the byte count.
void stream_sound_bank_load(u32 arg0, double a2, double a3, u32 arg4, u32 arg5, u32 arg6,
                            u32 arg7, u32 arg8, u32 arg9, u32 arg10, int *pSize,
                            u32 arg12, u32 arg13, u32 arg14, u32 arg15, u32 arg16,
                            short *outRec, byte *pName)
{
    (void)arg0; (void)arg4; (void)arg5; (void)arg6; (void)arg7; (void)arg8;
    (void)arg9; (void)arg10; (void)arg12; (void)arg13;

    // Header scratch (mirrors the on-stack layout the reader fills in).
    unsigned char hdr[0x80];
    unsigned char *cursor = hdr;
    unsigned int scratch;

    int size = asset_get_size(0, a2, a3, arg4, arg5, arg6, arg7, arg8, pName);
    if ((-1 < size) && (0 < size)) {
        *pSize = size;
        scratch = occ_scratch_alloc(((uint)size - 0x41) & 0xffffffe0u, 0);

        // pState = { cursorPtr, scratch }
        struct { unsigned char *pCur; unsigned int scr; } state;
        state.pCur = hdr;
        state.scr  = scratch;

        asset_stream_read(0, a2, a3, arg4, arg5, arg6, arg7, arg8, pName,
                          DAT_8060f940, 0x10000, &state, stream_asset_read_chunk_cb,
                          arg14, arg15, arg16);

        // Header fields (offsets within the streamed 0x80-byte block).
        u32   f_u4    = *(u32 *)(hdr + 4);      // uStack_78
        short f_type  = *(short *)(hdr + 8);     // sStack_74
        short f_magic = *(short *)(hdr + 0xa);   // sStack_72
        int   f_a     = *(int *)(hdr + 0x10);    // iStack_70
        int   f_b     = *(int *)(hdr + 0x14);    // iStack_6c
        short *coeffs = (short *)(hdr + 0x1c);    // auStack_64[..]
        short loopA0  = *(short *)(hdr + 0x3c);   // sStack_44
        short loopA1  = *(short *)(hdr + 0x3e);
        short loopA2  = *(short *)(hdr + 0x40);
        short loopA3  = *(short *)(hdr + 0x42);
        short loopB0  = *(short *)(hdr + 0x44);
        short loopB1  = *(short *)(hdr + 0x46);
        short loopB2  = *(short *)(hdr + 0x48);

        if (f_magic == 0x7f7f) {
            memset_wrapper((int)outRec, 0, 0x44);
            *(uint *)(outRec + 8) = scratch >> 1;
            *(int *)(outRec + 4) = *(int *)(outRec + 8) + f_a;
            *(int *)(outRec + 6) = *(int *)(outRec + 8) + f_b;
            *(u32 *)(outRec + 2) = f_u4;
            *outRec = f_type;
            outRec[1] = f_magic;
            outRec[0x1a] = 0x800;
            outRec[0x1e] = 0;
            outRec[0x1f] = 0;
            outRec[0x20] = 0;
        } else if (f_type == 0) {
            *(uint *)(outRec + 8) = scratch << 1;
            *(int *)(outRec + 4) = *(int *)(outRec + 8) + 2;
            *(int *)(outRec + 6) = *(int *)(outRec + 8) + f_b;
            outRec[0x1a] = loopA0;
            outRec[0x1b] = loopA1;
            outRec[0x1c] = loopA2;
            outRec[0x1d] = loopA3;
            outRec[0x1e] = 0;
            outRec[0x1f] = 0;
            outRec[0x20] = 0;
            *(u32 *)(outRec + 2) = f_u4;
            *outRec = f_type;
            outRec[1] = f_magic;
            for (int i = 0; i < 16; i = i + 1) {
                outRec[0xa + i] = coeffs[i];
            }
        } else {
            *(uint *)(outRec + 8) = scratch << 1;
            *(int *)(outRec + 4) = *(int *)(outRec + 8) + f_a;
            *(int *)(outRec + 6) = *(int *)(outRec + 8) + f_b;
            outRec[0x1a] = loopA0;
            outRec[0x1b] = loopA1;
            outRec[0x1c] = loopA2;
            outRec[0x1d] = loopA3;
            outRec[0x1e] = loopB0;
            outRec[0x1f] = loopB1;
            outRec[0x20] = loopB2;
            *(u32 *)(outRec + 2) = f_u4;
            *outRec = f_type;
            outRec[1] = f_magic;
            for (int i = 0; i < 16; i = i + 1) {
                outRec[0xa + i] = coeffs[i];
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80188cb4 | Name: stream_voice_byteswap_field | Date: 2026-09-06
// Byte-swap the 32-bit field at rec+8 (endian fix-up for streamed data).
void stream_voice_byteswap_field(int rec)
{
    uint v = *(uint *)(rec + 8);
    *(uint *)(rec + 8) =
        v << 0x18 | (v & 0xff00) << 8 | (v >> 8 & 0xff00) | v >> 0x18;
}

// ---------------------------------------------------------------------------
// Address: 0x80188cc4 | Name: stream_voice_spawn_from_desc_ptr | Date: 2026-09-06
// Spawn a voice from a descriptor pointer (desc index = *desc).
void stream_voice_spawn_from_desc_ptr(int *desc, u32 arg1)
{
    stream_voice_spawn_by_desc((u32)(uintptr_t)desc, arg1, *desc);
}

// ---------------------------------------------------------------------------
// Address: 0x80188ce8 | Name: stream_voice_stop_by_id | Date: 2026-09-06
// Look up a voice by id and request it to stop.
void stream_voice_stop_by_id(int id)
{
    int *rec = stream_voice_lookup(id);
    if (rec != (int *)0) {
        stream_voice_request_stop(rec);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80188d14 | Name: stream_voice_stop_all | Date: 2026-09-06
// Request every active voice (3D list then SFX list) to stop.
void stream_voice_stop_all(void)
{
    int *p = (int *)DAT_805a3bbc;
    while ((int *)p != (int *)&DAT_805a3bbc) {
        int *next = (int *)*p;
        stream_voice_request_stop(p);
        p = next;
    }
    p = (int *)DAT_805a3b78;
    while ((int *)p != (int *)&DAT_805a3b78) {
        int *next = (int *)*p;
        stream_voice_request_stop(p);
        p = next;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80188d88 | Name: stream_voice_set_scale_by_id | Date: 2026-09-06
// Look up a voice by id and set its scale/volume float at +0x28.
void stream_voice_set_scale_by_id(double scale, int id)
{
    int rec = (int)stream_voice_lookup(id);
    if (rec != 0) {
        *(float *)(rec + 0x28) = (float)scale;
    }
}

// ===========================================================================
// Batch: batch_485 — streaming-voice control setters + per-frame mute/update +
//                     sound-data (.sfx bank) master loader
// ===========================================================================

// ---- Additional raw symbols referenced by the batch_485 functions ----
// NOTE: this TU includes only common.h (not generated_decls.h), so every
// cross-module helper needs a per-file declaration here with the ABI-padded
// signature that matches the call sites below.
extern "C" {
// Hardware-voice (dual-channel) volume / stereo-pan control.
extern void FUN_8018b4b0(double gain, int channel);            // set hw voice gain (base+ch*0x48)
extern void FUN_8018b514(u32 a1, u32 a2, int right);           // set hw stereo pan (L=save_gpr_28, R)

// Sound-bank per-entry name loader (parses "!"-prefixed indirection).
extern void FUN_8036412c(u32 a1, double a2, double a3, u32 a4, u32 a5, u32 a6, u32 a7, u32 a8,
                         char *pName, int outRec, u32 a11, u32 a12, u32 a13, u32 a14,
                         u32 a15, u32 a16);
// strchr-like scan (find first byte == ch, or end when ch==0).
extern char *FUN_8038b308(int pStr, char ch);

// Named cross-module helpers (ABI-padded prototypes for this TU's call sites).
extern void loadscreen_set_phase(int phase);
extern void loadscreen_advance_progress(double v);
extern void music_stub_nop(void);
extern void *asset_load_from_dvd(u32 f1, double a2, double a3, u32 a4, u32 a5, u32 a6,
                                 u32 a7, u32 a8, char *pPath, int zero, void *pSizeOut,
                                 u32 a12, u32 a13, u32 a14, u32 a15, u32 a16);
extern int  mem_alloc(int size, int type);
extern void mem_free_top(int ptr);
extern void *sound_get_bank_flags(int sound_id);
extern int  pack_find_entry_index_by_hash(int nSlot, int pName);
extern void audio_scratch_reserve(void);
extern void sound_calc_distance_volume(double distance);
// (stream_sound_bank_load is defined earlier in this TU — already visible here.)

// Data referenced by batch_485.
extern int   DAT_80612614;     // sound-data table build revision / count gate (0x67 == already built)
extern int   DAT_80611880;     // sound pack handle (hashed-entry lookup base)
extern u16   DAT_803ebfd8;     // sample-rate table (2-byte stride, indexed by descriptor byte+4)
extern char  s_sound_sounddata_8041e788[];  // "sound/sounddata" asset path

extern float  FLOAT_806167d4;  // stereo-pan mix scale
extern float  FLOAT_806167d8;  // pan->centibel scale
extern float  FLOAT_806167cc;  // pan divisor (shared with spawn path)
extern float  FLOAT_806167dc;  // samples->ticks divisor
extern float  FLOAT_806167e0;  // pitch-ratio floor
extern float  FLOAT_806167e4;  // per-voice distance-volume weight
}

// ---------------------------------------------------------------------------
// Address: 0x80188dc0 | Name: stream_voice_set_pitch_by_id | Date: 2026-09-06
// Look up a voice by id and set its pitch float at record+0x18.
void stream_voice_set_pitch_by_id(double pitch, int id)
{
    int rec = (int)stream_voice_lookup(id);
    if (rec != 0) {
        *(float *)(rec + 0x18) = (float)pitch;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80188df8 | Name: stream_voice_set_volume_by_id | Date: 2026-09-06
// Look up a voice by id (which also encodes a mode bit) and set its volume.
// Skips muted records (+0x3f). Software voices (rec+8 == 0) with the 0x10000000
// id-flag route to the hardware channel gain; otherwise scale the descriptor's
// sample-rate factor and push the gain to the mixer voice, caching it at +0x2c.
void stream_voice_set_volume_by_id(double vol, uint id)
{
    int rec = (int)stream_voice_lookup((int)id);
    if ((rec != 0) && (*(char *)(rec + 0x3f) == '\0')) {
        if (*(int *)(rec + 8) == 0) {
            if ((id & 0x10000000) != 0) {
                FUN_8018b4b0(vol, (int)*(char *)(rec + 0x41));
            }
        } else {
            u32 sampAddr = (u32)(DAT_80612484 +
                *(int *)(DAT_8060fa8c + (uint)*(u16 *)(rec + 0x38) * 0x14 + 0xc) * 0x44 + 4);
            double scale = (double)((float)((double)CONCAT44(0x43300000, *(u32 *)sampAddr) -
                                            DOUBLE_806167b0) / FLOAT_806167c4);
            FUN_8039cca0();
            FUN_803aee48((double)(float)(vol * scale), *(int *)(rec + 8));
            FUN_8039ccc8();
            *(float *)(rec + 0x2c) = (float)vol;
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80188ef4 | Name: stream_voice_update_stereo_pan | Date: 2026-09-06
// Recompute a voice's stereo image from a left/right listener-position pair.
// Derives the biased L and R distance-attenuation terms, stores the combined
// pan word at rec+0x3c, converts to a pitch/time index, and (subject to the
// per-list gate counters at +0x30..+0x36) pushes centibel volume + pitch to the
// mixer: software voices via FUN_803affbc/FUN_803b0000, hardware dual-channel
// via FUN_8018b514, and single hardware voices via FUN_8018b3ac/FUN_8018b448.
void stream_voice_update_stereo_pan(u32 arg0, u32 arg1, uint posR)
{
    (void)arg0;
    (void)arg1;

    unsigned long long frame = FUN_803826e8();
    int rec = (int)stream_voice_lookup((int)(frame >> 0x20));
    if (rec != 0) {
        double biasR = (double)CONCAT44(0x43300000, DAT_80611010 ^ 0x80000000u);
        double distL = (double)((float)((double)CONCAT44(0x43300000, (uint)frame ^ 0x80000000u) -
                                        DOUBLE_806167a8) *
                                (float)(biasR - DOUBLE_806167a8) * FLOAT_806167d0);
        double distR = (double)((float)((double)CONCAT44(0x43300000, posR ^ 0x80000000u) -
                                        DOUBLE_806167a8) *
                                (float)(biasR - DOUBLE_806167a8) * FLOAT_806167d0);

        float mix = FLOAT_806167d4 * (float)(distL + distR);
        *(short *)(rec + 0x3c) = (short)(int)(FLOAT_806167d8 * (mix / FLOAT_806167cc));
        int pitchIdx = stream_time_to_index((double)((float)(distL - distR) / mix));

        bool play;
        int cb = 0;
        if (((*(u16 *)(rec + 0x30) == 0) || (*(u16 *)(rec + 0x30) <= *(u16 *)(rec + 0x34))) &&
            ((*(u16 *)(rec + 0x32) == 0) || (*(u16 *)(rec + 0x32) <= *(u16 *)(rec + 0x36)))) {
            play = true;
            cb = audio_linear_to_centibels((int)*(short *)(rec + 0x3c));
        } else {
            play = false;
        }

        uint hw = frame & 0x1000000000000000ULL ? 1u : 0u;

        FUN_8039cca0();
        if (play) {
            if (hw == 0) {
                FUN_803affbc(*(int *)(rec + 8), cb);
            } else if ((*(uint *)(rec + 0x10) & 0x240) == 0x240) {
                int cbL = audio_linear_to_centibels(
                    (int)((float)((double)FLOAT_806167d8 * distL) / FLOAT_806167cc));
                int cbR = audio_linear_to_centibels(
                    (int)((float)((double)FLOAT_806167d8 * distR) / FLOAT_806167cc));
                FUN_8018b514((u32)*(char *)(rec + 0x41), (u32)cbL, cbR);
            } else {
                FUN_8018b3ac();
            }
        }
        if (hw == 0) {
            FUN_803b0000(*(int *)(rec + 8), pitchIdx);
        } else if ((*(uint *)(rec + 0x10) & 0x240) != 0x240) {
            FUN_8018b448((int)*(char *)(rec + 0x41), pitchIdx);
        }
        FUN_8039ccc8();
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// Address: 0x80189130 | Name: stream_voices_mute_inactive | Date: 2026-09-06
// Walk the 3D active list then the SFX active list; mute each voice whose
// sound-descriptor bank flags (0x800 "keep-playing-while-paused") are clear.
void stream_voices_mute_inactive(void)
{
    int *p;
    for (p = (int *)DAT_805a3bbc; (int *)p != (int *)&DAT_805a3bbc; ) {
        int *next = (int *)*p;
        if ((*(uint *)(DAT_8060fa8c + (uint)*(u16 *)((int)p + 0x38) * 0x14 + 8) & 0x800) == 0) {
            stream_voice_mute((int)p);
        }
        p = next;
    }
    for (p = (int *)DAT_805a3b78; (int *)p != (int *)&DAT_805a3b78; ) {
        int *next = (int *)*p;
        if ((*(uint *)(DAT_8060fa8c + (uint)*(u16 *)((int)p + 0x38) * 0x14 + 8) & 0x800) == 0) {
            stream_voice_mute((int)p);
        }
        p = next;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x801891dc | Name: stream_voices_unmute_all | Date: 2026-09-06
// Walk the 3D active list then the SFX active list, unmuting every voice.
void stream_voices_unmute_all(void)
{
    int *p;
    for (p = (int *)DAT_805a3bbc; (int *)p != (int *)&DAT_805a3bbc; ) {
        int *next = (int *)*p;
        stream_voice_unmute((int)p);
        p = next;
    }
    for (p = (int *)DAT_805a3b78; (int *)p != (int *)&DAT_805a3b78; ) {
        int *next = (int *)*p;
        stream_voice_unmute((int)p);
        p = next;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80189250 | Name: stream_sound_data_load | Date: 2026-09-06
// Master loader for the "sound/sounddata" asset: builds the 0x14-stride
// sound-descriptor table (DAT_8060fa8c) and the 0x44-stride sample table
// (DAT_80612484). Streams the asset in from DVD, parses the name block (per
// entry, rewriting a trailing ".v??" to ".dsp" else ".mss"), byte-swaps each
// descriptor, counts playable banks, allocates the sample table, then loads or
// hash-resolves each referenced bank. Drives the load screen throughout. Gated
// on DAT_80612614 != 0x67 (skips when already built).
void stream_sound_data_load(u32 arg0, double a2, double a3, u32 arg4, u32 arg5, u32 arg6,
                            u32 arg7, u32 arg8)
{
    (void)arg0;

    FUN_803826c8();

    int voiceSlot = 0;   // iVar21 — assigned mixer-slot index
    int sampCur = 0;     // iVar18 — running offset into the sample table
    uint playable = 0;   // uVar20 — number of playable banks
    uint loaded = 0;     // uVar19 — number processed (for progress)

    if (DAT_80612614 == 0x67) {
        FUN_80382714();
        return;
    }

    loadscreen_set_phase(4);
    loadscreen_advance_progress((double)FLOAT_80616798);
    music_stub_nop();

    unsigned int uStack_118;
    char nameBuf[204];   // acStack_114

    byte *raw = (byte *)asset_load_from_dvd(0, a2, a3, arg4, arg5, arg6, arg7, arg8,
                                            s_sound_sounddata_8041e788, 0, &uStack_118,
                                            0, 0, 0, 0, 0);
    byte *cursor = raw + 2;
    uint count = (uint)(u16)((raw[0] << 8) | raw[1]);   // CONCAT11(raw[1], raw[0]) as name count

    loadscreen_advance_progress((double)FLOAT_80616794);
    loadscreen_set_phase(5);
    loadscreen_set_phase(6);
    loadscreen_advance_progress((double)FLOAT_80616798);

    int nameTable = mem_alloc(count * 0xd4 + 0xf & 0xfffffff0, 0);

    // Pass 1: copy + fix-up each entry name into the 0xd4-stride name table.
    int nameOff = 0;
    for (uint i = 0; i < count; i = i + 1) {
        uint len = (uint)*cursor;
        leaf_31e8((uint)nameBuf, (uint)(cursor + 1), (int)len);
        nameBuf[len] = '\0';
        if (nameBuf[len - 3] == 'v') {
            nameBuf[len - 3] = 'd';
            nameBuf[len - 2] = 's';
            nameBuf[len - 1] = 'p';
        } else {
            nameBuf[len - 3] = 'm';
            nameBuf[len - 2] = 's';
            nameBuf[len - 1] = 's';
        }
        FUN_8036412c(0, a2, a3, arg4, arg5, arg6, arg7, arg8, nameBuf,
                     nameTable + nameOff, len, 0, 0, 0, 0, 0);
        cursor = cursor + 1 + len;
        *(u32 *)(nameTable + nameOff + 200) = 0xffffffff;
        nameOff = nameOff + 0xd4;
    }

    // Pass 2: parse the descriptor block into DAT_8060fa8c, counting playable banks.
    int descOff = 0;
    byte *dcur = cursor;
    int di = 0;
    do {
        uint nlen = (uint)*dcur;
        leaf_31e8((uint)(DAT_8060fa8c + descOff + 4), (uint)(dcur + nlen + 3), 2);
        leaf_31e8((uint)(DAT_8060fa8c + descOff + 8), (uint)(dcur + nlen + 5), 4);
        leaf_31e8((uint)(DAT_8060fa8c + descOff + 6), (uint)(dcur + nlen + 9), 2);
        dcur = dcur + nlen + 0xb;
        stream_voice_byteswap_field(DAT_8060fa8c + descOff);
        void *flags = sound_get_bank_flags(di);
        if ((flags != (void *)0) && ((*(uint *)(DAT_8060fa8c + descOff + 8) & 0x240) == 0)) {
            playable = playable + 1;
        }
        di = di + 1;
        descOff = descOff + 0x14;
    } while (di < 0x1647);

    DAT_80612484 = mem_alloc(playable * 0x44 + 0xf & 0xfffffff0, 1);

    // Pass 3: bind each descriptor to its bank (load or hash-resolve).
    di = 0;
    descOff = 0;
    do {
        cursor = cursor + *cursor + 1;
        uint nameIdx = (uint)(u16)((cursor[1] << 8) | cursor[0]);
        cursor = cursor + 10;

        if (nameIdx < count) {
            void *flags = sound_get_bank_flags(di);
            if (flags != (void *)0) {
                loaded = loaded + 1;
                byte *rec = (byte *)(nameTable + nameIdx * 0xd4);
                if (*(int *)(rec + 200) == -1) {
                    if ((*(int *)(DAT_8060fa8c + descOff + 8) & 0x240) == 0) {
                        // Load/decode the bank into the sample-table slot; the
                        // byte count comes back via pSize (stored to the
                        // descriptor's handle slot, mirroring the original).
                        int loadedSize = 0;
                        stream_sound_bank_load(
                            0, a2, a3, arg4, arg5, arg6, arg7, arg8, 0, 0,
                            &loadedSize, 0, 0, 0, 0, 0,
                            (short *)(DAT_80612484 + sampCur), (byte *)rec);
                        *(int *)(DAT_8060fa8c + descOff + 0x10) = loadedSize;
                        sampCur = sampCur + 0x44;
                        *(int *)(DAT_8060fa8c + descOff + 0xc) = voiceSlot;
                        voiceSlot = voiceSlot + 1;
                        *(int *)(rec + 0xd0) = di;
                        rec[200] = 0;
                        rec[0xc9] = 0;
                        rec[0xca] = 0;
                        rec[0xcb] = 0;
                    } else {
                        char *colon = FUN_8038b308((int)rec, ':');
                        byte *hashName = (colon != (char *)0) ? (byte *)(colon + 1) : rec;
                        int idx = pack_find_entry_index_by_hash(DAT_80611880, (int)hashName);
                        *(int *)(DAT_8060fa8c + descOff + 0x10) = idx;
                        *(int *)(DAT_8060fa8c + descOff + 0xc) = 0x7fff;
                        int sz = asset_get_size(0, a2, a3, arg4, arg5, arg6, arg7, arg8, hashName);
                        *(int *)(DAT_8060fa8c + descOff) = sz;
                        *(int *)(rec + 0xd0) = di;
                        rec[200] = 0xff;
                        rec[0xc9] = 0xff;
                        rec[0xca] = 0xff;
                        rec[0xcb] = 0xfb;
                    }
                } else {
                    // Alias an already-loaded bank: copy handle/slot/flags from
                    // the owning descriptor (rec+0xd0 -> descriptor index).
                    int srcOff = *(int *)(rec + 0xd0) * 0x14;
                    *(int *)(DAT_8060fa8c + descOff + 0x10) = *(int *)(DAT_8060fa8c + srcOff + 0x10);
                    *(int *)(DAT_8060fa8c + descOff + 0xc) = *(int *)(DAT_8060fa8c + srcOff + 0xc);
                    *(uint *)(DAT_8060fa8c + descOff + 8) =
                        (*(uint *)(DAT_8060fa8c + descOff + 8) & 0xfffffdbf) |
                        (*(uint *)(DAT_8060fa8c + srcOff + 8) & 0x240);
                }
            }
        }

        if (playable != 0) {
            double num = (double)CONCAT44(0x43300000, loaded ^ 0x80000000u) - DOUBLE_806167a8;
            double den = (double)CONCAT44(0x43300000, playable ^ 0x80000000u) - DOUBLE_806167a8;
            loadscreen_advance_progress((double)((float)num / (float)den));
        }
        di = di + 1;
        descOff = descOff + 0x14;
    } while (di < 0x1647);

    mem_free_top((int)nameTable);
    mem_free_top((int)raw);

    // Original spins a short delay loop (0xd5 iterations).
    for (int spin = 0xd5; spin != 0; spin = spin - 1) {
    }

    if (DAT_80612614 < 0x53) {
        stream_voice_loader_nop();
    }
    loadscreen_advance_progress((double)FLOAT_80616794);
    audio_scratch_reserve();

    FUN_80382714();
}

// ---------------------------------------------------------------------------
// Address: 0x8018966c | Name: stream_samples_to_ticks | Date: 2026-09-06
// Convert a sample count at 48kHz to engine ticks (samples*48000 / divisor).
int stream_samples_to_ticks(int samples)
{
    return (int)((float)((double)CONCAT44(0x43300000, samples * 48000 ^ 0x80000000u) -
                         DOUBLE_806167a8) / FLOAT_806167dc);
}

// ---------------------------------------------------------------------------
// Address: 0x801896b4 | Name: stream_voice_compute_pitch_ratio | Date: 2026-09-06
// Compute a descriptor's pitch ratio: (sampleOffset-0x60) scaled by the
// mono/stereo channel factor (bit 0x200 of the flags word) over the sample-rate
// ticks (from DAT_803ebfd8[byte+4]); clamped above FLOAT_806167e0 by subtracting
// the floor.
double stream_voice_compute_pitch_ratio(int descIndex)
{
    int desc = DAT_8060fa8c + descIndex * 0x14;
    int sampOff = *(int *)desc;
    uint flags = *(uint *)(desc + 8);
    int ticks = stream_samples_to_ticks(
        (int)(uint)*(u16 *)(&DAT_803ebfd8 + (uint)*(byte *)(desc + 4) * 2));

    int chanFactor = 2 - (int)((flags >> 9 & 1) != 0);
    double ratio = (double)((float)((double)CONCAT44(0x43300000,
                                        (sampOff - 0x60) * chanFactor ^ 0x80000000u) -
                                    DOUBLE_806167a8) /
                            (float)((double)CONCAT44(0x43300000, (uint)ticks ^ 0x80000000u) -
                                    DOUBLE_806167a8));
    if ((double)FLOAT_806167e0 < ratio) {
        ratio = (double)(float)(ratio - (double)FLOAT_806167e0);
    }
    return ratio;
}

// ---------------------------------------------------------------------------
// Address: 0x80189760 | Name: stream_voices_update_all | Date: 2026-09-06
// Per-frame driver: walk the 3D active list then the SFX active list. Retire
// finished voices (mixer voice == 0 and no hardware channel: unlink + free);
// otherwise run stream_voice_update and, for looping/streamed voices (flag
// 0x200), accumulate a weighted distance-volume term. Push the accumulated
// term to sound_calc_distance_volume.
void stream_voices_update_all(void)
{
    FUN_803826f0();

    double accum = (double)FLOAT_80616798;
    double weight = (double)FLOAT_806167e4;

    int *p;
    for (p = (int *)DAT_805a3bbc; (int *)p != (int *)&DAT_805a3bbc; ) {
        int *next = (int *)*p;
        if ((p[2] == 0) && ((p[4] & 0x240U) == 0)) {
            stream_list_unlink(p);
            stream_voice_free((u32 *)p);
        } else {
            stream_voice_update();
            if ((*(u16 *)((int)p + 0x3a) & 0x200) != 0) {
                accum = (double)(float)((double)(float)p[5] * weight + accum);
            }
        }
        p = next;
    }

    weight = (double)FLOAT_806167e4;
    for (p = (int *)DAT_805a3b78; (int *)p != (int *)&DAT_805a3b78; ) {
        int *next = (int *)*p;
        if ((p[2] == 0) && ((p[4] & 0x240U) == 0)) {
            stream_list_unlink(p);
            stream_voice_free((u32 *)p);
        } else {
            stream_voice_update();
            if ((*(u16 *)((int)p + 0x3a) & 0x200) != 0) {
                accum = (double)(float)((double)(float)p[5] * weight + accum);
            }
        }
        p = next;
    }

    sound_calc_distance_volume(accum);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Address: 0x80189898 | Name: stream_voice_frame_end_nop | Date: 2026-09-06
// Empty routine (stubbed per-frame end hook).
void stream_voice_frame_end_nop(void)
{
}

// ===========================================================================
// Batch: batch_486 — streaming-voice system init/reset + play-state queries +
//                     position/volume update + mixer-voice release
// ===========================================================================

// ---- Additional raw symbols referenced by the batch_486 functions ----
// (common.h does not pull generated_decls.h; declare every helper per-file.)
extern "C" {
// Intrusive freelist init over the 0x44-stride voice-record pool.
extern void freelist_init(u32 *pList, int stride, u32 *pPool, int count);

// Per-update mixer state push (recompute centibel + pitch from spawn-frame pair).
extern void FUN_80187da8(void);

// DSP streaming-scratch allocator init (reserve 8x 0x40000 blocks).
extern void FUN_8018b124(void);

// Mixer voice-table reset + enable flag.
extern int  FUN_8039eadc(void);      // returns nonzero when audio hw ready (flag bit 4)
extern void FUN_803af050(void);      // clear the 0x40-entry mixer voice table
extern void FUN_803af118(int enable);// set mixer master-enable (DAT_806133e8)
extern void *FUN_803ad930(void *pShutdownCb);  // install voice-shutdown callback, ret old

// Hardware-voice play-state query.
extern int  FUN_8018b37c(int channel);

// Data referenced by batch_486.
extern int  DAT_805a3b68;      // voice-record freelist head (0x44 stride)
extern int  DAT_805a3bc0;      // 3D active-list head sentinel (prev link)
extern int  DAT_805a3b7c;      // SFX active-list head sentinel (prev link)
extern int  DAT_805a3c00;      // voice-record pool base (0x24 records of 0x44)
extern int  DAT_805ac9a0;      // DSP scratch block pointer table (8 entries)
extern u8   DAT_80612491;      // DSP scratch cursor A
extern u8   DAT_80612492;      // DSP scratch cursor B
}

// ---------------------------------------------------------------------------
// Address: 0x8018989c | Name: stream_voice_stop_all_b | Date: 2026-09-06
// Second entry point for "stop every active voice" (identical body to
// stream_voice_stop_all at 0x80188d14; kept as a separate address in the ROM).
void stream_voice_stop_all_b(void)
{
    int *p;
    for (p = (int *)DAT_805a3bbc; (int *)p != (int *)&DAT_805a3bbc; ) {
        int *next = (int *)*p;
        stream_voice_request_stop(p);
        p = next;
    }
    for (p = (int *)DAT_805a3b78; (int *)p != (int *)&DAT_805a3b78; ) {
        int *next = (int *)*p;
        stream_voice_request_stop(p);
        p = next;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80189910 | Name: stream_sound_data_reset | Date: 2026-09-06
// Tear down the current sound-data set: stop every voice, then clear each
// 0x14-stride descriptor's bound-bank slot (+0xc = 0) and handle (+0x10 = -1)
// across all 0x1647 descriptors.
void stream_sound_data_reset(void)
{
    stream_voice_stop_all_b();
    int off = 0;
    for (int i = 0x1647; i != 0; i = i - 1) {
        *(u32 *)(DAT_8060fa8c + off + 0xc) = 0;
        *(u32 *)(DAT_8060fa8c + off + 0x10) = 0xffffffff;
        off = off + 0x14;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80189964 | Name: stream_voice_system_init | Date: 2026-09-06
// One-time streaming-voice subsystem init: reserve the DSP streaming scratch,
// reset the mixer voice table, set the mixer master-enable from the audio-hw
// ready flag, install the voice-shutdown callback, self-link both active-list
// heads (3D at DAT_805a3bbc, SFX at DAT_805a3b78), and build the voice-record
// freelist (0x24 records of 0x44 bytes at DAT_805a3c00).
void stream_voice_system_init(void)
{
    FUN_8018b124();
    FUN_803af050();
    stream_voice_nop();

    if (FUN_8039eadc()) {
        FUN_803af118(1);
    } else {
        FUN_803af118(0);
    }
    FUN_803ad930((void *)&stream_voice_shutdown);

    DAT_805a3bbc = (int)&DAT_805a3bbc;
    DAT_805a3bc0 = (int)&DAT_805a3bbc;
    DAT_805a3b78 = (int)&DAT_805a3b78;
    DAT_805a3b7c = (int)&DAT_805a3b78;

    freelist_init((u32 *)&DAT_805a3b68, 0x44, (u32 *)&DAT_805a3c00, 0x24);
}

// ---------------------------------------------------------------------------
// Address: 0x801899f4 | Name: stream_voice_is_playing | Date: 2026-09-06
// Return 1 if the voice for id is actively playing. Software voices are
// "playing" in state 2 (+0x3e); hardware voices (id flag 0x10000000) defer to
// the mixer channel query.
u32 stream_voice_is_playing(uint id)
{
    int rec = (int)stream_voice_lookup((int)id);
    u32 result = 0;
    if (rec != 0) {
        if ((id & 0x10000000) == 0) {
            if (*(char *)(rec + 0x3e) == '\x02') {
                result = 1;
            }
        } else {
            result = (u32)FUN_8018b37c((int)*(char *)(rec + 0x41));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Address: 0x80189a5c | Name: stream_voice_is_active | Date: 2026-09-06
// Return 1 if the voice for id exists and is not in the stopping state (3);
// 0 otherwise (including a missing voice).
u32 stream_voice_is_active(int id)
{
    int rec = (int)stream_voice_lookup(id);
    u32 result = 0;
    if (rec != 0) {
        result = (*(char *)(rec + 0x3e) == '\x03') ? 0u : 1u;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Address: 0x80189aa4 | Name: stream_voice_set_position | Date: 2026-09-06
// Update a voice's world position (rec[7..9]) and, unless it is a paused 3D
// voice on the SFX list (flag 0x400 with an SFX match), recompute its 3D
// volume/pan and push the new mixer state.
void stream_voice_set_position(int id, u32 *pPos)
{
    int isSfx;
    u32 *rec = (u32 *)stream_voice_find_by_id(id, &isSfx);
    if (rec != (u32 *)0) {
        rec[7] = pPos[0];
        rec[8] = pPos[1];
        rec[9] = pPos[2];
        if (((rec[4] & 0x400) == 0) || (isSfx == 0)) {
            u32 scratch[3];
            stream_voice_compute_3d((u32)(uintptr_t)rec, (u32)isSfx, scratch);
            FUN_80187da8();
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80189b34 | Name: stream_voice_set_pos_and_volume | Date: 2026-09-06
// Update a voice's optional world position (rec[7..9], when pPos != NULL) and
// its base volume (rec[5]); recompute 3D + push mixer state under the same
// 0x400/SFX gate as stream_voice_set_position.
void stream_voice_set_pos_and_volume(double vol, int id, u32 *pPos)
{
    int isSfx;
    u32 *rec = (u32 *)stream_voice_find_by_id(id, &isSfx);
    if (rec != (u32 *)0) {
        if (pPos != (u32 *)0) {
            rec[7] = pPos[0];
            rec[8] = pPos[1];
            rec[9] = pPos[2];
        }
        *(float *)&rec[5] = (float)vol;
        if (((rec[4] & 0x400) == 0) || (isSfx == 0)) {
            u32 scratch[3];
            stream_voice_compute_3d((u32)(uintptr_t)rec, (u32)isSfx, scratch);
            FUN_80187da8();
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x80189be4 | Name: stream_voice_stop_all_wrapper | Date: 2026-09-06
// Thin wrapper forwarding to stream_voice_stop_all.
void stream_voice_stop_all_wrapper(void)
{
    stream_voice_stop_all();
}

// ---------------------------------------------------------------------------
// Address: 0x80189c04 | Name: sound_desc_is_loaded | Date: 2026-09-06
// Return 1 if the descriptor's bound-bank handle (+0xc) is non-negative
// (loaded), 0 if its sign bit is set (unbound/placeholder).
u32 sound_desc_is_loaded(int descIndex)
{
    return (*(u32 *)(DAT_8060fa8c + descIndex * 0x14 + 0xc) >> 0x1f) ^ 1u;
}

// ---------------------------------------------------------------------------
// Address: 0x80189c20 | Name: stream_voice_release_mixer | Date: 2026-09-06
// Release the mixer voice bound to a record. The low bit of rec+0x14 selects
// which of the two channel-voice slots (+0x1c or +0x20, off the masked base) to
// free; the freed slot pointer is nulled.
void stream_voice_release_mixer(int rec)
{
    uint base = *(uint *)(rec + 0x14) & 0xfffffffe;
    if ((*(uint *)(rec + 0x14) & 1) == 0) {
        FUN_803aff9c(*(int *)(base + 0x1c));
        *(u32 *)(base + 0x1c) = 0;
    } else {
        FUN_803aff9c(*(int *)(base + 0x20));
        *(u32 *)(base + 0x20) = 0;
    }
}
