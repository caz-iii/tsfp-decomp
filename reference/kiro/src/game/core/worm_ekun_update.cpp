// worm_ekun_update.cpp
// CDH1 "EKUN" worm-boss set-piece: resource preload, per-frame master update,
// and the scripted level-event glue that drives the column/shutter/gas payload
// gates. Address range 0x801e9990 - 0x801ea31b (batch_620), continuation of
// worm_ekun.cpp. subsystem:game, worm-effect module.
//
// Functions:
//   ekun_preload_resources       (0x801e9990) preload the whole EKUN object-type
//                                set (~24 class ids) then open slides.pak and
//                                reserve the projectile/sound banks (0x1e, 0x3b4,
//                                0xc6b).
//   ekun_update_ambient_loops    (0x801e9a9c) drive two 3D loop-sound sources
//                                (sample 4 and 0x3a2) from the level intensity
//                                ratios (FLOAT_80610d6c / FLOAT_8061269c over
//                                FLOAT_806175f8).
//   ekun_trigger_flyb_spawn_menu (0x801e9b08) put player 0 into a scripted menu
//                                /camera state anchored at the 'FLYB' spawn point.
//   ekun_trigger_cxdp_spawn_menu (0x801e9b5c) same, anchored at the 'CXDP' spawn.
//   ekun_master_update           (0x801e9bb0) the per-frame EKUN master update:
//                                walk the active players, resolve which zone each
//                                occupies (soundpad_find_zone_by_id) and fire the
//                                matching scripted payload; then a long chain of
//                                per-level (DAT_80611d74 class id) payload gates
//                                (shutter water sweep, tip damage, oil/gas/mac1/
//                                fbn/gas0/shut/jum/guy/pete gates), the arc-bolt
//                                and column anim-event ticks, the WIND entity
//                                push, and the DAT_80612704 spray-emitter scale.
//   ekun_khlr_door_sequence      (0x801e9fe8) one-shot 'KHLR' switch sequence:
//                                open the 'GSD1' door if a 'GODC' spawn is ready,
//                                close the 'BARD' door, latch DAT_80612698.
//   ekun_guy_all_absent_gate     (0x801ea17c) fire the payload once GUY1/GUY2/GUY3
//                                are all absent.
//   ekun_hud_show_f8e_text       (0x801ea1f8) submit HUD string 0xf8e to player 0.
//   ekun_trn3_gas_event          (0x801ea244) TRN3-level gas objective: on
//                                objective 4, play the persistent gas loop, fire
//                                the payload, and bark 0xf94 once the fill drops.
//   ekun_toggle_trigger_thunk    (0x801ea2fc) thin thunk to
//                                switch_action_open_close_trigger.
//
// Non-matching: entity ids are 4CC class words (aiactor_find_player_by_team /
// soundpad_find_zone_by_id / spawn_point_find_by_entity resolve them). The PPC
// FP-context spill frames (FUN_803826e8/FUN_803826f0 open, FUN_80382734/
// FUN_8038273c close) are dead ABI plumbing kept opaque; the ROM's uVar7 return
// chaining across the void column_* gates is just register reuse and is dropped.
// Several shared helpers whose generated prototype is reduced-arity are routed
// through file-local typed fn-ptr aliases (__we2_*).
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- raw data symbols (file-local) ---
extern int   DAT_80611d74;      // active-level record ptr
extern int   DAT_80611d78;      // CDH1 level-object array base
extern int   DAT_80611d54;      // weather/wind state block
extern int   DAT_80611b1c;      // scripted flag block
extern int   DAT_80611c24;      // trigger slot pool base
extern int   DAT_80612610;      // active player-slot count
extern int   DAT_80612694;      // OILY zone latch
extern int   DAT_80612698;      // KHLR door one-shot latch
extern int   DAT_806126c8;      // slides.pak handle
extern int   DAT_806126d4;      // shutter "closed" latch
extern int   DAT_80612704;      // wind spray emitter handle
extern int   DAT_8061271c;      // TRN3 bark one-shot latch
extern int   DAT_80610d78;      // active loop-sound handle
extern int   DAT_80610de8;      // ambient loop A handle
extern int   DAT_80610dec;      // ambient loop B handle
extern float FLOAT_80610d6c;    // intensity numerator A
extern float FLOAT_8061269c;    // intensity numerator B

extern float FLOAT_80617428;    // 1.0f
extern float FLOAT_80617440;    // 0.0f
extern float FLOAT_8061750c;
extern float FLOAT_80617504;
extern float FLOAT_80617628;    // TRN3 fill bark threshold
extern float FLOAT_806175f8;    // intensity denominator
extern float FLOAT_806176ac;    // WIND entity Z when state 0x14
extern float FLOAT_806176b0;    // wind push activation threshold
extern const char s_pak_slides_pak_8045300c[]; // "slides.pak" path

