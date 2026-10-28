// Address range: 0x803610b4 - 0x80362080 | batch_1021 | 2026-09-12
//
// Grapple / tether weapon subsystem (the "grab beam" harpoon tool).
//
// Fires an electric-arc tether at a target entity, attaches a physics-island
// joint (tag 0x746b5f6a) between the muzzle and the target body, reels the
// target in, applies contact damage, and releases when the tether charge
// (entity field +0x2dc) is exhausted. Drives several entity update callbacks
// registered via entity_register_for_update.
//
//   grapple_fire_update      0x803610b4  per-frame fire/attach update
//   grapple_release          0x803614a8  detach + free joint + re-register
//   grapple_apply_damage     0x803615ec  dispatch tether contact damage
//   grapple_clamp_length     0x80361684  clamp tether rest length
//   grapple_retract_update   0x80361784  retract animation callback
//   grapple_attached_update  0x803619a8  attached-state physics callback
//   grapple_rebuild_matrix   0x803621fc  rebuild bone/matrix palette
//   grapple_idle_update       0x80362080  dropped-object settle callback
//
// (warpfx_get_state 0x80360f70 / warpfx_set_state 0x80360f78 are in
//  src/graphics/radar_warp_fx.cpp.)
//
// NOTE: non-matching decomp. Float register-passing artifacts from the
// decompiler (unused param_1..8 / in_fN doubles) are dropped; the per-object
// dispatch handlers (FUN_80362f20 etc.) remain raw addresses pending decomp.
#include "common.h"
#include "generated_decls.h"

#define GRAPPLE_JOINT_TAG 0x746b5f6a   // physics-island joint callback tag

// ---- runtime globals ----
extern int  DAT_80611c24;   // physics-island body pool base (stride 0x2e8)
extern int  DAT_80611d74;   // active render context record
extern int  DAT_80611928;   // free-running frame counter
extern int  DAT_806109c0;   // frame timebase (int; xor 0x80000000 -> signed)
extern unsigned char DAT_805015bd;   // menu/game state
extern float DAT_80583228[];          // sin/cos LUT

// ---- rodata float/double constants ----
extern float FLOAT_806108ec;  // frame delta scale
extern float FLOAT_8061b128, FLOAT_8061b12c, FLOAT_8061b130, FLOAT_8061b134;
extern float FLOAT_8061b138_unused;
extern float FLOAT_8061b140, FLOAT_8061b144, FLOAT_8061b148, FLOAT_8061b14c;
extern float FLOAT_8061b150, FLOAT_8061b154, FLOAT_8061b158, FLOAT_8061b15c;
extern float FLOAT_8061b160, FLOAT_8061b164, FLOAT_8061b168, FLOAT_8061b16c;
extern float FLOAT_8061b170, FLOAT_8061b174, FLOAT_8061b178, FLOAT_8061b17c, FLOAT_8061b180;
extern float FLOAT_8061b184, FLOAT_8061b188, FLOAT_8061b18c, FLOAT_8061b190;
extern double DOUBLE_8061b138;

// ---- raw per-object-type dispatch handlers + helpers (not yet decompiled) ----
extern void FUN_80362f20(int obj);
extern void FUN_80362f90(int obj);
extern void FUN_80362f58(int obj);
extern void FUN_8036326c(int obj);
extern void FUN_80362fc8(int obj);
extern void FUN_803631e8(int obj);
extern int  FUN_80364c40(int* p);
extern unsigned int FUN_80365bb0(int a, float* out);
extern void FUN_80365824(void);
extern void FUN_80365dc8(void);
extern int  FUN_80365694(void);
extern int  FUN_80364808(int entity, int a, int b);
extern void FUN_803656c4(int a);
extern int  FUN_803826f0(void);
extern void FUN_8038273c(void);
extern void FUN_80382734(void);
// batch_1023 raw helpers (grapple targeting + detonation submodule).
// The FUN_803826xx family are "scratch scope enter" thunks returning a 64-bit
// register pair (hi = active render ctx / player record, lo = subject entity);
// callers use one or both halves.
extern unsigned long long FUN_803826d8(void);  // scratch scope enter (ctx pair)
extern unsigned long long FUN_803826e8(void);  // scratch scope enter (subject pair)
extern void FUN_80382724(void);                // scratch scope leave
extern void FUN_8036589c(void);                // grapple detach helper
extern unsigned int FUN_80365914(int subobj);  // subobj lock/state query

// register save/restore intrinsics (no-ops here)
static inline int __save_gpr_28(void) { return 0; }
static inline void __restore_gpr_28(void) {}

// Resolve a physics-island body handle to its pool record (or 0 if stale).
static int resolve_body(int entity, int fieldOff)
{
    unsigned int h = *(unsigned int*)(entity + fieldOff);
    int rec;
    if (h == 0) return 0;
    rec = DAT_80611c24 + (h >> 0x16) * 0x2e8;
    if (*(unsigned int*)(rec + 0x2c) != h) {
        *(int*)(entity + fieldOff) = 0;
        return 0;
    }
    return rec;
}

// -------------------------------------------------------------------------
// 0x80361684  grapple_clamp_length
// -------------------------------------------------------------------------
void grapple_clamp_length(double lo, double hi, int owner, int target)
{
    float dx = (*(float*)(target + 100) + *(float*)(target + 0x1a8)) - *(float*)(owner + 0xb4);
    float dy = (*(float*)(target + 0x68) + *(float*)(target + 0x1ac)) - *(float*)(owner + 0xb8);
    float dz = (*(float*)(target + 0x6c) + *(float*)(target + 0x1b0)) - *(float*)(owner + 0xbc);
    double cur = (double)*(float*)(owner + 0x13e4);
    float sq = dx * dx + dy * dy + dz * dz;
    float approx = (float)(((int)sq >> 1) + 0x1fc00000);   // fast inverse-sqrt seed
    double dist = (double)((approx * approx + sq) / (FLOAT_8061b144 * approx));
    double clamped = (dist < cur) ? dist : cur;
    double out = lo;
    if ((lo <= clamped) && (cur < dist)) out = cur;
    if (out <= hi) {
        clamped = (cur < dist) ? cur : dist;
        hi = lo;
        if ((lo <= clamped) && (cur < dist)) hi = cur;
    }
    *(float*)(owner + 0x13e4) = (float)hi;
}

// -------------------------------------------------------------------------
// 0x803615ec  grapple_apply_damage
// -------------------------------------------------------------------------
void grapple_apply_damage(int target)
{
    float pointA[3];
    float pointB[3];
    int   event[8];
    double dmg;

    grapple_endpoint_dir(DAT_80611d74, target, pointA, pointB);
    switch_slot_reset((int)&event[0]);
    dmg = (double)(float)((double)FLOAT_8061b140 * (double)*(float*)(target + 0x2dc));
    event[0] = target;                              // +0x00 victim
    event[3] = *(int*)(DAT_80611d74 + 0x14);        // +0x0c source
    event[2] = event[3];                            // +0x08 owner
    // event carries the damage scalar and hit points; dispatch to the victim.
    *(float*)&event[4] = (float)(dmg * (double)FLOAT_806108ec);
    (void)pointA; (void)pointB;
    damage_dispatch_to_entity(event);
}

// -------------------------------------------------------------------------
// 0x803621fc  grapple_rebuild_matrix
// -------------------------------------------------------------------------
void grapple_rebuild_matrix(int obj)
{
    if (*(int*)(obj + 0x80) != 0) {
        subobj_rebuild_matrix_palette(obj);
    }
}

// -------------------------------------------------------------------------
// 0x803614a8  grapple_release
// -------------------------------------------------------------------------
void grapple_release(int entity)
{
    int island = subobj_island_find_by_body_b(GRAPPLE_JOINT_TAG);
    int rec;

    *(float*)(entity + 0x2dc) = FLOAT_8061b134;   // reset charge
    rec = resolve_body(entity, 0x2e4);
    if (*(unsigned int*)(entity + 0x2e4) != 0) {
        *(int*)(entity + 0x2e4) = 0;
        if (rec != 0) {
            int body = *(int*)(rec + 0x150);
            if ((body != 0) && (*(int*)(body + 0x13e8) == *(int*)(entity + 0x2c))) {
                *(float*)(body + 0x13e4) = FLOAT_8061b12c;
                *(int*)(*(int*)(rec + 0x150) + 0x13e8) = 0;
                *(int*)(*(int*)(rec + 0x150) + 0x13ec) = -1;
            }
        }
    }
    if (island != -1) subobj_island_pool_free(island);

    if ((*(int*)(entity + 0x20) == 0x40) && ((*(unsigned int*)(entity + 0x28) & 0x80000000) == 0)) {
        if (*(int*)(*(int*)(entity + 0x7c) + 4) == 0)
            entity_register_for_update(entity, 6, -0x7fc9df80);
        else
            entity_register_for_update(entity, 6, 0);
        entity_register_for_update(entity, 3, -0x7ff69b30);
    } else {
        entity_register_for_update(entity, 6, 0);
    }
}

