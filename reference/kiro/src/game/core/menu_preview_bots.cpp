// menu_preview_bots.cpp
// Character / loadout preview-bot subsystem, recovered in batch_811. The
// front-end weapon/character screens spawn up to 4 live "preview" bots (one per
// display slot) so the player can see the selected character and weapon. Each
// slot owns a bot actor, an optional attached prop, a persistent sound, and a
// streamed model/texture set that is loaded on demand.
//
// Per-slot storage (slot index s = 0..3):
//   DAT_805cca74 + s*0x5b*4  block of slot pointers/state (bot, prop, sound id,
//                            asset cursor, load state, timers)
//   DAT_805cc920 + s*0x16c   weapon-selection record (type, subtype, variant)
//   DAT_805328e0 + s*0x54    render-parameter table for the slot's display xform
//
// NON-MATCHING decompilation.

#include "common.h"
#include "menu.h"     // pulls generated_decls.h (named cross-module helpers)
#include "hud.h"
#include "sound.h"
#include "entity.h"
#include "weapon.h"
#include "resource.h"
#include "render.h"
#include "character.h"

// ---------------------------------------------------------------------------
// Per-slot data blocks and shared globals (raw ROM symbols).
// ---------------------------------------------------------------------------
extern int   DAT_805cca74;   // slot block base: bot actor ptr [s*0x5b]
extern int   DAT_805cca78;   // slot: current weapon subtype [s*0x5b]
extern int   DAT_805cca7c;   // slot: asset load cursor [s*0x5b]
extern int   DAT_805cca80;   // slot: asset range start [s*0x5b]
extern int   DAT_805cca84;   // slot: asset range count [s*0x5b]
extern int   DAT_805cca88;   // slot: persistent sound id [s*0x5b]
extern int   DAT_805cca5c;   // slot: active flag [s*0x5b]
extern int   DAT_805cca60;   // slot: attached prop entity [s*0x5b]
extern int   DAT_805cca70;   // slot: display column arg [s*0x16c]
extern float DAT_805cca44;   // slot: load timer [s*0x5b as float]
extern float DAT_805cca4c;   // slot: fade timer [s*0x5b]
extern int   DAT_805cca54;   // slot: load state [s*0x16c]
extern int   DAT_805cca58;   // slot: load substate [s*0x16c]
extern int   DAT_805cca28;   // slot: variant seed [s*0x16c]
extern int   DAT_805cc920;   // slot: weapon type [s*0x16c]
extern int   DAT_805cc924;   // slot: weapon subtype [s*0x16c]
extern int   DAT_805cc710;   // slot: display scale [s*4]
extern int   DAT_805328e0;   // slot render-param table base [s*0x54]
extern int   DAT_80532910;   // render-param fields (written by slot_init_render)
extern int   DAT_80532914;
extern int   DAT_80532918;
extern int   DAT_8053291c;
extern int   DAT_80532920;
extern int   DAT_80532924;
extern int   DAT_80532928;
extern int   DAT_8053292c;
extern int   DAT_80532930;
extern int   DAT_805328e4;
extern int   DAT_805328e8;
extern int   DAT_805328ec;
extern int   DAT_805328f0;
extern int   DAT_805328f4;
extern int   DAT_805328f8;
extern int   DAT_805328fc;
extern int   DAT_80532900;

extern int   DAT_80612aa8;   // preview system active flag
extern int   DAT_80611014;   // active preview slot count
extern int   DAT_80612b24;   // round-robin stream cursor
extern int   DAT_80612b28;   // slot currently owning the loader (-1 none)
extern int  *DAT_80612b38;   // mode-7 special preview entity
extern char  DAT_80612b18;   // per-slot display mode byte [s]
extern int   DAT_805cc614;   // active game mode
extern int   DAT_805cc628;   // worm-mode sub-id
extern int   DAT_805cca80;   // (declared above)
extern int   DAT_8060fa8c;   // weapon runtime table base (stride 0x14)
extern int   DAT_8048ca80;   // weapon -> asset id table (stride 0x30)
extern int   DAT_80473d58;   // weapon prop material table (stride 0x14)
extern int   DAT_80473d64;   // weapon prop type table (stride 0x14)
extern int   DAT_804744f8;   // prop entity-type table
extern int   DAT_804744ec;   // prop material table
extern short DAT_805c9858;   // asset -> context slot table (stride 2)
extern int   DAT_806125e8;   // context record base (stride 0x30)
extern int   DAT_805cbf40;   // shuffled asset index table
extern char  DAT_805cc4bc;   // asset byte table
extern char  DAT_805cc4fc;   // asset byte table (alt)
extern int   DAT_805cc4b8;   // asset table modulus
extern int   DAT_805cca74;   // (declared above)
extern int   DAT_805cca88;   // (declared above)

