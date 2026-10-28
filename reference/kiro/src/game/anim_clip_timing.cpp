// =============================================================================
// anim_clip_timing.cpp - Anim clip speed/curve accessors + per-frame timescale
// Address range: 0x80026bcc - 0x80026ea8
// Batch: 395 | Date: 2026-09-05
//
// Continuation of the animation pipeline (batches 385-394). This module holds
// the small clip playback-speed / curve-value accessors, the "is this the active
// clip" predicate, the slot speed clamp/setter, and the two per-frame passes that
// (a) push the slow-motion timescale onto every eligible actor's blend slots and
// (b) commit the sampled pose into the skeleton matrices.
//
//   * anim_clip_field_b08          (0x80026bcc) clip curve/speed field A (class 8:+0xb08 / 0x1000:+0x40) (FUN_80026bcc)
//   * anim_clip_field_b10          (0x80026c10) clip curve/speed field B (class 8:+0xb10 / 0x1000:+0x44) (FUN_80026c10)
//   * anim_slot_is_active_clip     (0x80026c54) is the given anim id the active blend slot? (FUN_80026c54)
//   * anim_active_curve_value      (0x80026c9c) sample the active clip's curve value (or base) (FUN_80026c9c)
//   * anim_slot_get_speed          (0x80026ce8) slot playback speed (+8) or base 0.0 (FUN_80026ce8)
//   * anim_slot_set_speed          (0x80026d00) clamp+store a slot's playback speed (+8/+0xc) (FUN_80026d00)
//   * anim_active_slot_get_speed   (0x80026d50) active-slot wrapper over get_speed (FUN_80026d50)
//   * anim_active_slot_set_speed   (0x80026d74) active-slot wrapper over set_speed (FUN_80026d74)
//   * anim_apply_timescale_all     (0x80026da8) per-frame: push slow-mo timescale onto actor slots (FUN_80026da8)
//   * anim_commit_pose_matrices    (0x80026ea8) per-frame: sample pose + build skeleton matrices (FUN_80026ea8)
//
// Compile-clean non-matching reconstruction. Named cross-module helpers defined
// in prior batches are reached by their FUN_ address-symbol (that is how those
// TUs export them); the anim-state accessors keep the FUN_ signatures documented
// in anim_state_query.cpp / anim_playback.cpp / anim_descriptor.cpp.
// =============================================================================
#include "common.h"
#include "generated_decls.h"

// ---- anim-state accessors defined in prior batches (by FUN_ symbol) --------
extern "C" {
    int    FUN_80025868(int rec);                 // anim_active_clip_slot -> active slot addr (batch 392)
    double FUN_800233f8(double t, int rec);       // anim_sample_curve_value (batch 390)
    void   FUN_80022c14(int* model, int boneIdx, int mode);              // skel_concat_slerp (batch 388)
    void   FUN_80022a38(int* model, int mtxBase, int boneIdx, int par);  // skel_concat_matrices_a (batch 388)
    void   FUN_80022b1c(int* model, int mtxBase, int boneIdx, int par);  // skel_concat_matrices_b (batch 388)

    // this-batch entry points referenced by each other / by other TUs
    double FUN_80026ce8(int slot);
    void   FUN_80026d00(double speed, int slot);
}

// ---- raw runtime helpers (kept raw: not yet decompiled) --------------------
extern "C" {
    int  FUN_801a8608(int renderInst);   // secondary-render-instance active? (returns nonzero)
    void FUN_80026fa8(int rec, int primSlot, unsigned int* dstIdx, int outBase,
                      int startBone, int endBone);   // pose sampler (next batch)
}

// ---- raw runtime globals ---------------------------------------------------
extern int   DAT_80540a44;     // head of the live actor linked list
extern int   DAT_80611c50;     // current-actor cursor (walk state)
extern int   DAT_80611824;     // animation descriptor/instance table base (idx*4)
extern int   DAT_80611d78;     // render-instance table base (idx * 0x141c)

extern float FLOAT_80613930;   // 0.0 (base speed)
extern float FLOAT_80613944;   // 1.0 (default clip value)
extern float FLOAT_806118ec;   // slow-motion timescale divisor/factor

// model draw callbacks used only for identity comparison (declared void in decls)
// model_pose_eval / model_render_with_override_a resolve via generated_decls.