// -------------------------------------------------------------------------
// 0x803610b4  grapple_fire_update - per-frame fire/attach update.
// -------------------------------------------------------------------------
void grapple_fire_update(int owner)
{
    int ctx = DAT_80611d74;
    int src = *(int*)(DAT_80611d74 + 0x14);
    float muzzle[3];
    float hitPos[3];
    float dt;
    int target;

    dt = (float)(FLOAT_806108ec * (FLOAT_8061b128 / (float)(int)(unsigned int)DAT_806109c0));

    if ((*(int*)(DAT_80611d74 + 4) < 0) || (*(int*)(DAT_80611d74 + 8) == 0) ||
        ((*(unsigned int*)(*(int*)(DAT_80611d74 + 8) + 0x21c) & 4) == 0)) {
        object_build_muzzle_matrix();
        muzzle[0] = 0.0f; muzzle[1] = 0.0f; muzzle[2] = 0.0f;
    } else {
        object_resolve_attach_world_point((unsigned int)owner, 0, muzzle);
        object_get_ejection_point(ctx, owner, 0);
    }

    // Resolve / re-validate the attached target body.
    target = resolve_body(ctx, 0x13e8);
    if (target == 0) {
        if (grapple_acquire_target(ctx) == 0) { __restore_gpr_28(); return; }
        target = resolve_body(ctx, 0x13e8);
        if (target == 0) { __restore_gpr_28(); return; }
    }

    if (target != 0) {
        int arc;
        if (target_scan_test_entity((double)*(float*)(ctx + 0xe04),
                                    (double)*(float*)(ctx + 0xe08),
                                    (double)FLOAT_8061b12c, target) == 0) {
            *(int*)(ctx + 0x13e8) = 0;
            *(float*)(ctx + 0x13e4) = FLOAT_8061b12c;
            target = 0;
        } else {
            hitPos[0] = *(float*)(target + 100) + *(float*)(target + 0x1a8);
            hitPos[1] = *(float*)(target + 0x68) + *(float*)(target + 0x1ac);
            hitPos[2] = *(float*)(target + 0x6c) + *(float*)(target + 0x1b0);
            arc = *(int*)(ctx + 0x13f0);
            if (arc == 0)
                electric_arc_add((unsigned int)(ctx + 0x13f0), *(unsigned int*)(ctx + 4),
                                 (unsigned int*)muzzle, (unsigned int*)hitPos);
            else
                electric_arc_set_endpoints(arc, (unsigned int*)muzzle, (unsigned int*)hitPos);
            *(int*)(ctx + 0x13f0) = arc;
        }
    }
    if (target == 0) { __restore_gpr_28(); return; }

    // Back-link and ramp charge.
    if (resolve_body(target, 0x2e4) == 0)
        *(int*)(target + 0x2e4) = *(int*)(src + 0x2c);

    {
        double cur = (double)*(float*)(target + 0x2dc);
        double next = (double)(float)(cur + (double)(float)((double)dt / (double)FLOAT_8061b130));
        double clamped = (next > (double)FLOAT_8061b128) ? (double)FLOAT_8061b128 : next;
        double result;
        if ((double)FLOAT_8061b134 <= clamped)
            result = (next > (double)FLOAT_8061b128) ? (double)FLOAT_8061b128 : next;
        else
            result = (double)FLOAT_8061b134;
        *(float*)(target + 0x2dc) = (float)result;
    }
    *(unsigned int*)(target + 0x18) |= 0x10;

    if (((*(unsigned int*)(target + 0x28) & 0x10) != 0) &&
        ((*(unsigned int*)(target + 0x9c) & 0x8000000) != 0)) {
        grapple_apply_damage(target);
    }

    // Per-object-type dispatch.
    {
        int kind = *(int*)(target + 0x20);
        int hasPalette = (*(int*)(target + 0x80) != 0);
        switch (kind) {
        case 1:
        case 0x400000:
            if (hasPalette) FUN_80362f90(target); else FUN_80362f20(target);
            break;
        case 8:      FUN_80362f58(target); break;
        case 4:      FUN_8036326c(target); break;
        case 0x40:   FUN_80362fc8(target); break;
        case 0x200:  FUN_803631e8(target); break;
        case 0x20000:
            if (hasPalette) FUN_80362f90(target); else grapple_hold_update(target);
            break;
        default:
            if (hasPalette) FUN_80362f90(target);
            break;
        }
    }
    __restore_gpr_28();
}

// -------------------------------------------------------------------------
// 0x80361784  grapple_retract_update - retract animation callback.
// -------------------------------------------------------------------------
void grapple_retract_update(void)
{
    int e = FUN_803826f0();
    if (((*(unsigned int*)(e + 0x18) | *(unsigned int*)(e + 8)) & 0x10) == 0) {
        float dec = FLOAT_806108ec * *(float*)(e + 0x2dc) * FLOAT_8061b14c;
        if (dec < FLOAT_8061b148) dec = FLOAT_8061b148;
        *(float*)(e + 0x2dc) -= dec;
    }
    {
        float charge = *(float*)(e + 0x2dc);
        if (FLOAT_8061b134 < charge) {
            float rot[18];
            float vec[3];
            float sx = FLOAT_8061b144 * *(float*)(e + 0x1c0);
            float sy = FLOAT_8061b144 * *(float*)(e + 0x1c8);
            unsigned int ix, iy;
            int bone, off;
            if (sx < FLOAT_8061b128) sx = FLOAT_8061b128;
            if (sy < FLOAT_8061b128) sy = FLOAT_8061b128;
            ix = (unsigned int)(FLOAT_8061b154 * FLOAT_8061b158 * (float)(int)(unsigned int)DAT_80611928 + FLOAT_8061b150);
            iy = (unsigned int)(FLOAT_8061b154 * FLOAT_8061b15c * (float)(int)(unsigned int)DAT_80611928 + FLOAT_8061b150);
            mtx4_set_rotation_zyx(
                (double)((FLOAT_8061b14c * charge * DAT_80583228[ix & 0x7ff]) / sx),
                (double)FLOAT_8061b134,
                (double)((FLOAT_8061b14c * charge * *(float*)((int)&DAT_80583228 + ((iy + 0x200) * 4 & 0x1ffc))) / sy),
                rot);
            vec[0] = *(float*)(e + 0x1c0);
            vec[1] = FLOAT_8061b134;
            vec[2] = *(float*)(e + 0x1c8);
            mtx4_transform_vector_alias(rot, vec);
            mtx4_write_translation((double)FLOAT_8061b134, (double)vec[1],
                                   (double)FLOAT_8061b134, (int)rot);
            off = 0;
            for (bone = 0; bone < *(int*)(*(int*)(*(int*)(e + 4) + 4) + 4); bone = bone + 1) {
                float* m = (float*)(*(int*)(*(int*)(e + 4) + 0x10) + off);
                mtx4_multiply(rot, m, m);
                off = off + 0x40;
            }
            subobj_redirty_in_aabb_wrapper();
        }
    }
    FUN_8038273c();
}

