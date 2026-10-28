// Address range: 0x801356f0 - 0x80135e57 | batch_364 | 2026-09-05
//
// AI / bot "actor" match subsystem. Manages the roster of AI-controlled
// characters for a match: allocation of the pairwise-distance matrix, per-match
// reset, resource preload, level spawn, and the per-frame tick that drives
// behavior evaluation, deferred zone-light propagation and damage-flash decay.
//
// Global game-mode selector DAT_805015bd (0x0a = coop story path, 0x02/0x03 =
// story variants). DAT_80612610 = base active-actor count, DAT_806120fc =
// story-extra actor count. DAT_8061208c = cached level-info pointer.
//
// Functions:
//   aiactor_render_debug_gate       0x801356f0  story-gated debug overlay forward
//   aiactor_system_reset            0x80135440  per-match full reset
//   aiactor_system_alloc            0x80135734  alloc pairwise matrix + spawn
//   aiactor_preload_resources       0x801357b0  preload character resources
//   aiactor_spawn_all               0x801358b4  level actor spawn + gib preload
//   aiactor_pre_tick                0x80135a08  per-frame pre-update gate
//   aiactor_tick_update             0x80135a58  MASSIVE per-frame tick
//   aiactor_update_passability_flag 0x80135dc0  mover passability flag update

#include "common.h"

// --- Game-mode / roster globals --------------------------------------------
extern char  DAT_805015bd;    // game mode byte (0x0a coop, 0x02/0x03 story)
extern char  DAT_805015be;    // secondary mode selector
extern char  DAT_80501572;    // arcade/story player count
extern char  DAT_80501570;    // global context struct (address taken)
extern char  DAT_80501584;    // character-id table base (byte array)
extern u32   DAT_805015c0;    // cheat/flag word

extern int   DAT_80612610;    // base active-actor count
extern int   DAT_806120fc;    // story-extra actor count
extern int   DAT_80612614;    // current level id
extern int   DAT_8061208c;    // cached level-info pointer
extern int   DAT_80612088;    // pairwise-distance matrix buffer
extern int   DAT_80611930;    // subsystem-enabled flag
extern int   DAT_806108e4;    // subsystem-active flag
extern int   DAT_80612090;    // (reset scratch)
extern int   DAT_80612094;
extern int   DAT_80612098;    // visible-node count
extern int   DAT_8061209c;
extern int   DAT_806120a0;
extern int   DAT_806120a4;    // active-actor round-robin cursor
extern int   DAT_806120a8;    // per-frame callback cursor
extern int   DAT_806120ac;
extern int   DAT_806120b0;
extern int   DAT_806120b4;
extern int   DAT_806120b8;
extern int   DAT_806120bc;
extern int   DAT_806120c0;
extern int   DAT_806120dc;
extern int   DAT_806120e0;
extern int   DAT_806120e4;    // double-buffer index (0/1)
extern int   DAT_806120e8;    // idle flag
extern int   DAT_806120ec;
extern int   DAT_806120f0;    // live story-extra actor count
extern int   DAT_806120f4;
extern int   DAT_806120f8;
extern int   DAT_80612100;
extern int   DAT_80612104;
extern int   DAT_80610998;
extern char  DAT_80501572_b;  // (unused alias placeholder)
extern int   DAT_805885b8;    // double-buffered behavior list base
extern int   DAT_805885e4;
extern int   DAT_80588780;    // 16-entry table
extern int   DAT_80588590;    // 10-entry table
extern int   DAT_80587e10;    // zone sub-entity table base
extern int   DAT_80587e14;    // damage-flash active flags (0x60 stride)
extern int   DAT_80587e1c;    // damage-flash levels (0x60 stride)
extern int   DAT_80588a1c;    // story-extra actor pointer table
extern int   DAT_80588610;    // visible-node pointer table
extern int   DAT_805885f0;    // spawn scratch region (0x80 bytes)
extern int   DAT_80611d90;    // per-player state pool (0xea0 stride)
extern int   DAT_80611d78;    // 8-slot actor table (0x141c stride)
extern int   DAT_80611d54;    // primary player state pointer
extern char  DAT_80501572_c;  // (placeholder)
extern signed char DAT_80501572_d;
extern int   DAT_806109c0;    // frame counter (raw)
extern float FLOAT_806108ec;  // frame-time delta
extern float FLOAT_80615ca8;  // damage-flash fade numerator
extern float FLOAT_80615cac;  // damage-flash floor
extern double DOUBLE_80615cb0; // fp int->double bias constant
extern char  DAT_80501572_e;

