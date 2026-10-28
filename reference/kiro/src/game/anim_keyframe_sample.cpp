// =============================================================================
// anim_keyframe_sample.cpp - Animation keyframe channel decode + bone-matrix commit
// Address range: 0x800241d8 - 0x80025744
// Batch: 391 | Date: 2026-09-05
//
// The keyframe-decompression core of the animation pipeline (used by every
// pose sample from batches 385-390). anim_sample_keyframe (FUN_80024800) is the
// universal channel decoder: given a clip channel record and a key index it
// unpacks the stored pose (position + quaternion) for that key, handling every
// on-disk channel layout (types 0/2/4/8/0xa/0xb/0xc/0xd) including the 10-bit
// quantized-quaternion "smallest-three" encoding.
//
//   * anim_clear_render_scale      (0x800241d8) restore render squash-scale (inverse of set) (FUN_800241d8)
//   * anim_sample_keyframe_packed  (0x80024278) sample a key from the packed/shared layout (FUN_80024278)
//   * anim_pose_query_scaled       (0x800246e8) pose query + position scale (FUN_800246e8)
//   * anim_sample_pose_raw         (0x8002474c) fetch a raw 8-float pose or fall to packed (FUN_8002474c)
//   * anim_sample_keyframe         (0x80024800) universal keyframe channel decoder (FUN_80024800)
//   * anim_clip_total_translation  (0x800254d0) end-minus-start root translation of a clip (FUN_800254d0)
//   * anim_clip_endpoint_pose      (0x800255c4) fetch a clip start/end endpoint pose (FUN_800255c4)
//   * anim_commit_bone_matrix_slot (0x8002570c) commit a bone matrix by slot index (FUN_8002570c)
//   * anim_set_bone_matrix_base    (0x8002573c) set the bone-matrix table base pointer (FUN_8002573c)
//   * anim_write_bone_matrix       (0x80025744) write/blend a bone matrix into the ring (FUN_80025744)
//
// Compile-clean non-matching reconstruction. Quantized-int-to-float uses the
// classic PPC (0x43300000<<32 | bits) magic-double bias trick, preserved here so
// the numeric result matches the hardware.
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // anim_keyframe_pose_query, math_sqrt, halt_stub_assert, leaf_31e8

// ---- raw runtime globals ---------------------------------------------------
extern int    DAT_80611824;    // animation descriptor table base (idx*4)
extern short  DAT_804d1cd0;    // anim id -> descriptor index table (short)
extern int    DAT_80611d78;    // render-instance table base (idx * 0x141c)
extern int    DAT_80612610;    // render-instance count
extern int    DAT_80611828;    // bone-matrix table base pointer
extern int    DAT_806117d0;    // bone-matrix ring write cursor
extern int    DAT_806117d4;    // bone-matrix ring half-flag
extern int    DAT_806117f8;    // packed-pose data arena base
extern int    DAT_804d5bf8;    // channel -> descriptor index table
extern int    DAT_803f2484;    // quat "smallest-three" component index table (+0)
extern int    DAT_803f2488;    // quat component index table (+4)
extern int    DAT_803f248c;    // quat component index table (+8)

// ---- numeric constants -----------------------------------------------------
extern float  FLOAT_80613930;  // 0.0
extern float  FLOAT_80613944;  // 1.0
extern float  FLOAT_80613960;  // position scale (pose-query path)
extern double DOUBLE_80613938; // uint->double magic bias (time)
extern double DOUBLE_806139b8; // uint->double magic bias (quantized components)
extern float  FLOAT_806139a0;  // quat w reconstruction epsilon
extern float  FLOAT_806139b0;  // 16-bit component scale
extern float  FLOAT_806139c0;  // 16-bit position scale
extern float  FLOAT_806139c4;  // 16-bit position bias
extern float  FLOAT_806139c8;  // 10-bit quat scale
extern float  FLOAT_806139cc;  // 10-bit quat normalizer
extern float  FLOAT_806139d0;  // 10-bit quat bias
extern float  FLOAT_806139a4;  // render scale X factor
extern float  FLOAT_806139a8;  // render scale Z factor
extern float  FLOAT_806139ac;  // render scale W floor