// -------------------------------------------------------------------------
// 0x803619a8  grapple_attached_update - attached-state physics callback.
// -------------------------------------------------------------------------
void grapple_attached_update(void)
{
    int e = (int)FUN_803826e8();
    int* weapon = *(int**)(e + 0x7c);
    int rec;

    if ((FLOAT_8061b134 < *(float*)(e + 0x2dc)) && ((*(unsigned int*)(e + 0x28) & 0x80000000) == 0)) {
        rec = resolve_body(e, 0x2e4);
        if (rec != 0) {
            int body = *(int*)(rec + 0x150);
            int island = FUN_80364c40(weapon + 0x13);
            if (island != 0) {
                // Reel the tether toward the anchor.
                float anchor[3];
                float vel[3];
                unsigned int handle;
                int isl;
                double dist, gain;

                if (((*(unsigned int*)(e + 0x18) | *(unsigned int*)(e + 8)) & 0x10) == 0) {
                    float dec = (float)((double)FLOAT_806108ec * (double)(*(float*)(e + 0x2dc) * FLOAT_8061b160));
                    if (dec < FLOAT_8061b148) dec = FLOAT_8061b148;
                    *(float*)(e + 0x2dc) -= dec;
                }
                isl = subobj_island_find_by_body_b(GRAPPLE_JOINT_TAG);
                grapple_clamp_length((double)FLOAT_8061b164, (double)FLOAT_8061b12c, body, e);
                handle = FUN_80365bb0(weapon[0x13], anchor);
                FUN_80365824();
                grapple_endpoint_dir(body, e, anchor, vel);   // fills anchor(point)+vel(dir); +0x13e4 is an input
                anchor[0] -= *(float*)(body + 0xb4);
                anchor[1] -= *(float*)(body + 0xb8);
                anchor[2] -= *(float*)(body + 0xbc);
                dist = math_sqrt((double)(anchor[0]*anchor[0] + anchor[1]*anchor[1] + anchor[2]*anchor[2]));

                {
                    float d = (float)((double)FLOAT_8061b168 - (double)*(float*)(body + 0x13e4)) * FLOAT_8061b16c;
                    float d2 = (d > FLOAT_8061b128) ? FLOAT_8061b128 : d;
                    float d3 = FLOAT_8061b134;
                    if ((FLOAT_8061b134 <= d2) && (d > FLOAT_8061b128)) d3 = FLOAT_8061b128;
                    else if (FLOAT_8061b134 <= d2) d3 = d;
                    gain = (double)(FLOAT_8061b170 * d3 + FLOAT_8061b174 * (FLOAT_8061b128 - d3));
                    if (((float)(dist - (double)*(float*)(body + 0x13e4)) <= FLOAT_8061b178) &&
                        (subobj_is_handle_free(handle) != 0)) {
                        double age = subobj_get_age_fraction(handle);
                        if (age > (double)FLOAT_8061b150) age = (double)FLOAT_8061b150;
                        gain = (double)(float)(gain * (double)(float)(age / (double)FLOAT_8061b150));
                        if (gain < (double)FLOAT_8061b17c) gain = (double)FLOAT_8061b17c;
                    }
                }
                if (isl == -1) {
                    subobj_island_create_point(handle, 0, anchor);
                    subobj_island_set_joint_callback(0, (void*)GRAPPLE_JOINT_TAG);
                    isl = 0;
                }
                subobj_island_set_anchor(isl, 1, anchor);
                subobj_island_set_drift_gain(gain, isl);
                subobj_island_set_restitution((double)FLOAT_8061b180, isl);

                if (((*(unsigned int*)(e + 0x18) | *(unsigned int*)(e + 8)) & 0x10) == 0) {
                    float s = FLOAT_8061b184 * *(float*)(e + 0x2dc);
                    vel[0] = vel[0] * s; vel[1] = vel[1] * s; vel[2] = vel[2] * s;
                    FUN_80365dc8();
                    subobj_set_flags(handle, subobj_get_flags(handle) & 0xffffbfff | 0x1000000);
                    *(float*)(e + 0x2dc) = FLOAT_8061b134;
                } else {
                    subobj_set_flag_bits(handle, 0x4000);
                }
                if (*(float*)(e + 0x2dc) <= FLOAT_8061b134) {
                    grapple_release(e);
                    subobj_clear_flag_bits(handle, 0x4000);
                }
                FUN_80382734();
                return;
            }

            // Not yet an island: AI-target handoff / spawn island.
            if (weapon[0x11] == 1) {
                if ((*(int*)(body + 0x13ec) == -1) || (*(float*)(e + 0x2dc) <= FLOAT_8061b134)) {
                    grapple_release(e);
                } else if (FLOAT_8061b188 <= *(float*)(e + 0x2dc)) {
                    int killer;
                    player_ai_set_active_index(*(int*)(body + 0x13ec));
                    grapple_apply_damage(e);
                    killer = player_ai_resolve_killer_entity();
                    if (killer != 0) {
                        float dx, dy, dz;
                        grapple_release(e);
                        dx = *(float*)(killer + 100) - *(float*)(body + 0xb4);
                        dy = *(float*)(killer + 0x68) - *(float*)(body + 0xb8);
                        dz = *(float*)(killer + 0x6c) - *(float*)(body + 0xbc);
                        *(float*)(body + 0x13e4) = (float)math_sqrt((double)(dx*dx + dy*dy + dz*dz));
                        *(int*)(body + 0x13e8) = *(int*)(killer + 0x2c);
                        *(int*)(body + 0x13ec) = -1;
                        *(int*)(killer + 0x2e4) = *(int*)(*(int*)(body + 0x14) + 0x2c);
                        *(int*)(killer + 0x2dc) = *(int*)(e + 0x2dc);
                    }
                }
                FUN_80382734();
                return;
            }

            if (*(float*)(e + 0x2dc) <= FLOAT_8061b134) grapple_release(e);

            // Latch fire input into the weapon state.
            if ((weapon[9] & 8) == 0) {
                unsigned int btn = (unsigned int)weapon[5];
                if (((btn & 1) != 0) && (btn != 0 || weapon[4] != 2)) {
                    weapon[0x2af] = (int)btn;
                    weapon[0x2ae] = weapon[4];
                    if ((weapon[0x2ab] == 1) && (weapon[0x2aa] == 0)) {
                        unsigned int b2 = (DAT_805015bd == '\n') ? (unsigned int)weapon[5] : (unsigned int)weapon[7];
                        if ((b2 & 1) != 0) {
                            weapon[0x2ab] = 0;
                            weapon[0x2aa] = 2;
                        }
                    } else {
                        weapon[0x2ab] = 0;
                        weapon[0x2aa] = 2;
                    }
                }
            }

            // Try to acquire an island once the charge is high enough.
            if ((double)*(float*)(e + 0x2dc) > (double)FLOAT_8061b150) {
                int flag = *(int*)(weapon[0x2fc] + 0x80);
                if (flag < 0) {
                    unsigned int cls = *(unsigned int*)(*(int*)(*(int*)(*weapon + 4) + 0x80) + 4);
                    int ok = 1;
                    if ((1 < cls) && (cls != 0x15) && (cls != 0x7c)) ok = 0;
                    if (ok) flag = 0;
                }
                if (-1 < flag) {
                    int slot = FUN_80365694();
                    if (slot != -1) {
                        int isl2 = FUN_80364808(e, slot, flag);
                        weapon[0x13] = isl2;
                        if (isl2 == 0) {
                            FUN_803656c4(slot);
                        } else {
                            anim_clip_field_b08(e);
                            anim_start_clip((double)FLOAT_8061b178, (double)FLOAT_8061b128);
                        }
                    }
                }
                if (FUN_80364c40(weapon + 0x13) != 0) {
                    if (*(short*)(weapon + 0x12) == 0x43) {
                        float anchor2[3];
                        unsigned int h2 = FUN_80365bb0(weapon[0x13], anchor2);
                        subobj_set_extra_words(h2, 0, (unsigned int)(uintptr_t)&grapple_detonate_impl);
                        entity_register_for_update(e, 0xd, -0x7fc9c120);
                    }
                    weapon[0x2ab] = 1;
                    weapon[0x2aa] = 0;
                }
            }
            FUN_80382734();
            return;
        }
    }
    grapple_release(e);
    FUN_80382734();
}

// -------------------------------------------------------------------------
// 0x80362080  grapple_idle_update - dropped-object settle callback.
// -------------------------------------------------------------------------
void grapple_idle_update(void)
{
    int entity = __save_gpr_28();
    int weapon = *(int*)(entity + 0x7c);
    unsigned int handle = *(unsigned int*)(entity + 0x80);

    if ((*(int*)(weapon + 4) == 0) && ((*(unsigned int*)(entity + 0x28) & 0x80000000) == 0) &&
        (handle != 0)) {
        spawn_pickup_proximity_check();   // (void) proximity gate
        {
            // Nudge upright while recently activated.
            if ((*(int*)(weapon + 0xc) == 2) &&
                (DAT_80611928 - subobj_get_activation_frame(handle) < 10)) {
                int mtx = subobj_get_object_matrix(handle);
                float* axis = 0;
                if (ABS(*(float*)(mtx + 0x14)) <= FLOAT_8061b18c) {
                    if (FLOAT_8061b18c < ABS(*(float*)(mtx + 0x24)))
                        axis = (float*)(mtx + 0x10);
                } else {
                    axis = (float*)(mtx + 0x20);
                }
                if (axis != 0) {
                    float torque[3];
                    double inv = subobj_get_inverse_mass(handle);
                    float s = (float)((double)FLOAT_8061b190 * inv);
                    torque[0] = axis[0] * s;
                    torque[1] = axis[1] * s;
                    torque[2] = axis[2] * s;
                    subobj_add_angular_force(handle, torque);
                }
            }
            // Once at rest, re-parent, settle, and release.
            if (subobj_is_idle(handle) != 0) {
                *(int*)(weapon + 0x38) = soundpad_find_nearest_entity((double)FLOAT_8061b134,
                                                                      (float*)(entity + 100));
                *(int*)(weapon + 0xa0) = 1;
                grapple_release(entity);
                entity_register_for_update(entity, 6, 0);
                entity_register_for_update(entity, 3, -0x7ff69b30);
                subobj_detach_from_entity(entity);
            }
        }
    }
    __restore_gpr_28();
}

// =========================================================================
// batch_1022: per-object-type begin handlers + reel/aim/shake update callbacks
// =========================================================================

// ---- additional globals / constants used by this batch ----
extern int  DAT_806109c0;               // (also declared above) frame timebase
extern int  DAT_80612614;               // game-mode id
extern unsigned char DAT_805015c0;      // game-flags byte
extern int  DAT_8043e23c[];             // per-map object-type -> material table (stride 0xf)
extern unsigned char DAT_80610ba0[];    // object-type descriptor table (stride 0x29c)