// -----------------------------------------------------------------------------
// anim_clip_field_b08 (0x80026bcc)
// Read curve/speed field A from a clip record: class-8 (AI) records store it in
// the weapon/audio block (+0xb08), class-0x1000 records in their def (+0x40).
// Returns the 1.0 default for null or other classes.
// -----------------------------------------------------------------------------
extern "C" double FUN_80026bcc(int rec)
{
    if (rec == 0)
        return (double)FLOAT_80613944;
    if (*(int*)(rec + 0x20) == 8)
        return (double)*(float*)(*(int*)(rec + 0x7c) + 0xb08);
    if (*(int*)(rec + 0x20) == 0x1000)
        return (double)*(float*)(*(int*)(rec + 0x7c) + 0x40);
    return (double)FLOAT_80613944;
}

// -----------------------------------------------------------------------------
// anim_clip_field_b10 (0x80026c10)
// Companion accessor for curve/speed field B (class 8:+0xb10 / 0x1000:+0x44).
// -----------------------------------------------------------------------------
extern "C" double FUN_80026c10(int rec)
{
    if (rec == 0)
        return (double)FLOAT_80613944;
    if (*(int*)(rec + 0x20) == 8)
        return (double)*(float*)(*(int*)(rec + 0x7c) + 0xb10);
    if (*(int*)(rec + 0x20) == 0x1000)
        return (double)*(float*)(*(int*)(rec + 0x7c) + 0x44);
    return (double)FLOAT_80613944;
}