// bone-matrix ring buffer base (0x804e52a0, seen as -0x7fb1ad60 signed)
#define BONE_MTX_RING_BASE ((int)0x804e52a0)

// ---- raw runtime helpers (self-extern at call-site arity) ------------------
extern "C" {
    unsigned long long FUN_803826e8(void);      // scratch: packed (record<<32 | keyIdx)
    void   FUN_80382734(void);                   // scratch free (e8)
    int    FUN_80027f9c(unsigned int key, int channel);  // compressed frame byte offset
    void   FUN_803a14c4(float* dst, float* src);         // matrix copy (4x4)
    void   FUN_803a1508(float* dst, float* a, float* b); // matrix blend/slerp
    void   FUN_80024278(int record, int keyIdx, unsigned int key, float* out);  // packed sampler
    void   FUN_800246e8(int a, int b, unsigned int key, float* out);            // pose-query path
    // forward decls for same-file entry points used before definition
    void   FUN_80024800(int animTbl, int bone, unsigned int key, float* out);
    void   FUN_80025744(float* mtx, unsigned int mode);
}

// ---------------------------------------------------------------------------
// anim_clear_render_scale (0x800241d8)  [FUN_800241d8]
// Inverse of anim_set_render_scale: clear the squash-scale flag (bit 0x20) and
// restore the "resting" scale from +0xb0c into both +0xb08 and the live
// render-instance table entry, keeping the W component clamped to a floor.
// ---------------------------------------------------------------------------
extern "C" void FUN_800241d8(int rec)
{
    *(unsigned int*)(rec + 0x28) &= 0xffffffdfu;
    *(unsigned int*)(rec + 0xb08) = *(unsigned int*)(rec + 0xb0c);

    float wFloor = FLOAT_806139ac;
    float zFactor = FLOAT_806139a8;
    if (DAT_80612610 <= *(int*)(rec + 8))
        return;

    int inst = DAT_80611d78 + *(int*)(rec + 8) * 0x141c;
    *(float*)(inst + 0x80) = FLOAT_806139a4 * *(float*)(rec + 0xb0c);
    *(unsigned int*)(inst + 0x84) = *(unsigned int*)(rec + 0xb0c);
    *(float*)(inst + 0x8c) = zFactor * *(float*)(rec + 0xb0c);
    if (wFloor <= *(float*)(inst + 0x90))
        return;
    *(float*)(inst + 0x90) = wFloor;
}

// ---------------------------------------------------------------------------
// anim_pose_query_scaled (0x800246e8)  [FUN_800246e8]
// Thin wrapper over the shared pose-query helper: reads position (out[0..2]),
// rotation (out[3..6]) and scale (out[7]) for a key, then applies the global
// position scale to the translation channel.
// ---------------------------------------------------------------------------
extern "C" void FUN_800246e8(int a, int b, unsigned int key, float* out)
{
    (void)a; (void)b;
    anim_keyframe_pose_query((void*)(unsigned int)key, (void*)(out + 3),
                             (int*)out, (int*)(out + 7));
    float s = FLOAT_80613960;
    out[0] *= s;
    out[1] *= s;
    out[2] *= s;
}

// ---------------------------------------------------------------------------
// anim_sample_pose_raw (0x8002474c)  [FUN_8002474c]
// Fetch a full 8-float pose (pos+quat+scale) straight from an uncompressed
// channel record, clamping the key index to the channel length. Records whose
// flag bit 1 is set use the packed layout, so delegate to the packed sampler.
// ---------------------------------------------------------------------------
extern "C" void FUN_8002474c(int record, int keyIdx, unsigned int key, float* out)
{
    unsigned int count = *(unsigned int*)(record + 0x10);
    // clamp key to [0, count-1] via the original branchless test
    if (-1 < (int)(((int)((count ^ key) >> 1)) - (int)((count ^ key) & count)))
        key = count - 1;

    if ((*(unsigned int*)(record + 8) & 2) == 0) {
        int base = *(int*)(*(int*)(record + 0x28) + keyIdx * 0x20 + 0x14);
        float* p = (float*)(base + key * 0x20 & -(int)(base != 0));
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
        out[4] = p[4]; out[5] = p[5]; out[6] = p[6]; out[7] = p[7];
    } else {
        FUN_800246e8(record, keyIdx, key, out);   // packed path (shares pose-query)
    }
}