extern float FLOAT_8061b194, FLOAT_8061b198, FLOAT_8061b19c, FLOAT_8061b1a0;
extern float FLOAT_8061b1a4, FLOAT_8061b1a8, FLOAT_8061b1ac;
extern float FLOAT_8061b1b0, FLOAT_8061b1b4, FLOAT_8061b1b8, FLOAT_8061b1bc;
extern float FLOAT_8061b1c0, FLOAT_8061b1c4, FLOAT_8061b1c8, FLOAT_8061b1cc;
extern float FLOAT_8061b1d0, FLOAT_8061b1d4, FLOAT_8061b1d8, FLOAT_8061b1dc;
extern float FLOAT_8061b1e0, FLOAT_8061b1e4, FLOAT_8061b1e8, FLOAT_8061b1ec;
extern float FLOAT_8061b1f0, FLOAT_8061b1f4;

extern int  FUN_803826e4(void);
extern void FUN_80382730(void);

// LUT index helper (matches the &DAT_80583228 sin/cos table lookups).
static inline float lut_sin(unsigned int idx) { return DAT_80583228[idx & 0x7ff]; }
static inline float lut_cos(unsigned int idx) {
    return *(float*)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc));
}

// -------------------------------------------------------------------------
// 0x80362f20  grapple_begin_character  (object type 1 / 0x400000)
// 0x80362f58  grapple_begin_item       (object type 8)
// 0x80362f90  grapple_begin_generic    (default / animated palette)
// 0x8036326c  grapple_begin_ragdoll    (object type 4)
// Each registers the matching per-type reel update callback if not already set.
// -------------------------------------------------------------------------
void grapple_begin_character(int obj)
{
    if (*(int*)(obj + 0x16c) == 0)
        entity_register_for_update(obj, 6, -0x7fc9e87c);
}

void grapple_begin_item(int obj)
{
    if (*(int*)(obj + 0x16c) == 0)
        entity_register_for_update(obj, 6, -0x7fc9e658);
}

void grapple_begin_generic(int obj)
{
    if (*(int*)(obj + 0x16c) == 0)
        entity_register_for_update(obj, 6, -0x7fc9da20);
}

void grapple_begin_ragdoll(int obj)
{
    if (*(int*)(obj + 0x16c) == 0)
        entity_register_for_update(obj, 6, -0x7fc9d27c);
}

// -------------------------------------------------------------------------
// 0x803631e8  grapple_begin_door  (object type 0x200)
// -------------------------------------------------------------------------
void grapple_begin_door(int obj)
{
    if (*(int*)(obj + 0x16c) == 0) {
        int weapon = *(int*)(obj + 0x7c);
        if (((*(unsigned int*)(weapon + 8) & 0x40) == 0) &&
            (prop_is_destroyed(obj) == 0) && (prop_is_state_idle(obj) == 0)) {
            *(int*)(weapon + 0x10) = 0xc;
        }
        entity_register_for_update(obj, 6, -0x7fc9ddd8);
    }
}

// -------------------------------------------------------------------------
// 0x80362fc8  grapple_begin_prop  (object type 0x40) - spawn collision subobj.
// -------------------------------------------------------------------------
void grapple_begin_prop(int obj)
{
    int weapon;
    if (*(int*)(obj + 0x16c) != 0) return;
    weapon = *(int*)(obj + 0x7c);
    if ((**(unsigned int**)(weapon + 0x18) & 0x80) != 0) return;
    if (subobj_effect_edge_state(1) == 0) return;

    if (resolve_body(obj, 0x2e4) == 0)
        entity_register_for_update(obj, 6, -0x7fc9df80);
    else
        grapple_begin_generic(obj);

    entity_register_for_update(obj, 3, 0);
    *(int*)(weapon + 0xa0) = 1;
    *(int*)(weapon + 0xc0) = 3;
    *(int*)(weapon + 0xa8) = 0;
    *(unsigned int*)(weapon + 0x10) |= 8;

    if (*(int*)(obj + 0x80) == 0) {
        int mat = -1;
        if (*(int*)(weapon + 0xc) == 2) {
            int typeMat = DAT_8043e23c[*(int*)(weapon + 0x14) * 0xf];
            if (object_type_dispatch(0x10, typeMat, 0, 0, mat) == 0) {
                mat = ((*(unsigned int*)(DAT_80610ba0 + typeMat * 0x29c + 0xc) & 1) == 0) ? 6 : 5;
            }
        } else {
            mat = 7;
        }
        if ((subobj_material_matches(*(int**)(*(int*)(obj + 4) + 4), mat) == 0) &&
            (subobj_material_matches(*(int**)(*(int*)(obj + 4) + 4), -1) != 0)) {
            mat = -1;
        }
        subobj_pool_alloc();  // allocates and stores the handle into obj+0x80 internally
        if (*(int*)(obj + 0x80) != 0) {
            subobj_build_descriptor_default(*(unsigned int*)(obj + 0x80), mat, 1.1754944e-38f);
            subobj_set_collision_groups(*(unsigned int*)(obj + 0x80), 4, 0xf1);
        }
    }
    entity_register_for_update(obj, 2, -0x7fc9de04);
    if (*(unsigned int*)(obj + 0x80) != 0)
        subobj_dirty_list_add(*(unsigned int*)(obj + 0x80));
}

// -------------------------------------------------------------------------
// 0x80362564  grapple_decay_update - decay charge and release when exhausted.
// -------------------------------------------------------------------------
void grapple_decay_update(int e)
{
    if (((*(unsigned int*)(e + 0x18) | *(unsigned int*)(e + 8)) & 0x10) == 0) {
        float dec = FLOAT_806108ec * *(float*)(e + 0x2dc) * FLOAT_8061b1ac;
        if (dec < FLOAT_8061b148) dec = FLOAT_8061b148;
        *(float*)(e + 0x2dc) -= dec;
    }
    if (*(float*)(e + 0x2dc) <= FLOAT_8061b134)
        grapple_release(e);
}

// -------------------------------------------------------------------------
// 0x80362228  grapple_aim_wobble_update - crosshair/aim wobble offsets.
// -------------------------------------------------------------------------
void grapple_aim_wobble_update(int e)
{
    int weapon = *(int*)(e + 0x7c);
    float charge;

    if (((*(unsigned int*)(e + 0x18) | *(unsigned int*)(e + 8)) & 0x10) == 0) {
        float dec = FLOAT_806108ec * *(float*)(e + 0x2dc) * FLOAT_8061b14c;
        if (dec < FLOAT_8061b148) dec = FLOAT_8061b148;
        *(float*)(e + 0x2dc) -= dec;
    }
    charge = *(float*)(e + 0x2dc);
    if (FLOAT_8061b134 < charge) {
        float t = (float)(int)(unsigned int)DAT_80611928;
        if ((*(unsigned int*)(weapon + 8) & 0x40) == 0) {
            unsigned int ix = (unsigned int)(FLOAT_8061b154 * t * FLOAT_8061b1a4 + FLOAT_8061b150);
            unsigned int iy = (unsigned int)(FLOAT_8061b154 * t * FLOAT_8061b1a8 + FLOAT_8061b150);
            *(float*)(weapon + 0x4c) =
                (FLOAT_8061b194 * FLOAT_8061b14c * FLOAT_8061b128 * charge * lut_sin(ix)) / FLOAT_8061b19c;
            if ((FLOAT_8061b134 < ABS(*(float*)(weapon + 0x38))) ||
                (FLOAT_8061b134 < ABS(*(float*)(weapon + 0x34)))) {
                float lo = *(float*)(weapon + 0x1c);
                float a = *(float*)(weapon + 0x34);
                float hi = (a < lo) ? lo : a;
                float b = *(float*)(weapon + 0x38);
                float out = (hi <= b) ? ((a < lo) ? lo : a) : a;
                *(float*)(weapon + 0x1c) = out;
            }
            *(float*)(weapon + 0x50) =
                (FLOAT_8061b194 * FLOAT_8061b14c * FLOAT_8061b128 * *(float*)(e + 0x2dc) * lut_sin(iy)) / FLOAT_8061b19c;
            if ((FLOAT_8061b134 < ABS(*(float*)(weapon + 0x40))) ||
                (FLOAT_8061b134 < ABS(*(float*)(weapon + 0x3c)))) {
                float lo = *(float*)(weapon + 0x24);
                float a = *(float*)(weapon + 0x3c);
                float hi = (a < lo) ? lo : a;
                float b = *(float*)(weapon + 0x40);
                float out = (hi <= b) ? ((a < lo) ? lo : a) : a;
                *(float*)(weapon + 0x24) = out;
            }
        } else {
            unsigned int ix = (unsigned int)(FLOAT_8061b154 * t * FLOAT_8061b150 + FLOAT_8061b150);
            unsigned int iy = (unsigned int)(FLOAT_8061b154 * t * FLOAT_8061b1a0 + FLOAT_8061b150);
            *(float*)(weapon + 0x4c) =
                (FLOAT_8061b194 * FLOAT_8061b14c * FLOAT_8061b198 * charge * lut_sin(ix)) / FLOAT_8061b19c;
            *(float*)(weapon + 0x50) =
                (FLOAT_8061b194 * FLOAT_8061b14c * FLOAT_8061b198 * *(float*)(e + 0x2dc) * lut_sin(iy)) / FLOAT_8061b19c;
        }
    } else {
        if (*(int*)(weapon + 0x10) == 0xc) *(int*)(weapon + 0x10) = 4;
        *(float*)(weapon + 0x4c) = FLOAT_8061b134;
        *(float*)(weapon + 0x50) = FLOAT_8061b134;
        grapple_release(e);
    }
}

