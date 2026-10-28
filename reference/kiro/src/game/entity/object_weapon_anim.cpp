// object_weapon_anim.cpp
// Weapon-animation-pack PRELOAD orchestration + per-slot anim classification /
// playback-rate driver, sitting just past the object_type_dispatch weapon
// handler table (object_weapon_types2.cpp). Address range 0x801baa78 -
// 0x801bb58c.
//
// object_weapon_preload_skeleton     (0x801baa78) loads the shared weapon
//                                    skeleton pack + kicks the anim descriptor
//                                    build, then latches DAT_806125d8.
// object_weapon_preload_anim_packs   (0x801baaac) THE per-game-mode weapon
//                                    animation-pack loader: for each weapon in
//                                    the built match list it opens that
//                                    weapon's anim pack (dedup vs earlier
//                                    entries), then loads a large set of
//                                    per-mode shared anim packs
//                                    (anim/feanims/animts3/prt/cov/gren/car/z/
//                                    mg/arc/rb/ts + level-specific lv_d) gated
//                                    by the game-mode id DAT_80612614 and the
//                                    single-player flag DAT_805015bd.
// object_weapon_anim_slot_enabled    (0x801bb118) predicate: is a given anim
//                                    slot allowed to play given the object's
//                                    current channel state / user flags.
// object_weapon_anim_rate_driver     (0x801bb390) per-frame: pick the anim
//                                    playback rate/mode for the current weapon
//                                    by weapon-class id.
// object_weapon_melee_lunge_solve    (0x801bb58c) melee/lunge turn-toward-
//                                    target solver: build the aim direction,
//                                    rotate the weapon toward it, and drive the
//                                    swing anim (anim_play_scaled).
//
// Non-matching: 0x43300000 int->double bias idioms + paired-single spill temps
// reduced to plain int/float math; the large fast-inverse-sqrt refine in the
// lunge solver is kept as ordinary float math; raw field-offset pointer
// arithmetic on the object / weapon-list / anim-slot structures is preserved
// (layouts not yet typed). The pack-loader's wide 16-arg PPC ABI tail (the
// register/stack args threaded into spawn_pack_open_by_name) is carried
// verbatim so the call sites keep matching generated_decls' variadic extern.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Spill-frame bracketing (register-pair push/pop).
extern unsigned long long FUN_803826f0(void);   // spill push: (op<<32)|object
extern void               FUN_8038273c(void);    // spill pop
extern unsigned long long FUN_803826d4(void);    // spill push variant (anim-pack loader)
extern void               FUN_80382720(void);    // spill pop variant

// Raw runtime helpers.
extern double FUN_80390324(double x);                // periodic-angle reduce
extern int    FUN_80281b50(int object, int handle);  // weapon anim "is-reversed" predicate
extern void   FUN_801be89c(unsigned long long ctx, double a, double b, unsigned long long c,
                           unsigned long long d, unsigned long long e, unsigned long long f,
                           unsigned long long g);     // post-skeleton-pack anim build
extern int    FUN_8038b378(unsigned int *a, unsigned int *b);  // 3-char tag compare
extern unsigned long long FUN_80387fb8(unsigned long long, double, double, unsigned long long,
                           unsigned long long, unsigned long long, unsigned long long,
                           unsigned long long, int outBuf, const char *fmt, int a, int b,
                           int c, int d, int e, int f); // path sprintf

// Anim-slot descriptor tables + per-mode config globals.
extern short DAT_804d1cd0;   // anim-slot -> descriptor index map (short[])
extern int   DAT_804d34dc;   // anim descriptor flag table base (stride 8)
extern int   DAT_80612614;   // current game-mode id
extern char  DAT_805015bd;   // single-player flag ('\n' == SP)
extern int   DAT_80612610;   // own-team block / min entity index
extern unsigned int DAT_805015c0; // global feature-suppress mask
extern int   DAT_805015d8;   // level/sublevel id
extern int   DAT_806125d8;   // skeleton-pack-loaded latch

// Weapon-list / weapon-def tables.
extern int   DAT_8043e22c;   // weapon-def flags table base (stride 0x3c)
extern int   DAT_8043e244;   // weapon-def pack-name ptr table base (stride 0x3c)