// --- External helpers ------------------------------------------------------
extern s32   mem_alloc(s32 size, s32 type);
extern int   FUN_8013dbac(void);                 // active-actor count
extern int   level_get_info_cached(int levelId);
extern int   level_get_char_count(void);
extern int   level_get_default_weapon(int idx, int *outA, int *outB);
extern void  player_ai_reset_weapon_inventory(void);
extern int   player_ai_has_alt_model(int charId);
extern void  memset_wrapper(int dst, int val, int len);

extern void  FUN_80279020(unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, void*, u32, u32, u32,
                          u32, u32, u32, u32);
extern int   FUN_80140968(void);
extern int   FUN_80140940(void);
extern int   FUN_80140974(unsigned long long, double, double, double, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long,
                          int, int, int*, u32, u32, u32, u32, u32);
extern int   FUN_8013db0c(int slot);
extern int   player_ai_attach_nop(void);
extern int   player_ai_on_death_stub(void);
extern int   player_ai_preload_character_resources(unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long);
extern int   player_ai_spawn_death_gibs(unsigned long long, double, double, double,
                          unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, int, int, int);

// spawn / reset dispatch (coop vs standard)
extern int   FUN_8025c568(void);
extern int   FUN_802b8cc4(void);
extern int   FUN_8025c614(unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, void*, u32, u32, u32,
                          u32, u32, u32, u32);
extern int   FUN_802b8d64(unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, void*, u32, u32, u32,
                          u32, u32, u32, u32);
extern unsigned long long FUN_8025c6c8(void);
extern int   FUN_802b8da8(unsigned long long, double, double, double, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long);

// per-frame tick helpers
extern int   FUN_80278ff0(void);
extern int   FUN_8027688c(void);
extern int   FUN_80272de0(void);
extern int   FUN_8025c9d8(void);
extern int   FUN_80142b38(void);
extern int   FUN_8013e868(void);
extern int   FUN_8013fec0(u32 mask);
extern int   FUN_80138e74(void);
extern int   FUN_802603a4(void);
extern int   FUN_802ba204(void);
extern int   FUN_80280b94(int *state);
// countLeadingZeros is provided by common.h (Ghidra intrinsic) — do not redeclare.

// register save/restore intrinsic for the big tick frame
extern void  __save_gpr_25(void);
extern void  __restore_gpr_25(void);