extern int   DAT_803e08a4;   // mode-7 preview spawn params (position/orient block)
extern int   DAT_803e08a8;
extern int   DAT_803e08ac;
extern int   DAT_803e08b0;
extern int   DAT_803e08b4;
extern int   DAT_803e08b8;

extern float FLOAT_80619f08; // 0.0f
extern float FLOAT_80619f38; // 1.0f-ish layout const
extern float FLOAT_80619f4c;
extern float FLOAT_80619ee8; // base scale const
extern float FLOAT_80619ff8;
extern float FLOAT_80619ff4;
extern float FLOAT_80619fa8; // fade step
extern float FLOAT_806108ec; // frame-time scale
extern float FLOAT_8061a050;
extern float FLOAT_8061a054;
extern float FLOAT_8061a058;
extern float FLOAT_8061a05c;
extern float FLOAT_8061a060;
extern float FLOAT_8061a064;
extern double DOUBLE_80619f00; // int->double bias
extern unsigned int DAT_806109c0; // frame delta (raw)

// ---- helpers not in generated headers (per-file typed externs) ----
// wait_resource_by_id, resource_get_asset_id, resource_get_context,
// texture_slot_unload, wait_resources_loaded resolve via generated_externs.h
// (declared int (...)); do not redeclare here.
extern unsigned int FUN_80364098(void);   // "hide-weapon-in-mode" query

// frame-actor accessor pair (return the active preview slot index)
extern int  FUN_803826d8(void);
extern int  FUN_803826e4(void);
extern void FUN_80382724(void);
extern void FUN_80382730(void);
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);

// ---- ABI-bound helpers (generated decls have wrong arity/return; bind by addr) ----
typedef int (*rmat_fp)(double, int *, int);
static const rmat_fp __mp_set_material = (rmat_fp)0x80061da0;   // render_instance_set_material (returns entity)
typedef int (*spawnact_fp)(int, double, double, int, int, int, int, int, int, int, void *, void *, int, int);
static const spawnact_fp __mp_spawn_activation = (spawnact_fp)0x80072c38; // entity_spawn_with_activation
typedef int (*spawntype_fp)(int, double, double, double, int, int, int, int, int, void *, void *, int, int);
static const spawntype_fp __mp_spawn_type = (spawntype_fp)0x80072d70;     // entity_spawn_by_type
typedef int (*spawnbot_fp)(double, double, double, double, int, int, int, int, int, int, int, int, int, int);
static const spawnbot_fp __mp_spawn_bot = (spawnbot_fp)0x800acdac;        // player_ai_spawn_bot

// Intra-file callees are forward-declared via generated_decls.h (pulled by the
// shim includes), so no local forward declarations are needed here.

// ---------------------------------------------------------------------------
// 0x802cf21c  menu_preview_slot_init_render
// Zero the slot's 0x54-byte render-param block and seed it with the default
// display transform constants (position/scale/orientation for the preview
// camera).
// ---------------------------------------------------------------------------
void menu_preview_slot_init_render(int slot)
{
    int off = slot * 0x54;
    memset_wrapper((int)(&DAT_805328e0 + off), 0, 0x54);

    *(float *)(&DAT_80532910 + off) = FLOAT_8061a050;
    *(float *)(&DAT_80532914 + off) = FLOAT_8061a054;
    *(float *)(&DAT_80532918 + off) = FLOAT_80619f08;
    *(float *)(&DAT_8053291c + off) = FLOAT_8061a058;
    *(float *)(&DAT_80532920 + off) = FLOAT_8061a05c;
    *(float *)(&DAT_80532924 + off) = FLOAT_80619f08;
    *(float *)(&DAT_80532928 + off) = FLOAT_80619f38;
    *(float *)(&DAT_8053292c + off) = FLOAT_80619f38;
    *(float *)(&DAT_80532930 + off) = FLOAT_80619f38;
    *(float *)(&DAT_805328e0 + off) = FLOAT_80619f4c;
    *(float *)(&DAT_805328e4 + off) = FLOAT_80619ff8;
    *(float *)(&DAT_805328e8 + off) = FLOAT_80619ff4;
    *(float *)(&DAT_805328ec + off) = FLOAT_80619ee8;
    *(float *)(&DAT_805328f0 + off) = FLOAT_80619ee8;
    *(float *)(&DAT_805328f4 + off) = FLOAT_8061a060;
    *(float *)(&DAT_805328f8 + off) = FLOAT_80619f08;
    *(float *)(&DAT_805328fc + off) = FLOAT_8061a064;
    *(float *)(&DAT_80532900 + off) = FLOAT_80619ee8;
}