// Pack-name string constants.
extern char PTR_s_pak_anim_gun_uplink_pak_8043e280[];
extern char s_pak_anim_skelts3_pak_80441188[];
extern char s_pak_anim_feanims_pak_804411a0[];
extern char s_pak_anim_anim_pak_804411b8[];
extern char s_pak_anim_animts3_pak_804411cc[];
extern char s_pak_anim_anim_prt_pak_804411e4[];
extern char s_pak_anim_anim_cov_pak_804411fc[];
extern char s_pak_anim_animgren_pak_80441214[];
extern char s_pak_anim_animcar_pak_8044122c[];
extern char s_pak_anim_anim_z_pak_80441244[];
extern char s_pak_anim_anim_mg_pak_80441258[];
extern char s_pak_anim_arc_b_pak_80441270[];
extern char s_pak_anim_animrb_pak_80441284[];
extern char s_pak_anim_anim_ts_pak_80441298[];
extern char s_pak_stream_lv_d_anim_lv_d_pak_804412b0[];

// Melee-lunge tuning constants.
extern float FLOAT_80616e00;  // reversed-anim playback rate
extern float FLOAT_80616e04;  // 0.0 / default rate
extern float FLOAT_80616e08;  // min-distance^2 gate
extern float FLOAT_80616e0c;  // rsqrt refine coefficient
extern float FLOAT_80616e10;  // rsqrt refine constant
extern float FLOAT_80616e14;  // rsqrt refine scale
extern float FLOAT_80616e18;  // yaw scale numerator
extern float FLOAT_80616e1c;  // yaw scale denominator
extern float FLOAT_80616e20;  // swing anim playback speed
extern float FLOAT_80616e24;  // wrap bias
extern float FLOAT_80616e28;  // wrap divisor
} // extern "C"

// anim_play_scaled is declared (unsigned long long, double) in generated_decls,
// but this melee solver calls it positionally with two doubles (the ctx pointer
// arrives in the first fp/gp pair). Call it through a correctly-typed pointer to
// its ROM address to preserve the two-double call without a conflicting decl.
static inline void __ow_anim_play_scaled(double a, double b)
{
    ((void (*)(double, double))0x80023280)(a, b);
}

// spawn_pack_open_by_name / anim_set_playback_rate resolve via generated_decls
// (variadic permissive externs) - called directly below with the full arg list.
// match_build_weapon_list and anim_active_curve_value are declared void/int in
// the generated headers but actually return a value in r3(:r4); call them
// through correctly-typed pointers to their ROM addresses.
static inline unsigned long long __ow_match_build_weapon_list(void)
{
    return ((unsigned long long (*)(void))0x800357c0)();
}
static inline float __ow_anim_active_curve_value(int object)
{
    return ((float (*)(int))0x80026c9c)(object);
}