// -------------------------------------------------------------------------
// 0x80362d84  grapple_shake_update - per-frame shake offset (+0x28).
// -------------------------------------------------------------------------
void grapple_shake_update(int e)
{
    int weapon = *(int*)(e + 0x7c);
    if (((*(unsigned int*)(e + 0x18) | *(unsigned int*)(e + 8)) & 0x10) == 0) {
        float dec = FLOAT_806108ec * *(float*)(e + 0x2dc) * FLOAT_8061b14c;
        if (dec < FLOAT_8061b148) dec = FLOAT_8061b148;
        *(float*)(e + 0x2dc) -= dec;
    }
    if (FLOAT_8061b134 < *(float*)(e + 0x2dc)) {
        float dt = FLOAT_8061b128 / (float)(int)(unsigned int)DAT_806109c0;
        float tf = (float)(int)(unsigned int)DAT_80611928;
        unsigned int ix = (unsigned int)(FLOAT_8061b154 * FLOAT_8061b1ec * tf + FLOAT_8061b150);
        unsigned int iy = (unsigned int)(FLOAT_8061b154 * FLOAT_8061b1f4 * tf + FLOAT_8061b150);
        *(unsigned int*)(weapon + 0xc) |= 0x2000000;
        *(float*)(weapon + 0x28) =
            dt * FLOAT_8061b1e8 * *(float*)(e + 0x2dc) * lut_sin(ix) + *(float*)(weapon + 0x28);
        *(float*)(weapon + 0x28) =
            dt * FLOAT_8061b1f0 * *(float*)(e + 0x2dc) * lut_cos(iy) + *(float*)(weapon + 0x28);
    } else {
        *(unsigned int*)(weapon + 0xc) &= 0xfdffffff;
        grapple_release(e);
    }
}

// -------------------------------------------------------------------------
// 0x803625e0  grapple_pull_object_update - physics-prop reel update.
//
// Non-matching: reels a grabbed physics prop toward the muzzle. Resolves the
// grabbed body + the collision sub-object contact origin, decays charge, and
// applies either a short-range spring (physics-island anchor + drift + linear
// force) or a longer-range linear pull. Picks up the prop when close; releases
// when charge is exhausted. The fast inverse-sqrt idiom is condensed.
// -------------------------------------------------------------------------
void grapple_pull_object_update(void)
{
    int e = FUN_803826e4();
    int rec;

    if (!((FLOAT_8061b134 < *(float*)(e + 0x2dc)) && ((*(unsigned int*)(e + 0x28) & 0x80000000) == 0))) {
        grapple_release(e);
        FUN_80382730();
        return;
    }
    rec = resolve_body(e, 0x2e4);
    if (rec == 0) { grapple_release(e); FUN_80382730(); return; }

    {
        int body = *(int*)(rec + 0x150);
        float dt = FLOAT_806108ec * (FLOAT_8061b128 / (float)(int)(unsigned int)DAT_806109c0);
        float* origin = (float*)subobj_get_contact_origin(*(unsigned int*)(e + 0x80));
        double invMass = subobj_get_inverse_mass(*(unsigned int*)(e + 0x80));
        double bodyRadius, reach;
        float d[3], vel[3], anchor[3], force[3];
        float distSq;
        int island;
        bool bigMode = false;

        (void)invMass;
        bodyRadius = max3f((double)*(float*)(e + 0x1c0), (double)*(float*)(e + 0x1c4), (double)*(float*)(e + 0x1c8));
        {
            float rad = FLOAT_8061b150;
            if (FLOAT_8061b150 <= (float)((double)FLOAT_8061b144 * bodyRadius))
                rad = (float)((double)FLOAT_8061b144 * bodyRadius);
            reach = (double)(FLOAT_8061b150 + rad);
        }
        if (((*(unsigned int*)(e + 0x18) | *(unsigned int*)(e + 8)) & 0x10) == 0) {
            float dec = FLOAT_806108ec * *(float*)(e + 0x2dc) * FLOAT_8061b14c;
            if (dec < FLOAT_8061b148) dec = FLOAT_8061b148;
            *(float*)(e + 0x2dc) -= dec;
        }
        d[0] = *(float*)(body + 0xb4) - origin[0];
        d[1] = *(float*)(body + 0xb8) - origin[1];
        d[2] = *(float*)(body + 0xbc) - origin[2];
        distSq = d[0]*d[0] + d[1]*d[1] + d[2]*d[2];

        if (((DAT_805015c0 & 0x40) != 0) && (DAT_80612614 == 0x4e)) bigMode = true;
        {
            float range = bigMode ? FLOAT_8061b1b0 : FLOAT_8061b1b4;
            if (distSq <= (float)((double)range + reach) * (float)((double)range + reach)) {
                float pullK = (*(int*)(e + 0x20) == 0x40) ? FLOAT_8061b1c4 : FLOAT_8061b1c8;
                double cur = (double)*(float*)(body + 0x13e4);
                double denom = (double)(float)((double)range - reach);
                *(float*)(body + 0x13e4) =
                    (float)((double)(float)((double)pullK * (double)(float)((double)(float)(reach - cur) / denom)) * dt + cur);
                grapple_clamp_length(reach, (double)range, body, e);
                grapple_endpoint_dir(body, e, anchor, vel);   // refresh anchor(point)+vel(dir)

                if ((*(int*)(e + 0x20) == 0x40) &&
                    (ABS((float)(reach - (double)*(float*)(body + 0x13e4))) < FLOAT_8061b1cc)) {
                    int weapon = *(int*)(e + 0x7c);
                    if (*(int*)(weapon + 4) == 0) {
                        spawn_pickup_proximity_check();
                        *(unsigned int*)(weapon + 0x10) |= 0x10;
                        spawn_pickup_dispatch();
                        *(unsigned int*)(weapon + 0x10) &= 0xffffffef;
                    }
                    grapple_release(e);
                    FUN_80382730();
                    return;
                }

                anchor[0] -= origin[0]; anchor[1] -= origin[1]; anchor[2] -= origin[2];
                {
                    float aSq = anchor[0]*anchor[0] + anchor[1]*anchor[1] + anchor[2]*anchor[2];
                    float along = anchor[0]*(*(float*)(body + 0xc0)) +
                                  anchor[1]*(*(float*)(body + 0xc4)) +
                                  anchor[2]*(*(float*)(body + 200));
                    if (FLOAT_8061b1d0 < aSq) {
                        float spd = (FLOAT_8061b128 / (float)(int)(unsigned int)DAT_806109c0) * FLOAT_8061b1d4 * aSq;
                        float clampSpd = (spd < FLOAT_8061b1d8) ? spd : FLOAT_8061b1d8;
                        float inv = 1.0f / (float)math_sqrt((double)aSq);
                        force[0] = anchor[0]*clampSpd*inv + (*(float*)(body + 0xc0))*along;
                        force[1] = anchor[1]*clampSpd*inv + (*(float*)(body + 0xc4))*along;
                        force[2] = anchor[2]*clampSpd*inv + (*(float*)(body + 200))*along;
                    } else {
                        force[0] = FLOAT_8061b134; force[1] = FLOAT_8061b134; force[2] = FLOAT_8061b134;
                    }
                }
                subobj_dirty_list_add(*(unsigned int*)(e + 0x80));
                island = subobj_island_find_by_body_b(GRAPPLE_JOINT_TAG);
                if (island == -1) {
                    subobj_island_create_point(*(unsigned int*)(e + 0x80), 0, origin);
                    subobj_island_set_joint_callback(0, (void*)GRAPPLE_JOINT_TAG);
                    island = 0;
                }
                force[0] += origin[0]; force[1] += origin[1]; force[2] += origin[2];
                subobj_island_set_anchor(island, 1, force);
                {
                    double gain = (double)(FLOAT_8061b1dc);
                    if (subobj_is_handle_free(*(unsigned int*)(e + 0x80)) != 0) {
                        double age = subobj_get_age_fraction(*(unsigned int*)(e + 0x80));
                        if (age > (double)FLOAT_8061b150) age = (double)FLOAT_8061b150;
                        gain = (double)(float)(gain * (double)(float)(age / (double)FLOAT_8061b150));
                        if (gain < (double)FLOAT_8061b148) gain = (double)FLOAT_8061b148;
                    }
                    subobj_island_set_drift_gain(gain, island);
                }
                subobj_island_set_restitution((double)FLOAT_8061b14c, island);

                if (((*(unsigned int*)(e + 0x18) | *(unsigned int*)(e + 8)) & 0x10) == 0) {
                    float s = (float)((double)(FLOAT_8061b1e4 * *(float*)(e + 0x2dc)));
                    float lf[3];
                    lf[0] = d[0]*s; lf[1] = d[1]*s; lf[2] = d[2]*s;
                    subobj_add_scaled_linear_force(*(unsigned int*)(e + 0x80), lf);
                    subobj_set_flags(*(unsigned int*)(e + 0x80),
                                     subobj_get_flags(*(unsigned int*)(e + 0x80)) & 0xffffbfff | 0x1000000);
                    *(float*)(e + 0x2dc) = FLOAT_8061b134;
                } else {
                    subobj_set_flag_bits(*(unsigned int*)(e + 0x80), 0x4000);
                }
            } else {
                // Long-range linear pull.
                float inv = 1.0f / (float)math_sqrt((double)distSq);
                float lf[3];
                float mag = FLOAT_8061b128 * inv;
                lf[0] = d[0] * mag; lf[1] = d[1] * mag; lf[2] = d[2] * mag;
                subobj_add_linear_force(*(unsigned int*)(e + 0x80), lf);
            }
        }
        if (*(float*)(e + 0x2dc) <= FLOAT_8061b134) {
            grapple_release(e);
            subobj_clear_flag_bits(*(unsigned int*)(e + 0x80), 0x4000);
        }
    }
    FUN_80382730();
}

