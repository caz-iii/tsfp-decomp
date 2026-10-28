// =============================================================================
// anim_reaction.cpp - Anim clip phase/flag queries + AI reaction-anim selection
// Address range: 0x800259f0 - 0x80026608
// Batch: 393 | Date: 2026-09-05
//
// Continues the animation pipeline (batches 385-392). This module holds the
// remaining clip-state query helpers (phase, per-slot flag tests, playback-speed
// setter), the AI "which reaction animation should this actor play" decision
// (aiactor_choose_reaction_anim), the descriptor-slot loader wrapper, and the
// per-frame pose-eval entry point.
//
//   * anim_primary_clip_phase       (0x800259f0) primary clip normalized phase (t/len) (FUN_800259f0)
//   * anim_active_clip_phase         (0x80025a30) active-slot clip normalized phase (FUN_80025a30)
//   * anim_primary_clip_has_flags    (0x80025a80) flag-mask test on the primary slot (FUN_80025a80)
//   * anim_secondary_clip_has_flags  (0x80025ad8) flag-mask test on the secondary slot (FUN_80025ad8)
//   * anim_clip_flags_active_slot    (0x80025b30) flag-mask test on whichever slot is active (FUN_80025b30)
//   * anim_primary_clip_time_left    (0x80025bc0) is the primary clip within dt of its end? (FUN_80025bc0)
//   * anim_set_clip_speed_fields     (0x80025c04) set clip playback-speed fields (FUN_80025c04)
//   * aiactor_choose_reaction_anim   (0x80025cb4) pick the AI hit/aim reaction anim (FUN_80025cb4)
//   * anim_load_descriptor_slot      (0x800265b8) register + load an anim descriptor slot (FUN_800265b8)
//   * anim_pose_eval_frame           (0x80026608) per-frame pose evaluation entry (FUN_80026608)
//
// Compile-clean non-matching reconstruction.
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // anim_active_clip_slot, anim_is_clip_active, anim_clip_has_flags,
                               // anim_play_scaled, player_slot_get_pool_base, rng_counter_tick,
                               // player_ai_is_ranged_weapon/explosive_weapon,
                               // character_has_shield_active, aiactor_classify_aim_cone,
                               // aiactor_turn_toward_entity

// ---- raw runtime globals ---------------------------------------------------
extern int    DAT_80611824;    // animation descriptor table base (idx*4)
extern uint   DAT_804d34d8;    // per-anim flag table (word 0, idx*8)
extern uint   DAT_804d34dc;    // per-anim flag table (word 1, idx*8)
extern uint   DAT_805015c0;    // global mode flags
extern uint   DAT_805015c4;    // global mode flags
extern char   DAT_805015bd;    // region/mode selector ('\n' special)
extern int    DAT_80612614;    // level/mode id (100 == special)
extern int    DAT_80610ba0;    // weapon-def table base (idx * 0x29c)
extern int    DAT_80611c24;    // handle-table base (0x2e8 stride)
extern char   DAT_804e5f20;    // descriptor-slot registry (0xc stride)
extern int    DAT_804e5f24;
extern int    DAT_804e5f28;
extern int    DAT_806117ec;    // pose-eval accumulator reset targets
extern int    DAT_806117d8;
extern int    DAT_806117dc;
extern int    DAT_80611820;
extern int    DAT_8061181c;

// ---- numeric constants -----------------------------------------------------
extern float  FLOAT_80613930;  // 0.0
extern float  FLOAT_80613944;  // 1.0
extern float  FLOAT_80613948;  // 0.5 (round bias)
extern float  FLOAT_8061399c;  // small epsilon (clip-length guard)
extern float  FLOAT_80613964;  // default speed
extern float  FLOAT_80613970;  // turn-phase threshold
extern float  FLOAT_806139a8;  // reaction speed A
extern float  FLOAT_806139c4;  // rng range hi
extern double DOUBLE_806139b8; // uint->double magic bias
extern float  FLOAT_806139d4;  // aim-phase gate 1
extern float  FLOAT_806139d8;  // rng normalizer
extern float  FLOAT_806139dc;  // reaction speed B
extern float  FLOAT_806139e0;  // aim-phase gate 2
extern float  FLOAT_806139e4;  // slowdown factor
extern float  FLOAT_806118ec;  // score-mode speed divisor