// ===========================================================================
// Address: 0x801baa78 | Name: object_weapon_preload_skeleton | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load the shared weapon skeleton pack (skelts3.pak), run the follow-on anim
// build (FUN_801be89c), then latch DAT_806125d8 to signal the weapon skeleton
// is resident.
// ===========================================================================
void object_weapon_preload_skeleton(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_10,
                                    void *param_11, int param_12, int param_13,
                                    unsigned int param_14, unsigned int param_15,
                                    unsigned int param_16)
{
    spawn_pack_open_by_name(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            s_pak_anim_skelts3_pak_80441188, (unsigned int *)(intptr_t)param_10,
                            param_11, param_12, param_13, param_14, param_15, param_16);
    FUN_801be89c(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    DAT_806125d8 = 1;
}

// ===========================================================================
// Address: 0x801baaac | Name: object_weapon_preload_anim_packs | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Per-game-mode weapon animation-pack loader. For each weapon in the built
// match weapon-list (match_build_weapon_list) it opens that weapon's anim pack
// (deduplicating vs earlier list entries by comparing the 3-char pack tag), and
// as a fallback ensures the uplink gun pack is present. It then loads a large
// set of shared per-mode anim packs, each gated by the game-mode id
// (DAT_80612614) and the single-player flag (DAT_805015bd). The wide 16-arg
// ABI tail is carried verbatim through spawn_pack_open_by_name.
// ===========================================================================
void object_weapon_preload_anim_packs(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9,
                                      unsigned int param_10, unsigned int *param_11, int param_12,
                                      int param_13, unsigned int param_14, unsigned int param_15,
                                      unsigned int param_16)
{
    (void)param_9; (void)param_10;
    unsigned long long ctx = FUN_803826d4();
    if (DAT_80612614 == 0x66) {
        FUN_80382720();
        return;
    }

    unsigned int *listPtr = param_11;
    if (DAT_80612614 < 0x65) {
        unsigned long long lw = __ow_match_build_weapon_list();
        int list = (int)(lw >> 0x20);
        listPtr = (unsigned int *)lw;
        if (list != 0) {
            bool haveUplink = false;
            int outer = 0;
            for (int i = 0; i < *(int *)(list + 4); i++) {
                int slot = outer + 8;
                param_11 = (unsigned int *)0;
                int defId = *(int *)(list + slot);
                if ((defId != 0) &&
                    (*(int *)(&DAT_8043e22c + defId * 0x3c) != 0)) {
                    param_11 = (unsigned int *)1;
                }
                // dedup: skip if an earlier list entry shares this pack tag
                int inner = 0;
                unsigned int *keep = param_11;
                for (int j = 0; (keep != (unsigned int *)0) && (j < i); j++) {
                    int cur = *(int *)(list + slot);
                    if (cur != 0) {
                        param_11 = (unsigned int *)&DAT_8043e22c;
                        unsigned int *earlier = *(unsigned int **)(&DAT_8043e244 + *(int *)(list + inner + 8) * 0x3c);
                        if ((earlier != 0) &&
                            (FUN_8038b378(*(unsigned int **)(&DAT_8043e244 + cur * 0x3c), earlier) == 0)) {
                            keep = (unsigned int *)0;
                        }
                    }
                    inner += 4;
                }
                if (keep != (unsigned int *)0) {
                    int defIdx = *(int *)(list + slot);
                    spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                            *(const char **)(&DAT_8043e244 + defIdx * 0x3c),
                                            listPtr, param_11, param_12, param_13, param_14, param_15, param_16);
                    if (*(int *)(list + slot) == 0x38) {
                        haveUplink = true;
                    }
                }
                outer += 4;
            }
            if (!haveUplink) {
                spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                        PTR_s_pak_anim_gun_uplink_pak_8043e280,
                                        listPtr, param_11, param_12, param_13, param_14, param_15, param_16);
            }
        }
    }

    if (DAT_80612614 == 0x65) {
        spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                s_pak_anim_feanims_pak_804411a0, listPtr, param_11,
                                param_12, param_13, param_14, param_15, param_16);
    }
    spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            s_pak_anim_anim_pak_804411b8, listPtr, param_11,
                            param_12, param_13, param_14, param_15, param_16);
    spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            s_pak_anim_animts3_pak_804411cc, listPtr, param_11,
                            param_12, param_13, param_14, param_15, param_16);

    int mode = DAT_80612614;
    bool sp = (DAT_805015bd == '\n');

    // per-mode prt pack
    if (!sp ? false :
        (mode == 0xe || mode == 6 ||
         (mode >= 2 && mode <= 2) || (mode >= 3 && mode <= 4) ||
         (mode >= 9 && mode <= 9) || (mode >= 0xa && mode <= 0xb) ||
         mode == 0x1a || mode == 0x16 || mode == 0x12 || mode == 100 || mode == 0x51)) {
        spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                s_pak_anim_anim_prt_pak_804411e4, listPtr, param_11,
                                param_12, param_13, param_14, param_15, param_16);
    }
    // per-mode cov pack
    if (sp && (mode == 0x12 || mode == 9 ||
               (mode >= 3 && mode <= 6) || (mode >= 1 && mode <= 2) ||
               mode == 0xe || mode == 0xb || mode == 0x1a || mode == 0x16 ||
               mode == 100 || (mode >= 0x48 && mode <= 0x49))) {
        spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                s_pak_anim_anim_cov_pak_804411fc, listPtr, param_11,
                                param_12, param_13, param_14, param_15, param_16);
    }
    // per-mode gren pack
    if (sp && (mode == 0x12 || mode == 9 ||
               (mode >= 3 && mode <= 6) || (mode >= 1 && mode <= 2) ||
               mode == 0xe || mode == 0x1a || mode == 0x16 || mode == 100)) {
        spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                s_pak_anim_animgren_pak_80441214, listPtr, param_11,
                                param_12, param_13, param_14, param_15, param_16);
    }
    // car pack (both SP and MP forms hit the same pack)
    if (mode == 0x35 || mode == 100 ||
        (sp && (mode == 0x12 || (mode >= 4 && mode <= 5)))) {
        spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                s_pak_anim_animcar_pak_8044122c, listPtr, param_11,
                                param_12, param_13, param_14, param_15, param_16);
    }
    // z pack + coop-carry variant
    if (sp) {
        if (mode == 0x38 || mode == 0x11 || mode == 1 || mode == 0x16 ||
            mode == 100 || mode == 0x4a) {
            spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    s_pak_anim_anim_z_pak_80441244, listPtr, param_11,
                                    param_12, param_13, param_14, param_15, param_16);
        }
        if (((DAT_805015c0 & 0x40) != 0) &&
            ((DAT_805015d8 == 0) || (DAT_805015d8 == 1) || (DAT_805015d8 == 2))) {
            spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    s_pak_anim_anim_z_pak_80441244, listPtr, param_11,
                                    param_12, param_13, param_14, param_15, param_16);
        }
    }
    // mg pack
    if (!sp || (mode == 100)) {
        spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                s_pak_anim_anim_mg_pak_80441258, listPtr, param_11,
                                param_12, param_13, param_14, param_15, param_16);
    }
    // arc pack (MP only)
    if (!sp) {
        spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                s_pak_anim_arc_b_pak_80441270, listPtr, param_11,
                                param_12, param_13, param_14, param_15, param_16);
    }
    // rb pack
    if (sp && (mode == 0x48 || mode == 9 || mode == 5 || mode == 0xe ||
               mode == 100 || mode == 0x51)) {
        spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                s_pak_anim_animrb_pak_80441284, listPtr, param_11,
                                param_12, param_13, param_14, param_15, param_16);
    }
    // coop-carry rb/ts variants
    if ((DAT_805015c0 & 0x40) != 0) {
        if (DAT_805015d8 == 0xf) {
            spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    s_pak_anim_animrb_pak_80441284, listPtr, param_11,
                                    param_12, param_13, param_14, param_15, param_16);
        }
        if ((DAT_805015d8 == 0x13) || ((unsigned int)(DAT_805015d8 - 0xc) < 2) || (DAT_805015d8 == 0xe)) {
            spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    s_pak_anim_anim_ts_pak_80441298, listPtr, param_11,
                                    param_12, param_13, param_14, param_15, param_16);
        }
    }
    // ts pack
    if (sp && (mode == 0x1a || mode == 2 || mode == 100)) {
        spawn_pack_open_by_name(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                s_pak_anim_anim_ts_pak_80441298, listPtr, param_11,
                                param_12, param_13, param_14, param_15, param_16);
    }
    // level-specific lv_d pack for the modes in this large predicate set
    if ((mode == 0x38) || (mode == 0x2e) || (mode == 9) || (mode == 0x16) ||
        ((unsigned int)(mode - 0x3f) < 2) || (mode == 0x45) || (mode == 0x1a) ||
        (mode == 0xb) || (mode == 0x47) || ((unsigned int)(mode - 0x50) < 2) ||
        (mode == 0xe) || ((unsigned int)(mode - 4) < 3) ||
        ((unsigned int)(mode - 0x11) < 2) || ((unsigned int)(mode - 0x4a) < 4) ||
        ((unsigned int)(mode - 0x3c) < 2) || ((unsigned int)(mode - 1) < 2) || (mode == 5)) {
        char pathBuf[104];
        unsigned long long p = FUN_80387fb8(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                            (int)(intptr_t)pathBuf, s_pak_stream_lv_d_anim_lv_d_pak_804412b0,
                                            DAT_80612614, DAT_80612614, param_13, param_14, param_15, param_16);
        spawn_pack_open_by_name(p, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                pathBuf, (unsigned int *)0, (void *)(intptr_t)DAT_80612614,
                                DAT_80612614, param_13, param_14, param_15, param_16);
    }

    FUN_80382720();
}