// --- raw code symbols (file-local; not in generated headers or wrong arity) ---
extern int   FUN_803826e8(void);   // FP spill frame open
extern void  FUN_80382734(void);   // FP spill frame close
extern void  FUN_803826f0(void);   // FP spill frame open (khlr)
extern void  FUN_8038273c(void);   // FP spill frame close (khlr)
extern void  FUN_802212b4(int mode);       // scripted payload variant (void ret used as chain)
extern void  FUN_80221074(double a, double b, double c, double d,
                          double e, double f, double g, double h); // scripted payload fire
extern int   FUN_8023c32c(int tag);        // entity lookup by 4CC
extern unsigned int FUN_80238fb4(int objId); // objective-state query
extern unsigned int FUN_80220f30(int flagId); // scripted flag query
// forward refs into the next batch (called by ekun_master_update)
extern void  FUN_801ea31c(unsigned long long, double, double, double, double,
                          double, double, double);
extern void  FUN_801ea3e8(unsigned long long, double, double, double, double,
                          double, double, double);

} // extern "C"

// column_* gates + worm_cdh1_level_intensity_curve are declared with C++ linkage
// in generated_decls.h (via entity.h); do not re-declare.

// --- typed fn-ptr aliases for reduced-arity generated_decls prototypes ---

// entity_preload_resources declared (int); the ROM form takes the FP context +
// 7 doubles then the class id, and returns the running context.
static unsigned long long (*const __we2_preload)(unsigned long long, double, double,
                                                 unsigned long long, unsigned long long,
                                                 unsigned long long, unsigned long long,
                                                 unsigned long long, int) =
    (unsigned long long (*)(unsigned long long, double, double, unsigned long long,
                            unsigned long long, unsigned long long, unsigned long long,
                            unsigned long long, int))&entity_preload_resources;

// pack_open declared (const char*,int,int); the ROM form is the full 16-arg
// asset-open context and returns the pack handle.
static int (*const __we2_pack_open)(unsigned long long, double, double, unsigned long long,
                                    unsigned long long, unsigned long long, unsigned long long,
                                    unsigned long long, const char *, int, int, int, int,
                                    int, int, int) =
    (int (*)(unsigned long long, double, double, unsigned long long, unsigned long long,
             unsigned long long, unsigned long long, unsigned long long, const char *, int,
             int, int, int, int, int, int))&pack_open;

// sound_update_3d_source generated order differs; the ROM order is
// (range, pitch, volume, handlePtr, positionPtr, sampleId, flags).
static void (*const __we2_sound3d)(double, double, double, void *, int, int, int) =
    (void (*)(double, double, double, void *, int, int, int))&sound_update_3d_source;

// aiactor_play_bark_global declared 5-arg; the ROM call here is (barkId, frame, rec).
static void (*const __we2_bark)(int, int, int *) =
    (void (*)(int, int, int *))&aiactor_play_bark_global;

// particle_cached_set_scale 2nd param is an integer scale (0 / 0xffff) here.
static void (*const __we2_cached_scale)(int *, int) =
    (void (*)(int *, int))&particle_cached_set_scale;


// ---------------------------------------------------------------------------
// 0x801e9990  ekun_preload_resources
// ---------------------------------------------------------------------------
void ekun_preload_resources(unsigned long long param_1, double param_2, double param_3,
                            unsigned long long param_4, unsigned long long param_5,
                            unsigned long long param_6, unsigned long long param_7,
                            unsigned long long param_8)
{
    unsigned long long ctx = param_1;
    static const int kClassIds[] = {
        0x323, 0x326, 0x327, 0x321, 0x328, 0x36c, 0x743, 0x745, 0x376, 0x377,
        0x378, 0x379, 0x38b, 0x38c, 0x38d, 0x147, 0x144, 0x31d, 0x389, 0x38a,
        0x337, 0x338, 0x339, 0x336
    };
    for (int i = 0; i < (int)(sizeof(kClassIds) / sizeof(kClassIds[0])); ++i) {
        ctx = __we2_preload(ctx, param_2, param_3, param_4, param_5, param_6, param_7,
                            param_8, kClassIds[i]);
    }
    DAT_806126c8 = __we2_pack_open(ctx, param_2, param_3, param_4, param_5, param_6, param_7,
                                   param_8, s_pak_slides_pak_8045300c, 0, 1, 0, 0, 0, 0, 0);
    player_ai_projectile_clear_sound_banks(0x1e);
    sound_clear_bank_ref(0x3b4);
    sound_clear_bank_ref(0xc6b);
}