// ---------------------------------------------------------------------------
// 0x802cf2d8  menu_preview_system_init
// Initialise the preview subsystem: tear down any existing bots, reset the four
// slot render blocks, mark the mode-state active, push the preview HUD screen,
// and lay out the per-slot column assignments based on the active slot count
// (DAT_80611014). Special-cases the mode-7 worm sub-modes 6/7/8 by spawning a
// backdrop entity (type 0x385) at a fixed transform.
// ---------------------------------------------------------------------------
void menu_preview_system_init(unsigned long long p1, double p2, double p3, int p4,
                              int p5, int p6, int p7, int p8)
{
    menu_preview_all_destroy();
    DAT_80612aa8 = 1;
    for (int i = 0; i < 4; i++) {
        menu_preview_slot_init_render(i);
    }
    mode_fill_state_word(1);
    player_ai_on_death_stub();

    if ((DAT_805cc614 == 7) &&
        ((DAT_805cc628 == 6) || (DAT_805cc628 == 7) || (DAT_805cc628 == 8))) {
        int orient[3];
        int pos[3];
        pos[0] = DAT_803e08a4;
        pos[1] = DAT_803e08a8;
        pos[2] = DAT_803e08ac;
        orient[0] = DAT_803e08b0;
        orient[1] = DAT_803e08b4;
        orient[2] = DAT_803e08b8;
        if (DAT_80612b38 != (int *)0) {
            entity_destroy_with_event(DAT_80612b38);
        }
        DAT_80612b38 = (int *)__mp_spawn_activation(0, p2, p3, p4, p5, p6, p7, p8,
                                                  0x10000000, 0x385, orient, pos, 0, 0);
        *(unsigned int *)(DAT_80612b38[0x1f] + 0x24) |= 2;
    }

    hud_screen_push();

    if (DAT_80611014 == 3) {
        int col = 0, off = 0;
        for (int n = 3; n != 0; n--) {
            *(int *)((int)&DAT_805cca80 + off) = col;
            *(int *)((int)&DAT_805cca84 + off) = 1;
            *(int *)((int)&DAT_805cca7c + off) = *(int *)((int)&DAT_805cca80 + off);
            *(int *)((int)&DAT_805cca74 + off) = 0;
            *(int *)((int)&DAT_805cca78 + off) = 0xfffffffe;
            *(int *)((int)&DAT_805cca88 + off) = 0xffffffff;
            off += 0x16c;
            col++;
        }
    } else if (0 < DAT_80611014) {
        int stride = 4 / DAT_80611014;
        int col = 0, off = 0;
        for (int n = DAT_80611014; n != 0; n--) {
            int start = stride * col;
            col++;
            *(int *)((int)&DAT_805cca80 + off) = start;
            *(int *)((int)&DAT_805cca84 + off) = stride;
            *(int *)((int)&DAT_805cca7c + off) = *(int *)((int)&DAT_805cca80 + off);
            *(int *)((int)&DAT_805cca74 + off) = 0;
            *(int *)((int)&DAT_805cca78 + off) = 0xfffffffe;
            *(int *)((int)&DAT_805cca88 + off) = 0xffffffff;
            off += 0x16c;
        }
    }
    DAT_80612b28 = -1;
    DAT_80612b24 = 0;
}