// ============================================================================
// Address range: 0x803632a4 - 0x80364097 | batch_1023 | 2026-09-12
//
// Grapple targeting + detonation submodule (continuation of the batch_1021/1022
// grapple weapon). These implement target acquisition (a target_scan pass driven
// by four filter/hit/LOS callbacks), the aim-direction and closest-segment math
// used by the reel/damage updates, a grabbed-object hold callback, and the
// "detonate at target" routine.
//
//   grapple_hold_update       0x803632a4  held-object hold/release callback
//   grapple_aim_direction     0x803633a4  screen aim -> world direction vector
//   grapple_endpoint_dir      0x803634f8  muzzle endpoint + normalized direction
//   grapple_closest_on_target 0x8036360c  closest distance to target skeleton segs
//   grapple_acquire_target    0x80363804  target_scan pass to pick a grapple target
//   grapple_scan_los_cb       0x8036396c  target_scan line-of-sight raycast callback
//   grapple_scan_bounds_cb    0x80363ac8  target_scan screen-bounds hit-test callback
//   grapple_scan_filter_cb    0x80363c38  target_scan candidate eligibility filter
//   grapple_detonate          0x80363ee0  forwarder -> grapple_detonate_impl
//   grapple_detonate_impl     0x80363f08  spawn explosion at the grabbed target
// ============================================================================

// batch_1023 rodata float/double constants
extern float  FLOAT_8061b1f8;   // hold-charge release threshold
extern float  FLOAT_8061b1fc;   // aim screen->world scale
extern float  FLOAT_8061b200;   // min direction length^2 (below -> skip normalize)
extern float  FLOAT_8061b204;   // closest-segment search seed distance
extern float  FLOAT_8061b208;   // target_scan search box half-extent
extern float  FLOAT_8061b1b8;   // fast-rsqrt Newton constant (1.5)
extern double DOUBLE_8061b138;  // int->float magic (0x43300000 bias)
extern int    DAT_80611d54;     // active camera/view record base
extern float  DAT_803e0c28, DAT_803e0c2c, DAT_803e0c30;  // default detonation normal

// target_scan_screen_entities / scene_raycast are defined `void` in their home
// TUs but actually return a value in EAX (hit count / hit node). Read them here
// through typed function-pointer aliases to the same symbols so we can branch on
// the result without redeclaring (which would collide with generated_decls.h).
typedef int (*ScreenScanFn)(unsigned long long, double, double, double, double, int*, void*, int);
typedef int (*RaycastFn)(int, float*, float*, int, float*, float*, int*, unsigned int, unsigned int, int*);
// entity_compute_screen_bounds is the real 7-arg int form; generated_decls.h has a
// stale 5-arg void prototype, so call through an alias to avoid the redeclaration.
typedef int (*ScreenBoundsFn)(int, unsigned int*, unsigned int*, unsigned int*, unsigned int*, float*, int);
// target_scan_set_callbacks is declared (undefined4 x5); build the callback args as
// plain 32-bit function addresses to feed those slots.
typedef unsigned int (*ScanCb)(int, int);

// -------------------------------------------------------------------------
// 0x803632a4  grapple_hold_update - held-object hold/release callback.
//   Keeps a grabbed physics-prop bound to the tether: while charge (+0x2dc)
//   stays above the release threshold it latches the "held" material flag
//   (0x80) and, on the first hold frame, invokes the per-object hold handler
//   at (+0x154) with the resolved body record. Below threshold it clears the
//   "grabbed" material flag (0x400) and lets the object drop.
// -------------------------------------------------------------------------
void grapple_hold_update(int e)
{
    int body = *(int*)(e + 0x7c);

    if (*(int*)(e + 0x16c) == 0) {
        entity_register_for_update(e, 6, 0x80362564);   // grapple_decay_update
    }

    if (*(float*)(e + 0x2dc) < FLOAT_8061b1f8) {
        if ((*(unsigned int*)(body + 0x10) & 0x400) != 0) {
            *(unsigned int*)(body + 0x10) &= 0xffffff7f;
        }
        return;
    }

    if ((*(unsigned int*)(body + 0x10) & 0x100) != 0) {
        *(float*)(e + 0x2dc) = FLOAT_8061b134;
    }
    unsigned int flags = *(unsigned int*)(body + 0x10);
    if ((flags & 0x80) != 0) {
        return;
    }
    if ((flags & 0x100) == 0) {
        *(unsigned int*)(body + 0x10) = flags | 0x80;
    }
    if (*(int*)(e + 0x154) != 0) {
        int rec = resolve_body(e, 0x2e4);
        (**(void (**)(int, int))(e + 0x154))(e, rec);
    }
}

// -------------------------------------------------------------------------
// 0x803633a4  grapple_aim_direction - screen aim bias -> world direction.
//   Maps the player's screen-space aim offset (aimX, aimY) into a world-space
//   direction using the camera's right/up basis (matrix at [ctx+8], rows 0x364
//   /0x370 scaled by 0x37c/0x380), adds the camera forward bias ([ctx+0xc0..c8]),
//   and normalizes when the result is longer than the minimum. When the camera
//   matrix is absent it falls back to the forward bias alone.
// -------------------------------------------------------------------------
void grapple_aim_direction(double aimX, double aimY, int ctx, float* outDir)
{
    int m = *(int*)(ctx + 8);
    if (m == 0) {
        outDir[0] = FLOAT_8061b134;
        outDir[1] = FLOAT_8061b134;
        outDir[2] = FLOAT_8061b134;
    } else {
        float sx = (float)((double)FLOAT_8061b1fc * aimX);
        float sy = (float)((double)FLOAT_8061b1fc * -aimY);
        outDir[0] = *(float*)(m + 0x370) * sx * *(float*)(m + 0x380) +
                    *(float*)(m + 0x364) * sy * *(float*)(m + 0x37c);
        m = *(int*)(ctx + 8);
        outDir[1] = *(float*)(m + 0x374) * sx * *(float*)(m + 0x380) +
                    *(float*)(m + 0x368) * sy * *(float*)(m + 0x37c);
        m = *(int*)(ctx + 8);
        outDir[2] = *(float*)(m + 0x378) * sx * *(float*)(m + 0x380) +
                    *(float*)(m + 0x36c) * sy * *(float*)(m + 0x37c);
    }

    outDir[0] += *(float*)(ctx + 0xc0);
    outDir[1] += *(float*)(ctx + 0xc4);
    outDir[2] += *(float*)(ctx + 0xc8);

    float x = outDir[0];
    float lenSq = outDir[2] * outDir[2] + x * x + outDir[1] * outDir[1];
    if (lenSq <= FLOAT_8061b200) {
        return;
    }
    // fast inverse sqrt with one Newton-Raphson step
    float rs = 1.0f / (float)math_sqrt((double)lenSq);
    rs = rs * -(FLOAT_8061b150 * lenSq * rs * rs - FLOAT_8061b1b8);
    float scale = FLOAT_8061b128 * rs * -(FLOAT_8061b150 * lenSq * rs * rs - FLOAT_8061b1b8);
    outDir[0] = x * scale;
    outDir[1] = outDir[1] * scale;
    outDir[2] = outDir[2] * scale;
}