// ---------------------------------------------------------------------------
// 0x801e9a9c  ekun_update_ambient_loops
// ---------------------------------------------------------------------------
void ekun_update_ambient_loops(void)
{
    __we2_sound3d((double)(FLOAT_80610d6c / FLOAT_806175f8), (double)FLOAT_8061750c,
                  (double)FLOAT_80617428, &DAT_80610de8, 0, 4, 0);
    __we2_sound3d((double)(FLOAT_8061269c / FLOAT_806175f8), (double)FLOAT_8061750c,
                  (double)FLOAT_80617428, &DAT_80610dec, 0, 0x3a2, 0);
}


// ---------------------------------------------------------------------------
// 0x801e9b08  ekun_trigger_flyb_spawn_menu
// ---------------------------------------------------------------------------
void ekun_trigger_flyb_spawn_menu(void)
{
    int iVar2 = *(int *)(DAT_80611d74 + 0x14);
    int iVar1 = spawn_point_find_by_entity(0x464c5942);  // 'FLYB'
    player_enter_menu_state(*(unsigned int *)(iVar2 + 0x150), (unsigned int)iVar1, 0, 4, 0xb, 0);
}


// ---------------------------------------------------------------------------
// 0x801e9b5c  ekun_trigger_cxdp_spawn_menu
// ---------------------------------------------------------------------------
void ekun_trigger_cxdp_spawn_menu(void)
{
    int iVar2 = *(int *)(DAT_80611d74 + 0x14);
    int iVar1 = spawn_point_find_by_entity(0x50445843);  // 'CXDP' (byte-swapped)
    player_enter_menu_state(*(unsigned int *)(iVar2 + 0x150), (unsigned int)iVar1, 0, 2, 0xb, 0);
}


