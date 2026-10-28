// Batch: batch_983 | Module: level entity-spawn setup + spawned-entity pool
// Address range: 0x803490f4 - 0x803494b4
// Date: 2026-09-12
//
// The level-load spawn pipeline entered from level_spawn_all_entities (0x80038544). Given the current
// game mode (DAT_805015bd), level_spawn_dispatch selects one of three spawn setups, all of which run
// aiactor_spawn_all then a mode-specific post-spawn chain (FUN_8034df30 = entity fixup,
// FUN_803507a4 = objective wiring). level_alloc_entity_pool sizes and allocates the run-time
// spawned-entity pool DAT_80612db0 (a 0x1960-byte header plus per-category sub-arrays) by first
// counting the level's member/cell records by class. entity_pool_find_type1 / _type2 are linear
// lookups into two of those sub-arrays by id.
//
//   scene_flag_bit25       0x803490f4  bit 25 of the scene config value for slot (ctx+0xdec)-1.
//   scene_flag_bit25_bool  0x80349120  the above normalized to 0/1 (clz>>5).
//   spawn_mode_variant     0x8034915c  small classifier over DAT_805015bd (mode 10 -> 0, mode 3 -> 1, else -1).
//   level_spawn_default_set 0x80349194 spawn + FUN_8034df30 fixup with the default info block DAT_805c7ae0.
//   level_spawn_mode3_set  0x803491c0  cached-level-info spawn + fixup + objective wiring + worm registry.
//   level_spawn_actors_only 0x80349218 plain aiactor_spawn_all.
//   level_spawn_dispatch   0x80349238  mode switch -> one of the three setups; resets DAT_80612da8/timer.
//   level_alloc_entity_pool 0x8034929c count records by class, allocate DAT_80612db0 + sub-arrays.
//   entity_pool_find_type1 0x8034946c  find pool[0x84] entry (stride 0x28) whose +4 key == id.
//   entity_pool_find_type2 0x803494b4  find pool[0x8c] entry (stride 0x24) whose key == id.
//
// NON-MATCHING: reproduces the decompiler's structure. The spawn wrappers keep the mode-specific call
// chains; named helpers (aiactor_spawn_all, mem_alloc, scene_get_config_value, level_get_info_cached,
// worm_event_registry_init) are called through their canonical shared-header prototypes, so the
// decompiler's ABI-widened register-passthrough tails are dropped (Compile-Clean rule 1/6). The raw
// mode-post-spawn helpers (FUN_8034df30/FUN_803507a4/FUN_8034e194) and guard stubs are declared
// file-locally (rule 5). countLeadingZeros comes from common.h.

#include "common.h"