// ===========================================================================
// Address: 0x801bb118 | Name: object_weapon_anim_slot_enabled | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Predicate: is anim slot `slot` allowed to play for object `object` given the
// object's current channel state and the per-slot descriptor user-flags
// (DAT_804d34dc, indexed via DAT_804d1cd0[slot]*8)? Returns 1 if allowed.
// Gates: type-8 objects with the model 0x800000 flag require the slot to carry
// bits 0x1000028; wounded/limb bits 0x20 vs channel bits 0x1c00/0x1000000/0x8
// enforce upper/lower-body anim exclusivity.
// ===========================================================================
unsigned int object_weapon_anim_slot_enabled(int object, int slot)
{
    int model = 0;
    if (*(int *)(object + 0x20) == 8) {
        model = *(int *)(object + 0x7c);
        if (((*(unsigned int *)(model + 0x28) & 0x800000) != 0) &&
            ((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[slot] * 8) & 0x1000028) == 0)) {
            return 0;
        }
    }
    if (model != 0) {
        int channel = *(int *)(*(int *)(object + 4) + 0xcc);
        if (channel != 0) {
            if (((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[channel] * 8) & 0x20) != 0) &&
                ((*(unsigned int *)(model + 0x10) & 1) == 0)) {
                return 0;
            }
        }
    }
    int base = *(int *)(object + 4);
    int channel2 = *(int *)(base + 0xcc);
    if (channel2 != 0) {
        if ((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[slot] * 8) & 0x20) == 0) {
            if (((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[channel2] * 8) & 0x1c00) != 0) &&
                ((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[slot] * 8) & 0x1000000) == 0)) {
                if ((*(int *)(base + 0x11c) == 0) && ((*(unsigned int *)(base + 0x120) & 4) == 0)) {
                    return 0;
                }
            }
        }
    }
    if ((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[slot] * 8) & 0x20) == 0) {
        if (channel2 != 0) {
            if (((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[slot] * 8) & 8) == 0) &&
                ((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[channel2] * 8) & 8) != 0) &&
                (*(int *)(base + 0x11c) == 0)) {
                return 0;
            }
        }
    }
    return 1;
}