// ---- raw runtime helpers (self-extern at call-site arity) ------------------
extern "C" {
    unsigned long long FUN_803826e8(void);   // scratch: packed (record<<32 | keyIdx)
    void   FUN_80382734(void);                // scratch free (e8)
    void   FUN_801bb58c(int rec, int def);    // melee lunge toward target
    void   FUN_80151b5c(int rec);             // grappled/finisher reaction
    int    FUN_802acfa0(int rec);             // special-state reaction gate
    unsigned long long FUN_80026884(unsigned long long a, double b, double c,
                                    unsigned long long d, unsigned long long e,
                                    unsigned long long f, unsigned long long g,
                                    unsigned long long h);  // pose-eval setup -> (recHi|rec)
    void   FUN_80026648(int recHi, int rec);  // pose-eval commit
    // forward decls for same-file entry points used before definition
    double FUN_80025a30(int rec);
    int    FUN_80025a80(int rec, int unused, unsigned int maskLo, unsigned int maskHi);
    int    FUN_80025ad8(int rec, int unused, unsigned int maskLo, unsigned int maskHi);
}

// rng_counter_tick is declared void in generated_decls but returns a value here;
// call through a typed fn-ptr cast.
typedef unsigned int (*rng_u32_fn)(void);
#define RNG_U32 ((rng_u32_fn)&rng_counter_tick)

// anim_load_descriptor (0x80022438) has a 16-arg ABI; generated_decls declares a
// simplified prototype, so invoke through a typed fn-ptr cast.
typedef void (*anim_load_desc_fn)(unsigned long long, double, double, unsigned long long,
                                  unsigned long long, unsigned long long, unsigned long long,
                                  unsigned long long, int, int, int, void*, int, int, int, int);
#define ANIM_LOAD_DESCRIPTOR ((anim_load_desc_fn)&anim_load_descriptor)

// ---------------------------------------------------------------------------
// anim_primary_clip_phase (0x800259f0)  [FUN_800259f0]
// Normalized playback phase (currentTime +0xd4 / clipLength +0x18) of the
// primary slot's clip, or 0.0 when the clip length is degenerate.
// ---------------------------------------------------------------------------
extern "C" double FUN_800259f0(int st)
{
    int tbl = *(int*)(DAT_80611824 + *(int*)(st + 0xd0) * 4);
    if (tbl != 0) {
        float len = *(float*)(tbl + 0x18);
        if ((double)FLOAT_8061399c <= ABS(len))
            return (double)(*(float*)(st + 0xd4) / len);
    }
    return (double)FLOAT_80613930;
}

// ---------------------------------------------------------------------------
// anim_active_clip_phase (0x80025a30)  [FUN_80025a30]
// Normalized playback phase of whichever blend slot is currently active.
// ---------------------------------------------------------------------------
extern "C" double FUN_80025a30(int rec)
{
    int* slot = (int*)anim_active_clip_slot(rec);
    float phase = FLOAT_80613930;
    if (slot[0] != 0)
        phase = (float)slot[2] / *(float*)(*(int*)(DAT_80611824 + slot[1] * 4) + 0x18);
    return (double)phase;
}