// ---------------------------------------------------------------------------
// 0x801e9bb0  ekun_master_update
// ---------------------------------------------------------------------------
void ekun_master_update(unsigned long long param_1, double param_2, double param_3, double param_4,
                        unsigned long long param_5, unsigned long long param_6,
                        unsigned long long param_7, unsigned long long param_8)
{
    int iVar1, iVar3, iVar4, iVar5, iVar6;
    bool bVar2;
    int local_38, local_34;
    int local_30[10];
    (void)param_1;

    FUN_803826e8();
    bVar2 = false;
    worm_cdh1_level_intensity_curve();
    double dVar8 = (double)FLOAT_80617440;
    iVar6 = 0;
    for (iVar5 = 0; iVar5 < DAT_80612610; iVar5 = iVar5 + 1) {
        iVar4 = DAT_80611d78 + iVar6;
        iVar1 = *(int *)(iVar4 + 0x14);
        if (dVar8 < (double)*(float *)(iVar1 + 0x1cc)) {
            iVar3 = *(int *)(*(int *)(iVar1 + 0x7c) + 0x678);
            if (iVar3 == soundpad_find_zone_by_id(0x5eb)) { FUN_802212b4(1); }
            if (iVar3 == soundpad_find_zone_by_id(0x632)) { DAT_80612694 = 1; }
            if (iVar3 == soundpad_find_zone_by_id(0x5eb)) { FUN_802212b4(3); }
            if (iVar3 == soundpad_find_zone_by_id(0x5e7)) { FUN_802212b4(0); }
            if ((iVar3 == soundpad_find_zone_by_id(0x58c)) &&
                (aiactor_find_player_by_team(0x4f494c59) != 0)) {   // 'OILY'
                FUN_80221074((double)FLOAT_80617440, param_2, param_3, param_4, param_5, param_6,
                             param_7, param_8);
            }
            if (!bVar2) {
                iVar1 = *(int *)(iVar4 + 0xdec);
                if ((iVar1 == 0x4c) || (iVar1 == 0x4b) || (iVar1 == 0x53) ||
                    ((unsigned int)(iVar1 - 0x4f) < 2) || (iVar1 == 0x4e)) {
                    bVar2 = true;
                }
            }
        }
        iVar6 = iVar6 + 0x141c;
    }
    if (bVar2) {
        shutter_water_sweep(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    } else if (-1 < DAT_80610d78) {
        sound_stop_handle((unsigned int)DAT_80610d78);
        DAT_80610d78 = -1;
    }
    if ((DAT_80611d74 != 0) && (*(int *)(DAT_80611d74 + 0xdec) == 0x4c)) {
        ekun_trn3_gas_event(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if (DAT_80611d74 != 0) {
        iVar5 = *(int *)(DAT_80611d74 + 0xdec);
        if ((iVar5 == 0x4c) || (iVar5 == 0x4b) || (iVar5 == 0x53) ||
            ((unsigned int)(iVar5 - 0x55) < 5) || ((unsigned int)(iVar5 - 0x4d) < 4) ||
            ((unsigned int)(iVar5 - 0x46) < 4) || (iVar5 == 0x4a)) {
            shutter_tip_damage_tick(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                    param_8);
            FUN_801ea31c(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        }
    }
    if ((DAT_80611d74 != 0) && (*(int *)(DAT_80611d74 + 0xdec) == 2)) {
        column_oilx_zone_gate(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if ((DAT_80612694 == 1) && (*(int *)(DAT_80611d74 + 0xdec) == 1)) {
        column_oily_absent_gate(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if ((DAT_80611d74 != 0) && (*(int *)(DAT_80611d74 + 0xdec) == 5)) {
        column_mac1_absent_gate(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        column_fbn_zone_gate(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if (((DAT_80611d74 != 0) && (*(int *)(DAT_80611d74 + 0xdec) == 0x4b)) && (DAT_80612698 == 1)) {
        column_gas0_gate(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if ((DAT_80611d74 != 0) && (*(int *)(DAT_80611d74 + 0xdec) == 0x4b)) {
        column_shut_zone_gate(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if ((DAT_80611d74 != 0) && (*(int *)(DAT_80611d74 + 0xdec) == 0x44)) {
        column_jum_all_cleared_gate(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if ((DAT_80611d74 != 0) && (*(int *)(DAT_80611d74 + 0xdec) == 0x2b)) {
        ekun_guy_all_absent_gate(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if ((DAT_80611d74 != 0) &&
        ((*(int *)(DAT_80611d74 + 0xdec) == 2) || (*(int *)(DAT_80611d74 + 0xdec) == 3))) {
        FUN_801ea3e8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    }
    if (DAT_80611d74 != 0) {
        iVar5 = *(int *)(DAT_80611d74 + 0xdec);
        if ((iVar5 == 0x4d) || (iVar5 == 0x4a) || (iVar5 == 0x51)) {
            column_pete_anim_gate(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        }
    }
    arc_bolt_tick();
    arc_nodes_update();
    ekun_update_ambient_loops();
    column_anim_event_dispatch(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    iVar5 = FUN_8023c32c(0x57494e44);   // 'WIND'
    if (iVar5 != 0) {
        *(int *)(iVar5 + 0x64) = *(int *)(DAT_80611d54 + 0x27c);
        if (*(int *)(DAT_80611d78 + 0x13e0) == 0x14) {
            *(float *)(iVar5 + 0x6c) = FLOAT_806176ac;
        } else {
            *(float *)(iVar5 + 0x6c) = FLOAT_80617440;
        }
    }
    if (*(float *)(DAT_80611d54 + 0x280) <= FLOAT_806176b0) {
        weather_set_wind_push_active(0);
    } else {
        weather_set_wind_push_active(1);
    }
    local_30[0] = DAT_80612704;
    bVar2 = particle_handle_is_valid(local_30);
    if (bVar2) {
        if ((((*(char *)(DAT_80611b1c + 0x5b) == '\0') && (*(char *)(DAT_80611b1c + 0x5c) == '\0')) &&
             ((*(char *)(DAT_80611b1c + 0x5d) == '\0') && (*(char *)(DAT_80611b1c + 0x5e) == '\0'))) ||
            ((*(int *)(DAT_80611d54 + 0x2a0) == 0x58) || (*(int *)(DAT_80611d54 + 0x2a0) == 0x57))) {
            local_38 = DAT_80612704;
            __we2_cached_scale(&local_38, 0);
        } else {
            local_34 = DAT_80612704;
            __we2_cached_scale(&local_34, 0xffff);
        }
    }
    FUN_80382734();
}


// ---------------------------------------------------------------------------
// 0x801e9fe8  ekun_khlr_door_sequence
// ---------------------------------------------------------------------------
void ekun_khlr_door_sequence(void)
{
    float fVar1;
    unsigned int *puVar4;
    unsigned int uVar5;
    int iVar3;

    FUN_803826f0();
    int *puVar2 = (int *)switch_find_slot_by_entity(0x4b484c52);   // 'KHLR'
    if (DAT_80612698 == 0) {
        iVar3 = spawn_point_find_by_entity(0x474f4443);            // 'GODC'
        spectator_set_mode_if_ready((double)FLOAT_80617504, iVar3);
        if (DAT_806126d4 != 0) {
            puVar4 = (unsigned int *)trigger_slot_find_by_entity_id(0x47534431);  // 'GSD1'
            fVar1 = FLOAT_80617440;
            if (puVar4 != 0) {
                puVar4[3] = puVar4[3] & 0xfffffffd;
                if (fVar1 == *(float *)&puVar2[0x10]) {
                    uVar5 = *puVar4;
                    if (uVar5 == 0) {
                        iVar3 = 0;
                    } else {
                        iVar3 = DAT_80611c24 + (uVar5 >> 0x16) * 0x2e8;
                        if (*(unsigned int *)(iVar3 + 0x2c) != uVar5) { iVar3 = 0; *puVar4 = 0; }
                    }
                    if (trigger_is_fully_open(iVar3) != 0) {
                        uVar5 = *puVar4;
                        if (uVar5 == 0) {
                            iVar3 = 0;
                        } else {
                            iVar3 = DAT_80611c24 + (uVar5 >> 0x16) * 0x2e8;
                            if (*(unsigned int *)(iVar3 + 0x2c) != uVar5) { iVar3 = 0; *puVar4 = 0; }
                        }
                        trigger_open_full(iVar3, 0);
                    }
                }
            }
        }
        puVar4 = (unsigned int *)trigger_slot_find_by_entity_id(0x42415244);  // 'BARD'
        if (puVar4 != 0) {
            uVar5 = *puVar4;
            if (uVar5 == 0) {
                iVar3 = 0;
            } else {
                iVar3 = DAT_80611c24 + (uVar5 >> 0x16) * 0x2e8;
                if (*(unsigned int *)(iVar3 + 0x2c) != uVar5) { iVar3 = 0; *puVar4 = 0; }
            }
            trigger_close_if_idle(iVar3);
            puVar4[3] = puVar4[3] | 2;
        }
        DAT_80612698 = 1;
        switch_action_open_close_trigger((int)(uintptr_t)puVar2);
    }
    FUN_8038273c();
}


// ---------------------------------------------------------------------------
// 0x801ea17c  ekun_guy_all_absent_gate
// ---------------------------------------------------------------------------
void ekun_guy_all_absent_gate(unsigned long long param_1, double param_2, double param_3,
                              double param_4, double param_5, double param_6, double param_7,
                              double param_8)
{
    (void)param_1;
    int iVar1 = aiactor_find_player_by_team(0x47555931);  // 'GUY1'
    int iVar2 = aiactor_find_player_by_team(0x47555932);  // 'GUY2'
    int iVar3 = aiactor_find_player_by_team(0x47555933);  // 'GUY3'
    if ((iVar1 == 0) && (iVar2 == 0) && (iVar3 == 0)) {
        FUN_80221074((double)FLOAT_80617440, param_2, param_3, param_4, param_5, param_6, param_7,
                     param_8);
    }
}


// ---------------------------------------------------------------------------
// 0x801ea1f8  ekun_hud_show_f8e_text
// ---------------------------------------------------------------------------
void ekun_hud_show_f8e_text(void)
{
    int iVar2 = *(int *)(DAT_80611d78 + 0x14);
    unsigned int *puVar1 = (unsigned int *)get_string_by_id(0xf8e);
    hud_text_submit(*(int *)(*(int *)(iVar2 + 0x150) + 4), 2, puVar1);
}


// ---------------------------------------------------------------------------
// 0x801ea244  ekun_trn3_gas_event
// ---------------------------------------------------------------------------
void ekun_trn3_gas_event(unsigned long long param_1, double param_2, double param_3, double param_4,
                         double param_5, double param_6, double param_7, double param_8)
{
    (void)param_1;
    if ((level_get_current_id() == 0x54524e33) && (FUN_80238fb4(4) != 0)) {   // 'TRN3'
        if (FUN_80220f30(0x11) == 0) {
            sound_play_persistent(0x3b6, 0, 1);
        }
        FUN_80221074((double)FLOAT_80617440, param_2, param_3, param_4, param_5, param_6, param_7,
                     param_8);
        if ((*(float *)(*(int *)(DAT_80611d74 + 0x14) + 0x1cc) <= FLOAT_80617628) &&
            (FUN_80220f30(2) == 0) && (DAT_8061271c == 0)) {
            __we2_bark(0xf94, 0x140, (int *)0);
            DAT_8061271c = 1;
        }
    }
}


// ---------------------------------------------------------------------------
// 0x801ea2fc  ekun_toggle_trigger_thunk
// ---------------------------------------------------------------------------
void ekun_toggle_trigger_thunk(int param_1)
{
    switch_action_open_close_trigger(param_1);
}