// -------------------------------------------------------------------------
// 0x803634f8  grapple_endpoint_dir - tether muzzle endpoint + direction.
//   From the local player's aim (grapple_aim_direction) projects a point a
//   fixed reach ([ctx+0x13e4]) out from the camera origin ([ctx+0xb4..bc]) and
//   returns it in outPoint, plus the normalized muzzle->point direction in
//   outDir. Either output may be null. Wrapped in a scratch scope.
// -------------------------------------------------------------------------
void grapple_endpoint_dir(int a, int b, float* outPoint, float* outDir)
{
    (void)a; (void)b;
    float dir[3];
    int ctx = FUN_803826f0();

    grapple_aim_direction((double)*(float*)(ctx + 0xe04),
                          (double)*(float*)(ctx + 0xe08), ctx, dir);

    float reach = *(float*)(ctx + 0x13e4);
    float py = dir[1] * reach + *(float*)(ctx + 0xb8);
    float px = dir[0] * reach + *(float*)(ctx + 0xb4);
    float dy = py - *(float*)(ctx + 0xb8);
    float pz = dir[2] * reach + *(float*)(ctx + 0xbc);
    float dx = px - *(float*)(ctx + 0xb4);
    float dz = pz - *(float*)(ctx + 0xbc);
    float lenSq = dz * dz + dx * dx + dy * dy;
    float k = FLOAT_8061b150 * lenSq;
    float rs = 1.0f / (float)math_sqrt((double)lenSq);
    rs = rs * -(k * rs * rs - FLOAT_8061b1b8);
    rs = FLOAT_8061b128 * rs * -(k * rs * rs - FLOAT_8061b1b8);

    if (outPoint != (float*)0) {
        outPoint[0] = px;
        outPoint[1] = py;
        outPoint[2] = pz;
    }
    if (outDir != (float*)0) {
        outDir[0] = dx * rs;
        outDir[1] = dy * rs;
        outDir[2] = dz * rs;
    }
    FUN_8038273c();
}

// -------------------------------------------------------------------------
// 0x8036360c  grapple_closest_on_target - closest distance to target segments.
//   Builds the aim ray in the target's local space, then walks the target
//   skeleton's collision segment list (type-4 sub-parts, skipping bodies with
//   the +0xbe8 bit 0x40 set) and keeps the minimum closest-approach distance
//   between the aim segment and each bone segment. Returns that distance
//   (seeded from FLOAT_8061b204). Used to score/validate a grapple target.
// -------------------------------------------------------------------------
double grapple_closest_on_target(void)
{
    float aabbA[3], aabbB[3];
    float localMtx[31];
    float invMtx[16];
    float aimOrigin[3];   // local_d0/cc/c8
    float aimDir[3];      // local_dc/d8/d4
    float aimEnd[3];      // local_e8/e4/e0

    unsigned long long pair = FUN_803826d8();
    int ctx  = (int)(pair >> 0x20);
    int subj = (int)pair;
    int skel = *(int*)(subj + 4);
    int owner = *(int*)(subj + 0x7c);
    int* segCount = *(int**)(skel + 4);
    double best = (double)FLOAT_8061b204;
    int stride0 = *segCount;

    grapple_aim_direction((double)*(float*)(ctx + 0xe04),
                          (double)*(float*)(ctx + 0xe08), ctx, aimDir);

    aimOrigin[0] = *(float*)(ctx + 0xb4) - *(float*)(subj + 0x64);
    aimOrigin[1] = *(float*)(ctx + 0xb8) - *(float*)(subj + 0x68);
    aimOrigin[2] = *(float*)(ctx + 0xbc) - *(float*)(subj + 0x6c);

    entity_build_local_matrix(localMtx, (void*)(intptr_t)subj, 0, 0);
    mtx4_transpose(localMtx, invMtx);
    mtx4_transform_point_alias(invMtx, aimOrigin);
    mtx4_transform_point_alias(invMtx, aimDir);

    aimEnd[0] = FLOAT_8061b12c * aimDir[0] + aimOrigin[0];
    aimEnd[1] = FLOAT_8061b12c * aimDir[1] + aimOrigin[1];
    aimEnd[2] = FLOAT_8061b12c * aimDir[2] + aimOrigin[2];

    int byteOff = 0;
    int typeOff = 0;
    for (int i = 0; i < *segCount; i++) {
        if ((*(int*)(*(int*)(*(int*)(skel + 0x84) + 0x5c) + typeOff) == 4) &&
            ((*(unsigned int*)(owner + 0xbe8) & 0x40) == 0)) {
            float* seg = *(float**)((int)segCount + byteOff + 0xa0 + stride0 * -0xc0);
            if (seg != (float*)0) {
                float segA[6];
                segA[0] = seg[0]; segA[1] = seg[1]; segA[2] = seg[2];
                segA[3] = seg[3]; segA[4] = seg[4]; segA[5] = seg[5];
                mtx4_transform_point_alias(
                    (float*)(*(int*)(skel + 0x10) + (unsigned int)*(unsigned short*)(seg + 7) * 0x40),
                    &segA[0]);
                mtx4_transform_point_alias(
                    (float*)(*(int*)(skel + 0x10) + (unsigned int)*(unsigned short*)((int)seg + 0x1e) * 0x40),
                    &segA[3]);
                double d = closest_between_segments(aimOrigin, aimEnd, &segA[0], &segA[3],
                                                    aabbA, aabbB);
                if (d < best) {
                    best = d;
                }
            }
        }
        byteOff += 0xc0;
        typeOff += 4;
    }
    FUN_80382724();
    return best;
}

// -------------------------------------------------------------------------
// 0x80363804  grapple_acquire_target - pick a grapple target via target_scan.
//   Installs the four grapple scan callbacks, runs a screen-space entity scan
//   biased by the player's aim, and on a hit records the target's reach factor
//   (+0x13e4) and body id (+0x13e8). For item targets (type 8) with a live
//   sub-object it additionally computes the closest-segment distance (+0x13ec)
//   and, when out of grab range and not otherwise grabbable, rejects the hit.
//   Returns 1 on a valid target, 0 otherwise.
// -------------------------------------------------------------------------
int grapple_acquire_target(int player)
{
    int hitList[3];
    int hitDist;

    *(int*)(player + 0x13f0) = 0;
    target_scan_set_callbacks((unsigned int)(uintptr_t)&grapple_scan_filter_cb,
                              (unsigned int)(uintptr_t)&grapple_scan_bounds_cb,
                              0, 0x80363e3c,
                              (unsigned int)(uintptr_t)&grapple_scan_los_cb);

    hitList[0] = 0;
    int count = ((ScreenScanFn)&target_scan_screen_entities)(
        0,
        (double)*(float*)(player + 0xe04), (double)*(float*)(player + 0xe08),
        (double)FLOAT_8061b208, (double)FLOAT_8061b208,
        hitList, &hitDist, 0);

    if (count == 0) {
        target_scan_set_callbacks(0, 0, 0, 0, 0);
        return 0;
    }

    int body = 0;
    if (*(int*)(hitList[0] + 0x20) == 8) {
        body = *(int*)(hitList[0] + 0x7c);
    }

    if ((body == 0) || (*(int*)(body + 0x44) != 1)) {
        *(int*)(player + 0x13ec) = -1;
    } else {
        *(int*)(player + 0x13ec) = (int)grapple_closest_on_target();
    }

    if ((body != 0) && (*(int*)(player + 0x13ec) < 0) &&
        (FUN_80364c40((int*)(body + 0x4c)) == 0)) {
        unsigned int flags = 0;
        if (*(int*)(body + 0xbf0) != 0) {
            flags = *(unsigned int*)(*(int*)(body + 0xbf0) + 0x58);
        }
        if ((flags & 0x400000) == 0) {
            target_scan_set_callbacks(0, 0, 0, 0, 0);
            return 0;
        }
    }

    target_scan_set_callbacks(0, 0, 0, 0, 0);
    *(int*)(player + 0x13e4) = hitDist;
    *(int*)(player + 0x13e8) = *(int*)(hitList[0] + 0x2c);
    return 1;
}

// -------------------------------------------------------------------------
// 0x8036396c  grapple_scan_los_cb - target_scan line-of-sight raycast callback.
//   For prop/mover/door target types (0x40/0x800/0x20000) temporarily clears
//   the object's contact flags and raycasts from the camera to the object's
//   grab anchor; returns 1 if the anchor is reachable (no blocker). For other
//   types it raycasts to the camera focus point and returns 1 only if the first
//   thing hit is the candidate itself.
// -------------------------------------------------------------------------
unsigned int grapple_scan_los_cb(int param_1, int obj)
{
    (void)param_1;
    float anchor[3];
    int type = *(int*)(obj + 0x20);

    if ((type == 0x40) || (type == 0x800) || (type == 0x20000)) {
        unsigned int savedFlags = *(unsigned int*)(obj + 0x28) & 0x60;
        *(unsigned int*)(obj + 0x28) &= ~savedFlags;
        anchor[0] = (*(float*)(obj + 0x64) + *(float*)(obj + 0x1a8)) - *(float*)(DAT_80611d54 + 0x27c);
        anchor[1] = (*(float*)(obj + 0x68) + *(float*)(obj + 0x1ac)) - *(float*)(DAT_80611d54 + 0x280);
        anchor[2] = (*(float*)(obj + 0x6c) + *(float*)(obj + 0x1b0)) - *(float*)(DAT_80611d54 + 0x284);
        int blocked = ((RaycastFn)&scene_raycast)(
            *(int*)(DAT_80611d54 + 0x2a0), (float*)(DAT_80611d54 + 0x27c),
            anchor, 0, 0, 0, 0, 0x2010070, 0x18, 0);
        *(unsigned int*)(obj + 0x28) |= savedFlags;
        return (blocked == 0) ? 1u : 0u;
    }

    int hit = ((RaycastFn)&scene_raycast)(
        *(int*)(DAT_80611d54 + 0x2a0), (float*)(DAT_80611d54 + 0x27c),
        (float*)(DAT_80611d54 + 0x334), 1, 0, 0, 0, 0x2010030, 0x18, 0);
    return (unsigned int)(hit == obj);
}