// ---------------------------------------------------------------------------
// anim_primary_clip_has_flags (0x80025a80)  [FUN_80025a80]
// Does the primary slot's clip carry any of maskLo/maskHi in its flag tables.
// ---------------------------------------------------------------------------
extern "C" int FUN_80025a80(int rec, int unused, unsigned int maskLo, unsigned int maskHi)
{
    (void)unused;
    int idx = *(int*)(*(int*)(rec + 4) + 0xd0);
    if (*(int*)(DAT_80611824 + idx * 4) == 0)
        return 0;
    if ((maskHi & *(unsigned int*)(&DAT_804d34dc + idx * 8)) == 0 &&
        (maskLo & *(unsigned int*)(&DAT_804d34d8 + idx * 8)) == 0)
        return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// anim_secondary_clip_has_flags (0x80025ad8)  [FUN_80025ad8]
// Does the secondary slot's clip (descriptor +0x128) carry any of the masks.
// ---------------------------------------------------------------------------
extern "C" int FUN_80025ad8(int rec, int unused, unsigned int maskLo, unsigned int maskHi)
{
    (void)unused;
    int idx = *(int*)(*(int*)(rec + 4) + 0x128);
    if (*(int*)(DAT_80611824 + idx * 4) == 0)
        return 0;
    if ((maskHi & *(unsigned int*)(&DAT_804d34dc + idx * 8)) == 0 &&
        (maskLo & *(unsigned int*)(&DAT_804d34d8 + idx * 8)) == 0)
        return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// anim_clip_flags_active_slot (0x80025b30)  [FUN_80025b30]
// Test the masks against whichever slot is active: if the secondary blend is
// engaged (weight +400 > 0) check it; when no secondary is active check the
// primary. (The original threads a saved register; the logic is preserved.)
// ---------------------------------------------------------------------------
extern "C" void FUN_80025b30(int rec, int unused, unsigned int maskLo, unsigned int maskHi)
{
    (void)unused;
    int st = *(int*)(rec + 4);
    if ((*(float*)(st + 400) <= FLOAT_80613930 || FUN_80025ad8(rec, st, maskLo, maskHi) == 0) &&
        FLOAT_80613930 == *(float*)(*(int*)(rec + 4) + 400)) {
        FUN_80025a80(rec, st, maskLo, maskHi);
    }
}

// ---------------------------------------------------------------------------
// anim_primary_clip_time_left (0x80025bc0)  [FUN_80025bc0]
// True when the primary slot is the sole active clip and its remaining time
// (clipLength +0x18 minus dt) is below the current playback time (+0xd4) — i.e.
// the clip will finish within dt.
// ---------------------------------------------------------------------------
extern "C" int FUN_80025bc0(double dt, int st)
{
    if (FLOAT_80613930 == *(float*)(st + 400) &&
        (float)((double)*(float*)(*(int*)(DAT_80611824 + *(int*)(st + 0xd0) * 4) + 0x18) - dt)
            < *(float*)(st + 0xd4))
        return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// anim_set_clip_speed_fields (0x80025c04)  [FUN_80025c04]
// Set a def's playback-speed fields (+0x100 / +0x158, i.e. int slots 0x40/0x56).
// Applies the score-mode speed divisor and clamps negative speed to 0 for the
// player-controlled (type 8) case.
// ---------------------------------------------------------------------------
extern "C" void FUN_80025c04(double speed, int* def)
{
    if (((DAT_805015c0 & 0x800) != 0) && ((DAT_805015c4 & 0x2000000) == 0)) {
        int pool = (int)player_slot_get_pool_base();
        if (pool != 0) {
            pool = (int)player_slot_get_pool_base();
            if (*(int*)(pool + 0x218) != 0x1b)
                speed = (double)(float)(speed * (double)FLOAT_806118ec);
        }
    }
    if (def[0] != 0 && *(int*)(def[0] + 0x20) == 8 && speed < (double)FLOAT_80613930)
        speed = (double)FLOAT_80613930;
    def[0x40] = (int)(float)speed;
    def[0x56] = (int)(float)speed;
}

// ---------------------------------------------------------------------------
// aiactor_choose_reaction_anim (0x80025cb4)  [FUN_80025cb4]
// Decide which reaction/aim animation an AI actor should play in response to an
// event `def` (param at +0x18 is the event kind). Gated by difficulty tier
// (+0xb04), current state flags, and whether certain reaction clips are already
// active. Depending on weapon class (ranged / explosive / melee), aim-cone
// classification and RNG, plays one of several reaction anims via
// anim_play_scaled (or delegates to a melee/finisher/special handler).
// ---------------------------------------------------------------------------
extern "C" void FUN_80025cb4(int a, int b, int def)
{
    (void)a; (void)b;

    unsigned long long packed = FUN_803826e8();
    int rec = (int)(packed >> 0x20);
    int animIdIn = (int)packed;
    int self = *(int*)(rec + 0x7c);

    float baseSpeed = FLOAT_80613964;
    if (DAT_805015bd == '\n')
        baseSpeed = FLOAT_80613944;
    double speed = (double)baseSpeed;

    bool tierOk = (DAT_805015bd == '\n') || (*(int*)(self + 0xb04) < 6) ||
                  (animIdIn != 1 && *(int*)(self + 0xb04) < 0xb);
    if (!tierOk) { FUN_80382734(); return; }

    if (!((*(unsigned int*)(self + 0xc6c) & 4) == 0 &&
          (*(unsigned int*)(&DAT_804d34dc + *(int*)(*(int*)(rec + 4) + 0xd0) * 8) & 0x10000) == 0 &&
          *(short*)(self + 0x48) != 0x36 &&
          *(short*)(self + 0x48) != 0x62 && *(int*)(self + 0x44) != 2 &&
          anim_is_clip_active(rec, 0x39) == 0 &&
          anim_is_clip_active(rec, 0x3a) == 0)) {
        FUN_80382734();
        return;
    }

    unsigned int sflags = (DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                                  : *(unsigned int*)(self + 0x1c);
    if ((sflags & 0x501) != 0) { FUN_80382734(); return; }

    if (*(int*)(def + 0x18) == 0x24) {
        FUN_801bb58c(rec, def);
        FUN_80382734();
        return;
    }

    unsigned int sflags2 = (DAT_805015bd == '\n') ? *(unsigned int*)(self + 0x14)
                                                  : *(unsigned int*)(self + 0x1c);
    if ((sflags2 & 0x8000) != 0) {
        unsigned int h = *(unsigned int*)(self + 0xc08);
        int slot = 0;
        if (h != 0) {
            slot = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int*)(slot + 0x2c) != h) {
                *(int*)(self + 0xc08) = 0;
                slot = 0;
            }
        }
        if (slot != 0) {
            FUN_80151b5c(rec);
            FUN_80382734();
            return;
        }
    }

    bool ranged = player_ai_is_ranged_weapon((int)*(short*)(self + 0x4a)) != 0;
    if (ranged && character_has_shield_active(self) == 0 &&
        DAT_805015bd == '\n' && DAT_80612614 != 100) {
        FUN_80382734();
        return;
    }

    bool explosive = player_ai_is_explosive_weapon((int)*(short*)(self + 0x4a)) != 0;
    if (explosive && DAT_805015bd == '\n' && DAT_80612614 != 100) {
        FUN_80382734();
        return;
    }

    if (*(int*)(self + 0x44) == 1) {
        // ranged/aiming behaviour
        int cone = aiactor_classify_aim_cone(rec, *(float**)(def + 0x30), 0);
        if ((*(unsigned int*)(&DAT_804d34dc + *(int*)(*(int*)(rec + 4) + 0xd0) * 8) & 8) == 0 ||
            FLOAT_806139d4 <= *(float*)(*(int*)(rec + 4) + 0xd4)) {
            if (cone == 2 && (*(int*)(def + 0x18) == 6 || *(int*)(def + 0x18) == 7)) {
                unsigned int r = RNG_U32();
                int pick = (int)(FLOAT_806139c4 *
                    ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_806139b8) / FLOAT_806139d8)
                    + FLOAT_80613948);
                if (pick >= 0 && pick <= 3) {
                    anim_play_scaled((double)FLOAT_80613930, (double)FLOAT_806139dc);
                    FUN_80382734();
                    return;
                }
            }
            unsigned int r2 = RNG_U32();
            int pick2 = (int)(FLOAT_80613948 +
                (float)((double)CONCAT44(0x43300000, r2) - DOUBLE_806139b8) / FLOAT_806139d8);
            if (pick2 >= 0 && pick2 <= 1)
                anim_play_scaled((double)FLOAT_806139a8, (double)FLOAT_80613944);
        }
    } else {
        // melee / generic behaviour
        if (*(int*)(def + 0x18) == 6 || *(int*)(def + 0x18) == 7) {
            int cone = aiactor_classify_aim_cone(rec, *(float**)(def + 0x30), 0);
            if (cone == 2 || (cone > 0 && cone < 2)) {
                anim_play_scaled((double)FLOAT_80613930, (double)FLOAT_806139dc);
                FUN_80382734();
                return;
            }
            if (cone >= 2 && cone < 5 && *(int*)(def + 4) != 0 &&
                (anim_is_clip_active(rec, 0x44) == 0 ||
                 (double)FLOAT_80613970 < FUN_80025a30(rec))) {
                aiactor_turn_toward_entity(*(unsigned int*)(def + 4), 0, 1);
                anim_play_scaled((double)FLOAT_80613930, (double)FLOAT_806139dc);
                FUN_80382734();
                return;
            }
        }
        int special = 0;
        if ((*(unsigned int*)(&DAT_804d34dc + *(int*)(*(int*)(rec + 4) + 0xd0) * 8) & 8) == 0 ||
            FLOAT_806139e0 <= *(float*)(*(int*)(rec + 4) + 0xd4)) {
            if ((*(unsigned int*)(self + 0x14) & 0x70000) == 0 || FUN_802acfa0(rec) == 0) {
                int hasBlock = anim_clip_has_flags(rec, special, 0x800000, 0);
                if (hasBlock == 0) {
                    if (anim_is_clip_active(rec, 0x604) == 0 &&
                        anim_is_clip_active(rec, 0x605) == 0 &&
                        anim_is_clip_active(rec, 0x606) == 0 &&
                        anim_is_clip_active(rec, 0x615) == 0 &&
                        anim_is_clip_active(rec, 0x616) == 0 &&
                        anim_is_clip_active(rec, 0x617) == 0) {
                        // ordinary idle-reaction: RNG chooses variant (both branches
                        // play at the same base speed on this build)
                        RNG_U32();
                        anim_play_scaled((double)FLOAT_80613930, speed);
                    } else {
                        anim_play_scaled((double)FLOAT_80613930,
                                         (double)(float)((double)FLOAT_806139e4 * speed));
                    }
                } else {
                    RNG_U32();
                    anim_play_scaled((double)FLOAT_80613930, speed);
                }
            }
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// anim_load_descriptor_slot (0x800265b8)  [FUN_800265b8]
// Register descriptor slot `slot` in the slot table (DAT_804e5f20, 0xc stride)
// then load the descriptor into it via anim_load_descriptor.
// ---------------------------------------------------------------------------
extern "C" void FUN_800265b8(unsigned long long p1, double p2, double p3,
                             unsigned long long p4, unsigned long long p5, unsigned long long p6,
                             unsigned long long p7, unsigned long long p8,
                             int slot, int a10, int a11, int a12,
                             int mode, int a14, int a15, int a16)
{
    (void)a12; (void)a14;
    int off = slot * 0xc;
    *(int*)(&DAT_804e5f20 + off) = 1;
    *(int*)(&DAT_804e5f24 + off) = slot;
    if (mode == 0)
        *(int*)(&DAT_804e5f28 + off) = a10;
    ANIM_LOAD_DESCRIPTOR(p1, p2, p3, p4, p5, p6, p7, p8,
                         a11, a10, 0, (void*)(&DAT_804e5f20 + off), mode, off, a15, a16);
}

// ---------------------------------------------------------------------------
// anim_pose_eval_frame (0x80026608)  [FUN_80026608]
// Per-frame pose evaluation entry: reset the pose-eval accumulators, run the
// setup pass (FUN_80026884) to build the model's evaluation context, then commit
// it (FUN_80026648).
// ---------------------------------------------------------------------------
extern "C" void FUN_80026608(unsigned long long p1, double p2, double p3,
                             unsigned long long p4, unsigned long long p5, unsigned long long p6,
                             unsigned long long p7, unsigned long long p8)
{
    DAT_806117ec = 0;
    DAT_806117d8 = DAT_806117dc;
    DAT_80611820 = DAT_8061181c;
    unsigned long long ctx = FUN_80026884(p1, p2, p3, p4, p5, p6, p7, p8);
    FUN_80026648((int)(ctx >> 0x20), (int)ctx);
}