// -----------------------------------------------------------------------------
// anim_slot_is_active_clip (0x80026c54)
// True (1) when anim id `animId` is the actor's active clip: either the blend
// weight is at/under zero and the primary slot (+0xcc) matches, or the weight is
// positive and the secondary slot (+0x124) matches.
// -----------------------------------------------------------------------------
extern "C" int FUN_80026c54(int rec, int animId)
{
    int st = *(int*)(rec + 4);
    if ((*(float*)(st + 400) <= FLOAT_80613930) || (*(int*)(st + 0x124) != animId)) {
        if (FLOAT_80613930 < *(float*)(st + 400))
            return 0;
        if (*(int*)(st + 0xcc) != animId)
            return 0;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// anim_active_curve_value (0x80026c9c)
// Resolve the active clip's descriptor (via primary slot index +0xd0) and sample
// its curve at the current clip time (+0xd4); returns base speed (0.0) when the
// descriptor has no curve channel (+0x2c == 0).
// -----------------------------------------------------------------------------
extern "C" double FUN_80026c9c(int rec)
{
    int st = *(int*)(rec + 4);
    int desc = *(int*)(DAT_80611824 + *(int*)(st + 0xd0) * 4);
    if (*(int*)(desc + 0x2c) == 0)
        return (double)FLOAT_80613930;
    return FUN_800233f8((double)*(float*)(st + 0xd4), desc);
}

// -----------------------------------------------------------------------------
// anim_slot_get_speed (0x80026ce8)
// Return a slot's stored playback speed (+8), or the base 0.0 for a null slot.
// -----------------------------------------------------------------------------
extern "C" double FUN_80026ce8(int slot)
{
    if (slot != 0)
        return (double)*(float*)(slot + 8);
    return (double)FLOAT_80613930;
}

// -----------------------------------------------------------------------------
// anim_slot_set_speed (0x80026d00)
// Clamp then store a slot's playback speed into +8 and +0xc. The requested speed
// is clamped up to the instance's current +0x18 timer value (when present) and
// floored at the base 0.0.
// -----------------------------------------------------------------------------
extern "C" void FUN_80026d00(double speed, int slot)
{
    if (slot == 0)
        return;

    int inst = *(int*)(DAT_80611824 + *(int*)(slot + 4) * 4);
    double v = speed;
    if ((inst == 0) || (speed <= (double)*(float*)(inst + 0x18))) {
        if (speed < (double)FLOAT_80613930)
            v = (double)FLOAT_80613930;
    }
    *(float*)(slot + 0xc) = (float)v;
    *(float*)(slot + 8)  = (float)v;
}

// -----------------------------------------------------------------------------
// anim_active_slot_get_speed (0x80026d50)
// Active-slot wrapper: resolve the active blend slot then read its speed.
// -----------------------------------------------------------------------------
extern "C" double FUN_80026d50(int rec)
{
    int slot = FUN_80025868(rec);
    return FUN_80026ce8(slot);
}

// -----------------------------------------------------------------------------
// anim_active_slot_set_speed (0x80026d74)
// Active-slot wrapper: resolve the active blend slot then clamp+store the speed.
// -----------------------------------------------------------------------------
extern "C" void FUN_80026d74(double speed, int rec)
{
    int slot = FUN_80025868(rec);
    FUN_80026d00(speed, slot);
}

// -----------------------------------------------------------------------------
// anim_apply_timescale_all (0x80026da8)
// Per-frame slow-motion pass (driven by slowmo_activate/slowmo_update). Walks the
// live actor list; for AI actors (class 8) and model-drawn actors whose draw
// callback is model_pose_eval / model_render_with_override_a (excluding a couple
// of special classes), either restore the saved speed fields (param_1 == 0) or
// save them and scale the active blend-slot speeds (+0x100/+0x158) plus the root
// timer (+0xa8/+0xac) by the slow-motion factor.
// -----------------------------------------------------------------------------
extern "C" void FUN_80026da8(int restore)
{
    int* actor = (int*)DAT_80540a44;
    while (actor != (int*)0) {
        int cls = actor[8];
        DAT_80611c50 = (int)actor;
        void* draw = (void*)actor[0x57];
        bool eligible =
            (cls == 8) ||
            (((draw == (void*)&model_pose_eval) ||
              (draw == (void*)&model_render_with_override_a)) &&
             (cls != 0x2000000) && (cls != 0x10));
        if (eligible) {
            int st = actor[1];
            if (restore == 0) {
                *(unsigned int*)(st + 0x100) = *(unsigned int*)(st + 0x1a0);
                *(unsigned int*)(st + 0x158) = *(unsigned int*)(st + 0x1a4);
            } else {
                *(unsigned int*)(st + 0x1a0) = *(unsigned int*)(st + 0x100);
                *(unsigned int*)(st + 0x1a4) = *(unsigned int*)(st + 0x158);
                *(float*)(st + 0x100) = *(float*)(st + 0x100) * FLOAT_806118ec;
                *(float*)(st + 0x158) = *(float*)(st + 0x158) * FLOAT_806118ec;
                *(float*)(st + 0xa8)  = *(float*)(st + 0xa8) / FLOAT_806118ec;
                *(float*)(st + 0xac)  = *(float*)(st + 0xa4) * *(float*)(st + 0xa8);
            }
        }
        actor = (int*)*(int*)DAT_80611c50;
    }
    DAT_80611c50 = (int)actor;
}

// -----------------------------------------------------------------------------
// anim_commit_pose_matrices (0x80026ea8)
// Per-frame pose commit for one model. Resolves the incoming record from the
// frame pointer, fetches its render-instance (DAT_80611d78 + idx*0x141c). Samples
// the primary clip pose (FUN_80026fa8); if a secondary render-instance is active
// it also samples that half and its own slot. When either blend weight is live it
// runs the skeleton concat/slerp passes to build the final bone matrices.
// -----------------------------------------------------------------------------
extern "C" void FUN_80026ea8(int frame)
{
    // __save_gpr_28 in the original just re-reads the incoming frame/record
    // pointer spilled by the prologue; expressed here as the parameter.
    unsigned int wHi = *(unsigned int*)(frame + 0x10);
    unsigned int wLo = *(unsigned int*)(frame + 0xc);
    int model = *(int*)(frame + 4);
    int inst = DAT_80611d78 + *(int*)(frame + 0x8c) * 0x141c;

    if (FUN_801a8608(inst) == 0) {
        int rec = *(int*)(frame + 4);
        FUN_80026fa8(frame, rec + 0xcc, (unsigned int*)(rec + 0x124), rec + 0x184, 0, 0x13);
    } else {
        int rec = *(int*)(frame + 4);
        FUN_80026fa8(frame, rec + 0xcc, (unsigned int*)(rec + 0x124), rec + 0x184, 0, 7);
        FUN_80026fa8(frame, inst + 0xd3c, (unsigned int*)(inst + 0xd94), inst + 0xd20, 8, 0x13);
    }

    if (((wHi | wLo) & 0xffff) != 0) {
        FUN_80022c14((int*)model, 0, 1);
        FUN_80022a38((int*)model, *(int*)(model + 0x10), 0, -1);
        FUN_80022b1c((int*)model, *(int*)(model + 0x10), 0, -1);
    }
}
