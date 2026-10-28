// =============================================================================
// aiactor_grab_melee.cpp - AI actor "grab / melee-drag" behaviour cluster
// (batch 794).
//
// This is the melee grab-and-drag behaviour used by close-combat AI actors
// (dispatched from tracker_behavior_state_dispatch). The actor latches onto a
// visible teammate/target, drags it toward itself with an animation-driven
// position lerp, and applies melee damage when the two are close enough. A
// co-op variant gate and a proximity damage sweep round out the group.
//
//   - aiactor_behavior_grab_drag_tick (0x802c3e10): the per-frame grab tick.
//     Resolves the current frame actor + its control record (rec @ actor+0x7c),
//     acquires a visible teammate, then runs a small state machine on the mode
//     field (rec+0x20): mode 2 = drag toward the grab anchor with a swept
//     rsqrt-normalised pull step and (once the grab clip finishes / flags) apply
//     damage via aiactor_apply_melee_damage; mode 0 = play the grab-start clip
//     and seed the drag timer; mode 1 = pull the victim along the zone path with
//     a facing-relative offset, spin it about the actor's facing, and run the
//     proximity sweep. State 3/4 = wind-down (play the release clip, snap back to
//     the hold state when out of range).
//   - aiactor_play_grab_impact_sound (0x802c4894): fire a randomised grab-impact
//     sound (id 0x5dd + rng%6) positioned at a world point.
//   - aiactor_coop_grab_state_gate (0x802c4910): co-op event gate
//     (tracker_coop_event_dispatch). For event 2 returns -1/-2 depending on the
//     current mode; for events 0<e<2 kicks the actor into the release state
//     (mode 3) and emits the impact sound, returning -1. Otherwise returns 0.
//   - aiactor_grab_proximity_melee_sweep (0x802c49b0): once the drag timer is up,
//     scan every active bot; anything within the inner radius takes melee damage
//     (aiactor_apply_melee_damage) and resets the timer, anything within the
//     outer radius (and not already latched) triggers a one-shot "near" sound
//     (id 0x5d9 + rng-scaled).
//   - aiactor_apply_melee_damage (0x802c4b28): build a damage descriptor from a
//     reset switch slot + the attacker instance, dispatch it to the victim via
//     damage_dispatch_to_entity, and flash the attached weather/impact effect.
//
// AI actor control record at (actor+0x7c) ("rec"), int- and byte-indexed:
//   rec+0x04 target/victim instance   rec+0x08 owning group
//   rec+0x20 mode   rec+0x9d0 queued mode   rec+0x9d4 prev mode
//   rec+0x9d8 mode timer   rec+0x9dc near-sound latch   rec+0x9e0 spin phase
//   rec+0x2c status bits (0x80 = timed, 0x100000 = damage-done)
//   rec+0x30..0x38 grab anchor pos   rec+0xa74..0xa7c cached last pos
//   rec+0x678 zone id   rec+0x684 fallback zone   rec+0xa90 hold timer
//   rec+0xb30 drag timer   rec+0xb34 target-visible timer
//
// Address range: 0x802c3e10 - 0x802c4c00
// Subsystem: game (AI actor melee-grab behaviour)
//
// NON-MATCHING: the PPC frame register-save/restore helpers (FUN_803826xx /
// FUN_8038273xx) are compiler prologue/epilogue stubs; the value Ghidra reads
// back from them is really the current frame actor, expressed here through
// aiactor_current_frame_actor(). The dropped FP-register spill args on the drag
// tick are frame junk (the damage helper re-reads the geometry). The rsqrt
// refinement uses a self-contained SQRT. CONCAT44 int->double bias is reduced to
// a plain (float)(int) conversion. Helpers are file-local C-linkage matched to
// the call sites.
// =============================================================================

#include "common.h"

// Ghidra SQRT intrinsic (single-precision Newton-Raphson).
static inline float SQRT(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x;
    for (int i = 0; i < 6; i = i + 1) g = 0.5f * (g + x / g);
    return g;
}