// ---------------------------------------------------------------------------
// aiactor_render_debug_gate 0x801356f0
// Story-mode-gated forwarder to the AI debug/overlay renderer (FUN_80279020).
// Fires only in story variants (mode 0x02/0x03), never in coop (0x0a).
// ---------------------------------------------------------------------------
void aiactor_render_debug_gate(unsigned long long param_1, unsigned long long param_2,
                               unsigned long long param_3, unsigned long long param_4,
                               unsigned long long param_5, unsigned long long param_6,
                               unsigned long long param_7, unsigned long long param_8,
                               u32 param_9, u32 param_10, u32 param_11, u32 param_12,
                               u32 param_13, u32 param_14, u32 param_15, u32 param_16)
{
    (void)param_9;
    if ((DAT_805015bd != '\n') &&
        ((DAT_805015bd == '\x03') || (DAT_805015bd == '\x02'))) {
        FUN_80279020(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     &DAT_80501570, param_10, param_11, param_12, param_13, param_14,
                     param_15, param_16);
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_system_reset 0x80135440
// Full per-match reset of the AI actor subsystem.
// ---------------------------------------------------------------------------
void aiactor_system_reset(void)
{
    u32 pairCount;
    int i;
    int slotOff;
    int count;

    DAT_806120fc = 0;
    DAT_80612094 = 0;
    if (DAT_80612614 < 0x65) {
        DAT_806120fc = (int)DAT_80501572;
    }
    DAT_8061209c = 0;
    DAT_806120a0 = 0;
    DAT_80610998 = 4;
    DAT_806120a4 = DAT_80612610;
    DAT_806120a8 = 0;
    memset_wrapper((int)0x805889c0, 0, 0x1b8);   // -0x7fa77564 sign-extended runtime addr
    DAT_80612100 = 0;
    memset_wrapper((int)0x805889ec, 0, 0x30);    // -0x7fa77614
    memset_wrapper((int)0x80588924, 0, 200);     // -0x7fa776dc
    DAT_806120ac = 0;
    memset_wrapper((int)0x80588884, 0, 0xa0);    // -0x7fa7777c
    DAT_806120b0 = 0;
    memset_wrapper((int)0x805886e0, 0, 0x28);    // -0x7fa77920
    DAT_806120b4 = 0;
    memset_wrapper((int)0x805887e4, 0, 0xa0);    // -0x7fa7781c
    DAT_80612090 = 0;
    DAT_806120f4 = 0;
    memset_wrapper((int)0x805887c0, 0, 0x10);    // -0x7fa77840
    DAT_806120ec = 0;
    memset_wrapper((int)0x805887d0, 0, 0x14);    // -0x7fa77830
    DAT_8061208c = 0;
    DAT_806120b8 = 0;
    DAT_806120bc = 0;
    memset_wrapper((int)0x80588708, 0, 0x78);    // -0x7fa778f8
    DAT_80612094 = 0;
    DAT_80612104 = 0;
    DAT_806120f8 = DAT_80612610;
    DAT_806120dc = 0;
    DAT_806120c0 = 0;
    DAT_806120e8 = 1;
    DAT_806120e4 = 0;
    DAT_805885b8 = 0;
    DAT_805885e4 = 0;
    DAT_806120e0 = 0;

    // Clear 16-entry table at DAT_80588780.
    slotOff = 0;
    count = 0x10;
    do {
        *(int *)((int)&DAT_80588780 + slotOff) = 0;
        slotOff = slotOff + 4;
        count = count + -1;
    } while (count != 0);

    // Reset each active player's weapon inventory + clear flag bit 2.
    slotOff = 0;
    for (i = 0; i < DAT_80612610 + DAT_806120fc; i = i + 1) {
        int poolOff = slotOff + 0x28;
        slotOff = slotOff + 0xea0;
        player_ai_reset_weapon_inventory();
        *(u32 *)(DAT_80611d90 + poolOff) = *(u32 *)(DAT_80611d90 + poolOff) & 0xfffffffd;
    }

    // Clear 10-entry table at DAT_80588590.
    slotOff = 0;
    count = 10;
    do {
        *(int *)((int)&DAT_80588590 + slotOff) = 0;
        slotOff = slotOff + 4;
        count = count + -1;
    } while (count != 0);

    DAT_8061208c = level_get_info_cached(DAT_80612614);
    {
        int n = FUN_8013dbac();
        pairCount = (u32)(n * (n + -1));
        memset_wrapper(DAT_80612088, 0,
                       (((int)pairCount >> 1) +
                        (u32)((int)pairCount < 0 && (pairCount & 1) != 0)) * 0xc + 0xf &
                       0xfffffff0);
    }
    memset_wrapper((int)0x80588690, 0, 0x50);    // -0x7fa77970
    FUN_80140940();
    if (DAT_805015bd == '\n') {
        FUN_8025c568();
    }
    else {
        FUN_802b8cc4();
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_system_alloc 0x80135734
// Allocate the pairwise-distance matrix and dispatch the spawn path.
// ---------------------------------------------------------------------------
void aiactor_system_alloc(unsigned long long param_1, unsigned long long param_2,
                          unsigned long long param_3, unsigned long long param_4,
                          unsigned long long param_5, unsigned long long param_6,
                          unsigned long long param_7, unsigned long long param_8,
                          u32 param_9, u32 param_10, u32 param_11, u32 param_12,
                          u32 param_13, u32 param_14, u32 param_15, u32 param_16)
{
    u32 pairCount;
    int n;
    unsigned long long handle;
    (void)param_9; (void)param_10;

    if (DAT_80611930 != 0) {
        n = FUN_8013dbac();
        pairCount = (u32)(n * (n + -1));
        DAT_80612088 = (int)mem_alloc(
            (s32)((((int)pairCount >> 1) +
                   (u32)((int)pairCount < 0 && (pairCount & 1) != 0)) * 0xc + 0xf & 0xfffffff0),
            1);
        handle = (unsigned long long)(u32)FUN_80140968();
        if (DAT_805015bd == '\n') {
            FUN_8025c614(handle, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                         &DAT_80501570, param_10, param_11, param_12, param_13, param_14,
                         param_15, param_16);
        }
        else {
            FUN_802b8d64(handle, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                         &DAT_80501570, param_10, param_11, param_12, param_13, param_14,
                         param_15, param_16);
        }
        aiactor_system_reset();
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_preload_resources 0x801357b0
// Preload AI actor character resources for the current level.
// ---------------------------------------------------------------------------
void aiactor_preload_resources(unsigned long long param_1, unsigned long long param_2,
                               unsigned long long param_3, unsigned long long param_4,
                               unsigned long long param_5, unsigned long long param_6,
                               unsigned long long param_7, unsigned long long param_8)
{
    int charCount;
    int i;
    unsigned long long ctx;
    int outB;
    int outA[3];

    if ((DAT_805015c0 & 0x20000000) == 0) {
        ctx = (unsigned long long)(u32)player_ai_attach_nop();
        if ((DAT_805015bd == '\n') && (DAT_80612614 != 100)) {
            for (i = 0; charCount = level_get_char_count(), i < charCount; i = i + 1) {
                ctx = (unsigned long long)(u32)level_get_default_weapon(i, outA, &outB);
                ctx = (unsigned long long)(u32)player_ai_preload_character_resources(
                    ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
            }
        }
        else if (0 < DAT_806120fc) {
            for (i = 0; i < (int)DAT_80501572; i = i + 1) {
                ctx = (unsigned long long)(u32)player_ai_preload_character_resources(
                    ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
            }
        }
        if (DAT_805015be == '\x13') {
            player_ai_preload_character_resources(
                ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        }
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_spawn_all 0x801358b4
// Level actor spawn/init pass + per-character death-gib model preload.
// ---------------------------------------------------------------------------
void aiactor_spawn_all(unsigned long long param_1, double param_2, double param_3, double param_4,
                       unsigned long long param_5, unsigned long long param_6,
                       unsigned long long param_7, unsigned long long param_8)
{
    int i;
    int slotOff;
    unsigned long long ctx;
    int outB;
    int outA[3];
    (void)param_1;

    DAT_8061208c = level_get_info_cached(DAT_80612614);
    DAT_80612098 = 0;
    memset_wrapper((int)&DAT_805885f0, 0, 0x80);

    // Init 8 actor-table slots (0x141c stride) up to base actor count.
    slotOff = 0;
    for (i = 0; i < 8; i = i + 1) {
        if (i < DAT_80612610) {
            FUN_8013db0c(*(int *)(DAT_80611d78 + slotOff + 0x14));
        }
        slotOff = slotOff + 0x141c;
    }

    ctx = (unsigned long long)(u32)player_ai_on_death_stub();
    if (DAT_805015bd == '\n') {
        int charCount;
        for (i = 0; charCount = level_get_char_count(), i < charCount; i = i + 1) {
            ctx = (unsigned long long)(u32)level_get_default_weapon(i, outA, &outB);
            if ((outA[0] != -1) && (player_ai_has_alt_model(outA[0]) == 0)) {
                ctx = (unsigned long long)(u32)player_ai_spawn_death_gibs(
                    ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                    outA[0], outB, 0);
            }
        }
    }
    else if (0 < DAT_806120fc) {
        for (i = 0; i < (int)DAT_80501572; i = i + 1) {
            int cid = (u32)(unsigned char)(&DAT_80501584)[i + DAT_80612610];
            ctx = (unsigned long long)(u32)player_ai_spawn_death_gibs(
                ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                cid, cid, 0);
        }
    }

    if (DAT_805015bd == '\n') {
        ctx = FUN_8025c6c8();
    }
    else {
        ctx = (unsigned long long)(u32)FUN_802b8da8(
            ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    FUN_80140974(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 (int)(ctx >> 0x20), (int)ctx, (int *)0, 0, 0, 0, 0, 0);
    return;
}

// ---------------------------------------------------------------------------
// aiactor_pre_tick 0x80135a08
// Per-frame pre-update gate.
// ---------------------------------------------------------------------------
void aiactor_pre_tick(void)
{
    if (FUN_80278ff0() != 0) {
        FUN_8027688c();
    }
    if ((DAT_805015bd == '\x03') && ('\0' < DAT_80501572)) {
        FUN_80272de0();
    }
    return;
}

// ---------------------------------------------------------------------------
// aiactor_tick_update 0x80135a58
// MASSIVE per-frame AI actor tick: callback round-robin, deferred zone-light
// propagation (coop), behavior tick, damage-flash decay, active-count mgmt.
// ---------------------------------------------------------------------------
void aiactor_tick_update(void)
{
    int *pNode;
    float fadeNum;
    float floorVal;
    u32 clz;
    int cbOff;
    int listOff;
    int slotEntry;
    int bit;
    int typeOff;
    int subOff;
    int subIdx;
    int subPtr;

    __save_gpr_25();
    if ((DAT_8061208c != 0) && (0 < DAT_806108e4)) {
        // Round-robin the level's per-frame callback table (+0x6c ptrs, +0x70 count).
        if (0 < *(int *)(DAT_8061208c + 0x70)) {
            cbOff = DAT_806120a8 * 4;
            DAT_806120a8 = DAT_806120a8 + 1;
            (**(void (**)(void))(*(int *)(DAT_8061208c + 0x6c) + cbOff))();
            if (*(int *)(DAT_8061208c + 0x70) <= DAT_806120a8) {
                DAT_806120a8 = 0;
            }
        }

        if (DAT_805015bd == '\n') {
            FUN_8025c9d8();
            if (((*(u32 *)(DAT_80611d54 + 0x21c) & 1) == 0) &&
                (FUN_80142b38() < DAT_80612098)) {
                FUN_8013e868();
            }
            // Propagate deferred zone-light color changes for the active buffer.
            listOff = 0;
            for (subIdx = 0; subIdx < (int)(&DAT_805885b8)[DAT_806120e4 * 0xb]; subIdx = subIdx + 1) {
                slotEntry = *(int *)((int)(&DAT_805885b8 + DAT_806120e4 * 0xb) + listOff + 4);
                if ((-1 < *(int *)(slotEntry + 0x114)) && (*(int *)(slotEntry + 0x114) < 0x100000)) {
                    bit = 0;
                    typeOff = 0;
                    do {
                        if ((*(u32 *)(slotEntry + 0x114) & 1 << bit) != 0) {
                            FUN_8013fec0(*(u32 *)(slotEntry + 0x114));
                            subOff = 0;
                            for (subIdx = 0; subIdx < *(int *)((int)&DAT_80587e10 + typeOff); subIdx = subIdx + 1) {
                                subPtr = *(int *)((int)&DAT_80587e10 + typeOff + subOff + 0x10);
                                if ((subPtr != 0) && (subPtr = *(int *)(subPtr + 0x7c), subPtr != 0)) {
                                    *(int *)(subPtr + 0xac8) = *(int *)(slotEntry + 0x110);
                                }
                                subOff = subOff + 4;
                            }
                        }
                        bit = bit + 1;
                        typeOff = typeOff + 0x60;
                    } while (bit < 0x14);
                }
                listOff = listOff + 4;
            }
            clz = countLeadingZeros(DAT_806120e4);
            DAT_806120e4 = clz >> 5;   // toggle 0<->1
            (&DAT_805885b8)[DAT_806120e4 * 0xb] = 0;
        }

        FUN_80138e74();   // behavior tick

        if (DAT_805015bd == '\n') {
            FUN_802603a4();
            clz = (u32)DAT_806109c0;
            fadeNum = FLOAT_806108ec;
            cbOff = 0;
            subIdx = 0x14;
            do {
                floorVal = FLOAT_80615cac;
                if (*(int *)((int)&DAT_80587e14 + cbOff) != 0) {
                    *(float *)((int)&DAT_80587e1c + cbOff) =
                        -((FLOAT_80615ca8 /
                           (float)((double)CONCAT44(0x43300000, clz ^ 0x80000000) - DOUBLE_80615cb0)) *
                              fadeNum -
                          *(float *)((int)&DAT_80587e1c + cbOff));
                    if (*(float *)((int)&DAT_80587e1c + cbOff) <= floorVal) {
                        *(float *)((int)&DAT_80587e1c + cbOff) = floorVal;
                        *(int *)((int)&DAT_80587e14 + cbOff) = 0;
                    }
                }
                cbOff = cbOff + 0x60;
                subIdx = subIdx + -1;
            } while (subIdx != 0);
        }
        else {
            FUN_802ba204();
            if (DAT_805015bd != '\n') {
                cbOff = 0;
                for (subIdx = 0; subIdx < DAT_80612610; subIdx = subIdx + 1) {
                    FUN_80280b94((int *)(DAT_80611d90 + cbOff));
                    cbOff = cbOff + 0xea0;
                }
            }
        }

        // Advance active-actor cursor (wraps back to the base count).
        DAT_806120a4 = DAT_806120a4 + 1;
        if (DAT_80612610 + DAT_806120fc <= DAT_806120a4) {
            DAT_806120a4 = DAT_80612610;
        }

        // Count live story-extra actors.
        DAT_806120f0 = 0;
        cbOff = 0;
        subIdx = DAT_806120fc;
        if (0 < DAT_806120fc) {
            do {
                if ((*(int *)((int)&DAT_80588a1c + cbOff) != 0) &&
                    ((*(u32 *)(*(int *)((int)&DAT_80588a1c + cbOff) + 0x10) & 0xffff) != 0)) {
                    DAT_806120f0 = DAT_806120f0 + 1;
                }
                cbOff = cbOff + 4;
                subIdx = subIdx + -1;
            } while (subIdx != 0);
        }

        // Clear the per-visible-node scratch flag (+0xa0).
        cbOff = 0;
        for (subIdx = 0; subIdx < DAT_80612098; subIdx = subIdx + 1) {
            pNode = (int *)((int)&DAT_80588610 + cbOff);
            cbOff = cbOff + 4;
            *(int *)(*(int *)*pNode + 0xa0) = 0;
        }

        if ((DAT_806120e8 == 0) && (DAT_806120f0 == 0)) {
            DAT_806120e8 = 1;
        }
    }
    __restore_gpr_25();
    return;
}

// ---------------------------------------------------------------------------
// aiactor_update_passability_flag 0x80135dc0
// Set/clear the mover passability flag (bit 0x10000 at render_data+0x2c) for
// an actor's linked render object, based on occupancy / visibility state.
// ---------------------------------------------------------------------------
void aiactor_update_passability_flag(int actor)
{
    int rdata;
    u32 occupied;
    bool blocked;

    rdata = *(int *)(actor + 0x7c);
    blocked = (*(u32 *)(actor + 0x10) & 0xffff) != 0;
    if (!blocked) {
        blocked = (*(u32 *)(rdata + 0x14) & 0x60000) != 0;
    }
    occupied = (u32)blocked;
    if (occupied == 0) {
        occupied = *(u32 *)(rdata + 0x28) >> 0xe & 1;
    }
    blocked = occupied != 0;
    if (!blocked) {
        blocked = *(int *)(rdata + 0xe98) != 0;
    }
    if (blocked) {
        *(u32 *)(rdata + 0x2c) = *(u32 *)(rdata + 0x2c) & 0xfffeffff;
        return;
    }
    *(u32 *)(rdata + 0x2c) = *(u32 *)(rdata + 0x2c) | 0x10000;
    return;
}
