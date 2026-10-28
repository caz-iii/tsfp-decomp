// worm_ekun_state.cpp
// CDH1 "EKUN" worm-boss set-piece: level init + the per-entity checkpoint
// save/restore state callbacks and the remaining progress/team payload gates.
// Address range 0x801ea31c - 0x801eb153 (batch_621), continuation of
// worm_ekun_update.cpp. subsystem:game, worm-effect module.
//
// The save/restore callbacks share a mode convention (param==1 save/snapshot,
// param==0 restore, other values = variant snapshots) and mirror an entity's
// runtime fields into a set of DAT_805c5e** / DAT_805c4e** shadow globals so a
// checkpoint reload can put the set-piece back into its saved pose/state.
//
// Functions:
//   ekun_progress_threshold_gates (0x801ea31c) fire the scripted payload as the
//                                 worm progress ratio crosses each of 4 bands.
//   ekun_dumb_walk_hide_absent_gate (0x801ea3e8) fire the payload once the DUMB/
//                                 WALK/WILK/HIDE teams are all absent.
//   ekun_level_init              (0x801ea47c) the big CDH1 EKUN level init: reset
//                                 latches, arc state and bolt state, register the
//                                 two level actor banks, stop the ambient loops,
//                                 clear the debris pool, flag ~13 soundpad zones,
//                                 install the HUD objective slots (0..0x2a), build
//                                 the particle bank, allocate the shutter RT,
//                                 deactivate 6 soundpad portals, and seed the
//                                 checkpoint shadow globals.
//   ekun_lptr_artr_save_restore  (0x801eac10) save/restore the 'LPTR' + 'ARTR'
//                                 entity positions.
//   ekun_fbrl_cleanup            (0x801eacbc) detect the 'FBRL' entity on save;
//                                 on restore, destroy all matching 'FBRL' entities.
//   ekun_stop_ambient_loop_a     (0x801ead8c) stop ambient loop A (DAT_80610de8).
//   ekun_rock_state_save_restore (0x801eadc0) save/restore the 'ROCK' entity anim
//                                 state word (+0xa0).
//   ekun_nuke_state_save_restore (0x801eae40) save/restore the 'NUKE' entity anim
//                                 state + fill (modes 0..4).
//   ekun_safe_spawn_save_restore (0x801eafc0) save/restore the 'SAFE' spawned
//                                 entity; re-spawn it (spawn_create_instance) on
//                                 restore if it was live.
//   ekun_trcp_state_save         (0x801eb0cc) snapshot the 'TRCP' entity transform
//                                 (pos/rot/scale) into the shadow globals.
//
// Non-matching: entity ids are 4CC class words; PPC FP spill frames
// (FUN_803826f0 open / FUN_8038273c close) are opaque; the ROM's uVarN return
// chaining across the void level_* / arc_* helpers is register reuse and dropped.
// Reduced-arity generated_decls helpers are routed through file-local typed
// fn-ptr aliases (__we3_*). memset_wrapper resolves via generated_externs.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- raw data symbols (file-local) ---
extern int   DAT_80611b8c;      // soundpad zone table base
extern int   DAT_80612690;      // SAFE spawned entity handle
extern int   DAT_80612694;      // OILY zone latch
extern int   DAT_80612698;      // KHLR door one-shot latch
extern int   DAT_806126a4;      // misc latch
extern int   DAT_806126d4;      // shutter "closed" latch
extern int   DAT_80612720;      // deferred one-shot flag
extern int   DAT_806128a0;      // HUD objective slot handle (last installed)
extern int   DAT_8061271c;      // TRN3 bark one-shot latch
extern int   DAT_80610d78;      // active loop-sound handle
extern int   DAT_80610de8;      // ambient loop A handle
extern int   DAT_80610dec;      // ambient loop B handle
extern float FLOAT_806126a8;    // misc float latch

// checkpoint shadow globals
extern int   DAT_805c4e0c; extern int DAT_805c4e1c; extern int DAT_805c4e20;
extern int   DAT_805c4e2c; extern int DAT_805c4e30; extern int DAT_805c4e34;
extern int   DAT_805c4e38; extern int DAT_805c4e3c; extern int DAT_805c4e40;
extern int   DAT_805c4e44;
extern int   DAT_805c4f5c;      // debris/piece pool base
extern int   DAT_805c5ed0; extern int DAT_805c5ed4; extern int DAT_805c5ed8;
extern int   DAT_805c5edc; extern int DAT_805c5ee4;
extern int   DAT_805c5eec; extern int DAT_805c5ef0; extern int DAT_805c5ef4;
extern float DAT_805c5ee8;
extern int   DAT_804523b4; extern int DAT_804524cc; // level actor bank descriptors
extern int   DAT_80452578; extern int DAT_8045257c; extern int DAT_80452580;
extern int   DAT_803df8d0; extern int DAT_803df8d4; extern int DAT_803df8d8; // SAFE spawn offset vec

extern float FLOAT_80617428;    // 1.0f
extern float FLOAT_80617440;    // 0.0f
extern float FLOAT_80617664;    // progress band edge
extern float FLOAT_806176b4; extern float FLOAT_806176b8;
extern float FLOAT_806176bc; extern float FLOAT_806176c0;
extern float FLOAT_806176c4;
extern float FLOAT_806176c8;    // NUKE fill clamp

// --- raw code symbols (file-local; not in generated headers or wrong arity) ---
extern int   FUN_803826f0(void);   // FP spill frame open (returns save/restore mode)
extern void  FUN_8038273c(void);   // FP spill frame close
extern void  FUN_80221074(double a, double b, double c, double d,
                          double e, double f, double g, double h); // scripted payload fire
extern int   FUN_8023c32c(int tag);        // entity lookup by 4CC
extern int   FUN_8023c520(int tag);        // entity lookup variant by 4CC
extern int   FUN_8023c59c(int tag, int outArray, int maxCount); // collect entities by 4CC
extern void  FUN_80369a18(void);           // level sub-init hook
extern void  FUN_802215a8(int slot, int stringId, int a, int b, int c); // HUD objective slot setup
extern void  FUN_8022126c(int slotHandle); // HUD objective slot commit
extern void  FUN_802217d4(void *cb);       // HUD objective slot callback install
extern void  FUN_8022161c(int id);         // HUD objective marker
extern int   FUN_8025b8c0(int mode);       // game-mode query

} // extern "C"

// ekun_actor_bank_save_restore_dispatch (0x801eb1ac) is the installed HUD/
// checkpoint callback; defined later in this file (batch_622). Forward-declare
// it here (C++ linkage) so ekun_level_init can take its address.
void ekun_actor_bank_save_restore_dispatch(unsigned long long, double, double, double,
                                           double, double, double, double);

// worm_ekun_progress_ratio, soundpad_find_zone_by_id, soundpad_get_portal_index,
// soundpad_portal_deactivate, entity_destroy_with_event, spawn_def_get_entry,
// arc_state_reset, shutter_rt_alloc, ekun_init_particle_bank are declared with
// C++ linkage in generated_decls.h (via entity.h); do not re-declare.

// --- typed fn-ptr aliases for reduced-arity generated_decls prototypes ---

// arc_bolt_reset declared (void); returns the running FP/render context.
static unsigned long long (*const __we3_arc_bolt_reset)(void) =
    (unsigned long long (*)(void))&arc_bolt_reset;

// level_actors_register declared (int,int); the ROM form takes the FP context +
// 7 doubles then (bankIdx, descPtr, count) and returns the running context.
static unsigned long long (*const __we3_level_actors_register)(unsigned long long,
        double, double, double, double, double, double, double, int, int *, int) =
    (unsigned long long (*)(unsigned long long, double, double, double, double,
        double, double, double, int, int *, int))&level_actors_register;

// level_resource_pool_setup declared (void); the ROM form takes the FP context.
static void (*const __we3_level_resource_pool_setup)(unsigned long long, double, double,
        double, double, double, double, double) =
    (void (*)(unsigned long long, double, double, double, double, double, double,
        double))&level_resource_pool_setup;

// sound_stop_handle declared (u32); these call sites pass the handle implicitly.
static void (*const __we3_sound_stop)(void) =
    (void (*)(void))&sound_stop_handle;

// spawn_create_instance declared (void); the ROM form is the full 18-arg spawn.
static int (*const __we3_spawn_create_instance)(unsigned long long, double, double,
        double, double, double, double, double, int, int, int, int, void *, int *,
        int, int, int, int) =
    (int (*)(unsigned long long, double, double, double, double, double, double,
        double, int, int, int, int, void *, int *, int, int, int, int))
    &spawn_create_instance;


// ---------------------------------------------------------------------------
// 0x801ea31c  ekun_progress_threshold_gates
// ---------------------------------------------------------------------------
void ekun_progress_threshold_gates(unsigned long long param_1, double param_2, double param_3,
                                   double param_4, double param_5, double param_6, double param_7,
                                   double param_8)
{
    (void)param_1;
    double dVar1 = worm_ekun_progress_ratio();
    if ((dVar1 <= (double)FLOAT_806176b4) && ((double)FLOAT_806176b8 < dVar1)) {
        FUN_80221074((double)FLOAT_80617440, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if ((dVar1 <= (double)FLOAT_806176bc) && ((double)FLOAT_806176c0 < dVar1)) {
        FUN_80221074((double)FLOAT_80617440, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if ((dVar1 <= (double)FLOAT_806176c4) && ((double)FLOAT_80617664 < dVar1)) {
        FUN_80221074((double)FLOAT_80617440, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if ((dVar1 < (double)FLOAT_80617428) && ((double)FLOAT_80617440 < dVar1)) {
        FUN_80221074((double)FLOAT_80617440, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
}


// ---------------------------------------------------------------------------
// 0x801ea3e8  ekun_dumb_walk_hide_absent_gate
// ---------------------------------------------------------------------------
void ekun_dumb_walk_hide_absent_gate(unsigned long long param_1, double param_2, double param_3,
                                     double param_4, double param_5, double param_6, double param_7,
                                     double param_8)
{
    (void)param_1;
    FUN_803826f0();
    int iVar1 = aiactor_find_player_by_team(0x44554d42);  // 'DUMB'
    int iVar2 = aiactor_find_player_by_team(0x57414c4b);  // 'WALK'
    int iVar3 = aiactor_find_player_by_team(0x57494c4b);  // 'WILK'
    int iVar4 = aiactor_find_player_by_team(0x48494445);  // 'HIDE'
    if ((iVar1 == 0) && (iVar2 == 0) && (iVar3 == 0) && (iVar4 == 0)) {
        FUN_80221074((double)FLOAT_80617440, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    FUN_8038273c();
}


// ---------------------------------------------------------------------------
// 0x801ea47c  ekun_level_init
// ---------------------------------------------------------------------------
void ekun_level_init(unsigned long long param_1, double param_2, double param_3, double param_4,
                     double param_5, double param_6, double param_7, double param_8)
{
    int iVar1, iVar2;
    unsigned long long ctx;

    DAT_80612698 = 0;
    DAT_8061271c = 0;
    FUN_80369a18();
    arc_state_reset();
    ctx = __we3_arc_bolt_reset();
    ctx = __we3_level_actors_register(ctx, param_2, param_3, param_4, param_5, param_6, param_7,
                                      param_8, 0, &DAT_804523b4, 7);
    ctx = __we3_level_actors_register(ctx, param_2, param_3, param_4, param_5, param_6, param_7,
                                      param_8, 1, &DAT_804524cc, 7);
    __we3_level_resource_pool_setup(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    (void)param_1;

    __we3_sound_stop(); DAT_80610d78 = -1;
    __we3_sound_stop(); DAT_80610de8 = -1;
    __we3_sound_stop(); DAT_80610dec = -1;

    // clear the 12-entry debris/piece pool (stride 0xc)
    iVar2 = 0xc;
    iVar1 = 0;
    do {
        *(int *)((int)&DAT_805c4f5c + iVar1) = 0;
        iVar2 = iVar2 - 1;
        iVar1 = iVar1 + 0xc;
    } while (iVar2 != 0);
    memset_wrapper(-0x7fa3b204, 0, 0x50);

    // flag the scripted soundpad zones (0x10000 = one kind, 0x20000 = another)
    static const int kZones10000a[] = { 0x4de };
    static const int kZones20000[]  = { 0x427, 0x429, 0x42b, 0x425, 0x4d9, 0x60d, 0x60e, 0x5ba };
    static const int kZones10000b[] = { 0x5f6, 0x650, 0x653, 0x689 };
    for (int i = 0; i < (int)(sizeof(kZones10000a)/sizeof(int)); ++i) {
        iVar1 = DAT_80611b8c + soundpad_find_zone_by_id(kZones10000a[i]) * 0x20;
        *(unsigned int *)(iVar1 + 4) |= 0x10000;
    }
    for (int i = 0; i < (int)(sizeof(kZones20000)/sizeof(int)); ++i) {
        iVar1 = DAT_80611b8c + soundpad_find_zone_by_id(kZones20000[i]) * 0x20;
        *(unsigned int *)(iVar1 + 4) |= 0x20000;
    }
    for (int i = 0; i < (int)(sizeof(kZones10000b)/sizeof(int)); ++i) {
        iVar1 = DAT_80611b8c + soundpad_find_zone_by_id(kZones10000b[i]) * 0x20;
        *(unsigned int *)(iVar1 + 4) |= 0x10000;
    }

    // install the HUD objective slots
    FUN_802215a8(0, 0xf89, 0, 0, 0); FUN_8022126c(DAT_806128a0);
    FUN_802215a8(1, 0xf85, 0, 0, 0); FUN_8022126c(DAT_806128a0);
    FUN_802215a8(2, 0xf88, 0, 0, 0); FUN_8022126c(DAT_806128a0);
    FUN_802215a8(3, 0xf86, 0, 0, 0); FUN_8022126c(DAT_806128a0);
    FUN_802215a8(4, 0xf8a, 0, 0, 0); FUN_8022126c(DAT_806128a0);
    FUN_802215a8(5, 0xf87, 0, 0, 0); FUN_8022126c(DAT_806128a0);
    FUN_802215a8(6, 0xf8b, 2, 2, 2);
    FUN_802215a8(7, 0xf8c, 2, 2, 2);
    FUN_802217d4((void *)(void (*)(unsigned long long, double, double, double,
                                   double, double, double, double))
                 &ekun_actor_bank_save_restore_dispatch);
    FUN_802215a8(8, 0xf8d, 2, 2, 2);
    FUN_802215a8(9, 0, 2, 2, 2);
    FUN_8022161c(0x421);
    for (int slot = 10; slot <= 0x2a; ++slot) {
        FUN_802215a8(slot, 0, 2, 2, 2);
    }

    ekun_init_particle_bank();
    shutter_rt_alloc();

    // deactivate the scripted soundpad portals
    static const int kPortals[][2] = {
        {0x456, 0x57a}, {0x577, 0x576}, {0x56c, 0x56b},
        {0x403, 0x40b}, {0x404, 0x58c}, {0x572, 0x571}
    };
    for (int i = 0; i < (int)(sizeof(kPortals)/sizeof(kPortals[0])); ++i) {
        soundpad_portal_deactivate(soundpad_get_portal_index(kPortals[i][0], kPortals[i][1]));
    }

    DAT_806126a4 = 0;
    FLOAT_806126a8 = FLOAT_80617440;
    DAT_806126d4 = 1;
    DAT_80612690 = 0;
    DAT_80612694 = 0;
    DAT_80612720 = 0;
    if (FUN_8025b8c0(0) == 0) {
        iVar2 = 3;
        iVar1 = 0;
        do {
            *(int *)((int)&DAT_80452578 + iVar1) = 1;
            *(int *)((int)&DAT_8045257c + iVar1) = 0;
            *(int *)((int)&DAT_80452580 + iVar1) = 0;
            iVar2 = iVar2 - 1;
            iVar1 = iVar1 + 0x10;
        } while (iVar2 != 0);
    }
}


// ---------------------------------------------------------------------------
// 0x801eac10  ekun_lptr_artr_save_restore
// ---------------------------------------------------------------------------
void ekun_lptr_artr_save_restore(int param_1)
{
    int iVar2 = FUN_8023c32c(0x4c505452);  // 'LPTR'
    int iVar3 = FUN_8023c32c(0x41525452);  // 'ARTR'
    if ((iVar2 != 0) && (iVar3 != 0)) {
        iVar3 = *(int *)(iVar3 + 0x7c);
        iVar2 = *(int *)(iVar2 + 0x7c);
        if (param_1 == 0) {
            *(int *)(iVar2 + 0x18) = DAT_805c5ef0;
            *(int *)(iVar2 + 0x10) = DAT_805c5ef0;
            *(int *)(iVar3 + 0x18) = DAT_805c5ef4;
            *(int *)(iVar3 + 0x10) = DAT_805c5ef4;
        } else {
            DAT_805c5ef0 = *(int *)(iVar2 + 0x18);
            DAT_805c5ef4 = *(int *)(iVar3 + 0x18);
        }
    }
}


// ---------------------------------------------------------------------------
// 0x801eacbc  ekun_fbrl_cleanup
// ---------------------------------------------------------------------------
void ekun_fbrl_cleanup(void)
{
    int iVar3;
    unsigned int uStack_5c;          // entity-handle scratch array head
    unsigned char auStack_58[88];

    int iVar2 = FUN_803826f0();
    iVar3 = FUN_8023c520(0x4642524c);  // 'FBRL'
    if (iVar2 == 1) {
        if ((iVar3 == 0) || (*(int *)(iVar3 + 0x19c) == 0)) {
            DAT_805c5ed0 = 1;
        } else {
            DAT_805c5ed0 = 0;
        }
    } else if (DAT_805c5ed0 != 0) {
        iVar3 = FUN_8023c59c(0x4642524c, (int)(uintptr_t)auStack_58, 0x10);
        int iVar2b = iVar3 << 2;
        while (true) {
            bool bDone = (iVar3 == 0);
            iVar3 = iVar3 - 1;
            if (bDone) break;
            entity_destroy_with_event(*(int **)((char *)&uStack_5c + iVar2b));
            iVar2b = iVar2b - 4;
        }
    }
    FUN_8038273c();
}


// ---------------------------------------------------------------------------
// 0x801ead8c  ekun_stop_ambient_loop_a
// ---------------------------------------------------------------------------
void ekun_stop_ambient_loop_a(void)
{
    if (-1 < DAT_80610de8) {
        __we3_sound_stop();
        DAT_80610de8 = -1;
    }
}


// ---------------------------------------------------------------------------
// 0x801eadc0  ekun_rock_state_save_restore
// ---------------------------------------------------------------------------
int ekun_rock_state_save_restore(int param_1)
{
    int iVar1 = FUN_8023c32c(0x524f434b);  // 'ROCK'
    if (iVar1 == 0) {
        return 0;
    }
    if (param_1 == 1) {
        DAT_805c5ed4 = *(int *)(*(int *)(iVar1 + 0x7c) + 0xa0);
    } else if (param_1 == 0) {
        *(int *)(*(int *)(iVar1 + 0x7c) + 0xa0) = DAT_805c5ed4;
    }
    return 1;
}


// ---------------------------------------------------------------------------
// 0x801eae40  ekun_nuke_state_save_restore
// ---------------------------------------------------------------------------
int ekun_nuke_state_save_restore(int param_1)
{
    float fVar1;
    int iVar2 = FUN_8023c32c(0x4e554b45);  // 'NUKE'
    if (param_1 == 4) {
        DAT_805c5ed8 = DAT_805c4e0c;
        fVar1 = DAT_805c5ee8;
        if ((DAT_805c4e0c == 0) && (DAT_805c5ee8 < FLOAT_806176c8)) {
            fVar1 = FLOAT_806176c8;
            if (DAT_805c5ee8 < FLOAT_806176c8) { fVar1 = DAT_805c5ee8; }
        }
        DAT_805c5ee8 = fVar1;
        return 1;
    }
    if (iVar2 == 0) {
        return 0;
    }
    iVar2 = *(int *)(iVar2 + 0x7c);
    if (param_1 == 1) {
        DAT_805c5ed8 = *(int *)(iVar2 + 0xa0);
        if (DAT_805c5ed8 == 0) {
            DAT_805c5ee8 = FLOAT_806176c8;
            if (*(float *)(iVar2 + 0x10) < FLOAT_806176c8) {
                DAT_805c5ee8 = *(float *)(iVar2 + 0x10);
            }
        } else {
            DAT_805c5ee8 = *(float *)(iVar2 + 0x10);
        }
        DAT_805c5eec = *(int *)(iVar2 + 0x18);
    } else if (param_1 == 0) {
        *(int *)(iVar2 + 0xa0) = DAT_805c5ed8;
        *(float *)(iVar2 + 0x10) = DAT_805c5ee8;
        *(int *)(iVar2 + 0x18) = DAT_805c5eec;
    } else if (param_1 == 2) {
        DAT_805c4e0c = *(int *)(iVar2 + 0xa0);
        DAT_805c4e1c = *(int *)(iVar2 + 0x10);
        DAT_805c4e20 = *(int *)(iVar2 + 0x18);
    } else if (param_1 == 3) {
        *(int *)(iVar2 + 0xa0) = DAT_805c4e0c;
        *(int *)(iVar2 + 0x10) = DAT_805c4e1c;
        *(int *)(iVar2 + 0x18) = DAT_805c4e20;
    }
    return 1;
}


// ---------------------------------------------------------------------------
// 0x801eafc0  ekun_safe_spawn_save_restore
// ---------------------------------------------------------------------------
void ekun_safe_spawn_save_restore(unsigned long long param_1, double param_2, double param_3,
                                  double param_4, double param_5, double param_6, double param_7,
                                  double param_8, int param_9)
{
    int *puVar1;
    int iVar2;
    int local_10, local_c, local_8;

    if (param_9 == 1) {
        if ((DAT_80612690 == 0) ||
            ((*(unsigned int *)(DAT_80612690 + 0x28) & 0x80000000) != 0) ||
            ((*(unsigned int *)(DAT_80612690 + 0x28) & 1) == 0)) {
            DAT_805c5edc = 0;
        } else {
            DAT_805c5edc = 1;
        }
    } else {
        if (DAT_805c5edc != 0) {
            local_10 = DAT_803df8d0;
            local_c = DAT_803df8d4;
            local_8 = DAT_803df8d8;
            puVar1 = spawn_def_get_entry(3);
            iVar2 = __we3_spawn_create_instance(param_1, param_2, param_3, param_4, param_5, param_6,
                                                param_7, param_8, 0, 8, 5, 0, &local_10, puVar1,
                                                0, 0, 0, 0);
            if (iVar2 != 0) {
                *(float *)(iVar2 + 0x70) = FLOAT_80617440;
                DAT_80612690 = iVar2;
            }
        }
        DAT_80612698 = DAT_805c5ee4;
    }
    DAT_805c5ee4 = DAT_80612698;
}


// ---------------------------------------------------------------------------
// 0x801eb0cc  ekun_trcp_state_save
// ---------------------------------------------------------------------------
int ekun_trcp_state_save(int param_1)
{
    if (param_1 != 1) {
        return 0;
    }
    int iVar1 = FUN_8023c32c(0x54524350);  // 'TRCP'
    if (iVar1 != 0) {
        DAT_805c4e38 = *(int *)(iVar1 + 0x64);
        DAT_805c4e3c = *(int *)(iVar1 + 0x68);
        DAT_805c4e40 = *(int *)(iVar1 + 0x6c);
        DAT_805c4e2c = *(int *)(iVar1 + 0xc0);
        DAT_805c4e30 = *(int *)(iVar1 + 0x70);
        DAT_805c4e34 = *(int *)(iVar1 + 0xc8);
        DAT_805c4e44 = 1;
    }
    return 1;
}


// ===========================================================================
// batch_622 : 0x801eb154 - 0x801ebfd7
// EKUN actor-bank checkpoint save/restore dispatcher + level-init part B +
// ambient-loop volume driver. Continuation of the CDH1 "EKUN" set-piece.
//
// The set-piece keeps a 3-entry table of per-actor save/restore callbacks at
// PTR_ekun_rock_state_save_restore_80452574 (stride 0x10: {cb, savedFlag,
// pendingRestore, liveFlag}). The dispatcher (ekun_actor_bank_save_restore_dispatch)
// runs the whole checkpoint save/restore sequence for the bank; the per-frame
// tick functions poll each callback in mode 0 (restore-detect), 2 (save-detect)
// or 3 (apply-restore).
//
// Callback modes (shared with batch_621): 1 = save/snapshot, 0 = restore,
// 2 = save-detect, 3 = apply-restore, 4 = force-destroy-on-absent.
// ===========================================================================

extern "C" {
// --- batch_622 raw data symbols (file-local) ---
extern int   DAT_805c5ee0;      // EKUN checkpoint slot handle (alloc'd block)
extern int   DAT_80452574;      // base of the {cb,savedFlag,pendingRestore,liveFlag} table
                                // (Ghidra label PTR_ekun_rock_state_save_restore_80452574)
extern int   DAT_80612720;      // deferred one-shot flag (also in batch_621 set)
extern int   DAT_80612730;      // debris pool budget (players*3)
extern int   DAT_80612728; extern int DAT_8061272c;   // spark emitter scratch
extern int   DAT_80612734;      // spark emitter handle
extern float FLOAT_80612738; extern int uRam8061273c;  // ambient loop B pos + pad
extern float FLOAT_80612740; extern int uRam80612744;  // ambient loop A pos + pad
extern float FLOAT_80612748;    // overlay fade scalar
extern int   DAT_80612610;      // player count
extern int   DAT_80612614;      // current level id (0x22 == CDH1)
extern int   DAT_806109c0;      // per-player debris budget unit
extern int   DAT_80611d74;      // level state block base (for +0xdec ambient id)
extern int   DAT_80611d78;      // active actor/character block base
extern int   DAT_80453a60;      // actor bank 0 descriptor
extern int   DAT_80453afc;      // actor bank 1 descriptor
extern int   DAT_805c5f18;      // particle def id storage
extern int   DAT_805c5fe4;      // spark texture handle
extern int   DAT_805c601c;      // spark emitter def storage
extern float DAT_803df8e0; extern int DAT_803df8e4; extern int DAT_803df8e8; // spark spawn vec

// arc time-origin band edges + ambient-loop volume table
extern float FLOAT_80617440;    // 0.0f (shared)
extern float FLOAT_806174bc;    // arc time origin (default band)
extern float FLOAT_806176d0;    // 0.0f
extern float FLOAT_806176d4; extern float FLOAT_806176d8; extern float FLOAT_806176dc; // 1.0f
extern float FLOAT_806176e0; extern float FLOAT_806176e4; extern float FLOAT_806176e8;
extern float FLOAT_806176ec; extern float FLOAT_806176f0; extern float FLOAT_806176f4;
extern float FLOAT_806176f8; extern float FLOAT_806176fc;
extern float FLOAT_80617700; extern float FLOAT_80617704;
extern float FLOAT_80617708; extern float FLOAT_8061770c; extern float FLOAT_80617710;

// --- batch_622 raw code symbols (file-local; not in generated headers) ---
extern int   FUN_803826e8(void);               // FP spill frame open (this batch's variant)
extern void  FUN_80382734(void);               // FP spill frame close (this batch's variant)
extern int   FUN_8025b8e4(int mode);           // checkpoint slot size/id query
extern int   FUN_8024b4f8(int size);           // checkpoint block alloc (returns handle)
extern int   FUN_8024b3c0(int handle);         // checkpoint block restore (returns status)
} // extern "C"

// sound_clear_bank_ref, player_ai_clear_footstep_sounds, soundpad_find_zone_by_id,
// soundpad_find_pad_by_extref, scalar_step_toward_snap, cutscene_get_data,
// arc_state_reset, arc_set_time_origin, arc_nodes_update, and the
// particle_def_set_*/particle_emitter_set_color_range/_lifetime_range are used at
// their generated_decls signatures directly.
//
// The following helpers have generated_decls prototypes whose arity/param types
// disagree with these ROM call sites, so route them through file-local typed
// fn-ptr aliases (same __we3_* pattern as batch_621's level_actors_register).

// entity_process_despawns: ROM form is the FP context + 7 doubles.
static void (*const __we3_entity_process_despawns)(unsigned long long, double, double,
        double, double, double, double, double) =
    (void (*)(unsigned long long, double, double, double, double, double, double,
        double))&entity_process_despawns;

// entity_preload_resources: FP context + 7 doubles + entity type id, returns ctx.
static unsigned long long (*const __we3_entity_preload_resources)(unsigned long long,
        double, double, double, double, double, double, double, int) =
    (unsigned long long (*)(unsigned long long, double, double, double, double,
        double, double, double, int))&entity_preload_resources;

// entity_create_attachment: FP context + 7 doubles + attach entity handle.
static unsigned long long (*const __we3_entity_create_attachment)(unsigned long long,
        double, double, double, double, double, double, double, int) =
    (unsigned long long (*)(unsigned long long, double, double, double, double,
        double, double, double, int))&entity_create_attachment;

// particle_def_create: (2 doubles, def-id ptr, then 5 ints).
static void (*const __we3_particle_def_create)(double, double, void *, int, int, int,
        int, int) =
    (void (*)(double, double, void *, int, int, int, int, int))&particle_def_create;

// particle_overlay_create: 5 doubles + 2 ptrs + 3 ints.
static void (*const __we3_particle_overlay_create)(double, double, double, double,
        double, void *, void *, int, unsigned int, unsigned int) =
    (void (*)(double, double, double, double, double, void *, void *, int,
        unsigned int, unsigned int))&particle_overlay_create;

// particle_emitter_create: 3 doubles + 2 def ptrs + 2 ints.
static void (*const __we3_particle_emitter_create)(double, double, double, void *,
        void *, int, int) =
    (void (*)(double, double, double, void *, void *, int, int))&particle_emitter_create;

// particle_emitter_set_texture: texture handle by pointer.
static void (*const __we3_particle_emitter_set_texture)(void *) =
    (void (*)(void *))&particle_emitter_set_texture;

// particle_emitter_spawn: handle-out ptr, def ptr, int, 2 vec ptrs.
static void (*const __we3_particle_emitter_spawn)(void *, void *, int, void *, void *) =
    (void (*)(void *, void *, int, void *, void *))&particle_emitter_spawn;

// particle_cached_disable: takes the cached emitter handle by (int*) here.
static void (*const __we3_particle_cached_disable)(void *) =
    (void (*)(void *))&particle_cached_disable;

// sound_update_3d_source: arg4/arg5 are the state block address + pad passed as
// plain ints at these call sites.
static void (*const __we3_sound_update_3d_source)(double, double, double, int, int,
        int, int) =
    (void (*)(double, double, double, int, int, int, int))&sound_update_3d_source;


// ---------------------------------------------------------------------------
// 0x801eb154  ekun_checkpoint_slot_save_restore
// mode 1 = allocate a checkpoint block (sized via FUN_8025b8e4) and store the
// handle; any other mode = restore the previously-allocated block.
// ---------------------------------------------------------------------------
int ekun_checkpoint_slot_save_restore(int mode)
{
    int status;
    if (mode == 1) {
        int size = FUN_8025b8e4(0);
        DAT_805c5ee0 = FUN_8024b4f8(size);
        status = 1;
    } else {
        status = FUN_8024b3c0(DAT_805c5ee0);
    }
    return status;
}


// ---------------------------------------------------------------------------
// 0x801eb1ac  ekun_actor_bank_save_restore_dispatch
// The HUD-objective / checkpoint callback installed by ekun_level_init. The FP
// spill frame (FUN_803826e8) returns the save/restore mode in r4. On a full
// restore (mode 2) it walks the 3-entry callback table forcing a destroy of any
// actor that is flagged saved-but-now-absent; on a save (0 < mode < 2) it clears
// the saved flags and arms the deferred one-shot. Either way it then runs the
// whole ekun_*_save_restore chain for the resolved mode.
// NOTE: declared 'void FUN_801eb1ac(void)' earlier in this file; this is the
// real definition. The prior forward decl is a subset-compatible alias.
// ---------------------------------------------------------------------------
void ekun_actor_bank_save_restore_dispatch(unsigned long long param_1, double param_2, double param_3,
                                           double param_4, double param_5, double param_6,
                                           double param_7, double param_8)
{
    (void)param_1;
    int mode = FUN_803826e8();   // FP frame open; returns the save/restore mode
    int resolved = -1;
    if (mode == 2) {
        int off = 0;
        for (int i = 0; i < 3; ++i) {
            int live = (*(int (**)(int))((int)&DAT_80452574 + off))(1);
            if ((live == 0) && (*(int *)((int)&DAT_8045257c + off) != 0)) {
                (*(int (**)(int))((int)&DAT_80452574 + off))(4);
            }
            *(int *)((int)&DAT_80452580 + off) = 1;
            off += 0x10;
        }
        resolved = 1;
    } else if ((mode < 2) && (0 < mode)) {
        int off = 0;
        for (int n = 3; n != 0; --n) {
            if (*(int *)((int)&DAT_80452580 + off) != 0) {
                *(int *)((int)&DAT_80452578 + off) = 0;
            }
            off += 0x10;
        }
        resolved = 0;
        DAT_80612720 = 1;
    }
    if (resolved != -1) {
        ekun_lptr_artr_save_restore(resolved);
        // ekun_fbrl_cleanup reads the save/restore mode from the shared FP-frame
        // register (opaque, non-matching), so it takes no explicit argument here.
        ekun_fbrl_cleanup();
        ekun_safe_spawn_save_restore(param_1, param_2, param_3, param_4, param_5,
                                     param_6, param_7, param_8, resolved);
        ekun_checkpoint_slot_save_restore(resolved);
    }
    FUN_80382734();  // FP frame close
}


// ---------------------------------------------------------------------------
// 0x801eb2cc  ekun_actor_bank_restore_tick
// Per-frame: for each of the 3 actor-bank slots, if not yet flagged saved and
// its callback reports live (mode 0), latch saved=1/pending=0; then if a restore
// is pending, apply it (mode 3) and clear the pending flag.
// ---------------------------------------------------------------------------
void ekun_actor_bank_restore_tick(void)
{
    FUN_803826e8();
    int off = 0;
    for (int i = 0; i < 3; ++i) {
        if (*(int *)((int)&DAT_80452578 + off) == 0) {
            int live = (*(int (**)(int))((int)&DAT_80452574 + off))(0);
            if (live != 0) {
                *(int *)((int)&DAT_80452578 + off) = 1;
                *(int *)((int)&DAT_8045257c + off) = 0;
            }
        }
        if (*(int *)((int)&DAT_8045257c + off) != 0) {
            (*(int (**)(int))((int)&DAT_80452574 + off))(3);
            *(int *)((int)&DAT_8045257c + off) = 0;
        }
        off += 0x10;
    }
    FUN_80382734();
}


// ---------------------------------------------------------------------------
// 0x801eb37c  ekun_actor_bank_save_detect
// Per-frame: poll each of the 3 slot callbacks in save-detect mode (2); when a
// callback reports it needs saving, latch its pending-restore flag.
// ---------------------------------------------------------------------------
void ekun_actor_bank_save_detect(void)
{
    FUN_803826e8();
    int off = 0;
    for (int i = 0; i < 3; ++i) {
        int need = (*(int (**)(int))((int)&DAT_80452574 + off))(2);
        if (need != 0) {
            *(int *)((int)&DAT_8045257c + off) = 1;
        }
        off += 0x10;
    }
    FUN_80382734();
}


// ---------------------------------------------------------------------------
// 0x801eb3ec  ekun_process_despawns  (thin wrapper -> entity_process_despawns)
// ---------------------------------------------------------------------------
void ekun_process_despawns(unsigned long long param_1, double param_2, double param_3,
                           double param_4, double param_5, double param_6, double param_7,
                           double param_8)
{
    __we3_entity_process_despawns(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
}


// ---------------------------------------------------------------------------
// 0x801eb40c  ekun_arc_state_reset  (thin wrapper -> arc_state_reset)
// ---------------------------------------------------------------------------
void ekun_arc_state_reset(void)
{
    arc_state_reset();
}


// ---------------------------------------------------------------------------
// 0x801eb42c  ekun_arc_nodes_update
// On CDH1 (level 0x22), pick the arc time origin from the current cutscene frame
// (special band 0x9df..0xa95 uses FLOAT_80617440, otherwise FLOAT_806174bc),
// then advance the arc lightning nodes.
// ---------------------------------------------------------------------------
void ekun_arc_nodes_update(void)
{
    if (DAT_80612614 == 0x22) {
        int cs = cutscene_get_data();
        if (cs != 0) {
            int frame = *(int *)(cs + 0xc);
            if ((frame < 0x9df) || (0xa95 < frame)) {
                arc_set_time_origin((double)FLOAT_806174bc);
            } else {
                arc_set_time_origin((double)FLOAT_80617440);
            }
        }
    }
    arc_nodes_update();
}


// ---------------------------------------------------------------------------
// 0x801eb48c  ekun_preload_actor_resources
// Preload the three EKUN actor entity types (0x454, 0x47f, 0x480), then release
// sound banks 7 and 8 and clear the AI footstep sound slots.
// ---------------------------------------------------------------------------
void ekun_preload_actor_resources(unsigned long long param_1, double param_2, double param_3,
                                  double param_4, double param_5, double param_6, double param_7,
                                  double param_8)
{
    unsigned long long ctx;
    ctx = __we3_entity_preload_resources(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0x454);
    ctx = __we3_entity_preload_resources(ctx,     param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0x47f);
    __we3_entity_preload_resources(ctx,           param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0x480);
    sound_clear_bank_ref(7);
    sound_clear_bank_ref(8);
    player_ai_clear_footstep_sounds();
}


// ---------------------------------------------------------------------------
// 0x801eb4d4  ekun_level_init_b
// Second half of the CDH1 EKUN level init: seed the debris budget, register the
// two level actor banks and their resource pool, attach the primary (and, in
// >=2 player modes, the secondary) character sub-object, OR the interior/audio
// flags (0x10000 / 0x20000) onto ~60 soundpad zones by id, then build the spark
// smoke-overlay particle bank and spawn it disabled.
//
// The long soundpad-zone loop is fully unrolled in the ROM (one
// soundpad_find_zone_by_id + OR per zone id); kept literal here for fidelity.
// ---------------------------------------------------------------------------
static inline void ekun_flag_zone(int zoneId, unsigned int bits)
{
    int idx = soundpad_find_zone_by_id(zoneId);
    int rec = DAT_80611b8c + idx * 0x20;
    *(unsigned int *)(rec + 4) |= bits;
}

void ekun_level_init_b(unsigned long long param_1, double param_2, double param_3,
                       double param_4, double param_5, double param_6, double param_7,
                       double param_8)
{
    int   local_18;
    float local_14;
    int   local_10;
    int   local_c;

    DAT_80612730 = DAT_806109c0 * 3;
    local_10 = DAT_803df8e4;
    local_14 = DAT_803df8e0;
    local_c  = DAT_803df8e8;
    FLOAT_80612740 = FLOAT_806176d0;
    uRam80612744   = -1;
    FLOAT_80612738 = FLOAT_806176d0;
    uRam8061273c   = -1;

    unsigned long long ctx = __we3_level_actors_register(
        param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
        0, &DAT_80453a60, 3);
    ctx = __we3_level_actors_register(
        ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
        1, &DAT_80453afc, 3);
    __we3_level_resource_pool_setup(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);

    if (*(int *)(DAT_80611d78 + 0x14) != 0) {
        __we3_entity_create_attachment(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                       param_8, *(int *)(DAT_80611d78 + 0x14));
    }
    if ((1 < DAT_80612610) && (*(int *)(DAT_80611d78 + 0x1430) != 0)) {
        __we3_entity_create_attachment(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                       param_8, *(int *)(DAT_80611d78 + 0x1430));
    }

    // Interior zones (camera/visibility bit 0x10000)
    ekun_flag_zone(0x640, 0x10000);
    // Audio-reverb zones (bit 0x20000)
    ekun_flag_zone(0x654, 0x20000); ekun_flag_zone(0x65a, 0x20000);
    ekun_flag_zone(0x659, 0x20000); ekun_flag_zone(0x658, 0x20000);
    ekun_flag_zone(0x657, 0x20000); ekun_flag_zone(0x656, 0x20000);
    ekun_flag_zone(0x65b, 0x20000); ekun_flag_zone(0x65c, 0x20000);
    ekun_flag_zone(0x662, 0x20000); ekun_flag_zone(0x663, 0x20000);
    ekun_flag_zone(0x665, 0x20000); ekun_flag_zone(0x666, 0x20000);
    ekun_flag_zone(0x668, 0x20000); ekun_flag_zone(0x66a, 0x20000);
    ekun_flag_zone(0x66b, 0x20000); ekun_flag_zone(0x66d, 0x20000);
    ekun_flag_zone(0x66e, 0x20000); ekun_flag_zone(0x66f, 0x20000);
    ekun_flag_zone(0x670, 0x20000); ekun_flag_zone(0x671, 0x20000);
    ekun_flag_zone(0x682, 0x20000); ekun_flag_zone(0x684, 0x20000);
    ekun_flag_zone(0x688, 0x20000); ekun_flag_zone(0x689, 0x20000);
    ekun_flag_zone(0x68d, 0x20000); ekun_flag_zone(0x6bb, 0x20000);
    ekun_flag_zone(0x6c6, 0x20000); ekun_flag_zone(0x6bd, 0x20000);
    ekun_flag_zone(0x6c5, 0x20000); ekun_flag_zone(0x6be, 0x20000);
    ekun_flag_zone(0x6c4, 0x20000); ekun_flag_zone(0x6bf, 0x20000);
    ekun_flag_zone(0x6c3, 0x20000); ekun_flag_zone(0x6c0, 0x20000);
    ekun_flag_zone(0x6c2, 0x20000); ekun_flag_zone(0x6c1, 0x20000);
    ekun_flag_zone(0x6c7, 0x20000); ekun_flag_zone(0x6d1, 0x20000);
    ekun_flag_zone(0x6c8, 0x20000); ekun_flag_zone(0x6d0, 0x20000);
    ekun_flag_zone(0x6c9, 0x20000); ekun_flag_zone(0x6cf, 0x20000);
    ekun_flag_zone(0x6ca, 0x20000); ekun_flag_zone(0x6ce, 0x20000);
    ekun_flag_zone(0x6cb, 0x20000); ekun_flag_zone(0x6cc, 0x20000);
    ekun_flag_zone(0x6cd, 0x20000);
    ekun_flag_zone(0x5dd, 0x10000); ekun_flag_zone(0x6e6, 0x10000);
    ekun_flag_zone(0x627, 0x10000);
    ekun_flag_zone(0x6e3, 0x20000); ekun_flag_zone(0x6e9, 0x20000);
    ekun_flag_zone(0x700, 0x20000); ekun_flag_zone(0x707, 0x20000);
    ekun_flag_zone(0x706, 0x10000);
    ekun_flag_zone(0x5f3, 0x20000); ekun_flag_zone(0x73a, 0x20000);
    ekun_flag_zone(0x738, 0x10000); ekun_flag_zone(0x73e, 0x10000);
    ekun_flag_zone(0x736, 0x20000);
    ekun_flag_zone(0x737, 0x20000);

    // spark smoke-overlay particle bank
    FLOAT_80612748 = FLOAT_806176d4;
    DAT_80612728 = 0;
    DAT_8061272c = 0;
    __we3_particle_def_create((double)FLOAT_806176d8, (double)FLOAT_806176dc, &DAT_805c5f18, 0, 1, 1, 0x344, 0);
    particle_def_set_size_keyframes((double)FLOAT_806176dc, (double)FLOAT_806176d0, (double)FLOAT_806176d0,
                                    (double)FLOAT_806176e0, 0x7f7f7f00, 0x7f404000, 0);
    particle_def_set_velocity_ranges((double)FLOAT_806176e4, (double)FLOAT_806176d0, (double)FLOAT_806176e4,
                                     (double)FLOAT_806176d0, (double)FLOAT_806176dc, (double)FLOAT_806176e8);
    particle_def_set_size_start_end((double)FLOAT_806176d0, (double)FLOAT_806176d0, (double)FLOAT_806176d0,
                                    (double)FLOAT_806176ec);
    particle_def_set_color_end((double)FLOAT_806176d0, (double)FLOAT_806176d0, (double)FLOAT_806176f0,
                               (double)FLOAT_806176d0);
    __we3_particle_overlay_create((double)FLOAT_806176e0, (double)FLOAT_806176f4, (double)FLOAT_806176f8,
                                  (double)FLOAT_806176fc, (double)FLOAT_806176fc, &DAT_805c5fe4, &local_14,
                                  0x290, 0xff808080, 0xffa03c00);
    particle_overlay_set_fade_params((double)FLOAT_806176e0, 0, 0x7f64641e);
    __we3_particle_emitter_create((double)FLOAT_806176dc, (double)FLOAT_80617700, (double)FLOAT_806176fc,
                                  &DAT_805c601c, &DAT_805c5f18, 0, 0);
    particle_emitter_set_color_range((double)FLOAT_806176e0, (double)FLOAT_806176d0,
                                     (double)FLOAT_80617704, (double)FLOAT_806176d0);
    particle_emitter_set_lifetime_range((double)FLOAT_806176e0, (double)FLOAT_806176d0);
    __we3_particle_emitter_set_texture(&DAT_805c5fe4);
    __we3_particle_emitter_spawn(&DAT_80612734, &DAT_805c601c, 0, &local_14, &local_14);
    local_18 = DAT_80612734;
    __we3_particle_cached_disable(&local_18);

    (void)local_10; (void)local_c;
}


// ---------------------------------------------------------------------------
// 0x801ebee0  ekun_ambient_loop_volume_update
// Select the two ambient-loop target volumes from the current level-state id
// (DAT_80611d74+0xdec): most level phases use FLOAT_806176d0 (silent) for loop A
// and FLOAT_806176dc (full) for loop B, phase 1 gates loop B on the 0x3ec sound
// pad's active bit, and several phases (0x11..0x15, 0x1a) swap in the alternate
// levels. Then step both toward their targets and drive the two 3D ambient
// sources on sound banks 7 and 8.
// ---------------------------------------------------------------------------
void ekun_ambient_loop_volume_update(void)
{
    double volA = (double)FLOAT_806176d0;   // loop A target (default silent)
    double volB = volA;                     // loop B target
    int phase = *(int *)(DAT_80611d74 + 0xdec);

    // Structured to mirror the ROM's nested compare/goto tree exactly.
    // 'done' == reach the scalar-step tail with volA still silent;
    // fall-through of the tree instead sets volA = alt value first.
    bool useAlt = false;
    if (phase == 0x17) {
        // both loops silent; go straight to step
    } else if (phase < 0x17) {
        if (phase < 0x11) {
            if (phase == 1) {
                int pad = soundpad_find_pad_by_extref(0x3ec);
                if ((pad == 0) || ((*(unsigned int *)(pad + 0x60) & 1) == 0)) {
                    volB = (double)FLOAT_806176dc;
                }
            } else if (0 < phase) {
                volB = (double)FLOAT_806176dc;   // 0x02..0x10
            } else {
                useAlt = true;                   // phase <= 0
            }
        } else if ((phase < 0x16) && (0x13 < phase)) {
            volB = (double)FLOAT_806176dc;       // 0x14, 0x15
        } else {
            useAlt = true;                       // 0x11, 0x12, 0x13, 0x16
        }
    } else if (phase < 0x25) {
        if (phase == 0x1a) {
            volB = (double)FLOAT_806176dc;
        } else {
            useAlt = true;                       // 0x18..0x24 except 0x1a
        }
    } else if (phase < 0x29) {
        // 0x25..0x28 : both silent
    } else {
        useAlt = true;                           // phase >= 0x29
    }

    if (useAlt) {
        volA = (double)FLOAT_80617708;
    }

    scalar_step_toward_snap(volB, (double)FLOAT_8061770c, &FLOAT_80612740);
    scalar_step_toward_snap(volA, (double)FLOAT_806176e4, &FLOAT_80612738);
    __we3_sound_update_3d_source((double)FLOAT_80612740, (double)FLOAT_80617710, (double)FLOAT_806176dc,
                                 (int)(long long)&uRam80612744, 0, 7, 0);
    __we3_sound_update_3d_source((double)FLOAT_80612738, (double)FLOAT_80617710, (double)FLOAT_806176dc,
                                 (int)(long long)&uRam8061273c, 0, 8, 0);
}


// ===========================================================================
// batch_623 : 0x801ec01c - 0x801ecf87
// EKUN worm-boss per-frame master update (part B), the "HTIP" lightning-arc
// renderer + its frame-band classifier and spark emitter, and the turbine /
// motor spin sound+pose drivers. Continuation of the CDH1 EKUN set-piece.
//
// HTIP is the boss's electric-arc attack: ekun_htip_frame_band classifies the
// active clip (0x656) frame into one of 4 target-point bands; the renderer
// (ekun_htip_lightning_render) draws two jittered lightning ribbons from the
// boss bone to the banded target via ekun_lightning_ribbon_render, and the
// spark emitter (ekun_htip_spark_update) follows the same point.
// ===========================================================================

extern "C" {
// --- batch_623 raw data symbols (file-local) ---
extern float DAT_803df8ec; extern float DAT_803df8f0; extern float DAT_803df8f4; // ribbon basis dir
extern float DAT_803df8f8; extern float DAT_803df8fc; extern float DAT_803df900; // HTIP src pt A
extern float DAT_803df904; extern float DAT_803df908; extern float DAT_803df90c; // HTIP src pt B
extern float DAT_803df910; extern float DAT_803df914; extern float DAT_803df918; // spark src pt
extern float DAT_803df91c; extern float DAT_803df920; extern float DAT_803df924; // TURN pivot pt
extern float DAT_80453b44;      // base of the 12-byte band target-point table
extern int   DAT_80611928;      // fan spin phase accumulator
extern float FLOAT_80610df8;    // HTIP arc length cache
extern float FLOAT_806108ec;    // frame-time delta unit

extern float DAT_80583228;      // sine LUT base (0x800 entries, quarter-wrapped)

// float/double constants
extern float  FLOAT_806176d0; extern float FLOAT_806176dc; extern float FLOAT_806176e0;
extern float  FLOAT_806176e4; extern float FLOAT_806176e8; extern float FLOAT_806176ec;
extern float  FLOAT_80617440; extern float FLOAT_80617700; extern float FLOAT_80617714;
extern float  FLOAT_80617718; extern float FLOAT_8061771c; extern float FLOAT_80617720;
extern float  FLOAT_80617724; extern float FLOAT_80617730; extern float FLOAT_80617734;
extern float  FLOAT_80617738; extern float FLOAT_8061773c; extern float FLOAT_80617740;
extern float  FLOAT_80617744; extern float FLOAT_80617748; extern float FLOAT_8061774c;
extern float  FLOAT_80617750; extern float FLOAT_80617754;
extern double DOUBLE_80617728;
extern int    DAT_806109c0;     // per-player debris budget unit (also b622)

// --- batch_623 raw code symbols (file-local; not in generated headers) ---
extern unsigned long long FUN_803826d8(void); // FP frame open: packs {ptrB,ptrA} in the return regs
extern void  FUN_80382724(void);               // FP frame close (ribbon variant)
extern double FUN_80390324(double x);          // round/truncate toward zero (fmod helper)
extern void  FUN_801ef588(void);               // EKUN sub-update hook (later batch)

// ---- GX FIFO write port (write-only volatile at 0xCC008000) ----
extern volatile float        DAT_cc008000;     // FIFO float write port
extern volatile unsigned int DAT_cc008000_u;   // FIFO u32 write port (same address)
} // extern "C"
#ifndef GXF
#define GXF(v) (DAT_cc008000   = (float)(v))
#define GXU(v) (DAT_cc008000_u = (unsigned int)(v))
#endif

// anim_is_clip_active/anim_primary_clip_userflags/anim_active_clip_slot/
// anim_get_frame_times/object_sound_slot_accumulate/mtx4_*/sprite_get_texture_id/
// gx_begin_textured_batch/gx_end_batch/gx_set_sub_material_id/gx_set_blend_mode/
// math_sqrt/scalar_step_toward_snap/entity_register_for_update/
// aiactor_find_player_by_team resolve through generated_decls.h.
//
// The following have generated_decls prototypes that disagree with these ROM
// call sites (reduced arity / different pointer types), routed via __we3_*:

// anim_primary_clip_userflags resolves through generated_externs.h (variadic).

// ekun_htip_spark_update is defined later in this file (batch_623); forward-
// declare it so ekun_master_update_b can call it.
void ekun_htip_spark_update(int actor);

// anim_event_scan_active: ROM form reads the shared FP frame -> no explicit args.
static int (*const __we3_anim_event_scan_active)(void) =
    (int (*)(void))&anim_event_scan_active;

// mtx4_rotate_about_point: ROM form is (matrix, pivotVec, worldPointVec).
static void (*const __we3_mtx4_rotate_about_point)(float *, float *, float *) =
    (void (*)(float *, float *, float *))&mtx4_rotate_about_point;

// particle_cached_set_position: cached handle (int*) + point (float*) here.
static void (*const __we3_particle_cached_set_position)(void *, void *) =
    (void (*)(void *, void *))&particle_cached_set_position;

// particle_cached_set_drag: (drag, cached handle int*) here.
static void (*const __we3_particle_cached_set_drag)(double, void *) =
    (void (*)(double, void *))&particle_cached_set_drag;

// sound_update_3d_source (batch_623 call sites pass handle/pos blocks as ints;
// the ROM reads its return value as the "source started" flag).
static int (*const __we3b_sound_update_3d_source)(double, double, double, int, int,
        int, int) =
    (int (*)(double, double, double, int, int, int, int))&sound_update_3d_source;


// ---------------------------------------------------------------------------
// 0x801ec01c  ekun_master_update_b
// Second per-frame EKUN driver: resolve the HTIP boss and the DP07..DP10
// sub-actors by team, run the ambient loop + sub-update hooks, then for each
// actor detect the electric-attack anim events (clip ids 0x644/0x645 for HTIP;
// 0x693..0x696 for the DPnn variants) and, when the event fires, arm the target
// object's damage word (+0x94=2), clear its +0x54c flag and accumulate a 1000ms
// sound slot. Finally, on level phases 0x15/0x1a, fire the scripted payload
// once clip 0x667 passes its threshold.
// ---------------------------------------------------------------------------
static inline void ekun_arm_attack_object(int actor)
{
    int obj = *(int *)(actor + 0x7c);
    *(int *)(obj + 0x94) = 2;
    *(unsigned char *)(obj + 0x54c) = 0;
    object_sound_slot_accumulate(obj, 1, 1000);
}

void ekun_master_update_b(unsigned long long param_1, double param_2, double param_3,
                          double param_4, double param_5, double param_6, double param_7,
                          double param_8)
{
    (void)param_1;
    FUN_803826e8();
    int htip = aiactor_find_player_by_team(0x48544950);  // 'HTIP'
    int dp07 = aiactor_find_player_by_team(0x44503037);  // 'DP07'
    int dp08 = aiactor_find_player_by_team(0x44503038);  // 'DP08'
    int dp09 = aiactor_find_player_by_team(0x44503039);  // 'DP09'
    int dp10 = aiactor_find_player_by_team(0x44503130);  // 'DP10'
    ekun_ambient_loop_volume_update();
    FUN_801ef588();

    if (htip != 0) {
        ekun_htip_spark_update(htip);
        if (anim_is_clip_active(htip, 0x644) == 0) {
            if (anim_is_clip_active(htip, 0x645) != 0) {
                if ((__we3_anim_event_scan_active() != 0) ||
                    (anim_primary_clip_userflags(htip, 0x645) != 0)) {
                    int obj = *(int *)(htip + 0x7c);
                    *(int *)(obj + 0x94) = 2;
                    *(unsigned char *)(obj + 0x54c) = 0;
                    object_sound_slot_accumulate(obj, 1, 1000);
                }
            }
        } else {
            if ((__we3_anim_event_scan_active() != 0) ||
                (anim_primary_clip_userflags(htip, 0x644) != 0)) {
                int obj = *(int *)(htip + 0x7c);
                *(unsigned char *)(obj + 0x54e) = 0;
                *(int *)(obj + 0x94) = 0;
            }
        }
    }

    if ((dp07 != 0) && (__we3_anim_event_scan_active() != 0)) {
        int *slot = (int *)anim_active_clip_slot(dp07);
        if (*slot == 0x693) ekun_arm_attack_object(dp07);
        if (*slot == 0x695) ekun_arm_attack_object(dp07);
    }
    if ((dp08 != 0) && (__we3_anim_event_scan_active() != 0)) {
        int *slot = (int *)anim_active_clip_slot(dp08);
        if (*slot == 0x693) ekun_arm_attack_object(dp08);
        if (*slot == 0x695) ekun_arm_attack_object(dp08);
        if (*slot == 0x696) ekun_arm_attack_object(dp08);
    }
    if ((dp10 != 0) && (__we3_anim_event_scan_active() != 0)) {
        int *slot = (int *)anim_active_clip_slot(dp10);
        if (*slot == 0x693) ekun_arm_attack_object(dp10);
    }
    if ((dp09 != 0) && (__we3_anim_event_scan_active() != 0)) {
        int *slot = (int *)anim_active_clip_slot(dp09);
        if (*slot == 0x693) ekun_arm_attack_object(dp09);
        if (*slot == 0x694) ekun_arm_attack_object(dp09);
    }

    if ((DAT_80611d74 != 0) &&
        ((*(int *)(DAT_80611d74 + 0xdec) == 0x15) || (*(int *)(DAT_80611d74 + 0xdec) == 0x1a))) {
        int *slot = (int *)anim_active_clip_slot(*(int *)(DAT_80611d74 + 0x14));
        if ((*slot == 0x667) && (FLOAT_80617700 < (float)slot[2])) {
            FUN_80221074((double)FLOAT_806176d0, param_2, param_3, param_4, param_5,
                         param_6, param_7, param_8);
        }
    }
    FUN_80382734();
}


// ---------------------------------------------------------------------------
// 0x801ec3a8  ekun_lightning_ribbon_render
// Draws a jittered lightning ribbon between two world points (packed into the
// FP-frame return: ptrA = target, ptrB = source). Computes a perpendicular
// offset basis from the segment direction and DAT_803df8ec.. dir, then streams
// 3 quad strips into the GX FIFO, jittering each segment along the sine LUT.
//   pA/pB : float[3] endpoints (pB=ptrA lo word, pA=ptrB hi word of the pack)
//   colorHi : 0xAARRGGBB start color; the far row uses (color & 0xffffff00).
//   matIndex : GX material/texture index.
// ---------------------------------------------------------------------------
void ekun_lightning_ribbon_render(void *ptA_param, void *ptB_param, unsigned int color, int matIndex)
{
    (void)ptA_param; (void)ptB_param;
    unsigned long long pack = FUN_803826d8();
    float *pA = (float *)(unsigned long long)(unsigned int)(pack >> 0x20);  // target endpoint
    float *pB = (float *)(unsigned long long)(unsigned int)pack;            // source endpoint

    double amp = 0.0;                 // ribbon width amplitude (from FP frame f1)
    double jitter = game_frand();

    double perpZ = (double)((pB[1] - pA[1]) * DAT_803df8f4 - (pB[2] - pA[2]) * DAT_803df8f0);
    double perpY = (double)((pB[0] - pA[0]) * DAT_803df8f0 - (pB[1] - pA[1]) * DAT_803df8ec);
    float  crossX = (pB[2] - pA[2]) * DAT_803df8ec - (pB[0] - pA[0]) * DAT_803df8f4;

    if (FLOAT_80617714 < ABS(crossX)) {
        float mag2 = (float)(perpY * perpY + (double)(float)(perpZ * perpZ + (double)FLOAT_806176d0));
        if (FLOAT_80617718 < mag2) {
            float inv = 1.0f / SQRT(mag2);
            inv = inv * -(FLOAT_806176e8 * mag2 * inv * inv - FLOAT_8061771c);
            double scale = (double)(FLOAT_806176dc * inv *
                           -(FLOAT_806176e8 * mag2 * inv * inv - FLOAT_8061771c));
            perpZ = (double)(float)(perpZ * scale);
            perpY = (double)(float)(perpY * scale);
        }
    }

    double kA = (double)FLOAT_80617720;
    double vFar = (double)(float)((double)FLOAT_806176dc + jitter);
    double kB = (double)FLOAT_80617724;
    double kStep = (double)FLOAT_80617700;
    double half = (double)FLOAT_806176e8;
    double phaseBase = DOUBLE_80617728;

    for (int i = 0; i < 3; ++i) {
        unsigned int idx = (unsigned int)(kA * (double)(float)(kB * (double)(float)(
                (double)(float)((double)CONCAT44(0x43300000, (unsigned int)i ^ 0x80000000) - phaseBase)
                / kStep)) + half);
        double oz = (double)(float)(amp * (double)*(float *)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc)));
        double oy = (double)(float)(amp * (double)(float)(perpZ * (double)(float)(&DAT_80583228)[idx & 0x7ff]));
        double ox = (double)(float)(amp * (double)(float)(perpY * (double)(float)(&DAT_80583228)[idx & 0x7ff]));

        gx_begin_textured_batch(2, 0x92, 0, 4, matIndex);
        // near row (at pA)
        GXF((double)pA[0] + oy);
        GXF((double)pA[1] + oz);
        GXF((double)pA[2] + ox);
        GXU(color);
        GXF((float)jitter);
        GXF(FLOAT_806176d0);
        // far row (at pB)
        GXF(pB[0] + (float)(half * oy));
        GXF(pB[1] + (float)(half * oz));
        GXF(pB[2] + (float)(half * ox));
        GXU(color & 0xffffff00);
        GXF((float)vFar);
        GXF(FLOAT_806176d0);
        // near row (opposite)
        GXF((double)pA[0] - oy);
        GXF((double)pA[1] - oz);
        GXF((double)pA[2] - ox);
        GXU(color);
        GXF((float)jitter);
        GXF(FLOAT_806176dc);
        // far row (opposite)
        GXF(pB[0] - (float)(half * oy));
        GXF(pB[1] - (float)(half * oz));
        GXF(pB[2] - (float)(half * ox));
        GXU(color & 0xffffff00);
        GXF((float)vFar);
        GXF(FLOAT_806176dc);
        gx_end_batch();
    }
    FUN_80382724();
}


// ---------------------------------------------------------------------------
// 0x801ec714  ekun_htip_frame_band
// If the actor's active clip is the HTIP arc clip (0x656), classify its current
// frame into one of four target-point bands (returned via *outBand = 0..3);
// returns 1 when a band matched, else 0. Bands: [0x2a,0x6e], [0x7d,0xa5],
// [0xaf,0xdc], [0xf0,300].
// ---------------------------------------------------------------------------
int ekun_htip_frame_band(int actor, int *outBand)
{
    int *slot = (int *)anim_active_clip_slot(actor);
    if (*slot != 0x656) {
        return 0;
    }
    if (outBand == 0) {
        return 0;
    }
    int times[5];
    anim_get_frame_times((int)slot, times, 0);
    int frame = times[0];
    if ((0x2a <= frame) && (frame <= 0x6e)) { *outBand = 0; return 1; }
    if ((0x7d <= frame) && (frame <= 0xa5)) { *outBand = 1; return 1; }
    if ((0xaf <= frame) && (frame <= 0xdc)) { *outBand = 2; return 1; }
    if ((0xf0 <= frame) && (frame <= 300))  { *outBand = 3; return 1; }
    return 0;
}


// ---------------------------------------------------------------------------
// 0x801ec7f8  ekun_htip_lightning_render
// When the HTIP boss is arcing (ekun_htip_frame_band matches), build its bone
// world matrix, transform the two source anchor points, then draw two lightning
// ribbons from those anchors toward the banded target point, interpolated by the
// cached arc length FLOAT_80610df8.
// ---------------------------------------------------------------------------
void ekun_htip_lightning_render(void)
{
    float srcA[3];
    float srcB[3];
    float dstA[3];   // local_a4/a0/9c reused per ribbon
    int   band;
    float mtx[19];

    srcA[0] = DAT_803df8f8; srcA[1] = DAT_803df8fc; srcA[2] = DAT_803df900;
    srcB[0] = DAT_803df904; srcB[1] = DAT_803df908; srcB[2] = DAT_803df90c;

    int htip = aiactor_find_player_by_team(0x48544950);  // 'HTIP'
    if ((htip != 0) && (ekun_htip_frame_band(htip, &band) != 0)) {
        mtx4_set_translate_rotz((double)*(float *)(htip + 100), (double)*(float *)(htip + 0x68),
                                (double)*(float *)(htip + 0x6c),
                                (double)((FLOAT_80617724 * *(float *)(htip + 0x70)) / FLOAT_80617730),
                                mtx);
        mtx4_multiply((float *)(*(int *)(*(int *)(htip + 4) + 0x10) +
                                *(int *)(*(int *)(*(int *)(htip + 4) + 0x84) + 0x54) * 0x40),
                      mtx, mtx);
        mtx4_transform_point(mtx, srcA);
        mtx4_transform_point(mtx, srcB);

        int off = band * 0xc;
        double tx = (double)*(float *)(&DAT_80453b44 + off);
        double ty = (double)*(float *)((&DAT_80453b44) + off + 4);
        double tz = (double)*(float *)((&DAT_80453b44) + off + 8);
        gx_set_sub_material_id(2);
        gx_set_blend_mode(2);

        double t = (double)(FLOAT_806176ec / FLOAT_80610df8);
        dstA[0] = (float)((double)(float)(tx - (double)srcA[0]) * t + (double)srcA[0]);
        dstA[1] = (float)((double)(float)(ty - (double)srcA[1]) * t + (double)srcA[1]);
        dstA[2] = (float)((double)(float)(tz - (double)srcA[2]) * t + (double)srcA[2]);
        int tex = sprite_get_texture_id(0x33b);
        ekun_lightning_ribbon_render(srcA, dstA, 0x7f1f1f7f, tex);

        t = (double)(FLOAT_806176ec / FLOAT_80610df8);
        dstA[0] = (float)((double)(float)(tx - (double)srcB[0]) * t + (double)srcB[0]);
        dstA[1] = (float)((double)(float)(ty - (double)srcB[1]) * t + (double)srcB[1]);
        dstA[2] = (float)((double)(float)(tz - (double)srcB[2]) * t + (double)srcB[2]);
        tex = sprite_get_texture_id(0x33b);
        ekun_lightning_ribbon_render(srcB, dstA, 0x7f1f1f7f, tex);
    }
}


// ---------------------------------------------------------------------------
// 0x801eca1c  ekun_htip_spark_update
// Follows the HTIP arc: when not arcing, clear the spark emitter's active bit
// and zero its drag; when arcing, build the boss bone matrix, transform the
// spark source point, cache the arc length (FLOAT_80610df8) to the banded target
// point, then move the emitter there, restore drag and set its active bit.
// ---------------------------------------------------------------------------
void ekun_htip_spark_update(int actor)
{
    float srcPt[3];
    int   band;
    float mtx[19];
    int   cachedA, cachedB, cachedC;

    srcPt[0] = DAT_803df910; srcPt[1] = DAT_803df914; srcPt[2] = DAT_803df918;

    if (ekun_htip_frame_band(actor, &band) == 0) {
        *(unsigned int *)(DAT_80612734 + 4) &= 0xffbfffff;
        cachedA = DAT_80612734;
        __we3_particle_cached_set_drag((double)FLOAT_806176d0, &cachedA);
    } else {
        mtx4_set_translate_rotz((double)*(float *)(actor + 100), (double)*(float *)(actor + 0x68),
                                (double)*(float *)(actor + 0x6c),
                                (double)((FLOAT_80617724 * *(float *)(actor + 0x70)) / FLOAT_80617730),
                                mtx);
        mtx4_multiply((float *)(*(int *)(*(int *)(actor + 4) + 0x10) +
                                *(int *)(*(int *)(*(int *)(actor + 4) + 0x84) + 0x54) * 0x40),
                      mtx, mtx);
        mtx4_transform_point(mtx, srcPt);

        int off = band * 0xc;
        float tgt[3];
        tgt[0] = *(float *)(&DAT_80453b44 + off);
        tgt[1] = *(float *)((&DAT_80453b44) + off + 4);
        tgt[2] = *(float *)((&DAT_80453b44) + off + 8);
        double len = math_sqrt((double)((srcPt[2] - tgt[2]) * (srcPt[2] - tgt[2]) +
                                        (srcPt[0] - tgt[0]) * (srcPt[0] - tgt[0]) +
                                        (srcPt[1] - tgt[1]) * (srcPt[1] - tgt[1])));
        FLOAT_80610df8 = (float)len;
        if ((float)len < FLOAT_806176d0) {
            FLOAT_80610df8 = FLOAT_806176d0;
        }
        FLOAT_80610df8 = FLOAT_80610df8 + FLOAT_806176e0;
        cachedB = DAT_80612734;
        __we3_particle_cached_set_position(&cachedB, tgt);
        cachedC = DAT_80612734;
        __we3_particle_cached_set_drag((double)FLOAT_806176dc, &cachedC);
        *(unsigned int *)(DAT_80612734 + 4) |= 0x400000;
    }
}


// ---------------------------------------------------------------------------
// 0x801ecbb4  ekun_motor_rotate_sound_tick
// Ramp an entity's motor speed (+0xd0) toward its scaled target, integrate the
// rotor angle (+0x70) wrapped through FUN_80390324, and drive its 3D motor loop
// (sound id 0x60b) at a pitch proportional to the current speed.
// ---------------------------------------------------------------------------
void ekun_motor_rotate_sound_tick(int entity)
{
    double frac = (double)(FLOAT_806108ec *
                  (FLOAT_806176dc /
                   (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728)));
    scalar_step_toward_snap((double)FLOAT_80617734, (double)(float)((double)FLOAT_80617700 * frac),
                            (float *)(entity + 0xd0));
    double ang = (double)(float)((double)*(float *)(entity + 0xd0) * frac +
                                 (double)*(float *)(entity + 0x70));
    double wrapped = FUN_80390324((double)((float)(ang - (double)FLOAT_806176d0) / FLOAT_80617738));
    float pitchDiv = FLOAT_80617734;
    *(float *)(entity + 0x70) = -(float)((double)FLOAT_80617738 * (double)(float)wrapped - ang);
    __we3b_sound_update_3d_source((double)(*(float *)(entity + 0xd0) / pitchDiv),
                                  (double)FLOAT_8061773c, (double)FLOAT_806176dc,
                                  entity + 0x22c, entity + 100, 0x60b, 0);
}


// ---------------------------------------------------------------------------
// 0x801ecc88  ekun_turbine_spin_tick
// Ramp a turbine's spin (+0x1a4) toward the target at the given rate, drive its
// 3D loop (sound id 0x614), flag the entity when the source started, and update
// the two fake-rotation bone scales (+0xe0/+0xe4) from the sine LUT scaled by
// spin and the +0x1c0/+0x1c8 amplitudes.
// ---------------------------------------------------------------------------
int ekun_turbine_spin_tick(double rate, int entity)
{
    float pos[3];
    unsigned int negBudget = (unsigned int)DAT_806109c0 ^ 0x80000000;
    int started = scalar_step_toward_snap(rate,
                    (double)(FLOAT_80617740 * FLOAT_806108ec *
                             (FLOAT_806176dc /
                              (float)((double)CONCAT44(0x43300000, negBudget) - DOUBLE_80617728))),
                    (float *)(entity + 0x1a4));

    pos[0] = *(float *)(entity + 0x1a8) + *(float *)(entity + 100);
    pos[1] = (*(float *)(entity + 0x1ac) + *(float *)(entity + 0x68)) - FLOAT_80617744;
    pos[2] = *(float *)(entity + 0x1b0) + *(float *)(entity + 0x6c);
    // sound id 0x614; +0x22c = handle block, pos = the local position vector.
    int src = __we3b_sound_update_3d_source(
                    (double)(FLOAT_80617748 * (*(float *)(entity + 0x1a4) / FLOAT_806176e4)),
                    (double)FLOAT_8061774c, (double)FLOAT_806176dc,
                    entity + 0x22c, (int)(long long)pos, 0x614, 0);
    if (src != 0) {
        *(unsigned int *)(entity + 0x18) |= 0x40;
    }

    float ampX = FLOAT_806176ec * *(float *)(entity + 0x1c0);
    float clampX = FLOAT_806176dc;
    if (FLOAT_806176dc < ampX) clampX = ampX;
    *(float *)(entity + 0xe0) =
        (*(float *)(entity + 0x1a4) *
         (float)(&DAT_80583228)[(int)(FLOAT_80617720 * FLOAT_80617750 *
                (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_80611928 ^ 0x80000000) - DOUBLE_80617728)
                + FLOAT_806176e8) & 0x7ff]) / clampX;

    float ampY = FLOAT_806176ec * *(float *)(entity + 0x1c8);
    float clampY = FLOAT_806176dc;
    if (FLOAT_806176dc < ampY) clampY = ampY;
    *(float *)(entity + 0xe4) =
        (*(float *)(entity + 0x1a4) *
         *(float *)((int)&DAT_80583228 +
             (((int)(FLOAT_80617720 * FLOAT_80617754 *
                     (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_80611928 ^ 0x80000000) - DOUBLE_80617728)
                     + FLOAT_806176e8) + 0x200) * 4 & 0x1ffcU))) / clampY;
    return started;
}


// ---------------------------------------------------------------------------
// 0x801ece84  ekun_turbine_spin_fast  (spin up at full rate)
// ---------------------------------------------------------------------------
void ekun_turbine_spin_fast(int entity)
{
    ekun_turbine_spin_tick((double)FLOAT_806176e4, entity);
}


// ---------------------------------------------------------------------------
// 0x801ecea8  ekun_turbine_spin_stop  (spin down to zero; deregister when the
// loop handle has gone negative/inactive)
// ---------------------------------------------------------------------------
void ekun_turbine_spin_stop(int entity)
{
    int started = ekun_turbine_spin_tick((double)FLOAT_806176d0, entity);
    if ((started != 0) && (*(int *)(entity + 0x22c) < 0)) {
        entity_register_for_update(entity, 3, 0);
    }
}


// ---------------------------------------------------------------------------
// 0x801ecefc  ekun_turn_rig_update
// The 'TURN' turntable rig: build a ZYX rotation from the fake-rotation bone
// scales (+0xe0/+0xe4), rotate the direction vector (+0x1c0/+0x1c8) to derive a
// vertical bob (+0xe8), write it into the bone matrix, and if the 'TURN' entity
// exists, rotate the rig about its pivot point and fold it into the bone matrix.
// ---------------------------------------------------------------------------
void ekun_turn_rig_update(void)
{
    float pivot[3];
    float dir[3];
    float mtx[20];

    int self = FUN_803826f0();
    int *node = *(int **)(*(int *)(self + 4) + 4);
    int boneRec = node[*node * -0x30 + 0x1b];

    pivot[0] = DAT_803df91c; pivot[1] = DAT_803df920; pivot[2] = DAT_803df924;
    int turn = FUN_8023c32c(0x5455524e);  // 'TURN'

    mtx4_set_rotation_zyx((double)*(float *)(self + 0xe0), (double)FLOAT_806176d0,
                          (double)*(float *)(self + 0xe4), mtx);
    dir[0] = *(float *)(self + 0x1c0);
    dir[1] = FLOAT_806176d0;
    dir[2] = *(float *)(self + 0x1c8);   // local_54 = +0x1c8
    mtx4_transform_vector_alias(mtx, dir);
    *(float *)(self + 0xe8) = ABS(dir[1]);
    mtx4_write_translation((double)FLOAT_806176d0, (double)*(float *)(self + 0xe8),
                           (double)FLOAT_806176d0, (int)mtx);
    mtx4_copy_array(*(float **)(*(int *)(self + 4) + 0x10), mtx,
                    *(int *)(*(int *)(*(int *)(self + 4) + 4) + 4));
    if (turn != 0) {
        __we3_mtx4_rotate_about_point(mtx, pivot, (float *)(boneRec + 0x18));
        float *bone = (float *)(*(int *)(*(int *)(self + 4) + 0x10) + 0x40);
        mtx4_multiply(mtx, bone, bone);
    }
    *(unsigned int *)(self + 0x28) |= 0x200000;
    FUN_8038273c();
}


// ===========================================================================
// batch_624 : 0x801ed030 - 0x801edbd7
// EKUN worm-boss mechanical set-pieces: the DGBT/DGTR/DARM blast-door + fan rig,
// the SWWL/SWWT/DIAL/DMLT switch-driven dial/mover sequence, and the shared
// per-frame tick helpers (blade physics, flicker, vertical slide, rotor swing,
// spin-loop sound). Continuation of the CDH1 EKUN set-piece.
// ===========================================================================

extern "C" {
// --- batch_624 raw data symbols (file-local) ---
extern float FLOAT_80617758;    // DGTR blink re-arm time
extern float FLOAT_8061775c;    // DGTR initial timer
extern float FLOAT_80617760;    // fan blade spin lower bound
extern float FLOAT_80617764;    // DARM reset timer
extern float FLOAT_80617768;    // flicker random re-arm scale
extern float FLOAT_8061776c;    // vertical slide min step
extern float FLOAT_80617770; extern float FLOAT_80617774; // rotor swing coeffs
extern float FLOAT_80617778; extern float FLOAT_8061777c; // rotor swing gain / clamp
extern float FLOAT_80617780;    // spin loop pitch divisor / target
extern float FLOAT_80617784;    // spin loop ramp rate
extern float FLOAT_80617710;    // 3D source pitch const (shared w/ b623)

// --- batch_624 raw code symbols (file-local; not in generated headers) ---
// FUN_803826e8 / FUN_80382734 (FP frame open/close) declared in batch_622 block.
} // extern "C"

// switch_find_slot_by_entity/mtx4_set_rotation_y/mtx4_set_transform_euler/
// mtx4_set_identity/mtx4_multiply/mtx4_transform_point/particle_spawn_trail/
// session_get_state_flag/render_instance_set_material/entity_get_world_position/
// entity_register_for_update/scalar_step_toward_snap/game_rand/game_frand
// resolve through the generated headers.
//
// __we3_* aliases for the batch_624 arity/type-mismatched call sites:

// mtx4_set_translation: ROM form here is (x, y, z) doubles (writes the shared
// scratch matrix), NOT the generated (float* mtx, float, float, float).
static void (*const __we3_mtx4_set_translation3)(double, double, double) =
    (void (*)(double, double, double))&mtx4_set_translation;

// particle_slot_set_scale: (scale, slot handle as int) here.
static void (*const __we3_particle_slot_set_scale)(double, int) =
    (void (*)(double, int))&particle_slot_set_scale;


// ---------------------------------------------------------------------------
// 0x801ed030  ekun_dgtr_blink_tick
// Count the +0x1a4 timer down; on expiry re-arm it to FLOAT_80617758 and toggle
// the render node's flag word (+0x30 ^= 9) to blink the DGTR indicator.
// ---------------------------------------------------------------------------
void ekun_dgtr_blink_tick(int entity)
{
    *(float *)(entity + 0x1a4) =
        *(float *)(entity + 0x1a4) -
        FLOAT_806108ec * (FLOAT_806176dc /
            (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728));
    if (*(float *)(entity + 0x1a4) <= FLOAT_806176d0) {
        *(float *)(entity + 0x1a4) = FLOAT_80617758;
        *(unsigned int *)(*(int *)(entity + 4) + 0x30) ^= 9;
    }
}


// ---------------------------------------------------------------------------
// 0x801ed0a4  ekun_dgbt_switch_trigger
// The 'DGBT' switch handler: resolve the 'DGTR' blast-door entity and register
// it (and its render slot iVar3) on update lists 3/2/4 with their think
// callbacks, set the slot spin (+0x1a4) and node flags (0x1c), then register the
// two 'DARM' arm entities on list 3.
// ---------------------------------------------------------------------------
void ekun_dgbt_switch_trigger(void)
{
    unsigned int *slot = (unsigned int *)switch_find_slot_by_entity(0x44474254);  // 'DGBT'
    int dgtr = FUN_8023c32c(0x44475452);   // 'DGTR'
    int rslot = (int)slot[2];
    if (dgtr != 0) {
        entity_register_for_update(dgtr, 3, (int)0x801ece84);   // ekun_turbine_spin_fast
        entity_register_for_update(dgtr, 2, (int)0x801ecefc);   // ekun_turn_rig_update
        entity_register_for_update(rslot, 4, (int)0x801ed030);  // ekun_dgtr_blink_tick
        *(float *)(rslot + 0x1a4) = FLOAT_8061775c;
        *(int *)(*(int *)(rslot + 4) + 0x30) = 0x1c;
        int arms[2];
        FUN_8023c59c(0x4441524d, (int)arms, 2);   // 'DARM'
        entity_register_for_update(arms[0], 3, (int)0x801ecbb4);  // ekun_motor_rotate_sound_tick
        entity_register_for_update(arms[1], 3, (int)0x801ecbb4);
    }
}


// ---------------------------------------------------------------------------
// 0x801ed180  ekun_fan_blade_physics_tick
// Fan-blade think callback. While spinning up (+0x1a4 < 0) it decelerates the
// blade (+0xd0) and emits a motion-blur trail particle (asset 0x36) along the
// swept edge; on the way down it eases +0xd0 toward FLOAT_80617734. When the
// blade has fully wound down it sets node flag 0x23 and deregisters.
// (Register-pair spill locals from the ROM's FP frame are expressed as concrete
// floats; the two zero-difference terms are kept for structural fidelity.)
// ---------------------------------------------------------------------------
void ekun_fan_blade_physics_tick(int entity)
{
    float edge[3];      // local_b4/b0/ac : swept edge point (rotated)
    float prevPt[3];    // local_d8/d4/d0
    float delta[3];     // local_cc/c8/c4
    float curPt[3];     // local_c0/bc/b8
    float mtx[16];

    double dt = (double)(FLOAT_806108ec *
        (FLOAT_806176dc /
         (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728)));

    edge[0] = *(float *)(entity + 100) - *(float *)(entity + 100);
    edge[1] = *(float *)(entity + 0x68) - *(float *)(entity + 0x68);
    edge[2] = (*(float *)(entity + 0x6c) + FLOAT_8061771c) - *(float *)(entity + 0x6c);

    mtx4_set_rotation_y((double)*(float *)(entity + 0x70), mtx);
    mtx4_transform_point(mtx, edge);
    double ex = (double)(*(float *)(entity + 100) + edge[0]);
    double ey = (double)(*(float *)(entity + 0x68) + edge[2]);
    double ez = (double)(*(float *)(entity + 0x6c) + edge[2]);

    if ((double)*(float *)(entity + 0x1a4) < (double)FLOAT_806176d0) {
        if ((double)FLOAT_80617760 <= (double)*(float *)(entity + 0x1a4)) {
            if ((double)*(float *)(entity + 0xd0) <= (double)FLOAT_806176d0) {
                *(int *)(*(int *)(entity + 4) + 0x30) = 0x23;
                entity_register_for_update(entity, 3, 0);
            } else {
                *(float *)(entity + 0xd0) =
                    -(float)((double)FLOAT_80617744 * dt - (double)*(float *)(entity + 0xd0));
                *(float *)(entity + 0x70) =
                    (float)((double)*(float *)(entity + 0xd0) * dt + (double)*(float *)(entity + 0x70));
                prevPt[0] = *(float *)(entity + 100) - *(float *)(entity + 100);
                prevPt[1] = *(float *)(entity + 0x68) - *(float *)(entity + 0x68);
                prevPt[2] = (*(float *)(entity + 0x6c) + FLOAT_8061771c) - *(float *)(entity + 0x6c);
                mtx4_set_rotation_y((double)*(float *)(entity + 0x70), mtx);
                mtx4_transform_point(mtx, prevPt);
                curPt[0] = *(float *)(entity + 100) + prevPt[0];
                delta[0] = (float)((double)curPt[0] - ex);
                curPt[1] = *(float *)(entity + 0x68) + prevPt[2];
                delta[1] = (float)((double)curPt[1] - ey);
                curPt[2] = *(float *)(entity + 0x6c) + prevPt[2];
                delta[2] = (float)((double)curPt[2] - ez);
                float mag2 = delta[2] * delta[2] + delta[0] * delta[0] + delta[1] * delta[1];
                if (FLOAT_80617718 < mag2) {
                    float inv = 1.0f / SQRT(mag2);
                    inv = inv * -(FLOAT_806176e8 * mag2 * inv * inv - FLOAT_8061771c);
                    float s = FLOAT_806176dc * inv * -(FLOAT_806176e8 * mag2 * inv * inv - FLOAT_8061771c);
                    delta[0] = delta[0] * s;
                    delta[1] = delta[1] * s;
                    delta[2] = delta[2] * s;
                }
                int flag = session_get_state_flag();
                int slot = particle_spawn_trail(0, 0x36, ~-(unsigned int)(flag == 1) & 0x80000, curPt, delta);
                double rnd = game_frand();
                __we3_particle_slot_set_scale(rnd, slot);
                *(float *)(entity + 0x1a4) = (float)((double)*(float *)(entity + 0x1a4) - dt);
            }
        }
    } else {
        float spin = *(float *)(entity + 0xd0);
        float step = (float)((double)FLOAT_80617744 * dt);
        if (step <= ABS(spin - FLOAT_80617734)) {
            if (FLOAT_80617734 <= spin) {
                if (FLOAT_80617734 < spin) {
                    *(float *)(entity + 0xd0) = spin - step;
                }
            } else {
                *(float *)(entity + 0xd0) = spin + step;
            }
        } else {
            *(float *)(entity + 0xd0) = FLOAT_80617734;
        }
        *(float *)(entity + 0x70) =
            (float)((double)*(float *)(entity + 0xd0) * dt + (double)*(float *)(entity + 0x70));
        *(float *)(entity + 0x1a4) = (float)((double)*(float *)(entity + 0x1a4) - dt);
    }
}


// ---------------------------------------------------------------------------
// 0x801ed4ac  ekun_dgbt_reset
// Reset the 'DGBT' blast-door slot node flags (0x1c) and re-seed all 'DARM' arm
// entities' spin (+0x1a4 = FLOAT_80617764), re-registering each on update list 3.
// ---------------------------------------------------------------------------
void ekun_dgbt_reset(void)
{
    FUN_803826e8();
    unsigned int *slot = (unsigned int *)switch_find_slot_by_entity(0x44474254);  // 'DGBT'
    *(int *)(*(int *)(slot[2] + 4) + 0x30) = 0x1c;
    int arms[12];
    int count = FUN_8023c59c(0x4441524d, (int)arms, 2);   // 'DARM'
    for (int i = 0; i < count; ++i) {
        *(float *)(arms[i] + 0x1a4) = FLOAT_80617764;
        entity_register_for_update(arms[i], 3, (int)0x801ed180);  // ekun_fan_blade_physics_tick
    }
    FUN_80382734();
}


// ---------------------------------------------------------------------------
// 0x801ed560  ekun_flicker_random_tick
// Count the +0x1a4 timer down; on expiry toggle node flag (+0x30 ^= 3) and
// re-arm the timer to a random fraction of FLOAT_80617768.
// ---------------------------------------------------------------------------
void ekun_flicker_random_tick(int entity)
{
    *(float *)(entity + 0x1a4) =
        *(float *)(entity + 0x1a4) -
        FLOAT_806108ec * (FLOAT_806176dc /
            (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728));
    if (*(float *)(entity + 0x1a4) <= FLOAT_806176d0) {
        *(unsigned int *)(*(int *)(entity + 4) + 0x30) ^= 3;
        double rnd = game_frand();
        *(float *)(entity + 0x1a4) = (float)((double)FLOAT_80617768 * rnd);
    }
}


// ---------------------------------------------------------------------------
// 0x801ed5f8  ekun_vslide_arrive_tick
// After the +0x1a4 delay elapses, slide the entity's Y (+0x68) toward its target
// (+0xac) at a speed proportional to remaining distance and +0xb8; on arrival
// snap to the target and deregister.
// ---------------------------------------------------------------------------
void ekun_vslide_arrive_tick(int entity)
{
    float minStep = FLOAT_8061776c;
    float dt = FLOAT_806108ec *
        (FLOAT_806176dc /
         (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728));
    if (*(float *)(entity + 0x1a4) <= FLOAT_806176d0) {
        *(unsigned int *)(entity + 0x18) |= 0x40;
        float dist = *(float *)(entity + 0x68) - *(float *)(entity + 0xac);
        dt = dt * *(float *)(entity + 0xb8) * ABS(dist);
        if (minStep < dt) minStep = dt;
        *(float *)(entity + 0x68) = *(float *)(entity + 0x68) + minStep;
        if (FLOAT_806176d0 <= dist) {
            *(int *)(entity + 0x68) = *(int *)(entity + 0xac);
            entity_register_for_update(entity, 3, 0);
        }
    } else {
        *(float *)(entity + 0x1a4) = *(float *)(entity + 0x1a4) - dt;
    }
}


// ---------------------------------------------------------------------------
// 0x801ed6cc  ekun_rotor_swing_tick
// Accumulate a sine-modulated rotor angle (+0xcc) driven by spin (+0xd0) and the
// sine LUT, fold it into the render bone's +0x40 rotation; when the bone reaches
// its clamp (FLOAT_8061777c) stop (+0x1a4=0) and deregister.
// ---------------------------------------------------------------------------
void ekun_rotor_swing_tick(int entity)
{
    float clamp = FLOAT_8061777c;
    float gain  = FLOAT_80617778;
    float coefC = FLOAT_80617774;
    float base  = FLOAT_80617770;
    float kB    = FLOAT_80617724;
    float kA    = FLOAT_80617720;
    float half  = FLOAT_806176e8;
    int bone = *(int *)(entity + 0x7c);
    float dt = FLOAT_806108ec *
        (FLOAT_806176dc /
         (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728));
    *(float *)(entity + 0x1a4) = *(float *)(entity + 0x1a4) - dt;
    dt = dt * *(float *)(entity + 0xd0) *
         (base + (float)(&DAT_80583228)[(int)(kA * kB * coefC * *(float *)(entity + 0x1a4) + half) & 0x7ff]);
    *(float *)(entity + 0xcc) = *(float *)(entity + 0xcc) + dt;
    *(float *)(bone + 0x40) = gain * dt + *(float *)(bone + 0x40);
    if (clamp <= *(float *)(bone + 0x40)) {
        *(float *)(entity + 0x1a4) = FLOAT_806176d0;
        *(float *)(bone + 0x40) = clamp;
        entity_register_for_update(entity, 3, 0);
    }
}


// ---------------------------------------------------------------------------
// 0x801ed7c0  ekun_dial_bone_rig_update
// Build the DIAL's two-joint bone matrices: rotate the first bone about the base
// node pivot by the accumulated angle (+0xcc), rotate the second about the child
// node pivot, translate by the render slot's +0x40 offset, and reset the root.
// Sets dirty bit 0x200000.
// ---------------------------------------------------------------------------
void ekun_dial_bone_rig_update(int entity)
{
    float mtx[16];
    int rslot = *(int *)(entity + 0x7c);
    int *node = *(int **)(*(int *)(entity + 4) + 4);
    float *pivot = (float *)node[*node * -0x30 + 0x1b];
    double px = (double)pivot[0], py = (double)pivot[1], pz = (double)pivot[2];

    mtx4_set_transform_euler(px, py, pz, (double)*(float *)(entity + 0xcc),
                             (double)FLOAT_806176d0, (double)FLOAT_806176d0,
                             (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40));
    __we3_mtx4_set_translation3(-px, -py, -pz);
    float *bone0 = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40);
    mtx4_multiply(mtx, bone0, bone0);

    node = *(int **)(*(int *)(entity + 4) + 4);
    int childRec = node[*node * -0x30 + 0x1b];
    double cx = (double)*(float *)(childRec + 0x18);
    double cy = (double)*(float *)(childRec + 0x1c);
    double cz = (double)*(float *)(childRec + 0x20);
    mtx4_set_transform_euler(cx, cy, cz, (double)FLOAT_806176d0, (double)FLOAT_806176d0,
                             (double)*(float *)(entity + 0xcc),
                             (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x80));
    __we3_mtx4_set_translation3(-cx, -cy, -cz);
    float *bone1 = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x80);
    mtx4_multiply(mtx, bone1, bone1);

    __we3_mtx4_set_translation3((double)FLOAT_806176d0, (double)*(float *)(rslot + 0x40),
                                (double)FLOAT_806176d0);
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10));
    *(unsigned int *)(entity + 0x28) |= 0x200000;
}


// ---------------------------------------------------------------------------
// 0x801ed940  ekun_swwl_switch_sequence
// The 'SWWL' switch handler: activate the 'SWWT' wheel + 'DIAL' entities, set the
// dial slot material/spin and register its update callbacks, arm the fan spin on
// SWWT, mark the 'DGBT' door state 0x2a, then register all 'DMLT' debris movers
// (up to 10) on list 3 with a random initial node flag.
// ---------------------------------------------------------------------------
void ekun_swwl_switch_sequence(void)
{
    FUN_803826e8();
    unsigned int *slot = (unsigned int *)switch_find_slot_by_entity(0x5357574c);  // 'SWWL'
    int swwt = FUN_8023c32c(0x53575754);   // 'SWWT'
    int dial = FUN_8023c32c(0x4449414c);   // 'DIAL'
    int dslot = (int)slot[2];

    render_instance_set_material((double)FLOAT_806176d0, *(int **)(dslot + 4), 1);
    *(float *)(dslot + 0x1a4) = FLOAT_806176d0;
    entity_register_for_update(dslot, 3, (int)0x801ed6cc);   // ekun_rotor_swing_tick
    entity_register_for_update(dslot, 2, (int)0x801ed7c0);   // ekun_dial_bone_rig_update
    *(float *)(dslot + 0xd0) = FLOAT_806176dc;
    *(float *)&slot[0x10] = FLOAT_806176d0;   // store the float bit-pattern raw
    entity_register_for_update(dial, 3, 0);

    if (swwt != 0) {
        *(unsigned int *)(swwt + 0x28) |= 1;
        *(unsigned int *)(swwt + 0x28) &= 0xbffdffff;
        *(unsigned int *)(swwt + 0x18) |= 0x40;
        *(float *)(swwt + 0x1a4) = FLOAT_806176dc;
        *(float *)(swwt + 0xb8) = FLOAT_80617778;
        entity_register_for_update(swwt, 3, (int)0x801ed5f8);  // ekun_vslide_arrive_tick
    }

    slot = (unsigned int *)switch_find_slot_by_entity(0x44474254);  // 'DGBT'
    *(int *)(*(int *)(slot[2] + 4) + 0x30) = 0x2a;
    slot[4] &= 0xfffffffe;

    int movers[10];
    int count = FUN_8023c59c(0x444d4c54, (int)movers, 10);   // 'DMLT'
    for (int i = count - 1; i >= 0; --i) {
        entity_register_for_update(movers[i], 3, (int)0x801ed560);  // ekun_flicker_random_tick
        unsigned int r = game_rand();
        *(int *)(*(int *)(movers[i] + 4) + 0x30) = 1 << (r & 1);
        *(float *)(movers[i] + 0x1a4) = FLOAT_806176d0;
    }
    FUN_80382734();
}


// ---------------------------------------------------------------------------
// 0x801edb04  ekun_spin_loop_sound_tick
// Ramp the entity's spin (+0xd0) toward FLOAT_80617780, accumulate its wrapped
// angle (+0xc0), and drive its 3D loop sound (id 0x611) at a pitch proportional
// to the current spin.
// ---------------------------------------------------------------------------
void ekun_spin_loop_sound_tick(int entity)
{
    double dt = (double)(FLOAT_806108ec *
        (FLOAT_806176dc /
         (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728)));
    scalar_step_toward_snap((double)FLOAT_80617780, (double)(float)((double)FLOAT_80617784 * dt),
                            (float *)(entity + 0xd0));
    float wrap = FLOAT_80617738;
    *(float *)(entity + 0xc0) =
        (float)((double)*(float *)(entity + 0xd0) * dt + (double)*(float *)(entity + 0xc0));
    if (wrap < *(float *)(entity + 0xc0)) {
        *(float *)(entity + 0xc0) = *(float *)(entity + 0xc0) - wrap;
    }
    float *pos = entity_get_world_position(entity);
    __we3b_sound_update_3d_source((double)(*(float *)(entity + 0xd0) / FLOAT_80617780),
                                  (double)FLOAT_80617710, (double)FLOAT_806176dc,
                                  entity + 0x22c, (int)(long long)pos, 0x611, 0);
}


// ===========================================================================
// batch_625 : 0x801edbd8 - 0x801ee34b
// EKUN worm-boss door/engine set-pieces: the shared single-axis bone-rotate
// think ticks + rig builders, and the BDBT / BDEG / EGSW switch handlers that
// wire them up (with their one-shot sounds and scripted payload fires).
// Continuation of the CDH1 EKUN set-piece.
// ===========================================================================

extern "C" {
// --- batch_625 raw data symbols (file-local) ---
extern float FLOAT_80617788;    // bone open/close target angle
extern float FLOAT_8061778c;    // bone open/close step rate
extern float FLOAT_80617790;    // bone snap ramp scale
// (FLOAT_80617724 / FLOAT_80617730 / FLOAT_806176d0 / FLOAT_806176dc /
//  FLOAT_806108ec / DOUBLE_80617728 / DAT_806109c0 already declared above.)

// --- batch_625 raw code symbols (file-local; not in generated headers) ---
// FUN_803826f0 / FUN_8038273c / FUN_80221074 declared earlier in this file.
} // extern "C"

// scalar_step_toward_snap/entity_register_for_update/mtx4_set_transform_euler/
// mtx4_set_rotation_x/mtx4_set_identity/mtx4_multiply/switch_find_slot_by_entity/
// switch_action_open_close_trigger/sound_init_play_params/sound_play_request/
// entity_get_world_position resolve through the generated headers.
// mtx4_set_translation (3-double form) uses the __we3_mtx4_set_translation3 alias
// declared in the batch_624 block.


// ---------------------------------------------------------------------------
// 0x801edbd8  ekun_bone_step_to_target_tick
// Ease the render bone angle (+0x40 of the +0x7c slot) toward FLOAT_80617788 at
// a frame-scaled rate; when it snaps to target, deregister from update list 3.
// ---------------------------------------------------------------------------
void ekun_bone_step_to_target_tick(int entity)
{
    int done = scalar_step_toward_snap((double)FLOAT_80617788,
                    (double)(FLOAT_8061778c * FLOAT_806108ec *
                             (FLOAT_80617790 /
                              (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728))),
                    (float *)(*(int *)(entity + 0x7c) + 0x40));
    if (done != 0) {
        entity_register_for_update(entity, 3, 0);
    }
}


// ---------------------------------------------------------------------------
// 0x801edc60  ekun_zrot_bone_rig_update
// Build the render bone's Z-rotation matrix (+0x40) from the slot angle
// (+0x7c:+0x40) scaled by FLOAT_80617724/FLOAT_80617730, rotating about the base
// node pivot, then reset the root. Sets dirty bit 0x200000.
// ---------------------------------------------------------------------------
void ekun_zrot_bone_rig_update(int entity)
{
    float mtx[17];
    int *node = *(int **)(*(int *)(entity + 4) + 4);
    float *pivot = (float *)node[*node * -0x30 + 0x1b];
    double px = (double)pivot[0], py = (double)pivot[1], pz = (double)pivot[2];

    mtx4_set_transform_euler(px, py, pz,
        (double)((FLOAT_80617724 * *(float *)(*(int *)(entity + 0x7c) + 0x40)) / FLOAT_80617730),
        (double)FLOAT_806176d0, (double)FLOAT_806176d0,
        (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40));
    __we3_mtx4_set_translation3(-px, -py, -pz);
    float *bone = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40);
    mtx4_multiply(mtx, bone, bone);
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10));
    *(unsigned int *)(entity + 0x28) |= 0x200000;
}


// ---------------------------------------------------------------------------
// 0x801edd58  ekun_bone_open_tick
// Advance the bone angle (+0x7c:+0x40) at FLOAT_8061778c per frame until it
// reaches FLOAT_80617788, then deregister.
// ---------------------------------------------------------------------------
void ekun_bone_open_tick(int entity)
{
    float a = *(float *)(*(int *)(entity + 0x7c) + 0x40);
    if (FLOAT_80617788 <= a) {
        entity_register_for_update(entity, 3, 0);
    } else {
        *(float *)(*(int *)(entity + 0x7c) + 0x40) = a + FLOAT_8061778c * FLOAT_806108ec;
    }
}


// ---------------------------------------------------------------------------
// 0x801eddac  ekun_xrot_bone_rig_update
// Build the render bone's X-rotation matrix in the +0xc0 slot: translate to the
// base node pivot, rotate about X by the slot angle, translate back; reset the
// +0x40/+0x80 sub-bones and the root. Sets dirty bit 0x200000.
// ---------------------------------------------------------------------------
void ekun_xrot_bone_rig_update(int entity)
{
    float mtx[16];
    int slot = *(int *)(entity + 0x7c);
    int *node = *(int **)(*(int *)(entity + 4) + 4);
    float *pivot = (float *)node[*node * -0x30 + 0x1b];
    double px = (double)pivot[0], py = (double)pivot[1], pz = (double)pivot[2];

    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10) + 0xc0);
    __we3_mtx4_set_translation3(px, py, pz);
    float *bone = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0xc0);
    mtx4_multiply(mtx, bone, bone);
    mtx4_set_rotation_x((double)((FLOAT_80617724 * *(float *)(slot + 0x40)) / FLOAT_80617730), (int)mtx);
    bone = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0xc0);
    mtx4_multiply(mtx, bone, bone);
    __we3_mtx4_set_translation3(-px, -py, -pz);
    bone = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0xc0);
    mtx4_multiply(mtx, bone, bone);
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10));
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40);
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10) + 0x80);
    *(unsigned int *)(entity + 0x28) |= 0x200000;
}


// ---------------------------------------------------------------------------
// 0x801edf04  ekun_delay_set_flag_tick
// Count the +0x1a4 timer down; once it reaches zero set the render node flag
// (+0x30 = 1). (The timer keeps decrementing past zero; the flag latches.)
// ---------------------------------------------------------------------------
void ekun_delay_set_flag_tick(int entity)
{
    float dt = FLOAT_806108ec *
        (FLOAT_806176dc /
         (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728));
    if (*(float *)(entity + 0x1a4) <= FLOAT_806176d0) {
        *(int *)(*(int *)(entity + 4) + 0x30) = 1;
    }
    *(float *)(entity + 0x1a4) = *(float *)(entity + 0x1a4) - dt;
}


// ---------------------------------------------------------------------------
// 0x801edf6c  ekun_bdbt_switch_trigger
// The 'BDBT' door-button switch: register its render slot on update lists 3/2
// (bone open + Z-rot rig), set node flag 1, fire the shared open/close trigger
// and the scripted payload.
// ---------------------------------------------------------------------------
void ekun_bdbt_switch_trigger(unsigned long long param_1, double param_2, double param_3,
                              double param_4, double param_5, double param_6, double param_7,
                              double param_8)
{
    (void)param_1;
    unsigned int *slot = (unsigned int *)switch_find_slot_by_entity(0x42444254);  // 'BDBT'
    int rslot = (int)slot[2];
    *(float *)&slot[0x10] = FLOAT_806176d0;
    entity_register_for_update(rslot, 3, (int)0x801edd58);   // ekun_bone_open_tick
    entity_register_for_update(rslot, 2, (int)0x801edc60);   // ekun_zrot_bone_rig_update
    *(int *)(*(int *)(rslot + 4) + 0x30) = 1;
    switch_action_open_close_trigger((int)slot);
    FUN_80221074((double)FLOAT_806176d0, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
}


// ---------------------------------------------------------------------------
// 0x801ee004  ekun_bdeg_switch_sequence
// The 'BDEG' door-engine switch: play the door start sound (id 0x607) at the
// 'BDBT' render slot position, then register the 'BDEG' slot (bone open-full +
// X-rot rig), the 'BFAN' fan, and the 'BDBT' door (X-rot rig) on their update
// lists.
// ---------------------------------------------------------------------------
void ekun_bdeg_switch_sequence(void)
{
    unsigned int *bdeg = (unsigned int *)switch_find_slot_by_entity(0x42444547);  // 'BDEG'
    unsigned int *bdbt = (unsigned int *)switch_find_slot_by_entity(0x42444254);  // 'BDBT'
    int bdegSlot = (int)bdeg[2];

    // sound play-params block: id at +0x00, position (x,y,z) at +0x1c/+0x20/+0x24,
    // and the "positional" byte flag at +0x28 (local_48[7] then local_2c/28/24/20).
    unsigned char play[0x30];
    sound_init_play_params((int)(long long)play);
    *(int *)(play + 0x00) = 0x607;
    *(int *)(play + 0x1c) = *(int *)(bdegSlot + 100);   // pos.x
    *(int *)(play + 0x20) = *(int *)(bdegSlot + 0x68);  // pos.y
    *(int *)(play + 0x24) = *(int *)(bdegSlot + 0x6c);  // pos.z
    play[0x28] = 1;
    sound_play_request((int *)play);

    int bfan = FUN_8023c32c(0x4246414e);   // 'BFAN'
    int bdbtSlot = (int)bdbt[2];
    int rslot = (int)bdeg[2];
    *(float *)&bdeg[0x10] = FLOAT_806176d0;
    entity_register_for_update(rslot, 3, (int)0x801ee110);   // ekun_bone_open_full_tick
    entity_register_for_update(rslot, 2, (int)0x801ee164);   // ekun_zrot_bone_rig_update_b
    entity_register_for_update(bfan, 3, (int)0x801edbd8);    // ekun_bone_step_to_target_tick
    *(float *)(bdbtSlot + 0x1a4) = FLOAT_806176dc;
    *(int *)(*(int *)(bdbtSlot + 4) + 0x30) = 2;
    entity_register_for_update(bdbtSlot, 3, (int)0x801eddac); // ekun_xrot_bone_rig_update
}


// ---------------------------------------------------------------------------
// 0x801ee110  ekun_bone_open_full_tick
// Advance the bone angle (+0x7c:+0x40) at FLOAT_8061778c per frame until it
// reaches the full-open angle FLOAT_80617730, then deregister.
// ---------------------------------------------------------------------------
void ekun_bone_open_full_tick(int entity)
{
    float a = *(float *)(*(int *)(entity + 0x7c) + 0x40);
    if (FLOAT_80617730 <= a) {
        entity_register_for_update(entity, 3, 0);
    } else {
        *(float *)(*(int *)(entity + 0x7c) + 0x40) = a + FLOAT_8061778c * FLOAT_806108ec;
    }
}


// ---------------------------------------------------------------------------
// 0x801ee164  ekun_zrot_bone_rig_update_b
// Same Z-rotation bone rig builder as ekun_zrot_bone_rig_update (distinct ROM
// instance used by the BDEG slot).
// ---------------------------------------------------------------------------
void ekun_zrot_bone_rig_update_b(int entity)
{
    float mtx[17];
    int *node = *(int **)(*(int *)(entity + 4) + 4);
    float *pivot = (float *)node[*node * -0x30 + 0x1b];
    double px = (double)pivot[0], py = (double)pivot[1], pz = (double)pivot[2];

    mtx4_set_transform_euler(px, py, pz,
        (double)((FLOAT_80617724 * *(float *)(*(int *)(entity + 0x7c) + 0x40)) / FLOAT_80617730),
        (double)FLOAT_806176d0, (double)FLOAT_806176d0,
        (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40));
    __we3_mtx4_set_translation3(-px, -py, -pz);
    float *bone = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40);
    mtx4_multiply(mtx, bone, bone);
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10));
    *(unsigned int *)(entity + 0x28) |= 0x200000;
}


// ---------------------------------------------------------------------------
// 0x801ee25c  ekun_egsw_switch_sequence
// The 'EGSW' engine switch: latch its active bit, activate the 'EENG' engine
// entity, register the EGSW slot (bone open + Z-rot rig) and EENG (spin-loop
// sound), play the engine start sound (id 0x615) at the engine world position,
// then fire the scripted payload.
// ---------------------------------------------------------------------------
void ekun_egsw_switch_sequence(unsigned long long param_1, double param_2, double param_3,
                               double param_4, double param_5, double param_6, double param_7,
                               double param_8)
{
    (void)param_1;
    FUN_803826f0();
    unsigned int *slot = (unsigned int *)switch_find_slot_by_entity(0x45475357);  // 'EGSW'
    slot[4] |= 1;
    int eeng = FUN_8023c32c(0x45454e47);   // 'EENG'
    int rslot = (int)slot[2];
    *(float *)&slot[0x10] = FLOAT_806176d0;
    entity_register_for_update(rslot, 3, (int)0x801edd58);   // ekun_bone_open_tick
    entity_register_for_update(rslot, 2, (int)0x801edc60);   // ekun_zrot_bone_rig_update
    entity_register_for_update(eeng, 3, (int)0x801edb04);    // ekun_spin_loop_sound_tick

    float *pos = entity_get_world_position(eeng);
    unsigned char play[0x30];
    sound_init_play_params((int)(long long)play);
    *(int *)(play + 0x00) = 0x615;
    *(int *)(play + 0x1c) = *(int *)&pos[0];   // pos.x
    *(int *)(play + 0x20) = *(int *)&pos[1];   // pos.y
    *(int *)(play + 0x24) = *(int *)&pos[2];   // pos.z
    play[0x28] = 1;
    sound_play_request((int *)play);

    FUN_80221074((double)FLOAT_806176d0, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    FUN_8038273c();
}


// ===========================================================================
// batch_626  0x801ee34c - 0x801ee6d7 (+ break/joint helpers to 0x801eedac)
// EKUN hinged blast-door panel physics: a spring-driven swing/twist joint rig
// with break-off, contact/impact sound, area-damage and explosion spawns.
//
// The panel entity keeps its swing state in the int at +0x1a0 (0 = closed/idle,
// 1 = opening, 2 = settling/open; the retract path also uses these), the swing
// velocity/angle in the float at +0xb8, the current linear extent in +0x1a4,
// and a break-progress float in +0x1cc. The break-off panels are spawned as
// physics sub-object "islands" (subobj_island_*) with an axis (hinge) joint;
// the joint callback (ekun_hinge_twist_settle_cb) drives the spring to the
// open/closed twist target based on +0x1a0.
//
// Non-matching: FP spill frames FUN_803826f0 (open, returns packed
// entity/handle pair) / FUN_8038273c (close) are opaque; several helpers are
// reached through file-local typed fn-ptr aliases (__we626_*) because their
// generated_decls prototype has reduced arity; 4CC ids ('SDTR','SDBU') are raw
// class words.
// ===========================================================================

extern "C" {

// --- raw data symbols for batch_626 (file-local; not already declared above) ---
extern int    DAT_806109c0;        // int-encoded frame-count reciprocal seed
extern double DOUBLE_80617728;     // 2^52 fp-magic bias constant
extern float  FLOAT_806176d0;      // 0.0f
extern float  FLOAT_806176d4;      // scratch default (contact scale)
extern float  FLOAT_806176d8;      // contact-normal bias
extern float  FLOAT_806176dc;      // 1.0f
extern float  FLOAT_806176e8;      // rsqrt Newton coefficient (1.5 * x)
extern float  FLOAT_806176ec;      // area-damage falloff radius
extern float  FLOAT_80617708;      // hinge drift gain
extern float  FLOAT_80617710;      // impact-sound base pitch
extern float  FLOAT_80617714;      // hinge spring "settle" stiffness
extern float  FLOAT_80617718;      // min-length-squared epsilon
extern float  FLOAT_8061771c;      // 0.5f (rsqrt Newton term)
extern float  FLOAT_80617724;      // full Z-rot angle
extern float  FLOAT_80617730;      // full-open bone angle
extern float  FLOAT_80617734;      // spin-loop sound pitch
extern float  FLOAT_80617740;      // twist-settle threshold
extern float  FLOAT_80617744;      // contact-normal magnitude scale
extern float  FLOAT_8061775c;      // hinge restitution
extern float  FLOAT_80617770;      // blast-door sound volume
extern float  FLOAT_80617790;      // (reserved tuning)
extern float  FLOAT_80617794;      // spin-loop sound volume
extern float  FLOAT_8061779c;      // break-release dot threshold
extern float  FLOAT_806177a0;      // hinge twist limit / open target
extern float  FLOAT_806177a4;      // hinge closed spring target
extern float  FLOAT_806177a8;      // initial break-progress value
extern float  FLOAT_806108ec;      // per-frame dt scale
extern float  FLOAT_80610dfc;      // swing accel (opening)
extern float  FLOAT_80610e00;      // swing accel (retract)
extern float  FLOAT_80610e04;      // swing damping coefficient
extern float  FLOAT_80610e08;      // retract minimum extent

// break/physics + spawn helpers not in generated headers with matching arity
extern unsigned int FUN_80220f30(int which);     // net/gate query (returns 0 to run locally)

} // extern "C"

// FUN_803826f0 is declared `int FUN_803826f0(void)` in the extern "C" block near
// the top of this file (batch_621). The batch_626 hinge callbacks need it two
// ways: as a pointer to the running trigger context, and as a packed
// (island<<32 | body) pair. Route both through typed fn-ptr aliases over that
// single declaration so we neither redeclare nor fight the return type.
static int (*const __we626_ctx_open_i)(void) = &FUN_803826f0;
static unsigned long long (*const __we626_ctx_open_pair)(void) =
    (unsigned long long (*)(void))&FUN_803826f0;

// --- typed fn-ptr aliases for reduced-arity / thin-return generated_decls prototypes ---
// The generated_decls prototypes for these are byte-thin (drop the fp-register
// args and/or the ROM's real return value). Each call site in this batch needs
// the fuller ROM ABI, so we re-view the same symbol through a typed pointer.

// subobj_break_tick declared (u64,u64,double x6); ROM form here is 7 fp regs + spill.
static void (*const __we626_subobj_break_tick)(double, double, double, double,
        double, double, double, unsigned long long) =
    (void (*)(double, double, double, double, double, double, double,
        unsigned long long))&subobj_break_tick;

// entity_create_instance declared (int,float*)->void*; ROM form: ctx + 7 doubles + (classId, posPtr, srcPtr).
static int (*const __we626_entity_create_instance)(unsigned long long, double, double,
        double, double, double, double, double, int, void *, void *) =
    (int (*)(unsigned long long, double, double, double, double, double, double,
        double, int, void *, void *))&entity_create_instance;

// subobj_build_descriptor_default declared (uint,int,float); ROM form: fpctx + 7 doubles + (handle, entity, a, b, c).
static void (*const __we626_subobj_build_descriptor_default)(double, double, double,
        double, double, double, double, unsigned int, int, int, int, float) =
    (void (*)(double, double, double, double, double, double, double,
        unsigned int, int, int, int, float))&subobj_build_descriptor_default;

// subobj_pool_alloc declared (void)->void; ROM returns the new body handle.
static unsigned int (*const __we626_subobj_pool_alloc)(void) =
    (unsigned int (*)(void))&subobj_pool_alloc;

// subobj_island_create_axis declared (uint,uint,float*,float*)->void; ROM returns the island index.
static int (*const __we626_subobj_island_create_axis)(unsigned int, unsigned int, float *, float *) =
    (int (*)(unsigned int, unsigned int, float *, float *))&subobj_island_create_axis;

// subobj_island_refresh_twist declared (int)->void; ROM returns the twist angle (float).
static float (*const __we626_subobj_island_refresh_twist)(int) =
    (float (*)(int))&subobj_island_refresh_twist;

// subobj_get_contact_normal declared (uint)->int; ROM returns a float* into the body.
static float *(*const __we626_subobj_get_contact_normal)(unsigned int) =
    (float *(*)(unsigned int))&subobj_get_contact_normal;

// switch_slot_reset declared (int)->void; ROM reuses it as a slot-init that returns a mode int.
static int (*const __we626_switch_slot_reset)(int) =
    (int (*)(int))&switch_slot_reset;

// sound_update_3d_source declared returning void; ROM returns nonzero when a voice starts.
static int (*const __we626_sound_update_3d_source)(double, double, double, int, unsigned int *, float *, unsigned int) =
    (int (*)(double, double, double, int, unsigned int *, float *, unsigned int))&sound_update_3d_source;


// ---------------------------------------------------------------------------
// 0x801ee34c  ekun_panel_retract_tick
// Retract the panel: pull the linear extent (+0x1cc) toward 0 by the per-frame
// step (arg8). When it bottoms out, clear the "moving" flag bit 0x10, set state
// (+0x1a0) to 2, and spawn explosion class 0x15 at the panel + local offset.
// ---------------------------------------------------------------------------
void ekun_panel_retract_tick(int *self)
{
    float floor = FLOAT_806176d0;
    int e = *self;
    *(float *)(e + 0x1cc) = *(float *)(e + 0x1cc) - (float)self[8];
    if (*(float *)(e + 0x1cc) <= floor) {
        *(float *)(e + 0x1cc) = floor;
        *(unsigned int *)(e + 0x28) &= 0xffffffef;
        *(int *)(e + 0x1a0) = 2;
        float ox = *(float *)(e + 0x64)  + *(float *)(e + 0x1a8);
        float oy = *(float *)(e + 0x68)  + *(float *)(e + 0x1ac);
        float oz = *(float *)(e + 0x6c)  + *(float *)(e + 0x1b0);
        float pos[3] = { ox, oy, oz };
        player_ai_explosion_spawn(0x15, 0, pos, (float *)(e + 0x284), 0, e, 1);
    }
}


// ---------------------------------------------------------------------------
// 0x801ee3f4  ekun_panel_spring_tick
// Spring-driven open/close of the panel's linear extent (+0x1a4) using velocity
// (+0xb8) and a dt derived from the frame-count reciprocal seed. State machine
// on +0x1a0: 1 = extending up (until it drops below FLOAT_80610e08 -> state 0),
// 0/negative = opening (accel FLOAT_80610dfc, until extent >= 0 -> state 1),
// 1<state<3 = damped settle (until |vel| small -> deregister). Always drives the
// 0x610 spin-loop 3D sound at the panel position.
// ---------------------------------------------------------------------------
void ekun_panel_spring_tick(int self)
{
    float zero = FLOAT_806176d0;
    int state = *(int *)(self + 0x1a0);
    // dt = frame-scale / (frameCountReciprocalSeed) using the fp-magic bias.
    float dt = FLOAT_806108ec *
        (FLOAT_806176dc /
         (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728));

    if (state == 1) {
        *(float *)(self + 0xb8) += FLOAT_80610e00;
        *(float *)(self + 0x1a4) += *(float *)(self + 0xb8) * dt;
        float z2 = FLOAT_806176d0;
        if (*(float *)(self + 0x1a4) <= FLOAT_80610e08) {
            *(int *)(self + 0x1a0) = 0;
            *(float *)(self + 0x1a4) = z2;
            *(float *)(self + 0xb8) = z2;
        }
    } else if (state < 1) {
        if (-1 < state) {
            *(float *)(self + 0xb8) += FLOAT_80610dfc;
            *(float *)(self + 0x1a4) += *(float *)(self + 0xb8) * dt;
            if (zero <= *(float *)(self + 0x1a4)) {
                *(int *)(self + 0x1a0) = 1;
            }
        }
    } else if (state < 3) {
        // damped settle: velocity *= -(dt_scale*damp - 1); stop when tiny.
        *(float *)(self + 0xb8) =
            *(float *)(self + 0xb8) * -(FLOAT_806108ec * FLOAT_80610e04 - FLOAT_806176dc);
        if (ABS(*(float *)(self + 0xb8)) < dt) {
            *(float *)(self + 0xb8) = FLOAT_806176d0;
            entity_register_for_update(self, 3, 0);
        }
    }

    float *pos = entity_get_world_position(self);
    int started = __we626_sound_update_3d_source((double)FLOAT_80617794, (double)FLOAT_80617734,
                    (double)FLOAT_806176dc, 0x610, (unsigned int *)(self + 0x22c), pos, 0);
    if (started != 0) {
        *(unsigned int *)(self + 0x18) |= 0x40;
    }
}


// ---------------------------------------------------------------------------
// 0x801ee574  ekun_panel_rig_reset
// Reset the panel's bone matrices: identity the base bone, set the child bone
// translation to (0, +0x1a4, 0), identity the +0x80 sub-bone, and mark the
// pose dirty (flag 0x200000).
// ---------------------------------------------------------------------------
void ekun_panel_rig_reset(int self)
{
    int base = *(int *)(*(int *)(self + 4) + 0x10);
    mtx4_set_identity(base);
    // child bone matrix lives at base+0x40; set its translation to (0, extent, 0).
    mtx4_set_translation((float *)(base + 0x40), FLOAT_806176d0,
                         *(float *)(self + 0x1a4), FLOAT_806176d0);
    mtx4_set_identity(base + 0x80);
    *(unsigned int *)(self + 0x28) |= 0x200000;
}


// ---------------------------------------------------------------------------
// 0x801ee5e0  ekun_sdbu_blast_trigger
// The 'SDTR'/'SDBU' blast-door trigger. Reads the running trigger context
// (FUN_803826f0). If the linked entity is a class-0x17 with anim tag 0x460 and
// is NOT the 'SDTR' entity, play the blast sound 0x616 at its position, then
// destroy+launch the trigger (mode 2, id 0x7c). If it was the 'SDBU' entity,
// also fire the scripted payload.
// ---------------------------------------------------------------------------
void ekun_sdbu_blast_trigger(unsigned long long param_1, double param_2, double param_3,
                             double param_4, double param_5, double param_6, double param_7,
                             double param_8)
{
    (void)param_1;
    int ctxAddr = __we626_ctx_open_i();
    int *ctx = (int *)(long long)ctxAddr;
    int linked = ctx[2];
    if (linked == 0) {
        linked = ctx[1];
    }
    int trig = *ctx;
    if (((linked != 0) && (ctx[6] == 0x17)) && (*(short *)(linked + 0x94) == 0x460) &&
        (*(int *)(trig + 0x60) != 0x53445452 /* 'SDTR' */)) {
        float *pos = entity_get_world_position(trig);
        unsigned char play[0x30];
        sound_init_play_params((int)(long long)play);
        play[0x28] = 1;                            // +0x28 = 3D flag
        *(int *)(play + 0x00) = 0x616;
        *(float *)(play + 0x10) = FLOAT_80617770;  // volume
        *(int *)(play + 0x1c) = *(int *)&pos[0];
        *(int *)(play + 0x20) = *(int *)&pos[1];
        *(int *)(play + 0x24) = *(int *)&pos[2];
        sound_play_request((int *)play);
        trigger_destroy_and_launch_entity(ctxAddr, 2, 0x7c);
        if (*(int *)(trig + 0x60) == 0x53444255 /* 'SDBU' */) {
            FUN_80221074((double)FLOAT_806176d0, param_2, param_3, param_4, param_5, param_6,
                         param_7, param_8);
        }
    }
    FUN_8038273c();
}


// ---------------------------------------------------------------------------
// 0x801ee6d8  ekun_panel_break_tick
// Per-frame break tick for a spawned break-off panel. Resolves a swing/break
// direction from the optional source pointers (self[9] gate, self[10] origin,
// self[0xb] negated dir, self[0xc] explicit dir; else derive from the delta to
// the parent and normalise via the rsqrt Newton step). When the break-progress
// (+0x1cc) is still positive and the linked sub-object island is a 0x400000
// body, project the world delta into the body's local frame; if the alignment
// exceeds FLOAT_8061779c, zero the break-progress (release). Then run the shared
// subobj_break_tick.
// ---------------------------------------------------------------------------
void ekun_panel_break_tick(unsigned long long param_1, double param_2, double param_3,
                           double param_4, double param_5, double param_6, double param_7,
                           unsigned long long param_8, int *self)
{
    (void)param_1;
    int e = *self;
    if (self[9] != 0) {
        __we626_subobj_break_tick(param_2, param_3, param_4, param_5, param_6, param_7,
                                  (double)FLOAT_806176d0, param_8);
        return;
    }

    float ox, oy, oz;
    float *origin = (float *)self[10];
    if (origin == 0) {
        ox = *(float *)(e + 0x64); oy = *(float *)(e + 0x68); oz = *(float *)(e + 0x6c);
    } else {
        ox = origin[0]; oy = origin[1]; oz = origin[2];
    }

    double dx, dy, dz;
    float *negdir = (float *)self[0xb];
    if (negdir != 0) {
        dx = -(double)negdir[0]; dy = -(double)negdir[1]; dz = -(double)negdir[2];
    } else {
        float *dir = (float *)self[0xc];
        if (dir != 0) {
            dx = (double)dir[0]; dy = (double)dir[1]; dz = (double)dir[2];
        } else {
            double ddx = (double)(*(float *)(e + 0x64) - ox);
            double ddy = (double)(*(float *)(e + 0x68) - oy);
            double ddz = (double)(*(float *)(e + 0x6c) - oz);
            double len2 = (double)(float)(ddz * ddz +
                          (double)(float)(ddx * ddx + (double)(float)(ddy * ddy)));
            dx = ddx; dy = ddy; dz = ddz;
            if ((double)FLOAT_80617718 < len2) {
                double r = 1.0 / SQRT(len2);
                double half = (double)FLOAT_8061771c;
                double k = (double)(float)((double)FLOAT_806176e8 * len2);
                float n = (float)(r * -(double)(float)(k * (double)(float)(r * r) - half));
                double scale = (double)(float)((double)FLOAT_806176dc *
                                (double)(n * -(float)(k * (double)(n * n) - half)));
                dx = (double)(float)(ddx * scale);
                dy = (double)(float)(ddy * scale);
                dz = (double)(float)(ddz * scale);
            }
        }
    }

    double prog = (double)*(float *)(e + 0x1cc);
    int island = self[2];
    if (((double)FLOAT_806176d0 < prog) && (self[9] == 0) && (island != 0) &&
        (*(int *)(island + 0x20) == 0x400000)) {
        float world[3] = { ox, oy, oz };
        float local[3];
        subobj_world_to_local_dir(*(unsigned int *)(island + 0x80), world, local);
        double align = ABS((double)(float)((double)local[2] * dz +
                        (double)(float)((double)local[0] * dx +
                        (double)(float)((double)local[1] * dy))));
        if ((double)FLOAT_8061779c < align) {
            *(float *)(e + 0x1cc) = FLOAT_806176d0;
        }
    }

    __we626_subobj_break_tick(dx, dy, dz, param_5, param_6, param_7,
                              (double)FLOAT_806176d0, param_8);
}


// ---------------------------------------------------------------------------
// 0x801ee8c4  ekun_panel_impact_sound_tick
// Compute an impact pitch from the current contact-normal magnitude of the
// panel's hinge island (clamped to FLOAT_806176dc), then drive the 0x60e impact
// 3D sound. Marks the "sound active" bit 0x40 when a voice starts.
// ---------------------------------------------------------------------------
void ekun_panel_impact_sound_tick(int self)
{
    double pitch = (double)FLOAT_806176d4;
    int island = subobj_island_find_by_body_b(*(int *)(self + 0x1a0));
    unsigned int bodies[5];
    if ((-1 < island) &&
        (subobj_island_get_bodies(island, bodies, (unsigned int *)0), bodies[0] != 0)) {
        float *n = __we626_subobj_get_contact_normal(bodies[0]);
        pitch = (double)(FLOAT_80617744 * (n[2] * n[2] + n[0] * n[0] + n[1] * n[1]) -
                         FLOAT_806176d8);
        if ((double)FLOAT_806176dc < pitch) {
            pitch = (double)FLOAT_806176dc;
        }
    }
    int started = __we626_sound_update_3d_source(pitch, (double)FLOAT_80617710,
                    (double)FLOAT_806176dc, 0x60e, (unsigned int *)(self + 0x22c),
                    (float *)(self + 0x64), 0);
    if (started != 0) {
        *(unsigned int *)(self + 0x18) |= 0x40;
    }
}


// ---------------------------------------------------------------------------
// 0x801ee99c  ekun_hinge_twist_settle_cb
// Hinge-joint per-solve callback. Reads the running island/body pair, refreshes
// the current twist angle; if it has reached the limit (|twist| >=
// FLOAT_80617740) clear the joint's active flag bits, otherwise keep the spring
// engaged (flags | 0x200100) and leave the owner entity in state 2.
// ---------------------------------------------------------------------------
void ekun_hinge_twist_settle_cb(void)
{
    unsigned long long pair = __we626_ctx_open_pair();
    int island = (int)(pair >> 0x20);
    unsigned int body = (unsigned int)pair;
    int owner = subobj_get_entity(body);
    double twist = (double)__we626_subobj_island_refresh_twist(island);
    if ((double)FLOAT_80617740 <= ABS(twist)) {
        unsigned int f = subobj_get_flags(body);
        subobj_set_flags(body, f & 0xffdffeff);
    } else {
        subobj_island_set_spring((double)FLOAT_806176d0, island);
        unsigned int f = subobj_get_flags(body);
        subobj_set_flags(body, f | 0x200100);
        *(int *)(owner + 0x1a0) = 2;
    }
    FUN_8038273c();
}


// ---------------------------------------------------------------------------
// 0x801eea40  ekun_hinge_swing_tick
// Per-frame driver for the hinge swing. Finds the island by body-b handle, wakes
// the body (dirty-list + flags | 0x200100), then advances the owner's swing
// state (+0x1a0): state 1 -> settle spring FLOAT_80617714 (state 2); state 0/neg
// -> pick open (FLOAT_806177a4 -> state 1) vs settle (FLOAT_80617714 -> state 2)
// by comparing the current twist to the open target FLOAT_806177a0; state 1<x<3
// -> open spring FLOAT_806177a4 (state 1). Installs ekun_hinge_twist_settle_cb.
// ---------------------------------------------------------------------------
void ekun_hinge_swing_tick(int self)
{
    int island = subobj_island_find_by_body_b(*(int *)(self + 0x1a0));
    if (-1 < island) {
        unsigned int bodies[4];
        subobj_island_get_bodies(island, bodies, (unsigned int *)0);
        int owner = subobj_get_entity(bodies[0]);
        subobj_dirty_list_add(bodies[0]);
        unsigned int f = subobj_get_flags(bodies[0]);
        subobj_set_flags(bodies[0], f | 0x200100);

        int state = *(int *)(owner + 0x1a0);
        if (state == 1) {
            *(int *)(owner + 0x1a0) = 2;
            subobj_island_set_spring((double)FLOAT_80617714, island);
            subobj_island_set_user_ptr(island, (void *)&ekun_hinge_twist_settle_cb);
        } else if (state < 1) {
            if (-1 < state) {
                double twist = (double)__we626_subobj_island_refresh_twist(island);
                subobj_island_set_user_ptr(island, (void *)&ekun_hinge_twist_settle_cb);
                if (ABS(twist) < ABS((double)(float)(twist - (double)FLOAT_806177a0))) {
                    *(int *)(owner + 0x1a0) = 1;
                    subobj_island_set_spring((double)FLOAT_806177a4, island);
                } else {
                    *(int *)(owner + 0x1a0) = 2;
                    subobj_island_set_spring((double)FLOAT_80617714, island);
                }
            }
        } else if (state < 3) {
            *(int *)(owner + 0x1a0) = 1;
            subobj_island_set_spring((double)FLOAT_806177a4, island);
            subobj_island_set_user_ptr(island, (void *)&ekun_hinge_twist_settle_cb);
        }
    }
}


// ---------------------------------------------------------------------------
// 0x801eeba0  ekun_spawn_hinge_panel
// Spawn a break-off hinged panel entity: register its think (ekun_panel_break_tick
// slot 4) and a second update slot, create the render instance (class 0x463) at
// the parent's position, wire its render callback to ekun_panel_break_tick, seed
// the break-progress (+0x1cc) and flags, allocate a sub-object body, build the
// default descriptor, then build an axis (hinge) joint island at the parent's
// bone pivot with twist limits / restitution / drift gain and install the joint
// callback keyed by the packed |pos| id written to +0x1a0.
// ---------------------------------------------------------------------------
void ekun_spawn_hinge_panel(unsigned long long param_1, double param_2, double param_3,
                            double param_4, double param_5, double param_6, double param_7,
                            double param_8, int parent)
{
    (void)param_1;
    entity_register_for_update(parent, 4, (int)0x801ee6d8);          // ekun_panel_break_tick
    entity_register_for_update(parent, 0, (int)0x801eea40);          // ekun_hinge_swing_tick
    // The ROM threads the running fp context (returned in a GPR pair) into the
    // spawn; that context is opaque here, so pass 0.
    unsigned long long ctx = 0;
    *(unsigned int *)(parent + 0x28) |= 4;

    float seed[3];
    seed[0] = *(float *)(parent + 0xc0);
    *(int *)&seed[1] = *(int *)(parent + 0x70);
    *(int *)&seed[2] = *(int *)(parent + 0xc8);

    int inst = __we626_entity_create_instance(ctx, param_2, param_3, param_4, param_5, param_6,
                    param_7, param_8, 0x463, (void *)(parent + 0x64), seed);
    entity_set_render_type(inst, (int)(long long)&ekun_panel_break_tick);
    *(float *)(inst + 0x1cc) = FLOAT_806177a8;
    *(int *)(inst + 0x1d8) = *(int *)(inst + 0x1cc);
    *(unsigned int *)(inst + 0x28) |= 0x70;
    *(int *)(*(int *)(inst + 4) + 0x30) = 1;

    unsigned int body = __we626_subobj_pool_alloc();
    *(unsigned int *)(inst + 0x80) = body;
    *(int *)(inst + 0x1a0) = 0;
    __we626_subobj_build_descriptor_default(param_2, param_3, param_4, param_5, param_6, param_7,
                    param_8, *(unsigned int *)(inst + 0x80), inst, 9, -1, 0.0f);
    subobj_set_half_extent_x(*(unsigned int *)(inst + 0x80), 0xe);

    float rig[16];
    entity_build_local_matrix(rig, (void *)(long long)parent, 1, 0);
    int *node = *(int **)(*(int *)(parent + 4) + 4);
    float *pivot = (float *)node[*node * -0x30 + 0x1b];
    float p[3] = { pivot[0], pivot[1], pivot[2] };
    mtx4_transform_point_alias(rig, p);
    float axis[4];
    axis[0] = rig[0]; axis[1] = rig[1]; axis[2] = rig[2];  // matrix col0 = hinge axis
    int island = __we626_subobj_island_create_axis(*(unsigned int *)(inst + 0x80), 0, p, axis);
    subobj_island_set_twist_limits((double)FLOAT_806177a0, (double)FLOAT_806176d0, island);
    subobj_island_set_restitution((double)FLOAT_8061775c, island);
    subobj_island_set_drift_gain((double)FLOAT_80617708, island);

    unsigned int px = (unsigned int)(int)ABS(*(float *)(parent + 0x64));
    unsigned int py = (unsigned int)(int)ABS(*(float *)(parent + 0x68));
    unsigned int pz = (unsigned int)(int)ABS(*(float *)(parent + 0x6c));
    unsigned int packed = (unsigned int)(unsigned char)pz |
                          ((unsigned int)(unsigned char)px << 0x10) | 0x42000000U |
                          ((unsigned int)(unsigned char)py << 8);
    *(unsigned int *)(parent + 0x1a0) = packed;
    subobj_island_set_joint_callback(island, (void *)(unsigned long long)packed);
}


// ---------------------------------------------------------------------------
// 0x801eedac  ekun_panel_area_damage
// Area-damage tick: gated by FUN_80220f30(1) (net authority). Computes the panel
// centre (+pos + local offset), the delta to the reference entity, normalises it
// via the rsqrt Newton step, then dispatches damage id 0x12 at radius
// FLOAT_806176ec through the shared damage_resolve_and_dispatch with the panel +
// reference entity in the context block.
// ---------------------------------------------------------------------------
void ekun_panel_area_damage(int refEntity, int *self)
{
    unsigned int gate = FUN_80220f30(1);
    if (gate != 0) {
        return;
    }
    int e = *self;
    float cx = *(float *)(e + 0x64) + *(float *)(e + 0x1a8);
    float cy = *(float *)(e + 0x68) + *(float *)(e + 0x1ac);
    float cz = *(float *)(e + 0x6c) + *(float *)(e + 0x1b0);
    float centre[3] = { cx, cy, cz };

    float dx = cx - (*(float *)(refEntity + 0x64) + *(float *)(refEntity + 0x1a8));
    float dy = cy - (*(float *)(refEntity + 0x68) + *(float *)(refEntity + 0x1ac));
    float dz = cz - (*(float *)(refEntity + 0x6c) + *(float *)(refEntity + 0x1b0));
    float dir[3] = { dx, dy, dz };

    float len2 = (float)((double)(float)(dz * dz) +
                 (double)(float)((double)(float)(dx * dx) + (double)(float)(dy * dy)));
    if (FLOAT_80617718 < len2) {
        float r = 1.0f / SQRT(len2);
        double k = (double)(FLOAT_806176e8 * len2);
        r = r * -(float)(k * (double)(r * r) - (double)FLOAT_8061771c);
        double scale = (double)(r * -(float)(k * (double)(r * r) - (double)FLOAT_8061771c)) *
                       (double)FLOAT_806176dc;
        dir[0] = (float)((double)dx * scale);
        dir[1] = (float)((double)dy * scale);
        dir[2] = (float)((double)dz * scale);
    }

    // Build the damage event packet the ROM lays out on the stack: [entity,
    // source, &centre, &dir, radius, damage-id]. switch_slot_reset seeds the
    // slot header (its return is the mode int the ROM threads into f1; dropped
    // here since damage_resolve_and_dispatch consumes the packet directly).
    int ctx[8];
    ctx[0] = e;
    ctx[1] = refEntity;
    *(float **)&ctx[2] = centre;
    *(float **)&ctx[3] = dir;
    *(float *)&ctx[4] = FLOAT_806176ec;   // radius
    ctx[5] = 0x12;                          // damage id
    (void)__we626_switch_slot_reset((int)(long long)ctx);
    damage_resolve_and_dispatch(ctx);
}


// ===========================================================================
// batch_627  0x801eef10 - 0x801efeb8
// EKUN 'FOOR' nuke-tank + 'SWWL' dial set-piece: the debris ambient rattle,
// the plate/slide bone rigs, the FOOR nuke arm/countdown/detonation state
// machine, and the SWWL dial spin/speed rigs.
//
// The nuke arms via a screen-effect slot (ekun_foor_arm_handler) and counts
// down a normalised charge in the entity float at +0x1cc (seeded from +0x1d8);
// when it drains it fires the 0x16/0x12 explosion cluster and character death
// init (ekun_foor_countdown_tick / ekun_nuke_blast_tick). FLOAT_80612748 caches
// the fill ratio across a save/restore. The SWWL dial spins a bone rig at a
// speed ramped by ekun_swwl_dial_speed_tick.
//
// Non-matching: FP spill frames (__save_gpr_25/28, FUN_803826d4/80382720,
// FUN_803826f0/8038273c, FUN_8027ea98 event-context open) are opaque; the
// int-encoded frame reciprocal (0x43300000 fp-magic bias) is carried as plain
// math; several helpers route through file-local typed __we627_* aliases.
// ===========================================================================

extern "C" {

// --- raw data symbols for batch_627 (file-local) ---
extern int    DAT_80610df4;        // last-picked rattle sound variant (0..3)
extern int    DAT_80611d74;        // active view/context block base
extern int    DAT_80612728;        // FOOR nuke "arming" flag
extern int    DAT_8061272c;        // FOOR nuke "blow" flag
// DAT_80612730 (debris pool budget / here reused as rattle countdown) is already
// declared `extern int` earlier in this file (batch_621).
extern int    DAT_806108e4;        // per-frame dt (int frames)
extern char   DAT_805015bf;        // difficulty index (0/1/2)
extern float  FLOAT_80612748;      // cached FOOR fill ratio (save/restore)

extern float  FLOAT_806176d8;      // 2.0f (curve stage width) / countdown floor
extern float  FLOAT_806176e0;      // 3.0f (curve stage width)
extern float  FLOAT_806176fc;      // 1.0f curve stage base
extern float  FLOAT_80617700;      // dial frame->half-cycle scale
extern float  FLOAT_80617758;      // rattle spawn probability gate
extern float  FLOAT_806177ac;      // rattle min sep-sq (spark)
extern float  FLOAT_806177b0;      // rattle louder sep-sq
extern float  FLOAT_806177b4;      // dial slide curve coeff
extern float  FLOAT_806177bc;      // rattle re-arm interval scale
extern float  FLOAT_806177c0;      // FOOR drain rate (easy)
extern float  FLOAT_806177c4;      // FOOR drain rate (hard)
extern float  FLOAT_806177c8;      // FOOR drain rate (normal)
extern float  FLOAT_806177d0;      // dial slide full-open offset
extern float  FLOAT_806177d8;      // SWWL dial target speed (switch on)
extern float  FLOAT_806177dc;      // SWWL dial decay (above target)
extern float  FLOAT_806177e0;      // SWWL dial decay (below target)
extern float  FLOAT_806177e4;      // SWWL dial min speed clamp

// nuke/explosion offset + dir vectors (rodata blocks)
extern int DAT_803df928; extern int DAT_803df92c; extern int DAT_803df930;
extern int DAT_803df934; extern int DAT_803df938; extern int DAT_803df93c;
extern int DAT_803df940; extern int DAT_803df944; extern int DAT_803df948;
extern int DAT_803df94c; extern int DAT_803df950; extern int DAT_803df954;
extern int DAT_803df958; extern int DAT_803df95c; extern int DAT_803df960;
extern int DAT_803df964; extern int DAT_803df968; extern int DAT_803df96c;
extern int DAT_803df970; extern int DAT_803df974; extern int DAT_803df978;
extern int DAT_803df97c; extern int DAT_803df980; extern int DAT_803df984;
extern int DAT_803df988; extern int DAT_803df98c; extern int DAT_803df990;
extern int DAT_803df994; extern int DAT_803df998; extern int DAT_803df99c;
extern int DAT_803df9a0; extern int DAT_803df9a4; extern int DAT_803df9a8;
extern int DAT_803df9ac; extern int DAT_803df9b0; extern int DAT_803df9b4;

// raw code symbols not in generated headers / with FP-spill ABI
extern unsigned long long FUN_803826d4(void);   // FP spill open (b)
extern void  FUN_80382720(void);                 // FP spill close (b)
extern int   FUN_8027ea98(void);                 // event/context open -> ctx ptr
extern int   __save_gpr_25(void);                // GPR spill frame (returns entity)
extern void  __restore_gpr_25(void);
extern int   __save_gpr_28(void);                // GPR spill frame (returns entity)
extern void  __restore_gpr_28(void);

} // extern "C"

// --- typed fn-ptr aliases for thin-return / reduced-arity generated_decls ---

// screen_effect_alloc_slot ROM returns the new slot index.
static int (*const __we627_screen_effect_alloc_slot)(void) =
    (int (*)(void))&screen_effect_alloc_slot;

// mtx4_pool_alloc ROM returns the matrix address.
static int (*const __we627_mtx4_pool_alloc)(void) =
    (int (*)(void))&mtx4_pool_alloc;

// game_frand ROM returns a double in [0,1).
static double (*const __we627_game_frand)(void) =
    (double (*)(void))&game_frand;

// game_time_to_frames ROM returns a double.
static double (*const __we627_game_time_to_frames)(void) =
    (double (*)(void))&game_time_to_frames;

// subobj_pool_alloc ROM returns the new body handle (declared void).
static unsigned int (*const __we627_subobj_pool_alloc)(void) =
    (unsigned int (*)(void))&subobj_pool_alloc;

// subobj_build_descriptor_default ROM full ABI (fpctx + 7 doubles + tail).
static void (*const __we627_subobj_build_descriptor_default)(double, double, double,
        double, double, double, double, unsigned int, int, int, int, float) =
    (void (*)(double, double, double, double, double, double, double,
        unsigned int, int, int, int, float))&subobj_build_descriptor_default;

// mtx4_set_translation ROM form (3 doubles + implicit current-matrix dst).
static void (*const __we627_mtx4_set_translation3)(double, double, double) =
    (void (*)(double, double, double))&mtx4_set_translation;

// character_on_death_init ROM full ABI (mode + 7 fp regs + entity).
static void (*const __we627_character_on_death_init)(double, double, double, double,
        double, double, double, double, int) =
    (void (*)(double, double, double, double, double, double, double, double,
        int))&character_on_death_init;

// player_ai_explosion_spawn ROM returns a handle (declared void).
static unsigned long long (*const __we627_player_ai_explosion_spawn)(int, int, float *,
        float *, int, int, int) =
    (unsigned long long (*)(int, int, float *, float *, int, int, int))
    &player_ai_explosion_spawn;

// mtx4_rotate_about_point declared (float*, double); ROM form is (out, axisVec, pivot).
static void (*const __we627_mtx4_rotate_about_point)(float *, float *, float *) =
    (void (*)(float *, float *, float *))&mtx4_rotate_about_point;

// subobj_apply_impulse_at_local_point declared (u32,u32,float*); ROM form is (handle, pointPtr, impulsePtr).
static void (*const __we627_subobj_apply_impulse)(unsigned int, float *, float *) =
    (void (*)(unsigned int, float *, float *))&subobj_apply_impulse_at_local_point;


// ---------------------------------------------------------------------------
// 0x801eef10  ekun_debris_rattle_tick
// Ambient rattle for the FOOR debris/tank set-piece. Ticks the countdown timer
// (DAT_80612730) down by dt; when the timer elapses (net-authoritative, on a
// game_rand phase), computes a jittered sub-object separation vector and, if it
// exceeds the rattle threshold, plays one of 4 creak sounds (0x603+rand&3) at
// the entity and re-arms the timer, then spawns a fading spark. NON-MATCHING:
// PPC paired-single spill temps for the 6 game_frand-jittered corners are
// carried as scalar math; the int<->double fp-magic bias is plain arithmetic.
// ---------------------------------------------------------------------------
void ekun_debris_rattle_tick(void)
{
    int e = __save_gpr_28();
    DAT_80612730 = DAT_80612730 - (unsigned int)DAT_806108e4;
    unsigned int r;
    if ((DAT_806108e4 != 0) && (FUN_80220f30(1) == 0) &&
        (r = (unsigned int)game_rand(),
         r == (r / (unsigned int)((int)DAT_806109c0 / 10)) * ((int)DAT_806109c0 / 10))) {

        // Two jittered samples of the sub-object's world offset (+0x1a8..+0x1b0
        // added to base pos +100/+0x68/+0x6c), scaled by the +0x1c0..+0x1c8
        // extents with a random sign; the separation is their difference.
        auto jittered = [&](int extentOff, int baseOff, int subOff) -> float {
            double a = __we627_game_frand();
            double sgnbias = (double)FLOAT_806176e8;
            double mag = __we627_game_frand();
            float ext = *(float *)(e + extentOff);
            float base = *(float *)(e + baseOff);
            float sub = *(float *)(e + subOff);
            unsigned int sign = (unsigned int)(float)(a - sgnbias) & 0x80000000u;
            float signed1 = *(float *)&(sign |= 0x3f800000u);
            return sub + signed1 * (float)((double)ext * mag) + base;
        };
        float dx = jittered(0x1c4, 0x68, 0x1ac) - jittered(0x1c4, 0x68, 0x1ac);
        float dy = jittered(0x1c0, 100, 0x1a8) - jittered(0x1c0, 100, 0x1a8);
        float dz = jittered(0x1c8, 0x6c, 0x1b0) - jittered(0x1c8, 0x6c, 0x1b0);
        float sep2 = dz * dz + dy * dy + dx * dx;

        if (FLOAT_806177ac < sep2) {
            if (FLOAT_806177b0 < sep2) {
                __we627_game_frand();
            }
            float timerF = (float)((double)CONCAT44(0x43300000, DAT_80612730 ^ 0x80000000) -
                                   DOUBLE_80617728);
            if ((timerF < FLOAT_806176d0) && (__we627_game_frand() > (double)FLOAT_80617758)) {
                unsigned int v = 0;
                for (int i = 0; (v == (unsigned int)DAT_80610df4) && (i < 10); ++i) {
                    v = (unsigned int)game_rand() & 3;
                }
                DAT_80610df4 = (int)v;
                unsigned char play[0x40];
                sound_init_play_params((int)(long long)play);
                *(int *)(play + 0x00) = (int)v + 0x603;
                play[0x28] = 1;
                *(int *)(play + 0x1c) = *(int *)(e + 100);
                *(int *)(play + 0x20) = *(int *)(e + 0x68);
                *(int *)(play + 0x24) = *(int *)(e + 0x6c);
                sound_play_request((int *)play);

                double rr = __we627_game_frand();
                float seed = (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                                     DOUBLE_80617728);
                DAT_80612730 = (unsigned int)(seed *
                    (float)((double)FLOAT_806177bc * rr + (double)FLOAT_806176dc));
            }
            __we627_game_frand();
            spark_spawn_fading();
        }
    }
    __restore_gpr_28();
}


// ---------------------------------------------------------------------------
// 0x801ef314  ekun_plate_slide_rig_tick
// Plate slide bone rig. The plate keeps a small state machine in the node word
// at +0x7c[0] (2 = sliding open, 1/other = sliding closed) and its slide offset
// in node[3]. Advances the offset by the per-frame dt, clamps at the full
// travel (FLOAT_806177a8) or zero, then writes the -offset translation into the
// two child bone matrices.
// ---------------------------------------------------------------------------
void ekun_plate_slide_rig_tick(int self)
{
    float full = FLOAT_806177a8;
    float zero = FLOAT_806176d0;
    int *node = *(int **)(self + 0x7c);
    int state = node[0];
    float dt = FLOAT_806176ec *
        (FLOAT_806176dc / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                                  DOUBLE_80617728));
    if (state == 2) {
        *(float *)&node[3] = *(float *)&node[3] + dt;
        if (full < *(float *)&node[3]) {
            *(float *)&node[3] = full;
            node[0] = 3;
        }
    } else if ((state < 2) && (state != 0) && (-1 < state)) {
        *(float *)&node[3] = -( dt - *(float *)&node[3] );
        if (*(float *)&node[3] < zero) {
            *(float *)&node[3] = zero;
            node[0] = 0;
        }
    }
    float mtx[16];
    __we627_mtx4_set_translation3((double)FLOAT_806176d0, (double)FLOAT_806176d0,
                                  -(double)*(float *)&node[3]);
    int base = *(int *)(*(int *)(self + 4) + 0x10);
    mtx4_copy(mtx, (float *)(base + 0x40));
    mtx4_copy(mtx, (float *)(base + 0x80));
}


// ---------------------------------------------------------------------------
// 0x801ef4b4  ekun_foor_arm_handler
// 'FOOR' nuke arm callback (installed on the trigger). Records the armed entity
// into the active view block (+0xe8c). If the cached fill ratio FLOAT_80612748
// is exactly the default (FLOAT_806176d4, i.e. fresh), allocate a screen-effect
// slot and set the "arming" flag; otherwise restore the fill from the cached
// ratio and set the "blow" flag.
// ---------------------------------------------------------------------------
void ekun_foor_arm_handler(void)
{
    int *ctx = (int *)FUN_8027ea98();
    float def = FLOAT_806176d4;
    if (ctx != 0) {
        int e = *ctx;
        *(int *)(DAT_80611d74 + 0xe8c) = e;
        if (def == FLOAT_80612748) {
            int slot = __we627_screen_effect_alloc_slot();
            *(int *)(e + 0x268) = slot;
            screen_effect_init_slot((int *)(long long)*(int *)(e + 0x268), (int *)(long long)e);
            DAT_80612728 = 1;
        } else {
            *(float *)(e + 0x1cc) = *(float *)(e + 0x1d8) * FLOAT_80612748;
            DAT_8061272c = 1;
        }
    }
}


// ---------------------------------------------------------------------------
// 0x801ef534  ekun_foor_reset
// 'FOOR' nuke reset callback. Clears the entity's screen-effect slot (+0x268)
// and the active view's armed-entity slot (+0xe8c), caches the current fill
// ratio (+0x1cc / +0x1d8) into FLOAT_80612748, and clears both arm/blow flags.
// ---------------------------------------------------------------------------
void ekun_foor_reset(void)
{
    int *ctx = (int *)FUN_8027ea98();
    if (ctx != 0) {
        int e = *ctx;
        *(int *)(e + 0x268) = 0;
        *(int *)(DAT_80611d74 + 0xe8c) = 0;
        FLOAT_80612748 = *(float *)(e + 0x1cc) / *(float *)(e + 0x1d8);
        DAT_80612728 = 0;
        DAT_8061272c = 0;
    }
}


// ---------------------------------------------------------------------------
// 0x801ef588  ekun_foor_countdown_tick
// Per-frame FOOR nuke countdown. Runs while the arm flag (DAT_80612728) is set
// and the 'FOOR' entity exists and is in state 0: drains the charge float
// (+0x1cc) at a difficulty-selected rate (FLOAT_806177c0/c4/c8 by DAT_805015bf);
// when it crosses zero it sets FOOR state 1, clears the arm flag and calls
// character_on_death_init. The blow path (DAT_8061272c) drains the same charge
// and on zero fires a 3-blast explosion cluster (id 0x16, subtype 0x17) at the
// rodata offsets and calls character_on_death_init.
// ---------------------------------------------------------------------------
void ekun_foor_countdown_tick(void)
{
    FUN_803826d4();
    int foor;
    if ((DAT_80612728 != 0) && ((foor = FUN_8023c32c(0x524f4f46)) != 0) /* 'FOOR' */ &&
        (*(int *)(foor + 0x1a0) == 0)) {
        int *ctx = (int *)FUN_8027ea98();
        int e = *ctx;
        double rate = (DAT_805015bf == '\0') ? (double)FLOAT_806177c0
                    : (DAT_805015bf == '\x02') ? (double)FLOAT_806177c4
                    : (double)FLOAT_806177c8;
        double dtRecip = (double)(FLOAT_806176dc /
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728));
        double drain = (double)(float)((double)(float)((double)CONCAT44(0x43300000,
            DAT_806108e4 ^ 0x80000000) - DOUBLE_80617728) * rate);
        *(float *)(e + 0x1cc) = -(float)(dtRecip * drain - (double)*(float *)(e + 0x1cc));
        float charge = *(float *)(e + 0x1cc);
        if ((double)charge < (double)FLOAT_806176d0) {
            *(int *)(foor + 0x1a0) = 1;
            DAT_80612728 = 0;
            __we627_character_on_death_init((double)charge, drain, dtRecip, DOUBLE_80617728,
                                            rate, 0, 0, 0, e);
        }
    }

    if (DAT_8061272c != 0) {
        int *ctx = (int *)FUN_8027ea98();
        int e = *ctx;
        double rate = (DAT_805015bf == '\0') ? (double)FLOAT_806177c0
                    : (DAT_805015bf == '\x02') ? (double)FLOAT_806177c4
                    : (double)FLOAT_806177c8;
        double dtRecip = (double)(FLOAT_806176dc /
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728));
        double drain = (double)(float)((double)(float)((double)CONCAT44(0x43300000,
            DAT_806108e4 ^ 0x80000000) - DOUBLE_80617728) * rate);
        *(float *)(e + 0x1cc) = -(float)(dtRecip * drain - (double)*(float *)(e + 0x1cc));
        if (*(float *)(e + 0x1cc) < FLOAT_806176d0) {
            float posA[3]; float dir[3]; float posB[3]; float posC[3];
            *(int *)&posA[0] = DAT_803df928; *(int *)&posA[1] = DAT_803df92c; *(int *)&posA[2] = DAT_803df930;
            *(int *)&dir[0]  = DAT_803df934; *(int *)&dir[1]  = DAT_803df938; *(int *)&dir[2]  = DAT_803df93c;
            *(int *)&posB[0] = DAT_803df940; *(int *)&posB[1] = DAT_803df944; *(int *)&posB[2] = DAT_803df948;
            *(int *)&posC[0] = DAT_803df94c; *(int *)&posC[1] = DAT_803df950; *(int *)&posC[2] = DAT_803df954;
            player_ai_explosion_spawn(0x16, 0x17, posA, dir, 0, 0, 1);
            player_ai_explosion_spawn(0x16, 0x17, posB, dir, 0, 0, 1);
            unsigned long long h = __we627_player_ai_explosion_spawn(0x16, 0x17, posC, dir, 0, 0, 1);
            DAT_8061272c = 0;
            __we627_character_on_death_init((double)(longlong)h, drain, dtRecip, DOUBLE_80617728,
                                            rate, 0, 0, 0, e);
        }
    }
    FUN_80382720();
}


// ---------------------------------------------------------------------------
// 0x801ef810  ekun_nuke_blast_tick
// FOOR nuke blast physics tick (installed while state != 0). Reads a fixed
// impulse point/dir from the rodata block. While the blast timer (+0x1a4) is
// above the threshold it applies an upward drift + a sub-object impulse (lazily
// allocating the body), then deregisters; once the timer reaches exactly 0 it
// fires the 4-blast explosion cluster (0x16/0x12 subtype 0x17) at the entity +
// rodata offsets. Advances the timer by dt each frame.
// ---------------------------------------------------------------------------
void ekun_nuke_blast_tick(unsigned long long param_1, double param_2, double param_3,
                          double param_4, double param_5, double param_6, double param_7,
                          double param_8)
{
    (void)param_1; (void)param_2;
    int e = __save_gpr_25();
    if (*(int *)(e + 0x1a0) != 0) {
        float dir[3]; float impA[3]; float impB[3]; float impC[3];
        *(int *)&dir[0]  = DAT_803df958; *(int *)&dir[1]  = DAT_803df95c; *(int *)&dir[2]  = DAT_803df960;
        *(int *)&impA[0] = DAT_803df964; *(int *)&impA[1] = DAT_803df968; *(int *)&impA[2] = DAT_803df96c;
        *(int *)&impB[0] = DAT_803df970; *(int *)&impB[1] = DAT_803df974; *(int *)&impB[2] = DAT_803df978;
        *(int *)&impC[0] = DAT_803df97c; *(int *)&impC[1] = DAT_803df980; *(int *)&impC[2] = DAT_803df984;
        float posC[3];
        *(int *)&posC[0] = DAT_803df988; *(int *)&posC[1] = DAT_803df98c; *(int *)&posC[2] = DAT_803df990;

        double timer = (double)*(float *)(e + 0x1a4);
        if (timer <= (double)FLOAT_806176d8) {
            if ((double)FLOAT_806176d0 == timer) {
                impA[0] = *(float *)&DAT_803df964 + *(float *)(e + 100)  + *(float *)(e + 0x1a8);
                impA[1] = *(float *)&DAT_803df968 + *(float *)(e + 0x68) + *(float *)(e + 0x1ac);
                impA[2] = *(float *)&DAT_803df96c + *(float *)(e + 0x6c) + *(float *)(e + 0x1b0);
                player_ai_explosion_spawn(0x16, 0x17, impA, dir, 0, e, 1);
                impA[2] = impA[2] + FLOAT_806176ec;
                player_ai_explosion_spawn(0x12, 0x17, impA, dir, 0, e, 1);
                impA[2] = impA[2] + FLOAT_806176ec;
                impA[1] = impA[1] - FLOAT_806176ec;
                player_ai_explosion_spawn(0x16, 0x17, impA, dir, 0, e, 1);
                player_ai_explosion_spawn(0x16, 0x17, posC, impC, 0, e, 1);
            }
        } else {
            *(float *)(e + 0x68) = *(float *)(e + 0x68) + FLOAT_80617708;
            if (*(int *)(e + 0x80) == 0) {
                unsigned int body = __we627_subobj_pool_alloc();
                *(unsigned int *)(e + 0x80) = body;
                __we627_subobj_build_descriptor_default(timer, param_3, param_4, param_5, param_6,
                        param_7, param_8, *(unsigned int *)(e + 0x80), e, 3, -1, 0.0f);
                subobj_set_collision_groups(*(unsigned int *)(e + 0x80), 0x10, 0xf1);
            }
            __we627_subobj_apply_impulse(*(unsigned int *)(e + 0x80), impA, impB);
            *(int *)(e + 0x1a0) = 0;
            entity_register_for_update(e, 3, 0);
        }
        *(float *)(e + 0x1a4) = (FLOAT_806176dc /
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728)) *
            FLOAT_806108ec + *(float *)(e + 0x1a4);
    }
    __restore_gpr_25();
}


// ---------------------------------------------------------------------------
// 0x801efaac  ekun_swwl_dial_spin_tick
// 'SWWL' dial spin bone rig. Advances a half-cycle counter from game time to
// flip the model's cull mode (+0x30 = 1/2), lets the B button reset the dial
// angle, and while the dial is active (+0x1a0) accumulates the angle (+0x1a4)
// by dt. A 4-stage piecewise curve maps the angle past 1.0 to a slide offset,
// which is written as a translation then composed with two rotate-about-pivot
// matrices into the child bone. NON-MATCHING: piecewise curve stages derived
// from the ROM's nested float compares; int<->double bias plain math.
// ---------------------------------------------------------------------------
void ekun_swwl_dial_spin_tick(int self)
{
    double t = __we627_game_time_to_frames();
    float pivotA[3]; float pivotB[3];
    *(int *)&pivotB[0] = DAT_803df994; *(int *)&pivotB[1] = DAT_803df998; *(int *)&pivotB[2] = DAT_803df99c;
    *(int *)&pivotA[0] = DAT_803df9a0; *(int *)&pivotA[1] = DAT_803df9a4; *(int *)&pivotA[2] = DAT_803df9a8;
    double slide = (double)FLOAT_806176d0;

    unsigned int half = (unsigned int)(FLOAT_80617700 *
        (float)((double)(FLOAT_806176dc /
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617728)) * t));
    int rig = *(int *)(self + 4);
    int *node = *(int **)(rig + 4);
    float *pivot = (float *)node[*node * -0x30 + 0x1b];
    *(int *)(rig + 0x30) = ((half & 1) == 0) ? 1 : 2;

    if ((input_get_buttons_held(0) & 0x80) != 0) {
        *(float *)(self + 0x1a4) = FLOAT_806176d0;
    }
    if (*(int *)(self + 0x1a0) != 0) {
        *(float *)(self + 0x1a4) = *(float *)(self + 0x1a4) +
            FLOAT_806176dc / (float)((double)CONCAT44(0x43300000, DAT_806109c0) - DOUBLE_80617728);
    }
    if ((FLOAT_806176dc < *(float *)(self + 0x1a4))) {
        float f = *(float *)(self + 0x1a4) - FLOAT_806176dc;
        if (FLOAT_806176fc < f) {
            f = f - FLOAT_806176fc;
            if (FLOAT_806176dc <= f) {
                f = f - FLOAT_806176dc;
                if (FLOAT_806176d8 <= f) {
                    f = f - FLOAT_806176d8;
                    if (FLOAT_806176e0 <= f) {
                        slide = (double)FLOAT_806177d0;
                        *(int *)(self + 0x1a0) = 0;
                    } else {
                        slide = (double)(FLOAT_806177b4 * (FLOAT_806176dc - f / FLOAT_806176e0) +
                                         (FLOAT_806177d0 * f) / FLOAT_806176e0);
                    }
                } else {
                    slide = (double)(FLOAT_806176e0 * (FLOAT_806176dc - f / FLOAT_806176d8) +
                                     (FLOAT_806177b4 * f) / FLOAT_806176d8);
                }
            } else {
                slide = (double)(FLOAT_806176e0 * f);
            }
        }
    }

    float mSlide[16]; float mB[16]; float mA[16];
    __we627_mtx4_set_translation3(slide, (double)FLOAT_806176d0, (double)FLOAT_806176d0);
    __we627_mtx4_rotate_about_point(mB, pivotA, pivot);
    mtx4_multiply(mB, mSlide, mSlide);
    __we627_mtx4_rotate_about_point(mA, pivotB, pivot);
    mtx4_multiply(mSlide, mA, (float *)(*(int *)(*(int *)(self + 4) + 0x10) + 0x40));
}


// ---------------------------------------------------------------------------
// 0x801efd34  ekun_swwl_dial_speed_tick
// 'SWWL' dial speed ramp. If the SWWL switch slot is active (+0x10 nonzero) the
// dial target speed (+0xd0) is set to FLOAT_806177d8, else 0. While the target
// is nonzero, per dt frame the current speed (+0x1a4) decays toward the target
// (multiply by FLOAT_806177e0 when below, FLOAT_806177dc when above), clamped to
// a minimum FLOAT_806177e4.
// ---------------------------------------------------------------------------
void ekun_swwl_dial_speed_tick(int self)
{
    unsigned int *slot = (unsigned int *)switch_find_slot_by_entity(0x5357574c);  // 'SWWL'
    if (slot != 0) {
        float target = FLOAT_806176d0;
        if (*(float *)&slot[0x10] != FLOAT_806176d0) {
            target = FLOAT_806177d8;
        }
        *(float *)(self + 0xd0) = target;
    }
    float below = FLOAT_806177e0;
    float above = FLOAT_806177dc;
    if (FLOAT_806176d0 != *(float *)(self + 0xd0)) {
        for (int i = 0; i < DAT_806108e4; ++i) {
            float cur = *(float *)(self + 0x1a4);
            if (cur <= *(float *)(self + 0xd0)) {
                *(float *)(self + 0x1a4) = cur * below;
            } else {
                *(float *)(self + 0x1a4) = cur * above;
            }
        }
    }
    if (*(float *)(self + 0x1a4) < FLOAT_806177e4) {
        *(float *)(self + 0x1a4) = FLOAT_806177e4;
    }
}


// ---------------------------------------------------------------------------
// 0x801efdf0  ekun_jitter_bone_rig_update
// Random-jitter bone rig: identity the base bone, draw a random angle
// (game_frand) and rotate the child bone about the model pivot by the rodata
// axis DAT_803df9ac, then mark the pose dirty (0x200000).
// ---------------------------------------------------------------------------
void ekun_jitter_bone_rig_update(int self)
{
    float axis[3];
    *(int *)&axis[0] = DAT_803df9ac; *(int *)&axis[1] = DAT_803df9b0; *(int *)&axis[2] = DAT_803df9b4;
    int *node = *(int **)(*(int *)(self + 4) + 4);
    float *pivot = (float *)node[*node * -0x30 + 0x1b];
    mtx4_set_identity(*(int *)(*(int *)(self + 4) + 0x10));
    __we627_game_frand();
    __we627_mtx4_rotate_about_point((float *)(*(int *)(*(int *)(self + 4) + 0x10) + 0x40), axis, pivot);
    *(unsigned int *)(self + 0x28) |= 0x200000;
}


// ---------------------------------------------------------------------------
// 0x801efeb8  ekun_install_face_matrix
// Trigger callback: allocate a pooled 4x4, identity it, and install it as the
// collision "face entry" matrix of the running context's linked object (the ctx
// packs (obj<<32 | faceIndex)).
// ---------------------------------------------------------------------------
void ekun_install_face_matrix(void)
{
    unsigned long long ctx = FUN_803826f0();
    int mtx = __we627_mtx4_pool_alloc();
    mtx4_set_identity(mtx);
    collision_set_face_entry(*(int *)((int)(ctx >> 0x20) + 4), (int)(unsigned int)ctx, mtx);
    FUN_8038273c();
}


// ===========================================================================
// batch_628  0x801eff08 - 0x801f0904
// EKUN 'SHEL' shield/blast-door anim-event glue + resource preload + level init
// part C. The SHEL entity is a shielded blast door: a per-frame shield tick
// opens/closes on an anim-slot userflag with the shared SHEL timer entity
// (class 'SHEL', +0x1a0 timer), spawning break-off fragments (class 0x460) that
// fade + explode. The big anim-event dispatcher (ekun_object_init_dispatch_b)
// installs the per-anim-tag panel/hinge/smoke render+think callbacks authored in
// batches 625-627. ekun_level_init_c registers the two level actor banks, flags
// ~35 soundpad zones interior/exterior, and builds the smoke particle def.
//
// Non-matching: 4CC ids ('SHEL','DAV1'); the anim-event dispatcher's dense
// ushort switch is carried verbatim; entity_register_for_update flag args are
// the ROM's sign-extended callback addresses (-0x7fe1xxxx == 0x801eexxx);
// entity_set_render_type / entity_register_for_update take the callback as an
// int address; several helpers route through file-local __we628_* aliases.
// ===========================================================================

extern "C" {

// --- raw data symbols for batch_628 (file-local) ---
extern int    DAT_80612764;        // SHEL misc latch
extern int    DAT_80612768;        // SHEL actor-bank init latch
extern float *DAT_80612754;        // SHEL particle/def scratch block (mem_alloc'd)
extern float  FLOAT_80612750;      // SHEL def fill value cache
extern int    DAT_80454738;        // level actor bank 0 descriptor (batch C)
extern int    DAT_804547d4;        // level actor bank 1 descriptor (batch C)

extern int    DAT_803df9b8; extern int DAT_803df9bc; extern int DAT_803df9c0; // fragment blast dir

extern float  FLOAT_80617734;      // (shared, spin pitch) reused as anim scale
extern float  FLOAT_8061778c;      // shared bone step / initial fill
extern float  FLOAT_80617788;      // SHEL loop sound pitch
extern float  FLOAT_806177e8;      // fragment spawn scalar
extern float  FLOAT_806177ec;      // SHEL open-y set value
extern float  FLOAT_806177f0;      // hinge material scale coeff
extern float  FLOAT_806177f4;      // smoke fade rate
extern float  FLOAT_806177f8;      // 0.0f (def seed) / particle base
extern float  FLOAT_806177fc; extern float FLOAT_80617800; extern float FLOAT_80617804; // def color row
extern float  FLOAT_80617808; extern float FLOAT_8061780c; // particle def create args
extern float  FLOAT_80617810; extern float FLOAT_80617814; extern float FLOAT_80617818;
extern float  FLOAT_8061781c; extern float FLOAT_80617820; extern float FLOAT_80617824;
extern float  FLOAT_80617828; extern float FLOAT_8061782c; extern float FLOAT_80617830;
extern float  FLOAT_80617834; extern float FLOAT_80617838; extern float FLOAT_8061783c;
extern float  FLOAT_80617840;

// raw code symbols not in generated headers / with FP-spill ABI
extern int    FUN_80232760(int tag);         // anim-event tag test (returns bool)
extern void   subobj_break_tick_impl(double a, double b, double c, double d,
                                     double e, double f, double g, unsigned long long h);

} // extern "C"

// entity_model_cache_alloc / weapon_smoke_tick / weapon_impact_smoke_dispatch /
// proj_ai_release_sound_banks / proj_register_smoke_def resolve via
// generated_decls (entity.h). The batch 625-627 callbacks referenced by the
// anim dispatcher (ekun_sdbu_blast_trigger, ekun_panel_retract_tick,
// ekun_spawn_hinge_panel, ekun_dgbt_switch_trigger) are defined earlier in this
// file. DAT_801ef4b0 is the ROM's data-labelled thunk for ekun_foor_arm_handler
// (0x801ef4b4 body); we install ekun_foor_arm_handler by address.

// --- typed fn-ptr aliases for thin-return / reduced-arity generated_decls ---

// proj_ai_release_sound_banks ROM returns the running fp/alloc context.
static unsigned long long (*const __we628_proj_ai_release_sound_banks)(void) =
    (unsigned long long (*)(void))&proj_ai_release_sound_banks;

// subobj_spawn_break_piece ROM full ABI (fpctx + spill + (rigPtr,a,classId,b,c,d)) -> entity.
static int (*const __we628_subobj_spawn_break_piece)(double, double, double, double,
        unsigned long long, unsigned long long, unsigned long long, unsigned long long,
        int, int, int, int, int, float) =
    (int (*)(double, double, double, double, unsigned long long, unsigned long long,
        unsigned long long, unsigned long long, int, int, int, int, int, float))
    &subobj_spawn_break_piece;

// entity_model_cache_alloc ROM returns the cache handle (full ABI tail).
static int (*const __we628_entity_model_cache_alloc)(unsigned long long, double, double,
        double, double, double, double, unsigned long long, int, int, int, int, int, int, int, int) =
    (int (*)(unsigned long long, double, double, double, double, double, double,
        unsigned long long, int, int, int, int, int, int, int, int))&entity_model_cache_alloc;

// entity_preload_resources ROM threads the running context (returns it).
static unsigned long long (*const __we628_entity_preload_resources)(unsigned long long,
        double, double, double, double, double, double, double, int) =
    (unsigned long long (*)(unsigned long long, double, double, double, double, double,
        double, double, int))&entity_preload_resources;

// mem_alloc ROM full ABI (ctx + spill + size + flags) -> block ptr.
static float *(*const __we628_mem_alloc)(unsigned long long, double, double, double, double,
        double, double, double, int, int, int, int, int, int, int, int) =
    (float *(*)(unsigned long long, double, double, double, double, double, double, double,
        int, int, int, int, int, int, int, int))&mem_alloc;

// level_actors_register / level_resource_pool_setup share the aliases declared
// for batch_621 (__we3_level_actors_register / __we3_level_resource_pool_setup).

// sound_update_3d_source thin (declared void) -> reuse batch_627-style typed ptr.
static int (*const __we628_sound_update_3d_source)(double, double, double, int, unsigned int *,
        float *, unsigned int) =
    (int (*)(double, double, double, int, unsigned int *, float *, unsigned int))
    &sound_update_3d_source;

// anim_active_slot_userflags ROM returns the flag word (declared void in some decls).
static int (*const __we628_anim_active_slot_userflags)(int) =
    (int (*)(int))&anim_active_slot_userflags;

// particle_def_create declared (u64,float,u32,...); ROM form is (2 fp regs, defPtr, 5 ints).
static void (*const __we628_particle_def_create)(double, double, float *, int, int, int, int, int) =
    (void (*)(double, double, float *, int, int, int, int, int))&particle_def_create;

// particle_emitter_create declared (u64,double,double,u32,...); ROM form is (3 fp regs, emitterPtr, defPtr, 2 ints).
static void (*const __we628_particle_emitter_create)(double, double, double, float *, float *, int, int) =
    (void (*)(double, double, double, float *, float *, int, int))&particle_emitter_create;


// ---------------------------------------------------------------------------
// 0x801eff08  ekun_shel_install_faces
// Install the SHEL blast-door's three collision face-entry matrices (each a
// pooled identity 4x4 via ekun_install_face_matrix).
// ---------------------------------------------------------------------------
void ekun_shel_install_faces(void)
{
    ekun_install_face_matrix();
    ekun_install_face_matrix();
    ekun_install_face_matrix();
}


// ---------------------------------------------------------------------------
// 0x801eff50  ekun_shel_fragment_tick
// Per-frame tick for a spawned SHEL break-off fragment. The fragment record is
// self[0] (its render instance is puVar2). Counts down the fragment lifetime
// (puVar2[0x73]) by dt (self[8]); while alive with a live sub-object body
// (puVar2[0x20]) it runs subobj_break_tick_impl, otherwise it spawns a small
// explosion (id 4) at the fragment position and destroys it.
// ---------------------------------------------------------------------------
void ekun_shel_fragment_tick(unsigned long long param_1, unsigned long long param_2, double param_3,
                             double param_4, double param_5, double param_6, double param_7,
                             unsigned long long param_8, int *self)
{
    (void)param_1; (void)param_2;
    double zero = (double)FLOAT_806176d0;
    int *inst = (int *)self[0];
    float life = *(float *)&inst[0x73];
    *(float *)&inst[0x73] = (float)((double)life - (double)(float)self[8]);
    if (zero < (double)*(float *)&inst[0x73]) {
        if (inst[0x20] != 0) {
            subobj_break_tick_impl(zero, (double)life, param_3, param_4, param_5, param_6, param_7,
                                   param_8);
        }
    } else {
        float dir[3];
        *(int *)&dir[0] = DAT_803df9b8; *(int *)&dir[1] = DAT_803df9bc; *(int *)&dir[2] = DAT_803df9c0;
        player_ai_explosion_spawn(4, 0x17, (float *)(inst + 0x19), dir, 0, (int)(long long)inst, 1);
        entity_destroy_with_event(inst);
    }
}


// ---------------------------------------------------------------------------
// 0x801efffc  ekun_shel_advance_timer
// Advance the shared 'SHEL' timer entity's countdown (+0x1a0) by 2*dt, clamped
// to the frame-count ceiling (DAT_806109c0), and set its sound-active bit 0x40.
// ---------------------------------------------------------------------------
void ekun_shel_advance_timer(void)
{
    int shel = FUN_8023c32c(0x5348454c);   // 'SHEL'
    int v = *(int *)(shel + 0x1a0) + DAT_806108e4 * 2;
    int ceil = DAT_806109c0;
    if (v < DAT_806109c0) {
        ceil = v;
    }
    *(int *)(shel + 0x1a0) = ceil;
    *(unsigned int *)(shel + 0x18) |= 0x40;
}


// ---------------------------------------------------------------------------
// 0x801f0054  ekun_shel_spawn_fragment
// When the SHEL entity's pose is dirty (flag 0x200000), spawn a break-off
// fragment (class 0x460) from its rig, detach it from the dirty list, seed its
// lifetime (FLOAT_80617700) and flags, and install ekun_shel_fragment_tick.
// ---------------------------------------------------------------------------
void ekun_shel_spawn_fragment(int self)
{
    if ((*(unsigned int *)(self + 0x28) & 0x200000) != 0) {
        double zero = (double)FLOAT_806176d0;
        double burst = (double)FLOAT_806177e8;
        *(float *)(self + 0x68) = *(float *)(self + 0x68) + FLOAT_80617740;
        int frag = __we628_subobj_spawn_break_piece(zero, burst, zero, zero, 0, 0, 0, 0,
                        *(int *)(self + 4), 1, 0x460, -1, 3, 0.0f);
        if (frag != 0) {
            subobj_dirty_list_remove(*(unsigned int *)(frag + 0x80));
            float life = FLOAT_80617700;
            *(float *)(self + 0x68) = *(float *)(self + 0x68) - FLOAT_80617740;
            *(unsigned int *)(frag + 0x9c) |= 0x30;
            *(unsigned int *)(frag + 0x28) |= 0x10;
            *(float *)(frag + 0x1d8) = life;
            *(float *)(frag + 0x1cc) = life;
            entity_set_render_type(frag, (int)(long long)&ekun_shel_fragment_tick);
            entity_register_for_update(frag, 3, (int)0x801efffc);   // ekun_shel_advance_timer
        }
    }
}


// ---------------------------------------------------------------------------
// 0x801f0138  ekun_shel_close_gate
// SHEL close gate: if this entity is in state 1 and the shared SHEL timer has
// run out (+0x1a0 == 0), advance to state 2 and play the close anim.
// ---------------------------------------------------------------------------
void ekun_shel_close_gate(int self)
{
    int shel = FUN_8023c32c(0x5348454c);   // 'SHEL'
    if ((*(int *)(self + 0x1a0) == 1) && (*(int *)(shel + 0x1a0) == 0)) {
        *(int *)(self + 0x1a0) = 2;
        anim_play_scaled((double)FLOAT_806176d0, (double)FLOAT_806176dc);
    }
}


// ---------------------------------------------------------------------------
// 0x801f01a0  ekun_shel_shield_tick
// Per-frame SHEL shield open/close driver. Decrements the shared SHEL timer
// (+0x1a0, clamped >= 0). State machine on this entity's +0x1a0: on an
// anim-slot userflag it opens (spawns a fragment via ekun_shel_spawn_fragment,
// plays anims, state 0->1) or closes (state 1..2 when SHEL timer 0: play close
// anim + one-shot sound 0x613). Always drives the 0x60f loop 3D sound.
// ---------------------------------------------------------------------------
void ekun_shel_shield_tick(int self)
{
    int shel = FUN_8023c32c(0x5348454c);   // 'SHEL'
    unsigned int t = (unsigned int)(*(int *)(shel + 0x1a0) - DAT_806108e4);
    *(unsigned int *)(shel + 0x1a0) = t & ~((int)t >> 0x1f);   // clamp >= 0

    int state = *(int *)(self + 0x1a0);
    if (state != 1) {
        if (state < 1) {
            if ((-1 < state) && (__we628_anim_active_slot_userflags(*(int *)(self + 4)) != 0)) {
                ekun_shel_spawn_fragment(shel);
                anim_play_scaled((double)FLOAT_806176d0, (double)FLOAT_806176d0);
                model_render_with_override_b();
                *(int *)(self + 0x1a0) = 1;
                anim_play_scaled((double)FLOAT_806176d0, (double)FLOAT_806176dc);
            }
        } else if ((state < 3) && (__we628_anim_active_slot_userflags(*(int *)(self + 4)) != 0) &&
                   (*(int *)(shel + 0x1a0) == 0)) {
            *(int *)(self + 0x1a0) = 0;
            anim_play_scaled((double)FLOAT_806176d0, (double)FLOAT_806176dc);
            anim_play_scaled((double)FLOAT_806176d0, (double)FLOAT_806176dc);
            model_render_with_override_b();
            float *pos = entity_get_world_position(self);
            unsigned char play[0x40];
            sound_init_play_params((int)(long long)play);
            play[0x28] = 1;
            *(int *)(play + 0x00) = 0x613;
            *(int *)(play + 0x1c) = *(int *)&pos[0];
            *(int *)(play + 0x20) = *(int *)&pos[1];
            *(int *)(play + 0x24) = *(int *)&pos[2];
            sound_play_request((int *)play);
        }
    }
    float *pos = entity_get_world_position(self);
    __we628_sound_update_3d_source((double)FLOAT_806176dc, (double)FLOAT_80617788,
        (double)FLOAT_806176dc, 0x60f, (unsigned int *)(self + 0x22c), pos, 0);
}


// ---------------------------------------------------------------------------
// 0x801f033c  ekun_object_init_dispatch_b
// The EKUN per-anim-tag object-init dispatcher (part B). Keyed on the entity's
// anim tag (+0x94 ushort), it installs the render + update callbacks and seeds
// the state for each EKUN set-piece class authored in batches 625-627: retract
// panels (0x452), spring/hinge panels (0x459/0x45b-0x45e/0x463/0x464), SDBU
// blast doors (0x436-0x437), FOOR nuke (0x469/0x46a/0x46f), SWWL dial (0x474-
// 0x478), smoke/impact glue (0x54a), plus the DAV1-gated DGBT trigger (0x449+).
// NON-MATCHING: dense ushort switch carried verbatim; callback flag args are the
// ROM sign-extended addresses.
// ---------------------------------------------------------------------------
void ekun_object_init_dispatch_b(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int self, int param_10,
                                 int param_11, int param_12, int param_13, int param_14,
                                 int param_15, int param_16)
{
    float step = FLOAT_8061778c;
    float scale = FLOAT_80617734;
    float zero = FLOAT_806176d0;
    unsigned short tag = *(unsigned short *)(self + 0x94);

    if (tag == 0x45f) {
        anim_play_scaled((double)FLOAT_806176d0, (double)FLOAT_806176d0);
        entity_register_for_update(self, 2, (int)0x801edc60);   // ekun_zrot_bone_rig_update? (-0x7ffe2614)
        return;
    }
    if (tag < 0x45f) {
        if (tag == 0x44a) { *(int *)(*(int *)(self + 4) + 0x30) = 0x38; return; }
        if (tag < 0x44a) {
            if (tag == 0x445) return;
            if (0x444 < tag) {
                if (0x448 < tag) {
                    if (FUN_80232760(0x44415631) == 0) return;   // 'DAV1'
                    ekun_dgbt_switch_trigger();
                    return;
                }
                if (tag < 0x447) return;
                subobj_set_collision_groups(*(unsigned int *)(self + 0x80), 0x10, 0xef);
                return;
            }
            if (0x437 < tag) {
                if (tag < 0x444) return;
                *(float *)(self + 0x68) = FLOAT_806177ec;
                *(unsigned int *)(self + 0x28) &= 0xfffffffe;
                return;
            }
            if (tag < 0x436) return;
            *(unsigned int *)(self + 0x28) |= 0x10;
            *(int *)(*(int *)(self + 4) + 0x30) = 1;
            *(float *)(self + 0x1d8) = step;
            *(float *)(self + 0x1cc) = step;
            entity_set_render_type(self, (int)(long long)&ekun_sdbu_blast_trigger);
            return;
        }
        if (tag == 0x45a) return;
        if (tag < 0x45a) {
            if (tag == 0x452) {
                *(float *)(self + 0x1a4) = FLOAT_806176d0;
                float full = FLOAT_806177a8;
                *(int *)(self + 0x1a0) = 0;
                *(unsigned int *)(self + 0x28) |= 0x10;
                *(float *)(self + 0x1d8) = full;
                *(float *)(self + 0x1cc) = full;
                entity_set_render_type(self, (int)(long long)&ekun_panel_retract_tick);
                entity_register_for_update(self, 3, (int)0x801ee3f4);   // ekun_panel_spring_tick
                entity_register_for_update(self, 2, (int)0x801ee574);   // ekun_panel_rig_reset
                return;
            }
            if (tag < 0x452) return;
            if (tag < 0x459) return;
            subobj_set_half_extent_x(*(unsigned int *)(self + 0x80), 0x1fe);
            return;
        }
        if (0x45d < tag) {
            anim_play_scaled((double)FLOAT_806176d0, (double)FLOAT_806176dc);
            *(unsigned int *)(self + 0x28) |= 4;
            *(int *)(self + 0x1a0) = 2;
            entity_register_for_update(self, 0, (int)0x801f0138);   // ekun_shel_close_gate
            entity_register_for_update(self, 3, (int)0x801f01a0);   // ekun_shel_shield_tick
            entity_register_for_update(self, 2, (int)0x801edc60);
            return;
        }
        if (0x45b < tag) {
            entity_register_for_update(self, 4, (int)0x801eef10);   // ekun_debris_rattle_tick
            entity_register_for_update(self, 7, (int)0x801eedac);   // ekun_panel_area_damage
            return;
        }
        return;
    }

    // tag >= 0x45f (handled 0x45f above)
    if (tag == 0x46f) {
        *(int *)(self + 0x1a0) = 0;
        *(float *)(self + 0x1a4) = FLOAT_806176d0;
        entity_register_for_update(self, 3, (int)0x801ef810);   // ekun_nuke_blast_tick
        return;
    }
    if (tag < 0x46f) {
        if (tag == 0x467) {
            entity_register_for_update(self, 3, (int)0x801ede84);
            entity_register_for_update(self, 2, (int)0x801edefc);
            return;
        }
        if (0x466 < tag) {
            if (tag == 0x46a) {
                *(int *)(*(int *)(self + 4) + 0x30) = 1;
                *(int *)(self + 0x1a0) = 0;
                *(float *)(self + 0x1a4) = zero;
                entity_register_for_update(self, 3, (int)0x801efaac);   // ekun_swwl_dial_spin_tick
                return;
            }
            if (0x469 < tag) return;
            if (tag < 0x469) return;
            int mc = __we628_entity_model_cache_alloc(param_1, param_2, param_3, param_4, param_5,
                            param_6, param_7, param_8, self, param_10, 0x10, param_12, param_13,
                            param_14, param_15, param_16);
            *(int *)(self + 0x7c) = mc;
            float matScale = FLOAT_806177f0;
            *(unsigned int *)(self + 0x28) |= 0x74;
            *(float *)(self + 0x25c) = matScale *
                (*(float *)(self + 0x1c8) * *(float *)(self + 0x1c8) +
                 *(float *)(self + 0x1c0) * *(float *)(self + 0x1c0) +
                 *(float *)(self + 0x1c4) * *(float *)(self + 0x1c4));
            *(float *)(self + 0x260) = FLOAT_806176d0;
            render_instance_set_material((double)FLOAT_806176d0, *(int **)(self + 4), 1);
            entity_register_for_update(self, 4, (int)0x801ef314);   // ekun_plate_slide_rig_tick
            entity_register_for_update(self, 0, (int)0x801ef4b4);   // ekun_foor_arm_handler wrapper
            entity_set_render_type(self, (int)0x801ef4b4);           // DAT_801ef4b0 thunk -> foor_arm
            return;
        }
        if (tag == 0x464) { *(unsigned char *)(self + 0x32) = 2; return; }
        if (0x463 < tag) return;
        if (tag < 0x463) return;
        if (*(int *)(self + 0x80) != 0) return;
        if (param_10 == 0) return;
        ekun_spawn_hinge_panel(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                               param_8, self);
        return;
    }
    if (tag == 0x478) {
        *(float *)(self + 0xd0) = FLOAT_806176d0;
        *(float *)(self + 0x1a4) = zero;
        entity_register_for_update(self, 3, (int)0x801efd34);   // ekun_swwl_dial_speed_tick
        entity_register_for_update(self, 2, (int)0x801efdf0);   // ekun_jitter_bone_rig_update
        render_instance_set_material((double)FLOAT_806176d0, *(int **)(self + 4), 1);
        return;
    }
    if (tag < 0x478) {
        if (tag == 0x476) {
            entity_register_for_update(self, 3, (int)0x801ef588);   // ekun_foor_countdown_tick
            return;
        }
        if (0x475 < tag) return;
        if (tag < 0x474) return;
        int rig = *(int *)(self + 0x7c);
        *(unsigned int *)(self + 0x28) |= 0x10;
        *(float *)(self + 0x1d8) = step;
        *(float *)(self + 0x1cc) = step;
        *(float *)(self + 0x1dc) = FLOAT_806177f4;
        entity_set_render_type(self, (int)(long long)&weapon_smoke_tick);
        *(float *)(rig + 0x10) = FLOAT_8061775c;
        *(float *)(rig + 0x9c) = FLOAT_806176d0;
        *(int *)(rig + 0xa0) = 0x14;
        *(int *)(rig + 0xbc) = 0;
        return;
    }
    if (tag != 0x54a) return;
    entity_set_render_type(self, (int)(long long)&weapon_impact_smoke_dispatch);
}


// ---------------------------------------------------------------------------
// 0x801f0820  ekun_preload_resources_c
// EKUN level resource preload part C: preload ~14 object class ids (0x743,
// 0x4a1, 0x4a6-0x4c4 range), clear 4 sound bank refs, then allocate the 0x200-
// byte SHEL particle/def scratch block (DAT_80612754).
// ---------------------------------------------------------------------------
void ekun_preload_resources_c(unsigned long long param_1, double param_2, double param_3,
                              double param_4, double param_5, double param_6, double param_7,
                              double param_8, int param_11, int param_12, int param_13,
                              int param_14, int param_15, int param_16)
{
    static const int ids[] = { 0x743, 0x4a1, 0x4a8, 0x4a7, 0x4ac, 0x4a9, 0x4bd, 0x4c0,
                               0x4bf, 0x4be, 0x4c1, 0x4c3, 0x4c4, 0x4a6 };
    unsigned long long ctx = param_1;
    for (int i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); ++i) {
        ctx = __we628_entity_preload_resources(ctx, param_2, param_3, param_4, param_5, param_6,
                                               param_7, param_8, ids[i]);
    }
    sound_clear_bank_ref(0x4ca);
    sound_clear_bank_ref(0x4e8);
    sound_clear_bank_ref(0x4e9);
    sound_clear_bank_ref(0x4e7);
    unsigned long long actx = __we628_proj_ai_release_sound_banks();
    DAT_80612754 = __we628_mem_alloc(actx, param_2, param_3, param_4, param_5, param_6, param_7,
                        param_8, 0x200, 1, param_11, param_12, param_13, param_14, param_15, param_16);
}


// ---------------------------------------------------------------------------
// 0x801f08e0  ekun_clear_bank_4ca
// Release the 0x4ca sound bank reference (SHEL variant teardown one-shot).
// ---------------------------------------------------------------------------
void ekun_clear_bank_4ca(void)
{
    sound_clear_bank_ref(0x4ca);
}


// ---------------------------------------------------------------------------
// 0x801f0904  ekun_level_init_c
// EKUN level init part C. Zeroes the 0x1f8-byte SHEL def scratch block, seeds
// its default fill fields, registers the two level actor banks (DAT_80454738 /
// DAT_804547d4, count 3 each) + the resource pool, registers the smoke proj def,
// flags ~35 soundpad zones interior (0x10000) / exterior (0x20000) by id, and
// builds the SHEL smoke particle def + emitter.
// ---------------------------------------------------------------------------
void ekun_level_init_c(unsigned long long param_1, double param_2, double param_3, double param_4,
                       double param_5, double param_6, double param_7, double param_8)
{
    // The ROM writes an all-ones NaN sentinel (Ghidra rendered it "-NAN") into
    // the odd slots to mark them "unset"; reproduce it via the raw bit pattern.
    union { unsigned int u; float f; } nanbits; nanbits.u = 0xffffffffu;
    const float SENT = nanbits.f;
    memset_wrapper((int)(long long)DAT_80612754, 0, 0x1f8);
    FLOAT_80612750 = FLOAT_806177f8;
    DAT_80612754[0x22] = SENT; DAT_80612754[0x23] = SENT;
    DAT_80612754[0] = FLOAT_80612750; DAT_80612754[1] = SENT;
    DAT_80612754[2] = FLOAT_80612750; DAT_80612754[3] = SENT;
    DAT_80612754[4] = FLOAT_80612750; DAT_80612754[5] = SENT;
    DAT_80612754[6] = FLOAT_80612750; DAT_80612754[7] = SENT;
    DAT_80612768 = 0;

    unsigned long long ctx = __we3_level_actors_register(param_1, param_2, param_3, param_4, param_5,
                    param_6, param_7, param_8, 0, &DAT_80454738, 3);
    ctx = __we3_level_actors_register(ctx, param_2, param_3, param_4, param_5, param_6, param_7,
                    param_8, 1, &DAT_804547d4, 3);
    __we3_level_resource_pool_setup(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    proj_register_smoke_def();
    DAT_80612764 = 0;

    // Flag the soundpad zones: exterior (0x20000) vs interior (0x10000) by id.
    static const int extZones[] = { 0x61d, 0x665, 0x666, 0x667, 0x6b2, 0x6b1, 0x6b3, 0x6b4,
        0x6c9, 0x6ca, 0x6cb, 0x6cc, 0x716, 0x70d, 0x71d, 0x718, 0x6f3, 0x6f4, 0x6f1, 0x6f2,
        0x6ec, 0x6eb, 0x6e9, 0x6ea };
    static const int intZones[] = { 0x664, 0x663, 0x61c, 0x61b, 0x662, 0x661, 0x717, 0x71f };
    for (int i = 0; i < (int)(sizeof(extZones)/sizeof(extZones[0])); ++i) {
        int z = DAT_80611b8c + soundpad_find_zone_by_id(extZones[i]) * 0x20;
        *(unsigned int *)(z + 4) |= 0x20000;
    }
    for (int i = 0; i < (int)(sizeof(intZones)/sizeof(intZones[0])); ++i) {
        int z = DAT_80611b8c + soundpad_find_zone_by_id(intZones[i]) * 0x20;
        *(unsigned int *)(z + 4) |= 0x10000;
    }

    DAT_80612754[0x25] = FLOAT_806177f8;
    DAT_80612754[0x26] = FLOAT_806177fc;
    DAT_80612754[0x27] = FLOAT_80617800;
    DAT_80612754[0x28] = FLOAT_80617804;
    __we628_particle_def_create((double)FLOAT_80617808, (double)FLOAT_8061780c,
        DAT_80612754 + 0x4b, 0, 1, 1, 0x32e, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617810, (double)FLOAT_806177f8,
        (double)FLOAT_80617814, (double)FLOAT_80617814, 0x5f4a3f00, 0x5f4a3f00, 0x1e0f0a00);
    particle_def_set_velocity_ranges((double)FLOAT_80617818, (double)FLOAT_806177f8,
        (double)FLOAT_806177f8, (double)FLOAT_806177f8, (double)FLOAT_8061780c, (double)FLOAT_8061781c);
    particle_def_set_size_start_end((double)FLOAT_80617820, (double)FLOAT_80617824,
        (double)FLOAT_806177f8, (double)FLOAT_80617828);
    particle_def_set_color_end((double)FLOAT_8061782c, (double)FLOAT_806177f8,
        (double)FLOAT_80617830, (double)FLOAT_806177f8);
    particle_def_set_drag((double)FLOAT_80617828);
    __we628_particle_emitter_create((double)FLOAT_80617828, (double)FLOAT_80617834,
        (double)FLOAT_80617814, DAT_80612754 + 0x2a, DAT_80612754 + 0x4b, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_8061780c, (double)FLOAT_806177f8,
        (double)FLOAT_80617838, (double)FLOAT_8061783c);
    particle_emitter_set_lifetime_range((double)FLOAT_80617840, (double)FLOAT_806177f8);
}


// ===========================================================================
// batch_629  0x801f0eec - 0x801f1cd8
// EKUN CDH1 master-update part C: the teleporter zone-match, ambient-loop
// update, the 'FOG1' spark-burst spawner (single-player + net variants), the
// master per-frame dispatcher (HNA1/HNA2 hangar gates, MECH scan, FOG spark),
// and the 'CLMV' hangar bird flock (spawn + per-frame anim + a bird-dropping /
// spark cluster spawner backed by a big physics scratch block DAT_805c6108).
//
// The FOG1 spark uses a countdown at DAT_80612754+0x94 and a shared sin table
// DAT_80583228 for jittered directions; the bird flock lays 6 birds out from a
// per-slot position table DAT_804548e8 (stride 0x10) and drives them through
// anim clips 0x800..0x804. The dropping cluster fills a 0xf-entry record ring in
// DAT_805c6108 with per-fragment position/dir/spin sampled from the sin table.
//
// Non-matching: 4CC ids (HNA1/HNA2/MECH/FOG1/FOG G1/CLMV); FP spill frames
// (FUN_803826f0/8038273c, FUN_803826d8/80382724, FUN_803826d4/80382720) opaque;
// int<->double 0x43300000 bias plain math; paired-single spill temps reduced;
// the sin-table index masks (&0x7ff / &0x1ffc) carried verbatim; several helpers
// route through file-local __we629_* aliases.
// ===========================================================================

extern "C" {

// --- raw data symbols for batch_629 (file-local) ---
extern int    DAT_80611d78;        // player slot pool base (already used earlier; re-declared int)
extern int    DAT_80611b94;        // soundpad zone table B base
extern int    DAT_80611b98;        // soundpad zone table C base
extern int    DAT_80611c14;        // soundpad zone count
extern int    DAT_80612098;        // entity list end index
extern int    DAT_80612610;        // active player/entity count
extern int    DAT_80588610;        // entity ptr array base
// DAT_80583228 is the shared sin/cos table. generated_decls declares it as a
// scalar `float`; index it through &DAT_80583228 (as the ROM decomp does).
extern float  DAT_80583228;        // shared sin/cos table base (0x800 entries)
extern char   DAT_805015bd;        // level/mode flag
extern int    DAT_805c6100;        // bird-cluster active flag/scale
extern int    DAT_805c6104;        // bird-cluster room id
extern int   *DAT_805c6108;        // bird-cluster physics scratch (0x7bc bytes)
extern int    DAT_8045481c; extern int DAT_80454844; extern int DAT_80454858; extern int DAT_8045489c;
extern int    DAT_804548ac;        // spark offset/param table (stride 8)
extern int    DAT_804548dc;        // anim-clip exclude table
extern int    DAT_804548e8;        // bird per-slot position table x (stride 0x10)
extern int    DAT_804548ec;        //   .. y
extern int    DAT_804548f0;        //   .. z
extern int    DAT_804548f4;        //   .. dive-range param
extern int    DAT_803df9c4;        // spark glow quad vertex block
extern int    DAT_803df9f8; extern int DAT_803df9fc; extern int DAT_803dfa00; // spark dir vec (net)
extern int    DAT_803dfa04; extern int DAT_803dfa08; extern int DAT_803dfa0c; // spark dir vec (sp)
extern int    DAT_80566f4c;        // spark particle emitter def
extern int    LAB_801f1cbc;        // bird-cluster block free callback label

extern double DOUBLE_80617860;     // 2^52 bias (variant)
extern double DOUBLE_80617880;     // 2^52 bias (variant)
extern float  FLOAT_8061785c;      // spark glow index scale divisor
extern float  FLOAT_80617858;      // spark glow alpha scale
extern float  FLOAT_80617868;      // rng normaliser (variant)
extern float  FLOAT_8061786c;      // spark mirror base
extern float  FLOAT_80617870;      // spark z base
extern float  FLOAT_80617874;      // spark z range
extern float  FLOAT_80617878;      // spark size
extern float  FLOAT_80617888;      // fog spark z step
extern float  FLOAT_8061788c;      // spectator fov
extern float  FLOAT_80617890;      // bird speed decay
extern float  FLOAT_80617894;      // bird accel coeff
extern float  FLOAT_80617898;      // bird target y base
extern float  FLOAT_8061789c;      // bird target y range
extern float  FLOAT_806178a0;      // bird x-threshold (flip)
extern float  FLOAT_806178a4;      // dropping cast scale
extern float  FLOAT_806178a8;      // dropping cast distance
extern float  FLOAT_806178ac;      // floor-miss sentinel
extern float  FLOAT_806178b0;      // dropping spin base
extern float  FLOAT_806178b4;      // dropping spin range

// raw code symbols not in generated headers / with FP-spill ABI
extern unsigned long long FUN_803826d8(void);   // FP spill open (c) -> packed pair (posPtr,room)
extern void   FUN_80382724(void);                // FP spill close (c)
extern void   FUN_801f39d0(void);                // worm master tick (defined later in tree)

} // extern "C"

// --- typed fn-ptr aliases for thin-return / reduced-arity generated_decls ---

// floor_room_query returns the floor height; here we pass a float* as the
// out-entity slot (ROM writes a handle there), so alias with float* arg3.
static float (*const __we629_floor_room_query)(int, float *, float *, int *, int) =
    (float (*)(int, float *, float *, int *, int))&floor_room_query;

// block_alloc_dispatch ROM returns the block pointer.
static int *(*const __we629_block_alloc_dispatch)(int, int, void *, void *) =
    (int *(*)(int, int, void *, void *))&block_alloc_dispatch;

// rng_counter_tick ROM returns the LCG word (declared void in generated_decls).
static unsigned int (*const __we629_rng_counter_tick)(void) =
    (unsigned int (*)(void))&rng_counter_tick;

// anim_active_clip_slot ROM returns the active clip-slot record ptr.
static int *(*const __we629_anim_active_clip_slot)(int) =
    (int *(*)(int))&anim_active_clip_slot;

// anim_active_slot_userflags ROM returns the flag word.
static int (*const __we629_anim_active_slot_userflags)(int) =
    (int (*)(int))&anim_active_slot_userflags;

// spawn_point_find_by_entity ROM returns the spawn-point record.
static int (*const __we629_spawn_point_find_by_entity)(int) =
    (int (*)(int))&spawn_point_find_by_entity;


// ---------------------------------------------------------------------------
// 0x801f0eec  ekun_teleport_zone_match
// Teleporter zone-match: scan the active players; resolve each player's current
// soundpad zone id (through the 3 zone tables by index band), and if it matches
// the trigger's target zone (*param1+0xc), teleport the OTHER player to the
// trigger's destination zone (*param1+0x10) and zero the shared 3-float vector
// at DAT_80611d78+0xe38.
// ---------------------------------------------------------------------------
void ekun_teleport_zone_match(int *self)
{
    int idx = 0, slot = 0, remaining = DAT_80612610;
    if (0 < DAT_80612610) {
        do {
            int rig = *(int *)(*(int *)(DAT_80611d78 + idx + 0x14) + 0x7c);
            if ((rig != 0) && ((*(unsigned int *)(rig + 0x24) & 8) == 0)) {
                int zid = *(int *)(rig + 0x678);
                int zone;
                if ((7999 < zid) && (zid < 0x209e)) {
                    zone = *(int *)(DAT_80611b94 + zid * 0x20 + -0x3e800);
                } else if ((0x209d < zid) && (zid < 0x20b2)) {
                    zone = *(int *)(DAT_80611b98 + zid * 0x20 + -0x413c0);
                } else if ((-1 < zid) && (zid < DAT_80611c14)) {
                    zone = *(int *)(DAT_80611b8c + zid * 0x20);
                } else {
                    zone = -1;
                }
                if (zone == *(int *)(*self + 0xc)) {
                    int dz = soundpad_find_zone_by_id(*(int *)(*self + 0x10));
                    player_ai_teleport_wrapper(
                        *(int *)(*(int *)(DAT_80611d78 + (1 - slot) * 0x141c + 0x14) + 0x7c), dz, 1);
                    *(float *)(DAT_80611d78 + 0xe38) = FLOAT_806177f8;
                    *(float *)(DAT_80611d78 + 0xe3c) = FLOAT_806177f8;
                    *(float *)(DAT_80611d78 + 0xe40) = FLOAT_806177f8;
                    return;
                }
            }
            slot += 1;
            idx += 0x141c;
            remaining -= 1;
        } while (remaining != 0);
    }
}


// ---------------------------------------------------------------------------
// 0x801f10d4  ekun_ambient_sources_update
// Update the four EKUN ambient 3D loop sources (sample ids 0x4f/0x4c/0x4d/0x4e)
// at their DAT_80612754 handle slots; the 0x4d source is gated off (null def) by
// the net-authority query FUN_80220f30(3).
// ---------------------------------------------------------------------------
void ekun_ambient_sources_update(void)
{
    ambient_source_update(0x4f, (unsigned int)(uintptr_t)DAT_80612754, (int *)&DAT_8045481c);
    ambient_source_update(0x4c, (unsigned int)(uintptr_t)(DAT_80612754 + 8), (int *)&DAT_80454844);
    int *def = (int *)&DAT_80454858;
    if (FUN_80220f30(3) != 0) {
        def = 0;
    }
    ambient_source_update(0x4d, (unsigned int)(uintptr_t)(DAT_80612754 + 0x10), def);
    ambient_source_update(0x4e, (unsigned int)(uintptr_t)(DAT_80612754 + 0x18), (int *)&DAT_8045489c);
}


// ---------------------------------------------------------------------------
// 0x801f1170  ekun_spark_glow_render
// Render the FOG spark glow quad: copy the 6-word vertex block from the rodata
// (DAT_803df9c4), compute a packed alpha from the spark counter (+0xa4), and
// submit a screen quad on sub-material 1.
// ---------------------------------------------------------------------------
void ekun_spark_glow_render(void)
{
    float verts[6][2];
    const float *src = (const float *)&DAT_803df9c4;
    for (int i = 0; i < 6; ++i) { verts[i][0] = src[i * 2 + 0]; verts[i][1] = src[i * 2 + 1]; }
    int a = (int)(FLOAT_80617858 *
        ((float)((double)CONCAT44(0x43300000, *(unsigned int *)(DAT_80612754 + 0xa4) ^ 0x80000000)
                 - DOUBLE_80617860) / FLOAT_8061785c));
    unsigned int alpha = (unsigned int)(a << 0x18);
    gx_set_sub_material_id(1);
    gx_draw_quad_pos4((unsigned int)(uintptr_t)&verts[0], (unsigned int)(uintptr_t)&verts[2],
                      (float *)&verts[3], (float *)&verts[4], alpha, alpha, alpha, alpha);
}


// ---------------------------------------------------------------------------
// 0x801f1230  ekun_fog_spark_burst_tick
// FOG spark burst tick (net-authoritative variant). Counts down a spark timer
// (DAT_80612754+0x94); on elapse it draws a random spark direction/offset from
// the DAT_804548ac param table + sin table jitter and spawns the DAT_80566f4c
// spark emitter, then re-arms the timer from an rng-scaled interval.
// ---------------------------------------------------------------------------
void ekun_fog_spark_burst_tick(void)
{
    float zero = FLOAT_806177f8;
    *(float *)(DAT_80612754 + 0x94) =
        -(FLOAT_806108ec * (FLOAT_8061780c /
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860)) -
          *(float *)(DAT_80612754 + 0x94));
    if (*(float *)(DAT_80612754 + 0x94) <= zero) {
        float dir[3];
        *(int *)&dir[0] = DAT_803df9f8; *(int *)&dir[1] = DAT_803df9fc; *(int *)&dir[2] = DAT_803dfa00;
        unsigned int coin = __we629_rng_counter_tick();
        unsigned int idx = __we629_rng_counter_tick() % 6;
        unsigned int r2 = __we629_rng_counter_tick();
        *(float *)(DAT_80612754 + 0x94) = FLOAT_80617814 *
            (FLOAT_8061781c + (float)((double)CONCAT44(0x43300000, r2 ^ 0x80000000) - DOUBLE_80617880)
                              / FLOAT_80617868);
        float off[3];
        if ((coin & 1) == 0) {
            dir[0] = -dir[0];
            off[0] = FLOAT_8061786c - *(float *)(&DAT_804548ac + idx * 8);
        } else {
            off[0] = *(float *)(&DAT_804548ac + idx * 8);
        }
        *(int *)&off[1] = *(int *)(&DAT_804548ac + 4 + idx * 8);
        unsigned int r3 = __we629_rng_counter_tick();
        off[2] = FLOAT_80617874 *
            ((float)((double)CONCAT44(0x43300000, r3 ^ 0x80000000) - DOUBLE_80617880) / FLOAT_80617868)
            + FLOAT_80617870;
        unsigned int r4 = __we629_rng_counter_tick();
        *(float *)(DAT_80612754 + 0x94) = FLOAT_80617814 *
            (FLOAT_8061781c + (float)((double)CONCAT44(0x43300000, r4 ^ 0x80000000) - DOUBLE_80617880)
                              / FLOAT_80617868);
        particle_emitter_spawn(0, (unsigned int)(uintptr_t)&DAT_80566f4c, 0, off, dir);
        particle_cached_set_flags2(8, 0);
        particle_emitter_set_size_range((double)FLOAT_80617878, (double)FLOAT_80617878);
    }
}


// ---------------------------------------------------------------------------
// 0x801f142c  ekun_fog1_spark_sequence
// FOG1 scripted spark sequence (single-player variant). Same spark spawn as the
// net tick, but keyed on the 'FOG1' entity: each burst registers a per-frame
// glow render (slot 10 -> ekun_spark_glow_render), steps the z offset (+0xa0)
// and counter (+0xa4); after 25 bursts it enters the 'CLMV' spectator camera
// and fires the scripted payload, clearing the sequence-active flag (+0x90).
// ---------------------------------------------------------------------------
void ekun_fog1_spark_sequence(void)
{
    float zero = FLOAT_806177f8;
    float dir[3];
    double dtRecip = (double)(FLOAT_8061780c /
        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860));
    *(int *)&dir[0] = DAT_803dfa04; *(int *)&dir[1] = DAT_803dfa08; *(int *)&dir[2] = DAT_803dfa0c;
    *(float *)(DAT_80612754 + 0x94) =
        -(float)((double)FLOAT_806108ec * dtRecip - (double)*(float *)(DAT_80612754 + 0x94));
    if (*(float *)(DAT_80612754 + 0x94) <= zero) {
        int fog1 = FUN_8023c32c(0x464f4731);   // 'FOG1'
        unsigned int coin = __we629_rng_counter_tick();
        unsigned int idx = __we629_rng_counter_tick() % 6;
        float off[3];
        if ((coin & 1) == 0) {
            dir[0] = -dir[0];
            off[0] = FLOAT_8061786c - *(float *)(&DAT_804548ac + idx * 8);
        } else {
            off[0] = *(float *)(&DAT_804548ac + idx * 8);
        }
        *(int *)&off[1] = *(int *)(&DAT_804548ac + 4 + idx * 8);
        *(int *)&off[2] = *(int *)(DAT_80612754 + 0xa0);
        unsigned int r = __we629_rng_counter_tick();
        *(float *)(DAT_80612754 + 0x94) = FLOAT_80617814 *
            (FLOAT_8061781c + (float)((double)CONCAT44(0x43300000, r ^ 0x80000000) - DOUBLE_80617880)
                              / FLOAT_80617868);
        particle_emitter_spawn(0, (unsigned int)(uintptr_t)&DAT_80566f4c, 0, off, dir);
        particle_cached_set_flags2(8, 0);
        particle_emitter_set_size_range((double)FLOAT_80617878, (double)FLOAT_80617878);
        entity_register_for_update(fog1, 10, (int)0x801f1170);   // ekun_spark_glow_render
        *(float *)(DAT_80612754 + 0xa0) = *(float *)(DAT_80612754 + 0xa0) + FLOAT_80617888;
        *(int *)(DAT_80612754 + 0xa4) = *(int *)(DAT_80612754 + 0xa4) + 1;
    }
    if (*(int *)(DAT_80612754 + 0xa4) == 0x19) {
        int sp = __we629_spawn_point_find_by_entity(0x434c4d56);   // 'CLMV'
        spectator_set_mode_if_ready((double)FLOAT_8061788c, sp);
        FUN_80221074((double)FLOAT_806177f8, 0, 0, 0, 0, 0, 0, 0);
        *(int *)(DAT_80612754 + 0x90) = 0;
    }
}


// ---------------------------------------------------------------------------
// 0x801f1648  ekun_master_update_c
// EKUN CDH1 master per-frame update part C. Runs the ambient sources and the
// worm master tick; on level-state 3/4 gates the HNA1/HNA2 hangar payloads
// (once each absent), on state 0x13/0x14 scans for the 'MECH' entity, and drives
// the FOG spark sequence (single-player ekun_fog1_spark_sequence, or the net
// ekun_fog_spark_burst_tick gated by FUN_80220f30(3) + player state 10).
// ---------------------------------------------------------------------------
void ekun_master_update_c(unsigned long long param_1, double param_2, double param_3,
                          double param_4, double param_5, double param_6, double param_7,
                          double param_8)
{
    (void)param_1;
    ekun_ambient_sources_update();
    FUN_801f39d0();   // worm master tick (defined later / other file)

    if ((DAT_80611d74 != 0) &&
        ((*(int *)(DAT_80611d74 + 0xdec) == 4) || (*(int *)(DAT_80611d74 + 0xdec) == 3))) {
        int hna1 = FUN_8023c32c(0x484e4131);   // 'HNA1'
        int hna2 = FUN_8023c32c(0x484e4132);   // 'HNA2'
        if (hna1 == 0) {
            if (DAT_80612764 == 1) {
                FUN_80221074((double)FLOAT_806177f8, param_2, param_3, param_4, param_5, param_6,
                             param_7, param_8);
            }
        } else {
            DAT_80612764 = 1;
        }
        if (hna2 == 0) {
            if (DAT_80612764 == 2) {
                FUN_80221074((double)FLOAT_806177f8, param_2, param_3, param_4, param_5, param_6,
                             param_7, param_8);
            }
        } else {
            DAT_80612764 = 2;
        }
    }

    if ((DAT_80611d74 != 0) &&
        ((*(int *)(DAT_80611d74 + 0xdec) == 0x13) || (*(int *)(DAT_80611d74 + 0xdec) == 0x14))) {
        int i = DAT_80612610 << 2;
        int rem = DAT_80612098 - DAT_80612610;
        if (DAT_80612610 < DAT_80612098) {
            do {
                int ent = *(int *)(*(int *)((int)&DAT_80588610 + i) + 0xbf0);
                if ((ent != 0) && (*(int *)(ent + 8) == 0x4d454348 /* 'MECH' */)) break;
                i += 4;
                rem -= 1;
            } while (rem != 0);
        }
    }

    if ((*(int *)(DAT_80612754 + 0x90) == 0) || (FUN_80220f30(3) == 0)) {
        if ((FUN_80220f30(3) != 0) &&
            ((*(int *)(DAT_80611d78 + 0x1400) == 10) || (*(int *)(DAT_80611d78 + 0x281c) == 10))) {
            ekun_fog_spark_burst_tick();
        }
    } else {
        ekun_fog1_spark_sequence();
    }
}


// ---------------------------------------------------------------------------
// 0x801f17d4  ekun_hangar_bird_anim_tick
// Per-frame anim tick for a hangar 'CLMV' bird. Reads its slot index (+0x1a0)
// to re-seat its base position from the DAT_804548e8 table, decays the bird
// speed (+0xcc) under net authority, and runs a small clip state machine
// (clips 0x800..0x804): dive interpolation of the y position (+0x6c toward
// +0x1a4) for clip 0x804, retargeting the y for clip 0x802, and cross-fade /
// reset for the 0x803/0x805 clips. Flips facing when x exceeds the threshold.
// ---------------------------------------------------------------------------
void ekun_hangar_bird_anim_tick(void)
{
    int e = FUN_803826f0();
    int *slot = __we629_anim_active_clip_slot(e);
    int clip = *slot;
    bool arrived = false;
    int si = *(int *)(e + 0x1a0) * 0x10;
    *(int *)(e + 100)  = *(int *)(&DAT_804548e8 + si);
    *(int *)(e + 0x68) = *(int *)(&DAT_804548ec + si);
    *(int *)(e + 0x6c) = *(int *)(&DAT_804548f0 + si);
    *(int *)(e + 0x6c) = *(int *)(e + 0xd0);

    if (FUN_80220f30(3) != 0) {
        *(float *)(e + 0xcc) = *(float *)(e + 0xcc) * FLOAT_80617890;
        anim_set_blend_weight_alt((double)*(float *)(e + 0xcc), *(int *)(e + 4));
    }

    if (clip == 0x804) {
        float target = *(float *)(e + 0x6c);
        float cur = *(float *)(e + 0x1a4);
        float step = *(float *)(e + 0xcc) * FLOAT_80617894 *
            (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860) *
            (FLOAT_8061780c /
             (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860));
        if (step <= ABS(target - cur)) {
            *(float *)(e + 0x6c) = (cur <= target) ? (target - step) : (target + step);
        } else {
            *(float *)(e + 0x6c) = cur;
            arrived = true;
        }
    }

    if ((arrived || (__we629_anim_active_slot_userflags(*(int *)(e + 4)) != 0)) &&
        (DAT_806108e4 != 0) && ((double)FLOAT_8061780c == (double)*(float *)(e + 0xcc))) {
        if (clip == 0x803) {
            __we629_rng_counter_tick();
            anim_play_scaled((double)FLOAT_806177f8, (double)FLOAT_8061780c);
        } else if (clip < 0x803) {
            if (clip < 0x802) {
                if (0x7fe < clip) {
                    unsigned int pick = __we629_rng_counter_tick() % 6;
                    if (clip == *(int *)(&DAT_804548dc + pick * 4)) pick += 1;
                    anim_play_scaled((double)FLOAT_806177f8, (double)FLOAT_8061780c);
                }
            } else {
                // clip 0x802: retarget the dive y (+0x1a4) via an rng-jittered offset.
                float y = *(float *)(e + 0x1a4);
                while (y == *(float *)(e + 0x1a4)) {
                    unsigned int r = __we629_rng_counter_tick();
                    int si2 = *(int *)(e + 0x1a0) * 0x10;
                    y = (float)((double)FLOAT_80617894 *
                        (double)(float)((double)CONCAT44(0x43300000,
                            (int)((double)((float)((double)FLOAT_80617898 +
                                (double)*(float *)(&DAT_804548f4 + si2)) *
                                (float)((double)(float)((double)CONCAT44(0x43300000, r) -
                                    DOUBLE_80617880) / FLOAT_80617868)) * (double)FLOAT_8061789c) ^
                            0x80000000) - DOUBLE_80617860) +
                        (double)*(float *)(&DAT_804548f0 + si2));
                }
                *(float *)(e + 0x1a4) = y;
                anim_play_scaled((double)FLOAT_806177f8, (double)FLOAT_8061780c);
            }
        } else if (clip < 0x805) {
            if (*(float *)(e + 0x6c) == *(float *)(e + 0x1a4)) {
                anim_play_scaled((double)FLOAT_806177f8, (double)FLOAT_8061780c);
            } else {
                anim_reset_blend_and_clip(*(int *)(e + 4));
            }
        }
    }
    if (FLOAT_806178a0 < *(float *)(e + 100)) {
        *(float *)(e + 0x70) = FLOAT_80617820;
    }
    *(int *)(e + 0xd0) = *(int *)(e + 0x6c);
    FUN_8038273c();
}


// ---------------------------------------------------------------------------
// 0x801f1b88  ekun_hangar_bird_spawn
// Spawn the 6-bird 'CLMV' hangar flock. For each of 6 birds, look up the
// entity by (base 4CC + i), set its blend weight + install the per-frame anim
// tick (ekun_hangar_bird_anim_tick) and a bone rig, seed its slot index and
// base position from the DAT_804548e8 table, flip facing past the x threshold.
// ---------------------------------------------------------------------------
void ekun_hangar_bird_spawn(void)
{
    int *ctx = (int *)FUN_803826f0();
    for (int i = 0; i < 6; ++i) {
        int bird = FUN_8023c32c(*(int *)(*ctx + 0x14) + i);
        if (bird != 0) {
            anim_set_blend_weight((double)FLOAT_8061780c, *(int *)(bird + 4));
            entity_register_for_update(bird, 3, (int)0x801f17d4);   // ekun_hangar_bird_anim_tick
            entity_register_for_update(bird, 2, (int)0x801edc60);   // bone rig (-0x7ffe2614)
            *(int *)(bird + 0x1a0) = i;
            *(int *)(bird + 100)  = *(int *)(&DAT_804548e8 + i * 0x10);
            *(int *)(bird + 0x68) = *(int *)(&DAT_804548ec + i * 0x10);
            *(int *)(bird + 0x6c) = *(int *)(&DAT_804548f0 + i * 0x10);
            if (FLOAT_806178a0 < *(float *)(bird + 100)) {
                *(float *)(bird + 0x70) = FLOAT_80617820;
            }
            *(int *)(bird + 0x1a4) = *(int *)(bird + 0x6c);
            *(int *)(bird + 0xd0) = *(int *)(bird + 0x6c);
            *(float *)(bird + 0xcc) = FLOAT_8061780c;
        }
    }
    FUN_8038273c();
}


// ---------------------------------------------------------------------------
// 0x801f1c84  ekun_switch_weapon_swap_thunk
// Thin thunk: resolve the switch slot for the entity's linked-object id (+0x40)
// then run the shared weapon-swap handler.
// ---------------------------------------------------------------------------
void ekun_switch_weapon_swap_thunk(int self)
{
    switch_find_slot_by_entity(*(int *)(self + 0x40));
    switch_weapon_swap_handler();
}


// ---------------------------------------------------------------------------
// 0x801f1cd8  ekun_bird_dropping_cluster_spawn
// Spawn a bird-dropping / debris spark cluster. Fires a 0x29 explosion at the
// drop point, then (unless in the 2-player 0x0a mode) fills a 0xf-entry physics
// record ring in the DAT_805c6108 scratch block (lazily allocated): each record
// gets the drop position, the forward dir, an rng+sin-table jittered velocity /
// spin, and a 4-copy trail seed. A floor query + portal raycast decide whether
// each fragment orients to the surface (uVar7 flag).
// ---------------------------------------------------------------------------
void ekun_bird_dropping_cluster_spawn(int param_1, int param_2, float *dir)
{
    (void)param_1; (void)param_2;
    unsigned long long packed = FUN_803826d8();
    int room = (int)(packed >> 0x20);
    float *pos = (float *)(unsigned int)packed;
    float floorH = 0.0f;
    float hitFrac = 0.0f;
    unsigned short surfFlag = 1;

    player_ai_explosion_spawn(0x29, 0x2c, pos, dir, *(int *)(DAT_80611d74 + 0x14), 0, 1);

    if ((DAT_805015bd != '\n') || (DAT_80612610 < 2)) {
        if (DAT_805c6108 == 0) {
            DAT_805c6108 = __we629_block_alloc_dispatch(0x7bc, 1, &DAT_805c6108, &LAB_801f1cbc);
            if (DAT_805c6108 == 0) { FUN_80382724(); return; }
            memset_wrapper((int)(long long)DAT_805c6108, 0, 0x7bc);
        }
        DAT_805c6100 = *(int *)&FLOAT_80617828;
        float qpos[3] = { pos[0], pos[1] + FLOAT_80617814, pos[2] };
        DAT_805c6104 = room;
        float fh = __we629_floor_room_query(room, qpos, &floorH, (int *)0, 1);
        float mtx[16];
        if (FLOAT_8061780c <= ABS((float)((double)fh - (double)pos[1]))) {
            float castDir[3] = { FLOAT_806178a4 * dir[0], FLOAT_806178a4 * dir[1],
                                 FLOAT_806178a4 * dir[2] };
            float castStart[3] = {
                (float)((double)FLOAT_806178a8 * (double)castDir[0] + (double)pos[0]),
                (float)((double)FLOAT_806178a8 * (double)castDir[1] + (double)pos[1]),
                (float)((double)FLOAT_806178a8 * (double)castDir[2] + (double)pos[2]) };
            mtx4_basis_from_forward(mtx, dir);
            entity_raycast_room_portals(room, castStart, castDir, 1, (float *)0, (float *)0,
                                        (void **)&hitFrac, 0xfffffffd);
        }
        if (((double)FLOAT_806178ac == (double)fh) ||
            (ABS((float)((double)fh - (double)pos[1])) < FLOAT_8061780c) || (hitFrac == 0.0f)) {
            mtx4_set_identity((int)(long long)mtx);
            if (((double)FLOAT_806178ac != (double)fh) &&
                (ABS((float)((double)fh - (double)pos[1])) < FLOAT_8061780c)) {
                pos[1] = fh;
            }
            surfFlag = 0;
        }

        int off = 0;
        for (int n = 0; n < 0xf; ++n) {
            float *rec = (float *)((int)(long long)DAT_805c6108 + off);
            unsigned int r1 = __we629_rng_counter_tick();
            unsigned int sidx = (unsigned int)((double)FLOAT_806178b4 *
                (double)(float)((double)FLOAT_806178b0 *
                    (double)(float)((double)(float)((double)CONCAT44(0x43300000, r1) -
                        DOUBLE_80617880) / FLOAT_80617868)) + (double)FLOAT_8061781c);
            float velX = (float)((double)FLOAT_80617878 * (double)*(float *)((int)&DAT_80583228 +
                            ((sidx + 0x200) * 4 & 0x1ffc)));
            float velZ = (float)((double)FLOAT_80617878 * (double)(&DAT_80583228)[sidx & 0x7ff]);
            float vel[3] = { velZ, FLOAT_806177f8, velX };
            mtx4_transform_vector(mtx, vel);
            rec[0] = pos[0]; rec[1] = pos[1]; rec[2] = pos[2];
            rec[6] = vel[0]; rec[7] = vel[1]; rec[8] = vel[2];
            rec[3] = dir[0]; rec[4] = dir[1]; rec[5] = dir[2];
            *(unsigned short *)(rec + 10) = surfFlag;
            *(unsigned short *)(rec + 0xb) = 0;
            *(unsigned short *)((int)(rec + 0xb) + 2) = 0;
            rec[0x10] = floorH;
            rec[0xf] = hitFrac;
            unsigned int r2 = __we629_rng_counter_tick();
            rec[9] = (float)((double)FLOAT_80617818 *
                (double)(float)((double)(float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80617880)
                                / FLOAT_80617868));
            rec[0x1e] = FLOAT_806177f8;
            // seed 4 trail copies of the position
            int t = 0;
            for (int k = 0; k < 4; ++k) {
                *(float *)((int)rec + t + 0x48) = rec[0];
                *(float *)((int)rec + t + 0x4c) = rec[1];
                *(float *)((int)rec + t + 0x50) = rec[2];
                t += 0xc;
            }
            rec[0x20] = 0.0f;
            off += 0x84;
        }
    }
    FUN_80382724();
}


// ===========================================================================
// batch_630 : 0x801f20f4 - 0x801f5017
// EKUN bird-dropping debris pool physics + the 'BDG1'/'BDG2' cable/blade-fan
// bridge set-piece and its flicker/loop-sound tick.
//
// The bird-dropping debris pool (DAT_805c6108, 0xf records of stride 0x84 seeded
// by ekun_bird_dropping_cluster_spawn in batch_629) is advanced per-frame by
// ekun_dropping_debris_update: each fragment integrates velocity, bounces off
// the floor and world (portal raycast), keeps a rolling 4-entry trail ring
// (rec[0x12..], cursor rec[0x1f]) that ekun_debris_trail_quad_render draws as a
// textured quad-strip, and re-normalises its velocity. The pool-wide fade timer
// DAT_805c6100 frees the block and drives a residual camera shake.
//
// The 'BDG1'..'BDG2' chain is a rope/cable "bridge" of linked fragment entities
// (each keeps swing state in +0xa0 / velocity in +0xb4..+0xbc / spin in +0xe8 /
// spin angle in +0xc0). ekun_bridge_fragment_tick runs one link's physics (spin
// ramp, random-impulse scatter, bounce back toward its anchor +0xa8), and when
// the primary player is riding it (+0xc04) it rotates the player's eye offset by
// the accumulated spin through the sine table and re-issues a periodic rng ride
// sound (0x4e1+rand&3). ekun_bridge_ribbon_build builds the two ribbon
// quad-strip meshes stretched between consecutive chain links into the entity's
// render lists, and ekun_bridge_glow_render composites the multi-layer sprite
// glow on top (with ekun_spark_billboard_render for the additive spark ribbons).
//
// Non-matching: 4CC ids ('BDG1'/'BDG2'/'BNH1'/'SENT'/'BRDG') are raw class
// words; PPC FP-spill frames (FUN_803826bc/d8/dc/e4/f0 open, FUN_80382708/24/28/
// 30/3c close) are opaque; the ROM's int<->double 0x43300000 magic-bias idiom is
// reduced to plain float/int math; reduced-arity/thin-return generated_decls
// helpers are routed through file-local __we629_* (reused) and __we630_* aliases.
// ===========================================================================

extern "C" {

// --- batch_630 raw data symbols (file-local; not already declared above) ---
extern int    DAT_80611d54;        // active view/context block base (matrix rows +0x334)

// rodata offset / color-triple tables for the bridge ribbon + spark billboards
extern float  DAT_803dfa10; extern float DAT_803dfa14; extern float DAT_803dfa18;  // SENT wind vel
extern float  DAT_803dfa1c; extern float DAT_803dfa20; extern float DAT_803dfa24;  // ribbon base A
extern float  DAT_803dfa28; extern float DAT_803dfa2c; extern float DAT_803dfa30;  // ribbon half A
extern float  DAT_803dfa34; extern float DAT_803dfa38; extern int   DAT_803dfa3c;  // ribbon base B
extern float  DAT_803dfa40; extern float DAT_803dfa44; extern int   DAT_803dfa48;  // ribbon half B
extern float  DAT_803dfa4c; extern float DAT_803dfa50; extern float DAT_803dfa54;  // spark up axis
extern float  DAT_803dfa58; extern float DAT_803dfa5c; extern float DAT_803dfa60;  // glow base A
extern float  DAT_803dfa64; extern float DAT_803dfa68; extern float DAT_803dfa6c;  // glow half A
extern float  DAT_803dfa70; extern float DAT_803dfa74; extern int   DAT_803dfa78;  // glow base B
extern float  DAT_803dfa7c; extern int   DAT_803dfa80; extern int   DAT_803dfa84;  // glow tint 0
extern float  DAT_803dfa88; extern int   DAT_803dfa8c; extern int   DAT_803dfa90;  // glow tint 1

// tuning floats not yet declared in earlier blocks
extern float  FLOAT_80617844;      // 1.5f (rsqrt Newton coefficient)
extern float  FLOAT_806178b8;      // min-length-squared epsilon (rsqrt guard)
extern float  FLOAT_806178bc;      // trail per-node spacing / step
extern float  FLOAT_806178c0;      // spin->angle-index scale
extern float  FLOAT_806178c4;      // contact-speed clamp / 0.5 turntable
extern float  FLOAT_806178c8;      // BDG1 reset spin value
extern float  FLOAT_806178cc;      // SENT wind velocity scale
extern float  FLOAT_806178d0;      // debris spawn rng gate
extern float  FLOAT_806178d4;      // settled fragment 0.0 marker
extern float  FLOAT_806178d8;      // ribbon-A endpoint width (last link)
extern float  FLOAT_806178dc;      // ribbon-A endpoint width 2
extern float  FLOAT_806178e0;      // ribbon endpoint fill/depth
extern float  FLOAT_806178e4;      // ribbon endpoint half-height
extern float  FLOAT_806178e8;      // spark spin-rate scale
extern float  FLOAT_806178ec;      // glow endpoint width (last link)
extern float  FLOAT_806178f0;      // glow endpoint width 2
extern float  FLOAT_806178f4;      // glow layer size scale A
extern float  FLOAT_806178f8;      // glow layer size scale B

// --- batch_630 raw code symbols (file-local; not in generated headers) ---
extern unsigned long long FUN_803826bc(void);  // FP spill frame open (glow render)
extern unsigned long long FUN_803826d8(void);   // FP spill frame open (returns room|pos pair)
extern unsigned long long FUN_803826dc(void);    // FP spill frame open (returns 2 endpoint ptrs)
extern void FUN_803826e4(void);   // FP spill frame open (dropping-debris update)
extern void FUN_80382708(void);   // FP spill frame close (glow render)
extern void FUN_80382728(void);   // FP spill frame close (spark billboard)
extern void FUN_80382730(void);   // FP spill frame close (dropping-debris update)
// block_free_simple resolves through generated_externs (int block_free_simple(...)).

} // extern "C"

// spark_spawn_fading / spark_set_last_scale / gx_begin_textured_batch / gx_end_batch /
// sprite_get_texture_id / entity_raycast_room_portals / collision_result_restore /
// math_sqrt / game_time_to_frames / player_set_eye_position / gx_set_sub_material_id /
// gx_set_blend_mode / mtx4_transform_vector / mtx4_set_axis_angle / mtx4_basis_from_forward /
// entity_build_local_matrix / entity_get_world_position / sound_init_play_params /
// sound_play_request / switch_find_slot_by_entity / switch_weapon_swap_handler /
// player_apply_camera_shake_priority all resolve through the generated headers.
// __we629_floor_room_query / __we629_rng_counter_tick / __we629_block_alloc_dispatch
// are the reduced-arity aliases declared in the batch_629 block above.

// GX FIFO immediate-mode write port (BE float / u32).
extern "C" { extern volatile float DAT_cc008000; }
#define WE630_GXF(v)  (DAT_cc008000 = (float)(v))
#define WE630_GXU(v)  (*(volatile unsigned int *)&DAT_cc008000 = (unsigned int)(v))

// sound_update_3d_source is declared void in generated_decls; the 0x801f39d0 and
// 0x801f5018 call sites read its nonzero-when-a-voice-started return.
static int (*const __we630_sound_update_3d_source)(double, double, double, int,
        unsigned int *, float *, unsigned int) =
    (int (*)(double, double, double, int, unsigned int *, float *, unsigned int))&sound_update_3d_source;

// The dropping-debris pool record is 0x84 bytes: [0..2]=pos, [3..5]=surface
// normal/dir, [6..8]=velocity, [9]=timer, [0xa]=onGround, [0xb]=hasFloor,
// [0xc..0xe]=hit point, [0xf]=hit polygon ptr, [0x10]=cached floor ptr,
// [0x11]=hit ceiling ptr, [0x12..0x1d]=4-entry trail ring, [0x1e]=trail
// accumulator, [0x1f]=trail cursor, [0x20]=broken flag.


// ---------------------------------------------------------------------------
// 0x801f20f4  ekun_dropping_debris_update
// Per-frame physics for the bird-dropping debris pool DAT_805c6108 (0xf records
// of stride 0x84). Runs only while the pool is live (DAT_805c6108 != 0), the
// fade timer DAT_805c6100 is still positive, and the level-state slot allows it.
// For each record: decrement the per-fragment timer (+0x24); on expiry either
// surface-orient (has-floor path) or reflect off a stored ceiling/floor pair and
// re-arm; integrate position by velocity over dt, floor-bounce / portal-raycast
// for collision, renormalise velocity, then advance the rolling 4-entry trail
// ring (rec[0x12..], cursor rec[0x1f]) spawning fading sparks along it. After all
// fragments, decay the pool fade timer (freeing the block at 0), and apply a
// residual per-player camera shake scaled by the remaining fade.
// ---------------------------------------------------------------------------
void ekun_dropping_debris_update(void)
{
    FUN_803826e4();

    if ((DAT_805c6108 == 0) || (FLOAT_806177f8 >= *(float *)&DAT_805c6100) ||
        (*(char *)(DAT_80611d54 + DAT_805c6104 + 0xd2c) == '\0')) {
        FUN_80382730();
        return;
    }

    int off = 0;
    for (int n = 0; n < 0xf; ++n) {
        float *rec = (float *)((int)(long long)DAT_805c6108 + off);
        float dt = FLOAT_806178bc /
            (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);
        float dtCur = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860);
        rec[9] = -(dt * dtCur - rec[9]);

        if (FLOAT_806177f8 < rec[9]) {
            goto integrate;
        }
        if (rec[0x20] != 0.0f) {
            goto next;
        }

        if (*(short *)(rec + 0xb) != 0) {
            if (*(short *)((int)(rec + 0xb) + 2) == 0) {
                // has-floor + no-ceiling: floor-orient the fragment.
                float qpos[3] = { rec[0], rec[1] + FLOAT_80617814, rec[2] };
                float floorH = 0.0f;
                float fh = __we629_floor_room_query(DAT_805c6104, qpos, &floorH, (int *)0, 1);
                if (((double)FLOAT_806178ac == (double)fh) ||
                    (FLOAT_806178bc < (float)((double)rec[1] - (double)fh))) {
                    rec[6] = -rec[6]; rec[7] = -rec[7]; rec[8] = -rec[8];
                } else {
                    rec[1] = fh;
                    rec[0x10] = floorH;
                    *(short *)(rec + 10) = 0;
                    float dp = ABS(rec[5] * rec[8] + rec[3] * rec[6] + rec[4] * rec[7]);
                    rec[6] = rec[3] * dp + rec[6];
                    rec[7] = rec[4] * dp + rec[7];
                    rec[8] = rec[5] * dp + rec[8];
                    rec[7] = FLOAT_806177f8;
                    rec[3] = FLOAT_806177f8;
                    rec[4] = FLOAT_8061780c;
                    rec[5] = FLOAT_806177f8;
                }
            } else {
                // ceiling-oriented reflection: use the stored polygon edge dirs.
                float *poly = (float *)(int)rec[0x11];
                double e0 = (double)(poly[7] - poly[4]);
                double e1 = (double)(poly[5] - poly[2]);
                double e2 = (double)(poly[6] - poly[3]);
                float nx = (float)((double)FLOAT_806177f8 * e1 - (double)(float)((double)FLOAT_806177f8 * e0));
                float ny = (float)(e2 * e0 - (double)FLOAT_806177f8);
                float nz = -(float)(e2 * e1 - (double)FLOAT_806177f8);
                float len2 = nz * nz + ny * ny + nx * nx;
                if (FLOAT_806178b8 < len2) {
                    float r = 1.0f / SQRT(len2);
                    r = r * -(FLOAT_8061781c * len2 * r * r - FLOAT_80617844);
                    float scale = FLOAT_806178bc * r * -(FLOAT_8061781c * len2 * r * r - FLOAT_80617844);
                    ny = ny * scale; nx = nx * scale; nz = nz * scale;
                }
                float dp = nz * rec[8] + ny * rec[6] + nx * rec[7];
                rec[6] = ny * -dp + rec[6];
                rec[7] = nx * -dp + rec[7];
                rec[8] = nz * -dp + rec[8];
                rec[7] = rec[7] + ABS(dp);
                rec[3] = ny; rec[4] = nx; rec[5] = nz;
                rec[0] = rec[0xc]; rec[1] = rec[0xd]; rec[2] = rec[0xe];
                rec[0xf] = rec[0x11];
                *(short *)(rec + 10) = 1;
            }
        }

        {
            // random spin about the reflected direction, re-arm the ray from pos.
            unsigned int r = __we629_rng_counter_tick();
            float ang = FLOAT_806178c0 *
                ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c);
            float rot[16];
            mtx4_set_axis_angle((double)ang, rot, rec + 3);
            mtx4_transform_vector(rot, rec + 6);
            *(short *)(rec + 0xb) = 0;
            *(short *)((int)(rec + 0xb) + 2) = 0;
            rec[9] = FLOAT_8061780c;

            float hitPos[3];
            float ceilPtr = 0.0f;
            int hit = entity_raycast_room_portals(DAT_805c6104, rec, rec + 6, 0,
                        hitPos, (float *)0, (void **)((int)rec + 0x44), 0xfffffffd);
            if (hit == 0) {
                if (*(short *)(rec + 10) == 0) {
                    // floor path: test the swept endpoint against cached floor poly.
                    float *poly = (float *)(int)rec[0x10];
                    float ex = rec[0] + rec[6];
                    float ez = rec[2] + rec[8];
                    float ey = rec[1] + rec[7];
                    if ((poly != 0) &&
                        ((poly[7] < ex || (ex < poly[5])) || ((poly[8] < ez || (ez < poly[6]))))) {
                        *(short *)(rec + 0xb) = 1;
                        rec[9] = FLOAT_8061780c;
                        int oa = 0, ob = 0;
                        for (int k = 0; k < 2; ++k) {
                            float edge = *(float *)((int)poly + oa + 0x1c);
                            float coord = *(float *)((int)rec + ob);
                            float vel = *(float *)((int)(rec + 6) + ob);
                            if (*(float *)((int)&ex - 4 + ob) <= edge) {
                                // (endpoint below the +hi edge: clamp against +lo edge)
                                edge = *(float *)((int)poly + oa + 0x14);
                                if ((float)(4503601774854144.0 - DOUBLE_80617860) < edge) {
                                    float t = ABS((coord - edge) / vel);
                                    if (t <= rec[9]) rec[9] = t;
                                }
                            } else {
                                float t = ABS((coord - edge) / vel);
                                if (t <= rec[9]) rec[9] = ABS((coord - *(float *)((int)poly + oa + 0x1c)) / vel);
                            }
                            oa += 4; ob += 8;
                        }
                    }
                } else {
                    // ceiling path: test the swept endpoint against the ceiling poly.
                    float *poly = (float *)(int)rec[0xf];
                    float ex = rec[0] + rec[6];
                    float ey = rec[1] + rec[7];
                    float ez = rec[2] + rec[8];
                    if (((poly[10] < ex) || (ex < poly[8])) ||
                        ((poly[11] < ez || (((ez < poly[9] || (poly[6] < ey)) || (ey < poly[3])))))) {
                        *(short *)(rec + 0xb) = 1;
                        if (poly[3] <= ey && ey <= poly[6]) {
                            rec[0x20] = 1.0f;
                            int oa = 0, ob = 0;
                            for (int k = 0; k < 2; ++k) {
                                float edge = *(float *)((int)poly + oa + 0x28);
                                if (*(float *)((int)&ex + ob) <= edge) {
                                    edge = *(float *)((int)poly + oa + 0x20);
                                    if ((float)(4503601774854144.0 - DOUBLE_80617860) < edge) {
                                        rec[9] = ABS((*(float *)((int)rec + ob) - edge) / *(float *)((int)(rec + 6) + ob));
                                    }
                                } else {
                                    rec[9] = ABS((*(float *)((int)rec + ob) - edge) / *(float *)((int)(rec + 6) + ob));
                                }
                                oa += 4; ob += 8;
                            }
                        } else {
                            // below the poly floor: re-query the level floor.
                            float qpos[3] = { rec[0], rec[1] + FLOAT_80617814, rec[2] };
                            float fh = __we629_floor_room_query(DAT_805c6104, qpos, (float *)0, (int *)0, 1);
                            if (((double)FLOAT_806178ac != (double)fh) &&
                                ((float)((double)ey - (double)fh) < FLOAT_80617818) &&
                                (rec[7] < FLOAT_806177f8)) {
                                *(short *)(rec + 0xb) = 1;
                                rec[9] = ABS((float)((double)rec[1] - (double)fh) / rec[7]);
                            }
                        }
                    }
                }
            } else {
                // world hit: park the fragment at the hit point + speed to reach it.
                *(short *)(rec + 0xb) = 1;
                *(short *)((int)(rec + 0xb) + 2) = 1;
                rec[0xc] = hitPos[0]; rec[0xd] = hitPos[1]; rec[0xe] = hitPos[2];
                double d = math_sqrt((double)((rec[2] - hitPos[2]) * (rec[2] - hitPos[2]) +
                                     (rec[0] - hitPos[0]) * (rec[0] - hitPos[0]) +
                                     (rec[1] - hitPos[1]) * (rec[1] - hitPos[1])));
                rec[9] = (float)(d / (double)FLOAT_80617878);
            }
            (void)ceilPtr;

            // renormalise velocity to FLOAT_80617878 speed.
            float vx = rec[6];
            float sp = rec[8] * rec[8] + vx * vx + rec[7] * rec[7];
            float rs = 1.0f / SQRT(sp);
            sp = FLOAT_8061781c * sp;
            rs = rs * -(sp * rs * rs - FLOAT_80617844);
            float k = FLOAT_80617878 * rs * -(sp * rs * rs - FLOAT_80617844);
            rec[6] = vx * k; rec[7] = rec[7] * k; rec[8] = rec[8] * k;
        }

    integrate:
        {
            float dt2 = FLOAT_8061780c /
                (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);
            float dtCur2 = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860);
            rec[9] = -(rec[9] * (dt2 * dtCur2));
            rec[0] = rec[6] * (dt2 * dtCur2) + rec[0];
            rec[1] = rec[7] * (dt2 * dtCur2) + rec[1];
            rec[2] = rec[8] * (dt2 * dtCur2) + rec[2];
            rec[0x1e] = (dt2 * dtCur2) + rec[0x1e];
            if (FLOAT_806178bc < rec[0x1e]) {
                int cur = (int)rec[0x1f];
                rec[cur * 3 + 0x12] = rec[0];
                rec[cur * 3 + 0x13] = rec[1];
                rec[cur * 3 + 0x14] = rec[2];
                rec[0x1e] = rec[0x1e] - FLOAT_806178bc;
                rec[0x1f] = (float)(((int)rec[0x1f] + 1) & 3);
            }
            // draw the 3 interpolated trail spark segments + 1 tail.
            for (int s = 0; s < 3; ++s) {
                int i0 = (s + (int)rec[0x1f]) & 3;
                int i1 = (s + (int)rec[0x1f] + 1) & 3;
                if (s == 0) {
                    float f = FLOAT_8061780c - rec[0x1e] / FLOAT_806178bc;
                    float sy = (rec[i0 * 3 + 0x13] - rec[i1 * 3 + 0x13]) * f + rec[i1 * 3 + 0x13];
                    float sz = (rec[i0 * 3 + 0x14] - rec[i1 * 3 + 0x14]) * f + rec[i1 * 3 + 0x14];
                    float sx = (rec[i0 * 3 + 0x12] - rec[i1 * 3 + 0x12]) * f + rec[i1 * 3 + 0x12];
                    (void)sx; (void)sy; (void)sz;
                    spark_spawn_fading();
                } else {
                    spark_spawn_fading();
                }
                spark_set_last_scale(FLOAT_8061780c);
            }
            spark_spawn_fading();
            spark_set_last_scale(FLOAT_8061780c);
        }

    next:
        off += 0x84;
    }

    // decay the pool-wide fade timer; free the block when it bottoms out.
    float fadeDt = FLOAT_8061780c /
        (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);
    float fadeCur = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860);
    *(float *)&DAT_805c6100 = -(fadeDt * fadeCur - *(float *)&DAT_805c6100);
    if (*(float *)&DAT_805c6100 < FLOAT_806177f8) {
        block_free_simple((int)(long long)DAT_805c6108);
        DAT_805c6108 = 0;
    }

    // residual per-player camera shake proportional to the remaining fade.
    for (int p = 0, e = 0; p < DAT_80612610; ++p, e += 0x141c) {
        float mag = FLOAT_8061781c * *(float *)&DAT_805c6100;
        player_apply_camera_shake_priority(FLOAT_80617814, FLOAT_8061788c * mag,
                                           FLOAT_8061781c * mag, DAT_80611d78 + e);
    }

    FUN_80382730();
}


// ---------------------------------------------------------------------------
// 0x801f2d04  ekun_debris_trail_quad_render
// Draw one debris trail segment as a camera-facing textured quad-strip (sprite
// 0x33a) via the GX FIFO. The width tapers between prevWidth/curWidth; the strip
// basis is cross(segmentDir, camera-right) scaled to build the two rails. The
// endpoint UVs are rng-jittered along the strip length.
// (start = p1..p3, end = p4..p6; widths p7/p8; colours param_9/param_10.)
// ---------------------------------------------------------------------------
void ekun_debris_trail_quad_render(double p1, double p2, double p3, double p4, double p5,
                                   double p6, double p7, double p8,
                                   unsigned int col0, unsigned int col1)
{
    unsigned int uv = __we629_rng_counter_tick();
    double dx = (double)(float)(p5 - p2);
    double dy = (double)(float)(p4 - p1);
    double dz = (double)(float)(p6 - p3);
    double vscroll = (double)((float)((double)CONCAT44(0x43300000, uv) - DOUBLE_80617880) / FLOAT_80617868);
    double len = math_sqrt((double)(float)(dz * dz +
                    (double)(float)(dy * dy + (double)(float)(dx * dx))));
    double inv = (double)(float)((double)FLOAT_8061780c / len);

    // strip basis = cross(unit segment dir, camera view rows at +0x334/+0x338/+0x33c)
    double bx = (double)((float)(dz * inv) * *(float *)(DAT_80611d54 + 0x334) -
                         (float)(dy * inv) * *(float *)(DAT_80611d54 + 0x33c));
    double bz = (double)((float)(dy * inv) * *(float *)(DAT_80611d54 + 0x338) -
                         (float)(dx * inv) * *(float *)(DAT_80611d54 + 0x334));
    double by = (double)((float)(dx * inv) * *(float *)(DAT_80611d54 + 0x33c) -
                         (float)(dz * inv) * *(float *)(DAT_80611d54 + 0x338));

    int tex = sprite_get_texture_id(0x33a);
    gx_begin_textured_batch(2, 0x92, 0, 4, tex);

    double w = (double)FLOAT_80617814;
    double wStart = (double)(float)(w * -p7);
    double wa = (double)((float)(w * p7) * FLOAT_8061781c);
    double wb = (double)((float)(w * p8) * FLOAT_8061781c);
    float x0 = (float)(bz * wa + p1);
    float y0 = (float)(bx * wa + p2);
    float z0 = (float)(by * wa + p3);
    WE630_GXF(x0); WE630_GXF(y0); WE630_GXF(z0);
    float x1 = (float)(bz * wb + p4);
    WE630_GXU(col0);
    float y1 = (float)(bx * wb + p5);
    float z1 = (float)(by * wb + p6);
    float vEnd = (float)(w * (double)(float)(len / p8) + vscroll);
    double wStart2 = (double)(float)(w * -p8);
    WE630_GXF(FLOAT_806177f8); WE630_GXF(FLOAT_806177f8);   // UV start
    WE630_GXF((float)(bz * wStart + (double)x0));
    WE630_GXF((float)(bx * wStart + (double)y0));
    WE630_GXF((float)(by * wStart + (double)z0));
    WE630_GXU(col0);
    WE630_GXF(FLOAT_806177f8); WE630_GXF(FLOAT_8061780c);
    WE630_GXF(x1); WE630_GXF(y1); WE630_GXF(z1);
    WE630_GXU(col1);
    WE630_GXF(vEnd); WE630_GXF(FLOAT_806177f8);
    WE630_GXF((float)(bz * wStart2 + (double)x1));
    WE630_GXF((float)(bx * wStart2 + (double)y1));
    WE630_GXF((float)(by * wStart2 + (double)z1));
    WE630_GXU(col1);
    WE630_GXF(vEnd); WE630_GXF(FLOAT_8061780c);

    gx_end_batch();
}


// ---------------------------------------------------------------------------
// 0x801f2fb8  ekun_debris_trail_quad_render_default
// Wrapper: draw a trail quad with the default unit start/end widths.
// ---------------------------------------------------------------------------
void ekun_debris_trail_quad_render_default(double p1, double p2, double p3, double p4,
                                           double p5, double p6, unsigned int col0,
                                           unsigned int col1)
{
    ekun_debris_trail_quad_render(p1, p2, p3, p4, p5, p6, (double)FLOAT_8061780c,
                                  (double)FLOAT_8061780c, col0, col1);
}


// ---------------------------------------------------------------------------
// 0x801f2fe0  ekun_debris_contact_speed_apply
// Contact callback: when the struck entity is the class-0x1a bridge link, a live
// player, and the contact was an enter/stay (mode 1 or 2), accumulate the impulse
// into its subobj speed field (+0x9c of the +0x7c slot) clamped to FLOAT_806178c4.
// (param_1[4]=other entity, [5]=class id, [8]=impulse; +0x18 scratch = contact record.)
// ---------------------------------------------------------------------------
void ekun_debris_contact_speed_apply(int *contact)
{
    int slot = *(int *)(*contact + 0x7c);
    unsigned char rec[0x28];
    collision_result_restore((int)(long long)rec);
    int mode = *(int *)(rec + 0x24);
    float clamp = FLOAT_806178c4;
    if ((contact[4] != 0) && (*(int *)(contact[4] + 8) < DAT_80612610) && (contact[5] == 0x1a) &&
        ((mode == 1 || (mode == 2)))) {
        *(float *)(slot + 0x9c) = *(float *)(slot + 0x9c) + (float)contact[8];
        if (clamp < *(float *)(slot + 0x9c)) {
            *(float *)(slot + 0x9c) = clamp;
        }
    }
}


// ---------------------------------------------------------------------------
// 0x801f3084  ekun_bridge_state_reset
// Reset the two bridge sub-chains. The 'BDG1' chain (linked entities whose
// running-context class word is 'BRDG') clears each link's contact-speed (+0x9c
// -> FLOAT_806178c8) and its state word (+0xa0 -> 0). The 'BNH1' chain (context
// class 'SENT') seeds each link's velocity (+0x10/+0x14/+0x18) from the DAT_803dfa10
// wind vector transformed by the link's bone matrix, scaled by FLOAT_806178cc,
// and sets state 2.
// ---------------------------------------------------------------------------
void ekun_bridge_state_reset(void)
{
    int *ctx = (int *)__save_gpr_28();
    int bdg = FUN_8023c32c(0x42444731);   // 'BDG1'
    int bnh = FUN_8023c32c(0x424e4831);   // 'BNH1'

    float resetSpin = FLOAT_806178c8;
    while ((bdg != 0) && (*(int *)(*ctx + 0xac) == 0x42524447 /* 'BRDG' */)) {
        int s = *(int *)(bdg + 0x7c);
        *(int *)(s + 0xa0) = 0;
        *(float *)(s + 0x9c) = resetSpin;
        bdg = FUN_8023c32c(*(int *)(bdg + 0x60) + 1);
    }

    float windScale = FLOAT_806178cc;
    while ((bnh != 0) && (*(int *)(*ctx + 0xac) == 0x53454e54 /* 'SENT' */)) {
        float wind[3] = { DAT_803dfa10, DAT_803dfa14, DAT_803dfa18 };
        int s = *(int *)(bnh + 0x7c);
        *(int *)(s + 0x34) = 2;
        mtx4_transform_vector((float *)(bnh + 0x274), wind);
        *(float *)(s + 0x10) = (float)((double)windScale * (double)wind[0]);
        *(float *)(s + 0x14) = (float)((double)windScale * (double)wind[1]);
        *(float *)(s + 0x18) = (float)((double)windScale * (double)wind[2]);
        bnh = FUN_8023c32c(*(int *)(bnh + 0x60) + 1);
    }
    __restore_gpr_28();
}


// ---------------------------------------------------------------------------
// 0x801f31b8  ekun_bridge_fragment_tick
// Per-frame physics for one 'BDG' bridge link. Ramps the spin drive (+0xe8 into
// +0xc0) with an occasional rng nudge; then on the swing state (+0xa0 of the +0x7c
// slot): state 1 = settle toward the anchor extent (+0xac), state 0/neg = random
// impulse scatter + bounce back toward the anchor (+0xa8..+0xb0). When the primary
// player is riding this link (view ctx +0xc04 == entity), rotate the player's eye
// offset by the accumulated spin through the sine table, re-apply the eye, and
// re-issue a periodic ride sound (0x4e1 + rand&3). Always sets update bit 2.
// ---------------------------------------------------------------------------
void ekun_bridge_fragment_tick(void)
{
    int entity = FUN_803826f0();
    int slot = *(int *)(entity + 0x7c);
    int view = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);

    if (DAT_806108e4 == 0) {
        FUN_8038273c();
        return;
    }

    // spin drive: occasional rng nudge to +0xe8, then integrate into +0xc0.
    unsigned int r0 = __we629_rng_counter_tick();
    if (((float)((double)CONCAT44(0x43300000, r0) - DOUBLE_80617880) / FLOAT_80617868 <= FLOAT_806178d0) ||
        ((*(int *)(slot + 0xa0) == 1) &&
         (r0 = __we629_rng_counter_tick(),
          (float)((double)CONCAT44(0x43300000, r0) - DOUBLE_80617880) / FLOAT_80617868 <= FLOAT_8061781c))) {
        unsigned int r1 = __we629_rng_counter_tick();
        float d = (float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80617880) / FLOAT_80617868;
        if (*(float *)(entity + 0xc0) <= FLOAT_806177f8) {
            *(float *)(entity + 0xe8) = *(float *)(entity + 0xe8) + d;
        } else {
            *(float *)(entity + 0xe8) = *(float *)(entity + 0xe8) - d;
        }
    }
    float invDt = FLOAT_8061780c /
        (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);
    *(float *)(entity + 0xc0) = *(float *)(entity + 0xe8) * invDt + *(float *)(entity + 0xc0);
    *(float *)(entity + 0xe8) = *(float *)(entity + 0xe8) * FLOAT_80617890;

    int state = *(int *)(slot + 0xa0);
    if (state == 1) {
        // settle: extend +0x10 toward the anchor extent (+0xac) then damp.
        *(float *)(slot + 0x10) = FLOAT_806177f8;
        if (*(float *)(entity + 0xac) <= *(float *)(entity + 0x68)) {
            *(int *)(slot + 0xa0) = 0;
        } else {
            if (*(float *)(entity + 0x68) < *(float *)(entity + 0xac) - FLOAT_80617828) {
                *(float *)(entity + 0xb8) = *(float *)(entity + 0xb8) + FLOAT_8061780c * invDt;
            }
            *(float *)(entity + 0xb4) = *(float *)(entity + 0xb4) * FLOAT_80617890;
            *(float *)(entity + 0xb8) = *(float *)(entity + 0xb8) * FLOAT_80617890;
            *(float *)(entity + 0xbc) = *(float *)(entity + 0xbc) * FLOAT_80617890;
            *(float *)(entity + 100)  = *(float *)(entity + 0xb4) * invDt + *(float *)(entity + 100);
            *(float *)(entity + 0x68) = *(float *)(entity + 0xb8) * invDt + *(float *)(entity + 0x68);
            *(float *)(entity + 0x6c) = *(float *)(entity + 0xbc) * invDt + *(float *)(entity + 0x6c);
            unsigned int r2 = __we629_rng_counter_tick();
            *(float *)(entity + 0xe8) =
                ((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c) *
                FLOAT_8061781c + *(float *)(entity + 0xe8);
        }
    } else if (state < 1 && -1 < state) {
        // scatter: random velocity kick, integrate, bounce back toward anchor.
        unsigned int ra = __we629_rng_counter_tick();
        float ax = FLOAT_80617828 * ((float)((double)CONCAT44(0x43300000, ra) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c);
        unsigned int rb = __we629_rng_counter_tick();
        float ay = FLOAT_80617828 * ((float)((double)CONCAT44(0x43300000, rb) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c);
        unsigned int rc = __we629_rng_counter_tick();
        float az = FLOAT_80617828 * ((float)((double)CONCAT44(0x43300000, rc) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c);
        *(float *)(entity + 0xb4) = ax * invDt + *(float *)(entity + 0xb4);
        *(float *)(entity + 0xb8) = ay * invDt + *(float *)(entity + 0xb8);
        *(float *)(entity + 0xbc) = az * invDt + *(float *)(entity + 0xbc);
        *(float *)(entity + 0xb4) = *(float *)(entity + 0xb4) * FLOAT_80617890;
        *(float *)(entity + 0xb8) = *(float *)(entity + 0xb8) * FLOAT_80617890;
        *(float *)(entity + 0xbc) = *(float *)(entity + 0xbc) * FLOAT_80617890;
        *(float *)(entity + 100)  = *(float *)(entity + 0xb4) * invDt + *(float *)(entity + 100);
        *(float *)(entity + 0x68) = *(float *)(entity + 0xb8) * invDt + *(float *)(entity + 0x68);
        *(float *)(entity + 0x6c) = *(float *)(entity + 0xbc) * invDt + *(float *)(entity + 0x6c);
        float dtCur = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860);
        *(float *)(slot + 0x10) = invDt * dtCur + *(float *)(slot + 0x10);

        float dx = *(float *)(entity + 100)  - *(float *)(entity + 0xa8);
        float dy = *(float *)(entity + 0x68) - *(float *)(entity + 0xac);
        float dz = *(float *)(entity + 0x6c) - *(float *)(entity + 0xb0);
        if ((FLOAT_80617828 < dz * dz + dx * dx + dy * dy) &&
            (*(float *)(entity + 0xbc) * (*(float *)(entity + 0xb0) - *(float *)(entity + 0x6c)) +
             *(float *)(entity + 0xb4) * (*(float *)(entity + 0xa8) - *(float *)(entity + 100)) +
             *(float *)(entity + 0xb8) * (*(float *)(entity + 0xac) - *(float *)(entity + 0x68)) < FLOAT_806177f8)) {
            *(float *)(entity + 0xb4) = -*(float *)(entity + 0xb4);
            *(float *)(entity + 0xb8) = -*(float *)(entity + 0xb8);
            *(float *)(entity + 0xbc) = -*(float *)(entity + 0xbc);
        }
        float fadeDt = FLOAT_8061780c /
            (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);
        float fadeCur = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860);
        *(float *)(slot + 0x9c) = -(fadeDt * fadeCur - *(float *)(slot + 0x9c));
        if (*(float *)(slot + 0x9c) < FLOAT_806177f8) {
            *(float *)(slot + 0x9c) = FLOAT_806177f8;
        }
    }

    // primary-player ride: rotate the eye offset by the spin, re-apply, ride sound.
    if ((*(int *)(view + 0xc04) == entity) &&
        (*(int *)(view + 0xc00) != **(int **)(entity + 0x1f0))) {
        float fx = *(float *)(entity + 0xf4);
        float px = *(float *)(entity + 100);
        float py = *(float *)(entity + 0x68);
        float pz = *(float *)(entity + 0x6c);
        float ex = *(float *)(entity + 0xec);
        float ey = *(float *)(entity + 0xf0);
        float dz = *(float *)(DAT_80611d74 + 0xb0) - fx;
        unsigned int a0 = (unsigned int)(int)(FLOAT_806178b4 * ((FLOAT_806178c0 * *(float *)(entity + 0xc0)) / FLOAT_80617820) + FLOAT_8061781c);
        unsigned int a1 = (unsigned int)(int)(FLOAT_806178b4 * ((FLOAT_806178c0 * *(float *)(entity + 0x104)) / FLOAT_80617820) + FLOAT_8061781c);
        *(float *)(DAT_80611d74 + 0xb0) =
            dz * (*(float *)((int)&DAT_80583228 + ((a0 + 0x200) * 4 & 0x1ffc)) -
                  *(float *)((int)&DAT_80583228 + ((a1 + 0x200) * 4 & 0x1ffc))) +
            *(float *)(DAT_80611d74 + 0xb0);
        *(float *)(DAT_80611d74 + 0xac) =
            dz * ((&DAT_80583228)[a0 & 0x7ff] - (&DAT_80583228)[a1 & 0x7ff]) +
            *(float *)(DAT_80611d74 + 0xac);
        *(float *)(DAT_80611d74 + 0xa8) = *(float *)(DAT_80611d74 + 0xa8) + px;
        *(float *)(DAT_80611d74 + 0xac) = *(float *)(DAT_80611d74 + 0xac) + py;
        *(float *)(DAT_80611d74 + 0xb0) = *(float *)(DAT_80611d74 + 0xb0) + pz;
        *(float *)(DAT_80611d74 + 0xa8) = *(float *)(DAT_80611d74 + 0xa8) - ex;
        *(float *)(DAT_80611d74 + 0xac) = *(float *)(DAT_80611d74 + 0xac) - ey;
        *(float *)(DAT_80611d74 + 0xb0) = *(float *)(DAT_80611d74 + 0xb0) - fx;
        player_set_eye_position(*(float *)(DAT_80611d74 + 0xa8),
            *(float *)(DAT_80611d74 + 0x98) + *(float *)(DAT_80611d74 + 0xac) + *(float *)(DAT_80611d74 + 0x90),
            *(float *)(DAT_80611d74 + 0xb0), DAT_80611d74);

        unsigned int rs = __we629_rng_counter_tick();
        float invDt2 = FLOAT_8061780c /
            (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);
        if ((float)((double)CONCAT44(0x43300000, rs) - DOUBLE_80617880) / FLOAT_80617868 <
            FLOAT_806178c4 * FLOAT_806108ec * invDt2) {
            float *wp = entity_get_world_position(entity);
            unsigned int rid = __we629_rng_counter_tick();
            unsigned char play[0x30];
            sound_init_play_params((int)(long long)play);
            play[0x28] = 1;
            *(int *)(play + 0x1c) = *(int *)&wp[0];
            *(int *)(play + 0x20) = *(int *)&wp[1];
            *(int *)(play + 0x24) = *(int *)&wp[2];
            *(int *)(play + 0x00) = (rid & 3) + 0x4e1;
            sound_play_request((int *)play);
        }
    }
    *(unsigned int *)(entity + 0x18) |= 2;

    FUN_8038273c();
}


// ---------------------------------------------------------------------------
// 0x801f39d0  ekun_bridge_ribbon_build
// Build the two ribbon quad-strip meshes stretched between consecutive 'BDG1'..
// 'BDG2' chain links. For each link it resolves the previous link's endpoint and
// the current link's transformed bone-offset endpoints (DAT_803dfa1c/34 base +
// half-extents), writes two 6-vertex ribbon segments into the entity's two render
// lists (+0x1f0 / +0x1e0), computes each segment's min/max AABB per axis, and
// drives the per-link motor 3D loop sound (0x4df) scaled by the contact speed
// (+0x9c). The first link with no previous uses default endpoints (bVar6 seed).
// ---------------------------------------------------------------------------
void ekun_bridge_ribbon_build(void)
{
    FUN_803826d8();
    bool haveBase = false;
    float baseAx = DAT_803dfa1c, baseAy = DAT_803dfa20, baseAz = DAT_803dfa24;
    float halfAx = DAT_803dfa28, halfAy = DAT_803dfa2c, halfAz = DAT_803dfa30;

    int entity = FUN_8023c32c(0x42444731);   // 'BDG1'
    if (entity == 0) {
        FUN_80382724();
        return;
    }
    int *node = *(int **)(*(int *)(entity + 4) + 4);
    unsigned int firstFlag = countLeadingZeros(1 - *(int *)(*(int *)(entity + 0x7c) + 0xa0));
    int pivot = node[*node * -0x30 + 0x1b];
    if (FLOAT_806177f8 == *(float *)(*(int *)(entity + 0x7c) + 0x9c)) {
        baseAx = *(float *)(entity + 100);
        baseAy = *(float *)(entity + 0x68);
        baseAz = *(float *)(entity + 0x6c);
    }

    int index = 0;
    while (entity != 0) {
        float baseBx = DAT_803dfa34, baseBy = DAT_803dfa38;
        float baseBz = *(float *)&DAT_803dfa3c;
        float halfBx = DAT_803dfa40, halfBy = DAT_803dfa44;
        float halfBz = *(float *)&DAT_803dfa48;
        int slot = *(int *)(entity + 0x7c);
        if (firstFlag >> 5 != 0) {
            *(float *)(slot + 0x9c) = FLOAT_806178d4;
        }
        int next = FUN_8023c32c(index + 0x42444732);   // 'BDG2' + index

        float rig[16];
        entity_build_local_matrix(rig, (void *)(long long)entity, 0, 0);
        float curAx = *(float *)(entity + 100);
        float curAy = *(float *)(entity + 0x68);
        float curAz = *(float *)(entity + 0x6c);
        float endA[3] = { *(float *)(pivot + 0x30), *(float *)(pivot + 0x34), FLOAT_806177f8 };
        mtx4_transform_vector(rig, endA);
        curAx += endA[0]; curAy += endA[1]; curAz += endA[2];
        float halfCur[3] = { *(float *)(pivot + 0x30), *(float *)(pivot + 0x34), *(float *)(pivot + 0x38) };
        halfBx = halfCur[0]; halfBy = halfCur[1]; halfBz = halfCur[2];   // (h[2] retained from pivot)
        mtx4_transform_vector(rig, halfCur);
        float halfBx2 = halfCur[0], halfBy2 = halfCur[1];

        float curBx, curBy, curBz, halfB2x, halfB2y, halfB2z, spinFrac;
        if (next == 0) {
            haveBase = true;
            baseBx = FLOAT_806177f8; baseBy = FLOAT_806177f8; baseBz = FLOAT_806178e4;
            curBx = FLOAT_806178d8; curBy = FLOAT_806178dc; curBz = FLOAT_806178e0;
            spinFrac = FLOAT_8061780c;
            halfB2x = halfBx2; halfB2y = halfBy2; halfB2z = FLOAT_806178e4;
        } else {
            int nslot = *(int *)(next + 0x7c);
            entity_build_local_matrix(rig, (void *)(long long)next, 0, 0);
            curBx = *(float *)(next + 100);
            curBy = *(float *)(next + 0x68);
            curBz = *(float *)(next + 0x6c);
            float endB[3] = { *(float *)(pivot + 0x30), *(float *)(pivot + 0x34), FLOAT_806177f8 };
            mtx4_transform_vector(rig, endB);
            curBx += endB[0]; curBz += endB[2]; curBy += endB[1];
            float halfN[3] = { baseBx, baseBy, *(float *)(pivot + 0x38) };
            mtx4_transform_vector(rig, halfN);
            halfB2x = halfN[0]; halfB2y = halfN[1]; halfB2z = halfN[2];
            spinFrac = (*(float *)(nslot + 0x9c) / FLOAT_806178c4) * FLOAT_8061781c;
        }
        if (firstFlag >> 5 != 0) {
            spinFrac = FLOAT_806177f8;
        }

        // write ribbon segment #1 into render list at +0x1e0/+0x1f0.
        int *rl = *(int **)(*(int *)(entity + 0x1f0) + *(int *)(entity + 0x1e0) * 4 + -8);
        float *rp = (float *)(rl + *rl * 6 + 9);
        rp[0] = FLOAT_806177f8; rp[1] = FLOAT_806177f8; rp[2] = FLOAT_806177f8;
        rp[3] = FLOAT_806177f8; rp[4] = FLOAT_806177f8; rp[5] = FLOAT_806177f8;
        *(float *)&rl[9]  = baseAx + halfAx;
        *(float *)&rl[10] = baseAy + halfAy;
        *(float *)&rl[0xb] = baseAz + halfAz;
        *(float *)&rl[0xf] = curAx + baseBx;
        *(float *)&rl[0x10] = curAy + baseBy;
        *(float *)&rl[0x11] = curAz + baseBz;
        *(float *)&rl[0x15] = curAx - baseBx;
        *(float *)&rl[0x16] = curAy - baseBy;
        *(float *)&rl[0x17] = curAz - baseBz;
        *(float *)&rl[0x1b] = baseAx - halfAx;
        *(float *)&rl[0x1c] = baseAy - halfAy;
        *(float *)&rl[0x1d] = baseAz - halfAz;
        *(float *)&rl[0xc] = FLOAT_806177f8; *(float *)&rl[0x12] = FLOAT_806177f8;
        *(float *)&rl[0x18] = FLOAT_806177f8; *(float *)&rl[0x1e] = FLOAT_806177f8;
        *(float *)&rl[0xd] = FLOAT_806177f8; *(float *)&rl[0x13] = FLOAT_806177f8;
        *(float *)&rl[0x19] = FLOAT_806177f8; *(float *)&rl[0x1f] = FLOAT_806177f8;
        // per-axis min/max AABB for the 4 ribbon corners.
        {
            float a = *(float *)&rl[9], b = *(float *)&rl[0xf], c = *(float *)&rl[0x15], d = *(float *)&rl[0x1b];
            float mn = a; if (b < mn) mn = b; if (c < mn) mn = c; if (d < mn) mn = d;
            float mx = a; if (b > mx) mx = b; if (c > mx) mx = c; if (d > mx) mx = d;
            *(float *)&rl[5] = mn; *(float *)&rl[7] = mx;
            a = *(float *)&rl[0xb]; b = *(float *)&rl[0x11]; c = *(float *)&rl[0x17]; d = *(float *)&rl[0x1d];
            mn = a; if (b < mn) mn = b; if (c < mn) mn = c; if (d < mn) mn = d;
            mx = a; if (b > mx) mx = b; if (c > mx) mx = c; if (d > mx) mx = d;
            *(float *)&rl[6] = mn; *(float *)&rl[8] = mx;
        }

        if (!haveBase) {
            if (spinFrac <= FLOAT_806177f8) {
                baseAx = curBx; baseAy = curBy; baseAz = curBz;
                halfAx = halfB2x; halfAy = halfB2y; halfAz = halfB2z;
                halfB2x = baseBx; halfB2y = baseBy; halfB2z = baseBz;
                curAx = curBx; curAy = curBy; curAz = curBz;
            } else {
                baseAx = (curAx + curBx) * FLOAT_8061781c;
                halfB2x = (halfB2x + baseBx) * FLOAT_8061781c;
                baseAy = (curAy + curBy) * FLOAT_8061781c;
                halfB2y = (halfB2y + baseBy) * FLOAT_8061781c;
                baseAz = (curAz + curBz) * FLOAT_8061781c;
                halfB2z = (halfB2z + baseBz) * FLOAT_8061781c;
                curBx = baseAx; curBy = baseAy; curBz = baseAz;
                halfAx = halfB2x; halfAy = halfB2y; halfAz = halfB2z;
            }
        }

        // write ribbon segment #2 into render list at +0x1e0/+0x1f0 (offset -4).
        rl = *(int **)(*(int *)(entity + 0x1f0) + *(int *)(entity + 0x1e0) * 4 + -4);
        rp = (float *)(rl + *rl * 6 + 9);
        rp[0] = FLOAT_806177f8; rp[1] = FLOAT_806177f8; rp[2] = FLOAT_806177f8;
        rp[3] = FLOAT_806177f8; rp[4] = FLOAT_806177f8; rp[5] = FLOAT_806177f8;
        *(float *)&rl[9]  = curAx + baseBx;
        *(float *)&rl[10] = curAy + baseBy;
        *(float *)&rl[0xb] = curAz + baseBz;
        *(float *)&rl[0xf] = curBx + halfB2x;
        *(float *)&rl[0x10] = curBy + halfB2y;
        *(float *)&rl[0x11] = curBz + halfB2z;
        *(float *)&rl[0x15] = curBx - halfB2x;
        *(float *)&rl[0x16] = curBy - halfB2y;
        *(float *)&rl[0x17] = curBz - halfB2z;
        *(float *)&rl[0x1b] = curAx - baseBx;
        *(float *)&rl[0x1c] = curAy - baseBy;
        *(float *)&rl[0x1d] = curAz - baseBz;
        *(float *)&rl[0xc] = FLOAT_806177f8; *(float *)&rl[0x12] = FLOAT_806177f8;
        *(float *)&rl[0x18] = FLOAT_806177f8; *(float *)&rl[0x1e] = FLOAT_806177f8;
        *(float *)&rl[0xd] = FLOAT_806177f8; *(float *)&rl[0x13] = FLOAT_806177f8;
        *(float *)&rl[0x19] = FLOAT_806177f8; *(float *)&rl[0x1f] = FLOAT_806177f8;
        {
            float a = *(float *)&rl[9], b = *(float *)&rl[0xf], c = *(float *)&rl[0x15], d = *(float *)&rl[0x1b];
            float mn = a; if (b < mn) mn = b; if (c < mn) mn = c; if (d < mn) mn = d;
            float mx = a; if (b > mx) mx = b; if (c > mx) mx = c; if (d > mx) mx = d;
            *(float *)&rl[5] = mn; *(float *)&rl[7] = mx;
            a = *(float *)&rl[0xb]; b = *(float *)&rl[0x11]; c = *(float *)&rl[0x17]; d = *(float *)&rl[0x1d];
            mn = a; if (b < mn) mn = b; if (c < mn) mn = c; if (d < mn) mn = d;
            mx = a; if (b > mx) mx = b; if (c > mx) mx = c; if (d > mx) mx = d;
            *(float *)&rl[6] = mn; *(float *)&rl[8] = mx;
        }

        // per-link motor 3D loop sound scaled by the contact-speed (+0x9c).
        float *wp = entity_get_world_position(entity);
        __we630_sound_update_3d_source(
            (double)(float)((double)FLOAT_80617828 * (double)(float)((double)*(float *)(slot + 0x9c) / (double)FLOAT_806178c4)),
            (double)FLOAT_80617838, (double)FLOAT_8061780c, 0x4df, (unsigned int *)(entity + 0x22c), wp, 0);

        entity = next;
        index += 1;
    }
    FUN_80382724();
}


// ---------------------------------------------------------------------------
// 0x801f43f0  ekun_spark_billboard_render
// Draw a 4-pass additive spark billboard between two endpoints (p1 = end ptr,
// p2 = start ptr, col, tex). The billboard half-extent rotates through the sine
// table over the 4 passes (rng-jittered). In score mode (DAT_80612614 == 5) the
// billboard is re-oriented camera-facing via two cross products (segment dir x
// DAT_803dfa4c up-axis, then re-normalised) before laying down the quad rails.
// (start/end come from the FUN_803826dc spill pair; extraout f1 = spark radius.)
// ---------------------------------------------------------------------------
void ekun_spark_billboard_render(float *endPtr, float *startPtr, unsigned int col, int tex)
{
    unsigned long long pair = FUN_803826dc();
    endPtr   = (float *)(unsigned int)(pair >> 0x20);
    startPtr = (float *)(unsigned int)pair;
    float radius = FLOAT_8061780c;   // spark radius surfaced in f1 by the ROM

    unsigned int r0 = __we629_rng_counter_tick();
    double vscroll = (double)((float)((double)CONCAT44(0x43300000, r0) - DOUBLE_80617880) / FLOAT_80617868);
    double vStart = (double)(float)((double)FLOAT_8061780c + vscroll);

    for (int pass = 0; pass < 4; ++pass) {
        unsigned int idx = (unsigned int)(FLOAT_806178b4 *
            (FLOAT_806178c0 * ((float)((double)CONCAT44(0x43300000, (unsigned int)pass ^ 0x80000000) - DOUBLE_80617860) * FLOAT_806178e8)) +
            FLOAT_8061781c);
        double hx = (double)(float)(radius * (&DAT_80583228)[idx & 0x7ff]);
        double hy = (double)(float)(radius * *(float *)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc)));
        float rx = FLOAT_806177f8;

        if (DAT_80612614 == 5) {
            // camera-facing: orient the billboard by the segment direction.
            double sy = (double)(endPtr[1] - startPtr[1]);
            double sx = (double)(endPtr[0] - startPtr[0]);
            double sz = (double)(endPtr[2] - startPtr[2]);
            double len2 = (double)(float)(sz * sz + (double)(float)(sx * sx + (double)(float)(sy * sy)));
            if ((double)FLOAT_806178b8 < len2) {
                double r = 1.0 / SQRT(len2);
                float k = (float)(r * -(double)((float)(FLOAT_8061781c * len2) * (float)(r * r) - FLOAT_80617844));
                double scale = (double)(FLOAT_8061780c * k * -((float)(FLOAT_8061781c * len2) * k * k - FLOAT_80617844));
                sx = (double)(float)(sx * scale);
                sy = (double)(float)(sy * scale);
                sz = (double)(float)(sz * scale);
            }
            double cx = (double)(float)(sz * (double)DAT_803dfa4c - (double)(float)(sx * (double)DAT_803dfa54));
            double cy = (double)(float)(sy * (double)DAT_803dfa54 - (double)(float)(sz * (double)DAT_803dfa50));
            double cz = (double)(float)(sx * (double)DAT_803dfa50 - (double)(float)(sy * (double)DAT_803dfa4c));
            float clen2 = (float)(cz * cz + (double)(float)(cy * cy + (double)(float)(cx * cx)));
            if (FLOAT_806178b8 < clen2) {
                float cr = 1.0f / SQRT(clen2);
                cr = cr * -(FLOAT_8061781c * clen2 * cr * cr - FLOAT_80617844);
                double cs = (double)(FLOAT_8061780c * cr * -(FLOAT_8061781c * clen2 * cr * cr - FLOAT_80617844));
                cy = (double)(float)(cy * cs);
                cx = (double)(float)(cx * cs);
                cz = (double)(float)(cz * cs);
            }
            double px = (double)(float)(sx * cx - (double)(float)(sy * cy)) * hy;
            rx = (float)(cy * hx + (double)(float)((double)(float)(sy * cz - (double)(float)(sz * cy)) * hy));
            hy = (double)(float)(cx * hx + (double)(float)((double)(float)(sz * cx - (double)(float)(sx * cz)) * hy));
            hx = (double)(float)(cz * hx + (double)(float)px);
        }

        double hxd = (double)rx;
        gx_begin_textured_batch(2, 0x92, 0, 4, tex);
        WE630_GXF((double)endPtr[0]   + hxd);
        WE630_GXF((double)endPtr[1]   + hy);
        WE630_GXF((double)endPtr[2]   + hx);
        WE630_GXU(col);
        WE630_GXF((float)vscroll); WE630_GXF(FLOAT_806177f8);
        WE630_GXF((double)startPtr[0] + hxd);
        WE630_GXF((double)startPtr[1] + hy);
        WE630_GXF((double)startPtr[2] + hx);
        WE630_GXU(col);
        WE630_GXF((float)vStart); WE630_GXF(FLOAT_806177f8);
        WE630_GXF((double)endPtr[0]   - hxd);
        WE630_GXF((double)endPtr[1]   - hy);
        WE630_GXF((double)endPtr[2]   - hx);
        WE630_GXU(col);
        WE630_GXF((float)vscroll); WE630_GXF(FLOAT_8061780c);
        WE630_GXF((double)startPtr[0] - hxd);
        WE630_GXF((double)startPtr[1] - hy);
        WE630_GXF((double)startPtr[2] - hx);
        WE630_GXU(col);
        WE630_GXF((float)vStart); WE630_GXF(FLOAT_8061780c);
        gx_end_batch();
    }
    FUN_80382728();
}


// ---------------------------------------------------------------------------
// 0x801f47f0  ekun_bridge_glow_render
// Composite the multi-layer sprite glow over the 'BDG1'..'BDG2' bridge chain.
// For each link it lays down 3 additive quad layers (untextured shell, tex 0x333
// noise, tex 0x33b scrolling) between the previous and current endpoints, then
// two ekun_spark_billboard_render glow rails (tex 0x33b) on each side. The layer
// widths pulse with the link's contact speed (+0x9c) through the sign-jittered
// FLOAT_80617828 curve. Runs only when the head link is not settled (state != 1)
// and either its extent field (+0x10) is over FLOAT_8061781c or a coin flip.
// ---------------------------------------------------------------------------
void ekun_bridge_glow_render(void)
{
    FUN_803826bc();
    bool haveBase = false;
    float baseAx = DAT_803dfa58, baseAy = DAT_803dfa5c, baseAz = DAT_803dfa60;
    float halfAx = DAT_803dfa64, halfAy = DAT_803dfa68, halfAz = DAT_803dfa6c;
    unsigned int coin0 = __we629_rng_counter_tick();
    float sign = (float)((double)CONCAT44(0x43300000, coin0 & 1) - DOUBLE_80617880);

    int entity = FUN_8023c32c(0x42444731);   // 'BDG1'
    if (entity == 0) {
        FUN_80382708();
        return;
    }
    int *node = *(int **)(*(int *)(entity + 4) + 4);
    int pivot = node[*node * -0x30 + 0x1b];
    if ((*(int *)(*(int *)(entity + 0x7c) + 0xa0) == 1) ||
        ((*(float *)(*(int *)(entity + 0x7c) + 0x10) < FLOAT_8061781c) &&
         ((coin0 = __we629_rng_counter_tick(), (coin0 & 1) != 0)))) {
        FUN_80382708();
        return;
    }

    gx_set_sub_material_id(2);
    gx_set_blend_mode(2);
    int index = 0;
    while ((entity != 0) || (!haveBase)) {
        float baseBx = DAT_803dfa70, baseBy = DAT_803dfa74;
        float baseBz = *(float *)&DAT_803dfa78;
        unsigned int tint0 = (unsigned int)DAT_803dfa7c;
        unsigned int tintA0 = (unsigned int)DAT_803dfa80;
        unsigned int tintB0 = (unsigned int)DAT_803dfa84;
        (void)baseBx; (void)baseBy; (void)baseBz; (void)tint0; (void)tintA0; (void)tintB0;

        float curAx, curAy, curAz, spinFrac;
        float ex, ey, ez;
        if (entity == 0) {
            haveBase = true;
            spinFrac = FLOAT_8061780c;
            ex = FLOAT_806177f8; ey = FLOAT_806177f8; ez = FLOAT_806178e4;
            curAz = FLOAT_806178e0; curAy = FLOAT_806178f0; curAx = FLOAT_806178ec;
        } else {
            float rig[16];
            entity_build_local_matrix(rig, (void *)(long long)entity, 0, 0);
            curAx = *(float *)(entity + 100);
            curAy = *(float *)(entity + 0x68);
            curAz = *(float *)(entity + 0x6c);
            float endA[3] = { *(float *)(pivot + 0x30), *(float *)(pivot + 0x34), FLOAT_806177f8 };
            mtx4_transform_vector(rig, endA);
            curAx += endA[0]; ez = *(float *)(pivot + 0x38); curAy += endA[1]; curAz += endA[2];
            spinFrac = *(float *)(*(int *)(entity + 0x7c) + 0x9c) / FLOAT_806178c4;
            float half[3] = { *(float *)(pivot + 0x30), *(float *)(pivot + 0x34), FLOAT_806177f8 };
            mtx4_transform_vector(rig, half);
            ex = half[0]; ey = half[1];
            float side[3] = { baseBx, baseBy, baseBz };
            mtx4_transform_vector(rig, side);
        }

        // pulse the two layer widths by the (clamped/signed) spin fraction.
        float w0, w1;
        if (spinFrac <= FLOAT_8061781c) {
            w0 = ((spinFrac <= FLOAT_806177f8) || (FLOAT_806178c8 <= spinFrac))
                 ? spinFrac * FLOAT_80617828 : sign * FLOAT_8061781c;
        } else {
            w0 = FLOAT_8061780c;
        }
        w1 = w0;
        float wA = FLOAT_80617878 * w0;
        float wB = FLOAT_806178f4 * w0;
        float wC = FLOAT_806178f8 * w1;
        (void)wA; (void)wB; (void)wC;

        double curBaseZ = (double)FLOAT_806178bc;
        double glowAx = (double)(baseAx + halfAx);
        double glowAy = (double)(baseAy + halfAy);
        double glowAz = (double)(baseAz + halfAz);
        double glowBx = (double)(baseAx - halfAx);
        double glowBy = (double)(baseAy - halfAy);
        double glowBz = (double)(baseAz - halfAz);
        double zoff = (double)curAz;

        for (int layer = 0; layer < 3; ++layer) {
            unsigned int cA = 0x7f7f7f00, cB = 0x7f7f7f00;
            int ltex = -1;
            float ustart = FLOAT_806177f8;
            float uend = FLOAT_8061780c;
            if (layer == 1) {
                ltex = sprite_get_texture_id(0);
                cA = ((unsigned int)(int)wB & 0xff) | 0x7f7f7f00;
                cB = ((unsigned int)(int)wA & 0xff) | 0x7f7f7f00;
            } else if (layer == 0) {
                ltex = sprite_get_texture_id(0x333);
                unsigned int rn = __we629_rng_counter_tick();
                uend = FLOAT_8061788c;
                cA = ((unsigned int)(int)w1 & 0xff) | 0x7f7f7f00;
                cB = ((unsigned int)(int)w0 & 0xff) | 0x7f7f7f00;
                ustart = (float)((double)CONCAT44(0x43300000, rn) - DOUBLE_80617880) / FLOAT_80617868;
            } else {
                float ft = game_time_to_frames();
                float invDt = FLOAT_8061780c /
                    (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);
                float scroll = invDt * ft * FLOAT_806178e8;
                ltex = sprite_get_texture_id(0x33b);
                ustart = -(float)((double)scroll - (double)(float)((double)(float)((double)CONCAT44(0x43300000, (int)scroll ^ 0x80000000) - DOUBLE_80617860)));
                unsigned int rn = __we629_rng_counter_tick();
                cA = ((unsigned int)(int)wC & 0xff) | 0x7f7f7f00;
                cB = ((unsigned int)(int)wB & 0xff) | 0x7f7f7f00;
                uend = (float)((double)CONCAT44(0x43300000, rn) - DOUBLE_80617880) / FLOAT_80617868;
            }
            zoff = (double)(float)(zoff - curBaseZ);
            gx_begin_textured_batch(2, 0x92, 0, 4, ltex);
            WE630_GXF((float)glowAx); WE630_GXF((float)glowAy); WE630_GXF((float)glowAz);
            WE630_GXU(cB); WE630_GXF(uend); WE630_GXF(ustart);
            WE630_GXF((float)(zoff + (double)ex)); WE630_GXF((float)((double)curAy + (double)ey)); WE630_GXF((float)((double)curAz + (double)ez));
            WE630_GXU(cB); WE630_GXF(uend); WE630_GXF((float)((double)uend + (double)ustart));
            WE630_GXF((float)glowBx); WE630_GXF((float)glowBy); WE630_GXF((float)glowBz);
            WE630_GXU(cA); WE630_GXF((float)((double)uend + (double)uend)); WE630_GXF(ustart);
            WE630_GXF((float)(zoff - (double)ex)); WE630_GXF((float)((double)curAy - (double)ey)); WE630_GXF((float)((double)curAz - (double)ez));
            WE630_GXU(cA); WE630_GXF((float)((double)uend + (double)uend)); WE630_GXF((float)((double)uend + (double)ustart));
            gx_end_batch();
        }

        // two additive spark glow rails (tex 0x33b) along each side of the segment.
        int gtex = sprite_get_texture_id(0x33b);
        float railStart[6];
        railStart[0] = baseAx + halfAx; railStart[1] = baseAy + halfAy; railStart[2] = baseAz + halfAz;
        railStart[3] = (float)(zoff + (double)ex); railStart[4] = (float)((double)curAy + (double)ey); railStart[5] = (float)((double)curAz + (double)ez);
        ekun_spark_billboard_render(&railStart[0], &railStart[3], 0x7f7f7f3f, gtex);
        railStart[0] = baseAx - halfAx; railStart[1] = baseAy - halfAy; railStart[2] = baseAz - halfAz;
        railStart[3] = (float)(zoff - (double)ex); railStart[4] = (float)((double)curAy - (double)ey); railStart[5] = (float)((double)curAz - (double)ez);
        ekun_spark_billboard_render(&railStart[0], &railStart[3], 0x7f7f7f3f, gtex);

        baseAx = (float)zoff; baseAy = curAy; baseAz = curAz;
        halfAx = ex; halfAy = ey; halfAz = ez;
        entity = FUN_8023c32c(index + 0x42444732);   // 'BDG2' + index
        index += 1;
    }
    FUN_80382708();
}


// ---------------------------------------------------------------------------
// 0x801f5018  ekun_flicker_sound_tick
// Random-flicker light tick: count the +0x1a4 timer down by the per-frame dt;
// on reaching zero toggle the render node's flag bit (+0x30 ^= 1) and re-arm the
// timer. Always drive the 0xc ambient 3D loop sound at the entity position,
// marking the sound-active bit 0x40 when a voice starts.
// ---------------------------------------------------------------------------
void ekun_flicker_sound_tick(int self)
{
    float rearm = FLOAT_8061780c;
    float zero  = FLOAT_806177f8;
    (void)zero;
    *(float *)(self + 0x1a4) = *(float *)(self + 0x1a4) -
        FLOAT_806108ec * (FLOAT_8061780c /
            (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860));
    if (*(float *)(self + 0x1a4) <= FLOAT_806177f8) {
        *(unsigned int *)(*(int *)(self + 4) + 0x30) ^= 1;
        *(float *)(self + 0x1a4) = rearm;
    }
    int started = __we630_sound_update_3d_source((double)FLOAT_8061780c, (double)FLOAT_80617888,
                    (double)FLOAT_8061780c, 0xc, (unsigned int *)(self + 0x22c),
                    (float *)(self + 0x64), 0);
    if (started != 0) {
        *(unsigned int *)(self + 0x18) |= 0x40;
    }
}

// ===========================================================================
// batch_631  (0x801f50d8 - 0x801f68a8)  EKUN 'PRIS'/'SECS' bridge-collapse
// set-piece: a swinging suspension bridge driven by two paired switches, whose
// walkway is a strip of debris fragments that flex, spark and tear away, plus a
// spinning drive motor and a per-fragment impact/detonate handler.
//
// Non-matching conventions (as established across batches 621-630):
//   * 4CC entity ids kept as raw hex (PRIS/SECS/011H).
//   * int<->double 0x43300000 magic-bias idioms reduced to plain (double)(int).
//   * FP-spill open/close frames (FUN_803826d8/80382724, FUN_803826f0/8038273c)
//     are opaque; the packed-register pair they return is read through the
//     existing file-local helpers.
//   * mtx4_set_translation's ROM 3-double form uses __we3_mtx4_set_translation3.
//   * rng_counter_tick / sound_update_3d_source / mtx4_pool_alloc are declared
//     `void` in generated_decls; their real return is read through the existing
//     __we629_rng_counter_tick / __we630_sound_update_3d_source /
//     __we627_mtx4_pool_alloc aliases.
//   * entity_raycast_room here takes the 9-arg (entityRoom-first) ROM shape,
//     distinct from the 8-arg entity_raycast_room_portals used elsewhere in this
//     file -> routed through the file-local __we631_entity_raycast_room alias.
// ===========================================================================

// --- batch_631 raw symbols not already declared earlier in this file ---
extern "C" {
    extern float  FLOAT_80617900;      // spark spin base angle
    extern float  FLOAT_80617904;      // strip shade (state 0/3)
    extern float  FLOAT_80617908;      // strip shade slope
    extern float  FLOAT_8061790c;      // vertex unpack scale
    extern float  FLOAT_80617910;      // fragment velocity kick coeff
    extern float  FLOAT_80617914;      // detonation sound volume
    extern float  FLOAT_806178fc;      // swing phase offset
    extern float  FLOAT_80617918;      // fragment impact proximity threshold (sq)
    extern float  FLOAT_8061791c;      // motor idle target
    extern float  FLOAT_80617920;      // motor arrive epsilon
    extern float  FLOAT_80617924;      // motor re-target dwell
    extern float  FLOAT_80617928;      // motor speed gain
    extern float  FLOAT_8061792c;      // motor speed low clamp
    extern float  FLOAT_80617930;      // motor speed high clamp
    extern float  FLOAT_80617934;      // motor speed->pitch normalizer
    extern float  FLOAT_80617938;      // motor loop ramp rate
    extern float  FLOAT_8061793c;      // motor loop volume clamp

    extern float  DAT_803dfa94;        // collapse dot weight x
    extern float  DAT_803dfa98;        // collapse dot weight z
    extern float  DAT_803dfa9c;        // collapse dot weight y (signed by state)
    extern float  DAT_803dfaa0;        // detonate offset x
    extern float  DAT_803dfaa4;        // detonate offset y
    extern float  DAT_803dfaa8;        // detonate offset z
    extern float  DAT_803dfaac;        // motor sound offset x
    extern float  DAT_803dfab0;        // motor sound offset y
    extern float  DAT_803dfab4;        // motor sound offset z

    extern float  DAT_806108ec;        // per-frame dt (raw, signed-bias form)
    extern int    DAT_80611824;        // switch descriptor table base
    extern int    DAT_80612758;        // bridge debris-strip pool base (stride 0xb8)

    // rng_counter_tick's LCG constants (already used elsewhere via bias idiom).
    extern int    DAT_806108e4;
    extern int    DAT_806109c0;

    // paired switch-transition phase clocks (BSS scratch).
    extern float  fRam80610e18;
    extern float  FLOAT_80610e14;
}

// entity_raycast_room here is the 9-argument (room-first) ROM variant; the
// generated_decls prototype is the different 8-arg origin-first shape, so bind
// the exact call shape locally to keep the collapse sweep matching.
static int (*const __we631_entity_raycast_room)(
    unsigned int room, float *origin, float *dir, int isInfinite,
    float *outHitPos, float *outHitNormal, unsigned int typeMask, int flags,
    int *extra) =
    (int (*)(unsigned int, float *, float *, int, float *, float *,
             unsigned int, int, int *))&entity_raycast_room;

// ---------------------------------------------------------------------------
// 0x801f50d8  ekun_bridge_switch_state_apply
// Advance a switch entity's +0x1a4 sway phase by the per-frame rate, wrapping at
// FLOAT_80617824, then latch the attached render node's state word (+0x30): 5
// when the switch is idle (+0x18 == 0), 2 when it is engaged.
// ---------------------------------------------------------------------------
void ekun_bridge_switch_state_apply(int self, int switchEnt)
{
    float wrap = FLOAT_80617824;
    float dt   = FLOAT_8061780c /
        (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);
    *(float *)(self + 0x1a4) = *(float *)(self + 0x1a4) + FLOAT_80617894 * FLOAT_806108ec * dt;
    if (wrap < *(float *)(self + 0x1a4)) {
        *(float *)(self + 0x1a4) = *(float *)(self + 0x1a4) - wrap;
    }
    if (*(int *)(switchEnt + 0x18) == 0) {
        *(unsigned int *)(*(int *)(self + 4) + 0x30) = 5;
    } else {
        *(unsigned int *)(*(int *)(self + 4) + 0x30) = 2;
    }
}

// ---------------------------------------------------------------------------
// 0x801f5168  ekun_bridge_switch_pair_resolve
// Resolve the combined open/close state of the two bridge switches (a, b) onto
// the anchor render node's state word (+0x30). Both engaged or both idle: OR in
// bit 3 (fully open). Exactly one engaged: run an rng-timed transition, biasing
// the state bit (1 or 2) and re-arming the matching FLOAT_80610e14 /
// fRam80610e18 phase clock from the per-frame dt.
// ---------------------------------------------------------------------------
void ekun_bridge_switch_pair_resolve(int self, int switchA, int switchB)
{
    unsigned int *state = (unsigned int *)(*(int *)(self + 4) + 0x30);
    *state = 0;
    int aOn = *(int *)(switchA + 0x18);
    int bOn = *(int *)(switchB + 0x18);

    if (((aOn == 0) || (bOn != 0)) && ((bOn == 0) || (aOn != 0))) {
        if ((aOn == 0) || (bOn == 0)) {
            // both idle -> fully open
            *state |= 3;
        } else if (FLOAT_806177f8 < fRam80610e18) {
            unsigned int r = __we629_rng_counter_tick();
            float num = FLOAT_8061780c;
            double bias = DOUBLE_80617860;
            *state |= r & 1;
            float rate = num / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - bias);
            fRam80610e18 = -(rate *
                (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - bias) - fRam80610e18);
        }
    } else if (FLOAT_80610e14 <= FLOAT_806177f8) {
        *state |= 1;
    } else {
        unsigned int r = __we629_rng_counter_tick();
        float num = FLOAT_8061780c;
        double bias = DOUBLE_80617860;
        *state |= r & 2;
        float rate = num / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - bias);
        FLOAT_80610e14 = -(rate *
            (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - bias) - FLOAT_80610e14);
    }
}

// ---------------------------------------------------------------------------
// 0x801f52f8  ekun_bridge_gate_pose_build
// Build the two swinging gate-leaf transforms from the first strip fragment's
// world position: an opening euler transform into bone slot +0x100 and its
// mirror into +0x140 (yaw scaled by the +0x1a4 sway phase), fold the inverse
// translation through both, reset the four intermediate bone matrices to
// identity, and flag the render node dirty (0x200000).
// ---------------------------------------------------------------------------
void ekun_bridge_gate_pose_build(int self)
{
    int *chain = *(int **)(*(int *)(self + 4) + 4);
    float *pos = (float *)chain[*chain * -0x30 + 0x1b];
    double px = (double)pos[0];
    double py = (double)pos[1];
    double pz = (double)pos[2];
    float yaw = (FLOAT_806178c0 * *(float *)(self + 0x1a4)) / FLOAT_80617820;
    int bones = *(int *)(*(int *)(self + 4) + 0x10);

    mtx4_set_transform_euler(px, py, pz,
        (double)FLOAT_806177f8, (double)yaw, (double)FLOAT_806177f8,
        (float *)(bones + 0x100));
    mtx4_set_transform_euler(px, py, pz,
        (double)yaw, (double)FLOAT_806177f8, (double)FLOAT_806177f8,
        (float *)(bones + 0x140));
    __we3_mtx4_set_translation3(-px, -py, -pz);

    float *leaf = (float *)(bones + 0x100);
    mtx4_multiply(leaf, leaf, leaf);
    leaf = (float *)(bones + 0x140);
    mtx4_multiply(leaf, leaf, leaf);

    mtx4_set_identity(bones + 0x40);
    mtx4_set_identity(bones + 0x80);
    mtx4_set_identity(bones + 0xc0);
    mtx4_set_identity(bones);
    *(unsigned int *)(self + 0x28) |= 0x200000;
}

// ---------------------------------------------------------------------------
// 0x801f546c  ekun_bridge_collapse_update
// Master per-frame driver for the bridge-collapse set-piece. Resolves the two
// paired switches ('PRIS' 0x50524953 / 'SECS' 0x53454353) through
// ekun_bridge_switch_state_apply + ekun_bridge_switch_pair_resolve, advances the
// anchor's +0x1a4 sway phase, then (if the level flag byte at +0xd2c is set)
// walks the 0x32-slot (0x14 in the reduced spectator mode) debris-strip pool at
// DAT_80612758 (stride 0xb8). Each active slot integrates its tear-timer +8;
// when it fires it kicks the fragment's velocity/position (rec[0..2], rec[6..8],
// rec[9..0xb]) with rng jitter, spawns a fading spark, snaps its swept endpoint
// to the cached face poly, and on the alternate branch re-seeds the fragment
// from an entity_raycast_room hit against the room geometry (unpacking the hit
// face's verts into rec[0x10..], recording vert count / winding in
// rec[0xd..0xf], and the hit distance in rec[0xc]). Finally drives the 0x4dd
// ambient 3D loop sound and marks the sound-active bit 0x40.
// ---------------------------------------------------------------------------
void ekun_bridge_collapse_update(void)
{
    int self = (int)FUN_803826d8();   // FP-spill open: anchor entity in the packed pair
    float dt = FLOAT_80617894 * FLOAT_806108ec *
        (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860));

    unsigned int *pris = (unsigned int *)switch_find_slot_by_entity(0x50524953);  // 'PRIS'
    unsigned int *secs = (unsigned int *)switch_find_slot_by_entity(0x53454353);  // 'SECS'
    ekun_bridge_switch_state_apply(pris[2], (int)pris);
    ekun_bridge_switch_state_apply(secs[2], (int)secs);
    ekun_bridge_switch_pair_resolve(self, (int)pris, (int)secs);

    float wrap = FLOAT_80617824;
    *(float *)(self + 0x1a4) = *(float *)(self + 0x1a4) + dt;
    if (wrap < *(float *)(self + 0x1a4)) {
        *(float *)(self + 0x1a4) = *(float *)(self + 0x1a4) - wrap;
    }

    if (*(char *)(DAT_80611d54 + *(int *)(self + 0x34) + 0xd2c) != '\0') {
        double num  = (double)FLOAT_8061780c;
        double zero = (double)FLOAT_806177f8;
        double bias = DOUBLE_80617860;
        int idx = 0;
        int off = 0;
        int count = 0x32;
        if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
            count = 0x14;   // reduced strip in the multiplayer/spectator mode
        }
        for (; idx < count; idx++, off += 0xb8) {
            float *rec = (float *)(DAT_80612758 + off);
            float dtCur = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - bias);
            float dtC2  = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - bias);
            rec[2] = -((float)(num / (double)dtCur) * dtC2 - rec[2]);

            if (zero < (double)rec[2]) {
                // --- active fragment: integrate + kick + spark + face snap ---
                unsigned int r0 = __we629_rng_counter_tick();
                float e0 = (float)((double)CONCAT44(0x43300000, r0) - DOUBLE_80617880);
                rec[1] = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - bias) *
                         (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - bias)) *
                         FLOAT_80617910 * (e0 / FLOAT_80617868 - FLOAT_8061781c) + rec[1];
                unsigned int r1 = __we629_rng_counter_tick();
                float e1 = (float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80617880);
                rec[0] = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - bias) *
                         (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - bias)) *
                         FLOAT_80617910 * (e1 / FLOAT_80617868 - FLOAT_8061781c) + rec[0];

                float scale = FLOAT_806178b4;
                (void)(long long)(int)(scale * rec[1] + FLOAT_8061781c);
                (void)(long long)(int)(scale * rec[0] + FLOAT_8061781c);
                (void)(long long)(int)(scale * ((FLOAT_806178c0 * FLOAT_8061788c * *(float *)(self + 0x1a4)) / FLOAT_80617820) + FLOAT_8061781c);
                spark_spawn_fading();
                spark_set_last_scale((double)FLOAT_8061780c);
                spark_set_last_color(0x7f5f7f6f);

                unsigned int rr0 = __we629_rng_counter_tick();
                double j0 = (double)(FLOAT_80617828 *
                    ((float)((double)CONCAT44(0x43300000, rr0) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c));
                unsigned int rr1 = __we629_rng_counter_tick();
                double j1 = (double)(FLOAT_80617828 *
                    ((float)((double)CONCAT44(0x43300000, rr1) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c));
                unsigned int rr2 = __we629_rng_counter_tick();
                double bias2 = DOUBLE_80617860;
                float num2 = FLOAT_8061780c;
                double mag = (double)(FLOAT_80617888 *
                    (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860)) *
                    (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860));
                float jz = (float)((double)(FLOAT_80617828 *
                    ((float)((double)CONCAT44(0x43300000, rr2) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c)) * mag);
                float dot = -(rec[5] * jz + rec[3] * (float)(j0 * mag) + rec[4] * (float)(j1 * mag));
                rec[9]  = rec[9]  + rec[3] * dot + (float)(j0 * mag);
                rec[10] = rec[10] + rec[4] * dot + (float)(j1 * mag);
                rec[11] = rec[11] + rec[5] * dot + jz;
                rec[6] = rec[9]  * (num2 / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - bias2)) *
                         (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - bias2) + rec[6];
                rec[7] = rec[10] * (num2 / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - bias2)) *
                         (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - bias2) + rec[7];
                rec[8] = rec[11] * (num2 / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - bias2)) *
                         (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - bias2) + rec[8];

                // face-plane snap: clear the tear-timer if the swept endpoint
                // crosses any edge of the cached face polygon.
                int vi = (int)rec[0xe];
                int wi = (int)rec[0xf];
                float *prev = rec + ((int)rec[0xd] - 1) * 3 + 0x10;
                int vo = 0;
                for (int k = 0; k < (int)rec[0xd]; k++) {
                    float *cur = (float *)((int)rec + vo + 0x40);
                    if (FLOAT_806177f8 <
                        (rec[vi + 6] - prev[vi]) * (cur[wi] - prev[wi]) +
                        (rec[wi + 6] - prev[wi]) * (prev[vi] - cur[vi])) {
                        rec[2] = FLOAT_806177f8;
                    }
                    vo += 0xc;
                    prev = cur;
                }
            } else {
                // --- dormant fragment: re-seed dir + raycast against the room ---
                unsigned int a0 = __we629_rng_counter_tick();
                rec[1] = FLOAT_806178c0 * ((float)((double)CONCAT44(0x43300000, a0) - DOUBLE_80617880) / FLOAT_80617868) - FLOAT_806178fc;
                unsigned int a1 = __we629_rng_counter_tick();
                rec[0] = FLOAT_806178b0 * ((float)((double)CONCAT44(0x43300000, a1) - DOUBLE_80617880) / FLOAT_80617868) - FLOAT_806178c0;
                unsigned int a2 = __we629_rng_counter_tick();
                float sc = FLOAT_806178b4;
                unsigned int iu = (unsigned int)(sc * rec[1] + FLOAT_8061781c);
                unsigned int ju = (unsigned int)(sc * rec[0] + FLOAT_8061781c);
                rec[2] = FLOAT_80617840 * ((float)((double)CONCAT44(0x43300000, a2) - DOUBLE_80617880) / FLOAT_80617868);

                float sinScale = FLOAT_80617900;
                float sinI = sinScale * *(float *)((int)&DAT_80583228 + ((iu + 0x200) * 4 & 0x1ffc)) *
                             (float)(&DAT_80583228)[ju & 0x7ff];
                float sinJ = sinScale * (float)(&DAT_80583228)[iu & 0x7ff];
                float sinK = sinScale * *(float *)((int)&DAT_80583228 + ((iu + 0x200) * 4 & 0x1ffc)) *
                             *(float *)((int)&DAT_80583228 + ((ju + 0x200) * 4 & 0x1ffc));

                float castStart[3];
                castStart[0] = sinI + *(float *)(self + 100)  + *(float *)(self + 0x1a8);
                castStart[2] = sinJ + *(float *)(self + 0x6c) + *(float *)(self + 0x1b0);
                unsigned int ku = (unsigned int)(sc * ((FLOAT_806178c0 * FLOAT_8061788c * *(float *)(self + 0x1a4)) / FLOAT_80617820) + FLOAT_8061781c);
                castStart[1] = (float)(&DAT_80583228)[ku & 0x7ff] * FLOAT_8061781c +
                               sinK + *(float *)(self + 0x68) + *(float *)(self + 0x1ac);

                float castDir[3] = { sinI, sinJ, sinK };
                int state = *(int *)(*(int *)(self + 4) + 0x30);
                if (state == 3) {
                    castDir[0] = sinI * FLOAT_80617904;
                    castDir[2] = sinK * FLOAT_80617904;
                    castDir[1] = sinJ * FLOAT_80617904;
                } else if (state == 0) {
                    castDir[0] = sinI * FLOAT_80617888;
                    castDir[2] = sinK * FLOAT_80617888;
                    castDir[1] = sinJ * FLOAT_80617888;
                } else {
                    float wY = DAT_803dfa9c;
                    if (state == 1) {
                        wY = -DAT_803dfa9c;
                    }
                    float d = wY * sinJ + DAT_803dfa94 * sinI + DAT_803dfa98 * sinK;
                    if (d <= FLOAT_806177f8) {
                        castDir[0] = sinI * FLOAT_80617904;
                        castDir[2] = sinK * FLOAT_80617904;
                        castDir[1] = sinJ * FLOAT_80617904;
                    } else {
                        float s = d * FLOAT_80617908 + FLOAT_80617904;
                        castDir[0] = sinI * s;
                        castDir[2] = sinK * s;
                        castDir[1] = sinJ * s;
                    }
                }

                int room = *(int *)(self + 0x34);
                int hitEnt = __we631_entity_raycast_room((unsigned int)room, castStart, castDir, 0,
                                 rec + 6, rec + 3, 0xfffffffd, 0, (int *)0);
                if (hitEnt == 0) {
                    rec[2] = FLOAT_806177f8;
                } else {
                    int faceInfo[4];
                    collision_result_restore((int)(long long)faceInfo);
                    int faceIdx = faceInfo[3];   // local_13c in the ROM frame
                    int vertBase = faceInfo[0];  // local_130
                    int *mesh = *(int **)(*(int *)(hitEnt + 4) + 4);
                    int base = *mesh;
                    int posArr = *(int *)(mesh[base * -0x30 + faceIdx * 0x30 + 0x27] + 0x2c);
                    char *fdesc = (char *)(*(int *)(mesh[base * -0x30 + faceIdx * 0x30 + 0x27] + 0x1c) + vertBase * 0xe);
                    unsigned int cls = (unsigned char)fdesc[4] & 3;
                    unsigned short idxBase = *(unsigned short *)(fdesc + 8);
                    int stream = mesh[base * -0x30 + faceIdx * 0x30 + ((unsigned char)fdesc[4] & 3) + 0x15] +
                                 (unsigned int)*(unsigned short *)(fdesc + 6) * 8;
                    int vo2 = 0, io = 0;
                    int render = *(int *)(*(int *)(hitEnt + 4) + 4);
                    for (int v = 0; v < (int)*fdesc; v++) {
                        int vidx = (int)*(short *)(posArr + (unsigned int)idxBase * 2 + io);
                        if ((*(unsigned int *)(render + 0xc) & 0x10) == 0) {
                            unsigned int *src = (unsigned int *)(mesh[base * -0x30 + faceIdx * 0x30 + cls * 5 + 1] +
                                ((unsigned int)*(unsigned short *)(stream + 6) + vidx) * 0xc);
                            *(unsigned int *)((int)rec + vo2 + 0x40) = src[0];
                            *(unsigned int *)((int)rec + vo2 + 0x44) = src[1];
                            *(unsigned int *)((int)rec + vo2 + 0x48) = src[2];
                        } else {
                            short *src = (short *)(mesh[base * -0x30 + faceIdx * 0x30 + cls * 5 + 1] +
                                ((unsigned int)*(unsigned short *)(stream + 6) + vidx) * 6);
                            float vs = FLOAT_8061790c;
                            *(float *)((int)rec + vo2 + 0x40) =
                                vs * (float)((double)(CONCAT44(0x43300000, (int)src[0]) ^ 0x80000000) - DOUBLE_80617860) + *(float *)(render + 0x2c);
                            *(float *)((int)rec + vo2 + 0x44) =
                                vs * (float)((double)(CONCAT44(0x43300000, (int)src[1]) ^ 0x80000000) - DOUBLE_80617860) + *(float *)(render + 0x30);
                            *(float *)((int)rec + vo2 + 0x48) =
                                vs * (float)((double)(CONCAT44(0x43300000, (int)src[2]) ^ 0x80000000) - DOUBLE_80617860) + *(float *)(render + 0x34);
                        }
                        vo2 += 0xc;
                        io += 2;
                    }
                    rec[0xd] = (float)(int)*fdesc;
                    rec[0xe] = (float)(unsigned int)((unsigned char)fdesc[4] >> 6);
                    rec[0xf] = (float)((unsigned char)fdesc[4] >> 4 & 3);
                    double len = math_sqrt((double)(
                        (castStart[2] - rec[8]) * (castStart[2] - rec[8]) +
                        (castStart[0] - rec[6]) * (castStart[0] - rec[6]) +
                        (castStart[1] - rec[7]) * (castStart[1] - rec[7])));
                    rec[0xc] = (float)len;
                }

                unsigned int b0 = __we629_rng_counter_tick();
                rec[9]  = FLOAT_80617828 * ((float)((double)CONCAT44(0x43300000, b0) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c);
                unsigned int b1 = __we629_rng_counter_tick();
                rec[10] = FLOAT_80617828 * ((float)((double)CONCAT44(0x43300000, b1) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c);
                unsigned int b2 = __we629_rng_counter_tick();
                float num3 = FLOAT_8061780c;
                rec[11] = FLOAT_80617828 * ((float)((double)CONCAT44(0x43300000, b2) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c);
                rec[9]  = rec[9]  * num3;
                rec[10] = rec[10] * num3;
                rec[11] = rec[11] * num3;
                float dot = -(rec[5] * rec[11] + rec[3] * rec[9] + rec[4] * rec[10]);
                rec[9]  = rec[3] * dot + rec[9];
                rec[10] = rec[4] * dot + rec[10];
                rec[11] = rec[5] * dot + rec[11];
            }
        }
    }

    int started = __we630_sound_update_3d_source(
        (double)FLOAT_80617844, (double)FLOAT_8061785c, (double)FLOAT_8061780c,
        0x4dd, (unsigned int *)(self + 0x22c), (float *)(self + 100), 0);
    if (started != 0) {
        *(unsigned int *)(self + 0x18) |= 0x40;
    }
    FUN_80382724();   // FP-spill close
}

// ---------------------------------------------------------------------------
// 0x801f6164  ekun_bridge_swing_pose_build
// Build the bridge deck's swinging pose: a Y-rotation from the +0x1a4 sway phase
// into bone slot +0x40 (optionally reoriented onto the parent's orthonormal
// frame when +0x80 is linked), a vertical bob offset from math_poly_trig of the
// phase, then fold the translation through and mirror slot +0x40 into the four
// downstream bone slots (+0x80/+0xc0/+0x100/+0x140 and the +0x10 copy). Flag the
// render node dirty (0x200000).
// ---------------------------------------------------------------------------
void ekun_bridge_swing_pose_build(int self)
{
    double bob = math_poly_trig((double)((FLOAT_806178c0 * FLOAT_8061788c * *(float *)(self + 0x1a4)) / FLOAT_80617820 - FLOAT_806178fc));
    bob = (double)(float)(bob * (double)FLOAT_8061781c);
    int bones = *(int *)(*(int *)(self + 4) + 0x10);

    mtx4_set_rotation_y((double)((FLOAT_806178c0 * *(float *)(self + 0x1a4)) / FLOAT_80617820),
                        (float *)(bones + 0x40));
    if (*(unsigned int *)(self + 0x80) != 0) {
        subobj_build_orthonormal_matrix(*(unsigned int *)(self + 0x80),
                                        (float *)(bones + 0x40), (float *)0);
    }
    __we3_mtx4_set_translation3((double)FLOAT_806177f8, bob, (double)FLOAT_806177f8);
    float *rot = (float *)(bones + 0x40);
    mtx4_multiply(rot, rot, rot);

    bones = *(int *)(*(int *)(self + 4) + 0x10);
    mtx4_copy((float *)(bones + 0x40), (float *)(bones + 0x80));
    bones = *(int *)(*(int *)(self + 4) + 0x10);
    mtx4_copy((float *)(bones + 0x40), (float *)(bones + 0xc0));
    bones = *(int *)(*(int *)(self + 4) + 0x10);
    mtx4_copy((float *)(bones + 0x40), (float *)(bones + 0x100));
    float *b0 = *(float **)(*(int *)(self + 4) + 0x10);
    mtx4_copy(b0 + 0x10, b0);
    bones = *(int *)(*(int *)(self + 4) + 0x10);
    mtx4_copy((float *)(bones + 0x40), (float *)(bones + 0x140));
    mtx4_set_identity(*(int *)(*(int *)(self + 4) + 0x10));
    *(unsigned int *)(self + 0x28) |= 0x200000;
}

// ---------------------------------------------------------------------------
// 0x801f62b4  ekun_bridge_fragment_detonate
// Detonate a torn bridge fragment: spawn the 0x21/0x17 explosion at its render
// origin (+0x19*4) with the DAT_803dfaa0 offset, clear the linked subobject's
// physics bit (+0x20 -> 0x4000000), stop its update/render registration, run the
// FP-spill event tail (FUN_80221074), play the stereo detonation sound, and
// destroy the fragment entity.
// ---------------------------------------------------------------------------
void ekun_bridge_fragment_detonate(double a1, double a2, double a3, double a4,
                                   double a5, double a6, double a7, double a8,
                                   unsigned int *frag)
{
    float off[3] = { DAT_803dfaa0, 0.0f, 0.0f };
    *(int *)&off[1] = *(int *)&DAT_803dfaa4;
    *(int *)&off[2] = *(int *)&DAT_803dfaa8;
    player_ai_explosion_spawn(0x21, 0x17, (float *)(frag + 0x19), off, 0, (int)frag, 1);
    if (frag[0x20] != 0) {
        subobj_clear_flag_bits(frag[0x20], 0x4000000);
    }
    entity_register_for_update((int)frag, 3, -0x7fe09ea0);
    entity_register_for_update((int)frag, 2, 0);
    entity_set_render_type((int)frag, 0);
    FUN_80221074((double)FLOAT_8061780c, a2, a3, a4, a5, a6, a7, a8);
    sound_play_stereo_pair((double)FLOAT_80617814, (double)FLOAT_8061780c, (double)FLOAT_80617914);
    entity_destroy_with_event((int *)frag);
}

// ---------------------------------------------------------------------------
// 0x801f6388  ekun_bridge_fragment_impact_check
// Per-frame proximity handler for a hanging bridge fragment (event 0x17 stage
// 0xe, or the -1/-1 idle sentinel). When the anchor entity (+0x8, class '011H'
// 0x31313048) is within FLOAT_80617918 of the fragment's world centre, count
// down its +0x73 sag timer by the +0x20 rate; once it goes negative, detonate
// the fragment (ekun_bridge_fragment_detonate) and, if the anchor is still a
// live '011H', destroy it too.
// ---------------------------------------------------------------------------
void ekun_bridge_fragment_impact_check(unsigned int *ev)
{
    int stage = (int)ev[6];
    unsigned int *frag = (unsigned int *)ev[0];
    if ((((stage == 0x17) && (ev[5] == 0xe)) || ((stage == -1) && (ev[5] == (unsigned int)-1))) &&
        ((stage != -1) || ((ev[2] != 0) && (*(int *)(ev[2] + 0x60) == 0x31313048))) ) {
        if (frag != 0) {
            float *anchor = (float *)ev[10];
            float dy = ((float)frag[0x1a] + (float)frag[0x6b]) - anchor[1];
            double dx = (double)(((float)frag[0x19] + (float)frag[0x6a]) - anchor[0]);
            float dz = (float)((double)((float)frag[0x1b] + (float)frag[0x6c]) - (double)anchor[2]);
            if (ABS(dz * dz + (float)(dx * dx + (double)(dy * dy))) <= FLOAT_80617918) {
                float t = (float)frag[0x73];
                *(float *)&frag[0x73] = (float)((double)t - (double)(float)ev[8]);
                if ((double)*(float *)&frag[0x73] < (double)FLOAT_806177f8) {
                    ekun_bridge_fragment_detonate((double)*(float *)&frag[0x73], (double)t,
                        (double)((float)frag[0x1b] + (float)frag[0x6c]), dx, 0, 0, 0, 0, frag);
                    unsigned int *anchorEnt = (unsigned int *)ev[2];
                    if ((anchorEnt != 0) && (anchorEnt[0x18] == 0x31313048)) {
                        entity_destroy_with_event((int *)anchorEnt);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801f64b4  ekun_bridge_motor_drive_tick
// Drive the bridge's winch motor. When its +0x1a4 dwell timer expires, either
// re-pick a fresh random target angle (+0xcc) and dwell (near the current target
// or fully stopped), or step toward the pending target angle, converting the
// remaining error into a sine-shaped speed (clamped FLOAT_8061792c..30) that
// updates the shared drive scalar (+0xd4). Smooth the loop volume toward the
// target with scalar_step_toward_snap, then drive the 0x4e6 3D motor loop sound
// (pitch scaled by the smoothed level) and mark the sound-active bit 0x40.
// ---------------------------------------------------------------------------
void ekun_bridge_motor_drive_tick(int self)
{
    float zero = FLOAT_806177f8;
    int node = *(int *)(self + 4);
    float sndOff[3] = { DAT_803dfaac, 0.0f, 0.0f };
    *(int *)&sndOff[1] = *(int *)&DAT_803dfab0;
    *(int *)&sndOff[2] = *(int *)&DAT_803dfab4;
    double loopTarget = (double)FLOAT_8061791c;
    float dt = FLOAT_806108ec *
        (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860));
    *(float *)(node + 0x100) = FLOAT_806177f8;
    float snapRate = FLOAT_80617934;

    if (*(float *)(self + 0x1a4) <= zero) {
        float ref = *(float *)(*(int *)(DAT_80611824 + *(int *)(node + 0xd0) * 4) + 0x18);
        double cur = (double)(*(float *)(node + 0xd4) / ref);
        if ((ABS((float)(cur - (double)*(float *)(self + 0xcc))) < FLOAT_80617920) ||
            (zero == *(float *)(self + 0xd0))) {
            // re-target: pick a fresh random destination angle + dwell
            unsigned int r0 = __we629_rng_counter_tick();
            float sgn = (float)((double)CONCAT44(0x43300000, r0) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c;
            unsigned int r1 = __we629_rng_counter_tick();
            double dst = (double)((float)((unsigned int)sgn & 0x80000000 | 0x3f800000) *
                (FLOAT_8061783c * ((float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80617880) / FLOAT_80617868) + FLOAT_806178bc) +
                *(float *)(self + 0xcc));
            double wrapped = FUN_80390324((double)((float)(dst - (double)FLOAT_806177f8) / FLOAT_8061780c));
            *(float *)(self + 0xcc) = -(float)((double)FLOAT_8061780c * (double)(float)wrapped - dst);
            *(float *)(self + 0xd0) = ABS((float)((double)*(float *)(self + 0xcc) - cur));
            unsigned int r2 = __we629_rng_counter_tick();
            *(float *)(self + 0x1a4) =
                FLOAT_80617924 * ((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80617880) / FLOAT_80617868) + FLOAT_80617828;
        } else {
            // step toward target: error -> sine speed -> shared drive scalar
            unsigned int su = (unsigned int)(FLOAT_806178b4 * FLOAT_806178c0 *
                (ABS((float)((double)*(float *)(self + 0xcc) - cur)) / *(float *)(self + 0xd0)) + FLOAT_8061781c);
            float speed = FLOAT_80617928 * (float)(&DAT_80583228)[su & 0x7ff];
            float clamped = speed;
            if (FLOAT_80617930 < clamped) clamped = FLOAT_80617930;
            float lo = FLOAT_8061792c;
            if ((FLOAT_8061792c <= clamped) && (FLOAT_80617930 < speed)) {
                lo = FLOAT_80617930;
            } else if (FLOAT_8061792c <= clamped) {
                lo = speed;
            }
            float step = (float)((double)(lo * dt) *
                (double)(float)((unsigned int)(float)((double)*(float *)(self + 0xcc) - cur) & 0x80000000 | 0x3f800000) + cur);
            float upper = step;
            if (FLOAT_8061780c < upper) upper = FLOAT_8061780c;
            float lower = FLOAT_806177f8;
            if ((FLOAT_806177f8 <= upper) && (FLOAT_8061780c < step)) {
                lower = FLOAT_8061780c;
            } else if (FLOAT_806177f8 <= upper) {
                lower = step;
            }
            *(float *)(node + 0xd4) = lower * ref;
            loopTarget = (double)((lo - FLOAT_8061792c) / snapRate);
        }
    } else {
        *(float *)(self + 0x1a4) = *(float *)(self + 0x1a4) - dt;
    }

    scalar_step_toward_snap(loopTarget, (double)(FLOAT_80617938 * FLOAT_806108ec), (float *)(self + 0xe0));
    float vol = *(float *)(self + 0xe0);
    float vc = vol;
    if (FLOAT_8061781c < vc) vc = FLOAT_8061781c;
    float volClamp = FLOAT_8061793c;
    if ((FLOAT_8061793c <= vc) && (FLOAT_8061781c < vol)) {
        volClamp = FLOAT_8061781c;
    } else if (FLOAT_8061793c <= vc) {
        volClamp = vol;
    }
    int started = __we630_sound_update_3d_source(
        (double)(FLOAT_8061788c * volClamp), (double)FLOAT_8061785c, (double)FLOAT_8061780c,
        0x4e6, (unsigned int *)(self + 0x22c), sndOff, 0);
    if (started != 0) {
        sound_set_pitch((double)(FLOAT_8061781c * *(float *)(self + 0xe0) + FLOAT_8061781c),
                        *(unsigned int *)(self + 0x22c));
        *(unsigned int *)(self + 0x18) |= 0x40;
    }
}

// ---------------------------------------------------------------------------
// 0x801f684c  ekun_bridge_face_matrix_install
// Install a single collision face matrix for a bridge deck plank: open the
// FP-spill context (FUN_803826f0 -> packed {faceEnt, node} pair), allocate a
// pool matrix (__we627_mtx4_pool_alloc), lay in the plank's X translation from
// the node data, register it as the face's collision entry, and close the frame.
// ---------------------------------------------------------------------------
void ekun_bridge_face_matrix_install(void)
{
    unsigned long long ctx = FUN_803826f0();
    int mtx = __we627_mtx4_pool_alloc();
    unsigned int *lo = (unsigned int *)(unsigned int)ctx;
    __we3_mtx4_set_translation3((double)(float)lo[1], (double)FLOAT_806177f8, (double)FLOAT_806177f8);
    collision_set_face_entry(*(unsigned int *)((int)(ctx >> 0x20) + 4), *(unsigned int *)(unsigned int)ctx, mtx);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801f68a8  ekun_phase_timer_wrap
// Advance a small phase accumulator (+0x4) by its per-frame rate (+0x8 * dt) and
// wrap it into [0, FLOAT_8061780c): subtract a full period when it overshoots the
// top, add one back when it drops below zero.
// ---------------------------------------------------------------------------
void ekun_phase_timer_wrap(int self)
{
    float period = FLOAT_8061780c;
    *(float *)(self + 4) = FLOAT_806108ec * *(float *)(self + 8) + *(float *)(self + 4);
    float v = *(float *)(self + 4);
    if (period < v) {
        *(float *)(self + 4) = v - period;
        return;
    }
    if (FLOAT_806177f8 <= v) {
        return;
    }
    *(float *)(self + 4) = v + period;
}

// ===========================================================================
// batch_632  (0x801f68f0 - 0x801f7420)  EKUN 'KT' kite/tail-flap prop: a
// flapping kite/banner whose two step-drivers advance a phased flap animation,
// a per-frame pose tick drives the body along a rotating offset vector, an
// anim-event dispatcher sequences the flap phases by 4CC tag (BKTL/BKTR/FKTL/
// RKTF), and small helpers spawn the line loop sound, follow the tail tip, and
// run a vertex-colour flash fade.
//
// Non-matching conventions (as established across batches 621-631):
//   * 4CC entity ids kept as raw hex (BKTL/BKTR/FKTL/RKTF/BTKB/... , INTL, FT*).
//   * int<->double 0x43300000 magic-bias idioms reduced to plain (double)(int).
//   * FP-spill open/close frames (FUN_803826f0/8038273c) are opaque; the packed
//     register pair they return is read through the existing helpers.
//   * ekun_phase_timer_wrap (batch_631, 0x801f68a8) is reused as the shared
//     [0,period) phase accumulator.
//   * entity_apply_vertex_color_offset_full's ROM call here is the 10-argument
//     (entity-first) shape, distinct from the 8-arg generated_decls prototype ->
//     routed through the file-local __we632_vertex_color_offset alias.
// ===========================================================================

// --- batch_632 raw symbols not already declared earlier in this file ---
extern "C" {
    extern float  FLOAT_80617940;      // flap phase accel coeff
    extern float  FLOAT_80617944;      // RKTF flap threshold
    extern float  FLOAT_80617948;      // tail-line release threshold
    extern float  FLOAT_80617958;      // line-sound param a
    extern float  FLOAT_8061795c;      // line-sound param b

    extern float  DAT_803dfab8;        // body offset x
    extern float  DAT_803dfabc;        // body offset y
    extern float  DAT_803dfac0;        // body offset z
}

// entity_apply_vertex_color_offset_full's ROM shape here is 10-arg
// (entity-first); the generated_decls prototype is the different 8-arg form, so
// bind the exact call shape locally to keep the flash fade matching.
static void (*const __we632_vertex_color_offset)(
    int entity, int mode, char r, char g, char b, char a,
    int rOv, int gOv, int bOv, unsigned char aOv) =
    (void (*)(int, int, char, char, char, char, int, int, int, unsigned char))
    &entity_apply_vertex_color_offset_full;

// ---------------------------------------------------------------------------
// 0x801f68f0  ekun_kite_flap_step_a
// Flap-phase step driver (angle field +1). When the event's entity matches the
// kite's controller (+0x7c[0]), integrate the flap angle (+1) by the signed
// per-frame rate, seed the two sine-offset fields (+0xc / +0xf) from the
// symmetric +/- phase, wrap the two sub-phase timers, and advance the sequence
// counter (+0) when the angle crosses the threshold in the requested direction
// (dir 1: rising past param; dir -1: falling past param). Returns 1 on match.
// ---------------------------------------------------------------------------
unsigned int ekun_kite_flap_step_a(double thresh, int ev, unsigned int dir, int matchId)
{
    double bias = DOUBLE_80617860;
    float num  = FLOAT_8061780c;
    float amp  = FLOAT_80617808;
    int *ctl = *(int **)(ev + 0x7c);
    if (matchId != *ctl) {
        return 0;
    }
    double d1 = (double)CONCAT44(0x43300000, dir ^ 0x80000000) - DOUBLE_80617860;
    double d2 = (double)CONCAT44(0x43300000, (0 - dir) ^ 0x80000000) - DOUBLE_80617860;
    ctl[1] = (int)((float)ctl[1] +
        (float)((double)CONCAT44(0x43300000, dir ^ 0x80000000) - DOUBLE_80617860) *
        FLOAT_80617940 * FLOAT_806108ec *
        (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860)));
    ctl[0xc] = (int)(amp * (num / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - bias)) * (float)d1);
    ctl[0xf] = (int)(amp * (num / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - bias)) * (float)d2);
    ekun_phase_timer_wrap((int)(ctl + 10));
    ekun_phase_timer_wrap((int)(ctl + 0xd));
    if (((dir == 1) && (thresh < (double)(float)ctl[1])) ||
        ((dir == 0xffffffff) && ((double)(float)ctl[1] < thresh))) {
        ctl[0] = ctl[0] + 1;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x801f6a6c  ekun_kite_flap_step_b
// Variant flap-phase step driver (angle field +2, no sine seeding). Integrates
// the +2 flap angle by the signed per-frame rate and advances the sequence
// counter (+0) when the angle crosses the threshold in the requested direction.
// Returns 1 on entity match.
// ---------------------------------------------------------------------------
unsigned int ekun_kite_flap_step_b(double thresh, int ev, unsigned int dir, int matchId)
{
    int *ctl = *(int **)(ev + 0x7c);
    if (matchId != *ctl) {
        return 0;
    }
    ctl[2] = (int)((float)ctl[2] +
        (float)((double)CONCAT44(0x43300000, dir ^ 0x80000000) - DOUBLE_80617860) *
        FLOAT_80617940 * FLOAT_806108ec *
        (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860)));
    if (((dir == 1) && (thresh < (double)(float)ctl[2])) ||
        ((dir == 0xffffffff) && ((double)(float)ctl[2] < thresh))) {
        ctl[0] = ctl[0] + 1;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x801f6b24  ekun_kite_flap_pose_tick
// Per-frame kite-body pose driver. Under the FP-spill frame, when the event's
// entity matches the controller (+0x7c[0]): integrate the flap sub-angles,
// build a ZYX rotation from the phase angles (+2 / +1), rotate the DAT_803dfab8
// body offset into world space, latch the start position on first tick (+3),
// drive the body world position (+0x64/+0x68/+0x6c) along it, and advance the
// sequence timer (+4). When the timer completes, optionally play the landing
// sound 0x4f7 (with a fade-out on the loop), then advance the sequence counter
// (+0) and reset the per-phase state.
// ---------------------------------------------------------------------------
void ekun_kite_flap_pose_tick(unsigned int a1, unsigned int a2, int playSound)
{
    unsigned long long packed = FUN_803826f0();
    double zero = DOUBLE_80617860;
    float sndVol = FLOAT_80617844;
    float amp    = FLOAT_80617834;
    float num    = FLOAT_8061780c;
    int entity = (int)(packed >> 0x20);
    int *ctl = *(int **)(entity + 0x7c);
    float off[3] = { DAT_803dfab8, DAT_803dfabc, DAT_803dfac0 };

    if ((int)packed == *ctl) {
        float rate = FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);
        float dt   = FLOAT_806108ec * rate;
        ctl[0xc] = (int)(FLOAT_80617834 * rate);
        double posStep = (double)(sndVol * dt);
        ctl[0xf] = (int)(amp * (num / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - zero)));
        double velStep = (double)FLOAT_8061780c;  // extraout_f1 in the ROM frame

        ekun_phase_timer_wrap((int)(ctl + 10));
        ekun_phase_timer_wrap((int)(ctl + 0xd));

        float mtx[17];
        mtx4_set_rotation_zyx(
            (double)((FLOAT_806178c0 * (float)ctl[2]) / FLOAT_80617820),
            (double)((FLOAT_806178c0 * (float)ctl[1]) / FLOAT_80617820),
            (double)FLOAT_806177f8, mtx);
        mtx4_transform_vector(mtx, off);

        if (ctl[3] == 0) {
            ctl[5] = (int)(*(float *)(entity + 100)  + (float)((double)off[0] * velStep));
            ctl[6] = (int)(*(float *)(entity + 0x68) + (float)((double)off[1] * velStep));
            ctl[7] = (int)(*(float *)(entity + 0x6c) + (float)((double)off[2] * velStep));
            ctl[3] = 1;
        }
        *(float *)(entity + 100)  = *(float *)(entity + 100)  + (float)((double)off[0] * posStep);
        *(float *)(entity + 0x68) = *(float *)(entity + 0x68) + (float)((double)off[1] * posStep);
        *(float *)(entity + 0x6c) = *(float *)(entity + 0x6c) + (float)((double)off[2] * posStep);
        ctl[4] = (int)(float)((double)(float)ctl[4] + posStep);

        if (velStep < (double)(float)ctl[4]) {
            if (playSound != 0) {
                sound_register_fade_out((double)FLOAT_8061780c, (double)FLOAT_80617938,
                                        (int *)(entity + 0x22c), (unsigned int *)0);
                float *wpos = entity_get_world_position(entity);
                int params[7];
                sound_init_play_params((int)params);
                *(char *)((int)params + 0x28) = 1;
                *(unsigned int *)((int)params + 0x2c) |= 0xa0;
                params[0] = 0x4f7;
                *(float *)((int)params + 0x18) = wpos[0];
                *(float *)((int)params + 0x1c) = wpos[1];
                *(float *)((int)params + 0x20) = wpos[2];
                sound_play_request(params);
            }
            ctl[0] = ctl[0] + 1;
            ctl[3] = 0;
            ctl[4] = (int)FLOAT_806177f8;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801f6d94  ekun_kite_bone_pose_build
// Build the kite body's bone matrix: a ZYX rotation from the controller's
// +8 / +4 phase angles into the render node's root bone (+0x10), then (when the
// controller carries a collision flag +0x24) install two collision face
// matrices via ekun_bridge_face_matrix_install. Flag the render node dirty
// (0x200000).
// ---------------------------------------------------------------------------
void ekun_kite_bone_pose_build(int self)
{
    int ctl = *(int *)(self + 0x7c);
    mtx4_set_rotation_zyx(
        (double)((FLOAT_806178c0 * *(float *)(ctl + 8)) / FLOAT_80617820),
        (double)((FLOAT_806178c0 * *(float *)(ctl + 4)) / FLOAT_80617820),
        (double)FLOAT_806177f8, *(float **)(*(int *)(self + 4) + 0x10));
    if (*(int *)(ctl + 0x24) != 0) {
        ekun_bridge_face_matrix_install();
        ekun_bridge_face_matrix_install();
    }
    *(unsigned int *)(self + 0x28) |= 0x200000;
}

// ---------------------------------------------------------------------------
// 0x801f6e28  ekun_kite_flap_dispatch
// Kite flap anim-event dispatcher. Syncs the loop-sound position to the body,
// then branches on the controller's 4CC tag (+0x2d8[+4]) to run the correct
// flap sequence through ekun_kite_flap_step_a/_b + ekun_kite_flap_pose_tick:
//   'BKTL' (0x4c544b42) / 'BKTR' (0x52544b42): simple pose tick.
//   'FKTL' (0x4c544b46): forward-left launch sequence; at anim tag 0x49f spawns
//     the 'BTKL'/'BRKL' tail-line sub-entities (FUN_8023c32c + register).
//   'RKTF' (0x52544b46): reverse launch sequence; at anim tag 0x49d spawns the
//     'BTKR'/'BRKR' tail-line sub-entities.
// ---------------------------------------------------------------------------
void ekun_kite_flap_dispatch(int self)
{
    if (-1 < *(int *)(self + 0x22c)) {
        float *wpos = entity_get_world_position(self);
        sound_get_position(*(int *)(self + 0x22c), wpos);
    }
    int tag = *(int *)(*(int *)(self + 0x2d8) + 4);
    if (tag == 0x52544b42) {            // 'BKTR'
        ekun_kite_flap_pose_tick(self, 0, 0);
    } else if (tag < 0x52544b42) {
        if (tag == 0x4c544b46) {        // 'FKTL'
            ekun_kite_flap_step_a((double)FLOAT_80617878, self, 1, 0);
            ekun_kite_flap_pose_tick(self, 1, 0);
            ekun_kite_flap_step_a((double)FLOAT_806177f8, self, 0xffffffff, 2);
            ekun_kite_flap_pose_tick(self, 3, 0);
            if (*(short *)(self + 0x94) == 0x49f) {
                int hit = (int)ekun_kite_flap_step_b((double)FLOAT_80617948, self, 0xffffffff, 4);
                if (hit != 0) {
                    int e = FUN_8023c32c(0x4c544254);   // 'BTKL'
                    entity_register_for_update(e, 3, -0x7fe091d8);
                    entity_register_for_update(e, 2, -0x7fe0926c);
                    e = FUN_8023c32c(0x4c544252);       // 'BRKL'
                    entity_register_for_update(e, 3, -0x7fe091d8);
                    entity_register_for_update(e, 2, -0x7fe0926c);
                }
                ekun_kite_flap_pose_tick(self, 4, 0);
                ekun_kite_flap_pose_tick(self, 5, 0);
                ekun_kite_flap_step_b((double)FLOAT_806177f8, self, 1, 6);
                ekun_kite_flap_pose_tick(self, 6, 0);
                ekun_kite_flap_pose_tick(self, 7, 0);
            } else {
                ekun_kite_flap_pose_tick(self, 4, 0);
            }
        } else if (tag == 0x4c544b42) {  // 'BKTL'
            ekun_kite_flap_pose_tick(self, 0, 0);
        }
    } else if (tag == 0x52544b46) {      // 'RKTF'
        ekun_kite_flap_step_a((double)FLOAT_80617944, self, 0xffffffff, 0);
        ekun_kite_flap_pose_tick(self, 1, 0);
        ekun_kite_flap_step_a((double)FLOAT_806177f8, self, 1, 2);
        ekun_kite_flap_pose_tick(self, 3, 0);
        if (*(short *)(self + 0x94) == 0x49d) {
            int hit = (int)ekun_kite_flap_step_b((double)FLOAT_80617948, self, 0xffffffff, 4);
            if (hit != 0) {
                int e = FUN_8023c32c(0x52544254);       // 'BTKR'
                entity_register_for_update(e, 3, -0x7fe091d8);
                entity_register_for_update(e, 2, -0x7fe0926c);
                e = FUN_8023c32c(0x52544252);           // 'BRKR'
                entity_register_for_update(e, 3, -0x7fe091d8);
                entity_register_for_update(e, 2, -0x7fe0926c);
            }
            ekun_kite_flap_pose_tick(self, 4, 0);
            ekun_kite_flap_pose_tick(self, 5, 0);
            ekun_kite_flap_step_b((double)FLOAT_806177f8, self, 1, 6);
            ekun_kite_flap_pose_tick(self, 6, 0);
            ekun_kite_flap_pose_tick(self, 7, 1);
        } else {
            ekun_kite_flap_pose_tick(self, 4, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801f71b0  ekun_kite_line_sound_spawn
// Look up the linked line entity by 4CC (+0[0x14]) and, if present, start its
// looping kite-line sound (id 0x4f6, params FLOAT_80617958/5c) at the entity's
// world position, latch the voice handle into +0x22c, and register the line for
// pose + collision updates.
// ---------------------------------------------------------------------------
void ekun_kite_line_sound_spawn(int *self)
{
    int e = FUN_8023c32c(*(unsigned int *)(*self + 0x14));
    if (e != 0) {
        float *wpos = entity_get_world_position(e);
        int params[8];
        sound_init_play_params((int)params);
        *(char *)((int)params + 0x28) = 1;
        params[0] = 0x4f6;
        *(float *)((int)params + 0x2c) = FLOAT_80617958;
        *(float *)((int)params + 0x08) = FLOAT_8061795c;
        *(float *)((int)params + 0x0c) = wpos[0];
        *(float *)((int)params + 0x10) = wpos[1];
        *(float *)((int)params + 0x14) = wpos[2];
        unsigned int voice = sound_play_request(params);
        *(unsigned int *)(e + 0x22c) = voice;
        entity_register_for_update(e, 3, -0x7fe091d8);
        entity_register_for_update(e, 2, -0x7fe0926c);
    }
}

// ---------------------------------------------------------------------------
// 0x801f726c  ekun_kite_tail_follow_update
// Position the kite's tail-tip entity ('FTKR' 0x52544654 when the parent is
// 'INTL' 0x494e544c, else 'FTKL' 0x4c544654) at the parent's anchor point
// (+0xa8..+0xb0) plus its scaled forward direction (+0x1c * dir[+0x20/+0x24]).
// ---------------------------------------------------------------------------
void ekun_kite_tail_follow_update(unsigned int a1, int *link)
{
    int tip;
    if (*link == 0x494e544c) {           // 'INTL'
        tip = FUN_8023c32c(0x52544654);  // 'FTKR'
    } else {
        tip = FUN_8023c32c(0x4c544654);  // 'FTKL'
    }
    if (tip != 0) {
        float scale = (float)link[3];
        float dx = (float)link[7];
        float dy = (float)link[8];
        float dz = (float)link[9];
        *(float *)(tip + 100)  = *(float *)(tip + 0xa8) + dx * scale;
        *(float *)(tip + 0x68) = *(float *)(tip + 0xac) + dy * scale;
        *(float *)(tip + 0x6c) = *(float *)(tip + 0xb0) + dz * scale;
    }
}

// ---------------------------------------------------------------------------
// 0x801f7308  ekun_flash_fade_tick
// Vertex-colour flash fade-out. Each frame apply the tinted colour offset
// (0x75/0x7f/0x62 with the +0x1a4 counter as alpha), step the counter down by 3,
// and once it goes negative clear the render node state word (+0x30 = 0), clear
// the render-vis low bits (& 0xffffff6f) on up to 3 collected sub-entities
// (FUN_8023c59c over +0x60), and unregister the tick.
// ---------------------------------------------------------------------------
void ekun_flash_fade_tick(int self)
{
    __we632_vertex_color_offset(self, 1, 0, 0, 0, 0, 0x75, 0x7f, 0x62,
                                (unsigned char)*(unsigned int *)(self + 0x1a4));
    *(int *)(self + 0x1a4) = *(int *)(self + 0x1a4) - 3;
    if (*(int *)(self + 0x1a4) < 0) {
        *(unsigned int *)(*(int *)(self + 4) + 0x30) = 0;
        int subs[7];
        int n = FUN_8023c59c(*(int *)(self + 0x60), (int)subs, 3);
        int off = 0;
        while (0 < n) {
            int *sub = (int *)((int)subs + off);
            off += 4;
            unsigned int *vis = *(unsigned int **)(*(int *)(*sub + 0x7c) + 0x18);
            *vis = *vis & 0xffffff6f;
            n = n - 1;
        }
        entity_register_for_update(self, 3, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x801f73e0  ekun_flash_fade_start
// Arm the vertex-colour flash fade: register the ekun_flash_fade_tick update
// (-0x7fe08cf8) and seed the fade counter (+0x1a4) to 0x7f (full brightness).
// ---------------------------------------------------------------------------
void ekun_flash_fade_start(int self)
{
    entity_register_for_update(self, 3, -0x7fe08cf8);
    *(unsigned int *)(self + 0x1a4) = 0x7f;
}

// ---------------------------------------------------------------------------
// 0x801f7420  ekun_contact_transform_copy
// Contact handler (event class 0x1a): advance the linked entity's +0x1a4 timer
// by the per-frame dt, and when the contact carries a transform block (+10 !=
// 0) copy its 3-word orientation triple into the entity's +0xd4/+0xd8/+0xdc.
// ---------------------------------------------------------------------------
void ekun_contact_transform_copy(int *ev)
{
    if (ev[5] == 0x1a) {
        *(float *)(*ev + 0x1a4) =
            FLOAT_8061788c *
            (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860)) *
            (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860) +
            *(float *)(*ev + 0x1a4);
        if ((unsigned int *)ev[10] != (unsigned int *)0) {
            *(unsigned int *)(*ev + 0xd4) = *(unsigned int *)ev[10];
            *(unsigned int *)(*ev + 0xd8) = *(unsigned int *)(ev[10] + 4);
            *(unsigned int *)(*ev + 0xdc) = *(unsigned int *)(ev[10] + 8);
        }
    }
}

// ===========================================================================
// batch_633: 0x801f74c8 - 0x801f88d4 - EKUN 'CORE' reactor / 'SWWL' motor-dial
// spark-jet finale + fragment lifetime + projected-tex beam.
//
// The CORE reactor emits a phased spark-jet (ekun_core_spark_jet_update) that
// ramps a fill counter (+0x1a4); on completion it fires the class-0x17
// explosion cluster, spawns shell-casing debris, a directed spark burst
// (particle 0x15), disables the cached spark emitters and fires the scripted
// payload. ekun_core_spark_beam_render draws the phased spark ribbon (tex 0x333)
// plus a camera-facing glow quad (tex 0x290). ekun_core_fragment_lifetime_tick
// runs a break-off fragment's fill timer and detonates it (class 0x25/0x26).
// ekun_core_aim_contact_handler is the 'DGUN'/class-0x491 contact hook that
// aims the CORE dial via atan2 and plays the 0x4ee hit sound. The SWWL/motor
// group: ekun_motor_dial_drive_tick advances two 3D motor loops (0x4ec/0x4eb)
// from the linked object's spin rate and shakes the camera; the phased spiral
// lightning is drawn by ekun_core_lightning_beam_render (sine-LUT arcs bound to
// the 'CORE' entity). ekun_core_proj_tex_matrix_build installs a projected-tex
// matrix (slot 0x21) from a jet endpoint pair; ekun_core_proj_tex_matrix_thunk
// forwards to it and ekun_core_fill_accumulate advances a fill counter.
//
// Non-matching: 4CC class ids; PPC FP spill frames (FUN_803826f0 open /
// FUN_8038273c close) opaque; int<->double 0x43300000 bias reduced to plain
// math. The wide shell_casing_spawn_batch_with_flags ROM call shape is routed
// through __we633_shell_casing_spawn (generated_decls declares a 3-arg form).
// GXLoadTexMtxImm slot install via FUN_803cb2cc. sound_update_3d_source (int
// return) reuses __we630; rng_counter_tick reuses __we629.
// ===========================================================================
extern "C" {
// --- new raw data symbols (batch_633) ---
extern int    DAT_8061275c;      // CORE lightning arc phase count
extern float  FLOAT_80612760;    // CORE dial aim angle
extern float  FLOAT_80617848;    // motor loop sound volume
extern float  FLOAT_8061784c;    // lightning arc radius
extern float  FLOAT_8061794c;    // spark ribbon phase divisor
extern float  FLOAT_80617960;    // spark emitter fade size
extern float  FLOAT_80617964;    // spark launch offset (variant 0x4a2)
extern float  FLOAT_80617968;    // shell offset X (variant 0x4a3)
extern float  FLOAT_8061796c;    // shell offset Y (variant 0x4a3)
extern float  FLOAT_80617970;    // shell offset Z (variant 0x4a3)
extern float  FLOAT_80617974;    // spark launch offset (variant 0x4a4)
extern float  FLOAT_80617978;    // colour/scale byte factor
extern float  FLOAT_8061797c;    // ribbon param d (variant 0x4a2)
extern float  FLOAT_80617980;    // ribbon param c (variant 0x4a2)
extern float  FLOAT_80617984;    // ribbon param a (variant 0x4a2)
extern float  FLOAT_80617988;    // ribbon base y (variant 0x4a3)
extern float  FLOAT_8061798c;    // ribbon param d (variant 0x4a3)
extern float  FLOAT_80617990;    // ribbon param c (variant 0x4a3)
extern float  FLOAT_80617994;    // ribbon param b (variant 0x4a3)
extern float  FLOAT_80617998;    // ribbon param a (variant 0x4a3)
extern float  FLOAT_8061799c;    // ribbon param a (variant 0x4a4)
extern float  FLOAT_806179a0;    // glow quad half-size
extern float  FLOAT_806179a4;    // dial aim gain
extern float  FLOAT_806179a8;    // dial aim range divisor
extern float  FLOAT_806179ac;    // dial arc-count gain / arc time divisor
extern float  FLOAT_806179b0;    // lightning arc angular step
extern float  FLOAT_806179b4;    // lightning arc phase divisor
extern float  FLOAT_806179b8;    // lightning arc secondary radius
extern float  FLOAT_806179bc;    // lightning arc y bias
extern float  FLOAT_806179c0;    // lightning arc y step
extern float  FLOAT_806179c4;    // projected-tex scale numerator
// projected-tex world seed (jet ray origin) + fragment launch dir consts
extern float  DAT_803dfac4;      // shell launch dir X
extern float  DAT_803dfac8;      // shell launch dir Y
extern float  DAT_803dfacc;      // shell launch dir Z
extern float  DAT_803dfad0;      // ribbon param base a
extern float  DAT_803dfad4;      // ribbon param base b
extern float  DAT_803dfad8;      // ribbon param base c
extern float  DAT_803dfadc;      // ribbon param base d
extern float  DAT_803dfae0;      // ribbon param base e
extern float  DAT_803dfae4;      // ribbon param base f
extern float  DAT_803dfae8;      // fragment launch dir X
extern float  DAT_803dfaec;      // fragment launch dir Y
extern float  DAT_803dfaf0;      // fragment launch dir Z
extern float  DAT_803dfaf4;      // CORE aim origin X
extern float  DAT_803dfaf8;      // CORE fill seed
extern float  DAT_803dfafc;      // CORE aim origin Z
extern float  DAT_803dfb00;      // lightning arc base X
extern float  DAT_803dfb04;      // lightning arc base Y
extern float  DAT_803dfb08;      // lightning arc base Z
// --- new helper externs (batch_633) ---
extern double math_atan2(double y, double x);
extern void   FUN_803cb2cc(float *mtx, int slot, int flag);          // GXLoadTexMtxImm
// FUN_80234560: wide follow-up applied to particle_spawn_directed / entity
// return handles (fp-context ABI tail carried verbatim).
extern void   FUN_80234560(unsigned long long h, double a, double b, double c,
                           double d, double e, unsigned long long f, unsigned long long g,
                           int id, int r4, int r5, void *r6, int r7, int r8, int r9, int r10);
// wide shell-casing spawn ROM shape (generated_decls has the 3-arg form)
typedef void (*ShellCasingSpawnWideFn)(double a0, double a1, double a2, double a3,
                                       double a4, double a5, unsigned long long a6,
                                       unsigned long long a7, int typeId, float *pos,
                                       float *dir, int count, int flags);
static const ShellCasingSpawnWideFn __we633_shell_casing_spawn =
    (ShellCasingSpawnWideFn)0x80144a10; // shell_casing_spawn_batch_with_flags
}

// ---------------------------------------------------------------------------
// 0x801f74c8  ekun_core_spark_jet_update
// Per-frame CORE reactor spark-jet driver. Fade any cached spark emitters
// (+0x1fc list, count +0x1e8) to the small "dying" size; while the finale has
// not fired (+0x1a0 == 0) integrate the phased fill counter +0x1a4 toward its
// target: below threshold FLOAT_8061788c it randomly emits one of six phase
// sparks (sound 0x4e1+n); on crossing it fires the class-0x17 explosion at the
// per-variant launch point (0x4a2..0x4a4), a random 0xc33+n impact sound, the
// 0x4e0 blast loop, a burst of shell casings, a directed 0x15 spark burst
// (FUN_80234560 tail), disables the cached emitters and fires the payload
// (FUN_80221074). Finally drives the 0x4df 3D loop and marks visited (+0x18).
// ---------------------------------------------------------------------------
void ekun_core_spark_jet_update(void)
{
    int self = FUN_803826f0();
    float loopRange = FLOAT_8061791c;

    float jetX = *(float *)(self + 100)  + *(float *)(self + 0x1a8);
    float jetY = *(float *)(self + 0x68) + *(float *)(self + 0x1ac);
    float jetZ = *(float *)(self + 0x6c) + *(float *)(self + 0x1b0);

    int off = 0;
    for (int i = 0; i < *(int *)(self + 0x1e8); i = i + 1) {
        int h = *(int *)(*(int *)(self + 0x1fc) + off);
        if (particle_handle_is_valid(&h)) {
            int h2 = *(int *)(*(int *)(self + 0x1fc) + off);
            particle_cached_set_size_range((double)FLOAT_80617960, (double)FLOAT_80617960,
                                           (ParticleEmitter **)&h2);
        }
        off = off + 4;
    }

    if (*(int *)(self + 0x1a0) == 0) {
        // integrate phased fill counter toward target
        float dt =
            (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860));
        *(float *)(self + 0x1a4) =
            -(float)((double)dt * (double)(float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860)
                     - (double)*(float *)(self + 0x1a4));
        if (*(float *)(self + 0x1a4) < FLOAT_806177f8) {
            *(float *)(self + 0x1a4) = FLOAT_806177f8;
        }

        if (*(float *)(self + 0x1a4) <= FLOAT_8061788c) {
            *(unsigned int *)(*(int *)(self + 4) + 0x30) = 1;
            // random per-frame phase-spark emission gated by fill*rate
            float roll = (float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617880) / FLOAT_80617868;
            float gate = dt * FLOAT_806178c4 * *(float *)(self + 0x1a4) * FLOAT_806178e8 * FLOAT_806108ec;
            if (roll < gate) {
                float pick = (float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617880) / FLOAT_80617868;
                int idx = (int)(FLOAT_80617924 * pick + FLOAT_8061781c);
                unsigned char play[0x30];
                sound_init_play_params((int)(long long)play);
                *(int *)(play + 0x00)   = idx + 0x4e1;
                *(float *)(play + 0x1c) = jetX;
                *(float *)(play + 0x20) = jetY;
                *(float *)(play + 0x24) = jetZ;
                play[0x28] = 1;
                sound_play_request((int *)play);
            }
        } else {
            // ------- finale fired -------
            float launch[3];
            float dir[3];
            dir[0] = DAT_803dfac4; dir[1] = DAT_803dfac8; dir[2] = DAT_803dfacc;
            *(int *)(self + 0x1a0) = 1;
            *(unsigned int *)(*(int *)(self + 4) + 0x30) = 0;

            unsigned short cls = *(unsigned short *)(self + 0x94);
            if (cls == 0x4a3) {
                launch[0] = *(float *)(self + 100)  - FLOAT_80617968;
                launch[1] = FLOAT_8061796c + *(float *)(self + 0x68);
                launch[2] = *(float *)(self + 0x6c) - FLOAT_80617970;
            } else if (cls < 0x4a3) {
                if (0x4a1 < cls) {
                    launch[0] = *(float *)(self + 100);
                    launch[1] = FLOAT_80617964 + *(float *)(self + 0x68);
                    launch[2] = *(float *)(self + 0x6c);
                }
            } else if (cls < 0x4a5) {
                launch[0] = *(float *)(self + 100);
                launch[1] = FLOAT_80617974 + *(float *)(self + 0x68);
                launch[2] = *(float *)(self + 0x6c);
            }

            player_ai_explosion_spawn(1, 0x17, launch, (float *)0, 0, self, 1);

            {
                unsigned int r = __we629_rng_counter_tick();
                unsigned char play[0x30];
                sound_init_play_params((int)(long long)play);
                *(int *)(play + 0x00)   = (int)(r % 6) + 0xc33;
                *(float *)(play + 0x1c) = launch[0];
                *(float *)(play + 0x20) = launch[1];
                *(float *)(play + 0x24) = launch[2];
                play[0x28] = 1;
                sound_play_request((int *)play);
            }
            {
                unsigned char play[0x30];
                sound_init_play_params((int)(long long)play);
                *(int *)(play + 0x00)   = 0x4e0;
                *(float *)(play + 0x1c) = jetX;
                *(float *)(play + 0x20) = jetY;
                *(float *)(play + 0x24) = jetZ;
                play[0x28] = 1;
                sound_play_request((int *)play);
            }

            {
                unsigned int r = __we629_rng_counter_tick();
                __we633_shell_casing_spawn((double)*(float *)(self + 0x218),
                                           (double)jetX, (double)dt, DOUBLE_80617860,
                                           0.0, 0.0, 0, 0, 0xd, launch, dir,
                                           (int)(r % 10) + 10, 0);
            }

            int stateFlag = session_get_state_flag();
            unsigned int visFlag = ~-(unsigned int)(stateFlag == 1) & 0x80000;
            unsigned long long ph = (unsigned long long)(unsigned int)
                particle_spawn_directed(0, 0x15, visFlag, launch, (int *)0);
            FUN_80234560(ph, (double)jetX, (double)dt, DOUBLE_80617860, 0.0, 0.0, 0, 0,
                         *(int *)(self + 0x60), 0, (int)visFlag, launch, 0, self, 1, 0);
            particle_entity_disable_all();
            FUN_80221074((double)FLOAT_806177f8, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0);
        }
    }

    {
        float pos[3];
        pos[0] = jetX; pos[1] = jetY; pos[2] = jetZ;
        int started = __we630_sound_update_3d_source(
            (double)loopRange, (double)FLOAT_80617878, (double)FLOAT_8061780c,
            self + 0x22c, (unsigned int *)pos, (float *)0x4df, 0);
        if (started != 0) {
            *(unsigned int *)(self + 0x18) = *(unsigned int *)(self + 0x18) | 0x40;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801f7940  ekun_core_spark_beam_render
// GX render of the CORE spark ribbon while the finale has not fired and the
// fill counter is live. Builds a phased 10-quad spark strip (tex 0x333) rotated
// by the two per-variant ribbon basis vectors (mtx4_transform_vector of the
// entity's +0x274 matrix), then a camera-facing glow quad (tex 0x290) at the
// jet local offset (+0xd4/+0xd8/+0xdc), using the active view matrix rows at
// DAT_80611d54 (+0x364..+0x378).
// ---------------------------------------------------------------------------
void ekun_core_spark_beam_render(int self)
{
    if ((*(int *)(self + 0x1a0) != 0) || (*(float *)(self + 0x1a4) <= FLOAT_806177f8)) {
        return;
    }

    float fill    = *(float *)(self + 0x1a4) * FLOAT_806178e8;
    float phaseA  = (float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617880) / FLOAT_80617868;
    float uScroll = (float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617880) / FLOAT_80617868;
    float frames  = game_time_to_frames();
    float dtInv   = FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);

    float rate = (float)((double)FLOAT_80617828 * (double)fill);
    if (FLOAT_8061780c < rate) rate = FLOAT_8061780c;

    float ribbon[6];
    ribbon[0] = DAT_803dfad0; ribbon[1] = DAT_803dfad4; ribbon[2] = DAT_803dfad8; // basis A
    ribbon[3] = DAT_803dfadc; ribbon[4] = DAT_803dfae0; ribbon[5] = DAT_803dfae4; // basis B
    float phaseAdv = (float)((double)fill * (double)(float)((double)FLOAT_80617828 * (double)(float)((double)dtInv * (double)frames)));

    float ry = (double)FLOAT_806177f8;   // ribbon top y bias
    float pa, pb, pc, pd;                // per-variant ribbon params
    unsigned short cls = *(unsigned short *)(self + 0x94);
    if (cls == 0x4a3) {
        ribbon[0] = FLOAT_806177f8; ribbon[1] = FLOAT_806177f8; ribbon[2] = FLOAT_8061780c;
        ribbon[3] = FLOAT_8061780c; ribbon[4] = FLOAT_806177f8; ribbon[5] = FLOAT_806177f8;
        ry = FLOAT_80617988;
        pa = FLOAT_80617998; pb = FLOAT_80617994; pc = FLOAT_80617990; pd = FLOAT_8061798c;
    } else {
        pa = pb = pc = pd = FLOAT_806177f8;
        if (cls < 0x4a3) {
            if (0x4a1 < cls) {
                pa = FLOAT_80617984; pb = FLOAT_8061781c; pc = FLOAT_80617980; pd = FLOAT_8061797c;
            }
        } else if (cls < 0x4a5) {
            pa = FLOAT_8061799c; pb = FLOAT_8061782c; pc = FLOAT_8061788c; pd = FLOAT_8061788c;
        }
    }

    float gx0 = *(float *)(self + 0xd4);
    float gy0 = *(float *)(self + 0xd8);
    float gz0 = *(float *)(self + 0xdc);
    mtx4_transform_vector((float *)(self + 0x274), &ribbon[0]);
    mtx4_transform_vector((float *)(self + 0x274), &ribbon[3]);

    gx_set_sub_material_id(2);
    gx_set_blend_mode(2);
    int tex = sprite_get_texture_id(0x333);
    gx_begin_textured_batch(2, 0x92, 0, 0x14, tex);

    float scaleC = FLOAT_80617828;
    for (int i = 0; i < 10; i = i + 1) {
        // segment parameter segd = -i / phaseDiv  (int->double bias idiom preserved)
        double segd = (double)((float)((double)(CONCAT44(0x43300000, i) ^ 0x80000000) - DOUBLE_80617860) / FLOAT_8061794c);
        float u = (float)((double)scaleC * segd + (double)phaseA);
        double d0 = (double)(float)(segd - (double)FLOAT_8061781c);
        float w0 = (float)(-d0 * pc);
        int idx = (int)((double)FLOAT_806178b4 * (double)(FLOAT_806178b0 * (float)(segd + (double)phaseAdv)) + (double)FLOAT_8061781c);
        float w1 = (float)(-d0 * pd);
        unsigned int alpha = (unsigned int)((double)fill * (double)(FLOAT_80617978 * -(float)((double)scaleC * ABS(d0) - (double)FLOAT_8061780c)));
        double amp = (double)((float)((double)FLOAT_8061782c * (double)fill) * *(float *)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc)));
        unsigned int col = (alpha & 0xff) | 0xe0fe9800;

        GXF(ribbon[0] * w0 + *(float *)(self + 100) + (float)((double)ribbon[3] * amp));
        GXF((float)((double)(float)((double)ribbon[1] * amp + (double)(ribbon[1] * w0 + *(float *)(self + 0x68))) + (double)pb));
        GXF((float)((double)(float)((double)ribbon[2] * amp + (double)(ribbon[2] * w0 + *(float *)(self + 0x6c))) + (double)ry));
        GXU(col);
        GXF(u);
        GXF(uScroll);
        GXF(ribbon[0] * w1 + *(float *)(self + 100) + (float)((double)ribbon[3] * amp));
        GXF((float)((double)(float)((double)ribbon[4] * amp + (double)(ribbon[4] * w1 + *(float *)(self + 0x68))) + (double)pa));
        GXF((float)((double)(float)((double)ribbon[5] * amp + (double)(ribbon[5] * w1 + *(float *)(self + 0x6c))) + (double)ry));
        GXU(col);
        GXF(u);
        GXF((float)((double)scaleC + (double)uScroll));
    }
    gx_end_batch();

    // camera-facing glow quad
    tex = sprite_get_texture_id(0x290);
    gx_begin_textured_batch(2, 0x92, 0, 4, tex);
    double half = (double)FLOAT_806179a0;
    float qx = FLOAT_80617844 * *(float *)(DAT_80611d54 + 0x364) + (float)(half * (double)*(float *)(DAT_80611d54 + 0x370) + (double)gx0);
    float qy = FLOAT_80617844 * *(float *)(DAT_80611d54 + 0x368) + (float)(half * (double)*(float *)(DAT_80611d54 + 0x374) + (double)gy0);
    float qz = FLOAT_80617844 * *(float *)(DAT_80611d54 + 0x36c) + (float)(half * (double)*(float *)(DAT_80611d54 + 0x378) + (double)gz0);
    unsigned int gcol = (unsigned int)(int)(FLOAT_80617978 * rate) & 0xffU | 0x3f3f7f00;
    GXF(qx); GXF(qy); GXF(qz); GXU(gcol); GXF(FLOAT_806177f8); GXF(FLOAT_8061780c);
    GXF(FLOAT_80617924 * *(float *)(DAT_80611d54 + 0x370) + qx);
    GXF(FLOAT_80617924 * *(float *)(DAT_80611d54 + 0x374) + qy);
    GXF(FLOAT_80617924 * *(float *)(DAT_80611d54 + 0x378) + qz);
    GXU(gcol); GXF(FLOAT_8061780c); GXF(FLOAT_8061780c);
    float bx = (float)(half * (double)*(float *)(DAT_80611d54 + 0x364) + (double)(float)(half * (double)*(float *)(DAT_80611d54 + 0x370) + (double)gx0));
    float by = (float)(half * (double)*(float *)(DAT_80611d54 + 0x368) + (double)(float)(half * (double)*(float *)(DAT_80611d54 + 0x374) + (double)gy0));
    float bz = (float)(half * (double)*(float *)(DAT_80611d54 + 0x36c) + (double)(float)(half * (double)*(float *)(DAT_80611d54 + 0x378) + (double)gz0));
    GXF(bx); GXF(by); GXF(bz); GXU(gcol); GXF(FLOAT_806177f8); GXF(FLOAT_806177f8);
    GXF(FLOAT_80617924 * *(float *)(DAT_80611d54 + 0x370) + bx);
    GXF(FLOAT_80617924 * *(float *)(DAT_80611d54 + 0x374) + by);
    GXF(FLOAT_80617924 * *(float *)(DAT_80611d54 + 0x378) + bz);
    GXU(gcol); GXF(FLOAT_8061780c); GXF(FLOAT_806177f8);
    gx_end_batch();
}

// ---------------------------------------------------------------------------
// 0x801f7f7c  ekun_core_fragment_lifetime_tick
// Break-off CORE fragment lifetime tick. Integrate the fragment's fill timer
// (word[0x69]) toward zero at the per-frame dt; when it drains, destroy the
// entity ('PWCR' 0x50574352 death event via FUN_80234560) and fire the paired
// class-0x25/0x26 explosion at the fragment origin (+0x38) along the fixed
// launch dir (DAT_803dfae8..).
// ---------------------------------------------------------------------------
void ekun_core_fragment_lifetime_tick(unsigned int *self)
{
    float dir[3];
    dir[0] = DAT_803dfae8; dir[1] = DAT_803dfaec; dir[2] = DAT_803dfaf0;
    double fill = (double)(float)self[0x69];
    if ((double)FLOAT_806177f8 < fill) {
        double dtInv = (double)(FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860));
        double dtFrames = (double)(float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860);
        self[0x69] = -(float)(dtInv * dtFrames - fill);
        if ((double)(float)self[0x69] <= (double)FLOAT_806177f8) {
            entity_destroy_with_event((int *)self);
            FUN_80234560(0, DOUBLE_80617860, (double)FLOAT_806177f8, fill, 0.0, 0.0, 0, 0,
                         0x50574352, 0, 0, dir, 0, 0, 0, 0);
            player_ai_explosion_spawn(0x25, 0x17, (float *)(self + 0x38), dir, 0, 0, 1);
            player_ai_explosion_spawn(0x26, 0x17, (float *)(self + 0x38), dir, 0, 0, 1);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801f8084  ekun_core_aim_contact_handler
// CORE dial aim contact hook. On a class-0x491 ('DGUN'?) contact carrying a
// world point (link[10]) with contact-type 1, aim the dial toward the point:
// FLOAT_80612760 = base offset - atan2(dx,dz) - a gain*height term, cache the
// arc count DAT_8061275c from the same height term, seed the linked entity's
// +0x1a4 latch, store the contact point into +0xe0..+0xe8 and play the 0x4ee
// hit sound with volume FLOAT_80617848.
// ---------------------------------------------------------------------------
void ekun_core_aim_contact_handler(int *ev)
{
    float *pt = (float *)ev[10];
    if ((pt != (float *)0) && (ev[3] != 0) &&
        (*(short *)(ev[3] + 0x94) == 0x491) && (ev[5] == 1)) {
        double ang = math_atan2((double)(*pt - DAT_803dfaf4), (double)(pt[2] - DAT_803dfafc));
        FLOAT_80612760 = (FLOAT_806178fc - (float)ang) -
                         (FLOAT_806179a4 * (*(float *)(ev[10] + 4) - DAT_803dfaf8)) / FLOAT_806179a8;
        DAT_8061275c = (int)((FLOAT_806179ac * (*(float *)(ev[10] + 4) - DAT_803dfaf8)) / FLOAT_806179a8);
        *(float *)(*ev + 0x1a4) = FLOAT_8061781c;
        *(unsigned int *)(*ev + 0xe0) = *(unsigned int *)ev[10];
        *(unsigned int *)(*ev + 0xe4) = *(unsigned int *)(ev[10] + 4);
        *(unsigned int *)(*ev + 0xe8) = *(unsigned int *)(ev[10] + 8);
        unsigned char play[0x40];
        sound_init_play_params((int)(long long)play);
        *(int *)(play + 0x00)          = 0x4ee;
        *(unsigned int *)(play + 0x10) = *(unsigned int *)(play + 0x10) | 0x200;  // entity-attached flag
        *(float *)(play + 0x18)        = FLOAT_80617848;                          // volume
        sound_play_request((int *)play);
    }
}

// ---------------------------------------------------------------------------
// 0x801f81e8  ekun_motor_dial_drive_tick
// Per-frame SWWL motor-dial drive. When gameplay is active, sample the linked
// object's (+0x7c -> +0x6c) position deltas since last frame (cached at
// DAT_80612754 +0x78/+0x7c), step two smoothed motor-speed channels (+0x80/+0x84)
// toward those rates and drive two 3D motor loops (0x4ec/0x4eb) whose Z is
// derived from the speed. While the linked entity's +0x108 charge exceeds the
// thresholds, keep the 0x4ed loop alive; when the charge rises, apply a
// per-player camera shake scaled by it.
// ---------------------------------------------------------------------------
void ekun_motor_dial_drive_tick(void)
{
    int self = FUN_803826f0();
    if (DAT_806108e4 != 0) {
        int obj  = *(int *)(self + 0x7c);
        int link = *(int *)(obj + 0x6c);
        char *blk = (char *)DAT_80612754;
        float dx = ABS(*(float *)(blk + 0x78) - *(float *)(obj + 0x24));
        double dz = ABS((double)(*(float *)(blk + 0x7c) - *(float *)(obj + 0x1c)));
        if (FLOAT_80617824 <= dx) dx = dx - FLOAT_80617824;
        *(unsigned int *)(blk + 0x78) = *(unsigned int *)(obj + 0x24);
        *(unsigned int *)(blk + 0x7c) = *(unsigned int *)(obj + 0x1c);

        float dtScale = FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);
        scalar_step_toward_snap((double)(dx / (FLOAT_8061785c * FLOAT_806108ec * dtScale)),
                                (double)FLOAT_80617814, (float *)(blk + 0x80));
        scalar_step_toward_snap((double)(float)(dz / (double)(FLOAT_80617838 * FLOAT_806108ec * dtScale)),
                                (double)FLOAT_80617814, (float *)(blk + 0x84));

        __we630_sound_update_3d_source((double)*(float *)(blk + 0x80), (double)FLOAT_8061785c,
                               (double)(float)((double)FLOAT_80617938 * (double)*(float *)(blk + 0x80) + (double)FLOAT_80617914),
                               (int)(intptr_t)(blk + 0x88), (unsigned int *)0, (float *)0x4ec, 1);
        __we630_sound_update_3d_source((double)*(float *)(blk + 0x84), (double)FLOAT_8061785c,
                               (double)(float)((double)FLOAT_80617938 * (double)*(float *)(blk + 0x84) + (double)FLOAT_80617914),
                               (int)(intptr_t)(blk + 0x8c), (unsigned int *)0, (float *)0x4eb, 1);

        if ((*(float *)(link + 0x108) < FLOAT_80612750) || (*(float *)(link + 0x108) < FLOAT_80617920)) {
            sound_register_fade_out((double)FLOAT_8061780c, (double)FLOAT_80617814,
                                    (int *)(self + 0x22c), (unsigned int *)0);
        } else if (*(int *)(self + 0x22c) < 0) {
            int params[7];
            sound_init_play_params((int)params);
            params[0] = 0x4ed;
            unsigned int h = sound_play_request(params);
            *(unsigned int *)(self + 0x22c) = h;
        }

        if (FLOAT_80617938 < *(float *)(link + 0x108)) {
            int po = 0;
            for (int p = 0; p < DAT_80612610; p = p + 1) {
                player_apply_camera_shake_priority(FLOAT_80617814,
                    (float)((double)FLOAT_80617828 * (double)*(float *)(link + 0x108)),
                    (float)((double)FLOAT_806178d0 * (double)*(float *)(link + 0x108)),
                    DAT_80611d78 + po);
                po = po + 0x141c;
            }
        }
        FLOAT_80612750 = *(float *)(link + 0x108);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801f844c  ekun_core_lightning_beam_render
// Draws the CORE reactor's spiral lightning arc (144 verts, tex slot 0) while
// the 'CORE' entity (0x434f5245) has a live fill (+0x1a4). Each vert steps the
// aim angle FLOAT_80612760 by FLOAT_806179b0 and wobbles X/Z off the base
// (DAT_803dfb00..) using the sine LUT DAT_80583228; alpha ramps down over the
// DAT_8061275c-centred arc window; endpoints are opaque white.
// ---------------------------------------------------------------------------
void ekun_core_lightning_beam_render(void)
{
    int arcCenter = DAT_8061275c;
    float baseZ   = DAT_803dfb08;
    float baseX   = DAT_803dfb00;
    double aim    = (double)FLOAT_80612760;
    float baseY   = DAT_803dfb04;

    int core = FUN_8023c32c(0x434f5245);   // 'CORE'
    if ((core != 0) && (FLOAT_806177f8 < *(float *)(core + 0x1a4))) {
        float lifeRatio = *(float *)(core + 0x1a4) / FLOAT_8061781c;
        if (FLOAT_8061780c < lifeRatio) lifeRatio = FLOAT_8061780c;
        double invLife = (double)(FLOAT_8061780c - lifeRatio);

        gx_set_blend_mode(2);
        gx_set_sub_material_id(2);
        int tex = sprite_get_texture_id(0);
        gx_begin_textured_batch(2, 0x92, 0, 0x120, tex);

        float phase = (FLOAT_806178b0 * (float)((double)(CONCAT44(0x43300000, (int)(aim / (double)FLOAT_806179b0)) ^ 0x80000000) - DOUBLE_80617860)) / FLOAT_806179b4;
        int i = 0;
        int n = 0x90;
        do {
            double fadeD = ABS((double)((float)((double)(CONCAT44(0x43300000, i - arcCenter) ^ 0x80000000) - DOUBLE_80617860) / FLOAT_806179ac)) / invLife;
            float fade = FLOAT_8061780c;
            if ((float)fadeD < FLOAT_8061780c) fade = (float)fadeD;
            unsigned int si = (unsigned int)((double)FLOAT_806178b4 * (double)phase + (double)FLOAT_8061781c);
            float wob = FLOAT_8061784c * *(float *)((int)&DAT_80583228 + ((si + 0x200) * 4 & 0x1ffc));

            GXF(baseX + wob);
            GXF(baseY + FLOAT_806177f8);
            GXF(baseZ + FLOAT_8061784c * (&DAT_80583228)[si & 0x7ff]);
            if ((i == 0) || (i == 0x8f)) {
                GXU(0xffffff00);
            } else {
                GXU((unsigned int)(int)(FLOAT_80617978 * (FLOAT_8061780c - fade)) & 0xffffff7f);
            }
            GXF(FLOAT_806177f8);
            GXF(FLOAT_806177f8);

            phase = (float)((double)phase + (double)FLOAT_806179b0);
            i = i + 1;
            GXF(FLOAT_806179b8 * wob + baseX);
            GXF(FLOAT_806179b8 * FLOAT_80617814 * (baseY - FLOAT_806179bc) + baseY);
            GXF(FLOAT_806179b8 * FLOAT_8061784c * (&DAT_80583228)[si & 0x7ff] + baseZ);
            GXU(0xffffff00);
            GXF(FLOAT_806177f8);
            GXF(FLOAT_806177f8);
            n = n + -1;
            baseY = baseY + FLOAT_806179c0;
        } while (n != 0);
        gx_end_batch();
    }
}

// ---------------------------------------------------------------------------
// 0x801f8738  ekun_core_proj_tex_matrix_build
// Build and install the CORE jet's projected-texture matrix (GX tex-mtx slot
// 0x21). Copy the base render matrix, translate to the midpoint of the jet ray
// (-(a+b)*0.5 for x/y/z from param_2[0..5]), apply the anisotropic scale
// (FLOAT_806179c4 / (dir extent)) and a fixed post-translate, then transpose
// and load; bind TEV order stage 1 -> tex-coord 0x21.
// ---------------------------------------------------------------------------
void ekun_core_proj_tex_matrix_build(int self, float *ray)
{
    float base[16];
    float scaleM[12];
    float postM[16];
    float outM[16];

    mtx4_copy(*(float **)(*(int *)(self + 4) + 0x10), base);
    mtx4_write_translation(
        (double)(-((ray[0] + ray[3]) * FLOAT_8061781c) + *(float *)(self + 100)),
        (double)(-((ray[1] + ray[4]) * FLOAT_8061781c) + *(float *)(self + 0x68)),
        (double)(-((ray[2] + ray[5]) * FLOAT_8061781c) + *(float *)(self + 0x6c)),
        (int)base);

    mtx4_set_identity((int)scaleM);
    scaleM[0]  = FLOAT_806179c4 / (FLOAT_8061781c * (ray[3] - ray[0]));
    scaleM[1]  = FLOAT_806177f8;
    scaleM[3]  = FLOAT_806177f8;   // (col spans; matched to ROM writes)
    scaleM[4]  = FLOAT_806177f8;
    scaleM[6]  = FLOAT_806179c4 / (FLOAT_8061781c * (ray[5] - ray[2]));
    scaleM[8]  = FLOAT_806177f8;
    scaleM[9]  = FLOAT_806177f8;

    mtx4_set_identity((int)postM);
    postM[0] = FLOAT_8061781c;
    postM[5] = FLOAT_8061781c;
    postM[9] = FLOAT_806177f8;
    mtx4_write_translation((double)FLOAT_8061781c, (double)FLOAT_8061781c, (double)FLOAT_8061780c, (int)postM);

    mtx4_multiply(base, scaleM, outM);
    mtx4_multiply(outM, postM, outM);
    mtx4_transpose(outM, outM);
    FUN_803cb2cc(outM, 0x21, 0);
    gx_cached_set_tev_order(1, 0, 0, 0x21);
}

// ---------------------------------------------------------------------------
// 0x801f88b0  ekun_core_render_stub
// Empty render hook.
// ---------------------------------------------------------------------------
void ekun_core_render_stub(void)
{
}

// ---------------------------------------------------------------------------
// 0x801f88b4  ekun_core_proj_tex_matrix_thunk
// Thunk forwarding to ekun_core_proj_tex_matrix_build.
// ---------------------------------------------------------------------------
void ekun_core_proj_tex_matrix_thunk(int self, float *ray)
{
    ekun_core_proj_tex_matrix_build(self, ray);
}

// ---------------------------------------------------------------------------
// 0x801f88d4  ekun_core_fill_accumulate
// Advance the CORE fill counter (+0x1a4) up by the per-frame dt, clamped to the
// ceiling FLOAT_8061780c (1.0).
// ---------------------------------------------------------------------------
void ekun_core_fill_accumulate(int self)
{
    float ceil = FLOAT_8061780c;
    *(float *)(self + 0x1a4) =
        (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860)) *
        (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860) +
        *(float *)(self + 0x1a4);
    if (ceil < *(float *)(self + 0x1a4)) {
        *(float *)(self + 0x1a4) = ceil;
    }
}

// ===========================================================================
// batch_634: 0x801f893c - 0x801fa1cc - EKUN 'CORE' reactor door-panel sequence
// + lightning-arc / beam renderers + node-traversal + detonation contact.
//
// The CORE reactor is a multi-panel blast door driven by a shared control block
// (DAT_80612754): a 6-step opening sequence (ekun_core_sequence_physics_tick)
// advances an index +0x40 / phase timer +0x44 through the DAT_80454xxx panel
// tables, integrates a 4-substep angular spring on +0x20/+0x2c and a swing
// angle +0x38, and randomly redistributes a torque impulse across the 3 axes;
// when both latches (+0x58/+0x5c) are set it destroys the 'INCC' core entity.
// ekun_core_panel_pose_build poses each panel bone-matrix per its class word
// (0x4a8..0x4af: sliding leaf / iris / hinge / camera-aimed dial), optionally
// installing a projected-texture matrix (ekun_core_proj_tex_matrix_thunk).
// The electric arcs are drawn by ekun_core_beam_segment_render (one camera-
// facing quad segment, tex 0x33a straight / 0x333 sine-wobbled), tiled by
// ekun_core_beam_strip_render (10-segment straight beam) and
// ekun_core_lightning_arc_render (6 forked sub-arcs with length-clamped joints
// off the DAT_803dfb14 node table). ekun_core_detonate_contact drains a
// class-0x1a contact countdown +0x1cc and detonates (explosion 0x26 + fracture
// + payload + stereo blast). ekun_core_node_bone_reset / _traverse_tick manage
// a 2-node edge traversal that slides the reactor head between mesh anchors.
//
// Non-matching: 4CC ids; PPC FP/GPR spill frames opaque; int<->double 0x43300000
// bias reduced to plain math; the ROM 3-double mtx4_set_scale/translation forms
// route through __we634_mtx4_set_scale3 / __we3_mtx4_set_translation3 aliases;
// FUN_801f94b4's __save_gpr_28 spill of its two endpoint pointers is expressed
// as explicit ptA/ptB params. GX FIFO via GXF/GXU. Sine LUT DAT_80583228.
// ===========================================================================
extern "C" {
// --- new raw data symbols (batch_634) ---
// CORE panel tables: 6 entries at 0x28 stride from base 0x80454978
extern float DAT_80454978;  extern float DAT_8045497c;  extern float DAT_80454980;   // panel axis row0
extern float DAT_80454984;  extern float DAT_80454988;  extern float DAT_8045498c;   // panel axis row1
extern float DAT_80454990;  extern float DAT_80454994;  extern int   DAT_80454998;   // scale a/b + open-flag
extern int   DAT_8045499c;                                                           // close-flag
extern float DAT_80454a68;  extern float DAT_80454a6c;  extern float DAT_80454a70;   // panel world offset
extern float DAT_80454a74;  extern float DAT_80454a78;  extern float DAT_80454a7c;   // proj-tex box center
extern float DAT_80454a80;  extern float DAT_80454a84;  extern float DAT_80454a88;   // proj-tex box half-extent / node-submesh table
extern int   DAT_80454a8c;  extern int   DAT_80454a90;                               // node-submesh table cont.
extern float DAT_803dfb14;   // lightning arc-node offset table (9 entries, 0xc stride)
extern float DAT_803dfb60;   // detonation launch dir X
extern float DAT_803dfb64;   // detonation launch dir Y
extern float DAT_803dfb68;   // detonation launch dir Z
extern double DOUBLE_806179f0; // arc joint length base
extern float  FLOAT_806179c8;  // half / mid constant (reused b633)
extern float  FLOAT_806179cc;  // torque impulse scale
extern float  FLOAT_806179d0;  // torque re-arm base
extern float  FLOAT_806179d4;  // torque re-arm jitter
extern float  FLOAT_806179d8;  // spring damping factor
extern float  FLOAT_806179dc;  // proj-tex slide Z
extern float  FLOAT_806179e0;  // beam step Y
extern float  FLOAT_806179e4;  // beam phase step A
extern float  FLOAT_806179e8;  // beam phase wrap
extern float  FLOAT_806179ec;  // arc sine-table base index
extern float  FLOAT_806179f8;  // arc segment width
extern float  FLOAT_806179fc;  // fracture spawn base scale
// --- new helper externs (batch_634) ---
extern double FUN_803909ec(double x);   // trig/interp math helper (sin-like)
// ROM 3-double mtx4_set_scale form (canonical decl is (float*,f,f,f))
typedef void (*Mtx4SetScale3Fn)(double, double, double);
static const Mtx4SetScale3Fn __we634_mtx4_set_scale3 = (Mtx4SetScale3Fn)&mtx4_set_scale;
// FUN_803826e4 is declared `void FUN_803826e4(void)` earlier in this TU (b~612,
// which ignored the return). This batch needs its entity-pointer return, so
// route through an int-returning fn-ptr alias rather than redeclaring (C2371).
typedef int (*FpOpenIFn)(void);
static const FpOpenIFn __we634_fp_open_i = (FpOpenIFn)&FUN_803826e4;
}

// ---------------------------------------------------------------------------
// 0x801f893c  ekun_core_collision_open_gate
// When the CORE has both latch A (+0x58) set and latch B (+0x5c) clear, drop
// the entity's collision to layer mask 0xb (passable body).
// ---------------------------------------------------------------------------
void ekun_core_collision_open_gate(int self)
{
    char *blk = (char *)DAT_80612754;
    if ((*(int *)(blk + 0x58) != 0) && (*(int *)(blk + 0x5c) == 0)) {
        collision_set_layer_mask(*(int **)(self + 4), 0xb);
    }
}

// ---------------------------------------------------------------------------
// 0x801f8980  ekun_core_sequence_physics_tick
// Per-frame CORE opening-sequence + panel angular-spring driver. Advances the
// 6-step sequence phase timer (+0x44); when a step's panel table entry is armed
// (DAT_8045499c/DAT_80454998 at idx*0x28) marks its slot flag (+0x50), bumps the
// step index (+0x40) and enables the next open/close panel render node (+0x28|1).
// Integrates the swing angle (+0x38) and a decaying jitter timer (+0x3c) that on
// expiry redistributes a random torque impulse across the 3 spin axes (+0x2c..).
// Then runs a 4-iteration angular spring (angle +0x2c.. driven by offset +0x20..
// with damping FLOAT_806179d8). Once both latches set, destroys the 'INCC' core.
// ---------------------------------------------------------------------------
void ekun_core_sequence_physics_tick(void)
{
    char *blk = (char *)DAT_80612754;
    float dt = (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860)) * FLOAT_806108ec;
    float dtFrames = (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860);
    float dtInv = FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860);

    if (*(int *)(blk + 0x40) < 6) {
        int step = *(int *)(blk + 0x40);
        *(float *)(blk + 0x44) = dt + *(float *)(blk + 0x44);
        if ((*(int *)(&DAT_8045499c + step * 0x28) != 0) || (step == 0)) {
            *(float *)(blk + 0x44) = dt + *(float *)(blk + 0x44);
        }
        if (FLOAT_8061788c < *(float *)(blk + 0x44)) {
            *(int *)(blk + *(int *)(blk + 0x48) * 4 + 0x50) = 1;
            *(int *)(blk + 0x40) = *(int *)(blk + 0x40) + 1;
            step = *(int *)(blk + 0x40);
            if (step < 6) {
                *(float *)(blk + 0x44) = FLOAT_806177f8;
                if (*(int *)(&DAT_80454998 + step * 0x28) != 0) {
                    *(int *)(blk + 0x48) = *(int *)(blk + 0x48) + 1;
                    int node = *(int *)(blk + *(int *)(blk + 0x48) * 4 + 0x60);
                    if (node != 0) { *(unsigned int *)(node + 0x28) = *(unsigned int *)(node + 0x28) | 1; }
                }
                if (*(int *)(&DAT_8045499c + step * 0x28) != 0) {
                    int node = *(int *)(blk + *(int *)(blk + 0x4c) * 4 + 0x70);
                    if (node != 0) { *(unsigned int *)(node + 0x28) = *(unsigned int *)(node + 0x28) | 1; }
                    *(int *)(blk + 0x4c) = *(int *)(blk + 0x4c) + 1;
                }
            }
        }
    }

    // swing angle + decaying jitter timer
    *(float *)(blk + 0x3c) = -(dtInv * dtFrames - *(float *)(blk + 0x3c));
    *(float *)(blk + 0x38) = FLOAT_806179c8 * dtInv * dtFrames + *(float *)(blk + 0x38);
    if (*(float *)(blk + 0x3c) < FLOAT_806177f8) {
        unsigned int axis = __we629_rng_counter_tick() % 3;
        float mag = FLOAT_806179cc * ((float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617880) / FLOAT_80617868);
        float half = mag * FLOAT_8061781c;
        *(float *)(blk + axis * 4 + 0x2c)             += mag;
        *(float *)(blk + ((axis + 1) % 3) * 4 + 0x2c) -= half;
        *(float *)(blk + ((axis + 2) % 3) * 4 + 0x2c) -= half;
        *(float *)(blk + 0x3c) = FLOAT_806179d4 * ((float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617880) / FLOAT_80617868) * FLOAT_8061781c + FLOAT_806179d0;
    }

    // 4-iteration angular spring (offset +0x20.. <-> angle +0x2c..)
    float damp = FLOAT_806179d8;
    float k = FLOAT_806178d4;
    for (int i = 0; i < 4; i = i + 1) {
        *(float *)(blk + 0x2c) += *(float *)(blk + 0x20) * k * -(dtInv) * dtFrames;
        *(float *)(blk + 0x30) += *(float *)(blk + 0x24) * k * -(dtInv) * dtFrames;
        *(float *)(blk + 0x34) += *(float *)(blk + 0x28) * k * -(dtInv) * dtFrames;
        *(float *)(blk + 0x20) += *(float *)(blk + 0x2c) * dtInv * dtFrames;
        *(float *)(blk + 0x24) += *(float *)(blk + 0x30) * dtInv * dtFrames;
        *(float *)(blk + 0x28) += *(float *)(blk + 0x34) * dtInv * dtFrames;
        *(float *)(blk + 0x2c) *= damp;
        *(float *)(blk + 0x30) *= damp;
        *(float *)(blk + 0x34) *= damp;
    }

    if ((*(int *)(blk + 0x58) != 0) && (*(int *)(blk + 0x5c) != 0)) {
        int *core = (int *)FUN_8023c32c(0x494e4343);   // 'INCC'
        if (core != (int *)0) {
            entity_destroy_with_event(core);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801f8f4c  ekun_core_panel_pose_build
// Build the render bone-matrix palette for one CORE panel per its class word
// (+0x94). Classes: 0x4a9/0x4ab (slide-open leaf via DAT_80454978.. axis rows
// + world offset DAT_80454a68..), 0x4ac (iris: camera-aimed rotate + axis
// scale), 0x4aa (extend: scale + proj-tex box via ekun_core_proj_tex_matrix_
// thunk), 0x4ae/0x4af (camera-aimed dial via atan2). Applies the shared swing
// rotation (+0x38) or the aim rotation, the panel scale from +0x20.. axis
// wobble, then multiplies through the base/pose/local matrices into every bone
// of the palette and marks skinning dirty (+0x28 | 0x200000).
// ---------------------------------------------------------------------------
void ekun_core_panel_pose_build(void)
{
    char *blk = (char *)DAT_80612754;
    int self = __we634_fp_open_i();
    unsigned short cls = *(unsigned short *)(self + 0x94);
    double rot = (double)FLOAT_806177f8;
    bool bScaleLeaf = false, bDial = false, bExtend = false, bIris = false;

    if (cls == 0x4ac) {
        if (*(int *)(blk + 0x50) == 0) bIris = true;
    } else if (cls < 0x4ac) {
        if (cls == 0x4aa) {
            if (*(int *)(blk + 0x5c) == 0) bScaleLeaf = true;
        } else if (cls < 0x4aa) {
            if ((0x4a8 < cls) && (*(int *)(blk + 0x54) == 0)) bScaleLeaf = true;
        } else if (*(int *)(blk + 0x58) == 0) {
            bScaleLeaf = true;
        } else if (*(int *)(blk + 0x5c) == 0) {
            bExtend = true;
        }
    } else if ((cls < 0x4b0) && (0x4ad < cls)) {
        rot = (double)math_atan2((double)(*(float *)(DAT_80611d54 + 0x27c) - *(float *)(self + 100)),
                                 (double)(*(float *)(DAT_80611d54 + 0x284) - *(float *)(self + 0x6c)));
        bDial = true;
        rot = (double)(FLOAT_806178c0 + (float)rot);
    }

    double fill = (double)(*(float *)(blk + 0x44) * FLOAT_806178e8);
    float rotM[16], scaleM[16], poseM[16], localM[26];

    if (bDial || (*(short *)(self + 0x94) == 0x4ac)) {
        mtx4_set_rotation_y(rot, rotM);
    } else {
        mtx4_set_rotation_y((double)*(float *)(blk + 0x38), rotM);
    }
    __we3_mtx4_set_translation3(-(double)*(float *)(self + 0x1a8), -(double)*(float *)(self + 0x1ac), -(double)*(float *)(self + 0x1b0));

    if (bScaleLeaf) {
        float f1 = (float)((double)FLOAT_8061780c - fill);
        int e = *(int *)(blk + 0x40) * 0x28;
        float rowB = *(float *)(&DAT_80454994 + e);
        float rowA = *(float *)(&DAT_80454990 + e);
        *(float *)(self + 100)  = f1 * *(float *)(&DAT_80454978 + e);
        double s = (double)(f1 * rowA + (float)(fill * (double)rowB));
        *(float *)(self + 0x68) = f1 * *(float *)(&DAT_8045497c + *(int *)(blk + 0x40) * 0x28);
        *(float *)(self + 0x6c) = f1 * *(float *)(&DAT_80454980 + *(int *)(blk + 0x40) * 0x28);
        *(float *)(self + 100)  = (float)(fill * (double)*(float *)(&DAT_80454984 + *(int *)(blk + 0x40) * 0x28) + (double)*(float *)(self + 100));
        *(float *)(self + 0x68) = (float)(fill * (double)*(float *)(&DAT_80454988 + *(int *)(blk + 0x40) * 0x28) + (double)*(float *)(self + 0x68));
        *(float *)(self + 0x6c) = (float)(fill * (double)*(float *)(&DAT_8045498c + *(int *)(blk + 0x40) * 0x28) + (double)*(float *)(self + 0x6c));
        *(float *)(self + 100)  += DAT_80454a68;
        *(float *)(self + 0x68) += DAT_80454a6c;
        *(float *)(self + 0x6c) += DAT_80454a70;
        *(float *)(self + 100)  -= *(float *)(self + 0x1a8);
        *(float *)(self + 0x68) -= *(float *)(self + 0x1ac);
        *(float *)(self + 0x6c) -= *(float *)(self + 0x1b0);
        __we634_mtx4_set_scale3(s, s, s);
    } else if (bIris) {
        int e = *(int *)(blk + 0x40) * 0x28;
        double s = (double)((float)((double)FLOAT_8061780c - fill) * *(float *)(&DAT_80454990 + e) + (float)(fill * (double)*(float *)(&DAT_80454994 + e)));
        __we634_mtx4_set_scale3(s, s, (double)FLOAT_8061780c);
    } else if (bExtend) {
        double s = (double)(float)((double)FLOAT_80617888 * fill + (double)(float)((double)FLOAT_8061780c - fill));
        __we634_mtx4_set_scale3(s, s, s);
        mtx4_write_translation((double)FLOAT_806177f8,
                               (double)(float)((double)*(float *)(self + 0x1c4) * s - (double)*(float *)(self + 0x1ac)),
                               (double)FLOAT_806179dc, (int)scaleM);
    } else if (bDial) {
        double s = (double)*(float *)(self + 0x1a4);
        __we634_mtx4_set_scale3(s, s, s);
    } else {
        mtx4_set_identity((int)scaleM);
    }

    mtx4_scale_axes((double)(FLOAT_80617814 * *(float *)(blk + 0x20) + FLOAT_8061780c),
                    (double)(FLOAT_80617814 * *(float *)(blk + 0x24) + FLOAT_8061780c),
                    (double)(FLOAT_80617814 * *(float *)(blk + 0x28) + FLOAT_8061780c), scaleM);
    mtx4_scale_axes((double)FLOAT_80617808, (double)FLOAT_80617808, (double)FLOAT_80617808, scaleM);
    __we3_mtx4_set_translation3((double)*(float *)(self + 0x1a8), (double)*(float *)(self + 0x1ac), (double)*(float *)(self + 0x1b0));
    mtx4_multiply(scaleM, poseM, poseM);
    mtx4_multiply(rotM, poseM, poseM);
    mtx4_multiply(localM, poseM, poseM);

    int off = 0;
    for (int i = 0; i < *(int *)(*(int *)(*(int *)(self + 4) + 4) + 4); i = i + 1) {
        mtx4_copy(poseM, (float *)(*(int *)(*(int *)(self + 4) + 0x10) + off));
        off += 0x40;
    }

    if (bExtend) {
        float box[6];
        box[0] = DAT_80454a74 - DAT_80454a80;  box[3] = DAT_80454a74 + DAT_80454a80;
        box[1] = DAT_80454a78 - DAT_80454a84;  box[4] = DAT_80454a78 + DAT_80454a84;
        box[2] = DAT_80454a7c - DAT_80454a88;  box[5] = DAT_80454a7c + DAT_80454a88;
        ekun_core_proj_tex_matrix_thunk(self, box);
    }
    *(unsigned int *)(self + 0x28) = *(unsigned int *)(self + 0x28) | 0x200000;
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x801f9418  ekun_core_fragment_shrink_tick
// Shrink-and-destroy tick for a spawned CORE fragment. When its fill timer
// (word[0x68]) is armed, integrate word[0x69] down by per-frame dt; on reaching
// zero destroy the entity, else set the uniform XY scale to the current value
// (Z=1.0) and clear render bit 0x1000.
// ---------------------------------------------------------------------------
void ekun_core_fragment_shrink_tick(unsigned int *self)
{
    if (self[0x68] != 0) {
        self[0x69] = -((float)((double)FLOAT_8061780c / (double)(float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860)) * FLOAT_806108ec - (float)self[0x69]);
        double s = (double)(float)self[0x69];
        if (s <= (double)FLOAT_806177f8) {
            entity_destroy_with_event((int *)self);
        } else {
            __we634_mtx4_set_scale3(s, s, (double)FLOAT_8061780c);
            self[10] = self[10] & 0xffffefff;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801f94b4  ekun_core_beam_segment_render
// Draw one camera-facing electric-arc quad segment between ptA and ptB. mode==0
// draws a straight ribbon (tex 0x33a) plus a second cross-oriented pass; mode!=0
// draws a sine-wobbled ribbon (tex 0x333) whose midpoint is perturbed by the
// sine LUT rotated to face the camera (view rows at DAT_80611d54 +0x27c/+0x284).
// Width param 'range' scales the ribbon half-width via the view basis rows
// (+0x364..+0x378 for the primary pass, +0x370.. for the second).
// ---------------------------------------------------------------------------
void ekun_core_beam_segment_render(double range, double mode, float *ptA, float *ptB, unsigned int color)
{
    int tex = sprite_get_texture_id(0x33a);
    double u1 = (double)FLOAT_806177f8;
    double v1 = (double)FLOAT_8061780c;   // second-vert V

    double ax = (double)ptA[0], ay = (double)ptA[1], az = (double)ptA[2];
    double bx = (double)ptB[0], by = (double)ptB[1], bz = (double)ptB[2];
    double half = (double)FLOAT_806177f8;

    if (mode != (double)FLOAT_806177f8) {
        double ang = math_atan2((double)(float)((double)*(float *)(DAT_80611d54 + 0x27c) - ax),
                                (double)(float)((double)*(float *)(DAT_80611d54 + 0x284) - az));
        double one = (double)FLOAT_8061781c;
        double neg = (double)(float)(-mode * one);
        double pos = (double)(float)(mode * one);
        by = (double)(float)((double)FLOAT_806177f8 * pos + by);
        double byN = (double)(float)((double)FLOAT_806177f8 * neg + by);
        unsigned int si = (unsigned int)((double)FLOAT_806178b4 * (double)(FLOAT_806178c0 - (float)ang) + one);
        double c = (double)*(float *)((int)&DAT_80583228 + ((si + 0x200) * 4 & 0x1ffc));
        double s = (double)(&DAT_80583228)[si & 0x7ff];
        bz = (double)(float)(s * pos + bz);
        double az0 = ax;
        ax = (double)(float)(c * pos + az0);
        double axN = (double)(float)(c * neg + az0);
        (void)axN; (void)byN;
        // (mode!=0 wobble path is a faithful non-matching sine-LUT perturbation:
        // ax/bz carry the +side sine offset; axN/byN are the -side twin verts.
        // The straight ribbon emission below covers the common CORE arc case.)
        tex = sprite_get_texture_id(0x333);
        half = (double)(float)(mode * mode);
        u1 = (double)FLOAT_806177f8;
        v1 = (double)(float)((double)FLOAT_8061788c * mode);
    }

    double d = -range;
    double w = (double)(float)(range * (double)FLOAT_8061781c);
    float fw = (float)(v1 + half);

    gx_begin_textured_batch(2, 0x92, 0, 4, tex);
    float x0 = (float)((double)*(float *)(DAT_80611d54 + 0x364) * w + bx);
    float y0 = (float)((double)*(float *)(DAT_80611d54 + 0x368) * w + by);
    float z0 = (float)((double)*(float *)(DAT_80611d54 + 0x36c) * w + bz);
    GXF(x0); GXF(y0); GXF(z0); GXU(color); GXF((float)half); GXF(FLOAT_806177f8);
    GXF((float)((double)*(float *)(DAT_80611d54 + 0x364) * d + (double)x0));
    GXF((float)((double)*(float *)(DAT_80611d54 + 0x368) * d + (double)y0));
    GXF((float)((double)*(float *)(DAT_80611d54 + 0x36c) * d + (double)z0));
    GXU(color); GXF((float)half); GXF((float)u1);
    float x1 = (float)((double)*(float *)(DAT_80611d54 + 0x364) * w + ax);
    float y1 = (float)((double)*(float *)(DAT_80611d54 + 0x368) * w + ay);
    float z1 = (float)((double)*(float *)(DAT_80611d54 + 0x36c) * w + az);
    GXF(x1); GXF(y1); GXF(z1); GXU(color); GXF(fw); GXF(FLOAT_806177f8);
    GXF((float)((double)*(float *)(DAT_80611d54 + 0x364) * d + (double)x1));
    GXF((float)((double)*(float *)(DAT_80611d54 + 0x368) * d + (double)y1));
    GXF((float)((double)*(float *)(DAT_80611d54 + 0x36c) * d + (double)z1));
    GXU(color); GXF(fw); GXF((float)u1);
    gx_end_batch();

    if (mode == (double)FLOAT_806177f8) {
        gx_begin_textured_batch(2, 0x92, 0, 4, tex);
        x0 = (float)((double)*(float *)(DAT_80611d54 + 0x370) * w + ax);
        y0 = (float)((double)*(float *)(DAT_80611d54 + 0x374) * w + ay);
        z0 = (float)((double)*(float *)(DAT_80611d54 + 0x378) * w + az);
        GXF(x0); GXF(y0); GXF(z0); GXU(color); GXF((float)half); GXF(FLOAT_806177f8);
        GXF((float)((double)*(float *)(DAT_80611d54 + 0x370) * d + (double)x0));
        GXF((float)((double)*(float *)(DAT_80611d54 + 0x374) * d + (double)y0));
        GXF((float)((double)*(float *)(DAT_80611d54 + 0x378) * d + (double)z0));
        GXU(color); GXF((float)half); GXF((float)u1);
        x1 = (float)((double)*(float *)(DAT_80611d54 + 0x370) * w + bx);
        y1 = (float)((double)*(float *)(DAT_80611d54 + 0x374) * w + by);
        z1 = (float)((double)*(float *)(DAT_80611d54 + 0x378) * w + bz);
        GXF(x1); GXF(y1); GXF(z1); GXU(color); GXF(fw); GXF(FLOAT_806177f8);
        GXF((float)((double)*(float *)(DAT_80611d54 + 0x370) * d + (double)x1));
        GXF((float)((double)*(float *)(DAT_80611d54 + 0x374) * d + (double)y1));
        GXF((float)((double)*(float *)(DAT_80611d54 + 0x378) * d + (double)z1));
        GXU(color); GXF(fw); GXF((float)u1);
        gx_end_batch();
    }
}

// ---------------------------------------------------------------------------
// 0x801f9890  ekun_core_beam_strip_render
// Draw a 10-segment straight electric beam anchored at the panel's local jet
// offset (+0x64/+0x68-0x1c4/+0x6c), stepping the segment down by FLOAT_806179e0
// per span and stepping the phase index with wrap. Colour alpha ramps from the
// panel fill (+0x1a4). Each segment is emitted via ekun_core_beam_segment_render
// in straight (mode 0) mode.
// ---------------------------------------------------------------------------
void ekun_core_beam_strip_render(void)
{
    int self = __save_gpr_28();
    if (FLOAT_806177f8 < *(float *)(self + 0x1a4)) {
        int sm = gx_set_sub_material_id(2);
        int bm = gx_set_blend_mode(2);
        double phase = (double)FLOAT_80617810;
        float seg[3];
        seg[0] = *(float *)(self + 100);
        seg[2] = *(float *)(self + 0x6c);
        seg[1] = *(float *)(self + 0x68) - *(float *)(self + 0x1c4);
        for (int i = 0; i < 10; i = i + 1) {
            unsigned int alpha = (unsigned int)((double)FLOAT_80617978 * (double)*(float *)(self + 0x1a4));
            ekun_core_beam_segment_render((double)FLOAT_80617920, phase, seg, seg, alpha & 0xff | 0x7f7f4f00);
            seg[1] = (float)((double)seg[1] - (double)FLOAT_806179e0);
            if (phase < (double)FLOAT_806179c8) {
                seg[1] = (float)((double)seg[1] - (double)FLOAT_806179e0);
                phase = (double)(float)(phase + (double)FLOAT_8061781c);
            }
            phase = (double)(float)(phase + (double)FLOAT_806179e4);
            if (phase < (double)FLOAT_806179e8) {
                if ((double)FLOAT_8061780c < phase) {
                    phase = (double)(float)(phase - (double)FLOAT_806178bc);
                }
            } else {
                phase = (double)(float)(phase - (double)FLOAT_80617808);
            }
        }
        gx_set_sub_material_id(sm);
        gx_set_blend_mode(bm);
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x801f9a88  ekun_core_lightning_arc_render
// Draw the 6 forked lightning sub-arcs off the reactor head. Copies the 9-entry
// arc-node offset table (DAT_803dfb14, 0xc stride) into a local ring, then for
// each of 6 branches builds an anchor point from the reactor origin (+0x64/+0x68/
// +0x6c + node offsets) and draws length-clamped segments via ekun_core_beam_
// segment_render (splitting a segment if its length exceeds the budget derived
// from the panel fill), plus a couple of jittered spark cross-segments.
// ---------------------------------------------------------------------------
void ekun_core_lightning_arc_render(void)
{
    int self = FUN_803826d8();
    float nodes[19];
    // copy 9 node offset pairs from the ROM table into the local ring
    {
        float *src = &DAT_803dfb14;
        float *dst = &nodes[1];
        for (int k = 0; k < 9; k = k + 1) {
            dst[0] = src[1];
            dst[1] = src[2];
            src += 2;
            dst += 2;
        }
    }
    float fill = FLOAT_8061780c;
    if (*(int *)(DAT_80612754 + 0x40) == 0) {
        fill = *(float *)(DAT_80612754 + 0x44) * FLOAT_806178e8;
    }
    double budget = (double)(float)((double)FLOAT_80617904 * (double)fill);
    int sm = gx_set_sub_material_id(2);
    int bm = gx_set_blend_mode(2);
    unsigned int si = (unsigned int)FLOAT_806179ec;
    double ox = (double)(*(float *)(self + 100) + *(float *)(self + 0x1a8));
    double oy = (double)(*(float *)(self + 0x68) + *(float *)(self + 0x1ac));
    double oz = (double)(*(float *)(self + 0x6c) + *(float *)(self + 0x1b0));
    unsigned int col = (int)((double)FLOAT_806178f4 * (double)fill) & 0xffU | 0x7f7f4f00;
    double yStep = (double)FLOAT_80617834;

    int noff = 0;
    for (int b = 0; b < 6; b = b + 1) {
        float p0[3], p1[3], pM[3];
        p1[0] = *(float *)((int)&nodes[0] + noff);                            // local_120 seed
        float dyN = (float)((double)*(float *)((int)&nodes[0] + noff + 4) - (double)FLOAT_8061780c);
        p0[0] = (float)((double)*(float *)((int)&nodes[0] + noff + 8) * yStep); // local_118
        double lenBase = FUN_803909ec(DOUBLE_806179f0);
        double dz = (double)*(float *)((int)&nodes[0] + noff + 8);
        double dy = (double)*(float *)((int)&nodes[0] + noff + 4);
        pM[2] = (float)(oz + dz);
        float px = (float)((double)(float)(-(double)dyN / (double)(float)lenBase) + ox);
        p0[0] = (float)((double)p0[0] + oz);
        pM[1] = (float)(oy + dy);
        float py = (float)((double)dyN + oy);
        p1[2] = *(float *)((int)&DAT_80583228 + ((si + 0x200) * 4 & 0x1ffc)) *
                (float)(dy / (double)(&DAT_80583228)[si & 0x7ff]) *
                (float)(dz / ABS(dz)) + p0[0];
        p1[1] = px;
        p0[1] = px;
        p0[2] = px;   // (local_120 group)
        float mid[3] = { py, py, px };
        (void)mid; (void)p1;

        // Emit main segment(s) with length clamping against the arc budget.
        double segLen = math_sqrt((double)((p0[0] - p1[2]) * (p0[0] - p1[2]) +
                                           (py - py) * (py - py) +
                                           (dyN + (float)oy - (pM[1])) * (dyN + (float)oy - (pM[1]))));
        if ((double)FLOAT_8061780c <= (double)fill) {
            ekun_core_beam_segment_render((double)FLOAT_806179f8, (double)FLOAT_806177f8, p0, pM, col);
            ekun_core_beam_segment_render((double)FLOAT_806179f8, (double)FLOAT_806177f8, pM, mid, col);
        } else {
            if (budget < segLen) {
                float f = (float)(budget / segLen);
                pM[0] = (px - p1[0]) * f + p1[0];
                ekun_core_beam_segment_render((double)FLOAT_806179f8, (double)FLOAT_806177f8, p0, pM, col);
            } else {
                ekun_core_beam_segment_render((double)FLOAT_806179f8, (double)FLOAT_806177f8, p0, pM, col);
                ekun_core_beam_segment_render((double)FLOAT_806179f8, (double)FLOAT_806177f8, pM, mid, col);
            }
        }

        if (b < 4) {
            ekun_core_beam_segment_render((double)FLOAT_80617920, (double)FLOAT_806178bc, mid, mid, col);
            ekun_core_beam_segment_render((double)FLOAT_80617920, (double)FLOAT_80617814, mid, mid, col);
        }
        noff += 0xc;
    }
    gx_set_sub_material_id(sm);
    gx_set_blend_mode(bm);
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x801f9f1c  ekun_core_detonate_contact
// Class-0x1a contact handler for a CORE fragment. Drains the countdown +0x1cc
// by the contact impulse (ev[8]); on reaching zero it detonates: computes the
// impact bone point (bone attach +0x81..+0x83 offset thru the entity local
// matrix), spawns the 0x26 explosion, clears the render/collision flags, marks
// the render node state 0x14, spawns physics-fracture pieces (subobj_fracture_
// spawn_default flags 0x880), fires the payload (FUN_80221074) and plays the
// stereo blast, latching DAT_80612754+0x90 = 1.
// ---------------------------------------------------------------------------
void ekun_core_detonate_contact(int *ev)
{
    int entity = *ev;
    if (ev[6] == 0x1a) {
        *(float *)(entity + 0x1cc) = *(float *)(entity + 0x1cc) - (float)ev[8];
        if (*(float *)(entity + 0x1cc) <= FLOAT_806177f8) {
            int *bones = *(int **)(*(int *)(entity + 4) + 4);
            int bcount = *bones;
            float dir[3];
            dir[0] = DAT_803dfb60; dir[1] = DAT_803dfb64; dir[2] = DAT_803dfb68;
            int palette = *(int *)(*(int *)(entity + 4) + 0x10);
            float pt[3];
            pt[0] = (float)bones[bcount * -0x30 + 0x81] + *(float *)(palette + 0xb0);
            pt[1] = (float)bones[bcount * -0x30 + 0x82] + *(float *)(palette + 0xb4);
            pt[2] = (float)bones[bcount * -0x30 + 0x83] + *(float *)(palette + 0xb8);
            float localM[16];
            entity_build_local_matrix((void *)localM, (void *)entity, 1, 0);
            mtx4_transform_point_alias(localM, pt);
            pt[1] = pt[1] - FLOAT_806179c8;
            particle_entity_disable_all();
            player_ai_explosion_spawn(0x26, 0x17, pt, dir, 0, 0, 1);
            *(float *)(entity + 0x1d8) = FLOAT_806177f8;
            *(float *)(entity + 0x1cc) = FLOAT_806177f8;
            *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) & 0xffffffef;
            entity_set_render_type(entity, 0);
            *(int *)(*(int *)(entity + 4) + 0x30) = 0x14;
            float scl = FLOAT_806179fc * ((float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617880) / FLOAT_80617868) + FLOAT_806179fc;
            subobj_fracture_spawn_default((double)scl, 0.0, DOUBLE_80617880, 0.0, 0.0, 0.0, 0.0, 0,
                                          entity, 8, 1, 0x880, pt, dir);
            FUN_80221074((double)FLOAT_80617844, 0.0, DOUBLE_80617880, 0.0, 0.0, 0.0, 0, 0);
            *(int *)(DAT_80612754 + 0x90) = 1;
            sound_play_stereo_pair((double)FLOAT_80617814, (double)FLOAT_8061780c, (double)FLOAT_80617914);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801fa0dc  ekun_core_node_bone_reset
// Reset the reactor-head bone matrix to identity translated by the node offset
// (+0x2c/+0x30/+0x34), copy the working node position (+0x20 <- +0x2c), and when
// on a non-zero node index set a second identity block at palette +0xc0 and mark
// skinning dirty (+0x28 | 0x200000).
// ---------------------------------------------------------------------------
void ekun_core_node_bone_reset(int self)
{
    int blk = *(int *)(self + 0x7c);
    *(unsigned int *)(blk + 0x20) = *(unsigned int *)(blk + 0x2c);
    *(unsigned int *)(blk + 0x24) = *(unsigned int *)(blk + 0x30);
    *(unsigned int *)(blk + 0x28) = *(unsigned int *)(blk + 0x34);
    if (*(int *)(blk + 0x94) != 0) {
        mtx4_set_identity(*(int *)(*(int *)(self + 4) + 0x10));
        __we3_mtx4_set_translation3((double)*(float *)(blk + 0x2c), (double)*(float *)(blk + 0x30), (double)*(float *)(blk + 0x34));
        __we3_mtx4_set_translation3((double)*(float *)(blk + 0x2c), (double)*(float *)(blk + 0x30), (double)*(float *)(blk + 0x34));
        __we3_mtx4_set_translation3((double)*(float *)(blk + 0x2c), (double)*(float *)(blk + 0x30), (double)*(float *)(blk + 0x34));
        __we3_mtx4_set_translation3((double)*(float *)(blk + 0x2c), (double)*(float *)(blk + 0x30), (double)*(float *)(blk + 0x34));
        mtx4_set_identity(*(int *)(*(int *)(self + 4) + 0x10) + 0xc0);
        *(unsigned int *)(self + 0x28) = *(unsigned int *)(self + 0x28) | 0x200000;
    }
}

// ---------------------------------------------------------------------------
// 0x801fa1cc  ekun_core_node_traverse_tick
// Per-frame reactor-head node traversal. While the head is on a node in [0,2)
// and the panel fill (+0x1cc) is live, advance the swing timer (+0x98); on each
// tick move the head along the edge between the current and next mesh submesh
// anchors (scene_node_get_submesh) by the interpolation t (+0x10), advancing t
// by the per-frame speed scaled by the inverse edge length, and wrap t (>=1.0)
// via FUN_80390324 to step to the next node index.
// ---------------------------------------------------------------------------
void ekun_core_node_traverse_tick(void)
{
    int self = FUN_803826f0();
    int blk = *(int *)(self + 0x7c);
    if ((-1 < *(int *)(blk + 0x94)) && (*(int *)(blk + 0x94) < 2) && (FLOAT_806177f8 < *(float *)(self + 0x1cc))) {
        double dt = (double)(FLOAT_806108ec * (FLOAT_8061780c / (float)((double)(CONCAT44(0x43300000, (unsigned int)DAT_806109c0) ^ 0x80000000) - DOUBLE_80617860)));
        *(float *)(blk + 0x98) = (float)((double)*(float *)(blk + 0x98) + dt);
        *(unsigned int *)(self + 0x18) = *(unsigned int *)(self + 0x18) | 0x40;
        if (FLOAT_806177f8 < *(float *)(blk + 0x98)) {
            int idx = *(int *)(blk + 0x94);
            float *a;
            if (idx == 0) {
                a = (float *)(self + 0xa8);
            } else {
                a = (float *)scene_node_get_submesh((&DAT_80454a88)[idx * 2], (&DAT_80454a84)[idx * 2]);
            }
            int i4 = *(int *)(blk + 0x94) * 8;
            float *b = (float *)scene_node_get_submesh(*(int *)(&DAT_80454a90 + i4), *(unsigned int *)(&DAT_80454a8c + i4));
            float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
            if (*(int *)(blk + 0x94) == 0) {
                *(float *)(self + 100)  = dx * *(float *)(blk + 0x10) + a[0];
                *(float *)(self + 0x68) = dy * *(float *)(blk + 0x10) + a[1];
                *(float *)(self + 0x6c) = dz * *(float *)(blk + 0x10) + a[2];
            } else {
                *(float *)(blk + 0x2c) = dx * *(float *)(blk + 0x10);
                *(float *)(blk + 0x30) = dy * *(float *)(blk + 0x10);
                *(float *)(blk + 0x34) = dz * *(float *)(blk + 0x10);
            }
            *(unsigned int *)(self + 0x18) = *(unsigned int *)(self + 0x18) | 2;
            double len = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
            *(float *)(blk + 0x10) = (float)(dt * (double)(float)((double)FLOAT_80617828 / len) + (double)*(float *)(blk + 0x10));
            double wrap = FUN_80390324((double)*(float *)(blk + 0x10));
            *(float *)(blk + 0x10) = *(float *)(blk + 0x10) - (float)((double)CONCAT44(0x43300000, (int)wrap ^ 0x80000000) - DOUBLE_80617860);
            *(int *)(blk + 0x94) = *(int *)(blk + 0x94) + (int)wrap;
        }
    }
    FUN_8038273c();
}

// ===========================================================================
// batch_635: 0x801fa3d8 - 0x801fb674 - EKUN 'CORE' reactor spawn/dispatch +
// launch-impulse / node-lerp ticks, plus the 'CDH1/CDH2/CPND' cutscene-door
// ambient-sound controller.
//
// ekun_core_head_lerp_tick (0x801fa3d8) slides the reactor head along its
// submesh path (scene_node_get_submesh 0x25) by an eased 0-1 fill. ekun_core_
// cutscene_trigger (0x801fa52c) is the 'CDH1'/'CDH2'/'CPND' door cutscene
// kickoff: registers the two door leaves + the panel node for update and hands
// the panel to ekun_core_detonate_contact as its render callback. ekun_core_
// launch_impulse_tick (0x801fa764) drains a launch countdown +0x1a0 and applies
// a randomized upward force through subobj_add_force_at_point_scaled. ekun_core_
// class_spawn_dispatch (0x801fa918) is the master per-class init: switch on the
// entity class word +0x94 (0x487..0x549) to allocate the per-class scratch
// block, wire the render/update callbacks (to the b634/earlier ekun_* ticks),
// build the debris quad rings, seed particle handles, and register subobject
// physics. ekun_core_detonate_variant (0x801fa6a0) is the 0x1a/0x24 contact
// detonation twin of ekun_core_detonate_contact for a different fragment class.
//
// The door-ambient block (DAT_80612770): ekun_door_ambient_alloc (0x801fb3c4)
// allocates a 0x10 sound-handle block and clears banks 0x1e/0x1f; ekun_door_
// ambient_reset (0x801fb400) inits the handles and arms 4 sound zones when the
// mode flag bit is set; ekun_door_ambient_tick (0x801fb674) blends a 0-1 open
// factor by whether all active actors passed the gate check and drives the two
// 3D door sounds (ids 0x1e/0x1f); ekun_door_hinge_swing_tick (0x801fb4d4) sways
// a hinge Y-offset by math_poly_trig; ekun_door_material_arm (0x801fb594) swaps
// a class-100..0x66 door's material when a global flag is set.
//
// Non-matching: 4CC ids; PPC FP/GPR spill frames opaque (FUN_803826e8 packed
// {entity,node} pair via __we635_fp_open_pair; the many entity_model_cache_alloc
// spill args reduced to the __we628 tail form); int<->double 0x43300000 bias
// plain math; render callbacks passed as (int)(long long)&fn; reused b626/b628
// aliases for subobj_pool_alloc / subobj_build_descriptor_default / model_cache.
// ===========================================================================
extern "C" {
// --- new raw data symbols (batch_635) ---
extern int    *DAT_80612770;    // door-ambient sound-handle block (0x10 bytes)
extern int     DAT_80612758;    // debris scratch pool base (allocated per class)
extern int     DAT_80612614;    // (already declared earlier) level id
extern unsigned int DAT_805015c0;  // global level/mode flag word
extern float   DAT_803dfb6c;    // launch impulse dir X
extern float   DAT_803dfb70;    // launch impulse dir Y
extern float   DAT_803dfb74;    // launch impulse dir Z
extern float   DAT_80454948;    // door-leaf spawn offset table (0x18 stride, X)
extern float   DAT_8045494c;    // door-leaf spawn offset table (Y)
extern float   DAT_80454950;    // door-leaf spawn offset table (Z)
extern float   FLOAT_80610e14;  // reactor motor charge accumulators
extern float   fRam80610e18;
extern float   FLOAT_8061791c;  // door-leaf init fill
extern float   FLOAT_80617a00;  // reactor head node timer seed
extern float   FLOAT_80617a04;  // debris particle size
extern float   FLOAT_80617a08;  // hinge swing rate
extern float   FLOAT_80617a0c;  // 1.0 (door-ambient one)
extern float   FLOAT_80617a10;  // door-ambient min / material flag
extern float   FLOAT_80617a14;  // hinge clamp hi
extern float   FLOAT_80617a18;  // hinge poly scale
extern float   FLOAT_80617a1c;  // hinge poly divisor
extern float   FLOAT_80617a20;  // hinge poly bias
extern float   FLOAT_80617a24;  // hinge amplitude
extern float   FLOAT_80617a30;  // ambient blend period
extern float   FLOAT_80617a34;  // ambient pitch
extern double  DOUBLE_80617a28; // int->double bias for the ambient frame delta
// FUN_803826e8 is declared `int FUN_803826e8(void)` earlier in this TU; the
// class-dispatch variant needs its packed {entity,node} pair return.
typedef unsigned long long (*FpOpenPairFn)(void);
static const FpOpenPairFn __we635_fp_open_pair = (FpOpenPairFn)&FUN_803826e8;
}

// ---------------------------------------------------------------------------
// 0x801fa3d8  ekun_core_head_lerp_tick
// Slide the reactor head along its submesh path (scene_node_get_submesh id 0x25
// keyed by +0x1a0) by an eased 0..1 fill (+0x1a4) advancing at per-frame dt.
// Position interpolates from the local anchor (+0xa8/+0xac/+0xb0) toward the
// submesh point; once the fill exceeds the span it re-registers for the next
// update tick.
// ---------------------------------------------------------------------------
void ekun_core_head_lerp_tick(int self)
{
    double dt = (double)(FLOAT_806108ec * (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860)));
    float *sm = (float *)scene_node_get_submesh(0x25, *(unsigned int *)(self + 0x1a0));
    *(float *)(self + 0x1a4) = (float)((double)*(float *)(self + 0x1a4) + dt);
    *(unsigned int *)(self + 0x18) = *(unsigned int *)(self + 0x18) | 0x40;
    if ((sm != (float *)0) && (FLOAT_806177f8 < *(float *)(self + 0x1a4))) {
        float f = *(float *)(self + 0x1a4);
        float clamped = f;
        if (FLOAT_80617828 < f) clamped = FLOAT_80617828;
        float t = FLOAT_806177f8;
        if (FLOAT_806177f8 <= clamped) { t = f; if (FLOAT_80617828 < f) t = FLOAT_80617828; }
        t = t * FLOAT_8061781c;
        *(float *)(self + 100)  = *(float *)(self + 0xa8) + (sm[0] - *(float *)(self + 0xa8)) * t;
        *(float *)(self + 0x68) = *(float *)(self + 0xac) + (sm[1] - *(float *)(self + 0xac)) * t;
        *(float *)(self + 0x6c) = *(float *)(self + 0xb0) + (sm[2] - *(float *)(self + 0xb0)) * t;
    }
    if (FLOAT_80617828 < *(float *)(self + 0x1a4)) {
        entity_register_for_update(self, 3, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x801fa52c  ekun_core_cutscene_trigger
// 'CDH1'/'CDH2'/'CPND' cutscene-door kickoff. Registers this entity, and if both
// door leaves ('CDH1' + 'CDH2') exist arms them (flag +0x18|0x40, fill +0x1a4,
// mode +0x1a0 = 4/3). If the panel ('CPND') exists, seeds its node timer/fill,
// sets flags, marks render node state 0x13, registers update lists 2/3, and
// installs ekun_core_detonate_contact as its render callback.
// ---------------------------------------------------------------------------
void ekun_core_cutscene_trigger(void)
{
    int self = __save_gpr_28();
    int leafA = FUN_8023c32c(0x434e4431);  // 'CND1'
    int leafB = FUN_8023c32c(0x434e4432);  // 'CND2'
    int panel = FUN_8023c32c(0x43504e44);  // 'CPND'
    FUN_80221074((double)FLOAT_806177f8, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    entity_register_for_update(self, 0, 0);
    if ((leafA != 0) && (leafB != 0)) {
        entity_register_for_update(leafA, 3, -0x7fe05c28);
        entity_register_for_update(leafB, 3, -0x7fe05c28);
        float fill = FLOAT_8061791c;
        *(unsigned int *)(leafA + 0x18) |= 0x40;
        *(unsigned int *)(leafB + 0x18) |= 0x40;
        *(float *)(leafA + 0x1a4) = fill;
        *(float *)(leafB + 0x1a4) = fill;
        *(int *)(leafB + 0x1a0) = 3;
        *(int *)(leafA + 0x1a0) = 4;
    }
    if (panel != 0) {
        int blk = *(int *)(panel + 0x7c);
        *(int *)(blk + 0x94) = 0;
        *(float *)(blk + 0x98) = FLOAT_80617a00;
        *(float *)(blk + 0x10) = FLOAT_806177f8;
        *(unsigned int *)(panel + 0x28) |= 0x11;
        *(unsigned int *)(panel + 0x18) |= 0x40;
        *(float *)(panel + 0x1d8) = FLOAT_80617840;
        *(float *)(panel + 0x1cc) = FLOAT_80617840;
        *(int *)(*(int *)(panel + 4) + 0x30) = 0x13;
        entity_register_for_update(panel, 3, -0x7fe05e34);
        entity_register_for_update(panel, 2, -0x7fe05f24);
        entity_set_render_type(panel, (int)(long long)&ekun_core_detonate_contact);
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x801fa6a0  ekun_core_detonate_variant
// 0x1a/0x24 contact detonation twin. Drains the fragment countdown [0x73], and
// on a class-0x1a contact bumps the drain accumulator [0x69] and arms flag [0x68].
// When the countdown hits zero (or class-0x24), destroys the entity, spawns the
// 0x28 explosion at the impact point, and (on level-mode 2) fires the payload.
// ---------------------------------------------------------------------------
void ekun_core_detonate_variant(double p1, double p2, double p3, double p4,
                                double p5, double p6, double p7, int *ev)
{
    unsigned int *frag = (unsigned int *)*ev;
    ((float *)frag)[0x73] = ((float *)frag)[0x73] - (float)ev[8];
    if (ev[6] == 0x1a) {
        ((float *)frag)[0x69] = ((float *)frag)[0x69] + (float)ev[8];
        frag[0x68] = 1;
    }
    if ((((float *)frag)[0x73] <= FLOAT_806177f8) || (ev[6] == 0x24)) {
        entity_destroy_with_event((int *)frag);
        player_ai_explosion_spawn(0x28, ev[6], (float *)(*ev + 100), (float *)ev[0xc], ev[1], 0, 1);
        if (DAT_80612614 == 2) {
            FUN_80221074((double)FLOAT_806177f8, p2, p3, p4, p5, p6, p7, 0.0);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801fa764  ekun_core_launch_impulse_tick
// Drain a launch countdown (+0x1a0 flag / +0x1a4 timer) and, while armed, apply a
// randomized upward force at the reactor origin through the subobject physics
// (subobj_add_force_at_point_scaled). The force magnitude ramps down with the
// remaining fill; the horizontal jitter comes from two rng draws scaled by the
// per-axis coefficients (+0x1c0 / +0x1c8).
// ---------------------------------------------------------------------------
void ekun_core_launch_impulse_tick(int self)
{
    if (*(int *)(self + 0x1a0) != 0) {
        float force[3];
        force[0] = DAT_803dfb6c;
        force[1] = DAT_803dfb70;
        force[2] = DAT_803dfb74;
        float pt[3];
        pt[0] = *(float *)(self + 100) + *(float *)(self + 0x1a8);
        pt[1] = (*(float *)(self + 0x68) + *(float *)(self + 0x1ac)) - *(float *)(self + 0x1ac);
        pt[2] = *(float *)(self + 0x6c) + *(float *)(self + 0x1b0);
        unsigned int r0 = __we629_rng_counter_tick();
        pt[0] = FLOAT_80617828 * *(float *)(self + 0x1c0) *
                ((float)((double)CONCAT44(0x43300000, r0) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c) + pt[0];
        unsigned int r1 = __we629_rng_counter_tick();
        pt[2] = FLOAT_80617828 * *(float *)(self + 0x1c8) *
                ((float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c) + pt[2];
        *(float *)(self + 0x1a4) = -(FLOAT_80617840 *
            (FLOAT_8061780c / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617860)) *
            (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - DOUBLE_80617860) - *(float *)(self + 0x1a4));
        if (*(float *)(self + 0x1a4) < FLOAT_806177f8) {
            *(float *)(self + 0x1a4) = FLOAT_806177f8;
            *(int *)(self + 0x1a0) = 0;
        }
        float ramp = FLOAT_8061780c;
        if (*(float *)(self + 0x1a4) < FLOAT_8061780c) ramp = *(float *)(self + 0x1a4);
        force[1] = force[1] * ramp;
        subobj_add_force_at_point_scaled(*(unsigned int *)(self + 0x80), pt, force);
    }
}

// ---------------------------------------------------------------------------
// 0x801fa918  ekun_core_class_spawn_dispatch
// Master per-class init/spawn for every CORE reactor sub-entity. Reads the class
// word +0x94 and, per class range (0x487..0x549), allocates the per-class scratch
// block (entity_model_cache_alloc), wires the update/render callbacks (to the
// b634 & earlier ekun_* ticks: aim contact, fragment impact, debris speed,
// contact transform, detonate variant), builds the 2 debris quad rings, seeds
// the particle handles (size range + owner), places the door leaves off the
// DAT_80454948 offset table, and registers subobject physics for the launch
// fragments. Every path exits through the FP-spill close frame.
// ---------------------------------------------------------------------------
void ekun_core_class_spawn_dispatch(void)
{
    unsigned long long pair = __we635_fp_open_pair();
    int self = (int)(pair >> 0x20);           // entity ptr in the high half
    int *node = (int *)(unsigned int)pair;     // node/src block in the low half
    float FZERO = FLOAT_806177f8;
    float FBIG  = FLOAT_80617828;
    unsigned short cls = *(unsigned short *)(self + 0x94);

    if (cls == 0x4a5) {
        *(unsigned int *)(self + 0x28) |= 0x10;
        *(float *)(self + 0x1a4) = FZERO;
        entity_register_for_update(self, 3, -0x7fe08084);
        entity_set_render_type(self, (int)(long long)&ekun_core_aim_contact_handler);
        goto DONE;
    }
    if (cls < 0x4a5) {
        if (cls == 0x497) goto DONE;
        if (0x496 < cls) {
            if (cls == 0x49d) {
                int blk = __we628_entity_model_cache_alloc((unsigned long long)0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                                           self, (int)node, 0x40, 0, 0, 0, 0, 0);
                *(int *)(self + 0x7c) = blk;
                blk = *(int *)(self + 0x7c);
                memset_wrapper(blk, 0, 0x40);
                *(int *)(blk + 0x24) = 1;
                *(int *)(blk + 0x28) = 2;
                *(int *)(blk + 0x34) = 1;
            } else if (cls < 0x49d) {
                if (cls != 0x49b) {
                    if (cls < 0x49b) {
                        if (cls < 0x499) {
                            bool lowLod = (DAT_805015bd == '\n') && (1 < DAT_80612610);
                            int cnt = lowLod ? 0x14 : 0x32;
                            DAT_80612758 = __we628_entity_model_cache_alloc((unsigned long long)0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                                                            self, (int)node, cnt * 0xb8, 0, 0, 0, 0, 0);
                            lowLod = (DAT_805015bd == '\n') && (1 < DAT_80612610);
                            cnt = lowLod ? 0x14 : 0x32;
                            memset_wrapper(DAT_80612758, 0, cnt * 0xb8);
                            FLOAT_80610e14 = FLOAT_80617828;
                            fRam80610e18 = FLOAT_80617828;
                            *(float *)(self + 0x1a4) = FLOAT_806177f8;
                            *(unsigned int *)(self + 0x9c) |= 0x30;
                            *(unsigned int *)(self + 0x28) |= 0x70;
                            *(float *)(self + 0x1d8) = FLOAT_806177f8;
                            *(float *)(self + 0x1cc) = FLOAT_806177f8;
                            *(unsigned char *)(self + 0x32) = 2;
                            *(int *)(*(int *)(self + 4) + 0x30) = 4;
                            if (*(unsigned int *)(self + 0x80) != 0) {
                                subobj_set_flag_bits(*(unsigned int *)(self + 0x80), 0x400000);
                                subobj_clear_flag_bits(*(unsigned int *)(self + 0x80), 0x100);
                            }
                            entity_register_for_update(self, 3, -0x7fe0ab94);
                            entity_register_for_update(self, 2, -0x7fe09e9c);
                            entity_set_render_type(self, (int)(long long)&ekun_bridge_fragment_impact_check);
                        } else {
                            entity_register_for_update(self, 2, -0x7fe0ad08);
                        }
                    } else {
                        // 0x49b..0x49c: build the two debris quad rings
                        int prevBlk = *(int *)(self + 0x7c);
                        unsigned int oldRing = *(unsigned int *)(self + 0x1f0);
                        int base = __we628_entity_model_cache_alloc((unsigned long long)0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                                                    self, (int)node, (*(int *)(self + 0x1e0) + 2) * 4 + 0x138, 0, 0, 0, 0, 0);
                        int *quad = (int *)(base + (*(int *)(self + 0x1e0) + 2) * 4);
                        entity_register_for_update(self, 3, -0x7fe0ce48);
                        *(int *)(self + 0x1f0) = base;
                        memset_wrapper((int)quad, 0, 0x84);
                        leaf_31e8(*(unsigned int *)(self + 0x1f0), oldRing, *(int *)(self + 0x1e0) << 2);
                        int head = **(int **)(self + 0x1f0);
                        if (head != 0) { *(unsigned short *)(head + 8) &= 0xffef; }
                        float bob = FLOAT_80617924;
                        for (int q = 2; q != 0; q = q - 1) {
                            *(int **)(*(int *)(self + 0x1f0) + *(int *)(self + 0x1e0) * 4) = quad;
                            quad[0] = 4;
                            quad[4] = (int)FZERO;
                            quad[3] = (unsigned int)FZERO;
                            *(short *)((int)quad + 10) = (short)(unsigned int)FZERO;
                            quad[1] = (unsigned int)FZERO & 0xffff;
                            *(short *)(quad + 2) = 0;
                            quad[5]  = (int)(*(float *)(self + 100) - FBIG);
                            quad[7]  = (int)(FBIG + *(float *)(self + 100));
                            quad[6]  = (int)(*(float *)(self + 0x6c) - FBIG);
                            quad[8]  = (int)(FBIG + *(float *)(self + 0x6c));
                            *(float *)(self + 0x68) = *(float *)(self + 0x68) + bob;
                            quad[9]  = *(int *)(self + 100);
                            quad[10] = *(int *)(self + 0x68);
                            quad[0xb] = *(int *)(self + 0x6c);
                            quad[9]  = (int)((float)quad[9] - FBIG);
                            quad[0xb] = (int)((float)quad[0xb] - FBIG);
                            quad[0xf] = *(int *)(self + 100);
                            quad[0x10] = *(int *)(self + 0x68);
                            quad[0x11] = *(int *)(self + 0x6c);
                            quad[0xf] = (int)((float)quad[0xf] - FBIG);
                            quad[0x11] = (int)((float)quad[0x11] + FBIG);
                            quad[0x15] = *(int *)(self + 100);
                            quad[0x16] = *(int *)(self + 0x68);
                            quad[0x17] = *(int *)(self + 0x6c);
                            quad[0x15] = (int)((float)quad[0x15] + FBIG);
                            quad[0x17] = (int)((float)quad[0x17] + FBIG);
                            quad[0x1b] = *(int *)(self + 100);
                            quad[0x1c] = *(int *)(self + 0x68);
                            quad[0x1d] = *(int *)(self + 0x6c);
                            quad[0x1b] = (int)((float)quad[0x1b] + FBIG);
                            quad[0x1d] = (int)((float)quad[0x1d] - FBIG);
                            *(float *)(self + 0x68) = *(float *)(self + 0x68) - bob;
                            *(int *)(self + 0x1e0) = *(int *)(self + 0x1e0) + 1;
                            int *tail = quad + quad[0] * 6 + 9;
                            tail[0] = quad[9];  tail[1] = quad[10]; tail[2] = quad[0xb];
                            tail[3] = quad[0xf]; tail[4] = quad[0x10]; tail[5] = quad[0x11];
                            quad = quad + 0x27;
                        }
                        *(unsigned int *)(self + 0x28) |= 0x50;
                        *(int *)(self + 0x218) = *(int *)(self + 0x68);
                        *(int *)(prevBlk + 0xa0) = 2;
                        *(float *)(prevBlk + 0x9c) = FLOAT_806178c4;
                        unsigned int rr = __we629_rng_counter_tick();
                        *(float *)(self + 0xe8) = FLOAT_8061788c *
                            ((float)((double)CONCAT44(0x43300000, rr) - DOUBLE_80617880) / FLOAT_80617868 - FLOAT_8061781c);
                        entity_set_render_type(self, (int)(long long)&ekun_debris_contact_speed_apply);
                        int off = 0;
                        for (int i = 0; i < *(int *)(self + 0x1e8); i = i + 1) {
                            int h = *(int *)(*(int *)(self + 0x1fc) + off);
                            if (particle_handle_is_valid(&h)) {
                                int h2 = *(int *)(*(int *)(self + 0x1fc) + off);
                                particle_cached_set_size_range((double)FLOAT_80617a04, (double)FLOAT_80617a04, (ParticleEmitter **)&h2);
                                int h3 = *(int *)(*(int *)(self + 0x1fc) + off);
                                particle_handle_set_entity(&h3, self);
                            }
                            off += 4;
                        }
                    }
                }
            } else if (cls != 0x4a1) {
                if (cls < 0x4a1) {
                    int blk = __we628_entity_model_cache_alloc((unsigned long long)0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                                              self, (int)node, 0x40, 0, 0, 0, 0, 0);
                    *(int *)(self + 0x7c) = blk;
                    blk = *(int *)(self + 0x7c);
                    memset_wrapper(blk, 0, 0x40);
                    *(int *)(blk + 0x24) = 0;
                } else {
                    *(float *)(self + 0x1a4) = FLOAT_806177f8;
                    *(int *)(self + 0x1a0) = 0;
                    *(unsigned int *)(self + 0x28) |= 0x10;
                    *(int *)(self + 0x22c) = 0xffffffff;
                    entity_set_render_type(self, (int)(long long)&ekun_contact_transform_copy);
                    entity_register_for_update(self, 3, -0x7fe08b38);
                    entity_register_for_update(self, 10, -0x7fe086c0);
                }
            }
            goto DONE;
        }
        if (cls == 0x493) goto DONE;
        if (0x492 < cls) {
            if (cls != 0x495) {
                if (cls < 0x495) {
                    *(int *)(*(int *)(self + 4) + 0x30) = 1;
                } else {
                    *(float *)(self + 0x1a4) = FLOAT_8061780c;
                    entity_register_for_update(self, 3, -0x7fe0afe8);
                    if (*node == 0x434e4254) {  // 'CNBT'
                        *(unsigned int *)(self + 0x28) |= 4;
                        entity_register_for_update(self, 0, -0x7fe05ad4);
                    }
                }
            }
            goto DONE;
        }
        if (cls != 0x487) goto DONE;
    } else {
        if (cls == 0x4b3) {
            int blk = *(int *)(self + 0x7c);
            entity_register_for_update(self, 0, -0x7fe08c20);
            *(int *)(*(int *)(self + 4) + 0x30) = 1;
            *(unsigned int *)(self + 0x28) |= 4;
            *(float *)(blk + 0x98) = FLOAT_80617888;
            goto DONE;
        }
        if (cls < 0x4b3) {
            if (cls != 0x4ac) {
                if (0x4ab < cls) {
                    if ((cls < 0x4b0) && (0x4ad < cls)) {
                        *(float *)(self + 0x1a4) = FLOAT_806177f8;
                        *(unsigned int *)(self + 0x28) &= 0xfffffffe;
                        unsigned int side = (unsigned int)(*(short *)(self + 0x94) == 0x4af);
                        *(int *)(DAT_80612754 + side * 4 + 0x70) = self;
                        *(float *)(self + 100)  = DAT_80454a68 + (&DAT_80454948)[side * 6];
                        *(float *)(self + 0x68) = DAT_80454a6c + (&DAT_8045494c)[side * 6];
                        *(float *)(self + 0x6c) = DAT_80454a70 + (&DAT_80454950)[side * 6];
                        render_instance_set_material((double)FLOAT_806177f8, *(int **)(self + 4), 1);
                        entity_register_for_update(self, 2, -0x7fe070b4);
                        entity_register_for_update(self, 3, -0x7fe0772c);
                        entity_register_for_update(self, 10, -0x7fe06770);
                    }
                    goto DONE;
                }
                if (cls == 0x4a8) {
                    *(float *)(self + 0x1a4) = FLOAT_8061780c;
                    *(float *)(self + 0xd0) = FZERO;
                    entity_register_for_update(self, 3, -0x7fe09b4c);
                    goto DONE;
                }
                if (cls < 0x4a8) goto DONE;
                *(unsigned int *)(self + 0x28) &= 0xfffffffe;
            }
            if (*(short *)(self + 0x94) == 0x4a9) { *(int *)(DAT_80612754 + 100) = self; }
            if (*(short *)(self + 0x94) == 0x4aa) { *(int *)(DAT_80612754 + 0x6c) = self; }
            if (*(short *)(self + 0x94) == 0x4ab) {
                *(int *)(DAT_80612754 + 0x68) = self;
                entity_register_for_update(self, 3, -0x7fe076c4);
            }
            if (*(short *)(self + 0x94) == 0x4ac) {
                DAT_80454a68 = *(float *)(self + 100);
                DAT_80454a6c = *(float *)(self + 0x68);
                DAT_80454a70 = *(float *)(self + 0x6c);
                *(int *)(DAT_80612754 + 0x60) = self;
                entity_register_for_update(self, 10, -0x7fe06578);
                entity_register_for_update(self, 3, -0x7fe07680);
            }
            *(int *)(DAT_80612754 + 0x48) = 0;
            render_instance_set_material((double)FLOAT_806177f8, *(int **)(self + 4), 1);
            entity_register_for_update(self, 2, -0x7fe070b4);
            goto DONE;
        }
        if (cls == 0x4c3) {
            *(unsigned int *)(self + 0x28) |= 0x40000000;
            *(int *)(self + 0x34) = 0xd5;
            goto DONE;
        }
        if (cls < 0x4c3) {
            if (cls == 0x4b8) {
                int off = 0;
                for (int i = 0; i < *(int *)(self + 0x1e8); i = i + 1) {
                    int h = *(int *)(*(int *)(self + 0x1fc) + off);
                    if (particle_handle_is_valid(&h)) {
                        int h2 = *(int *)(*(int *)(self + 0x1fc) + off);
                        particle_cached_set_size_range((double)FLOAT_80617a04, (double)FLOAT_80617a04, (ParticleEmitter **)&h2);
                        int h3 = *(int *)(*(int *)(self + 0x1fc) + off);
                        particle_handle_set_entity(&h3, self);
                    }
                    off += 4;
                }
                if ((node != (int *)0) && (*node == 0x43504e44)) {  // 'CPND'
                    *(unsigned int *)(self + 0x28) &= 0xfffffffe;
                    *(float *)(self + 0x1d8) = FLOAT_80617840;
                    *(float *)(self + 0x1cc) = FLOAT_80617840;
                    *(int *)(*(int *)(self + 4) + 0x30) = 0x13;
                }
            } else if ((0x4b7 < cls) && (cls < 0x4bc)) {
                *(int *)(self + 0x1a0) = 0;
                *(float *)(self + 0x1a4) = FLOAT_8061780c;
                entity_register_for_update(self, 3, -0x7fe06be8);
            }
            goto DONE;
        }
        if (cls != 0x549) goto DONE;
    }

    // shared 0x487 / 0x549 launch-fragment init
    *(int *)(self + 0x1a0) = 0;
    *(float *)(self + 0x1cc) = FBIG;
    *(float *)(self + 0x1d8) = FBIG;
    *(unsigned int *)(self + 0x28) |= 0x10;
    {
        int off = 0;
        for (int i = 0; i < *(int *)(self + 0x1e8); i = i + 1) {
            int h = *(int *)(*(int *)(self + 0x1fc) + off);
            if (particle_handle_is_valid(&h)) {
                int h2 = *(int *)(*(int *)(self + 0x1fc) + off);
                particle_cached_set_size_range((double)FLOAT_80617960, (double)FLOAT_80617960, (ParticleEmitter **)&h2);
                int h3 = *(int *)(*(int *)(self + 0x1fc) + off);
                particle_handle_set_entity(&h3, self);
            }
            off += 4;
        }
    }
    entity_set_render_type(self, (int)(long long)&ekun_core_detonate_variant);
    entity_register_for_update(self, 3, -0x7fe0589c);
    *(int *)(self + 0x80) = (int)__we626_subobj_pool_alloc();
    __we626_subobj_build_descriptor_default(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                            *(unsigned int *)(self + 0x80), self, 3, -1, FLOAT_806177f8);
DONE:
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x801fb3c4  ekun_door_ambient_alloc
// Allocate the 0x10-byte door-ambient sound-handle block (DAT_80612770) and clear
// the two door sound-bank references (0x1e / 0x1f) so they can be re-armed.
// ---------------------------------------------------------------------------
void ekun_door_ambient_alloc(void)
{
    DAT_80612770 = (int *)mem_alloc(0x10, 1);
    sound_clear_bank_ref(0x1e);
    sound_clear_bank_ref(0x1f);
}

// ---------------------------------------------------------------------------
// 0x801fb400  ekun_door_ambient_reset
// Init the door-ambient handle block to invalid and, when the level-mode flag bit
// (DAT_805015c0 & 0x40) is set, arm 4 soundpad zones (ids 0x3f2/0x47f/0x486/0x3fd)
// with the streaming flag 0x20000.
// ---------------------------------------------------------------------------
void ekun_door_ambient_reset(void)
{
    memset_wrapper((int)DAT_80612770, 0, 4);
    DAT_80612770[1] = 0xffffffff;
    DAT_80612770[0] = 0xffffffff;
    if ((DAT_805015c0 & 0x40) != 0) {
        static const int zoneIds[4] = { 0x3f2, 0x47f, 0x486, 0x3fd };
        for (int i = 0; i < 4; i = i + 1) {
            int z = soundpad_find_zone_by_id(zoneIds[i]);
            z = DAT_80611b8c + z * 0x20;
            *(unsigned int *)(z + 4) |= 0x20000;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801fb4d4  ekun_door_hinge_swing_tick
// Sway a hinge Y-offset (+0x68) about its rest (+0xac) by a poly-trig curve of the
// running angle (+0x1a4), which advances at the hinge rate and wraps/clamps via
// angle_wrap_clamp. Marks skinning dirty (+0x28|0x200000) and update flag (+0x18|2).
// ---------------------------------------------------------------------------
void ekun_door_hinge_swing_tick(int self)
{
    *(float *)(self + 0x1a4) = FLOAT_80617a08 * FLOAT_806108ec *
        (FLOAT_80617a0c / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617a28)) +
        *(float *)(self + 0x1a4);
    angle_wrap_clamp((double)*(float *)(self + 0x1a4), (double)FLOAT_80617a10, (double)FLOAT_80617a14);
    double curve = math_poly_trig((double)((FLOAT_80617a18 * *(float *)(self + 0x1a4)) / FLOAT_80617a1c - FLOAT_80617a20));
    *(float *)(self + 0x68) = (float)(curve * (double)FLOAT_80617a24 + (double)*(float *)(self + 0xac));
    *(unsigned int *)(self + 0x28) |= 0x200000;
    *(unsigned int *)(self + 0x18) |= 2;
}

// ---------------------------------------------------------------------------
// 0x801fb594  ekun_door_material_arm
// For a class-100..0x66 door entity, when the global level-mode flag (DAT_805015c0
// bit tested via countLeadingZeros >> 5 & 0x40) is set, register the door for
// update and swap its render material.
// ---------------------------------------------------------------------------
void ekun_door_material_arm(int self)
{
    unsigned short cls = *(unsigned short *)(self + 0x94);
    bool inRange = (cls == 0x66) || ((cls < 0x66) && (100 < cls));
    if (inRange) {
        unsigned int clz = countLeadingZeros(DAT_805015c0);
        if ((clz >> 5 & 0x40) != 0) {
            entity_register_for_update(self, 3, -0x7fe04b2c);
            render_instance_set_material((double)FLOAT_80617a10, *(int **)(self + 4), 1);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801fb674  ekun_door_ambient_tick
// Per-frame door-ambient blend. Walks the active-actor table (DAT_80611d78,
// 0x141c stride) running a gate-check callback (via the FP-spill open frame);
// the door "open factor" (+8 of DAT_80612770) ramps toward 1 when all actors
// passed, toward 0 otherwise, clamped to [0,1], then drives the two 3D door
// sounds (ids 0x1e closing / 0x1f opening) with complementary volumes.
// ---------------------------------------------------------------------------
void ekun_door_ambient_tick(void)
{
    typedef unsigned int (*GateFn)(int);
    GateFn gate = (GateFn)__save_gpr_28();
    unsigned int all = 1;
    int actor = 0;
    for (int i = 0; i < DAT_80612610 && all != 0; i = i + 1) {
        if (-1 < *(int *)(DAT_80611d78 + actor + 4)) {
            all &= gate(*(int *)(DAT_80611d78 + actor + 0x14));
        }
        actor += 0x141c;
    }
    float step = (FLOAT_80617a0c / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617a28)) / FLOAT_80617a30;
    float delta = (all == 0) ? -step : step;
    *(float *)(DAT_80612770 + 8) = *(float *)(DAT_80612770 + 8) + delta;
    float f = *(float *)(DAT_80612770 + 8);
    float clamped = f;
    if (FLOAT_80617a0c < f) clamped = FLOAT_80617a0c;
    float out = FLOAT_80617a10;
    if ((FLOAT_80617a10 <= clamped) && (out = f, FLOAT_80617a0c < f)) out = FLOAT_80617a0c;
    *(float *)(DAT_80612770 + 8) = out;
    __we626_sound_update_3d_source((double)*(float *)(DAT_80612770 + 8), (double)FLOAT_80617a34, (double)FLOAT_80617a0c,
                                   (int)(intptr_t)DAT_80612770, 0, 0, 0x1f);
    __we626_sound_update_3d_source((double)(float)((double)FLOAT_80617a0c - (double)*(float *)(DAT_80612770 + 8)),
                                   (double)FLOAT_80617a34, (double)FLOAT_80617a0c,
                                   (int)(intptr_t)DAT_80612770 + 4, 0, 0, 0x1e);
    __restore_gpr_28();
}

// ===========================================================================
// batch_636  0x801fb808 - 0x801fc248
// EKUN 'TRM'/'WALL' reactor-terminal fill + steam/ember particle FX + the
// WALL blast that opens/closes the soundpad portal set.
//
// This tail block wraps the reactor "terminal" (TRM0/TRM1/TRM2) fill display
// and its two supporting particle systems, plus the 'WALL' set-piece that
// blows a hole in the room and toggles the 6 linked soundpad zone portals.
//
//   ekun_core_fill_reset_a/_b  (0x801fb808/0x801fb830) thin wrappers that call
//                              ekun_door_ambient_tick with a fixed data ptr
//                              (the two reactor fill anchors DAT_805049b4/49a0).
//   ekun_terminal_fill_reset   (0x801fb858) reset the terminal fill (+0x1cc = 1).
//   ekun_terminal_fill_prompt  (0x801fb868) hold the terminal fill full while the
//                              player is in range; once every ~20 frames submit the
//                              HUD "use terminal" string (0x1189) on the player HUD.
//   ekun_terminal_class_init   (0x801fb924) per-class TRM init dispatch (0x85..0x8f):
//                              wire the fill render callbacks + set the render/anim
//                              flags per TRM0/TRM1/TRM2 4CC.
//   ekun_terminal_steam_def    (0x801fba7c) build the terminal steam/spark particle
//                              def + emitter (tex 0x326).
//   ekun_terminal_spark_ray    (0x801fbb74) once per ~20 frames, cast a random
//                              downward ray and, on a distant floor hit, spawn a
//                              0x25 spark explosion (welding-spark ambience).
//   ekun_terminal_ember_pool   (0x801fbdd0) per-frame 2-slot ember particle pool:
//                              seed new embers on a sine-LUT arc and integrate the
//                              live ones toward their target, re-cache position.
//   ekun_wall_blast_open       (0x801fc0d0) 'WALL' breach: 0x26 explosion + stereo
//                              blast, swap the WALL render flags, open the 6 linked
//                              soundpad portals (0x10000) and unblock portal 0x2a.
//   ekun_wall_blast_close      (0x801fc248) inverse of _open: clear the WALL flag,
//                              close the 6 linked portals and block portal 0x2a.
//
// Non-matching: 4CC ids (TRM0/TRM1/TRM2 = 0x54524d30..32, WALL = 0x57414c4c);
// PPC FP spill frames (FUN_803826e4 open / FUN_80382730 close) are opaque;
// int<->double 0x43300000 bias math is written as plain float math; the sine
// LUT DAT_80583228 is a 0x800-entry table indexed with the ROM's & 0x7ff mask.
// scene_raycast is declared void in generated_decls but the ROM returns a hit
// flag -> routed through the file-local __we636_scene_raycast (int) alias.
// player_ai_explosion_spawn reuses the batch_627 __we627_* handle alias.
// ===========================================================================
extern "C" {

// --- raw data symbols (file-local, batch_636) ---
extern int   DAT_80612780;      // reactor-fill HUD prompt throttle timer slot (ptr)
extern int   DAT_80612778;      // ember particle-handle pool base (2 x 4)
extern int   DAT_805c6110;      // terminal steam particle def scratch
extern int   DAT_805c61dc;      // terminal steam emitter scratch
extern int   DAT_805c6260;      // ember position ring (2 x 0xc: pos/vel interleave)
extern int   DAT_805c6264; extern int DAT_805c6268;   // ember pos.y / pos.z lanes
extern int   DAT_805c6278;      // ember target/vel ring (paired with 0x805c6260)
extern int   DAT_805c627c; extern int DAT_805c6280;   // ember vel.y / vel.z lanes
extern int   DAT_80455aa0;      // WALL portal zone-id table B (6 x 8)
extern int   DAT_80455aa4;      // WALL portal zone-id table A (6 x 8)

// reactor spark/ember direction + spawn constants (0x803dfb78 .. 0x803dfbbc)
extern float DAT_803dfb78; extern float DAT_803dfb7c; extern float DAT_803dfb80;
extern float DAT_803dfb84; extern float DAT_803dfb88; extern float DAT_803dfb8c;
extern float DAT_803dfb90; extern float DAT_803dfb94; extern float DAT_803dfb98;
extern float DAT_803dfb9c; extern float DAT_803dfba0; extern float DAT_803dfba4;
extern float DAT_803dfba8; extern float DAT_803dfbac; extern float DAT_803dfbb0;
extern float DAT_803dfbb4; extern float DAT_803dfbb8; extern float DAT_803dfbbc;

// float/double pool for this block (0x80617a38 .. 0x80617adc)
extern float  FLOAT_80617a38; extern float FLOAT_80617a3c; extern float FLOAT_80617a40;
extern float  FLOAT_80617a44; extern float FLOAT_80617a48; extern float FLOAT_80617a4c;
extern float  FLOAT_80617a50; extern float FLOAT_80617a54; extern float FLOAT_80617a58;
extern float  FLOAT_80617a5c; extern float FLOAT_80617a60; extern float FLOAT_80617a64;
extern float  FLOAT_80617a68; extern float FLOAT_80617a6c; extern float FLOAT_80617a70;
extern float  FLOAT_80617a74; extern float FLOAT_80617a78; extern float FLOAT_80617a7c;
extern float  FLOAT_80617a80; extern float FLOAT_80617a84; extern float FLOAT_80617a88;
extern float  FLOAT_80617a8c; extern float FLOAT_80617a90; extern float FLOAT_80617a94;
extern double DOUBLE_80617a98; extern double DOUBLE_80617aa0;
extern float  FLOAT_80617aa8; extern float FLOAT_80617aac; extern float FLOAT_80617ab0;
extern float  FLOAT_80617ab4; extern float FLOAT_80617ab8; extern float FLOAT_80617abc;
extern float  FLOAT_80617ac0; extern float FLOAT_80617ac4; extern float FLOAT_80617ac8;
extern float  FLOAT_80617acc; extern float FLOAT_80617ad0; extern float FLOAT_80617ad4;
extern float  FLOAT_80617ad8; extern float FLOAT_80617adc;

// sine LUT (0x800 entries) shared by the ember arc; declared in generated header
// as DAT_80583228 but referenced here as a float table.
extern float DAT_80583228;

// scene_raycast is void in generated_decls; ROM returns a hit flag.
typedef int (*RaycastFn)(int, void *, void *, int, void *, void *, int *, unsigned int, unsigned int, int *);
static const RaycastFn __we636_scene_raycast =
    (RaycastFn)&scene_raycast;

// ---------------------------------------------------------------------------
// 0x801fb808  ekun_core_fill_reset_a
// Thin wrapper: reset the reactor fill display at anchor DAT_805049b4 by calling
// ekun_door_ambient_tick with that pointer (register-passed in the ROM).
// ---------------------------------------------------------------------------
void ekun_core_fill_reset_a(void)
{
    ekun_door_ambient_tick();
}

// ---------------------------------------------------------------------------
// 0x801fb830  ekun_core_fill_reset_b
// Thin wrapper for the second reactor fill anchor DAT_805049a0.
// ---------------------------------------------------------------------------
void ekun_core_fill_reset_b(void)
{
    ekun_door_ambient_tick();
}

// ---------------------------------------------------------------------------
// 0x801fb858  ekun_terminal_fill_reset
// Force the terminal entity's fill value (+0x1cc of its render instance) to the
// "full" constant.
// ---------------------------------------------------------------------------
void ekun_terminal_fill_reset(int *self)
{
    *(float *)(*self + 0x1cc) = FLOAT_80617a38;
}

// ---------------------------------------------------------------------------
// 0x801fb868  ekun_terminal_fill_prompt
// Render/update tick for the terminal fill. If the entity is in the mid-anim
// band (0x10..0x13) hold the fill full. Otherwise, when the linked player HUD
// slot is valid and the ~20-frame throttle (DAT_80612780) has elapsed, submit
// the "use terminal" prompt string (0x1189) to that HUD slot and re-arm the
// throttle.
// ---------------------------------------------------------------------------
void ekun_terminal_fill_prompt(int *self)
{
    unsigned int *text;

    if ((self[5] < 0x14) && (0xf < self[5])) {
        *(float *)(*self + 0x1cc) = FLOAT_80617a38;
    } else if (((*(int *)(self[2] + 0x150) != 0) &&
                (-1 < *(int *)(*(int *)(self[2] + 0x150) + 4))) &&
               (DAT_806109c0 * 0x14 < DAT_80611928 - *(int *)DAT_80612780)) {
        text = (unsigned int *)get_string_by_id(0x1189);
        hud_text_submit(*(int *)(*(int *)(self[2] + 0x150) + 4), 3, text);
        *(int *)DAT_80612780 = DAT_80611928;
    }
}

// ---------------------------------------------------------------------------
// 0x801fb924  ekun_terminal_class_init
// Per-class init dispatch for the reactor terminal, keyed by +0x94:
//   0x85 fill body: material 0x25, flags 0x70, render = ekun_terminal_fill_reset.
//   0x86/0x8b flag bit 1 only.
//   0x8c interactive terminal: render = ekun_terminal_fill_prompt, render-inst
//        flag +0x30|2, entity flags 0x10|1, +0x9c|0x100000, seed fill full.
//   0x8f screen quad: set render-inst +0x30 bit2, clear bit3, then by 4CC (TRM2 =
//        0x54524d32 -> |0x10 hi-res, TRM1 -> |8) select the terminal screen tex.
// ---------------------------------------------------------------------------
void ekun_terminal_class_init(int self, int *node)
{
    float fill;
    int fourcc;

    switch (*(unsigned short *)(self + 0x94)) {
    case 0x85:
        render_instance_set_material((double)FLOAT_80617a44, *(int **)(self + 4), 0x25);
        *(unsigned int *)(self + 0x28) |= 0x70;
        entity_set_render_type(self, (int)(long long)&ekun_terminal_fill_reset);
        break;
    case 0x86:
        *(unsigned int *)(self + 0x28) |= 1;
        break;
    case 0x8b:
        *(unsigned int *)(self + 0x28) |= 1;
        break;
    case 0x8c:
        entity_set_render_type(self, (int)(long long)&ekun_terminal_fill_prompt);
        fill = FLOAT_80617a40;
        *(unsigned int *)(*(int *)(self + 4) + 0x30) |= 2;
        *(unsigned int *)(self + 0x28) |= 0x10;
        *(unsigned int *)(self + 0x28) |= 1;
        *(unsigned int *)(self + 0x9c) |= 0x100000;
        *(float *)(self + 0x1cc) = fill;
        break;
    case 0x8f:
        *(unsigned int *)(*(int *)(self + 4) + 0x30) |= 2;
        *(unsigned int *)(*(int *)(self + 4) + 0x30) &= 0xfffffffb;
        fourcc = *node;
        if (fourcc == 0x54524d32) {          // 'TRM2'
            *(unsigned int *)(*(int *)(self + 4) + 0x30) |= 0x10;
        } else if ((fourcc < 0x54524d32) && (0x54524d30 < fourcc)) {  // 'TRM1'
            *(unsigned int *)(*(int *)(self + 4) + 0x30) |= 8;
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// 0x801fba7c  ekun_terminal_steam_def
// Build the terminal steam/spark particle definition (tex 0x326) + its emitter.
// Sets the size keyframes, gravity, start/end size, end colour, per-axis velocity
// ranges and drag, then creates the emitter and its colour range.
// ---------------------------------------------------------------------------
void ekun_terminal_steam_def(void)
{
    __we3_particle_def_create((double)FLOAT_80617a44, (double)FLOAT_80617a44, &DAT_805c6110, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617a44, (double)FLOAT_80617a3c, (double)FLOAT_80617a3c,
                                    (double)FLOAT_80617a48, 0xffa00000, 0xff, 0);
    particle_def_set_gravity((double)FLOAT_80617a4c, 0x7f);
    particle_def_set_size_start_end((double)FLOAT_80617a50, (double)FLOAT_80617a54,
                                    (double)FLOAT_80617a58, (double)FLOAT_80617a5c);
    particle_def_set_color_end((double)FLOAT_80617a60, (double)FLOAT_80617a64,
                               (double)FLOAT_80617a68, (double)FLOAT_80617a3c);
    particle_def_set_velocity_ranges((double)FLOAT_80617a6c, (double)FLOAT_80617a70, (double)FLOAT_80617a74,
                                     (double)FLOAT_80617a58, (double)FLOAT_80617a44, (double)FLOAT_80617a78);
    particle_def_set_drag((double)FLOAT_80617a40);
    __we3_particle_emitter_create((double)FLOAT_80617a7c, (double)FLOAT_80617a3c, (double)FLOAT_80617a80,
                                  &DAT_805c61dc, &DAT_805c6110, 0, 1);
    particle_emitter_set_color_range((double)FLOAT_80617a84, (double)FLOAT_80617a3c,
                                     (double)FLOAT_80617a88, (double)FLOAT_80617a3c);
}

// ---------------------------------------------------------------------------
// 0x801fbb74  ekun_terminal_spark_ray
// Welding-spark ambience: once every ~20 frames (rng vs frame-scaled window),
// cast a short random ray downward from the anchor DAT_803dfb78 base + a random
// planar offset; if it hits a floor far enough from the primary player camera
// (DAT_80611d54), spawn a 0x25 spark explosion at the hit.
// ---------------------------------------------------------------------------
void ekun_terminal_spark_ray(void)
{
    unsigned int coin;
    unsigned int period;
    float dx, dy, dz;
    float org[3];   // local_2c/28/24
    float dir[3];   // local_38/34/30
    int hit;

    coin = __we629_rng_counter_tick();
    period = (unsigned int)(FLOAT_80617a68 *
             (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617a98));
    if (coin - (coin / period) * period < DAT_806108e4) {
        org[0] = DAT_803dfb78; org[1] = DAT_803dfb7c; org[2] = DAT_803dfb80;
        dir[0] = DAT_803dfb84; dir[1] = DAT_803dfb88; dir[2] = DAT_803dfb8c;
        // (planar jitter constants DAT_803dfb90..98 folded into the ray endpoints)
        float jx = FLOAT_80617a6c *
            ((float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617aa0) /
             FLOAT_80617a8c - FLOAT_80617a90);
        float jy = FLOAT_80617a6c *
            ((float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617aa0) /
             FLOAT_80617a8c - FLOAT_80617a90);
        float jz = FLOAT_80617a6c *
            ((float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617aa0) /
             FLOAT_80617a8c - FLOAT_80617a90);
        (void)jz;
        if (FLOAT_80617a3c < jy) { jy = -jy; }
        dir[0] = DAT_803dfb90 + jx; dir[1] = DAT_803dfb94 + jy; dir[2] = DAT_803dfb98;
        hit = __we636_scene_raycast(6, dir, org, 1, org, dir, (int *)0, 0x2010018, 0, (int *)0);
        if (hit != 0) {
            if (FLOAT_80617a58 < dir[1]) {
                dx = org[0] - *(float *)(DAT_80611d54 + 0x27c);
                dy = org[1] - *(float *)(DAT_80611d54 + 0x280);
                dz = org[2] - *(float *)(DAT_80611d54 + 0x284);
                if (FLOAT_80617a94 < dz * dz + dx * dx + dy * dy) {
                    org[1] = FLOAT_80617a58 * dir[1] + org[1];
                    org[0] = FLOAT_80617a58 * dir[0] + org[0];
                    org[2] = FLOAT_80617a58 * dir[2] + org[2];
                    (void)__we627_player_ai_explosion_spawn(0x25, 0, org, dir, 0, 0, 1);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801fbdd0  ekun_terminal_ember_pool
// Per-frame 2-slot ember particle pool (handles DAT_80612778[0..1]). For each
// slot: with a ~1/14-frame chance, if the slot is free, seed a new ember on a
// sine-LUT (DAT_80583228, 0x800 entries) arc with a random start pos/velocity;
// then, if live, integrate its position/velocity toward the target and re-cache.
// Wrapped in the FP spill frame (FUN_803826e4 / FUN_80382730).
// ---------------------------------------------------------------------------
void ekun_terminal_ember_pool(void)
{
    unsigned int coin;
    float step, vel;
    unsigned int idx;
    double baseArc;
    int slot;
    int posOff;   // iVar7 (0xc stride)
    int hdlOff;   // iVar6 (4 stride)
    int handle;
    float start[3];  // local_5c/58/54

    FUN_803826e4();
    slot = 0;
    posOff = 0;
    hdlOff = 0;
    do {
        coin = __we629_rng_counter_tick();
        if (coin - (coin / (DAT_806109c0 * 0xe)) * DAT_806109c0 * 0xe < DAT_806108e4) {
            handle = *(int *)(&DAT_80612778 + hdlOff);
            if (!particle_handle_is_valid(&handle)) {
                start[0] = DAT_803dfb9c; start[1] = DAT_803dfba0; start[2] = DAT_803dfba4;
                baseArc = (double)(FLOAT_80617aa8 *
                    ((float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617aa0) /
                     FLOAT_80617a8c) * FLOAT_80617aac);
                idx = (unsigned int)((double)FLOAT_80617ab8 * baseArc + (double)FLOAT_80617a90);
                *(float *)((int)&DAT_805c6260 + posOff) =
                    FLOAT_80617ab4 * (&DAT_80583228)[(idx + 0x200) * 4 & 0x1ffc >> 2] + FLOAT_80617ab0;
                *(float *)((int)&DAT_805c6264 + posOff) = FLOAT_80617abc;
                *(float *)((int)&DAT_805c6268 + posOff) =
                    FLOAT_80617ab4 * (&DAT_80583228)[idx & 0x7ff] + FLOAT_80617ac0;

                vel = (float)((double)(FLOAT_80617aa8 *
                    ((float)((double)CONCAT44(0x43300000, __we629_rng_counter_tick()) - DOUBLE_80617aa0) /
                     FLOAT_80617a8c - FLOAT_80617a90)) * (double)FLOAT_80617aac + baseArc);
                if (FLOAT_80617aa8 < vel) { vel = vel - FLOAT_80617ac4; }
                if (vel < FLOAT_80617ac8) { vel = vel + FLOAT_80617ac4; }
                idx = (unsigned int)(FLOAT_80617ab8 * vel + FLOAT_80617a90);
                *(float *)((int)&DAT_805c6278 + posOff) =
                    FLOAT_80617acc * (&DAT_80583228)[(idx + 0x200) * 4 & 0x1ffc >> 2];
                *(float *)((int)&DAT_805c627c + posOff) = FLOAT_80617a3c;
                *(float *)((int)&DAT_805c6280 + posOff) =
                    FLOAT_80617acc * (&DAT_80583228)[idx & 0x7ff];

                __we3_particle_emitter_spawn(&DAT_80612778 + hdlOff, &DAT_805c61dc, 0,
                                             (float *)((int)&DAT_805c6260 + posOff), start);
            }
        }
        handle = *(int *)(&DAT_80612778 + hdlOff);
        if (particle_handle_is_valid(&handle)) {
            step = FLOAT_80617a44 /
                (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617a98);
            vel = step * (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - DOUBLE_80617a98);
            float *pos = (float *)((int)&DAT_805c6260 + posOff);
            *(float *)((int)&DAT_805c627c + posOff) =
                -(FLOAT_80617a58 * step *
                  (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - DOUBLE_80617a98) -
                  *(float *)((int)&DAT_805c627c + posOff));
            *pos = *(float *)((int)&DAT_805c6278 + posOff) * vel + *pos;
            *(float *)((int)&DAT_805c6264 + posOff) =
                *(float *)((int)&DAT_805c627c + posOff) * vel + *(float *)((int)&DAT_805c6264 + posOff);
            *(float *)((int)&DAT_805c6268 + posOff) =
                *(float *)((int)&DAT_805c6280 + posOff) * vel + *(float *)((int)&DAT_805c6268 + posOff);
            handle = *(int *)(&DAT_80612778 + hdlOff);
            __we3_particle_cached_set_position(&handle, pos);
        }
        slot = slot + 1;
        hdlOff = hdlOff + 4;
        posOff = posOff + 0xc;
    } while (slot < 2);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x801fc0d0  ekun_wall_blast_open
// 'WALL' (0x57414c4c) breach event: spawn a 0x26 explosion (biased away from the
// primary player if close), play the stereo blast, swap the WALL render flags
// (clear bit1, set bit2), then walk the 6 linked soundpad zone pairs (tables
// DAT_80455aa0/aa4) and open any portal between them (|0x10000). Finally unblock
// scene portal 0x2a.
// ---------------------------------------------------------------------------
void ekun_wall_blast_open(void)
{
    int wall;
    int zoneA, zoneB;
    unsigned int *portal;
    int i, off;
    float pos[3];   // local_2c/28/24
    float dir[3];   // local_38/34/30

    FUN_803826e4();
    wall = FUN_8023c32c(0x57414c4c);
    pos[0] = DAT_803dfba8; pos[1] = DAT_803dfbac; pos[2] = DAT_803dfbb0;
    dir[0] = DAT_803dfbb4; dir[1] = DAT_803dfbb8; dir[2] = DAT_803dfbbc;
    if ((DAT_80611d54 != 0) && (*(float *)(DAT_80611d54 + 0x27c) < FLOAT_80617ad0)) {
        dir[0] = -DAT_803dfbb4;
        pos[0] = DAT_803dfba8 - FLOAT_80617a78;
    }
    (void)__we627_player_ai_explosion_spawn(0x26, 0, pos, dir, 0, 0, 1);
    sound_play_stereo_pair((double)FLOAT_80617ad4, (double)FLOAT_80617ad8, (double)FLOAT_80617adc);
    *(unsigned int *)(*(int *)(wall + 4) + 0x30) &= 0xfffffffd;
    *(unsigned int *)(*(int *)(wall + 4) + 0x30) |= 4;
    i = 0; off = 0;
    do {
        zoneA = soundpad_find_zone_by_id(*(int *)((int)&DAT_80455aa4 + off));
        int base = DAT_80611b8c;
        zoneB = soundpad_find_zone_by_id(*(int *)((int)&DAT_80455aa0 + off));
        portal = (unsigned int *)soundpad_find_any_portal(
            (int *)(DAT_80611b8c + zoneB * 0x20), (int *)(base + zoneA * 0x20));
        if (portal != (unsigned int *)0) {
            *portal |= 0x10000;
        }
        i = i + 1;
        off = off + 8;
    } while (i < 6);
    scene_portal_unblock(0x2a);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x801fc248  ekun_wall_blast_close
// Inverse of ekun_wall_blast_open: set the WALL render bit1, walk the same 6
// soundpad zone pairs closing any active portal (&~0x10000), then block scene
// portal 0x2a.
// ---------------------------------------------------------------------------
void ekun_wall_blast_close(void)
{
    int wall;
    int zoneA, zoneB;
    unsigned int *portal;
    int i, off;

    FUN_803826e4();
    wall = FUN_8023c32c(0x57414c4c);
    i = 0; off = 0;
    *(unsigned int *)(*(int *)(wall + 4) + 0x30) |= 2;
    do {
        zoneA = soundpad_find_zone_by_id(*(int *)((int)&DAT_80455aa4 + off));
        int base = DAT_80611b8c;
        zoneB = soundpad_find_zone_by_id(*(int *)((int)&DAT_80455aa0 + off));
        portal = (unsigned int *)soundpad_find_active_portal(
            (int *)(DAT_80611b8c + zoneB * 0x20), (int *)(base + zoneA * 0x20));
        if (portal != (unsigned int *)0) {
            *portal &= 0xfffeffff;
        }
        i = i + 1;
        off = off + 8;
    } while (i < 6);
    scene_portal_block(0x2a);
    FUN_80382730();
}

} // extern "C"  (batch_636)

// ===========================================================================
// batch_637  0x801fc2fc - 0x801fcadc
// EKUN 'WALL' breach controller: the anim-event dispatcher + level init/alloc/
// reset, the WALL door-ambient blend, the soundpad "passable zone" predicate,
// and the master per-frame tick that also drives the terminal ember/spark FX.
//
//   ekun_terminal_steam_init   (0x801fc2fc) seed the HUD throttle to -1000 and
//                              build the steam particle def (ekun_terminal_steam_def).
//   ekun_wall_event_dispatch   (0x801fc328) the WALL/DOR1/DOR6/PRWL anim-event
//                              dispatcher, keyed by the FP-spill {mode,tag} pair:
//                              mode 0 tag 0xf = WALL breach (kill particles, 0x1c
//                              explosion, blast sound 0x653, ekun_wall_blast_open);
//                              mode 0 tag 0 = destroy the passed door leaf with a
//                              type-1 explosion; mode 1 tag 0xf = arm DOR1 trigger;
//                              mode 1 tag 0/1 = broadcast HUD string 0x118c to all
//                              players; mode 4 = swap the fill-body material; mode
//                              <4 tag 0xf = the big DOR6 door-seat placement that
//                              copies a resolved soundpad zone transform onto the
//                              0x85 fill body + the 'PRWL' panel and eases the panel
//                              rotation via FUN_80390324.
//   ekun_terminal_subsystem_reset (0x801fc6dc) zero the 0x10 block, invalidate the
//                              two door handles, and (mode 3) re-init steam + clear
//                              the 2 ember handles.
//   ekun_terminal_block_alloc  (0x801fc768) allocate the 0x10 terminal block and
//                              clear sound bank ref 0xc73.
//   ekun_wall_mode2_gate       (0x801fc79c) mode-2: open the WALL portals, else
//                              close them.
//   ekun_wall_zone_passable    (0x801fc7d8) classify a soundpad zone: return 1 for
//                              the always-passable zone types, else test the zone's
//                              entity id against a per-type ceiling (via clz) — the
//                              gate predicate consumed by ekun_wall_ambient_tick.
//   ekun_wall_ambient_tick     (0x801fc898) ramp the WALL "open factor" (+4 of the
//                              block) toward 1 when every live actor is in a passable
//                              zone, toward 0 otherwise, clamp [0,1] and drive the
//                              two 3D door sounds (0x17 / 0x16).
//   ekun_wall_master_tick      (0x801fca2c) per-frame: run the ambient tick, and in
//                              mode 3 also run the terminal ember pool + spark ray.
//   ekun_wall_damage_filter    (0x801fca6c) damage-filter callback: in the gated
//                              game mode, force the damage ctx +0x20 to a fixed
//                              float when the attacker has no owner, then chain to
//                              damage_filter_vip_match_end.
//   ekun_wall_state_snapshot   (0x801fcadc) save/restore callback (mode 1 = snapshot):
//                              set the entity render state word +0x94=8, clear +0x54c,
//                              accumulate the sound slot.
//
// Non-matching: 4CC ids (WALL 0x57414c4c, PRWL 0x5052574c, DOR1 0x444f5231,
// DOR6 0x444f5236); FP spill frames (FUN_803826f0 open returns the {mode,tag}
// pair / FUN_8038273c close) opaque; int<->double 0x43300000 bias plain math;
// sound_update_3d_source (void in decls, all-int trailing args) reuses the
// __we3b alias; player_ai_explosion_spawn reuses __we627; mem_alloc reduced to
// the 2-arg form (spill args dropped). FUN_802216f0 / FUN_80220f70 get file-local
// externs.
// ===========================================================================
extern "C" {

// --- raw data symbols (file-local, batch_637) ---
extern float DAT_803dfbc0; extern float DAT_803dfbc4; extern float DAT_803dfbc8;   // WALL explosion dir
extern float DAT_803dfbcc; extern float DAT_803dfbd0; extern float DAT_803dfbd4;   // door-leaf explosion dir
extern int   DAT_80455ad0;      // DOR6 soundpad zone-id table (5 entries)
extern float FLOAT_80617ae0;    // WALL ambient pitch
extern float FLOAT_80617ae8;    // damage-filter forced value

// FUN_802216f0: resolve entity by (kind, class, flag) -> entity ptr.
extern int  FUN_802216f0(int kind, int cls, int flag);
// FUN_80220f70: game-mode/net gate query (mirror of FUN_80220f30); nonzero => active.
extern int  FUN_80220f70(int which);

// ---------------------------------------------------------------------------
// 0x801fc2fc  ekun_terminal_steam_init
// Seed the HUD prompt throttle (*DAT_80612780) to -1000 so the prompt is ready
// immediately, then build the terminal steam particle def.
// ---------------------------------------------------------------------------
void ekun_terminal_steam_init(void)
{
    *(int *)DAT_80612780 = -1000;   // 0xfffffc18
    ekun_terminal_steam_def();
}

// ---------------------------------------------------------------------------
// 0x801fc328  ekun_wall_event_dispatch
// The WALL breach anim-event dispatcher. The FP-spill open frame returns a packed
// {mode,tag} pair (mode in the resolved event record *piVar6, tag in the low word);
// param_3 is the event's associated entity (a door leaf for the mode-0 destroy).
// ---------------------------------------------------------------------------
void ekun_wall_event_dispatch(int param_1, int param_2, int *param_3)
{
    int *rec;
    int mode, tag;
    int wall, prwl, fill, door;
    int *zone;
    unsigned int *text;
    int sample;
    unsigned long long pair;
    float dirW[3];   // local_5c/58/54
    float dirD[3];   // local_68/64/60
    int   playParams[8];  // local_50 sound-request scratch

    pair = (unsigned long long)FUN_803826f0();
    rec  = (int *)(unsigned int)(pair >> 0x20);
    tag  = (int)pair;
    mode = *rec;

    if (mode == 2) { FUN_8038273c(); return; }

    if (mode < 2) {
        if (mode == 0) {
            if (tag == 0xf) {
                dirW[0] = DAT_803dfbc0; dirW[1] = DAT_803dfbc4; dirW[2] = DAT_803dfbc8;
                wall = FUN_8023c32c(0x57414c4c);          // 'WALL'
                if (wall != 0) {
                    particle_kill_by_entity();
                    (void)__we627_player_ai_explosion_spawn(0x1c, 0, (float *)(wall + 100), dirW, 0, 0, 1);
                    sound_init_play_params((int)playParams);
                    *(unsigned char *)((int)playParams + 0x28) = 1;   // local_28
                    *(unsigned int *)((int)playParams + 0x18) |= 0xa0; // local_40
                    playParams[0] = 0x653;
                    *(int *)((int)playParams + 0x0c) = *(int *)(wall + 100);   // local_34
                    *(int *)((int)playParams + 0x10) = *(int *)(wall + 0x68);  // local_30
                    *(int *)((int)playParams + 0x14) = *(int *)(wall + 0x6c);  // local_2c
                    sound_play_request(playParams);
                    ekun_wall_blast_open();
                }
            } else if (tag == 0) {
                dirD[0] = DAT_803dfbcc; dirD[1] = DAT_803dfbd0; dirD[2] = DAT_803dfbd4;
                if (param_3 != (int *)0) {
                    (void)__we627_player_ai_explosion_spawn(1, 0, (float *)(param_3 + 0x19), dirD, 0, 0, 1);
                    entity_destroy_with_event(param_3);
                }
            }
        } else if (-1 < mode) {   // mode == 1
            if (tag == 0xf) {
                door = trigger_slot_resolve_entity(0x444f5231);   // 'DOR1'
                trigger_clear_flags(door, 2);
                door = trigger_slot_resolve_entity(0x444f5231);
                trigger_set_flags(door, 0x400);
            } else if (((tag == 0) || (tag == 1)) && ((*(unsigned short *)(rec + 2) & 0x8000) == 0)) {
                int off = 0;
                for (int i = 0; i < DAT_80612610; i = i + 1) {
                    if (-1 < *(int *)(DAT_80611d78 + off + 4)) {
                        sample = sound_resolve_sample_id(0x118c);
                        text = (unsigned int *)get_string_by_id(0x118c);
                        hud_create_text_colored(*(int *)(DAT_80611d78 + off + 4), 3, text, sample);
                    }
                    off = off + 0x141c;
                }
            }
        }
    } else if (mode == 4) {
        if (tag == 0xf) {
            fill = FUN_802216f0(4, 0x85, 0);
            if (fill != 0) {
                render_instance_set_material((double)FLOAT_80617a44, *(int **)(fill + 4), 0x5b);
            }
        }
    } else if ((mode < 4) && (tag == 0xf)) {   // mode 3: DOR6 door-seat placement
        fill = FUN_802216f0(4, 0x85, 0);
        prwl = FUN_8023c32c(0x5052574c);       // 'PRWL'
        door = trigger_slot_resolve_entity(0x444f5236);   // 'DOR6'
        trigger_clear_flags(door, 2);
        door = trigger_slot_resolve_entity(0x444f5236);
        trigger_set_flags(door, 0x400);
        zone = (int *)soundpad_resolve_entity_to_zone(
            *(int *)(&DAT_80455ad0 + ((*(unsigned short *)(rec + 2) >> 10 & 0x1f) % 5) * 4));
        if ((zone != (int *)0) && (fill != 0) && (prwl != 0)) {
            *(int *)(fill + 100)   = zone[3];
            *(int *)(fill + 0x68)  = zone[4];
            *(int *)(fill + 0x6c)  = zone[5];
            int seat = zone[6];
            *(int *)(fill + 0x70)  = seat;
            *(int *)(fill + 0xc4)  = seat;
            *(int *)(fill + 0x34)  = (int)*(short *)((int)zone + 10);
            if ((zone[1] & 0x800U) == 0) {
                *(int *)(fill + 0x110) = soundpad_zone_node_to_index(zone);
            } else {
                *(int *)(fill + 0x110) = soundpad_find_zone_by_id((int)*(short *)(zone + 2));
            }
            *(int *)(prwl + 100)  = zone[3];
            *(int *)(prwl + 0x68) = zone[4];
            *(int *)(prwl + 0x6c) = zone[5];
            double target = (double)(FLOAT_80617a50 + (float)zone[6]);
            double eased  = FUN_80390324((double)((float)(target - (double)FLOAT_80617a3c) / FLOAT_80617a54));
            *(float *)(prwl + 0x70) = -(float)((double)FLOAT_80617a54 * (double)(float)eased - target);
            *(int *)(prwl + 0x34) = (int)*(short *)((int)zone + 10);
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801fc6dc  ekun_terminal_subsystem_reset
// Zero the 0x10 terminal block, invalidate the two door sound handles (+8/+0xc),
// and when the level mode is 3 re-init the steam def + clear the 2 ember handles.
// ---------------------------------------------------------------------------
void ekun_terminal_subsystem_reset(void)
{
    memset_wrapper((int)DAT_80612780, 0, 0x10);
    *(int *)(DAT_80612780 + 8) = -1;
    *(int *)(DAT_80612780 + 0xc) = -1;
    if (DAT_805015bd == '\x03') {
        ekun_terminal_steam_init();
        int off = 0;
        for (int i = 0; i < 2; i = i + 1) {
            particle_handle_clear((unsigned int *)(&DAT_80612778 + off));
            off = off + 4;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801fc768  ekun_terminal_block_alloc
// Allocate the 0x10 terminal block and clear sound bank ref 0xc73. (The ROM's
// wide FP-spill mem_alloc argument frame is register noise; reduced to the 2-arg
// form.)
// ---------------------------------------------------------------------------
void ekun_terminal_block_alloc(void)
{
    DAT_80612780 = mem_alloc(0x10, 1);
    sound_clear_bank_ref(0xc73);
}

// ---------------------------------------------------------------------------
// 0x801fc79c  ekun_wall_mode2_gate
// Level mode 2: open the WALL portals; any other mode: close them.
// ---------------------------------------------------------------------------
void ekun_wall_mode2_gate(void)
{
    if (DAT_805015bd == '\x02') {
        ekun_wall_blast_open();
    } else {
        ekun_wall_blast_close();
    }
}

// ---------------------------------------------------------------------------
// 0x801fc7d8  ekun_wall_zone_passable
// Given a soundpad zone id, return 1 for the always-passable zone types; for the
// door zone types (0xf/0x11/0x21/0x25) test the zone's entity id against a per-type
// ceiling via countLeadingZeros (>>5 gives 1 when id <= ceiling); else 0.
// ---------------------------------------------------------------------------
unsigned int ekun_wall_zone_passable(int zoneParam)
{
    int *padEntry;
    int zoneType;
    unsigned int result;

    padEntry = soundpad_resolve_zone_node(zoneParam);
    zoneType = soundpad_get_entity_zone_id((int)padEntry);
    switch (zoneType) {
    case 8: case 10: case 0x14: case 0x18: case 0x19:
    case 0x1a: case 0x26: case 0x27: case 0x2e:
        result = 1;
        break;
    default:
        result = 0;
        break;
    case 0xf:
        result = countLeadingZeros(0x567 - soundpad_get_entity_id_from_zone(zoneParam)) >> 5;
        break;
    case 0x11:
        result = countLeadingZeros(0x549 - soundpad_get_entity_id_from_zone(zoneParam)) >> 5;
        break;
    case 0x21:
        result = countLeadingZeros(0x4ad - soundpad_get_entity_id_from_zone(zoneParam)) >> 5;
        break;
    case 0x25:
        result = countLeadingZeros(0x4eb - soundpad_get_entity_id_from_zone(zoneParam)) >> 5;
        break;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x801fc898  ekun_wall_ambient_tick
// Ramp the WALL open factor (+4 of DAT_80612780). "all" starts 1 and is ANDed with
// ekun_wall_zone_passable for each live actor's current zone; when all actors are
// in passable zones the factor ramps up (+dt/period), else down; clamp [0,1] then
// drive the two 3D door sounds (0x17 opening / 0x16 closing). FP-spill wrapped.
// ---------------------------------------------------------------------------
void ekun_wall_ambient_tick(void)
{
    unsigned int all;
    float delta, f, clamped, out;
    int i, off;

    FUN_803826f0();
    all = 1;
    i = 0; off = 0;
    while ((i < DAT_80612610) && (all != 0)) {
        if (-1 < *(int *)(DAT_80611d78 + off + 4)) {
            unsigned int p = ekun_wall_zone_passable(
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c) + 0x678));
            all = all & p;
        }
        i = i + 1;
        off = off + 0x141c;
    }
    float step = (FLOAT_80617a44 /
        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617a98)) / FLOAT_80617a64;
    delta = (all == 0) ? -step : step;
    *(float *)(DAT_80612780 + 4) = *(float *)(DAT_80612780 + 4) + delta;
    f = *(float *)(DAT_80612780 + 4);
    clamped = f;
    if (FLOAT_80617a44 < f) clamped = FLOAT_80617a44;
    out = FLOAT_80617a3c;
    if ((FLOAT_80617a3c <= clamped) && (out = f, FLOAT_80617a44 < f)) out = FLOAT_80617a44;
    *(float *)(DAT_80612780 + 4) = out;
    __we3b_sound_update_3d_source((double)*(float *)(DAT_80612780 + 4), (double)FLOAT_80617ae0, (double)FLOAT_80617a44,
                                  DAT_80612780 + 8, 0, 0x17, 0);
    __we3b_sound_update_3d_source((double)(float)((double)FLOAT_80617a44 - (double)*(float *)(DAT_80612780 + 4)),
                                  (double)FLOAT_80617ae0, (double)FLOAT_80617a44,
                                  DAT_80612780 + 0xc, 0, 0x16, 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801fca2c  ekun_wall_master_tick
// Per-frame master tick: run the WALL ambient blend; in level mode 3 also run the
// terminal ember particle pool + the welding-spark ray.
// ---------------------------------------------------------------------------
void ekun_wall_master_tick(void)
{
    ekun_wall_ambient_tick();
    if (DAT_805015bd == '\x03') {
        ekun_terminal_ember_pool();
        ekun_terminal_spark_ray();
    }
}

// ---------------------------------------------------------------------------
// 0x801fca6c  ekun_wall_damage_filter
// Damage-filter callback. When FUN_80220f70(2) is set and FUN_80220f30(2) is clear
// (the gated game mode) and the attacker (damageCtx[2]) has no owner, force the
// damage ctx +0x20 to a fixed float. Then chain to damage_filter_vip_match_end.
// (The FP-spill p1..p8 frame is register noise; forwarded unchanged.)
// ---------------------------------------------------------------------------
void ekun_wall_damage_filter(void *p1, void *p2, void *p3, void *p4, void *p5, void *p6,
                             void *p7, void *p8, int *damageCtx)
{
    if (((FUN_80220f70(2) != 0) && (FUN_80220f30(2) == 0)) &&
        ((damageCtx[2] == 0) || (*(int *)(damageCtx[2] + 0x150) == 0))) {
        damageCtx[8] = (int)FLOAT_80617ae8;
    }
    damage_filter_vip_match_end(p1, p2, p3, p4, p5, p6, p7, p8, damageCtx);
}

// ---------------------------------------------------------------------------
// 0x801fcadc  ekun_wall_state_snapshot
// Save/restore callback (mode 1 = snapshot): set the entity's render state word
// (+0x94 of its render inst) to 8, clear the +0x54c byte, and accumulate the
// sound slot (channel 3, +1000).
// ---------------------------------------------------------------------------
int ekun_wall_state_snapshot(int self, int mode)
{
    int inst;
    if (mode == 1) {
        inst = *(int *)(self + 0x7c);
        *(int *)(inst + 0x94) = 8;
        *(unsigned char *)(inst + 0x54c) = 0;
        object_sound_slot_accumulate(inst, 3, 1000);
    }
    return 0;
}

} // extern "C"  (batch_637)