// ---------------------------------------------------------------------------
// 0x802cf4b0  menu_preview_bot_spawn
// Spawn the preview bot for the active slot: resolve its weapon def record,
// apply the loaded resource context, spawn the bot actor, attach its persistent
// weapon sound, set the display material, and optionally spawn an attached prop
// entity. The slot index comes from the frame-actor accessor.
// ---------------------------------------------------------------------------
void menu_preview_bot_spawn(void)
{
    int slot = FUN_803826e4();
    int roff = slot * 0x16c;
    int *pSub = (int *)(&DAT_805cc924 + roff);
    int wpnType = *(int *)(&DAT_805cc920 + roff);

    unsigned char *def = weapon_def_find_record(wpnType, *pSub);
    menu_preview_apply_resource_ctx(slot, *(int *)(&DAT_8048ca80 + wpnType * 0x30));

    double z = (double)FLOAT_80619f08;
    int bot = __mp_spawn_bot(z, z, z, z, wpnType, wpnType, slot, -1, 1, *pSub, 0, 0, 0, 0);
    (&DAT_805cca74)[slot * 0x5b] = bot;
    (&DAT_805cca78)[slot * 0x5b] = *pSub;

    int soundId = *(int *)(def + 0x28);
    if ((FUN_80364098() != 0) &&
        ((*(unsigned int *)(DAT_8060fa8c + soundId * 0x14 + 8) & 0x4000) == 0)) {
        soundId = -1;
    }
    int *pSound = &DAT_805cca88 + slot * 0x5b;
    if (soundId != *pSound) {
        sound_stop_persistent_by_id(*pSound, 0);
        *pSound = soundId;
        sound_play_persistent((unsigned int)*pSound, 0, 1);
    }

    int botActor = (&DAT_805cca74)[slot * 0x5b];
    (&DAT_805cca5c)[slot * 0x5b] = 1;
    int inst = __mp_set_material((double)FLOAT_80619ee8, *(int **)(botActor + 4),
                               *(int *)(&DAT_80473d58 + *(int *)(def + 0x2c) * 0x14));

    int propType = *(int *)(&DAT_80473d64 + *(int *)(def + 0x2c) * 0x14);
    if (propType != -1) {
        int pos[3];
        pos[0] = (int)FLOAT_80619f08;
        pos[1] = (int)FLOAT_80619f08;
        pos[2] = (int)FLOAT_80619f08;
        int prop = __mp_spawn_type(inst, (double)FLOAT_80619f08, (double)FLOAT_80619f08,
                                 (double)FLOAT_80619f08, 0, 0, 0, 0,
                                 (&DAT_804744f8)[propType * 4], pos, pos, 0, 0);
        (&DAT_805cca60)[slot * 0x5b] = prop;
        *(int *)((&DAT_805cca60)[slot * 0x5b] + 0x254) = (int)(&DAT_805328e0 + slot * 0x54);
        *(unsigned int *)((&DAT_805cca60)[slot * 0x5b] + 0x28) |= 0x40000000;
        __mp_set_material((double)FLOAT_80619ee8, *(int **)((&DAT_805cca60)[slot * 0x5b] + 4),
                        *(int *)(&DAT_804744ec + propType * 0x10));
    }

    aiactor_visible_list_add(botActor);
    *(int *)(botActor + 0x254) = (int)(&DAT_805328e0 + slot * 0x54);
    *(unsigned int *)(botActor + 0x28) |= 0x40000000;

    if (((&DAT_80612b18)[slot] == '\x01') || ((&DAT_80612b18)[slot] == '\x02')) {
        *(float *)(&DAT_805cc710 + slot * 4) = FLOAT_80619f08;
    } else {
        *(float *)(&DAT_805cc710 + slot * 4) = FLOAT_80619ee8;
    }

    int bot2 = (&DAT_805cca74)[slot * 0x5b];
    if ((bot2 != 0) && (*(short *)(*(int *)(bot2 + 0x7c) + 0x48) == 0xa0)) {
        character_damage_spawn_effects();
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x802cf744  menu_preview_slot_destroy
// Tear down the active slot's bot (and its attached prop): trigger the death
// effect for special class 0xa0, remove the bot from the visible list, destroy
// the linked prop (+0xbc4) and the bot, then destroy any display prop
// (DAT_805cca60). Slot index from the gpr-save accessor.
// ---------------------------------------------------------------------------
void menu_preview_slot_destroy(void)
{
    int slot = __save_gpr_28();
    int *bot = (int *)(&DAT_805cca74)[slot * 0x5b];
    if (bot != (int *)0) {
        int actor = bot[0x1f];
        if ((bot[8] == 8) && ((bot[10] & 0x80000000) == 0) && (actor != -1) && (actor != 0)) {
            int *linked = *(int **)(actor + 0xbc4);
            if (*(short *)(actor + 0x48) == 0xa0) {
                character_death_effect_trigger((int *)(actor + 0xc68));
            }
            aiactor_visible_list_remove((int)bot);
            if (linked != (int *)0) {
                entity_destroy_with_event(linked);
                *(int *)(bot[0x1f] + 0xbc4) = 0;
            }
            entity_destroy_with_event(bot);
            (&DAT_805cca74)[slot * 0x5b] = 0;
        }
    }
    (&DAT_805cca78)[slot * 0x5b] = 0xfffffffe;
    if ((int *)(&DAT_805cca60)[slot * 0x5b] != (int *)0) {
        entity_destroy_with_event((int *)(&DAT_805cca60)[slot * 0x5b]);
        (&DAT_805cca60)[slot * 0x5b] = 0;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x802cf834  menu_preview_all_destroy
// Deactivate the subsystem, stop all persistent sounds, and destroy all four
// preview slots.
// ---------------------------------------------------------------------------
void menu_preview_all_destroy(void)
{
    DAT_80612aa8 = 0;
    sound_stop_all_persistent();
    for (int i = 0; i < 4; i++) {
        menu_preview_slot_destroy();
    }
}

// ---------------------------------------------------------------------------
// 0x802cf87c  menu_preview_slot_set_weapon
// Set slot's weapon selection (type/subtype/column arg). If it changed and the
// slot currently owns the loader, release the loader latch. Resolves the weapon
// variant index for the record.
// ---------------------------------------------------------------------------
void menu_preview_slot_set_weapon(int slot, int wpnType, int wpnSubtype, int columnArg)
{
    int owner = DAT_80612b28;
    int roff = slot * 0x16c;
    int *pType = (int *)(&DAT_805cc920 + roff);
    if ((wpnType != *pType) || (wpnSubtype != *(int *)(&DAT_805cc924 + roff))) {
        *pType = wpnType;
        *(int *)(&DAT_805cc924 + roff) = wpnSubtype;
        *(int *)(&DAT_805cca70 + roff) = columnArg;
        if (owner == slot) {
            DAT_80612b28 = -1;
        }
    }
    weapon_resolve_variant_index(pType);
}

// ---------------------------------------------------------------------------
// 0x802cf8f4  menu_preview_slot_matches_selection
// Returns 1 if the slot's spawned bot already matches its current weapon
// selection (character asset id and subtype), 0 otherwise.
// ---------------------------------------------------------------------------
int menu_preview_slot_matches_selection(int slot)
{
    if ((&DAT_805cca74)[slot * 0x5b] == 0) {
        return 0;
    }
    if ((unsigned int)*(int *)(&DAT_8048ca80 + *(int *)(&DAT_805cc920 + slot * 0x16c) * 0x30) !=
        (unsigned int)*(unsigned short *)((&DAT_805cca74)[slot * 0x5b] + 0x94)) {
        return 0;
    }
    if (*(int *)(&DAT_805cc924 + slot * 0x16c) != (&DAT_805cca78)[slot * 0x5b]) {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x802cf950  menu_preview_slot_stream_tick
// Per-frame model/texture streaming state machine for the active slot. Advances
// the slot's load timer, (re)spawns the bot when its assets finish loading,
// round-robins the shared streamer (DAT_80612b28/b24) across slots, and kicks
// off model loads for the next needed asset in the slot's range. The scratch
// values read from registers in the original (r7..r10) are streamer arguments;
// they are expressed here as zero-initialised locals per the compile-clean rule.
// ---------------------------------------------------------------------------
void menu_preview_slot_stream_tick(void)
{
    int *rArgPtr = 0;   // in_r7  (asset index table / count ptr)
    unsigned int rArgA = 0; // in_r8
    int rArgB = 0;      // in_r9  (asset cursor)
    int rArgC = 0;      // in_r10

    int slot = FUN_803826d8();
    int roff = slot * 0x16c;
    int *pType = (int *)(&DAT_805cc920 + roff);
    double frameScale = (double)FLOAT_806108ec;
    int assetId = *(int *)(&DAT_8048ca80 + *pType * 0x30);

    // Advance the load timer (decays toward 0).
    float dt = (float)(FLOAT_80619ee8 / (float)((double)(int)DAT_806109c0 - DOUBLE_80619f00));
    *(float *)(&DAT_805cca44 + roff) =
        -(float)(frameScale * (double)dt - (double)*(float *)(&DAT_805cca44 + roff));
    if ((double)*(float *)(&DAT_805cca44 + roff) < (double)FLOAT_80619f08) {
        *(float *)(&DAT_805cca44 + roff) = FLOAT_80619f08;
    }

    int matches = menu_preview_slot_matches_selection(slot);
    if (matches == 0) {
        bool waiting = false;
        int rid = wait_resource_by_id(0, (&DAT_805cca80)[slot * 0x5b],
                                      (&DAT_805cca84)[slot * 0x5b], assetId);
        if (-1 < rid) {
            waiting = true;
            if ((resource_get_load_state(0, rid) != 0) &&
                (*(int *)(&DAT_805cca54 + roff) == 1)) {
                menu_preview_slot_destroy();
                menu_preview_bot_spawn();
                if (DAT_80612b28 == slot) {
                    DAT_80612b28 = -1;
                }
                (&DAT_805cca4c)[slot * 0x5b] = (int)FLOAT_80619f08;
                *(int *)(&DAT_805cca54 + roff) = 2;
                *(int *)(&DAT_805cca58 + roff) = 0;
            }
        }
        if (((double)*(float *)(&DAT_805cca44 + roff) <= (double)FLOAT_80619f08) && (!waiting)) {
            if (DAT_80612b28 == -1) {
                DAT_80612b28 = slot;
            }
            if (DAT_80612b28 == slot) {
                if (((&DAT_805cca74)[slot * 0x5b] != 0) &&
                    (*(unsigned short *)((&DAT_805cca74)[slot * 0x5b] + 0x94) ==
                     (unsigned int)resource_get_asset_id(0, (&DAT_805cca7c)[slot * 0x5b]))) {
                    menu_preview_slot_destroy();
                }
                for (int i = 0; i < 4; i++) {
                    if (resource_is_loaded(0, i) != 0) {
                        texture_slot_unload();
                    }
                }
                model_begin_load(0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 (&DAT_805cca7c)[slot * 0x5b], assetId, assetId,
                                 rArgPtr, rArgA, rArgB, rArgC);
                int rel = ((&DAT_805cca7c)[slot * 0x5b] + 1) - (&DAT_805cca80)[slot * 0x5b];
                int cnt = (&DAT_805cca84)[slot * 0x5b];
                (&DAT_805cca7c)[slot * 0x5b] =
                    (&DAT_805cca80)[slot * 0x5b] + (rel - (rel / cnt) * cnt);
            }
        }
    } else if (*(int *)(&DAT_805cca54 + roff) == 1) {
        (&DAT_805cca4c)[slot * 0x5b] = (int)FLOAT_80619f08;
        *(int *)(&DAT_805cca54 + roff) = 2;
    }

    int botActor = (&DAT_805cca74)[slot * 0x5b];
    if ((botActor != 0) && (*(float *)(botActor + 0xc4) < FLOAT_80619f08)) {
        *(float *)(botActor + 0xc4) = *(float *)(botActor + 0xc4) + FLOAT_80619fa8;
    }

    // Round-robin loader for the group of slots.
    if ((wait_resources_loaded(0, 0, 4) == 0) && (slot == DAT_80612b24)) {
        int mode = *(int *)(&DAT_805cca70 + roff);
        DAT_80612b24 = (DAT_80612b24 + 1) - ((DAT_80612b24 + 1) / DAT_80611014) * DAT_80611014;

        int idx[4];
        for (int k = 0; k < 4; k++) idx[k] = 0;

        if (mode < 2) {
            if (-1 < mode) {
                int cnt = (&DAT_805cca84)[slot * 0x5b];
                rArgB = *(int *)(&DAT_805cca28 + roff) - (cnt / 2);
                if (mode == 0) {
                    rArgB -= 1;
                } else {
                    rArgB += (5 < (unsigned int)cnt) + 1;
                }
                while (rArgB < 0) { rArgB += DAT_805cc4b8; }
                int j = 0;
                int c = cnt;
                while (0 < c) {
                    int wrapped = rArgB - (rArgB / DAT_805cc4b8) * DAT_805cc4b8;
                    rArgB++;
                    idx[j] = (unsigned char)(&DAT_805cc4bc)[wrapped] + pType[wrapped + 2];
                    j++;
                    c--;
                }
            }
        } else if (mode < 4) {
            int seed = *(int *)(&DAT_805cca28 + roff);
            int cnt = (&DAT_805cca84)[slot * 0x5b];
            rArgA = (unsigned int)(unsigned char)(&DAT_805cc4fc)[seed];
            rArgB = pType[seed + 2] - (cnt / 2);
            if (mode == 2) {
                rArgB -= 1;
            } else {
                rArgB += (5 < (unsigned int)cnt) + 1;
            }
            while (rArgB < 0) { rArgB += (int)rArgA; }
            int j = 0;
            int c = cnt;
            while (0 < c) {
                int wrapped = rArgB - (rArgB / (int)rArgA) * (int)rArgA;
                rArgB++;
                idx[j] = (unsigned char)(&DAT_805cc4bc)[seed] + wrapped;
                j++;
                c--;
            }
        }

        // Kick a model load for the first not-yet-loaded asset in the range.
        for (int slotAsset = (&DAT_805cca80)[slot * 0x5b];
             slotAsset < (&DAT_805cca80)[slot * 0x5b] + (&DAT_805cca84)[slot * 0x5b];
             slotAsset++) {
            bool present = false;
            if (((&DAT_805cca74)[slot * 0x5b] == 0) ||
                (*(unsigned short *)((&DAT_805cca74)[slot * 0x5b] + 0x94) !=
                 (unsigned int)resource_get_asset_id(0, slotAsset))) {
                for (int k = 0; k < (&DAT_805cca84)[slot * 0x5b]; k++) {
                    int aid = resource_get_asset_id(0, slotAsset);
                    if (aid == *(int *)(&DAT_8048ca80 + (&DAT_805cbf40)[idx[k]] * 0x30)) {
                        present = true;
                        break;
                    }
                }
            } else {
                present = true;
            }
            if (((double)*(float *)(&DAT_805cca44 + roff) <= (double)FLOAT_80619f08) && (!present)) {
                bool kicked = false;
                for (int k = 0; k < (&DAT_805cca84)[slot * 0x5b]; k++) {
                    int aid = *(int *)(&DAT_8048ca80 + (&DAT_805cbf40)[idx[k]] * 0x30);
                    if (aid != -1) {
                        int existing = wait_resource_by_id(0, (&DAT_805cca80)[slot * 0x5b],
                                                           (&DAT_805cca84)[slot * 0x5b], aid);
                        if (existing == -1) {
                            model_begin_load(0, 0, 0, 0, 0, 0, 0, 0, 0,
                                             slotAsset, aid, aid, rArgPtr, rArgA, rArgB, rArgC);
                            kicked = true;
                            break;
                        }
                    }
                }
                if (kicked) break;
            }
        }
    }
    FUN_80382724();
}
// end menu_preview_slot_stream_tick

// ---------------------------------------------------------------------------
// 0x802cfed8  menu_preview_apply_resource_ctx
// If the given asset is loaded in the slot's resource range, copy its render
// context handle into the shared context record (DAT_806125e8) keyed by the
// asset's context-slot mapping (DAT_805c9858).
// ---------------------------------------------------------------------------
void menu_preview_apply_resource_ctx(int slot, int assetId)
{
    if (assetId == -1) {
        return;
    }
    int rid = wait_resource_by_id(0, (&DAT_805cca80)[slot * 0x5b],
                                  (&DAT_805cca84)[slot * 0x5b], assetId);
    if (rid < 0) {
        return;
    }
    if (resource_get_load_state(0, rid) == 0) {
        return;
    }
    int rec = DAT_806125e8 + (&DAT_805c9858)[assetId] * 0x30;
    int ctx = resource_get_context(0, rid);
    *(int *)(rec + 4) = ctx;
}

// ---------------------------------------------------------------------------
// 0x802cff7c  menu_preview_is_mode7
// Returns -1 (true) when the active mode is the worm mode-7 preview with
// sub-id 6/7/8, else -2. (Original returns -1 - !flag.)
// ---------------------------------------------------------------------------
char menu_preview_is_mode7(void)
{
    bool flag = false;
    if ((DAT_805cc614 == 7) &&
        ((DAT_805cc628 == 6) || (DAT_805cc628 == 7) || (DAT_805cc628 == 8))) {
        flag = true;
    }
    return (char)(-1 - (int)(!flag));
}