// ===========================================================================
// Address: 0x801bb390 | Name: object_weapon_anim_rate_driver | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Per-frame: choose the anim playback rate + mode for the current weapon's
// active anim state, by the weapon-class id (*(base[0x20]+4)). Reversed anims
// (FUN_80281b50) play at FLOAT_80616e00 mode 2; most weapon classes play at
// FLOAT_80616e04 mode 2; a mid range plays FLOAT_80616e00 mode 1; a low class
// range gates the loop rate by the weapon's per-slot flag word (+0x14 in SP /
// +0x1c otherwise, bit 0x200). Spill-frame push/pop bracket the object pointer.
// ===========================================================================
void object_weapon_anim_rate_driver(void)
{
    unsigned long long packed = FUN_803826f0();
    int object = (int)packed;
    int slot   = (int)(packed >> 0x20);

    int model = 0;
    if (*(int *)(object + 0x20) == 8) {
        model = *(int *)(object + 0x7c);
    }
    if ((model == 0) || (*(int *)(object + 0x20) == 0x1000) ||
        (*(int *)(model + 8) < DAT_80612610)) {
        FUN_8038273c();
        return;
    }

    int reversed = FUN_80281b50(object, slot);
    int *animState = *(int **)(object + 4);
    int cls = *(int *)(animState[0x20] + 4);

    // Classify the weapon-class id into one of three outcomes:
    //   fast   -> FLOAT_80616e04 mode 1
    //   slow   -> FLOAT_80616e00 mode 1
    //   lowGate-> low-class range with reversed/flag-gated selection
    //   (default fall-through) FLOAT_80616e04 mode 2
    bool fast = false;    // e04 mode1 (LAB_801bb550)
    bool slow = false;    // e00 mode1 (part of LAB_801bb550's sibling)
    bool lowGate = false; // LAB_801bb47c

    if (cls < 0x57) {
        if (cls == 0x45) {
            fast = true;
        } else if (cls > 0x44) {
            if (cls < 0x4e) {
                if (cls > 0x4a) fast = true;   // 0x4b..0x4d
            } else if (cls > 0x52) {
                fast = true;                    // 0x53..0x56
            }
        } else if (cls == 0x27) {
            fast = true;
        } else if (cls == 0) {
            lowGate = true;
        }
    } else if (cls != 0x7b) {
        if (cls < 0x7b) {
            if ((cls == 0x5e) || ((cls > 0x5d) && (cls > 0x79))) {
                slow = true;
            }
        } else {
            if (cls == 0x80) {
                slow = true;
            } else if ((cls < 0x80) && (cls < 0x7d)) {
                lowGate = true;                 // 0x7c
            }
        }
    }

    if (fast) {
        anim_set_playback_rate((double)FLOAT_80616e04, animState, 1);
    } else if (slow) {
        anim_set_playback_rate((double)FLOAT_80616e00, animState, 1);
    } else if (lowGate) {
        if (reversed) {
            anim_set_playback_rate((double)FLOAT_80616e00, animState, 2);
        } else if ((*(unsigned int *)(&DAT_804d34dc + (short)(&DAT_804d1cd0)[slot] * 8) & 0x20) == 0) {
            unsigned int flags = (DAT_805015bd == '\n')
                                     ? *(unsigned int *)(model + 0x14)
                                     : *(unsigned int *)(model + 0x1c);
            if ((flags & 0x200) == 0) {
                anim_set_playback_rate((double)FLOAT_80616e00, animState, 1);
            }
        } else {
            anim_set_playback_rate((double)FLOAT_80616e04, animState, 1);
        }
    } else {
        anim_set_playback_rate((double)FLOAT_80616e04, animState, 2);
    }

    FUN_8038273c();
}