extern "C" {

// --- current-frame AI actor (PPC frame-helper idiom) -------------------------
extern int aiactor_current_frame_actor(void);

// --- AI actor helpers (named cross-module) -----------------------------------
extern void  aiactor_facing_update(void);
extern void  aiactor_follow_zone_path(void);
extern int   aiactor_retarget_visible_teammate(void);
extern void  aiactor_update_aim_height(void);

// --- animation helpers -------------------------------------------------------
extern int    anim_is_clip_active(int actor, int clip);
extern void   anim_play_scaled(double a, double b);
extern int    anim_primary_clip_userflags(int actor, int clip);
extern int    anim_slot_is_primary_clip(int actor, int clip);
extern double anim_get_clip_end_time_of(int actor);

// --- math / misc helpers -----------------------------------------------------
extern void   angles_to_dir(double a, double b, float* out);
extern double math_poly_trig(double x);
extern unsigned int rng_counter_tick(void);
extern int    soundpad_find_nearest_entity(double a, float* pos);
extern void   character_nametag_anim_init(void);
extern float* entity_get_world_position(int actor);

// --- sound request builder ---------------------------------------------------
extern void sound_init_play_params(int params);
extern void sound_play_request(int* params);

// --- damage / effect helpers -------------------------------------------------
extern unsigned long long switch_slot_reset(int slot);
extern void damage_dispatch_to_entity(unsigned long long slot, double a, double b,
                                       unsigned long long p4, unsigned long long p5,
                                       unsigned long long p6, unsigned long long p7,
                                       unsigned long long p8, int* desc);
extern void weather_particle_trigger_flash(int fx);

// --- raw globals -------------------------------------------------------------
extern int   DAT_80611d78;    // player-entity table base (player @ +0x14, stride)
extern int   DAT_80612610;    // active-bot count
extern int   DAT_806120f8;    // "world" owning-group sentinel
extern unsigned int DAT_806109c0;  // int->float CONCAT44 low-word bias source

// --- tuning constants (from the 0x80619d.. / 0x8061.. float pool) ------------
extern const float FLOAT_80619d98;   // 0.0f
extern const float FLOAT_80619d9c;   // frame dt / unit
extern const float FLOAT_80619da0;   // spin base phase
extern const float FLOAT_80619da4;   // rng normaliser
extern const float FLOAT_80619da8;   // drag-timer threshold
extern const float FLOAT_80619dac;   // path-follow distance^2 gate
extern const float FLOAT_80619db0;   // clip play rate B
extern const float FLOAT_80619db4;   // min length^2 for normalise
extern const float FLOAT_80619db8;   // rsqrt refine coeff
extern const float FLOAT_80619dbc;   // rsqrt refine bias (1.5)
extern const float FLOAT_80619dc0;   // pull speed scale
extern const float FLOAT_80619dc4;   // clip end-time offset
extern const float FLOAT_80619dc8;   // min end-time delta
extern const float FLOAT_80619dcc;   // finish length^2 gate
extern const float FLOAT_80619dd0;   // grab damage amount
extern const float FLOAT_806108ec;   // per-frame scale (60fps step)
extern const float FLOAT_80619de8;   // inner radius^2 (damage gate lower bound)
extern const float FLOAT_80619dec;   // outer radius^2 (near-sound upper bound)
extern const double DOUBLE_80619dd8; // 2^52 int->double bias
extern const double DOUBLE_80619de0; // 2^52 int->double bias (signed)

// basis vector globals for the victim-spin cross product
extern const float DAT_803e0810;
extern const float DAT_803e0814;
extern const float DAT_803e0818;

// forward within this file
static void aiactor_play_grab_impact_sound(float* pos);
static void aiactor_grab_proximity_melee_sweep(void);
static void aiactor_apply_melee_damage(double amount, double dx, double dz,
                                       int attacker, int victim);

// -----------------------------------------------------------------------------
// aiactor_play_grab_impact_sound (0x802c4894)
// Fire a randomised grab-impact sound (id 0x5dd + rng%6) at a world point.
// -----------------------------------------------------------------------------
static void aiactor_play_grab_impact_sound(float* pos)
{
    unsigned int r;
    int params[7];
    unsigned int px, py, pz;
    unsigned char flag;

    r = rng_counter_tick();
    sound_init_play_params((int)params);
    flag = 1;                       // params[6] low byte: positional
    px = *(unsigned int*)&pos[0];
    py = *(unsigned int*)&pos[1];
    pz = *(unsigned int*)&pos[2];
    params[3] = (int)px;
    params[4] = (int)py;
    params[5] = (int)pz;
    *((unsigned char*)&params[6]) = flag;
    params[0] = (int)(r % 6 + 0x5dd);
    sound_play_request(params);
}

// -----------------------------------------------------------------------------
// aiactor_coop_grab_state_gate (0x802c4910)
// Co-op event gate driven by tracker_coop_event_dispatch.
//   event 2      -> -1 if mode==2 else -2
//   0 < event <2 -> if not already grabbing/releasing, play impact sound and
//                   enter the release state (mode 3); return -1
//   otherwise    -> 0
// -----------------------------------------------------------------------------
unsigned int aiactor_coop_grab_state_gate(int actor, int event)
{
    unsigned int result;
    float* worldPos;
    int rec;

    result = 0;
    rec = *(int*)(actor + 0x7c);
    if (event == 2) {
        if (*(int*)(rec + 0x20) == 2)
            result = 0xffffffff;    // -1
        else
            result = 0xfffffffe;    // -2
    } else if (event < 2 && event > 0) {
        if (*(int*)(rec + 0x20) != 2) {
            if (*(int*)(rec + 0x20) != 3) {
                worldPos = entity_get_world_position(actor);
                aiactor_play_grab_impact_sound(worldPos);
            }
            *(float*)(rec + 0xb30) = FLOAT_80619d98;
            if (*(int*)(rec + 0x20) != 3)
                *(int*)(rec + 0x9d0) = 3;
        }
        result = 0xffffffff;        // -1
    }
    return result;
}

// -----------------------------------------------------------------------------
// aiactor_apply_melee_damage (0x802c4b28)
// Build a damage descriptor (reset switch slot + attacker instance), dispatch it
// to the victim, and flash the attached impact/weather effect if present.
// -----------------------------------------------------------------------------
static void aiactor_apply_melee_damage(double amount, double dx, double dz,
                                       int attacker, int victim)
{
    unsigned long long slot;
    int desc[2];        // desc[0] victim, desc[1..2] attacker x2
    int attackerA;
    int attackerB;
    int extraA;
    int extraB;
    float amt;

    (void)dx; (void)dz;
    slot = switch_slot_reset((int)&desc[0]);
    amt = (float)amount;
    (void)amt;
    extraA = 0;
    extraB = 0;
    desc[0] = victim;
    attackerA = attacker;
    attackerB = attacker;
    // descriptor layout: [victim][attacker][attacker][0][0][amount]
    {
        int fullDesc[6];
        fullDesc[0] = victim;
        fullDesc[1] = attackerA;
        fullDesc[2] = attackerB;
        fullDesc[3] = extraA;
        fullDesc[4] = extraB;
        *(float*)&fullDesc[5] = (float)amount;
        damage_dispatch_to_entity(slot, dx, dz, 0, 0, 0, 0, 0, fullDesc);
    }
    if (*(int*)(victim + 0x150) != 0)
        weather_particle_trigger_flash(*(int*)(*(int*)(victim + 0x150) + 4));
}

// -----------------------------------------------------------------------------
// aiactor_grab_proximity_melee_sweep (0x802c49b0)
// Once the drag timer is up, scan every active bot: inner-radius bots take melee
// damage and reset the timer; outer-radius bots (not already latched) fire a
// one-shot "near" sound (id 0x5d9 + rng-scaled).
// -----------------------------------------------------------------------------
static void aiactor_grab_proximity_melee_sweep(void)
{
    int actor;
    int rec;
    int i;
    int stride;
    int bot;
    float dy, dist2;
    double ddx, ddz;
    int params[7];
    unsigned int rngWord;
    int soundIdx;

    actor = aiactor_current_frame_actor();
    rec = *(int*)(actor + 0x7c);
    if (FLOAT_80619da8 <= *(float*)(rec + 0xb30)) {
        stride = 0;
        for (i = 0; i < DAT_80612610; i = i + 1) {
            bot = *(int*)(DAT_80611d78 + stride + 0x14);
            if ((float)FLOAT_80619d98 < *(float*)(bot + 0x1cc)) {
                dy   = *(float*)(actor + 0x68) - *(float*)(bot + 0x68);
                ddx  = (double)(*(float*)(actor + 100) - *(float*)(bot + 100));
                ddz  = (double)(*(float*)(actor + 0x6c) - *(float*)(bot + 0x6c));
                dist2 = (float)(ddz * ddz + (double)(float)(ddx * ddx + (double)(dy * dy)));
                if (FLOAT_80619de8 <= dist2) {
                    if (*(int*)(rec + 0x9dc) == 0 && dist2 < FLOAT_80619dec) {
                        rngWord = rng_counter_tick();
                        soundIdx = (int)(FLOAT_80619dd0 *
                            (((float)((double)(int)rngWord) - (float)DOUBLE_80619de0) /
                             FLOAT_80619da4) + FLOAT_80619db8);
                        sound_init_play_params((int)params);
                        *((unsigned char*)&params[6]) = 1;
                        params[3] = *(int*)(actor + 100);
                        params[4] = *(int*)(actor + 0x68);
                        params[5] = *(int*)(actor + 0x6c);
                        params[0] = soundIdx + 0x5d9;
                        sound_play_request(params);
                        *(int*)(rec + 0x9dc) = 1;
                    }
                } else {
                    aiactor_apply_melee_damage((double)FLOAT_80619da8, ddz, ddx, actor, bot);
                    *(float*)(rec + 0xb30) = FLOAT_80619d98;
                }
            }
            stride = stride + 0x141c;
        }
    }
}

// -----------------------------------------------------------------------------
// aiactor_behavior_grab_drag_tick (0x802c3e10)
// Per-frame grab/drag behaviour tick. See file header for the state machine.
// -----------------------------------------------------------------------------
void aiactor_behavior_grab_drag_tick(void)
{
    int actor;
    int rec;
    int visibleMate;
    int mode;
    int r;
    bool grabFinished;
    float dx, dy, dz, len2, rsq, inv;
    float local[3];     // scratch pull vector / basis

    actor = aiactor_current_frame_actor();
    rec = *(int*)(actor + 0x7c);
    visibleMate = aiactor_retarget_visible_teammate();

    // commit any queued mode change
    if (-1 < *(int*)(rec + 0x9d0)) {
        if (*(int*)(rec + 0x9d0) != *(int*)(rec + 0x20)) {
            *(float*)(rec + 0x9d8) = FLOAT_80619d98;
            *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
            *(int*)(rec + 0x20) = *(int*)(rec + 0x9d0);
        }
        *(int*)(rec + 0x9d0) = -1;
    }

    // advance the hold timer only while the "timed" status bit is set
    if ((*(unsigned int*)(rec + 0x2c) & 0x80) == 0) {
        *(float*)(rec + 0xa90) = FLOAT_80619d98;
    } else {
        *(float*)(rec + 0xa90) =
            FLOAT_806108ec * (FLOAT_80619d9c /
                (float)((double)(int)(DAT_806109c0 ^ 0x80000000u) - DOUBLE_80619dd8)) +
            *(float*)(rec + 0xa90);
    }

    if (*(int*)(rec + 4) != 0) {
        // target-visible timer
        if (visibleMate == 0) {
            *(float*)(rec + 0xb34) = FLOAT_80619d98;
            grabFinished = false;
        } else {
            grabFinished = FLOAT_80619d9c < *(float*)(rec + 0xb34);
        }

        mode = *(int*)(rec + 0x20);
        if (mode == 2) {
            // ---- mode 2: drag victim toward the grab anchor -----------------
            grabFinished = false;
            if (anim_is_clip_active(actor, 0x24b) == 0)
                anim_play_scaled((double)FLOAT_80619db0, (double)FLOAT_80619d9c);

            *(int*)(rec + 0x30) = *(int*)(*(int*)(rec + 4) + 100);
            *(int*)(rec + 0x34) = *(int*)(*(int*)(rec + 4) + 0x68);
            *(int*)(rec + 0x38) = *(int*)(*(int*)(rec + 4) + 0x6c);
            *(int*)(rec + 0xa74) = *(int*)(actor + 100);
            *(int*)(rec + 0xa78) = *(int*)(actor + 0x68);
            *(int*)(rec + 0xa7c) = *(int*)(actor + 0x6c);

            dx = *(float*)(rec + 0x30) - *(float*)(actor + 100);
            dy = *(float*)(rec + 0x34) - *(float*)(actor + 0x68);
            dz = *(float*)(rec + 0x38) - *(float*)(actor + 0x6c);

            if (anim_slot_is_primary_clip(actor, 0x24b) == 0) {
                // constant-speed pull
                len2 = dz * dz + dx * dx + dy * dy;
                if (FLOAT_80619db4 < len2) {
                    inv = 1.0f / SQRT(len2);
                    inv = inv * -(FLOAT_80619db8 * len2 * inv * inv - FLOAT_80619dbc);
                    float step = FLOAT_80619d9c * inv;
                    dx = dx * step;
                    dy = dy * step;
                    dz = dz * step;
                }
                float sc = FLOAT_80619dc0 *
                    (FLOAT_80619d9c /
                     (float)((double)(int)(DAT_806109c0 ^ 0x80000000u) - DOUBLE_80619dd8)) *
                    FLOAT_806108ec;
                *(float*)(actor + 100)  = dx * sc + *(float*)(actor + 100);
                *(float*)(actor + 0x68) = dy * sc + *(float*)(actor + 0x68);
                *(float*)(actor + 0x6c) = dz * sc + *(float*)(actor + 0x6c);
            } else {
                // time-scaled pull driven by the clip end-time
                double endT = anim_get_clip_end_time_of(actor) - (double)FLOAT_80619dc4;
                len2 = dz * dz + dx * dx + dy * dy;
                double rem = endT - (double)*(float*)(*(int*)(actor + 4) + 0xd4);
                if (FLOAT_80619db4 < len2) {
                    inv = 1.0f / SQRT(len2);
                    inv = inv * -(FLOAT_80619db8 * len2 * inv * inv - FLOAT_80619dbc);
                    float step = FLOAT_80619d9c * inv;
                    dx = dx * step;
                    dy = dy * step;
                    dz = dz * step;
                }
                if ((double)FLOAT_80619dc8 <= ABS(rem)) {
                    float sc = (FLOAT_80619d9c /
                        (float)((double)(int)(DAT_806109c0 ^ 0x80000000u) - DOUBLE_80619dd8)) *
                        len2 * FLOAT_806108ec;
                    *(float*)(actor + 100)  = (float)((double)(dx * sc) / rem) + *(float*)(actor + 100);
                    *(float*)(actor + 0x68) = (float)((double)(dy * sc) / rem) + *(float*)(actor + 0x68);
                    *(float*)(actor + 0x6c) = (float)((double)(dz * sc) / rem) + *(float*)(actor + 0x6c);
                }
                if (len2 < FLOAT_80619dcc || rem < (double)FLOAT_80619d98 ||
                    ABS(rem) < (double)FLOAT_80619dc8)
                    grabFinished = true;
            }

            aiactor_facing_update();
            if (grabFinished || anim_primary_clip_userflags(actor, 0x24b) != 0) {
                aiactor_apply_melee_damage((double)FLOAT_80619dd0, 0.0, 0.0,
                                           actor, *(int*)(rec + 4));
                *(unsigned int*)(rec + 0x2c) = *(unsigned int*)(rec + 0x2c) | 0x100000;
            }
        } else if (mode < 2) {
            if (mode == 0) {
                // ---- mode 0: play grab-start clip, seed drag timer ----------
                character_nametag_anim_init();
                *(unsigned int*)(actor + 0x28) = *(unsigned int*)(actor + 0x28) & 0xffffffbf;
                *(unsigned int*)(rec + 0x28)   = *(unsigned int*)(rec + 0x28) | 0x1000000;
                unsigned int seed = rng_counter_tick();
                *(float*)(rec + 0x9e0) = FLOAT_80619da0 *
                    (((float)((double)(int)(seed ^ 0x80000000u) - DOUBLE_80619de0)) / FLOAT_80619da4);
                *(int*)(rec + 0x9dc) = 0;
                *(float*)(rec + 0xb30) = FLOAT_80619da8;
                if (*(int*)(rec + 0x20) != 1) {
                    *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                    *(int*)(rec + 0x20) = 1;
                    *(float*)(rec + 0x9d8) = FLOAT_80619d98;
                    *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                }
                if (anim_is_clip_active(actor, 0x249) == 0)
                    anim_play_scaled((double)FLOAT_80619d98, (double)FLOAT_80619d9c);
                *(int*)(rec + 0xa74) = *(int*)(actor + 100);
                *(int*)(rec + 0xa78) = *(int*)(actor + 0x68);
                *(int*)(rec + 0xa7c) = *(int*)(actor + 0x6c);
            } else {
                // ---- mode 1: pull victim along the zone path + spin ---------
                if (anim_is_clip_active(actor, 0x249) == 0)
                    anim_play_scaled((double)FLOAT_80619d98, (double)FLOAT_80619d9c);

                r = *(int*)(*(int*)(*(int*)(rec + 4) + 0x7c) + 0x678);
                if (*(int*)(rec + 0x684) != r) {
                    if (r < 0)
                        *(int*)(rec + 0x684) =
                            *(int*)(*(int*)(*(int*)(DAT_80611d78 + 0x14) + 0x7c) + 0x678);
                    else
                        *(int*)(rec + 0x684) = r;
                    *(int*)(rec + 0x9ac) = 0;
                    *(int*)(rec + 0x9a8) = 0;
                }

                float px = *(float*)(actor + 100);
                float pz = *(float*)(actor + 0x6c);
                *(int*)(actor + 100)  = *(int*)(rec + 0xa74);
                *(int*)(actor + 0x68) = *(int*)(rec + 0xa78);
                *(int*)(actor + 0x6c) = *(int*)(rec + 0xa7c);
                float vpx = *(float*)(*(int*)(rec + 4) + 100);
                px = px - vpx;
                pz = pz - *(float*)(*(int*)(rec + 4) + 0x6c);
                if (FLOAT_80619dac <= px * px + pz * pz) {
                    if (grabFinished || *(int*)(rec + 0x684) == *(int*)(rec + 0x678)) {
                        *(float*)(rec + 0x30) = vpx;
                        *(int*)(rec + 0x34) = *(int*)(*(int*)(rec + 4) + 0x68);
                        *(int*)(rec + 0x38) = *(int*)(*(int*)(rec + 4) + 0x6c);
                        if (*(int*)(rec + 8) == DAT_806120f8) {
                            *(int*)(rec + 0x678) =
                                soundpad_find_nearest_entity((double)FLOAT_80619d98,
                                                             (float*)(actor + 100));
                        }
                    } else {
                        aiactor_follow_zone_path();
                    }
                } else {
                    *(float*)(rec + 0x30) = vpx;
                    *(int*)(rec + 0x34) = *(int*)(*(int*)(rec + 4) + 0x68);
                    *(int*)(rec + 0x38) = *(int*)(*(int*)(rec + 4) + 0x6c);
                    anim_play_scaled((double)FLOAT_80619db0, (double)FLOAT_80619d9c);
                    if (*(int*)(rec + 0x20) != 2) {
                        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                        *(int*)(rec + 0x20) = 2;
                        *(float*)(rec + 0x9d8) = FLOAT_80619d98;
                        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                    }
                }

                // pull-vector normalise
                local[0] = *(float*)(rec + 0x30) - *(float*)(actor + 100);
                local[1] = *(float*)(rec + 0x34) - *(float*)(actor + 0x68);
                local[2] = *(float*)(rec + 0x38) - *(float*)(actor + 0x6c);
                len2 = local[2] * local[2] + local[0] * local[0] + local[1] * local[1];
                if (FLOAT_80619db4 < len2) {
                    inv = 1.0f / SQRT(len2);
                    inv = inv * -(FLOAT_80619db8 * len2 * inv * inv - FLOAT_80619dbc);
                    float step = FLOAT_80619d9c * inv;
                    local[0] = local[0] * step;
                    local[1] = local[1] * step;
                    local[2] = local[2] * step;
                }
                float sc = FLOAT_80619dc0 *
                    (FLOAT_80619d9c /
                     (float)((double)(int)(DAT_806109c0 ^ 0x80000000u) - DOUBLE_80619dd8)) *
                    FLOAT_806108ec;
                *(float*)(actor + 100)  = local[0] * sc + *(float*)(actor + 100);
                *(float*)(actor + 0x68) = local[1] * sc + *(float*)(actor + 0x68);
                *(float*)(actor + 0x6c) = local[2] * sc + *(float*)(actor + 0x6c);
                *(int*)(rec + 0xa74) = *(int*)(actor + 100);
                *(int*)(rec + 0xa78) = *(int*)(actor + 0x68);
                *(int*)(rec + 0xa7c) = *(int*)(actor + 0x6c);

                aiactor_facing_update();

                // spin the victim about the actor's facing direction
                angles_to_dir((double)FLOAT_80619d98, (double)*(float*)(actor + 0x70), &local[0]);
                float bx = DAT_803e0810, by = DAT_803e0814, bz = DAT_803e0818;
                float crossX = local[2] * bx - local[0] * bz;
                float crossY = local[1] * bz - local[2] * by;
                float crossZ = local[0] * by - local[1] * bx;
                double s0 = math_poly_trig((double)(*(float*)(rec + 0x9e0) - FLOAT_80619da0));
                *(float*)(actor + 100)  = (float)((double)crossY * s0 + (double)*(float*)(actor + 100));
                double s1 = math_poly_trig((double)(*(float*)(rec + 0x9e0) - FLOAT_80619da0));
                *(float*)(actor + 0x68) = (float)((double)crossX * s1 + (double)*(float*)(actor + 0x68));
                double s2 = math_poly_trig((double)(*(float*)(rec + 0x9e0) - FLOAT_80619da0));
                *(float*)(actor + 0x6c) = (float)((double)crossZ * s2 + (double)*(float*)(actor + 0x6c));
                *(float*)(rec + 0x9e0) = FLOAT_806108ec *
                    (FLOAT_80619d9c /
                     (float)((double)(int)(DAT_806109c0 ^ 0x80000000u) - DOUBLE_80619dd8)) +
                    *(float*)(rec + 0x9e0);
                aiactor_grab_proximity_melee_sweep();
            }
        } else if (mode < 4) {
            // ---- mode 3: release / wind-down --------------------------------
            if (anim_is_clip_active(actor, 0x24a) == 0)
                anim_play_scaled((double)FLOAT_80619db0, (double)FLOAT_80619d9c);
            if (FLOAT_80619db8 < *(float*)(rec + 0xb30)) {
                float vpx = *(float*)(*(int*)(rec + 4) + 100);
                float ddx = *(float*)(actor + 100) - vpx;
                float ddz = *(float*)(actor + 0x6c) - *(float*)(*(int*)(rec + 4) + 0x6c);
                if (FLOAT_80619dac <= ddx * ddx + ddz * ddz) {
                    anim_play_scaled((double)FLOAT_80619db0, (double)FLOAT_80619d9c);
                    if (*(int*)(rec + 0x20) != 1) {
                        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                        *(int*)(rec + 0x20) = 1;
                        *(float*)(rec + 0x9d8) = FLOAT_80619d98;
                        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                    }
                } else {
                    *(float*)(rec + 0x30) = vpx;
                    *(int*)(rec + 0x34) = *(int*)(*(int*)(rec + 4) + 0x68);
                    *(int*)(rec + 0x38) = *(int*)(*(int*)(rec + 4) + 0x6c);
                    anim_play_scaled((double)FLOAT_80619db0, (double)FLOAT_80619d9c);
                    if (*(int*)(rec + 0x20) != 2) {
                        *(int*)(rec + 0x9d4) = *(int*)(rec + 0x20);
                        *(int*)(rec + 0x20) = 2;
                        *(float*)(rec + 0x9d8) = FLOAT_80619d98;
                        *(int*)(rec + 0x9d0) = *(int*)(rec + 0x20);
                    }
                }
            }
        }
        aiactor_update_aim_height();
    }
}

} // extern "C"