// -------------------------------------------------------------------------
// 0x80363ac8  grapple_scan_bounds_cb - target_scan screen-bounds hit test.
//   Projects the candidate entity's screen AABB (expanding it by half for
//   prop/mover/door types) and returns 1 when the aim screen point (aimX,aimY)
//   lies inside that box.
// -------------------------------------------------------------------------
unsigned int grapple_scan_bounds_cb(double aimX, double aimY, int param_3, int obj)
{
    (void)param_3;
    unsigned int minX[13];   // local_58[]
    unsigned int minY, maxX, maxY;
    float depth;

    ((ScreenBoundsFn)&entity_compute_screen_bounds)(obj, minX, &minY, &maxX, &maxY, &depth, 0);

    int type = *(int*)(obj + 0x20);
    if ((type == 0x40) || (type == 0x800) || (type == 0x20000)) {
        int hx = (int)(maxX - minX[0]) >> 1;
        int hy = (int)(maxY - minY) >> 1;
        minX[0] -= hx;  maxX += hx;
        minY   -= hy;  maxY += hy;
    }

    float fMinX = (float)((double)CONCAT44(0x43300000, minX[0] ^ 0x80000000) - DOUBLE_8061b138);
    float fMaxX = (float)((double)CONCAT44(0x43300000, maxX    ^ 0x80000000) - DOUBLE_8061b138);
    float fMinY = (float)((double)CONCAT44(0x43300000, minY    ^ 0x80000000) - DOUBLE_8061b138);
    float fMaxY = (float)((double)CONCAT44(0x43300000, maxY    ^ 0x80000000) - DOUBLE_8061b138);

    if (((double)fMinX <= aimX) && (aimX <= (double)fMaxX) &&
        ((double)fMinY <= aimY) && (aimY <= (double)fMaxY)) {
        return 1;
    }
    return 0;
}

// -------------------------------------------------------------------------
// 0x80363c38  grapple_scan_filter_cb - target_scan candidate eligibility filter.
//   Gates which object types the grapple may lock onto. Requires the active
//   session record (DAT_80611d74) to be in the grapple game mode (+0x114 == 2)
//   with the wielder holding the grapple weapon (weapon id 0x38), then filters
//   by object type: characters (1) and skeletal props (0x80) are always
//   eligible; items (8) require the "grabbable" body flag; physics props (0x40)
//   require a matching material class; ragdolls (0x400000), boxes (0x200/0x800)
//   pass; type-4 collision parts are rejected. Returns 1 if eligible.
// -------------------------------------------------------------------------
unsigned int grapple_scan_filter_cb(int param_1, int obj)
{
    (void)param_1;

    if ((DAT_80611d74 == 0) || (*(int*)(DAT_80611d74 + 0x114) != 2)) {
        return 0;
    }
    if (*(int*)(*(int*)(*(int*)(DAT_80611d74 + 0x14) + 0x7c) + 0x94) != 0x38) {
        return 0;
    }

    int type = *(int*)(obj + 0x20);
    if ((type != 0x40) && (type != 8)) {
        unsigned int handle;
        if (((*(unsigned int*)(*(int*)(obj + 0x24) + 0x28) & 0x100000) == 0) &&
            ((*(unsigned int*)(obj + 0x80) == 0) ||
             (handle = subobj_get_flags(*(unsigned int*)(obj + 0x80)), (handle & 0x800000) == 0))) {
            return 0;
        }
        if (((*(unsigned int*)(*(int*)(obj + 0x24) + 0x28) & 0x400000) != 0) &&
            (*(float*)(obj + 0x1cc) <= FLOAT_8061b134) &&
            (FLOAT_8061b134 < *(float*)(obj + 0x1d8))) {
            return 0;
        }
    }

    if ((*(unsigned int*)(obj + 0x80) != 0) &&
        (subobj_get_flags(*(unsigned int*)(obj + 0x80)) & 0x400000) != 0) {
        return 0;
    }

    switch (type) {
        case 0x80:      // skeletal prop
        case 1:         // character
        case 0x200:     // box
        case 0x800:     // crate/mover
        case 0x400000:  // ragdoll
            return 1;
        case 8: {       // item/pickup: needs the "throwable" body flag (bit 0x16)
            int body = *(int*)(*(int*)(obj + 0x7c) + 0xbf0);
            unsigned int f = (body == 0) ? 0u : *(unsigned int*)(body + 0x58);
            return (f >> 0x16) & 1;
        }
        case 4:         // raw collision part
            return 0;
        case 0x40: {    // physics prop: only certain material classes
            int cls = *(int*)(*(int*)(obj + 0x7c) + 0xc);
            if ((cls != 2) && (cls != 8) && (cls != 0x10) && (cls != 0x40)) {
                return 0;
            }
            return 1;
        }
        case 0x20000:   // door/mover: needs sub-flag bit 6
            return *(unsigned int*)(*(int*)(obj + 0x7c) + 0x10) >> 6 & 1;
        default:
            return (unsigned int)(*(int*)(obj + 0x80) != 0);
    }
}

// -------------------------------------------------------------------------
// 0x80363ee0  grapple_detonate - forwarder to the detonation implementation.
// -------------------------------------------------------------------------
void grapple_detonate(void)
{
    grapple_detonate_impl();
}

// -------------------------------------------------------------------------
// 0x80363f08  grapple_detonate_impl - blow up the grabbed target.
//   Resolves the current grabbed subject (item body or a resolved handle),
//   picks a detonation normal (default DAT_803e0c28..30, or the sub-object
//   surface normal when the subject carries the "surface-align" flag 0x1000000
//   and the impact hits the front face), spawns the explosion effect
//   (player_ai_explosion_spawn type 0x12/0x17), and destroys or detaches the
//   subject. Skips the blast if the sub-object is locked or the hit is on the
//   back face. Wrapped in a scratch scope.
// -------------------------------------------------------------------------
void grapple_detonate_impl(void)
{
    float impactPos[12];   // afStack_30
    float local[3];        // local_48/44/40
    float normal[3];       // local_3c/38/34

    unsigned long long pair = FUN_803826e8();
    unsigned int* subj = (unsigned int*)(uintptr_t)(pair >> 0x20);
    float* hitDir = (float*)(uintptr_t)pair;
    int body = 0;
    int detachTarget = 0;

    normal[0] = DAT_803e0c28;
    normal[1] = DAT_803e0c2c;
    normal[2] = DAT_803e0c30;

    if (subj[8] == 8) {
        detachTarget = subj[0x1f];
        body = *(int*)(detachTarget + 0x4c);
    } else if (subj[8] == 0x20000000) {
        body = subj[0x1f];
        unsigned int h = *(unsigned int*)(body + 0x44);
        int rec = 0;
        if (h != 0) {
            rec = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int*)(rec + 0x2c) != h) {
                rec = 0;
                *(int*)(body + 0x44) = 0;
            }
        }
        if ((rec != 0) && (*(int*)(rec + 0x20) == 8)) {
            detachTarget = *(int*)(rec + 0x7c);
        }
    }

    if (body != 0) {
        if (FUN_80365914(body) != 0) {
            FUN_80382734();
            return;
        }
        unsigned int sub = FUN_80365bb0(body, impactPos);
        if ((subobj_get_flags(sub) & 0x1000000) == 0) {
            FUN_80382734();
            return;
        }
        if (hitDir != (float*)0) {
            subobj_world_to_local_dir(sub, hitDir, local);
            normal[0] = hitDir[3];
            normal[1] = hitDir[4];
            normal[2] = hitDir[5];
            if (local[2] * hitDir[5] + local[0] * hitDir[3] + local[1] * hitDir[4] < FLOAT_8061b128) {
                FUN_80382734();
                return;
            }
        }
    }

    player_ai_explosion_spawn(0x12, 0x17, impactPos, normal, 0, 0, 1);
    if (detachTarget == 0) {
        entity_destroy_with_event((int*)subj);
    } else {
        FUN_8036589c();
    }
    FUN_80382734();
}