// helper: dequantize a 16-bit component to float using the magic-double bias
static inline float deqQ16(unsigned int bits, float scale, float bias)
{
    return scale * (float)((double)CONCAT44(0x43300000, bits) - DOUBLE_806139b8) - bias;
}

// helper: copy `len` bytes with a leading 2-byte alignment fixup (mirrors the
// original half-word-then-leaf_31e8 memcpy prologue)
static inline void alignedCopy(void* dst, void* src, int len)
{
    unsigned char* s = (unsigned char*)src;
    unsigned char* d = (unsigned char*)dst;
    if (((unsigned int)s & 2) != 0) {
        *(unsigned short*)d = *(unsigned short*)s;
        d += 2; s += 2; len -= 2;
    }
    leaf_31e8((unsigned int)d, (unsigned int)s, len);
}

// ---------------------------------------------------------------------------
// anim_sample_keyframe (0x80024800)  [FUN_80024800]
// Universal keyframe channel decoder. Fetches the (record,keyIdx) pair from the
// scratch getter, clamps the key index, then dispatches on the channel's stored
// layout type to unpack the pose into `out` (out[0..2] = position,
// out[3..6] = quaternion, out[6] often the reconstructed w). Handles:
//   0/2/4/8 : half-word / float component layouts, optionally interleaved
//   0xa     : plain 7-float copy
//   0xb/c/d : 10-bit "smallest-three" quantized quaternion (+ 16-bit position)
// Records with flag bit 1 use the packed arena -> delegate to the packed sampler.
// ---------------------------------------------------------------------------
extern "C" void FUN_80024800(int animTbl, int bone, unsigned int key, float* out)
{
    (void)animTbl; (void)bone;

    unsigned long long packed = FUN_803826e8();
    int recBase = (int)(packed >> 0x20);
    int keyIdx  = (int)packed;

    // The original pulls an accumulated position scale from f31; on this build
    // that scale is 1.0 at entry, applied to the final translation below.
    double posScale = 1.0;

    unsigned int recFlags = *(unsigned int*)(recBase + 8);
    if ((recFlags & 2) != 0) {
        FUN_80024278(recBase, keyIdx, key, out);
        FUN_80382734();
        return;
    }

    unsigned int count = *(unsigned int*)(recBase + 0x10);
    unsigned int wide   = (recFlags >> 4) & 1;   // 1 => 4-component (w stored)
    unsigned int packed16 = recFlags & 1;        // 1 => 16-bit quantized comps
    if (-1 < (int)(((int)((count ^ key) >> 1)) - (int)((count ^ key) & count)))
        key = count - 1;

    int chan = *(int*)(recBase + 0x28) + keyIdx * 0x20;
    int type = *(int*)(chan + 4);
    unsigned int normalizeW = 0;   // when set, reconstruct out[6] from xyz below

    switch (type) {
    case 0:
    case 4: {
        // types 0 and 4 share the "key 0" fast path; type 4 for key!=0 differs
        if (type == 4 && key != 0)
            goto type4_indexed;
        if (packed16 == 0) {
            void* src = *(void**)(chan + 0x14);
            int len = wide ? 0x18 : 0x1c;
            alignedCopy(out, src, len);
        } else {
            void* src = *(void**)(chan + 0x14);
            unsigned short* q = (unsigned short*)((unsigned int)((char*)src + 12)
                                & -(int)(src != nullptr));
            alignedCopy(out, src, 0xc);
            out[3] = deqQ16(q[0], FLOAT_806139b0, FLOAT_80613944);
            out[4] = deqQ16(q[1], FLOAT_806139b0, FLOAT_80613944);
            out[5] = deqQ16(q[2], FLOAT_806139b0, FLOAT_80613944);
            if (wide == 0)
                out[6] = deqQ16(q[3], FLOAT_806139b0, FLOAT_80613944);
        }
        break;

    type4_indexed:
        if (packed16 == 0) {
            int cb = *(int*)(chan + 0x14);
            // rotation block for this key
            void* rsrc = (void*)((unsigned int)(cb + 0xc) & -(int)(cb != 0));
            alignedCopy(out + 3, rsrc, (4 - (int)wide) * 4);
            // position block
            void* psrc = (void*)((unsigned int)(cb + ((4 - (int)wide) + (int)key * 3) * 4)
                                & -(int)(cb != 0));
            alignedCopy(out, psrc, 0xc);
        } else {
            int cb = *(int*)(chan + 0x14);
            unsigned short* q = (unsigned short*)((unsigned int)(cb + 0xc) & -(int)(cb != 0));
            out[3] = deqQ16(q[0], FLOAT_806139b0, FLOAT_80613944);
            out[4] = deqQ16(q[1], FLOAT_806139b0, FLOAT_80613944);
            out[5] = deqQ16(q[2], FLOAT_806139b0, FLOAT_80613944);
            if (wide == 0)
                out[6] = deqQ16(q[3], FLOAT_806139b0, FLOAT_80613944);
            void* psrc = (void*)((unsigned int)(cb + (4 - (int)wide) * 2 + (int)key * 0xc)
                                & -(int)(cb != 0));
            alignedCopy(out, psrc, 0xc);
        }
        break;
    }

    case 2: {
        void* rsrc = *(void**)(chan + 0x14);
        alignedCopy(out, rsrc, 0xc);
        if (packed16 == 0) {
            int cb = *(int*)(chan + 0x14);
            void* psrc = (cb == 0) ? nullptr : (void*)(cb + (int)key * 0xc);
            alignedCopy(out + 3, psrc, wide ? 0xc : 0x10);
        } else {
            int cb = *(int*)(chan + 0x14);
            unsigned short* q = (cb == 0) ? nullptr
                : (unsigned short*)((4 - (int)wide) * (int)key * 2 + cb + 0xc);
            out[3] = deqQ16(q[0], FLOAT_806139b0, FLOAT_80613944);
            out[4] = deqQ16(q[1], FLOAT_806139b0, FLOAT_80613944);
            out[5] = deqQ16(q[2], FLOAT_806139b0, FLOAT_80613944);
            if (wide == 0)
                out[6] = deqQ16(q[3], FLOAT_806139b0, FLOAT_80613944);
        }
        break;
    }

    case 8: {
        int cb = *(int*)(chan + 0x14);
        if (packed16 == 0) {
            int stride = (7 - (int)wide) * 4;
            void* src = (void*)((unsigned int)(cb + (int)key * stride) & -(int)(cb != 0));
            alignedCopy(out, src, stride);
        } else {
            int off = (int)key * ((4 - (int)wide) * 2 + 0xc);
            void* psrc = (void*)((unsigned int)(cb + off) & -(int)(cb != 0));
            alignedCopy(out, psrc, 0xc);
            unsigned short* q = (unsigned short*)((unsigned int)(off + cb + 0xc)
                                & -(int)(cb != 0));
            out[3] = deqQ16(q[0], FLOAT_806139b0, FLOAT_80613944);
            out[4] = deqQ16(q[1], FLOAT_806139b0, FLOAT_80613944);
            out[5] = deqQ16(q[2], FLOAT_806139b0, FLOAT_80613944);
            if (wide == 0)
                out[6] = deqQ16(q[3], FLOAT_806139b0, FLOAT_80613944);
        }
        break;
    }

    case 0xa: {
        int off = (int)key * 0x20;
        int cb = *(int*)(chan + 0x14);
        for (int i = 0; i < 7; ++i)
            out[i] = *(float*)(cb + off + i * 4);
        break;
    }

    case 0xb:
    case 0xc:
    case 0xd: {
        // 16-bit quantized position + 10-bit "smallest-three" quaternion
        unsigned short* base;
        int cb = *(int*)(chan + 0x14);
        if (type == 0xb)
            base = (unsigned short*)((unsigned int)(cb + (int)key * 10) & -(int)(cb != 0));
        else if (type == 0xc)
            base = (unsigned short*)((unsigned int)(cb + ((int)key * 2 + 3) * 2) & -(int)(cb != 0));
        else /* 0xd */
            base = (unsigned short*)((unsigned int)(cb + ((int)key * 3 + 2) * 2) & -(int)(cb != 0));

        out[0] = deqQ16(base[0], FLOAT_806139c0, FLOAT_806139c4);
        out[1] = deqQ16(base[1], FLOAT_806139c0, FLOAT_806139c4);
        out[2] = deqQ16(base[2], FLOAT_806139c0, FLOAT_806139c4);

        // read the 32-bit packed quaternion word (aligned copy of the tail)
        unsigned int qword = 0;
        {
            unsigned short* q = base + 3;
            unsigned char* d = (unsigned char*)&qword;
            unsigned char* s = (unsigned char*)q;
            int len = 4;
            if (((unsigned int)q & 2) != 0) {
                *(unsigned short*)d = *(unsigned short*)s;
                d += 2; s += 2; len -= 2; q += 1;
            }
            leaf_31e8((unsigned int)d, (unsigned int)s, len);
        }

        // decode: low 2 bits pick the dropped (largest) component index; the
        // remaining three 10-bit fields are the other components.
        unsigned int drop = qword & 3;
        int idxTbl = (int)drop * 0xc;
        int shift = 0;
        for (int i = 0; i < 3; ++i) {
            int comp = *(int*)((int)(&DAT_803f2484 + idxTbl) + i * 4);
            unsigned int field = (qword >> (shift + 2)) & 0x3ff;
            shift += 10;
            out[comp + 3] = (FLOAT_806139c8 * (float)((double)CONCAT44(0x43300000, field)
                            - DOUBLE_806139b8)) / FLOAT_806139cc - FLOAT_806139d0;
        }
        // reconstruct the dropped component from unit-length constraint
        float a = out[*(int*)(&DAT_803f2484 + idxTbl) + 3];
        float b = out[*(int*)(&DAT_803f2488 + idxTbl) + 3];
        float c = out[*(int*)(&DAT_803f248c + idxTbl) + 3];
        double w = math_sqrt((double)(FLOAT_80613944 - (a * a + b * b + c * c)));
        out[drop + 3] = (float)w;
        break;
    }
    default:
        halt_stub_assert();
        break;
    }

    if (normalizeW != 0) {
        double d = (double)(FLOAT_80613944 - (out[3] * out[3] + out[4] * out[4] + out[5] * out[5]));
        if (d <= (double)FLOAT_806139a0)
            d = (double)FLOAT_80613930;
        else
            d = math_sqrt(d);
        out[6] = (float)d;
    }

    out[0] = (float)((double)out[0] * posScale);
    out[1] = (float)((double)out[1] * posScale);
    out[2] = (float)((double)out[2] * posScale);

    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_sample_keyframe_packed (0x80024278)  [FUN_80024278]
// Sampler for channels stored in the shared "packed" arena (flag bit 1 set).
// Locates the channel's base pose in the arena, and when a non-zero key is
// requested reads the delta block for that key (layout selected by the channel
// sub-type 2/4/8) merging position and rotation from the appropriate source.
// ---------------------------------------------------------------------------
extern "C" void FUN_80024278(int record, int keyIdx, unsigned int key, float* out)
{
    (void)record; (void)keyIdx;

    unsigned long long packed = FUN_803826e8();
    int recBase = (int)(packed >> 0x20);
    int kIdx    = (int)packed;

    unsigned int count = *(unsigned int*)(recBase + 0x10);
    int chanId = **(int**)(recBase + 0x20);
    unsigned int wide = *(unsigned int*)(recBase + 8) & 1;
    if (-1 < (int)(((int)((count ^ key) >> 1)) - (int)((count ^ key) & count)))
        key = count - 1;

    int chan = *(int*)(recBase + 0x28) + kIdx * 0x20;
    int stride = (wide == 0) ? kIdx * 0x1c : kIdx * 0x14;
    float* basePose = (float*)(DAT_806117f8 + (*(int**)(recBase + 0x20))[1]
                       + *(int*)((&DAT_804d5bf8)[chanId] + 0x18) * 0x18 + stride);

    double posScale = 1.0;

    if (key != 0) {
        int off = FUN_80027f9c(key, chanId);
        float* delta = (float*)(off + *(int*)(chan + 0x14));
        int sub = *(int*)(chan + 4);
        if (sub != 0) {
            if (sub == 4) {
                out[0] = delta[0]; out[1] = delta[1]; out[2] = delta[2];
                if (wide == 0) {
                    out[3] = basePose[3]; out[4] = basePose[4];
                    out[5] = basePose[5]; out[6] = basePose[6];
                } else {
                    unsigned short* q = (unsigned short*)(basePose + 3);
                    out[3] = deqQ16(q[0], FLOAT_806139b0, FLOAT_80613944);
                    out[4] = deqQ16(q[1], FLOAT_806139b0, FLOAT_80613944);
                    out[5] = deqQ16(q[2], FLOAT_806139b0, FLOAT_80613944);
                    out[6] = deqQ16(q[3], FLOAT_806139b0, FLOAT_80613944);
                }
                goto scale;
            } else if (sub == 2) {
                out[0] = basePose[0]; out[1] = basePose[1]; out[2] = basePose[2];
                if (wide == 0) {
                    out[3] = delta[0]; out[4] = delta[1];
                    out[5] = delta[2]; out[6] = delta[3];
                } else {
                    unsigned short* q = (unsigned short*)delta;
                    out[3] = deqQ16(q[0], FLOAT_806139b0, FLOAT_80613944);
                    out[4] = deqQ16(q[1], FLOAT_806139b0, FLOAT_80613944);
                    out[5] = deqQ16(q[2], FLOAT_806139b0, FLOAT_80613944);
                    out[6] = deqQ16(q[3], FLOAT_806139b0, FLOAT_80613944);
                }
                goto scale;
            } else if (sub == 8) {
                out[0] = delta[0]; out[1] = delta[1]; out[2] = delta[2];
                if (wide == 0) {
                    out[3] = delta[3]; out[4] = delta[4];
                    out[5] = delta[5]; out[6] = delta[6];
                } else {
                    unsigned short* q = (unsigned short*)(delta + 3);
                    out[3] = deqQ16(q[0], FLOAT_806139b0, FLOAT_80613944);
                    out[4] = deqQ16(q[1], FLOAT_806139b0, FLOAT_80613944);
                    out[5] = deqQ16(q[2], FLOAT_806139b0, FLOAT_80613944);
                    out[6] = deqQ16(q[3], FLOAT_806139b0, FLOAT_80613944);
                }
                goto scale;
            }
            halt_stub_assert();
            goto scale;
        }
    }

    // key 0 (or channel with no sub-type): use the base pose directly
    out[0] = basePose[0]; out[1] = basePose[1]; out[2] = basePose[2];
    if (wide == 0) {
        out[3] = basePose[3]; out[4] = basePose[4];
        out[5] = basePose[5]; out[6] = basePose[6];
    } else {
        unsigned short* q = (unsigned short*)(basePose + 3);
        out[3] = deqQ16(q[0], FLOAT_806139b0, FLOAT_80613944);
        out[4] = deqQ16(q[1], FLOAT_806139b0, FLOAT_80613944);
        out[5] = deqQ16(q[2], FLOAT_806139b0, FLOAT_80613944);
        out[6] = deqQ16(q[3], FLOAT_806139b0, FLOAT_80613944);
    }

scale:
    out[0] = (float)((double)out[0] * posScale);
    out[1] = (float)((double)out[1] * posScale);
    out[2] = (float)((double)out[2] * posScale);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_clip_total_translation (0x800254d0)  [FUN_800254d0]
// Compute a clip's net root translation: sample the first and last keyframe of
// the clip's root channel and return (last - first) into out[0..2].
// ---------------------------------------------------------------------------
extern "C" void FUN_800254d0(int a, int animId, float* out)
{
    (void)a;
    short desc = (&DAT_804d1cd0)[animId];
    float first[16];
    float last[16];
    FUN_80024800(*(int*)(DAT_80611824 + desc * 4), 0, 0, first);
    int tbl = *(int*)(DAT_80611824 + desc * 4);
    FUN_80024800(tbl, 0, *(unsigned int*)(tbl + 0x10) - 1, last);
    out[0] = last[0] - first[0];
    out[1] = last[1] - first[1];
    out[2] = last[2] - first[2];
}

// ---------------------------------------------------------------------------
// anim_clip_endpoint_pose (0x800255c4)  [FUN_800255c4]
// Fetch the position component of a clip's start (mode 0), end-of-first (mode 1)
// or mid (mode 2) endpoint pose into outA/outB, and return the clip's stored
// end-time (dequantized from the channel header +0xc) in outTime.
// ---------------------------------------------------------------------------
extern "C" void FUN_800255c4(int a, int b, float* outA, float* outTime, int mode)
{
    (void)a; (void)b;

    unsigned long long packed = FUN_803826e8();
    float* recPtr = (float*)(unsigned int)packed;
    int recHi = (int)(packed >> 0x20);
    int tbl = *(int*)(DAT_80611824 + *(int*)(*(int*)(recHi + 4) + 0xd0) * 4);

    float startPose[16];
    float endPose[16];
    FUN_80024800(tbl, 0, 0, startPose);
    FUN_80024800(tbl, 0, *(unsigned int*)(tbl + 0x10) - 1, endPose);

    if (mode == 1) {
        recPtr[0] = startPose[1];
        outA[0]   = endPose[1];
    } else if (mode < 1) {
        if (-1 < mode) {
            recPtr[0] = startPose[0];
            outA[0]   = endPose[0];
        }
    } else if (mode < 3) {
        recPtr[0] = startPose[2];
        outA[0]   = endPose[2];
    }

    unsigned int tbits = *(unsigned int*)(tbl + 0xc) ^ 0x80000000u;
    outTime[0] = (float)((double)CONCAT44(0x43300000, tbits) - DOUBLE_80613938);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_set_bone_matrix_base (0x8002573c)  [FUN_8002573c]
// Set the base pointer of the per-frame bone-matrix table.
// ---------------------------------------------------------------------------
extern "C" void FUN_8002573c(int base)
{
    DAT_80611828 = base;
}

// ---------------------------------------------------------------------------
// anim_write_bone_matrix (0x80025744)  [FUN_80025744]
// Write a bone matrix into the ring buffer. `mode` bit 1 selects a blend of the
// current+next ring slot (FUN_803a1508), bit 0 advances the ring cursor. Plain
// writes copy directly (FUN_803a14c4).
// ---------------------------------------------------------------------------
extern "C" void FUN_80025744(float* mtx, unsigned int mode)
{
    if ((mode & 2) == 0) {
        if ((mode & 1) == 0) {
            FUN_803a14c4(mtx, (float*)(DAT_806117d0 * 0x40 + BONE_MTX_RING_BASE));
        } else {
            FUN_803a14c4(mtx, (float*)((DAT_806117d0 + 1) * 0x40 + BONE_MTX_RING_BASE));
            DAT_806117d4 = 0;
            DAT_806117d0 = DAT_806117d0 + 1;
        }
    } else if ((mode & 1) == 0) {
        FUN_803a1508(mtx, (float*)(DAT_806117d0 * 0x40 + BONE_MTX_RING_BASE),
                     (float*)((DAT_806117d0 + 1) * 0x40 + BONE_MTX_RING_BASE));
        DAT_806117d4 = 1;
    } else {
        FUN_803a1508(mtx, (float*)(DAT_806117d0 * 0x40 + BONE_MTX_RING_BASE),
                     (float*)((DAT_806117d0 + 1) * 0x40 + BONE_MTX_RING_BASE));
        DAT_806117d4 = 0;
        DAT_806117d0 = DAT_806117d0 + 1;
    }
}

// ---------------------------------------------------------------------------
// anim_commit_bone_matrix_slot (0x8002570c)  [FUN_8002570c]
// Commit (blend, mode 2) the bone matrix for the given slot index from the
// bone-matrix table into the ring buffer.
// ---------------------------------------------------------------------------
extern "C" void FUN_8002570c(int slot)
{
    FUN_80025744((float*)(DAT_80611828 + slot * 0x40), 2);
}