extern "C" {

// ---- named cross-module helpers (canonical prototypes) ---------------------
extern unsigned int scene_get_config_value(int index);
extern void aiactor_spawn_all(unsigned long long a1, double a2, double a3, double a4,
                              unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8);
extern void* level_get_info_cached(int nLevelId);
extern void worm_event_registry_init(void);
extern int  mem_alloc(int size, int type);

// ---- raw callees (file-local, faithful to call-site) -----------------------
extern int  FUN_803826e4(void);          // editor/level context pointer
extern void FUN_80382730(void);          // pop guard
extern int  FUN_8034e194(void);          // fallback spawn context provider
extern void FUN_8034df30(unsigned long long a1, double a2, double a3, double a4,
                         unsigned long long a5, unsigned long long a6, unsigned long long a7,
                         unsigned long long a8, void* info, int r4, int p11, int p12,
                         int p13, int p14, int p15, int p16);   // entity fixup
extern void FUN_803507a4(unsigned long long a1, double a2, double a3, double a4,
                         unsigned long long a5, unsigned long long a6, unsigned long long a7,
                         unsigned long long a8, void* info, int r4, int p11, int p12,
                         int p13, int p14, int p15, int p16);   // objective wiring

// ---- data symbols ----------------------------------------------------------
extern int   DAT_805015bd;      // current game mode id
extern int   DAT_80501570;      // default spawn param block
extern int   DAT_805c7ae0;      // default level info block
extern int   DAT_80612614;      // current level id
extern int   DAT_80612ae8;      // spawn context override flag
extern int*  DAT_80612db0;      // spawned-entity pool
extern int   DAT_80612da8;      // active spawn index (reset to -1)
extern float FLOAT_80612da4;    // spawn timer
extern float FLOAT_8061ad00;    // spawn timer reset value

// ---------------------------------------------------------------------------
// 0x803490f4 : bit 25 of the scene config value for the slot at (ctx+0xdec)-1.
// ---------------------------------------------------------------------------
unsigned int scene_flag_bit25(int ctx)
{
    return scene_get_config_value(*(int*)(ctx + 0xdec) + -1) >> 0x19 & 1;
}

// ---------------------------------------------------------------------------
// 0x80349120 : the above normalized to a boolean (0/1) via clz>>5.
// ---------------------------------------------------------------------------
unsigned int scene_flag_bit25_bool(int ctx)
{
    return countLeadingZeros(scene_flag_bit25(ctx)) >> 5;
}

// ---------------------------------------------------------------------------
// 0x8034915c : classify the current game mode.
// ---------------------------------------------------------------------------
int spawn_mode_variant(void)
{
    if (DAT_805015bd == 10) { return 0; }
    if (9 < DAT_805015bd)   { return -1; }
    if (DAT_805015bd != 3)  { return -1; }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x80349194 : default spawn + entity fixup with the default info block.
// ---------------------------------------------------------------------------
void level_spawn_default_set(unsigned long long a1, double a2, double a3, double a4,
                             unsigned long long a5, unsigned long long a6, unsigned long long a7,
                             unsigned long long a8, int p9, int p10, int p11, int p12,
                             int p13, int p14, int p15, int p16)
{
    (void)p9; (void)p10;
    aiactor_spawn_all(a1, a2, a3, a4, a5, a6, a7, a8);
    FUN_8034df30(a1, a2, a3, a4, a5, a6, a7, a8, &DAT_805c7ae0, 0, p11, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x803491c0 : cached-level-info spawn + fixup + objective wiring + worm init.
// ---------------------------------------------------------------------------
void level_spawn_mode3_set(unsigned long long a1, double a2, double a3, double a4,
                           unsigned long long a5, unsigned long long a6, unsigned long long a7,
                           unsigned long long a8, int p9, int p10, int p11, int p12,
                           int p13, int p14, int p15, int p16)
{
    void* info;
    (void)p9; (void)p10;

    info = level_get_info_cached(DAT_80612614);
    if (DAT_80612ae8 == 0) {
        a1 = (unsigned long long)(unsigned)FUN_8034e194();
    }
    aiactor_spawn_all(a1, a2, a3, a4, a5, a6, a7, a8);
    FUN_8034df30(a1, a2, a3, a4, a5, a6, a7, a8, info, 0, p11, p12, p13, p14, p15, p16);
    FUN_803507a4(a1, a2, a3, a4, a5, a6, a7, a8, info, 0, p11, p12, p13, p14, p15, p16);
    worm_event_registry_init();
}

// ---------------------------------------------------------------------------
// 0x80349218 : plain actor spawn.
// ---------------------------------------------------------------------------
void level_spawn_actors_only(unsigned long long a1, double a2, double a3, double a4,
                             unsigned long long a5, unsigned long long a6, unsigned long long a7,
                             unsigned long long a8)
{
    aiactor_spawn_all(a1, a2, a3, a4, a5, a6, a7, a8);
}

// ---------------------------------------------------------------------------
// 0x80349238 : dispatch to a spawn setup based on the current game mode.
// ---------------------------------------------------------------------------
void level_spawn_dispatch(unsigned long long a1, double a2, double a3, double a4,
                          unsigned long long a5, unsigned long long a6, unsigned long long a7,
                          unsigned long long a8, int p9, int p10, int p11, int p12,
                          int p13, int p14, int p15, int p16)
{
    (void)p9;
    if (DAT_805015bd == 10) {
        level_spawn_default_set(a1, a2, a3, a4, a5, a6, a7, a8, (int)&DAT_80501570,
                                p10, p11, p12, p13, p14, p15, p16);
    }
    else if ((DAT_805015bd < 10) && (DAT_805015bd == 3)) {
        level_spawn_mode3_set(a1, a2, a3, a4, a5, a6, a7, a8, (int)&DAT_80501570,
                              p10, p11, p12, p13, p14, p15, p16);
    }
    else {
        level_spawn_actors_only(a1, a2, a3, a4, a5, a6, a7, a8);
    }
    DAT_80612da8 = -1;
    FLOAT_80612da4 = FLOAT_8061ad00;
}

// ---------------------------------------------------------------------------
// 0x8034929c : count records by class, allocate the spawned-entity pool + sub-arrays.
// ---------------------------------------------------------------------------
void level_alloc_entity_pool(void)
{
    int ctx;
    int count;
    int off;
    int cls;
    int n_type1 = 0;   // member class 1
    int n_type2 = 0;   // member class 2
    int n_c45 = 0;     // cell class 4/5
    int n_c4 = 0;      // cell class 4
    int n_c3 = 0;      // cell class 3

    ctx = FUN_803826e4();

    count = *(int*)(ctx + 300);
    off = 0;
    while (0 < count) {
        cls = *(int*)(*(int*)(ctx + 0x130) + off);
        if (cls == 1)      { n_type1 = n_type1 + 1; }
        else if (cls == 2) { n_type2 = n_type2 + 1; }
        off = off + 0xb4;
        count = count - 1;
    }

    count = *(int*)(ctx + 0x124);
    off = 0;
    while (0 < count) {
        cls = *(int*)(*(int*)(ctx + 0x128) + off + 0x80);
        if (cls == 4)      { n_c4 = n_c4 + 1; }
        else if (cls < 4)  { if (2 < cls) { n_c3 = n_c3 + 1; } }
        else if (cls < 6)  { n_c45 = n_c45 + 1; }
        off = off + 0x8c;
        count = count - 1;
    }

    DAT_80612db0 = (int*)mem_alloc(0x1960, 1);
    if (0 < *(int*)(ctx + 0xd8)) {
        DAT_80612db0[0] = mem_alloc((*(int*)(ctx + 0xd8) * 0x25e + 0xf) & 0xfffffff0, 1);
    }
    if (0 < n_type1) { DAT_80612db0[0x21] = mem_alloc((n_type1 * 0x28 + 0xf) & 0xfffffff0, 1); }
    if (0 < n_type2) { DAT_80612db0[0x23] = mem_alloc((n_type2 * 0x24 + 0xf) & 0xfffffff0, 1); }
    if (0 < n_c45)   { DAT_80612db0[0x26] = mem_alloc(n_c45 << 4, 1); }
    if (0 < n_c4)    { DAT_80612db0[0x28] = mem_alloc(n_c4 << 4, 1); }
    if (0 < n_c3)    { DAT_80612db0[0x2c] = mem_alloc((n_c3 * 0x58 + 0xf) & 0xfffffff0, 1); }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8034946c : find the class-1 pool entry (stride 0x28) whose +4 key == id.
// ---------------------------------------------------------------------------
int entity_pool_find_type1(int id)
{
    int off = 0;
    int count = *(int*)((int)DAT_80612db0 + 0x80);
    while (0 < count) {
        if (id == *(int*)(*(int*)((int)DAT_80612db0 + 0x84) + off + 4)) {
            return *(int*)((int)DAT_80612db0 + 0x84) + off + 4;
        }
        off = off + 0x28;
        count = count - 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x803494b4 : find the class-2 pool entry (stride 0x24) whose key == id.
// ---------------------------------------------------------------------------
int entity_pool_find_type2(int id)
{
    int off = 0;
    int count = *(int*)((int)DAT_80612db0 + 0x88);
    while (0 < count) {
        if (id == *(int*)(*(int*)((int)DAT_80612db0 + 0x8c) + off)) {
            return *(int*)((int)DAT_80612db0 + 0x8c) + off;
        }
        off = off + 0x24;
        count = count - 1;
    }
    return 0;
}

} // extern "C"