// ===========================================================================
// Address: 0x801bb58c | Name: object_weapon_melee_lunge_solve | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Melee / lunge turn-toward-target solver. Builds a planar aim direction toward
// the target (or from an explicit override vector at handle+0x2c / +0x30),
// normalizes it (fast inverse sqrt + one Newton refine) when past the min gate,
// rotates the weapon toward the aim by the curve-driven yaw, and drives the
// swing anim via anim_play_scaled. If the object is mid-attack (+0x1cc > 0) it
// just picks the closest swing; otherwise it also advances the aim phase +0xc4/
// +0x70 by the wrapped yaw. Returns 1 when a full swing begins, 0 mid-attack.
// ===========================================================================
unsigned int object_weapon_melee_lunge_solve(int object, int handle)
{
    float dirX, dirZ, bias;
    float *ovr = *(float **)(handle + 0x2c);
    if (ovr == (float *)0) {
        ovr = *(float **)(handle + 0x30);
        if (ovr == (float *)0) {
            dirX = *(float *)(object + 100) - *(float *)(*(int *)(handle + 8) + 100);
            dirZ = *(float *)(object + 0x6c) - *(float *)(*(int *)(handle + 8) + 0x6c);
        } else {
            dirX = ovr[0];
            dirZ = ovr[2];
        }
    } else {
        dirX = -ovr[0];
        dirZ = -ovr[2];
    }

    bias = FLOAT_80616e04;
    float lenSq = (float)((double)dirZ * (double)dirZ +
                          (double)(float)((double)dirX * (double)dirX + (double)bias));
    if (FLOAT_80616e08 < lenSq) {
        float inv = 1.0f / SQRT(lenSq);
        inv = inv * -(FLOAT_80616e0c * lenSq * inv * inv - FLOAT_80616e10);
        float scale = FLOAT_80616e14 * inv * -(FLOAT_80616e0c * lenSq * inv * inv - FLOAT_80616e10);
        dirX = (float)((double)dirX * (double)scale);
        bias = (float)((double)bias * (double)scale);
        dirZ = (float)((double)dirZ * (double)scale);
    }

    float curve = __ow_anim_active_curve_value(object);
    float yaw = (float)((double)*(float *)(object + 0x70) + (double)curve);

    float rot[8];
    mtx4_set_rotation_y((double)((float)((double)FLOAT_80616e18 * (double)yaw) / FLOAT_80616e1c), rot);

    float local[16];
    entity_build_local_matrix(local, (void *)(intptr_t)*(int *)(handle + 8), 0, 0);
    if (*(int *)(*(int *)(object + 4) + 0x10) != 0) {
        mtx4_multiply(local, *(float **)(*(int *)(*(int *)(handle + 8) + 4) + 0x10), rot);
    }

    double zero = (double)FLOAT_80616e04;
    if (zero < (double)*(float *)(object + 0x1cc)) {
        // mid-attack: just pick the nearest swing, no phase advance
        __ow_anim_play_scaled(zero, (double)FLOAT_80616e20);
        return 0;
    }

    // dot of the aim dir against the two candidate swing basis rows
    float dotA = (float)((double)rot[6] * (double)dirZ +
                         (double)(float)((double)rot[2] * (double)dirX +
                                         (double)(float)((double)rot[4] * (double)bias)));
    if (dotA <= zero) {
        __ow_anim_play_scaled(zero, (double)FLOAT_80616e20);
    } else {
        __ow_anim_play_scaled(zero, (double)FLOAT_80616e20);
        double phase = (double)(float)((double)FLOAT_80616e1c + (double)yaw);
        double wrapped = FUN_80390324((double)((float)(phase - (double)FLOAT_80616e24) / FLOAT_80616e28));
        float snapped = -(float)((double)FLOAT_80616e28 * (double)(float)wrapped - phase);
        *(float *)(object + 0xc4) = snapped;
        *(float *)(object + 0x70) = snapped;
    }
    return 1;
}
