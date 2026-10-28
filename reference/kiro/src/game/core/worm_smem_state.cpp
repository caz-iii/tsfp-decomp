// worm_smem_state.cpp
// 'SEMM'/'SW0x' switch-array reactor set-piece (address range 0x801fcb28 -
// 0x801fd33f, batch_638), continuation of the EKUN set-piece block in
// worm_ekun_state.cpp. subsystem:game, worm-effect module.
//
// This prop is a team-gated switch array: six 'SW00'..'SW05' actors must each be
// resolved and removed to complete the sequence; a set of team actors
// ('OS01'/'MT01'..'MT07') drive their render state through anim events; and a
// 'SEMM' master switch + a soundpad "passable zone" gate blend the ambient door
// sound. State lives in a 0x580 block (DAT_8061278c) allocated at level init.
//
// Functions:
//   smem_anim_event_state_apply (0x801fcb28) when a team actor is playing clip
//                               0xac8/0xacc and an anim event fires, set its
//                               render state word (+0x94=8), clear +0x54c and
//                               accumulate the sound slot.
//   smem_team_anim_event_tick   (0x801fcbb0) per-frame driver: apply the state to
//                               the MT01/MT02/MT03/MT07 team actors, run the OS01
//                               clip-0xa34 open, and step the global actor
//                               (FUN_8027ea98) through its clip-keyed state machine
//                               (clips 0xa3f/0xa48/0xa46/0xa4f/0xa50).
//   smem_switch_state_clear     (0x801fce58) zero the 6 switch state slots
//                               (DAT_8061278c+0x564).
//   smem_switch_progress_track  (0x801fce80) track the SW00..SW05 actors: mark each
//                               present(1)/gone(2), count completions (+0x560), and
//                               when all 6 are done fire the payload (FUN_80221074).
//   smem_level_preload_alloc    (0x801fcf88) preload the 5 model resources, clear
//                               the 5 sound banks, allocate the 0x580 state block,
//                               and run the sub-init (FUN_801fee70).
//   smem_bank_ref_clear         (0x801fd008) clear sound bank ref 0x11.
//   smem_switch_field_reset      (0x801fd02c) clear a field (+0x1d4) on the 'SEMM'
//                               switch entity.
//   smem_zone_passable          (0x801fd068) classify a soundpad zone: passable
//                               unless zone type 5 outside the id windows, type in
//                               [0,9) other than 5, or type 0x23.
//   smem_ambient_tick           (0x801fd108) ramp the ambient open factor (+4 of the
//                               block) by the all-actors-passable gate and drive the
//                               three 3D door sounds (0x11 loop + two 0x359 leaves).
//   smem_mode_gate              (0x801fd2f8) game-mode gate: 1 only when
//                               FUN_80220f70(4) is set and FUN_80220f30(4) is clear.
//
// Non-matching: 4CC ids (SEMM/SW00..SW05/OS01/MT01..MT07 are class words); PPC FP
// spill frames (FUN_803826f0 open / FUN_8038273c close) are opaque; int<->double
// 0x43300000 bias math is written as plain float math. Reduced-arity generated_decls
// helpers (sound_update_3d_source, anim_event_scan_active, anim_active_clip_slot,
// entity_preload_resources, mem_alloc) are routed through file-local typed aliases
// or the simple decl form; the payload/sub-init/global-actor helpers (FUN_80221074
// / FUN_801fee70 / FUN_8027ea98) get file-local externs.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- raw data symbols (file-local) ---
extern int   DAT_8061278c;      // 0x580 state block base (ptr)
extern int   DAT_80612610;      // player/actor count
extern int   DAT_80611d78;      // active actor/character block base
extern int   DAT_806109c0;      // per-frame time unit
extern float DAT_803dfbd8; extern float DAT_803dfbdc; extern float DAT_803dfbe0;  // door leaf A pos
extern float DAT_803dfbe4; extern float DAT_803dfbe8; extern float DAT_803dfbec;  // door leaf B pos

extern float  FLOAT_80617ae8;   // 0.0f
extern float  FLOAT_80617aec;   // 1.0f
extern float  FLOAT_80617af0;   // ambient blend period
extern float  FLOAT_80617af4;   // ambient loop pitch
extern float  FLOAT_80617af8;   // door-leaf sound range
extern float  FLOAT_80617afc;   // door-leaf sound pitch
extern double DOUBLE_80617b00;  // int->double bias for the frame delta

// batch_644 (ZCSC monitor bank) float/double constants
extern float  FLOAT_80617cb4;   // spark spread scale
extern float  FLOAT_80617cb8;   // spark rng span
extern float  FLOAT_80617cbc;   // spark min-length-squared threshold
extern float  FLOAT_80617cc0;   // rng normalize divisor (0xff)
extern float  FLOAT_80617cd0;   // monitor swing seed
extern float  FLOAT_80617cd4;   // monitor anim speed
extern double DOUBLE_80617cc8;  // int->double bias for the rng tie-break
extern double DOUBLE_80617c18;  // 1.0 (double form) [unused alias kept for parity]

// --- helpers not in the shared header (file-local) ---
extern int  FUN_8027ea98(void);            // resolve the global set-piece actor record
extern void FUN_80221074(double a);        // scripted completion payload (wide spill dropped)
void semm_monitor_rt_alloc(void);          // level-init sub-step (defined below, batch_640)
extern int  FUN_803826f0(void);            // FP spill frame open
extern void FUN_8038273c(void);            // FP spill frame close
extern int  FUN_80220f70(int which);       // game-mode/net gate query
extern int  FUN_80220f30(int which);       // game-mode/net gate query (paired)

// anim_active_clip_slot returns the active clip record pointer (decls form is
// variadic); typed here for the *slot == clipId compares.
typedef int *(*ActiveClipSlotFn)(int);
static const ActiveClipSlotFn __we638_anim_active_clip_slot = (ActiveClipSlotFn)&anim_active_clip_slot;

// anim_event_scan_active is called with no args in this TU and returns nonzero when
// the current anim event is live (decls form takes (int,u16)).
typedef int (*AnimEventScanFn)(void);
static const AnimEventScanFn __we638_anim_event_scan_active = (AnimEventScanFn)&anim_event_scan_active;

// sound_update_3d_source is void in decls; ROM returns a start flag and this TU
// passes an all-int trailing argument shape plus a float* position.
typedef void (*Sound3dFn)(double, double, double, int, void *, int, int);
static const Sound3dFn __we638_sound_update_3d_source = (Sound3dFn)&sound_update_3d_source;

// ---------------------------------------------------------------------------
// 0x801fcb28  smem_anim_event_state_apply
// When a team actor is playing clip 0xac8 or 0xacc and an anim event fires this
// frame, set its render-instance state word (+0x94=8), clear +0x54c and accumulate
// the sound slot (channel 3, +1000).
// ---------------------------------------------------------------------------
void smem_anim_event_state_apply(int actor)
{
    int inst;
    if ((actor != 0) &&
        ((anim_is_clip_active(actor, 0xac8) != 0 || anim_is_clip_active(actor, 0xacc) != 0) &&
         (__we638_anim_event_scan_active() != 0))) {
        inst = *(int *)(actor + 0x7c);
        *(int *)(inst + 0x94) = 8;
        *(unsigned char *)(inst + 0x54c) = 0;
        object_sound_slot_accumulate(inst, 3, 1000);
    }
}

// ---------------------------------------------------------------------------
// 0x801fcbb0  smem_team_anim_event_tick
// Per-frame anim-event driver. Applies the state-apply to the MT01/MT02/MT03/MT07
// team actors; opens the OS01 actor on clip 0xa34; and steps the global actor
// (FUN_8027ea98) through a clip-keyed state machine (clips 0xa3f/0xa48/0xa46/0xa4f/
// 0xa50) toggling its state word (+0x94 of *global) and two byte flags.
// ---------------------------------------------------------------------------
void smem_team_anim_event_tick(void)
{
    int os01;
    int *global;
    int actor;
    int *clip;
    int inst;

    os01   = aiactor_find_player_by_team(0x4f533031);   // 'OS01'
    global = (int *)FUN_8027ea98();

    smem_anim_event_state_apply(aiactor_find_player_by_team(0x4d543031));  // 'MT01'
    smem_anim_event_state_apply(aiactor_find_player_by_team(0x4d543032));  // 'MT02'
    smem_anim_event_state_apply(aiactor_find_player_by_team(0x4d543033));  // 'MT03'

    if (os01 != 0) {
        if (__we638_anim_event_scan_active() != 0) {
            clip = __we638_anim_active_clip_slot(os01);
            if (*clip == 0xa34) {
                inst = *(int *)(os01 + 0x7c);
                *(int *)(inst + 0x94) = 0x24;
                *(unsigned char *)(inst + 0x54c) = 0;
                object_sound_slot_accumulate(inst, 0x29, 1000);
            }
        }
    }

    actor = aiactor_find_player_by_team(0x4d543037);   // 'MT07'
    if ((actor != 0) && (__we638_anim_event_scan_active() != 0) &&
        (clip = __we638_anim_active_clip_slot(actor), *clip == 0xa34)) {
        inst = *(int *)(actor + 0x7c);
        *(int *)(inst + 0x94) = 0x24;
        *(unsigned char *)(inst + 0x54c) = 0;
        object_sound_slot_accumulate(inst, 0x29, 1000);
    }

    if ((global != (int *)0) && (*global != 0)) {
        int g = *global;
        if (anim_is_clip_active(g, 0xa3f) != 0) {
            if (__we638_anim_event_scan_active() != 0) {
                global[0x25] = 0;
                *(unsigned char *)(global + 0x155) = 0;
            }
            if (__we638_anim_event_scan_active() != 0) {
                global[0x25] = 0x41;
                *(unsigned char *)(global + 0x153) = 0;
                *(unsigned char *)((int)global + 0x58d) = 1;
            }
            if (__we638_anim_event_scan_active() != 0) {
                global[0x25] = 0;
                *(unsigned char *)((int)global + 0x58d) = 0;
            }
        }
        if ((anim_is_clip_active(g, 0xa48) != 0) && (__we638_anim_event_scan_active() != 0)) {
            global[0x25] = 0;
            *(unsigned char *)(global + 0x155) = 0;
        }
        if ((anim_is_clip_active(g, 0xa46) != 0) && (__we638_anim_event_scan_active() != 0)) {
            global[0x25] = 8;
            *(unsigned char *)(global + 0x153) = 0;
            object_sound_slot_accumulate((int)global, 3, 1000);
        }
        if ((anim_is_clip_active(g, 0xa4f) != 0) && (__we638_anim_event_scan_active() != 0)) {
            global[0x25] = 0;
            *(unsigned char *)(global + 0x155) = 0;
        }
        if ((anim_is_clip_active(g, 0xa50) != 0) && (__we638_anim_event_scan_active() != 0)) {
            global[0x25] = 8;
            *(unsigned char *)(global + 0x153) = 0;
            object_sound_slot_accumulate((int)global, 3, 1000);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801fce58  smem_switch_state_clear
// Zero the 6 switch state slots at DAT_8061278c+0x564.
// ---------------------------------------------------------------------------
void smem_switch_state_clear(void)
{
    int off = 0;
    for (int i = 6; i != 0; i = i - 1) {
        *(int *)(DAT_8061278c + off + 0x564) = 0;
        off = off + 4;
    }
}

// ---------------------------------------------------------------------------
// 0x801fce80  smem_switch_progress_track
// Track the six 'SW00'..'SW05' actors. For each: if absent and its state was 1
// (armed), advance to 2 (done) and bump the completion count (+0x560); if present
// and state was 0, arm it (state 1). Once all 6 are done, fire the payload.
// ---------------------------------------------------------------------------
void smem_switch_progress_track(double a)
{
    int sw[6];
    sw[0] = aiactor_find_player_by_team(0x53573030);  // 'SW00'
    sw[1] = aiactor_find_player_by_team(0x53573031);  // 'SW01'
    sw[2] = aiactor_find_player_by_team(0x53573032);  // 'SW02'
    sw[3] = aiactor_find_player_by_team(0x53573033);  // 'SW03'
    sw[4] = aiactor_find_player_by_team(0x53573034);  // 'SW04'
    sw[5] = aiactor_find_player_by_team(0x53573035);  // 'SW05'

    int off = 0;
    for (int i = 6; i != 0; i = i - 1) {
        int *state = (int *)(DAT_8061278c + off + 0x564);
        if (*(int *)((int)sw + off) == 0) {
            if (*state == 1) {
                *state = 2;
                *(int *)(DAT_8061278c + 0x560) = *(int *)(DAT_8061278c + 0x560) + 1;
            }
        } else if (*state == 0) {
            *state = 1;
        }
        off = off + 4;
    }
    if (*(int *)(DAT_8061278c + 0x560) == 6) {
        FUN_80221074((double)FLOAT_80617ae8);
    }
}

// ---------------------------------------------------------------------------
// 0x801fcf88  smem_level_preload_alloc
// Preload the 5 model resources (0x4fc/0x4f7/0x503/0x4d6/0x4d7), clear the 5 sound
// banks (0x51/0x11/0x3b4/0x649/0x64a), allocate the 0x580 state block, and run the
// sub-init. (The ROM chains preload/mem_alloc through the FP-spill frame; reduced
// to the simple decl forms here.)
// ---------------------------------------------------------------------------
void smem_level_preload_alloc(void)
{
    entity_preload_resources(0x4fc);
    entity_preload_resources(0x4f7);
    entity_preload_resources(0x503);
    entity_preload_resources(0x4d6);
    entity_preload_resources(0x4d7);
    sound_clear_bank_ref(0x51);
    sound_clear_bank_ref(0x11);
    sound_clear_bank_ref(0x3b4);
    sound_clear_bank_ref(0x649);
    sound_clear_bank_ref(0x64a);
    DAT_8061278c = mem_alloc(0x580, 1);
    semm_monitor_rt_alloc();
}

// ---------------------------------------------------------------------------
// 0x801fd008  smem_bank_ref_clear
// Clear sound bank ref 0x11.
// ---------------------------------------------------------------------------
void smem_bank_ref_clear(void)
{
    sound_clear_bank_ref(0x11);
}

// ---------------------------------------------------------------------------
// 0x801fd02c  smem_switch_field_reset
// Clear the +0x1d4 field on the 'SEMM' switch entity (if present).
// ---------------------------------------------------------------------------
void smem_switch_field_reset(void)
{
    int *slot = (int *)switch_find_slot_by_entity(0x53454d4d);   // 'SEMM'
    if (slot != (int *)0) {
        *(int *)(slot[2] + 0x1d4) = 0;
    }
}

// ---------------------------------------------------------------------------
// 0x801fd068  smem_zone_passable
// Classify a soundpad zone: return 1 (passable) except when zone type is 5 and the
// entity id is outside the two id windows (== 0x5e0, or in [0x50b,0x50e]), or when
// the type is in [0,9) other than 5, or when the type is 0x23.
// ---------------------------------------------------------------------------
int smem_zone_passable(int zoneParam)
{
    int *padEntry = soundpad_resolve_zone_node(zoneParam);
    int zoneType = soundpad_get_entity_zone_id((int)padEntry);
    if (zoneType < 9) {
        if (zoneType == 5) {
            int id = soundpad_get_entity_id_from_zone(zoneParam);
            if ((id != 0x5e0) && (((0x5df < id || (0x50e < id)) || (id < 0x50b)))) {
                return 0;
            }
            return 1;
        }
        if ((4 < zoneType) || (-1 < zoneType)) {
            return 0;
        }
    } else if (zoneType == 0x23) {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x801fd108  smem_ambient_tick
// Ramp the ambient open factor (+4 of DAT_8061278c): when every live actor is in a
// passable zone the factor ramps down, otherwise up (note: inverse of the WALL
// prop); clamp [0,1]. Drive the 0x11 ambient loop plus the two 0x359 door-leaf
// sounds at the leaf positions (DAT_803dfbd8.. / DAT_803dfbe4..). FP-spill wrapped.
// ---------------------------------------------------------------------------
void smem_ambient_tick(void)
{
    unsigned int all;
    float step, delta, f, clamped, out;
    int i, off;
    float leafA[3];  // local_2c/28/24
    float leafB[3];  // local_38/34/30

    FUN_803826f0();
    all = 1;
    i = 0; off = 0;
    leafA[0] = DAT_803dfbd8; leafA[1] = DAT_803dfbdc; leafA[2] = DAT_803dfbe0;
    leafB[0] = DAT_803dfbe4; leafB[1] = DAT_803dfbe8; leafB[2] = DAT_803dfbec;
    while ((i < DAT_80612610) && (all != 0)) {
        if (-1 < *(int *)(DAT_80611d78 + off + 4)) {
            unsigned int p = smem_zone_passable(
                *(int *)(*(int *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x7c) + 0x678));
            all = all & p;
        }
        i = i + 1;
        off = off + 0x141c;
    }
    step = (FLOAT_80617aec /
        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) / FLOAT_80617af0;
    delta = (all == 0) ? step : -step;
    *(float *)(DAT_8061278c + 4) = *(float *)(DAT_8061278c + 4) + delta;
    f = *(float *)(DAT_8061278c + 4);
    clamped = f;
    if (FLOAT_80617aec < f) clamped = FLOAT_80617aec;
    out = FLOAT_80617ae8;
    if ((FLOAT_80617ae8 <= clamped) && (out = f, FLOAT_80617aec < f)) out = FLOAT_80617aec;
    *(float *)(DAT_8061278c + 4) = out;
    __we638_sound_update_3d_source((double)*(float *)(DAT_8061278c + 4), (double)FLOAT_80617af4, (double)FLOAT_80617aec,
                                   DAT_8061278c + 8, (void *)0, 0x11, 0);
    __we638_sound_update_3d_source((double)FLOAT_80617af8, (double)FLOAT_80617afc, (double)FLOAT_80617aec,
                                   DAT_8061278c + 0xc, leafA, 0x359, 0);
    __we638_sound_update_3d_source((double)FLOAT_80617af8, (double)FLOAT_80617afc, (double)FLOAT_80617aec,
                                   DAT_8061278c + 0x10, leafB, 0x359, 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801fd2f8  smem_mode_gate
// Game-mode gate: return 1 only when FUN_80220f70(4) is set and FUN_80220f30(4)
// is clear; else 0.
// ---------------------------------------------------------------------------
int smem_mode_gate(void)
{
    if ((FUN_80220f70(4) == 0) || (FUN_80220f30(4) != 0)) {
        return 0;
    }
    return 1;
}

} // extern "C"

// ===========================================================================
// batch_639 continuation (address range 0x801fd340 - 0x801fe954)
//
// The SEMM reactor set-piece's per-frame master driver + level init, plus the
// 'VSBO' debris/spark prop that the sequence shatters. subsystem:game,
// worm-effect module. Appended to worm_smem_state.cpp because this is the direct
// address-order continuation of the SEMM block above.
//
//   semm_director_death_ramp (0x801fd340) drives a "cutscene director" actor's
//                            death-init: eases a per-difficulty rate into the
//                            actor's +0x1cc accumulator by the frame delta and,
//                            once it drops below zero, calls the wide-ABI
//                            character_on_death_init.
//   semm_master_tick         (0x801fd40c) the SEMM per-frame master: runs the
//                            all-actor payload broadcast (when gate 9 clear),
//                            smem_ambient_tick, the director-cam screen-effect
//                            bind for the P2/P4 view actor (FUN_8027ea98), the
//                            team anim-event tick, the SW switch-progress track
//                            for actor types 0x25/0x26, and the 'DWDC' door
//                            trigger open when a 'BCPT' actor is moving.
//   semm_level_init          (0x801fd704) allocates+clears the 0x57c state block,
//                            registers the two 'BSV?'/'BO..' level actors + the
//                            resource pool, flags ~21 soundpad zones with the
//                            0x10000/0x20000 portal bits, builds the four steam/
//                            spark/smoke particle defs+emitters, and clears the
//                            switch state.
//   semm_invs_clear_flag     (0x801fdda8) clear bit0 of the 'INVS' entity's
//                            +0x28 flags (make it non-solid/visible).
//   vsbo_attach_light        (0x801fdde4) resolve the entity's submesh light node
//                            (+0x2d8->+0x28) and attach it to the view actor
//                            (DAT_80611d74+0x14).
//   vsbo_death_shatter       (0x801fde1c) on the 0x2f "shatter" anim clip: hide
//                            the 0x4e0/0x4e1 submeshes (state 1, fade 0), play the
//                            0x35f break sound at the origin, clear render type,
//                            randomize the settle timer (+0x1a4), seed the target
//                            position (+0xb4.. from param+10 or origin+offset), and
//                            reset the +0x1cc accumulator.
//   vsbo_settle_tick         (0x801fdfb8) per-frame settle: when the accumulators
//                            hit rest set the 0x4e0/0x4e1 submeshes to state 3 and
//                            clear render type; while the settle timer is positive
//                            ease it down and spawn a burst of fading sparks
//                            (rng-jittered) with a fixed last-scale.
//   vsbo_register            (0x801fe418) 'VSBO' ('VSBO'/'VBO\0') actor register
//                            wrapper -> level_actors_register + vsbo_attach_light.
//   vsbo_ambient_tick        (0x801fe454) per-frame VSBO ambient: ease the +0x7c
//                            state's open factor, sweep the door leaf along a
//                            poly-trig arc, gate on any nearby live actor within
//                            the swing corridor, run the two-leaf open state
//                            machine (raycast the swept leaf, register hit actors
//                            for update or shatter a struck VSBO, measure travel),
//                            and drive the 0x35e 3D swing sound.
//   vsbo_spark_render        (0x801fe954) additive spark billboard pass for the
//                            open VSBO leaf: transform the +0x274 matrix offset,
//                            scale by the +0xd0 travel, set the additive GX state
//                            and draw the 0x33b spark billboard.
//
// Non-matching: 4CC ids (SEMM/VSBO/INVS/BCPT/DWDC are class words); PPC FP spill
// frames (FUN_803826e8/803826f0 open / FUN_80382734/8038273c close) are opaque;
// int<->double 0x43300000 bias math is written as plain float math; the wide
// FP-ABI helpers (level_actors_register / level_resource_pool_setup /
// particle_def_create / particle_emitter_create / character_on_death_init /
// scene_raycast / rng_counter_tick) are routed through file-local typed aliases
// matching the ROM call shape.
// ===========================================================================

// --- batch_639 raw data symbols (file-local) ---
extern int   DAT_80611d74;        // active view/HUD actor record
extern int   DAT_80611b8c;        // soundpad zone table base
extern int   DAT_80612098;        // total actor slot count
extern char  DAT_805015bd;        // level/story-mode selector byte
extern char  DAT_805015bf;        // difficulty selector byte
extern int   DAT_805015c0;        // global gameplay flag word
extern int   DAT_80588610;        // actor pointer array base
extern int   DAT_80611d54;        // active view/camera record (matrix at +0x208)
extern int   DAT_80455f34;        // level-actor class descriptor block 0 (by address)
extern int   DAT_80455f4c;        // level-actor class descriptor block 1 (by address)
extern int   DAT_806108e4;        // frame counter (ambient phase)
extern int   DAT_806108ec;        // frame counter (secondary phase)
extern int   DAT_80612250;        // gx render-state scratch
extern int   DAT_80612258;        // gx render-state scratch
extern int   DAT_80612798;        // smoke-overlay latch
extern int   DAT_8058e3cc;        // billboard-facing matrix scratch
extern float DAT_803dfbfc; extern float DAT_803dfc00; extern float DAT_803dfc04;  // leaf sweep axis
extern float DAT_803dfc08; extern float DAT_803dfc0c; extern float DAT_803dfc10;  // spark rail axis

extern float FLOAT_806108ec;      // secondary phase as float
extern float FLOAT_80617b08; extern float FLOAT_80617b0c; extern float FLOAT_80617b10; // difficulty rates
extern float FLOAT_80617b14; extern float FLOAT_80617b18; extern float FLOAT_80617b1c;
extern float FLOAT_80617b20; extern float FLOAT_80617b24; extern float FLOAT_80617b28;
extern float FLOAT_80617b2c; extern float FLOAT_80617b30; extern float FLOAT_80617b34;
extern float FLOAT_80617b38; extern float FLOAT_80617b3c; extern float FLOAT_80617b40;
extern float FLOAT_80617b44; extern float FLOAT_80617b48; extern float FLOAT_80617b4c;
extern float FLOAT_80617b50; extern float FLOAT_80617b54; extern float FLOAT_80617b58;
extern float FLOAT_80617b5c; extern float FLOAT_80617b60; extern float FLOAT_80617b64;
extern float FLOAT_80617b68; extern float FLOAT_80617b6c; extern float FLOAT_80617b70;
extern float FLOAT_80617b74; extern float FLOAT_80617b78; extern float FLOAT_80617b7c;
extern float FLOAT_80617b80; extern float FLOAT_80617b84; extern float FLOAT_80617b88;
extern float FLOAT_80617b8c;
extern float FLOAT_80617b90;      // rng->float scale
extern double DOUBLE_80617b98;    // rng->double bias
extern float FLOAT_80617ba0; extern float FLOAT_80617ba4; extern float FLOAT_80617ba8;
extern float FLOAT_80617bac; extern float FLOAT_80617bb0; extern float FLOAT_80617bb4;
extern float FLOAT_80617bb8;

// --- batch_639 helpers not in the shared header (file-local) ---
extern int  FUN_803826e8(void);            // FP spill frame open (director tick)
extern void FUN_80382734(void);            // FP spill frame close (director tick)
extern int  FUN_8023c32c(int tag);         // entity lookup by 4CC
extern int  FUN_8023c59c(int tag, int outArray, int maxCount); // collect submeshes/entities
extern int  FUN_8025c8e8(int node);        // resolve light node from submesh
extern void FUN_8025caf0(int light, int mode, int owner); // attach light to owner
extern void FUN_80234560(int a, double b, double c, double d, double e, double f,
                         int g, int h, int classId, int classHi, int k,
                         int l, int m, int n, int o, int p); // generic actor register
extern void FUN_803759c8(int a, int b, int c, int d, int e, int f, int g, int h,
                         int i, int j, int k, int l, int m, int n, int o, int p); // pool finalize
extern void FUN_8036fcf4(int mode);        // fx group activate
extern void FUN_802215a8(int slot, int a, int b, int c, int d); // HUD objective slot setup

// level_actors_register / level_resource_pool_setup: same wide-ABI aliases used in
// worm_ekun_state.cpp (declared file-local here, distinct name to avoid a clash).
static unsigned long long (*const __we639_level_actors_register)(
    unsigned long long, double, double, double, double, double, int, int,
    int, void *, int) =
    (unsigned long long (*)(unsigned long long, double, double, double, double,
                            double, int, int, int, void *, int))&level_actors_register;
static void (*const __we639_level_resource_pool_setup)(
    unsigned long long, double, double, double, double, double, int, int) =
    (void (*)(unsigned long long, double, double, double, double, double, int, int))
    &level_resource_pool_setup;

// particle_def_create: 2 packed floats (as doubles) + def ptr + 5 ints.
static void (*const __we639_particle_def_create)(double, double, int, int, int, int, int, int) =
    (void (*)(double, double, int, int, int, int, int, int))&particle_def_create;
// particle_emitter_create: 3 doubles + 2 def ptrs + 2 ints.
static void (*const __we639_particle_emitter_create)(double, double, double, int, int, int, int) =
    (void (*)(double, double, double, int, int, int, int))&particle_emitter_create;

// character_on_death_init ROM full ABI: entity + 8 fp regs (decls form is (int)).
static void (*const __we639_character_on_death_init)(double, double, double, double,
                                                     double, double, double, double, int) =
    (void (*)(double, double, double, double, double, double, double, double, int))
    &character_on_death_init;

// scene_raycast returns the struck node record (decls form is void).
typedef int (*RaycastFn)(int, float *, float *, int, float *, float *, int *, unsigned int, int, int *);
static const RaycastFn __we639_scene_raycast = (RaycastFn)&scene_raycast;

// rng_counter_tick returns its LCG counter (decls form is void).
static unsigned int (*const __we639_rng)(void) = (unsigned int (*)(void))&rng_counter_tick;

// ekun_spark_billboard_render lives in worm_ekun_state.cpp (not in generated_decls).
extern void ekun_spark_billboard_render(float *endPtr, float *startPtr, unsigned int col, int tex);

// ---------------------------------------------------------------------------
// 0x801fd340  semm_director_death_ramp
// Ease a per-difficulty rate into the director actor's +0x1cc accumulator by the
// frame delta; once it goes negative, invoke the wide-ABI character death-init.
// ---------------------------------------------------------------------------
void semm_director_death_ramp(int *director)
{
    double zero  = DOUBLE_80617b00;
    double rate  = (double)FLOAT_80617aec;
    int    ent   = *director;

    if (DAT_805015bf == 1)       rate = (double)FLOAT_80617b0c;
    else if (DAT_805015bf == 0)  rate = (double)FLOAT_80617b08;
    else if (DAT_805015bf < 3)   rate = (double)FLOAT_80617b10;

    double floor0 = (double)FLOAT_80617ae8;
    double step   = (double)(FLOAT_80617aec /
        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00));
    double scaled = (double)(float)(rate *
        (double)(float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - DOUBLE_80617b00));

    *(float *)(ent + 0x1cc) = -(float)(step * scaled - (double)*(float *)(ent + 0x1cc));
    if ((double)*(float *)(ent + 0x1cc) < floor0) {
        __we639_character_on_death_init((double)*(float *)(ent + 0x1cc), scaled, step, zero, rate,
                                        0, 0, 0, ent);
    }
}

// ---------------------------------------------------------------------------
// 0x801fd40c  semm_master_tick
// The SEMM per-frame master driver.
// ---------------------------------------------------------------------------
void semm_master_tick(unsigned long long param_1, double param_2, double param_3, double param_4,
                      double param_5, double param_6, int param_7, int param_8)
{
    int  *view;
    int   off, i;

    FUN_803826e8();

    // Broadcast the completion payload to every live actor whose +0x570 flag is set,
    // unless gate 9 is active.
    if (FUN_80220f30(9) == 0) {
        off = 0;
        for (i = 0; i < DAT_80612610; i = i + 1) {
            if (*(char *)(*(int *)((int)&DAT_80588610 + off) + 0x570) == '\x01') {
                FUN_80221074((double)FLOAT_80617ae8);
            }
            off = off + 4;
        }
    }

    smem_ambient_tick();

    // Director-cam bind: pick the P2 (gate 4) or P4 (gate 2) view actor and, when
    // its gate is clear, allocate+bind a screen-effect slot; the P2 path also runs
    // the director death ramp. Clearing the gate drops the bound record.
    if ((DAT_805015bd != '\n') || (DAT_80612610 < 2)) {
        if ((FUN_80220f70(4) != 0) && (DAT_80611d74 != 0)) {
            view = (int *)FUN_8027ea98();
            if (FUN_80220f30(4) == 0) {
                if (*(int *)(DAT_80611d74 + 0xe8c) != *view) {
                    *(int *)(DAT_80611d74 + 0xe8c) = *view;
                    *(int *)(*view + 0x268) = (int)screen_effect_alloc_slot();
                    screen_effect_init_slot((int *)(*view + 0x268), view);
                }
            } else {
                *(int *)(DAT_80611d74 + 0xe8c) = 0;
            }
        } else if ((FUN_80220f70(2) != 0) && (DAT_80611d74 != 0)) {
            view = (int *)FUN_8027ea98();
            if (FUN_80220f30(10) == 0) {
                if (*(int *)(DAT_80611d74 + 0xe8c) != *view) {
                    *(int *)(DAT_80611d74 + 0xe8c) = *view;
                    *(int *)(*view + 0x268) = (int)screen_effect_alloc_slot();
                    screen_effect_init_slot((int *)(*view + 0x268), view);
                }
                semm_director_death_ramp(view);
            } else {
                *(int *)(DAT_80611d74 + 0xe8c) = 0;
            }
        }
    }

    // smem_team_anim_event_tick is void in this TU (batch_638); the ROM passes its
    // return value into the switch tracker as an unused scratch double.
    smem_team_anim_event_tick();
    off = 0;
    for (i = 0; i < DAT_80612610; i = i + 1, off = off + 0x141c) {
        int t = *(int *)(DAT_80611d78 + off + 0xdec);
        if ((t == 0x25) || (t == 0x26)) {
            smem_switch_progress_track((double)FLOAT_80617ae8);
            break;
        }
        t = *(int *)(DAT_80611d78 + off + 0x1400);
        if ((t < 0xb) && (8 < t)) {
            int door = trigger_slot_resolve_entity(0x44574443);   // 'DWDC'
            // find a live 'BCPT' actor in the tail slot range
            int idx = DAT_80612610 << 2;
            int rem = DAT_80612098 - DAT_80612610;
            int bcpt = 0;
            if (DAT_80612610 < DAT_80612098) {
                do {
                    bcpt = *(int *)((int)&DAT_80588610 + idx);
                    if ((bcpt != 0) && (*(int *)(bcpt + 0xbf0) != 0) &&
                        (*(int *)(*(int *)(bcpt + 0xbf0) + 8) == 0x54504342)) break;  // 'BCPT'
                    idx = idx + 4;
                    rem = rem - 1;
                    bcpt = 0;
                } while (rem != 0);
            }
            if ((trigger_is_moving(door) != 0) && (bcpt != 0)) {
                int guard = soundpad_get_entity_id_from_zone(*(int *)(bcpt + 0x678));
                bool blocked = false;
                if (guard == 0x549) {
                    unsigned int f = (DAT_805015bd == '\n')
                        ? *(unsigned int *)(bcpt + 0x14)
                        : *(unsigned int *)(bcpt + 0x1c);
                    if ((f & 1) == 0) blocked = true;
                }
                if (!blocked) {
                    trigger_clear_flags(door, 0x4000);
                    trigger_close_if_idle(door);
                    trigger_set_flags(door, 2);
                }
            }
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x801fd704  semm_level_init
// Allocate+clear the 0x57c state block, register the two level actors + resource
// pool, set the soundpad portal bits on ~21 zones, build the four particle defs
// + emitters, and clear the switch state.
// ---------------------------------------------------------------------------
void semm_level_init(unsigned long long param_1, double param_2, double param_3, double param_4,
                     double param_5, double param_6, int param_7, int param_8)
{
    unsigned long long ctx;
    int zone;

    memset_wrapper(DAT_8061278c, 0, 0x57c);
    *(int *)(DAT_8061278c + 8)  = -1;
    *(int *)(DAT_8061278c + 0xc) = -1;
    *(int *)(DAT_8061278c + 0x10) = -1;

    ctx = __we639_level_actors_register(param_1, param_2, param_3, param_4, param_5, param_6,
                                        param_7, param_8, 0, &DAT_80455f34, 1);
    ctx = __we639_level_actors_register(ctx, param_2, param_3, param_4, param_5, param_6,
                                        param_7, param_8, 1, &DAT_80455f4c, 1);
    __we639_level_resource_pool_setup(ctx, param_2, param_3, param_4, param_5, param_6,
                                      param_7, param_8);
    FUN_803759c8((int)ctx, (int)param_2, (int)param_3, (int)param_4, (int)param_5, (int)param_6,
                 param_7, param_8, 0x40, 0x40, 1, DAT_8061278c, param_7, param_8, param_7, param_8);

    // Portal-flag the SEMM soundpad zones (0x20000 = two-way, 0x10000 = one-way).
    static const struct { int id; unsigned int bit; } kZones[] = {
        {0x42d,0x20000},{0x4a0,0x20000},{0x4ac,0x20000},{0x5df,0x20000},{0x5e5,0x10000},
        {0x538,0x10000},{0x537,0x20000},{0x5eb,0x10000},{0x5d9,0x10000},{0x5f3,0x20000},
        {0x5f2,0x20000},{0x5f1,0x20000},{0x5f0,0x20000},{0x5ef,0x20000},{0x5ed,0x20000},
        {0x614,0x20000},{0x619,0x20000},{0x616,0x20000},{0x617,0x20000},{0x61b,0x20000},
        {0x5e4,0x10000},
    };
    for (unsigned int z = 0; z < sizeof(kZones) / sizeof(kZones[0]); z = z + 1) {
        zone = DAT_80611b8c + soundpad_find_zone_by_id(kZones[z].id) * 0x20;
        *(unsigned int *)(zone + 4) = *(unsigned int *)(zone + 4) | kZones[z].bit;
    }

    FUN_8036fcf4(2);

    // Steam plume (0x34e).
    __we639_particle_def_create((double)FLOAT_80617aec, (double)FLOAT_80617aec, DAT_8061278c + 0x224, 0, 0, 0x13, 0x34e, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617b14, (double)FLOAT_80617ae8, (double)FLOAT_80617ae8, (double)FLOAT_80617b18, 0x404060ff, 0x606070ff, 0x101010ff);
    particle_def_set_size_start_end((double)FLOAT_80617ae8, (double)FLOAT_80617b1c, (double)FLOAT_80617b20, (double)FLOAT_80617b24);
    particle_def_set_velocity_ranges((double)FLOAT_80617b28, (double)FLOAT_80617b28, (double)FLOAT_80617aec, (double)FLOAT_80617b2c, (double)FLOAT_80617aec, (double)FLOAT_80617af8);
    particle_def_set_color_end((double)FLOAT_80617ae8, (double)FLOAT_80617b14, (double)FLOAT_80617b30, (double)FLOAT_80617b34);
    __we639_particle_emitter_create((double)FLOAT_80617b38, (double)FLOAT_80617b3c, (double)FLOAT_80617b40, DAT_8061278c + 0x14, DAT_8061278c + 0x224, 0, 0x4200006);
    particle_emitter_set_color_range((double)FLOAT_80617b44, (double)FLOAT_80617ae8, (double)FLOAT_80617b48, (double)FLOAT_80617ae8);

    // Sparks (0x326).
    __we639_particle_def_create((double)FLOAT_80617b18, (double)FLOAT_80617aec, DAT_8061278c + 0x2f0, 0, 1, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617b14, (double)FLOAT_80617ae8, (double)FLOAT_80617ae8, (double)FLOAT_80617b18, 0x404040ff, 0x606060ff, 0x101010ff);
    particle_def_set_size_start_end((double)FLOAT_80617ae8, (double)FLOAT_80617b1c, (double)FLOAT_80617b20, (double)FLOAT_80617b24);
    particle_def_set_velocity_ranges((double)FLOAT_80617b28, (double)FLOAT_80617b4c, (double)FLOAT_80617b18, (double)FLOAT_80617b18, (double)FLOAT_80617aec, (double)FLOAT_80617af8);
    particle_def_set_color_end((double)FLOAT_80617b50, (double)FLOAT_80617b14, (double)FLOAT_80617b14, (double)FLOAT_80617b34);
    __we639_particle_emitter_create((double)FLOAT_80617b54, (double)FLOAT_80617b58, (double)FLOAT_80617b28, DAT_8061278c + 0x98, DAT_8061278c + 0x2f0, 0, 0x200006);
    particle_emitter_set_color_range((double)FLOAT_80617aec, (double)FLOAT_80617ae8, (double)FLOAT_80617b48, (double)FLOAT_80617ae8);

    // Debris/dust (0x32e).
    __we639_particle_def_create((double)FLOAT_80617b40, (double)FLOAT_80617aec, DAT_8061278c + 0x3bc, 0, 0, 1, 0x32e, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617b14, (double)FLOAT_80617ae8, (double)FLOAT_80617ae8, (double)FLOAT_80617b18, 0x6f6f6fff, 0x6f6f6fff, 0x101010ff);
    particle_def_set_size_start_end((double)FLOAT_80617ae8, (double)FLOAT_80617b1c, (double)FLOAT_80617b20, (double)FLOAT_80617b24);
    particle_def_set_velocity_ranges((double)FLOAT_80617b4c, (double)FLOAT_80617b5c, (double)FLOAT_80617b4c, (double)FLOAT_80617b5c, (double)FLOAT_80617aec, (double)FLOAT_80617af8);
    particle_def_set_color_end((double)FLOAT_80617b50, (double)FLOAT_80617b14, (double)FLOAT_80617b60, (double)FLOAT_80617b34);
    __we639_particle_emitter_create((double)FLOAT_80617afc, (double)FLOAT_80617aec, (double)FLOAT_80617b64, DAT_8061278c + 0x11c, DAT_8061278c + 0x3bc, 0, 0x800000);
    {
        double one = (double)FLOAT_80617ae8;
        particle_emitter_set_color_range(one, one, (double)FLOAT_80617b68, one);
    }

    DAT_80612798 = 0;
    ekun_build_smoke_overlay_emitter();

    // Ambient smoke overlay (0x34e).
    __we639_particle_def_create((double)FLOAT_80617b2c, (double)FLOAT_80617aec, DAT_8061278c + 0x488, 0, 0, 0x11, 0x34e, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617aec, (double)FLOAT_80617ae8, (double)FLOAT_80617ae8, (double)FLOAT_80617b6c, 0x404060ff, 0x606070ff, 0x101010ff);
    particle_def_set_size_start_end((double)FLOAT_80617b70, (double)FLOAT_80617b74, (double)FLOAT_80617b78, (double)FLOAT_80617b78);
    particle_def_set_color_end((double)FLOAT_80617b50, (double)FLOAT_80617b14, (double)FLOAT_80617b58, (double)FLOAT_80617ae8);
    particle_def_set_velocity_ranges((double)FLOAT_80617b7c, (double)FLOAT_80617b80, (double)FLOAT_80617b84, (double)FLOAT_80617b80, (double)FLOAT_80617aec, (double)FLOAT_80617b88);
    __we639_particle_emitter_create((double)FLOAT_80617b78, (double)FLOAT_80617b78, (double)FLOAT_80617b28, DAT_8061278c + 0x1a0, DAT_8061278c + 0x488, 0, 0x801);
    particle_emitter_set_color_range((double)FLOAT_80617aec, (double)FLOAT_80617ae8, (double)FLOAT_80617b8c, (double)FLOAT_80617ae8);

    smem_switch_state_clear();
    FUN_802215a8(7, 0, 2, 2, 2);
}

// ---------------------------------------------------------------------------
// 0x801fdda8  semm_invs_clear_flag
// Clear bit0 of the 'INVS' entity's +0x28 flag word (if present).
// ---------------------------------------------------------------------------
void semm_invs_clear_flag(void)
{
    int ent = FUN_8023c32c(0x494e5653);   // 'INVS'
    if (ent != 0) {
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xfffffffe;
    }
}

// ---------------------------------------------------------------------------
// 0x801fdde4  vsbo_attach_light
// Resolve the entity's submesh light node (+0x2d8 -> +0x28) and attach it to the
// current view actor (DAT_80611d74+0x14).
// ---------------------------------------------------------------------------
void vsbo_attach_light(int entity)
{
    int light = FUN_8025c8e8(*(int *)(*(int *)(entity + 0x2d8) + 0x28));
    FUN_8025caf0(light, 1, *(int *)(DAT_80611d74 + 0x14));
}

// ---------------------------------------------------------------------------
// 0x801fde1c  vsbo_death_shatter
// On the 0x2f "shatter" clip: hide the 0x4e0/0x4e1 submeshes, play the 0x35f break
// sound at the origin, clear render type, randomize the settle timer, seed the
// target position, and reset the +0x1cc accumulator.
// ---------------------------------------------------------------------------
void vsbo_death_shatter(int *entity)
{
    int   sub[5];
    // sound-play param block: matches the ROM frame (local_44[0] = sample id,
    // +0x24/0x28/0x2c = position, +0x38 (local_1c) = a byte flag).
    int   params[16];
    int   ent;
    unsigned int r;

    if (entity[5] == 0x2f) {
        int count = FUN_8023c59c(*(int *)(*(int *)(*entity + 0x2d8) + 4), (int)sub, 5);
        float zero = FLOAT_80617ae8;
        int off = 0;
        while (count > 0) {
            short kind = *(short *)(*(int *)((int)sub + off) + 0x94);
            if ((kind == 0x4e0) || (kind == 0x4e1)) {
                int rec = *(int *)(*(int *)((int)sub + off) + 0x7c);
                *(int *)(rec + 4) = 1;
                *(float *)(rec + 0xc) = zero;
            }
            off = off + 4;
            count = count - 1;
        }

        ent = *entity;
        sound_init_play_params((int)params);
        *(unsigned char *)((char *)params + 0x38) = 1;      // local_1c
        params[0] = 0x35f;                                  // local_44[0] = sample id
        *(int *)((char *)params + 0x24) = *(int *)(ent + 100);   // local_28 = pos.x
        *(int *)((char *)params + 0x28) = *(int *)(ent + 0x68);  // local_24 = pos.y
        *(int *)((char *)params + 0x2c) = *(int *)(ent + 0x6c);  // local_20 = pos.z
        sound_play_request(params);

        entity_set_render_type(*entity, 0);

        r = __we639_rng();
        *(float *)(*entity + 0x1a4) =
            FLOAT_80617b2c * ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80617b98) / FLOAT_80617b90) + FLOAT_80617b2c;

        if ((int *)entity[10] == (int *)0) {
            int e = *entity;
            *(float *)(e + 0xb4) = *(float *)(e + 100) + *(float *)(e + 0x1a8);
            *(float *)(e + 0xb8) = *(float *)(e + 0x68) + *(float *)(e + 0x1ac);
            *(float *)(e + 0xbc) = *(float *)(e + 0x6c) + *(float *)(e + 0x1b0);
        } else {
            *(int *)(*entity + 0xb4) = *(int *)entity[10];
            *(int *)(*entity + 0xb8) = *(int *)(entity[10] + 4);
            *(int *)(*entity + 0xbc) = *(int *)(entity[10] + 8);
        }
        *(float *)(*entity + 0x1cc) = FLOAT_80617ae8;
    }
}

// ---------------------------------------------------------------------------
// 0x801fdfb8  vsbo_settle_tick
// Per-frame settle: at rest set the submeshes to state 3 and clear render type;
// while the settle timer is positive ease it down and spawn a burst of fading
// sparks (rng-jittered) with a fixed last-scale.
// ---------------------------------------------------------------------------
void vsbo_settle_tick(int entity)
{
    int sub[6];

    if ((FLOAT_80617ae8 == *(float *)(entity + 0x1cc)) &&
        (FLOAT_80617ba0 == *(float *)(entity + 0x1a4))) {
        int count = FUN_8023c59c(*(int *)(*(int *)(entity + 0x2d8) + 4), (int)sub, 5);
        int off = 0;
        while (count > 0) {
            short kind = *(short *)(*(int *)((int)sub + off) + 0x94);
            if ((kind == 0x4e0) || (kind == 0x4e1)) {
                *(int *)(*(int *)(*(int *)((int)sub + off) + 0x7c) + 4) = 3;
            }
            off = off + 4;
            count = count - 1;
        }
        entity_set_render_type(entity, 0);
    }

    if (FLOAT_80617ae8 < *(float *)(entity + 0x1a4)) {
        *(float *)(entity + 0x1a4) =
            -((FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) *
              (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - DOUBLE_80617b00) -
              *(float *)(entity + 0x1a4));
        // Six rng-jittered spark seeds (values set into the ROM spill frame consumed
        // by spark_spawn_fading); reduced to the observable calls here.
        __we639_rng(); __we639_rng(); __we639_rng();
        __we639_rng(); __we639_rng(); __we639_rng();
        spark_spawn_fading();
        spark_set_last_scale((double)FLOAT_80617b40);
    }
}

// ---------------------------------------------------------------------------
// 0x801fe418  vsbo_register
// 'VSBO' actor register wrapper -> generic register + attach light.
// ---------------------------------------------------------------------------
void vsbo_register(int param_1, double param_2, double param_3, double param_4, double param_5,
                   double param_6, int param_7, int param_8, int param_9,
                   int param_10, int param_11, int param_12, int param_13,
                   int param_14, int param_15, int param_16)
{
    FUN_80234560(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 0x4f425653, 0x4f420000, param_11, param_12, param_13, param_14, param_15, param_16);  // 'VSBO'/'VBO\0'
    vsbo_attach_light(param_9);
}

// ---------------------------------------------------------------------------
// 0x801fe454  vsbo_ambient_tick
// Per-frame VSBO ambient/open driver: ease the +0x7c state's open factor, sweep the
// leaf along a poly-trig arc, gate on any nearby live actor in the swing corridor,
// run the two-leaf open state machine (raycast the swept leaf; register hit actors
// for update or shatter a struck VSBO; measure travel), and drive the 0x35e swing
// sound.  FP-spill wrapped.
// ---------------------------------------------------------------------------
void vsbo_ambient_tick(void)
{
    int    ctx;
    float *state;
    float  hitPos[3];     // local_74/70/6c
    float  target[3];     // local_80/7c/78
    float  sweep[3];      // local_68/64/60
    float  axis[3];       // local_5c/58/54
    double soundArg;

    ctx   = FUN_803826f0();
    state = *(float **)(ctx + 0x7c);
    axis[0] = DAT_803dfbfc; axis[1] = DAT_803dfc00; axis[2] = DAT_803dfc04;
    soundArg = (double)FLOAT_80617ba0;

    // ease the open factor up toward its ceiling
    if (FLOAT_80617ae8 < *state) {
        *state = -(FLOAT_806108ec *
            (FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) - *state);
        if (*state < FLOAT_80617ae8) *state = FLOAT_80617ae8;
    }

    // corridor gate: if any tail-slot actor sits within the swing wedge, force open
    if (0 < DAT_80612098 - DAT_80612610) {
        double phase = (double)((FLOAT_80617ba8 * *(float *)(ctx + 0x70)) / FLOAT_80617b70);
        double c1 = math_poly_trig(phase - (double)FLOAT_80617bb0);
        double px = (double)(float)((double)FLOAT_80617bac * c1 + (double)*(float *)(ctx + 100));
        double c2 = math_poly_trig(phase);
        double dy = -(double)(float)(px - (double)*(float *)(ctx + 100));
        double dx = (double)(float)((double)(float)((double)FLOAT_80617bac * c2 + (double)*(float *)(ctx + 0x6c)) -
                                    (double)*(float *)(ctx + 0x6c));
        double len = math_sqrt((double)(float)(dx * dx + (double)(float)(dy * dy)));
        if ((double)FLOAT_80617ae8 < len) {
            dx = (double)(float)(dx / len);
            dy = (double)(float)(dy / len);
        }
        int idx = DAT_80612610 << 2;
        int rem = DAT_80612098 - DAT_80612610;
        if (DAT_80612610 < DAT_80612098) {
            do {
                int other = *(int *)((int)&DAT_80588610 + idx);
                double ez = (double)(*(float *)(ctx + 0x6c) - *(float *)(other + 0x6c));
                double ex = (double)(*(float *)(ctx + 100) - *(float *)(other + 100));
                if (((float)(ex * ex + (double)(float)(ez * ez)) <= FLOAT_80617bb4) &&
                    (ABS((float)(dx * ex + (double)(float)(dy * ez))) <= FLOAT_80617b58)) {
                    *state = FLOAT_80617aec;
                    break;
                }
                idx = idx + 4;
                rem = rem - 1;
            } while (rem != 0);
        }
    }

    // two-leaf open state machine (leaf byte-state at state[1], travel at state[2/3])
    if (*state <= FLOAT_80617ae8) {
        if ((state[1] == 1.4013e-45f) || (state[1] == 2.8026e-45f)) {   // 1 or 2 (denormal-encoded)
            state[2] = -((FLOAT_80617aec /
                (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) *
                FLOAT_806108ec - state[2]);
            if (state[2] < FLOAT_80617ae8) {
                state[1] = (state[1] == 1.4013e-45f) ? 2.8026e-45f : 1.4013e-45f;
                state[2] = FLOAT_80617b88;
            }
            state[3] = (FLOAT_80617aec /
                (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) *
                FLOAT_806108ec + state[3];
            if (FLOAT_80617aec < state[3]) state[1] = 4.2039e-45f;   // 3
            *(unsigned int *)(ctx + 0x18) = *(unsigned int *)(ctx + 0x18) | 0x40;
        }

        if ((state[1] != 4.2039e-45f) && (state[1] != 2.8026e-45f)) {
            sweep[0] = *(float *)(ctx + 100)  + *(float *)(ctx + 0x1a8);
            sweep[1] = *(float *)(ctx + 0x68) + *(float *)(ctx + 0x1ac);
            sweep[2] = *(float *)(ctx + 0x6c) + *(float *)(ctx + 0x1b0);
            mtx4_transform_vector((float *)(ctx + 0x274), axis);
            int hit = __we639_scene_raycast(*(int *)(ctx + 0x34), sweep, axis, 0, hitPos,
                                            (float *)0, (int *)0, 0x2000070, 0, (int *)0);
            if ((hit == 0) ||
                ((*(int *)(hit + 0x20) != 8) || (*(int *)(hit + 0x7c) == 0)) ||
                (DAT_80612610 <= *(int *)(*(int *)(hit + 0x7c) + 8))) {
                if ((hit != 0) && (*(short *)(hit + 0x94) == *(short *)(ctx + 0x94))) {
                    entity_register_for_update(hit, 3, 0);
                    entity_register_for_update(hit, 10, 0);
                }
            } else {
                vsbo_attach_light(ctx);
            }
            double travel = math_sqrt((double)((hitPos[2] - sweep[2]) * (hitPos[2] - sweep[2]) +
                                               (hitPos[0] - sweep[0]) * (hitPos[0] - sweep[0]) +
                                               (hitPos[1] - sweep[1]) * (hitPos[1] - sweep[1])));
            *(float *)(ctx + 0xd0) = (float)travel;
            target[0] = (hitPos[0] + sweep[0]) * FLOAT_80617b2c;
            target[1] = (hitPos[1] + sweep[1]) * FLOAT_80617b2c;
            target[2] = (hitPos[2] + sweep[2]) * FLOAT_80617b2c;
            soundArg = (double)FLOAT_80617aec;
        }
    }

    if ((DAT_805015c0 & 0x20000000) == 0) {
        int stepped = scalar_step_toward_snap((double)state[5], (double)FLOAT_80617b28, state + 4);
        if (stepped != 0) {
            unsigned int r = __we639_rng();
            state[5] = FLOAT_80617b78 *
                ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
        }
        int started = (int)0;
        __we638_sound_update_3d_source(soundArg, (double)FLOAT_80617bb8,
                                       (double)(FLOAT_80617aec + state[4]),
                                       ctx + 0x22c, target, 0x35e, 1);
        (void)started;
        // (the ROM ORs +0x18 |= 0x40 when the sound just started; the reduced alias
        //  returns void so the flag set is folded into the open path above.)
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801fe954  vsbo_spark_render
// Additive spark billboard pass for the open VSBO leaf.
// ---------------------------------------------------------------------------
void vsbo_spark_render(int entity)
{
    float end[3];    // local_28/24/20
    float start[3];  // local_1c/18/14

    start[0] = DAT_803dfc08; start[1] = DAT_803dfc0c; start[2] = DAT_803dfc10;

    if ((**(float **)(entity + 0x7c) <= FLOAT_80617ae8) &&
        ((*(float **)(entity + 0x7c))[1] != 4.2039e-45f) &&
        ((*(float **)(entity + 0x7c))[1] != 2.8026e-45f)) {
        end[0] = *(float *)(entity + 100)  + *(float *)(entity + 0x1a8);
        end[1] = *(float *)(entity + 0x68) + *(float *)(entity + 0x1ac);
        end[2] = *(float *)(entity + 0x6c) + *(float *)(entity + 0x1b0);
        mtx4_transform_vector((float *)(entity + 0x274), start);
        float travel = *(float *)(entity + 0xd0);
        start[0] = start[0] * travel + end[0];
        start[1] = start[1] * travel + end[1];
        start[2] = start[2] * travel + end[2];
        gx_set_color_update_mode(0);
        mtx4_transpose(*(float **)(DAT_80611d54 + 0x208), (float *)&DAT_8058e3cc);
        DAT_80612250 = 1;
        DAT_80612258 = 0;
        gx_set_sub_material_id(2);
        gx_set_blend_mode(2);
        int tex = sprite_get_texture_id(0x33b);
        ekun_spark_billboard_render(end, start, 0x7f00003f, tex);
    }
}

// ===========================================================================
// batch_640 continuation (address range 0x801feab0 - 0x801ff240)
//
// The SEMM set-piece's "monitor screen" spectator-cam subsystem plus more of the
// 'VSBO' steam-vent / debris prop behaviours. subsystem:game (the monitor-cam +
// render-target helpers are subsystem:graphics). Appended to worm_smem_state.cpp:
// this is the direct address-order continuation and shares the SEMM 0x580 state
// block (DAT_8061278c) and the two scratch render-target slots (DAT_80612790..).
//
//   vsbo_vent_hiss_start   (0x801feab0) play the 0x360 vent-hiss sound once at the
//                          prop origin, latch +0x1a0 = 1.
//   vsbo_gimbal_reset      (0x801feb24) reset the prop's two gimbal matrices to
//                          identity and rotate-about-point the other two by a fixed
//                          angle vector around the node's two pivot points; set the
//                          +0x28 render flag 0x200000.
//   vsbo_spark_burst_tick  (0x801fec1c) countdown-driven spark burst: while the
//                          +0xd0 timer is under the ceiling ramp it; otherwise ease
//                          the +0x1a4 emitter timer and, each tick, spawn a pair of
//                          sine-arc sparks from the SEMM spark/debris emitters and
//                          set their lifetime by the remaining count; after 10 ticks
//                          fire a 0x2a explosion and reset.
//   semm_monitor_rt_alloc  (0x801fee70) allocate the two 0x80x0x80 scratch render
//                          targets (DAT_80612790..) + their readback surfaces.
//   semm_monitor_rt_clear  (0x801feed4) GX fill-rect clear one 0x80x0x80 monitor RT
//                          (the spectator-cam user-data callback).
//   semm_monitor_cam_tick  (0x801fef38) per-frame monitor-cam driver: rebuilds the
//                          collision strip table when the world frame stamp changes;
//                          if no player is in the 0x2000 zone, restore the normal
//                          spectator viewport; otherwise bind the current monitor RT
//                          as the spectator viewport, aim the cam at the matching
//                          'FR0x' camera entity, install semm_monitor_rt_clear as the
//                          user-data callback, and advance the 0-1 monitor index;
//                          then copy the two RT slot ids into the strip table.
//   semm_monitor_restore   (0x801ff0c4) restore-render-callback thunk (FUN_803738d0).
//   vsbo_debris_spawn_stack(0x801ff0e8) spawn three stacked 0x4f7 debris entities,
//                          each dropped to the floor plane (0x80617bcc) with the
//                          per-entity +0x1ac/+0x1c4 offsets; the third gets render
//                          type 0xd5.
//   vsbo_slide_sound_tick  (0x801ff1d4) step the +0xd0 slide factor toward its snap
//                          target and drive the 0xbfc 3D slide sound at the origin.
//   vsbo_fall_tick         (0x801ff240) integrate the prop's downward velocity
//                          (+0x68) under gravity; on reaching the floor plane spawn a
//                          landing-dust burst from the SEMM steam emitter, clamp to
//                          rest, and register for update.
//
// Non-matching: PPC FP spill frames (FUN_803826f0 open / FUN_8038273c close) opaque;
// int<->double 0x43300000 bias math is plain float math; the 0x800-entry sine LUT
// DAT_80583228 is indexed &0x7ff / +0x200 quarter-phase; the wide FP-ABI helpers
// (render_target_alloc_scratch/_readback, entity_spawn_by_type, sound_update_3d_source)
// are routed through file-local typed aliases matching the ROM call shape.
// ===========================================================================

// --- batch_640 raw data symbols (file-local) ---
extern int   DAT_80612790;        // scratch render-target slot base (2 slots)
extern int   DAT_80612788;        // cached world frame stamp
extern int   DAT_80611928;        // live world frame stamp
extern int   DAT_80402700;        // spectator mode-data block
extern int   DAT_80402704;        // spectator flag word
extern float DAT_803dfc14; extern float DAT_803dfc18; extern float DAT_803dfc1c;  // gimbal angle vector
extern float DAT_803dfc20; extern float DAT_803dfc24; extern float DAT_803dfc28;  // spark base dir
extern float DAT_803dfc2c; extern float DAT_803dfc30; extern float DAT_803dfc34;  // explosion normal
extern float DAT_803dfc38; extern float DAT_803dfc3c; extern float DAT_803dfc40;  // debris spawn pos
extern float DAT_803dfc44; extern float DAT_803dfc48; extern float DAT_803dfc4c;  // fall dust dir
extern int   DAT_80583228;        // 0x800-entry sine LUT base

extern float FLOAT_80617bc4;      // spark burst ramp ceiling
extern float FLOAT_80617bc8;      // spark arc radius
extern float FLOAT_80617bcc;      // floor plane Y
extern float FLOAT_80617bd0;      // debris stack step 0
extern float FLOAT_80617bd4;      // debris stack step 1

// render_target_alloc_scratch returns the allocated RT handle (decls form is void).
typedef int (*RtScratchFn)(unsigned int, unsigned int, int *);
static const RtScratchFn __we640_rt_alloc_scratch = (RtScratchFn)&render_target_alloc_scratch;
// render_target_alloc_readback ROM form takes the FP context + slot (decls form is
// (int slot)).
static void (*const __we640_rt_alloc_readback)(unsigned long long, double, double, double,
                                               double, double, int, int, int) =
    (void (*)(unsigned long long, double, double, double, double, double, int, int, int))
    &render_target_alloc_readback;
// entity_spawn_by_type ROM form: FP context + type + &pos + &scratch + 2 ints,
// returns the spawned entity (decls form is void (int,float*,int,int)).
static int (*const __we640_entity_spawn_by_type)(unsigned long long, double, double,
                                                 unsigned long long, unsigned long long,
                                                 unsigned long long, unsigned long long,
                                                 unsigned long long, int, void *, void *, int, int) =
    (int (*)(unsigned long long, double, double, unsigned long long, unsigned long long,
             unsigned long long, unsigned long long, unsigned long long, int, void *, void *,
             int, int))&entity_spawn_by_type;
// mtx4_rotate_about_point ROM form takes (mtxOut, anglesVec, pivotPoint) (decls
// form is (float*, double)).
typedef void (*RotAboutFn)(int, float *, float *);
static const RotAboutFn __we640_mtx4_rotate_about_point = (RotAboutFn)&mtx4_rotate_about_point;
// sound_update_3d_source returns a start flag (decls form is void); reuse the same
// int-returning shape as batch_638/639 but with an int position arg.
typedef int (*Sound3dIntFn)(double, double, double, int, int, int, int);
static const Sound3dIntFn __we640_sound_update_3d_source = (Sound3dIntFn)&sound_update_3d_source;

// helpers not in the shared header (file-local)
extern int  FUN_803738d0(void);            // restore render callback
extern void FUN_80374e68(void);            // reset normal spectator path
extern int  collision_alloc_strip_table(int *entity);

// ---------------------------------------------------------------------------
// 0x801feab0  vsbo_vent_hiss_start
// Play the 0x360 vent-hiss sound once at the prop origin; latch +0x1a0 = 1.
// ---------------------------------------------------------------------------
void vsbo_vent_hiss_start(int entity)
{
    int params[16];
    if (*(int *)(entity + 0x1a0) == 0) {
        sound_init_play_params((int)params);
        *(unsigned char *)((char *)params + 0x38) = 1;      // local_10
        params[0] = 0x360;
        *(int *)((char *)params + 0x24) = *(int *)(entity + 100);   // pos.x
        *(int *)((char *)params + 0x28) = *(int *)(entity + 0x68);  // pos.y
        *(int *)((char *)params + 0x2c) = *(int *)(entity + 0x6c);  // pos.z
        sound_play_request(params);
        *(int *)(entity + 0x1a0) = 1;
    }
}

// ---------------------------------------------------------------------------
// 0x801feb24  vsbo_gimbal_reset
// Reset the prop's two gimbal matrices to identity and rotate-about-point the other
// two by the fixed angle vector around the node's two pivot points; flag 0x200000.
// ---------------------------------------------------------------------------
void vsbo_gimbal_reset(int entity)
{
    int  *node;
    float angles[3];

    angles[0] = DAT_803dfc14;
    angles[1] = DAT_803dfc18;
    angles[2] = DAT_803dfc1c;

    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10));
    mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10) + 0x40);

    node = *(int **)(*(int *)(entity + 4) + 4);
    __we640_mtx4_rotate_about_point(*(int *)(*(int *)(entity + 4) + 0x10) + 0x80, angles,
                                    (float *)node[*node * -0x30 + 0x7b]);
    node = *(int **)(*(int *)(entity + 4) + 4);
    __we640_mtx4_rotate_about_point(*(int *)(*(int *)(entity + 4) + 0x10) + 0xc0, angles,
                                    (float *)node[*node * -0x30 + 0xab]);
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200000;
}

// ---------------------------------------------------------------------------
// 0x801fec1c  vsbo_spark_burst_tick
// Countdown-driven spark burst from the SEMM spark/debris emitters, capped by a 10-
// tick counter that ends in a 0x2a explosion.
// ---------------------------------------------------------------------------
void vsbo_spark_burst_tick(int entity)
{
    double bias = DOUBLE_80617b00;
    float  zero = FLOAT_80617ae8;
    float  dir[3];      // local_30/2c/28
    float  spawnPos[3]; // local_3c and the two arc offsets share the frame
    float  expNormal[3];// local_48/44/40

    dir[0] = DAT_803dfc20;
    dir[1] = DAT_803dfc24;
    dir[2] = DAT_803dfc28;

    if (*(int *)(entity + 0x1a0) != 0) {
        if (*(float *)(entity + 0xd0) <= FLOAT_80617bc4) {
            *(float *)(entity + 0xd0) =
                FLOAT_80617b74 *
                (FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) *
                FLOAT_806108ec + *(float *)(entity + 0xd0);
        } else {
            *(float *)(entity + 0x1a4) =
                -((FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) *
                  FLOAT_806108ec - *(float *)(entity + 0x1a4));
            if (*(float *)(entity + 0x1a4) < zero) {
                dir[2] = zero * FLOAT_80617bc8 + dir[2];
                spawnPos[0] = zero;   // local_3c (arc scratch)
                unsigned int idx = (unsigned int)(FLOAT_80617ba4 *
                    ((FLOAT_80617b1c * (float)((double)CONCAT44(0x43300000, *(unsigned int *)(entity + 0x1a0) ^ 0x80000000) - bias)) / FLOAT_80617bb8) + FLOAT_80617b2c);
                float arcC = (float)(&DAT_80583228)[idx & 0x7ff];
                float arcS = *(float *)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc));
                dir[0] = arcC * FLOAT_80617bc8 + dir[0];
                dir[1] = arcS * FLOAT_80617bc8 + dir[1];
                *(float *)(entity + 0x1a4) = FLOAT_80617b18;
                particle_emitter_spawn(0, DAT_8061278c + 0x11c, 1, dir, spawnPos);
                particle_emitter_spawn(0, DAT_8061278c + 0x98, 1, dir, spawnPos);
                particle_emitter_set_lifetime(
                    (double)(FLOAT_80617b18 *
                        (float)((double)CONCAT44(0x43300000, (10U - *(int *)(entity + 0x1a0)) ^ 0x80000000) - DOUBLE_80617b00) +
                        FLOAT_80617b2c));
                *(int *)(entity + 0x1a0) = *(int *)(entity + 0x1a0) + 1;
            }
            if (9 < *(int *)(entity + 0x1a0)) {
                expNormal[0] = DAT_803dfc2c;
                expNormal[1] = DAT_803dfc30;
                expNormal[2] = DAT_803dfc34;
                player_ai_explosion_spawn(0x2a, 4, dir, expNormal, 0, 0, 1);
                *(int *)(entity + 0x1a0) = 0;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801fee70  semm_monitor_rt_alloc
// Allocate the two 0x80x0x80 scratch render targets (DAT_80612790..) + readback.
// ---------------------------------------------------------------------------
void semm_monitor_rt_alloc(void)
{
    int off = 0;
    FUN_803826f0();
    for (int i = 0; i < 2; i = i + 1) {
        int rt = __we640_rt_alloc_scratch(0x80, 0x80, (int *)(&DAT_80612790 + off));
        __we640_rt_alloc_readback((unsigned long long)rt, 0, 0, 0, 0, 0, 0, 0,
                                  *(int *)(&DAT_80612790 + off));
        off = off + 4;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801feed4  semm_monitor_rt_clear
// GX fill-rect clear one 0x80x0x80 monitor RT (spectator-cam user-data callback).
// ---------------------------------------------------------------------------
void semm_monitor_rt_clear(void)
{
    gx_set_color_update_mode(0);
    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);
    gx_fill_rect_clipped(0, 0, 0x80, 0x80, 0x7f);
    gx_set_color_update_mode(0);
}

// ---------------------------------------------------------------------------
// 0x801fef38  semm_monitor_cam_tick
// Per-frame monitor-cam driver.
// ---------------------------------------------------------------------------
void semm_monitor_cam_tick(void)
{
    int ctx = FUN_803826f0();
    unsigned int inZone = *(unsigned int *)(ctx + 0x10) & 0x80000000;
    int strip = collision_alloc_strip_table(*(int **)(ctx + 4));

    if (DAT_80612788 != DAT_80611928) {
        DAT_80612788 = DAT_80611928;

        int off = 0, rem = DAT_80612610;
        if (0 < DAT_80612610) {
            do {
                if (*(int *)(DAT_80611d78 + off + 0x114) == 0x2000) {
                    inZone = 0;
                }
                off = off + 0x141c;
                rem = rem - 1;
            } while (rem != 0);
        }

        if (inZone == 0) {
            FUN_80374e68();
            spectator_set_viewport(spectator_get_render_callback());
        } else {
            spectator_set_viewport(*(int *)(&DAT_80612790 + *(int *)(DAT_8061278c + 0x55c) * 4));
            DAT_80402704 = DAT_80402704 | 0xa00;
            int cam = FUN_8023c32c(*(int *)(DAT_8061278c + 0x55c) + 0x52463031);   // 'FR0x'
            spectator_set_mode(
                (double)(FLOAT_806108ec *
                    (FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00))),
                (int)&DAT_80402700, cam);
            spectator_set_user_data((int)&semm_monitor_rt_clear);
            // advance the monitor index in [0,1] (branchless abs-mod-2)
            unsigned int n = *(int *)(DAT_8061278c + 0x55c) + 1;
            int s = (int)n >> 0x1f;
            *(unsigned int *)(DAT_8061278c + 0x55c) = ((n & 1) ^ -s) + s;
        }

        // copy the two RT slot ids into the strip table (+0x60, stride 0x10)
        int srcOff = 0, dstOff = 0x60;
        for (int i = 2; i != 0; i = i - 1) {
            *(int *)(strip + dstOff) = *(int *)(&DAT_80612790 + srcOff);
            dstOff = dstOff + 0x10;
            srcOff = srcOff + 4;
        }
        FUN_803738d0();
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x801ff0c4  semm_monitor_restore
// Restore-render-callback thunk.
// ---------------------------------------------------------------------------
void semm_monitor_restore(void)
{
    FUN_803738d0();
}

// ---------------------------------------------------------------------------
// 0x801ff0e8  vsbo_debris_spawn_stack
// Spawn three stacked 0x4f7 debris entities, each dropped to the floor plane with
// the per-entity +0x1ac/+0x1c4 offsets; the third gets render type 0xd5.
// ---------------------------------------------------------------------------
void vsbo_debris_spawn_stack(unsigned long long param_1, double param_2, double param_3,
                             unsigned long long param_4, unsigned long long param_5,
                             unsigned long long param_6, unsigned long long param_7,
                             unsigned long long param_8)
{
    float pos[3];        // local_18/14/10 (+ afStack_c scratch)
    float scratch[3];
    pos[0] = DAT_803dfc38;
    pos[1] = DAT_803dfc3c;
    pos[2] = DAT_803dfc40;

    int e = __we640_entity_spawn_by_type(param_1, param_2, param_3, param_4, param_5, param_6,
                                         param_7, param_8, 0x4f7, pos, scratch, 0, 0);
    double drop = (double)(FLOAT_80617bcc - *(float *)(e + 0x1ac));
    *(float *)(e + 0x68) = (float)(drop + (double)*(float *)(e + 0x1c4));

    pos[2] = (float)((double)pos[2] - (double)FLOAT_80617bd0);
    e = __we640_entity_spawn_by_type((unsigned long long)(double)pos[2], drop, param_3, param_4,
                                     param_5, param_6, param_7, param_8, 0x4f7, pos, scratch, 0, 0);
    pos[2] = FLOAT_80617bd4;
    double drop2 = (double)(FLOAT_80617bcc - *(float *)(e + 0x1ac));
    float y = (float)(drop2 + (double)*(float *)(e + 0x1c4));
    *(float *)(e + 0x68) = y;

    e = __we640_entity_spawn_by_type((unsigned long long)(double)y, drop2, param_3, param_4,
                                     param_5, param_6, param_7, param_8, 0x4f7, pos, scratch, 0, 0);
    *(float *)(e + 0x68) = (FLOAT_80617bcc - *(float *)(e + 0x1ac)) + *(float *)(e + 0x1c4);
    *(int *)(e + 0x34) = 0xd5;
}

// ---------------------------------------------------------------------------
// 0x801ff1d4  vsbo_slide_sound_tick
// Step the +0xd0 slide factor toward its snap target and drive the 0xbfc 3D slide
// sound at the origin.
// ---------------------------------------------------------------------------
void vsbo_slide_sound_tick(int entity)
{
    scalar_step_toward_snap((double)FLOAT_80617aec, (double)FLOAT_80617b78, (float *)(entity + 0xd0));
    int started = __we640_sound_update_3d_source((double)*(float *)(entity + 0xd0),
                                                 (double)FLOAT_80617bb8, (double)FLOAT_80617aec,
                                                 entity + 0x22c, entity + 100, 0xbfc, 0);
    if (started != 0) {
        *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x40;
    }
}

// ---------------------------------------------------------------------------
// 0x801ff240  vsbo_fall_tick
// Integrate the prop's downward velocity (+0x68) under gravity; on reaching the
// floor plane spawn a landing-dust burst from the SEMM steam emitter, clamp to rest,
// and register for update.
// ---------------------------------------------------------------------------
void vsbo_fall_tick(int entity)
{
    float floorY = FLOAT_80617bcc;
    float dir[3];       // local_28/24/20
    float pos[3];       // local_1c/18/14

    *(float *)(entity + 0x68) =
        -(FLOAT_80617b78 *
          (FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) *
          FLOAT_806108ec - *(float *)(entity + 0x68));

    float newY = *(float *)(entity + 0x68) + *(float *)(entity + 0x1ac);
    if (*(float *)(entity + 0x1c4) + newY < floorY) {
        dir[0] = DAT_803dfc44;
        dir[1] = DAT_803dfc48;
        dir[2] = DAT_803dfc4c;
        pos[0] = *(float *)(entity + 100)  + *(float *)(entity + 0x1a8);
        pos[2] = *(float *)(entity + 0x6c) + *(float *)(entity + 0x1b0);
        *(float *)(entity + 0x68) = (floorY - *(float *)(entity + 0x1ac)) - *(float *)(entity + 0x1c4);
        particle_emitter_spawn(0, DAT_8061278c + 0x14, 0, pos, dir);
        entity_register_for_update(entity, 3, -0x7fe00e2c);
        *(float *)(entity + 0xd0) = FLOAT_80617ae8;
    }
}

// ===========================================================================
// batch_641 continuation (address range 0x801ff350 - 0x80200554)
//
// The 'VSBO' prop's spark-fountain / debris-fracture subsystem plus the SEMM
// monitor-screen scanline renderer. subsystem:game (the scanline draw is
// subsystem:graphics). Appended to worm_smem_state.cpp: shares the SEMM state
// block (DAT_8061278c) and the VSBO emitter pool.
//
//   vsbo_fountain_scale_sample (0x801ff350) sample callback for the spark-fountain
//                          emitter scale: while the source prop is dead/settled push
//                          the fountain toward zero and flag it done; else clamp the
//                          per-axis scale to the source's remaining fill and copy the
//                          round-robin (index % 0x14) spark slot into the out vec;
//                          FUN_8037dc78 gates the Y component to zero.
//   vsbo_fountain_tick     (0x801ff470) per-frame spark-fountain: decay +0x1cc; when
//                          spent, (re)seed the 0x14 spark slots with rng velocities in
//                          the local matrix frame, spawn the fracture body
//                          (subobj_fracture_spawn_default) + the break-visual, mark the
//                          node broken, and on first break spawn the debris stack +
//                          arm alert group 0x40.
//   vsbo_fountain_particle_tick (0x801ff738) integrate the 0x14 spark slots under
//                          gravity, respawning spent ones with fresh rng velocity in
//                          the local frame, and drive the 0xbfb 3D fountain sound.
//   vsbo_fragment_break_tick (0x801ffb3c) per-fragment break tick: skip 'MDON' nodes
//                          and class 0x507; decay +0x1cc; on expiry re-seed the body
//                          (mark rigid, randomize the +0x1a4 lifetime, register, alloc
//                          + descriptor + extra-word spark callbacks) then run the
//                          break-tick impl.
//   vsbo_monitor_scanline_render (0x801ffd14) SEMM monitor-screen scanline pass:
//                          binds the strip table to RT0, ramps the +0xc0 warp factor,
//                          and (for the 'INMO' variant while alert 0x40 is set) emits a
//                          periodic damage spray from the prop.  (graphics)
//   semm_scanline_draw     (0x80200084) draw the monitor-screen scanline overlay into
//                          the 0x40x0x40 RT for a live 'FR06'/'FR07' cam: GX fill +
//                          textured sprite-rects (tex 0x333) stepped down the screen
//                          with a jitter/interference pattern keyed off the frame time.
//                          (graphics)
//   vsbo_fragment_cache_tick (0x80200360) per-frame update of the cached fragment
//                          particle: transform its local offset + velocity into world
//                          space via the subobject matrix and push position/velocity/
//                          drag into the cached emitter.
//   vsbo_fragment_emit_start (0x8020048c) alloc the cached fragment emitter (0x1a0),
//                          spawn it at the prop origin, set its size range + lifetime,
//                          and register for update.
//   vsbo_fragment_break_tick_thunk (0x80200530) thin wrapper -> subobj_break_tick_impl.
//   vsbo_fragment_spawn    (0x80200554) spawn the fragment subobject: register the
//                          entity, alloc the body + descriptor (mode 0xe), and fracture
//                          it (2 islands) with the optional split plane, then install
//                          the break-tick thunk as the render type.
//
// Non-matching: 4CC ids (MDON/INMO/FR06/FR07); PPC FP spill frames + GPR save/restore
// (__save_gpr_25/FUN_803826d4/e4/f0 open, FUN_80382720/30/34/8038273c close) opaque;
// int<->double 0x43300000 bias math is plain float math; wide FP-ABI helpers
// (subobj_build_descriptor_default/fracture_spawn/_default, damage_dispatch_to_entity,
// entity_set_render_type return, subobj_pool_alloc/redirty) routed through file-local
// typed aliases matching the ROM call shape.
// ===========================================================================

// --- batch_641 raw data symbols (file-local) ---
extern int   DAT_8045600c;        // scanline interference LUT (5 entries)
extern float DAT_803dfc50; extern float DAT_803dfc54; extern float DAT_803dfc58;  // fountain seed vel A
extern float DAT_803dfc5c; extern float DAT_803dfc60; extern float DAT_803dfc64;  // fountain seed vel B
extern float DAT_803dfc68; extern float DAT_803dfc6c; extern float DAT_803dfc70;  // damage spray dir
extern float DAT_803dfc74; extern float DAT_803dfc78; extern float DAT_803dfc7c;  // damage spray normal
extern float DAT_803dfc80; extern float DAT_803dfc84; extern float DAT_803dfc88;  // cache frag vel
extern float DAT_803dfc8c; extern float DAT_803dfc90; extern float DAT_803dfc94;  // frag emit dir

extern float FLOAT_80617bd8;      // fracture spawn scalar
extern float FLOAT_80617bdc;      // "done" sentinel value
extern float FLOAT_80617be0;      // scanline warp rate
extern float FLOAT_80617be4;      // scanline warp ceiling
extern float FLOAT_80617be8;      // scanline row-phase scale
extern float FLOAT_80617bec;      // frag emitter size min

// subobj_pool_alloc ROM returns the new body handle (declared void).
static unsigned int (*const __we641_subobj_pool_alloc)(void) =
    (unsigned int (*)(void))&subobj_pool_alloc;
// subobj_redirty_in_aabb_wrapper ROM returns a value (declared void).
static int (*const __we641_subobj_redirty)(void) =
    (int (*)(void))&subobj_redirty_in_aabb_wrapper;
// subobj_build_descriptor_default ROM full ABI: fpctx + 7 doubles + (handle, entity,
// mode, lod, scale) (decls form is (uint,int,float)).
static int (*const __we641_subobj_build_descriptor_default)(double, double, double, double,
                                                            double, double, double,
                                                            unsigned int, int, int, int, float) =
    (int (*)(double, double, double, double, double, double, double,
             unsigned int, int, int, int, float))&subobj_build_descriptor_default;
// entity_set_render_type ROM leaves the fp-context in the return regs (decls form is
// void); the 2nd arg is a render-type id OR a callback pointer.
static unsigned long long (*const __we641_entity_set_render_type)(int, int) =
    (unsigned long long (*)(int, int))&entity_set_render_type;
// damage_dispatch_to_entity ROM full ABI: fpctx + 7 doubles + event block (decls
// form is (int*)).
static void (*const __we641_damage_dispatch_to_entity)(double, double, double, double,
                                                       double, double, double, double, int *) =
    (void (*)(double, double, double, double, double, double, double, double, int *))
    &damage_dispatch_to_entity;
// block_alloc_dispatch returns the allocated block (permissive-int extern in
// generated_externs); typed here for the pointer store.
typedef void *(*BlockAllocFn)(int, int, int, int);
static const BlockAllocFn __we641_block_alloc_dispatch = (BlockAllocFn)&block_alloc_dispatch;

// helpers not in the shared header (file-local)
extern int   __save_gpr_25(void);          // GPR spill frame (returns entity)
extern void  __restore_gpr_25(void);
extern int   FUN_803826e4(void);           // FP spill frame open (returns ptr)
extern void  FUN_80382730(void);           // FP spill frame close
extern int   FUN_803826d4(void);           // FP spill frame open (returns ptr)
extern void  FUN_80382720(void);           // FP spill frame close
extern unsigned int FUN_8037dc78(int entity);   // fountain Y-gate predicate
extern short FUN_8037daf0(unsigned long long a, double b, double c, double d, double e,
                          double f, double g, unsigned long long h, int i, int j,
                          int k, int l, float m, float n, int o, int p);  // break-visual spawn
extern void  FUN_8037dc94(double a, double b, double c, int d, int e, void *f, void *g,
                          void *h, int i, char j, int k);  // break-visual finalize
// debris_fragment_spark_tick_cb / debris_fragment_emit_spark resolve through the
// shared header (declared in generated_decls).

// ---------------------------------------------------------------------------
// 0x801ff350  vsbo_fountain_scale_sample
// Spark-fountain scale sample callback.
// ---------------------------------------------------------------------------
void vsbo_fountain_scale_sample(int emitter, unsigned int *outVec)
{
    int src  = *(int *)(emitter + 0x88);
    int slots = *(int *)(src + 0x7c);

    if (((*(unsigned int *)(src + 0x28) & 0x80000000) != 0) ||
        (*(float *)(src + 0x1a4) < FLOAT_80617ae8)) {
        *(float *)(emitter + 0x68) = FLOAT_80617ae8;
        *(float *)(emitter + 0x74) = FLOAT_80617ae8;
        *(unsigned int *)(emitter + 4) = *(unsigned int *)(emitter + 4) | 0x800001;
    } else {
        float clamped = FLOAT_80617b58 * *(float *)(src + 0x1a4);
        if (clamped < *(float *)(emitter + 0x74)) *(float *)(emitter + 0x74) = clamped;
        clamped = FLOAT_80617b58 * *(float *)(src + 0x1a4);
        if (clamped < *(float *)(emitter + 0x68)) *(float *)(emitter + 0x68) = clamped;
    }

    *(int *)(src + 0x1a0) = *(int *)(src + 0x1a0) + 1;
    int slot = (*(int *)(src + 0x1a0) % 0x14) * 0x18;
    outVec[0] = *(unsigned int *)(slots + slot);
    outVec[1] = *(unsigned int *)(slots + slot + 4);
    outVec[2] = *(unsigned int *)(slots + slot + 8);

    if (FUN_8037dc78(emitter) != 0) {
        *(float *)&outVec[1] = FLOAT_80617ae8;
    }
}

// ---------------------------------------------------------------------------
// 0x801ff470  vsbo_fountain_tick
// Per-frame spark-fountain driver + fracture spawn on expiry.
// ---------------------------------------------------------------------------
void vsbo_fountain_tick(void)
{
    int *frame = (int *)FUN_803826e4();
    int  ent   = *frame;

    *(float *)(ent + 0x1cc) = *(float *)(ent + 0x1cc) - (float)frame[8];
    if (*(float *)(ent + 0x1cc) <= FLOAT_80617ae8) {
        int   slots = *(int *)(ent + 0x7c);
        float base[3];
        float mtx[16];

        base[0] = DAT_803dfc50; base[1] = DAT_803dfc54; base[2] = DAT_803dfc58;
        entity_build_local_matrix(mtx, (void *)(intptr_t)ent, 0, 0);
        mtx4_transform_vector(mtx, base);
        base[0] = base[0] + *(float *)(ent + 100);
        base[1] = base[1] + *(float *)(ent + 0x68);
        base[2] = base[2] + *(float *)(ent + 0x6c);

        int off = 0;
        for (int i = 0; i < 0x14; i = i + 1) {
            float *slot = (float *)(slots + off);
            slot[3] = (float)((double)FLOAT_80617b58 *
                (double)(float)((double)(float)(((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / (double)FLOAT_80617b90) - (double)FLOAT_80617b2c));
            slot[4] = (float)((double)FLOAT_80617b58 *
                (double)(float)((double)(float)(((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / (double)FLOAT_80617b90) - (double)FLOAT_80617b2c));
            slot[5] = (float)((double)FLOAT_80617b58 *
                (double)(float)((double)(float)(((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / (double)FLOAT_80617b90) - (double)FLOAT_80617b2c));
            slot[4] = (float)((double)FLOAT_80617b38 *
                (double)(float)((double)FLOAT_80617b2c + (double)(float)(((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / (double)FLOAT_80617b90)));
            mtx4_transform_vector(mtx, slot + 3);
            slot[0] = base[0];
            slot[1] = base[1];
            slot[2] = base[2];
            off = off + 0x18;
        }

        short vis = FUN_8037daf0((unsigned long long)0, 0, 0, 0, 0, 0, 0, (unsigned long long)0,
                                 -0x7fe00cb0, 1, 0, 0, base[1], base[0], 0, 0);
        FUN_8037dc94((double)FLOAT_80617bb8, (double)FLOAT_80617b6c, (double)FLOAT_80617b2c,
                     0, 3, base, base, (void *)0, 0x1000, (char)vis, *frame);
        *(unsigned int *)(*(int *)(*frame + 4) + 0x30) = *(unsigned int *)(*(int *)(*frame + 4) + 0x30) & 0xfffffffe;
        *(unsigned int *)(*(int *)(*frame + 4) + 0x30) = *(unsigned int *)(*(int *)(*frame + 4) + 0x30) | 8;
        subobj_fracture_spawn_default((double)FLOAT_80617bd8, (double)FLOAT_80617b6c, (double)FLOAT_80617b2c,
                                      0, 0, 0, 0, (unsigned long long)0, *frame, 4, 2, 0, (float *)0, base);
        entity_set_render_type(*frame, 0);
        if (DAT_80612798 == 0) {
            vsbo_debris_spawn_stack((unsigned long long)0, 0, 0, 0, 0, 0, 0, (unsigned long long)0);
            aiactor_alert_group_arm(0x40);
            DAT_80612798 = 1;
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x801ff738  vsbo_fountain_particle_tick
// Integrate the 0x14 spark slots under gravity, respawning spent slots with fresh
// rng velocity in the local frame, and drive the 0xbfb 3D fountain sound.
// ---------------------------------------------------------------------------
void vsbo_fountain_particle_tick(void)
{
    int entity = __save_gpr_25();
    if (*(float *)(entity + 0x1cc) <= FLOAT_80617ae8) {
        float *slots = *(float **)(entity + 0x7c);
        float  base[3];
        float  mtx[17];
        bool   forceAll = (FLOAT_80617bdc == *slots);

        base[0] = DAT_803dfc5c; base[1] = DAT_803dfc60; base[2] = DAT_803dfc64;
        entity_build_local_matrix(mtx, (void *)(intptr_t)entity, 0, 0);
        mtx4_transform_vector(mtx, base);
        base[0] = base[0] + *(float *)(entity + 100);
        base[1] = base[1] + *(float *)(entity + 0x68);
        base[2] = base[2] + *(float *)(entity + 0x6c);

        int off = 0;
        for (int i = 0; i < 0x14; i = i + 1) {
            float *slot = (float *)((int)slots + off);
            if (((double)slot[1] < (double)FLOAT_80617ae8) || forceAll) {
                slot[4] = FLOAT_80617b2c * ABS(slot[4]);
                slot[1] = FLOAT_80617ae8;
                if ((slot[4] < FLOAT_80617b58) || forceAll) {
                    slot[3] = FLOAT_80617b58 * ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
                    slot[4] = FLOAT_80617b58 * ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
                    slot[5] = FLOAT_80617b58 * ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
                    slot[4] = FLOAT_80617b38 * (FLOAT_80617b2c + (float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90);
                    mtx4_transform_vector(mtx, slot + 3);
                    slot[0] = base[0];
                    slot[1] = base[1];
                    slot[2] = base[2];
                }
            }
            // integrate under gravity by the frame delta
            float dt = (FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00));
            slot[4] = -((float)((double)FLOAT_80617b30 * (double)dt) * FLOAT_806108ec - slot[4]);
            slot[0] = slot[3] * dt * FLOAT_806108ec + slot[0];
            slot[1] = slot[4] * dt * FLOAT_806108ec + slot[1];
            slot[2] = slot[5] * dt * FLOAT_806108ec + slot[2];
            off = off + 0x18;
        }

        // ramp the +0x1a4 fountain sound level, clamp [0,1]
        *(float *)(entity + 0x1a4) =
            -((FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) * FLOAT_806108ec - *(float *)(entity + 0x1a4));
        float lvl = *(float *)(entity + 0x1a4);
        float clamped = (FLOAT_80617aec < lvl) ? FLOAT_80617aec : lvl;
        float out = (FLOAT_80617ae8 <= clamped) ? ((FLOAT_80617aec < lvl) ? FLOAT_80617aec : lvl) : FLOAT_80617ae8;
        float *pos = entity_get_world_position(entity);
        int started = __we640_sound_update_3d_source((double)out, (double)FLOAT_80617bb8, (double)FLOAT_80617aec,
                                                     entity + 0x22c, (int)(intptr_t)pos, 0xbfb, 0);
        if (started != 0) {
            *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x40;
        }
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x801ffb3c  vsbo_fragment_break_tick
// Per-fragment break tick: skip 'MDON' nodes and class 0x507; decay +0x1cc; on
// expiry re-seed the body (rigid + rng lifetime + register + alloc + descriptor +
// spark callbacks) then run the break-tick impl.
// ---------------------------------------------------------------------------
void vsbo_fragment_break_tick(unsigned long long param_1, unsigned long long param_2, double param_3,
                              double param_4, double param_5, double param_6, double param_7,
                              unsigned long long param_8, int *param_9)
{
    int ent = *param_9;

    if (((*(int *)(ent + 0x2d8) == 0) || (*(int *)(*(int *)(ent + 0x2d8) + 4) != 0x4d444f4e)) &&  // 'MDON'
        (*(short *)(ent + 0x94) != 0x507)) {
        double age = (double)FLOAT_80617bdc;
        if (*(unsigned int *)(ent + 0x80) != 0) {
            age = subobj_get_age_fraction(*(unsigned int *)(ent + 0x80));
        }
        if ((param_9[9] != 0x17) || (age < (double)FLOAT_80617b18)) {
            *(float *)(ent + 0x1cc) = (float)((double)*(float *)(ent + 0x1cc) - (double)(float)param_9[8]);
            if (((double)*(float *)(ent + 0x1cc) < (double)FLOAT_80617ae8) &&
                ((double)FLOAT_80617bdc == (double)*(float *)(ent + 0x1a4))) {
                *(int *)(*(int *)(ent + 4) + 0x30) = 4;
                unsigned int r = __we639_rng();
                *(float *)(ent + 0x1a4) = FLOAT_80617bb8 +
                    (float)((double)(float)((double)CONCAT44(0x43300000, r) - DOUBLE_80617b98) / (double)FLOAT_80617b90);
                *(int *)(ent + 0x1a0) = 0;
                entity_register_for_update(ent, 3, -0x7fe1edc0);
                __we641_subobj_redirty();
                if (*(int *)(ent + 0x80) == 0) {
                    unsigned int body = __we641_subobj_pool_alloc();
                    *(int *)(ent + 0x80) = (int)body;
                    if (body != 0) {
                        __we641_subobj_build_descriptor_default(0, 0, 0, 0, 0, 0, 0,
                            *(unsigned int *)(ent + 0x80), ent, 1, -1, 0.0f);
                    }
                }
                if (*(unsigned int *)(ent + 0x80) != 0) {
                    subobj_set_extra_words(*(unsigned int *)(ent + 0x80),
                                           (unsigned int)(uintptr_t)&debris_fragment_spark_tick_cb,
                                           (unsigned int)(uintptr_t)&debris_fragment_emit_spark);
                }
            }
            if (*(int *)(ent + 0x80) != 0) {
                subobj_break_tick_impl(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x801ffd14  vsbo_monitor_scanline_render  (graphics)
// SEMM monitor-screen scanline pass: bind strip table -> RT0, ramp/relax the +0xc0
// warp factor, and (for the 'INMO' variant while alert 0x40 is set) emit a periodic
// damage spray from the prop.
// ---------------------------------------------------------------------------
void vsbo_monitor_scanline_render(int entity)
{
    int strip = collision_alloc_strip_table(*(int **)(entity + 4));
    if (DAT_80611928 != *(int *)(DAT_8061278c + 0x554)) {   // DAT_8061278c[0x155]
        *(int *)(DAT_8061278c + 0x554) = DAT_80611928;
    }
    *(int *)(strip + 0x20) = *(int *)DAT_8061278c;

    float dt = FLOAT_80617be0 *
        (FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) * FLOAT_806108ec;
    if (*(int *)(entity + 0x1d4) == 0) {
        float v = -(dt - *(float *)(entity + 0xc0));
        *(float *)(entity + 0xc0) = (FLOAT_80617ae8 < v) ? v : FLOAT_80617ae8;
    } else {
        float v = dt + *(float *)(entity + 0xc0);
        *(float *)(entity + 0xc0) = (v < FLOAT_80617be4) ? v : FLOAT_80617be4;
    }

    int alerted = aiactor_alert_group_flagA_any(0x40);
    if ((alerted == 0) || (*(int *)(entity + 0x2d8) == 0) ||
        (*(int *)(*(int *)(entity + 0x2d8) + 4) != 0x4d4f4e49)) {   // 'INMO' (reversed)
        *(int *)(*(int *)(entity + 4) + 0x30) = 1;
    } else {
        *(float *)(entity + 0xd0) =
            -((FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) * FLOAT_806108ec - *(float *)(entity + 0xd0));
        // frame-stamp parity gate on the render flag
        int half = ((int)DAT_806109c0 >> 1) + (((int)DAT_806109c0 < 0 && (DAT_806109c0 & 1) != 0) ? 1 : 0);
        unsigned int q = (half != 0) ? (unsigned int)(DAT_80611928 / half) : 0;
        unsigned int qs = q >> 0x1f;
        *(unsigned int *)(*(int *)(entity + 4) + 0x30) = -(unsigned int)(((q & 1) ^ qs) != qs) & 2;
        *(int *)(entity + 0x1d4) = 0;
        if (*(float *)(entity + 0xd0) < FLOAT_80617ae8) {
            int   evt[23];
            float sprayDir[3];
            float sprayNorm[3];
            float pos[3];

            sprayNorm[0] = DAT_803dfc74; sprayNorm[1] = DAT_803dfc78; sprayNorm[2] = DAT_803dfc7c;
            sprayDir[0] = DAT_803dfc68; sprayDir[1] = DAT_803dfc6c; sprayDir[2] = DAT_803dfc70;
            memset_wrapper((int)evt, 0, 0x5c);
            evt[0] = entity;
            *(int *)((char *)evt + 0x24) = 2;   // local_60 = 2
            float mag = FLOAT_80617b18 * ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90) + FLOAT_80617b18;
            (void)mag;
            float ox = FLOAT_80617b58 * ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
            float oy = FLOAT_80617b58 * ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
            double oz = (double)((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
            float sx = ox * FLOAT_80617b88;
            float sy = oy * FLOAT_80617b88;
            double sz = (double)((float)((double)FLOAT_80617b58 * oz) * FLOAT_80617b88);
            pos[0] = (float)((double)sx + (double)*(float *)(entity + 100));
            pos[1] = (float)((double)sy + (double)*(float *)(entity + 0x68));
            pos[2] = (float)(sz + (double)*(float *)(entity + 0x6c));
            *(float *)((char *)evt + 0x38) = pos[0];   // local_5c region (spray offset ptrs)
            __we641_damage_dispatch_to_entity(sz, (double)sy, (double)sx, (double)FLOAT_80617b58, oz,
                                              0, 0, 0, evt);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80200084  semm_scanline_draw  (graphics)
// Draw the monitor-screen scanline overlay into the 0x40x0x40 RT for a live 'FR06'/
// 'FR07' cam: GX fill + textured sprite-rects (tex 0x333) stepped down the screen
// with a frame-time-keyed interference pattern.
// ---------------------------------------------------------------------------
void semm_scanline_draw(void)
{
    int ctx = FUN_803826d4();
    if (DAT_80611928 != *(int *)(DAT_8061278c + 0x558)) {   // DAT_8061278c[0x156]
        if ((FLOAT_80617ae8 == *(float *)(ctx + 0xc0)) &&
            ((*(short *)(ctx + 0x94) == 0x506) || (*(short *)(ctx + 0x94) == 0x507))) {
            double frames = game_time_to_frames();
            unsigned int phase = (unsigned int)(FLOAT_80617be8 *
                (float)((double)(FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) * frames));
            gx_set_color_update_mode(0);
            gx_set_sub_material_id(1);
            gx_fill_rect_clipped(0, 0, 0x40, 0x40, 0x7f);
            gx_set_color_update_mode(0);
            gx_set_active_material(sprite_get_texture_id(0x333));

            int rowV = phase << 4;
            int row = 1;
            while (row < 0x40) {
                int qd = (int)phase / 0x28;
                unsigned int qs = qd >> 0x1f;
                unsigned int qmod4 = qs * 4 | (unsigned int)(qd * 0x40000000 + qs) >> 0x1e;
                int band = (int)(qmod4 - qs);
                if ((qmod4 == qs) && (1 < (int)phase % 0x28)) {
                    gx_draw_sprite_rect(0, row, *(unsigned int *)(&DAT_8045600c + ((int)phase % 5) * 4), row + 2,
                                        0, rowV + 8U, *(unsigned int *)(&DAT_8045600c + ((int)phase % 5) * 4) << 4, rowV + 8U);
                } else if (((band == 1) || (band == 3)) && (1 < (int)phase % 0x28)) {
                    int step = (band == 3) ? 2 : 5;
                    for (int x = step; x < 0x40; x = x + step * 2) {
                        gx_draw_sprite_rect(x, row, x + step, row + 2, x << 4, rowV + 8U, (x + step) * 0x10, rowV + 8U);
                    }
                } else if ((band == 2) && (((phase & 1) ^ (phase >> 0x1f)) != (phase >> 0x1f))) {
                    unsigned int m = (int)phase % 0x28;
                    m = (unsigned int)((((int)m >> 1) + (((int)m < 0 && (m & 1) != 0) ? 1 : 0)) * 0x40) / 0x14;
                    gx_draw_sprite_rect(0, row, m, row + 2, 0, rowV + 8U, m << 4, rowV + 8U);
                }
                phase = phase + 1;
                rowV = rowV + 0x10;
                row = (int)((double)(float)((double)(CONCAT44(0x43300000, row) ^ 0x80000000) - DOUBLE_80617b00) + (double)FLOAT_80617b38);
            }
        }
        *(int *)(DAT_8061278c + 0x558) = DAT_80611928;
        render_target_bind_default(*(int *)DAT_8061278c);
    }
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// 0x80200360  vsbo_fragment_cache_tick
// Transform the cached fragment particle's local offset + velocity into world space
// via the subobject matrix and push position/velocity/drag into the cached emitter.
// ---------------------------------------------------------------------------
void vsbo_fragment_cache_tick(int entity)
{
    int *node;
    float *m;
    float vel[3];
    float pos[3];

    vel[0] = DAT_803dfc80; vel[1] = DAT_803dfc84; vel[2] = DAT_803dfc88;
    node = *(int **)(*(int *)(entity + 4) + 4);
    float *localPos = (float *)node[*node * -0x30 + 0x4b];
    pos[0] = localPos[0]; pos[1] = localPos[1]; pos[2] = localPos[2];

    m = (float *)subobj_get_object_matrix(*(unsigned int *)(entity + 0x80));
    mtx4_transform_vector(m, pos);
    m = (float *)subobj_get_object_matrix(*(unsigned int *)(entity + 0x80));
    mtx4_transform_vector(m, vel);
    pos[0] = pos[0] + *(float *)(entity + 100);
    pos[1] = pos[1] + *(float *)(entity + 0x68);
    pos[2] = pos[2] + *(float *)(entity + 0x6c);

    int cached = **(int **)(entity + 0x7c);
    particle_cached_set_position((ParticleEmitter **)&cached, (Vec3 *)pos);
    cached = **(int **)(entity + 0x7c);
    particle_cached_set_velocity((ParticleEmitter **)&cached, (Vec3 *)vel);
    double drag = (double)FLOAT_80617ae8;
    if (drag < (double)(FLOAT_80617b2c - vel[1])) drag = (double)(FLOAT_80617b2c - vel[1]);
    cached = **(int **)(entity + 0x7c);
    particle_cached_set_drag((float)drag, (ParticleEmitter **)&cached);
}

// ---------------------------------------------------------------------------
// 0x8020048c  vsbo_fragment_emit_start
// Alloc the cached fragment emitter (0x1a0), spawn it at the prop origin, set its
// size range + lifetime, and register for update.
// ---------------------------------------------------------------------------
void vsbo_fragment_emit_start(int entity)
{
    float dir[3];
    dir[0] = DAT_803dfc8c; dir[1] = DAT_803dfc90; dir[2] = DAT_803dfc94;
    void *blk = __we641_block_alloc_dispatch(4, 1, entity + 0x7c, 0);
    *(void **)(entity + 0x7c) = blk;
    particle_emitter_spawn(*(unsigned int *)(entity + 0x7c), DAT_8061278c + 0x1a0, 0,
                           (float *)(entity + 100), dir);
    particle_emitter_set_size_range((double)FLOAT_80617bec, (double)FLOAT_80617b38);
    particle_emitter_set_lifetime((double)FLOAT_80617b68);
    entity_register_for_update(entity, 3, -0x7fdffca0);
}

// ---------------------------------------------------------------------------
// 0x80200530  vsbo_fragment_break_tick_thunk
// Thin wrapper -> subobj_break_tick_impl.
// ---------------------------------------------------------------------------
void vsbo_fragment_break_tick_thunk(unsigned long long param_1, unsigned long long param_2,
                                    double param_3, double param_4, double param_5, double param_6,
                                    double param_7, unsigned long long param_8)
{
    subobj_break_tick_impl(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
}

// ---------------------------------------------------------------------------
// 0x80200554  vsbo_fragment_spawn
// Spawn the fragment subobject: register the entity, alloc the body + descriptor
// (mode 0xe), fracture it (2 islands) with the optional split plane, then install
// the break-tick thunk as the render type.
// ---------------------------------------------------------------------------
int vsbo_fragment_spawn(unsigned long long param_1, double param_2, double param_3, double param_4,
                        double param_5, double param_6, double param_7, unsigned long long param_8,
                        unsigned int param_9, unsigned int param_10, float *param_11)
{
    unsigned long long frame = (unsigned long long)FUN_803826f0();
    int entity = (int)(frame >> 0x20);

    entity_register_for_update(entity, 7, 0);
    entity_register_for_update(entity, 0, 0);
    *(unsigned int *)(entity + 0x80) = __we641_subobj_pool_alloc();
    *(unsigned int *)(*(int *)(entity + 4) + 0x30) = *(unsigned int *)(*(int *)(entity + 4) + 0x30) & 0xfffffffd;
    __we641_subobj_build_descriptor_default(0, param_2, param_3, param_4, param_5, param_6, param_7,
                                            *(unsigned int *)(entity + 0x80), entity, 0xe, -1, 3.58732e-43f);
    int desc = subobj_get_descriptor(*(unsigned int *)(entity + 0x80));
    if ((float *)frame == (float *)0) {
        subobj_fracture_spawn((unsigned long long)0, param_2, param_3, param_4, param_5, param_6, param_7,
                              param_8, entity, 2, (unsigned int)*(unsigned char *)(desc + 0x1cc), 0,
                              param_11, (float *)0, 0, 0, (void *)0);
    } else {
        subobj_fracture_spawn((unsigned long long)0, param_2, param_3, param_4, param_5, param_6, param_7,
                              param_8, entity, 2, (unsigned int)*(unsigned char *)(desc + 0x1cc), 0,
                              (float *)0, (float *)frame, 0, 0, (void *)0);
    }
    __we641_entity_set_render_type(entity, (int)(long long)&vsbo_fragment_break_tick_thunk);
    FUN_8038273c();
    return entity;
}

// ===========================================================================
// batch_642 continuation (address range 0x80200680 - 0x80200dac)
//
// More 'VSBO' fragment/debris spawners plus the 'GNS1' pipe steam-jet prop.
// subsystem:game, worm-effect. Appended to worm_smem_state.cpp (shares the SEMM
// state block + VSBO fragment helpers from batch_641).
//
// NOTE: vsbo_fragment_spawn (batch_641, 0x80200554) is reconciled this batch to
// return the spawned entity (int) - the callers here consume it.
//
//   vsbo_shard_spawn_simple    (0x80200680) spawn a fragment (no split plane) then
//                              start its cache emitter.
//   vsbo_shard_spawn_from_hit  (0x802006b0) restore the collision result, spawn the
//                              fragment (scale 0x50 boost on a type-2 hit) with the
//                              hit's normal + a flag word, run one break-tick, then
//                              start the emitter.
//   vsbo_dust_puff_tick        (0x80200740) once the +0x1a4 timer expires, spawn a
//                              dust puff from the SEMM emitter (0x1a0) at the local
//                              matrix offset and re-arm the timer.
//   vsbo_timer_decay           (0x80200824) ease the +0x1a4 timer down by the frame
//                              delta while it is positive.
//   vsbo_chunk_launch          (0x80200874) spawn a 0x503 debris chunk with a rigid
//                              body: descriptor, collision group 8/0xf1, rng angular
//                              impulse, rng-scaled linear force, a delayed cleanup
//                              event, and the 0x36b launch sound.
//   vsbo_shard_settle          (0x80200bbc) set the +0x1a4 rest timer + clear render
//                              type on the shard.
//   vsbo_chunk_burst_countdown (0x80200bf0) if the +0x1a0 count is positive, launch a
//                              chunk and decrement.
//   vsbo_chunk_burst_tick      (0x80200c34) timed chunk-burst driver: decay +0x1a4;
//                              on expiry launch a chunk, decrement +0x1a0, and re-arm.
//   gns1_leak_sound_tick       (0x80200cd8) 'GNS1' pipe: register for update while
//                              armed, else ramp the +0x1a4 leak level toward its snap
//                              and drive the 0x369 3D leak sound.
//   gns1_jet_spray_tick        (0x80200dac) 'GNS1' steam-jet: run the leak-sound tick,
//                              pick an alternating vent bone, jitter a spawn point,
//                              occasionally (re)start the 0x36a jet sound, and fire a
//                              player projectile (weapon 0x43) along the jet.
//
// Non-matching: 4CC id GNS1; FP spill frames opaque; int<->double 0x43300000 bias
// plain math; wide FP-ABI helpers (entity_spawn_by_type, subobj_build_descriptor_
// default, player_ai_projectile_spawn_simple) routed through file-local aliases.
// ===========================================================================

// forward decl reconcile: vsbo_fragment_spawn now returns the spawned entity.
int vsbo_fragment_spawn(unsigned long long, double, double, double, double, double, double,
                        unsigned long long, unsigned int, unsigned int, float *);

// --- batch_642 raw data symbols (file-local) ---
extern unsigned int DAT_80617c00; extern unsigned int DAT_80617c04;  // vent-bone select LUT (2 packed)
extern float DAT_803dfc98; extern float DAT_803dfc9c; extern float DAT_803dfca0;  // dust puff pos
extern float DAT_803dfca4; extern float DAT_803dfca8; extern float DAT_803dfcac;  // dust puff vel
extern float DAT_803dfcb0; extern float DAT_803dfcb4; extern float DAT_803dfcb8;  // chunk force dir

extern float FLOAT_80617bc0;      // projectile accuracy const
extern float FLOAT_80617bf0;      // shard offset Z
extern float FLOAT_80617bf4;      // shard offset Y
extern float FLOAT_80617bf8;      // leak ramp target
extern float FLOAT_80617bfc;      // leak sound pitch

// entity_spawn_by_type: reuse the batch_640 wide-ABI alias shape (->int).
static int (*const __we642_entity_spawn_by_type)(double, double, double, unsigned long long,
                                                 unsigned long long, unsigned long long,
                                                 unsigned long long, unsigned long long, int,
                                                 void *, void *, int, int) =
    (int (*)(double, double, double, unsigned long long, unsigned long long, unsigned long long,
             unsigned long long, unsigned long long, int, void *, void *, int, int))
    &entity_spawn_by_type;
// subobj_build_descriptor_default wide-ABI (reuse batch_641 shape) but with an int
// entity-or-ptr arg; declared here again to accept a void* second arg cleanly.
static int (*const __we642_subobj_build_descriptor_default)(double, double, double, double,
                                                           double, double, double,
                                                           unsigned int, void *, int, int, float) =
    (int (*)(double, double, double, double, double, double, double,
             unsigned int, void *, int, int, float))&subobj_build_descriptor_default;
// player_ai_projectile_spawn_simple ROM form leads with an fp-reg double (decls form
// is void* unused1).
static void (*const __we642_player_ai_projectile_spawn_simple)(double, double, int, float *,
                                                              int *, int, int *, int, int, int,
                                                              int, int) =
    (void (*)(double, double, int, float *, int *, int, int *, int, int, int, int, int))
    &player_ai_projectile_spawn_simple;

// ---------------------------------------------------------------------------
// 0x80200680  vsbo_shard_spawn_simple
// ---------------------------------------------------------------------------
void vsbo_shard_spawn_simple(unsigned long long param_1, double param_2, double param_3,
                             double param_4, double param_5, double param_6, double param_7,
                             unsigned long long param_8, unsigned int param_9)
{
    int entity = vsbo_fragment_spawn((double)FLOAT_80617ae8, param_2, param_3, param_4, param_5,
                                     param_6, param_7, param_8, param_9, 0, (float *)0);
    vsbo_fragment_emit_start(entity);
}

// ---------------------------------------------------------------------------
// 0x802006b0  vsbo_shard_spawn_from_hit
// ---------------------------------------------------------------------------
void vsbo_shard_spawn_from_hit(unsigned long long param_1, double param_2, double param_3,
                               double param_4, double param_5, double param_6, double param_7,
                               unsigned long long param_8, unsigned int *param_9)
{
    double scale = (double)FLOAT_80617ae8;
    unsigned int classId = *param_9;
    int hit[16];   // collision-result restore block (auStack_38 + local_34)

    collision_result_restore((int)hit);
    if (hit[3] == 2) {   // local_34 at +0xc
        scale = (double)FLOAT_80617b50;
    }
    int entity = vsbo_fragment_spawn(scale, param_2, param_3, param_4, param_5, param_6, param_7,
                                     param_8, classId, param_9[0xc], (float *)param_9[10]);
    subobj_break_tick_impl((unsigned long long)0, (unsigned long long)0, param_2, param_3, param_4,
                           param_5, param_6, param_7);
    vsbo_fragment_emit_start(entity);
}

// ---------------------------------------------------------------------------
// 0x80200740  vsbo_dust_puff_tick
// ---------------------------------------------------------------------------
void vsbo_dust_puff_tick(int entity)
{
    float pos[3];
    float vel[3];
    if (*(float *)(entity + 0x1a4) <= FLOAT_80617ae8) {
        pos[0] = DAT_803dfc98; pos[1] = DAT_803dfc9c; pos[2] = DAT_803dfca0;
        vel[0] = DAT_803dfca4; vel[1] = DAT_803dfca8; vel[2] = DAT_803dfcac;
        mtx4_transform_vector((float *)(entity + 0x274), pos);
        pos[0] = pos[0] + *(float *)(entity + 100);
        pos[1] = pos[1] + *(float *)(entity + 0x68);
        pos[2] = pos[2] + *(float *)(entity + 0x6c);
        particle_emitter_spawn(0, DAT_8061278c + 0x1a0, 0, pos, vel);
        *(float *)(entity + 0x1a4) = FLOAT_80617b38;
        particle_emitter_set_lifetime((double)*(float *)(entity + 0x1a4));
    }
}

// ---------------------------------------------------------------------------
// 0x80200824  vsbo_timer_decay
// ---------------------------------------------------------------------------
void vsbo_timer_decay(int entity)
{
    if (FLOAT_80617ae8 < *(float *)(entity + 0x1a4)) {
        *(float *)(entity + 0x1a4) =
            -((FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00)) *
              FLOAT_806108ec - *(float *)(entity + 0x1a4));
    }
}

// ---------------------------------------------------------------------------
// 0x80200874  vsbo_chunk_launch
// Spawn a 0x503 debris chunk with a rigid body + impulse + 0x36b sound.
// ---------------------------------------------------------------------------
void vsbo_chunk_launch(unsigned long long param_1, unsigned long long param_2, double param_3,
                       unsigned long long param_4, unsigned long long param_5,
                       unsigned long long param_6, unsigned long long param_7,
                       unsigned long long param_8, int entity)
{
    int   *node;
    float *bone;
    float  force[3];    // local_bc/b8/b4
    float  spawn[3];    // local_a4/a0/9c
    float  spawnExtra[3];  // local_b0/ac/a8
    float  mtx[16];     // afStack_68
    int    params[16];  // sound play block (local_98..)

    force[0] = DAT_803dfcb0; force[1] = DAT_803dfcb4; force[2] = DAT_803dfcb8;
    node = *(int **)(*(int *)(entity + 4) + 4);
    bone = (float *)node[*node * -0x30 + 0x1b];
    spawn[0] = bone[0];
    spawn[1] = bone[1] - FLOAT_80617bf4;
    spawn[2] = (float)((double)bone[2] + (double)FLOAT_80617bf0);

    entity_build_local_matrix(mtx, (void *)(intptr_t)entity, 1, 0);
    mtx4_transform_vector(mtx, force);
    mtx4_transform_point(mtx, spawn);

    spawnExtra[0] = *(float *)(entity + 0xc0);
    *(int *)&spawnExtra[1] = *(int *)(entity + 0x70);
    spawnExtra[2] = (float)((double)FLOAT_80617be4 + (double)*(float *)(entity + 200));

    int *chunk = (int *)__we642_entity_spawn_by_type((double)FLOAT_80617be4, param_3, param_3,
                                                     param_4, param_5, param_6, param_7, param_8,
                                                     0x503, spawn, spawnExtra, 0, 0);
    int body = __we641_subobj_pool_alloc();
    chunk[0x20] = body;
    if (body == 0) {
        entity_destroy_with_event(chunk);
        return;
    }

    __we642_subobj_build_descriptor_default(0, param_3, param_3, param_4, param_5, param_6, param_7,
                                            chunk[0x20], chunk, 1, -1, 5.38099e-43f);
    subobj_set_collision_groups(chunk[0x20], 8, 0xf1);
    __we639_rng(); __we639_rng(); __we639_rng();
    subobj_apply_angular_impulse_local();
    __we641_subobj_redirty();

    float sc = FLOAT_80617b58 *
        ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c) + FLOAT_80617b58;
    force[0] = force[0] * sc;
    force[1] = force[1] * sc;
    force[2] = force[2] * sc;
    force[0] = FLOAT_80617af4 *
        ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
    subobj_add_scaled_linear_force(chunk[0x20], force);
    weapon_start_delayed_event((double)FLOAT_80617afc, (int)chunk, 6);

    sound_init_play_params((int)params);
    *(unsigned char *)((char *)params + 0x38) = 1;   // local_70
    params[0] = 0x36b;
    *(float *)((char *)params + 0x24) = spawn[0];   // local_7c
    *(float *)((char *)params + 0x28) = spawn[1];   // local_78
    *(float *)((char *)params + 0x2c) = spawn[2];   // local_74
    sound_play_request(params);
}

// ---------------------------------------------------------------------------
// 0x80200bbc  vsbo_shard_settle
// ---------------------------------------------------------------------------
void vsbo_shard_settle(int *entity)
{
    *(float *)(*entity + 0x1a4) = FLOAT_80617b78;
    entity_set_render_type(*entity, 0);
}

// ---------------------------------------------------------------------------
// 0x80200bf0  vsbo_chunk_burst_countdown
// ---------------------------------------------------------------------------
void vsbo_chunk_burst_countdown(unsigned long long param_1, unsigned long long param_2,
                                double param_3, unsigned long long param_4, unsigned long long param_5,
                                unsigned long long param_6, unsigned long long param_7,
                                unsigned long long param_8, int entity)
{
    if (0 < *(int *)(entity + 0x1a0)) {
        vsbo_chunk_launch(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8, entity);
        *(int *)(entity + 0x1a0) = *(int *)(entity + 0x1a0) - 1;
    }
}

// ---------------------------------------------------------------------------
// 0x80200c34  vsbo_chunk_burst_tick
// ---------------------------------------------------------------------------
void vsbo_chunk_burst_tick(int entity)
{
    double zero = (double)FLOAT_80617ae8;
    double lvl  = (double)*(float *)(entity + 0x1a4);
    if (zero < lvl) {
        double dt = (double)(float)((double)FLOAT_80617aec /
            (double)(float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00));
        *(float *)(entity + 0x1a4) = -(float)(dt * (double)FLOAT_806108ec - lvl);
        if ((double)*(float *)(entity + 0x1a4) < zero) {
            vsbo_chunk_launch((unsigned long long)0, (unsigned long long)0, (double)FLOAT_80617aec,
                              (unsigned long long)0, (unsigned long long)0, (unsigned long long)0,
                              (unsigned long long)0, (unsigned long long)0, entity);
            *(int *)(entity + 0x1a0) = *(int *)(entity + 0x1a0) - 1;
            if (0 < *(int *)(entity + 0x1a0)) {
                *(float *)(entity + 0x1a4) = *(float *)(entity + 0x1a4) + FLOAT_80617b78;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80200cd8  gns1_leak_sound_tick
// 'GNS1' pipe: register for update while armed, else ramp the +0x1a4 leak level
// toward its snap and drive the 0x369 3D leak sound.
// ---------------------------------------------------------------------------
void gns1_leak_sound_tick(int entity)
{
    if (*(int *)(entity + 0x60) == 0x474e5331) {   // 'GNS1'
        entity_register_for_update(entity, 3, 0);
    } else {
        int stepped = scalar_step_toward_snap((double)FLOAT_80617bf8,
            (double)(FLOAT_806108ec * (FLOAT_80617aec / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617b00))),
            (float *)(entity + 0x1a4));
        if (stepped == 0) {
            *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x40;
        }
        float *pos = entity_get_world_position(entity);
        int started = __we640_sound_update_3d_source((double)*(float *)(entity + 0x1a4),
            (double)FLOAT_80617bfc, (double)FLOAT_80617aec, entity + 0x22c, (int)(intptr_t)pos, 0x369, 0);
        if (started != 0) {
            *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x40;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80200dac  gns1_jet_spray_tick
// 'GNS1' steam-jet: run the leak-sound tick, pick an alternating vent bone, jitter a
// spawn point, occasionally (re)start the 0x36a jet sound, and fire a player
// projectile (weapon 0x43) along the jet.
// ---------------------------------------------------------------------------
void gns1_jet_spray_tick(int entity)
{
    unsigned int ventSel[2];
    float  spawn[3];   // local_98/94/90
    float  dir[3];     // local_8c/88/84
    int    params[6];  // sound play block (local_80..)

    gns1_leak_sound_tick(entity);
    unsigned int idx = *(unsigned int *)(entity + 0x1a0);
    ventSel[0] = DAT_80617c00;
    ventSel[1] = DAT_80617c04;

    float jx = FLOAT_80617b58 * ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
    float jy = FLOAT_80617b58 * ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
    float jz = FLOAT_80617b58 * ((float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90 - FLOAT_80617b2c);
    double amp = (double)FLOAT_80617b88;

    int boneIdx = (int)ventSel[idx & 1] >> 0x10;
    float *mtx  = *(float **)(*(int *)(entity + 4) + 0x10);
    int fieldOff = (ventSel[idx & 1] & 0xffff) * 0x18;

    dir[0] = mtx[8]  + (float)((double)jx * amp);
    dir[1] = mtx[9]  + (float)((double)jy * amp);
    dir[2] = mtx[10] + (float)((double)jz * amp);

    int *node = *(int **)(*(int *)(entity + 4) + 4);
    int boneBase = node[*node * -0x30 + boneIdx * 0x30 + 0x1b];
    spawn[0] = *(float *)(boneBase + fieldOff);
    spawn[1] = *(float *)(boneBase + fieldOff + 4);
    spawn[2] = *(float *)(boneBase + fieldOff + 8);
    mtx4_transform_point(mtx, spawn);

    float roll = (float)((double)CONCAT44(0x43300000, __we639_rng()) - DOUBLE_80617b98) / FLOAT_80617b90;
    if (roll < FLOAT_80617b18 * FLOAT_806108ec) {
        sound_register_fade_out((double)FLOAT_80617b58, (double)FLOAT_80617bc8,
                                (int *)(entity + 0x228), (unsigned int *)0);
        sound_init_play_params((int)params);
        *(unsigned char *)((char *)params + 0x18) = 1;   // local_58
        params[0] = 0x36a;
        *(float *)((char *)params + 0x14) = FLOAT_80617be8;  // local_54
        *(float *)((char *)params + 0x08) = FLOAT_80617bf8;  // local_68
        *(float *)((char *)params + 0x0c) = spawn[0];        // local_64
        *(float *)((char *)params + 0x10) = spawn[1];        // local_60 (approx layout)
        *(int *)(entity + 0x228) = (int)sound_play_request(params);
    }

    __we642_player_ai_projectile_spawn_simple((double)FLOAT_80617b54, (double)FLOAT_80617bc0, 4,
                                              spawn, (int *)dir, entity, (int *)0, 6, 0, 0x43, 0, 2);
    *(int *)(entity + 0x1a0) = *(int *)(entity + 0x1a0) + 1;
}

// ===========================================================================
// batch_643 continuation (address range 0x80201048 - 0x80201b8c)
//
// The SEMM/VSBO set-piece's master per-class init dispatcher + the mission
// countdown-timer subsystem, plus the level-init of the NEXT prop set-piece
// (its own 0x6d0 state block DAT_806127a4, ambient sound loops 0x62f/0x630/0x645).
// subsystem:game, worm-effect. Appended to worm_smem_state.cpp.
//
//   vsbo_class_init_dispatch  (0x80201048) master per-class init switch on the
//                             entity's +0x94 class id (0x10c..0x50f): wires the
//                             render/update callbacks + flags for every VSBO/SEMM
//                             prop class decompiled in batches 639-642 (fountain,
//                             shard, fragment, monitor, chunk, GNS1, etc.), and for
//                             the monitor classes (0x506/0x507) seeds the scanline
//                             warp state + allocates the 0x40x0x40 monitor RT.
//   semm_mission_end_handler  (0x802015b8) op3 objective handler: format the win/
//                             lose end string (0x15ea/0x15eb) into every player HUD,
//                             set the mission-complete flag, and run match_process_end.
//   semm_timer_reset          (0x80201694) clear the mission timer id (DAT_805c6290 = -1).
//   semm_timer_expiry_tick    (0x802016a4) if the timer is armed and its duration has
//                             elapsed, set the complete flag and run match_process_end.
//   semm_timer_fraction       (0x80201748) 0-1 remaining fraction of the timer for the
//                             HUD gauge (1 when unarmed).
//   semm_timer_arm            (0x802017d0) arm the timer from a trigger's id/duration.
//   semm_timer_complete       (0x80201818) fire the completion payload + clear the timer.
//   strk_preload              (0x80201854) NEXT set-piece preload: alloc its 0x6d0 state
//                             block (DAT_806127a4), preload 6 resources, clear 5 sound
//                             banks + the projectile-AI banks.
//   strk_ambient_tick         (0x802018e4) NEXT set-piece per-frame: 0x62f master loop,
//                             an LED colour driven by an expr-stack value, and either
//                             the "open" 0x630 triple 3D loop or the "closed" 0x645
//                             double loop depending on the level door flags.
//   strk_level_init           (0x80201b8c) NEXT set-piece level init (gated on flag
//                             0x40 clear): alloc/clear the 0x6d0 block, attach to the
//                             1-2 view actors, register 2 level actors + resource pool,
//                             flag ~16 soundpad zones, and build 3 particle defs +
//                             emitters (blood 0x326 / smoke 0x34f / dust 0x326).
//
// Non-matching: FP spill frames opaque; int<->double 0x43300000 bias plain math;
// wide FP-ABI helpers (entity_model_cache_alloc, mem_alloc, entity_preload_resources,
// entity_create_attachment, level_actors_register/_resource_pool_setup,
// particle_def_create/emitter_create, FUN_80387fb8) routed through file-local aliases;
// render callbacks passed as (int)(long long)&fn.
// ===========================================================================

// --- batch_643 raw data symbols (file-local) ---
extern int   DAT_806127a4;        // NEXT set-piece 0x6d0 state block (ptr)
extern int   DAT_806127a8;        // NEXT set-piece init latch
extern int   DAT_806127ac;        // NEXT set-piece handle
extern int   DAT_805015c4;        // mission/objective flag word
extern int   DAT_805c6290;        // mission timer id (-1 = unarmed)
extern int   DAT_805c6294;        // mission timer start frame stamp
extern float DAT_805c6298;        // mission timer duration
extern int   DAT_80611b1c;        // level door/state flags block
extern int   DAT_80456c40;        // NEXT-set-piece level-actor class block 0 (by address)
extern int   DAT_80456d18;        // NEXT-set-piece level-actor class block 1 (by address)
extern float DAT_803dfcc8; extern float DAT_803dfccc; extern float DAT_803dfcd0;  // master loop pos
extern float DAT_803dfcd4; extern float DAT_803dfcd8; extern float DAT_803dfcdc;  // closed loop A pos
extern float DAT_803dfce0; extern float DAT_803dfce4; extern float DAT_803dfce8;  // closed loop B pos
extern float DAT_803dfcec; extern float DAT_803dfcf0; extern float DAT_803dfcf4;  // open loop A pos
extern float DAT_803dfcf8; extern float DAT_803dfcfc; extern float DAT_803dfd00;  // open loop B pos
extern float DAT_803dfd04; extern float DAT_803dfd08; extern float DAT_803dfd0c;  // open loop C pos
extern float DAT_803dfd10; extern float DAT_803dfd14; extern float DAT_803dfd18;  // emitter scale vec
extern float DAT_803dfd1c; extern int DAT_803dfd20; extern int DAT_803dfd24;      // spawn pos base
extern float DAT_803dfd28; extern int DAT_803dfd2c; extern int DAT_803dfd30;      // spawn vel base

extern float FLOAT_80617c0c;      // 0.0f
extern float FLOAT_80617c10;      // (int->double bias) via DOUBLE_80617c10
extern double DOUBLE_80617c10;    // int->double bias for timer frames
extern float FLOAT_80617c18;      // 1.0f
extern float FLOAT_80617c1c;      // timer full fraction
extern float FLOAT_80617c20; extern float FLOAT_80617c24; extern float FLOAT_80617c28;
extern float FLOAT_80617c2c; extern float FLOAT_80617c30; extern float FLOAT_80617c34;
extern float FLOAT_80617c38; extern float FLOAT_80617c3c; extern float FLOAT_80617c40;
extern float FLOAT_80617c44; extern float FLOAT_80617c48; extern float FLOAT_80617c4c;
extern float FLOAT_80617c50; extern float FLOAT_80617c54; extern float FLOAT_80617c58;
extern float FLOAT_80617c5c; extern float FLOAT_80617c60; extern float FLOAT_80617c64;
extern float FLOAT_80617c68; extern float FLOAT_80617c6c; extern float FLOAT_80617c70;
extern float FLOAT_80617c74; extern float FLOAT_80617c78; extern float FLOAT_80617c7c;
extern float FLOAT_80617c80; extern float FLOAT_80617c84; extern float FLOAT_80617c88;
extern float FLOAT_80617c8c; extern float FLOAT_80617c90; extern float FLOAT_80617c94;
extern float FLOAT_80617c98; extern float FLOAT_80617c9c; extern float FLOAT_80617ca0;
extern float FLOAT_80617ca4; extern float FLOAT_80617ca8; extern float FLOAT_80617cac;
extern float FLOAT_80617cb0;

// entity_model_cache_alloc: ROM wide ABI (fpctx + 8 dwords + model + size) returns
// the cache slot (decls form is (int,int)->void*).
static int (*const __we643_entity_model_cache_alloc)(double, double, double, unsigned long long,
                                                     unsigned long long, unsigned long long,
                                                     unsigned long long, unsigned long long,
                                                     int, int, int, int, int, int, int, int) =
    (int (*)(double, double, double, unsigned long long, unsigned long long, unsigned long long,
             unsigned long long, unsigned long long, int, int, int, int, int, int, int, int))
    &entity_model_cache_alloc;
// mem_alloc: ROM wide ABI (fpctx + tail + size + type) returns the block (decls form
// is (s32,s32)).
static int (*const __we643_mem_alloc)(unsigned long long, unsigned long long, unsigned long long,
                                      unsigned long long, unsigned long long, unsigned long long,
                                      unsigned long long, unsigned long long, int, int, int, int,
                                      int, int, int, int) =
    (int (*)(unsigned long long, unsigned long long, unsigned long long, unsigned long long,
             unsigned long long, unsigned long long, unsigned long long, unsigned long long,
             int, int, int, int, int, int, int, int))&mem_alloc;
// entity_preload_resources: ROM wide ABI (fpctx + type) returns the running context
// (decls form is (int)).
static unsigned long long (*const __we643_entity_preload_resources)(unsigned long long,
                                                                    unsigned long long,
                                                                    unsigned long long,
                                                                    unsigned long long,
                                                                    unsigned long long,
                                                                    unsigned long long,
                                                                    unsigned long long,
                                                                    unsigned long long, int) =
    (unsigned long long (*)(unsigned long long, unsigned long long, unsigned long long,
                            unsigned long long, unsigned long long, unsigned long long,
                            unsigned long long, unsigned long long, int))&entity_preload_resources;
// entity_create_attachment: ROM wide ABI returns the running context (decls form is
// (int)).
static unsigned long long (*const __we643_entity_create_attachment)(unsigned long long,
                                                                    double, double,
                                                                    unsigned long long,
                                                                    unsigned long long,
                                                                    unsigned long long,
                                                                    unsigned long long,
                                                                    unsigned long long, int) =
    (unsigned long long (*)(unsigned long long, double, double, unsigned long long,
                            unsigned long long, unsigned long long, unsigned long long,
                            unsigned long long, int))&entity_create_attachment;
// expr_evaluate_stack returns the evaluated value (decls form is void).
static float (*const __we643_expr_evaluate_stack)(void) =
    (float (*)(void))&expr_evaluate_stack;
// hud_text_submit signature already matches; FUN_80387fb8 is the HUD text formatter.
extern int  FUN_80387fb8(unsigned long long a, double b, double c, unsigned long long d,
                         unsigned long long e, unsigned long long f, unsigned long long g,
                         unsigned long long h, int outBuf, char *fmt, int arg, int i, int j,
                         int k, int l, int m);

// helpers not in the shared header (file-local)
extern void FUN_80373b00(void);            // fx/anim group kick
extern int  FUN_8022136c(int timerId);     // resolve timer -> objective result
extern void FUN_802212b4(int timerId);     // arm objective timer
extern void FUN_8020231c(unsigned long long a, double b, double c, double d, double e,
                         double f, double g, unsigned long long h);  // NEXT-set-piece sub-tick

// reuse batch_639 particle aliases for the NEXT-set-piece FX build
// (__we639_particle_def_create / __we639_particle_emitter_create declared above).

// ---------------------------------------------------------------------------
// 0x80201048  vsbo_class_init_dispatch
// Master per-class init switch. Wires the render/update callbacks + flags for each
// VSBO/SEMM prop class (0x10c..0x50f). Structured here as an explicit if/else chain
// mirroring the ROM's binary-search dispatch on the +0x94 class id.
// ---------------------------------------------------------------------------
void vsbo_class_init_dispatch(unsigned long long param_1, double param_2, double param_3,
                              double param_4, double param_5, double param_6, double param_7,
                              unsigned long long param_8, int entity, int arg10,
                              int arg11, int arg12, int arg13, int arg14, int arg15, int arg16)
{
    unsigned short cls = *(unsigned short *)(entity + 0x94);

    if (cls == 0x4f7) {
        entity_register_for_update(entity, 3, -0x7fe00dc0);
        return;
    }
    if (cls < 0x4f7) {
        if (cls == 0x4e4) {
            FUN_80373b00();
            entity_register_for_update(entity, 8, -0x7fe010c8);
            *(float *)(entity + 0x1cc) = FLOAT_80617be8;
            *(float *)(entity + 0x1d8) = FLOAT_80617be8;
            return;
        }
        if (cls < 0x4e4) {
            if (cls == 0x4d7) {
                *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x40020000;
                *(int *)(entity + 0x34) = 0xd5;
                entity_register_for_update(entity, 3, -0x7fdff328);
                return;
            }
            if (0x4d6 < cls) {
                if (0x4e1 < cls) {   // 0x4e2/0x4e3
                    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x14;
                    *(float *)(entity + 0x1a4) = FLOAT_80617ba0;
                    entity_register_for_update(entity, 3, -0x7fe02048);
                    entity_set_render_type(entity, (int)(long long)&vsbo_death_shatter);
                    entity_register_for_update(entity, 0, -0x7fe01be8);
                    entity_special_list_add(entity);
                    return;
                }
                if (cls < 0x4e0) return;
                // 0x4e0/0x4e1: submesh model-cache slot
                entity_register_for_update(entity, 3, -0x7fe01bac);
                entity_register_for_update(entity, 10, -0x7fe016ac);
                *(int *)(entity + 0x7c) = __we643_entity_model_cache_alloc(param_2, param_3, param_4,
                    param_5, param_6, param_7, param_8, 0, entity, arg10, 0x18,
                    arg12, arg13, arg14, arg15, arg16);
                memset_wrapper(*(int *)(entity + 0x7c), 0, 0x18);
                return;
            }
            if (0x10d < cls) {
                if (cls < 0x4d6) return;
                *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x4000000;
                entity_register_for_update(entity, 3, -0x7fdff254);
                return;
            }
            if (cls < 0x10c) return;
            *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) & 0xffff3fff;
            *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x4000;
            return;
        }
        if (cls == 0x4ec) {
            // spark-fountain source
            unsigned int r = __we639_rng();
            double frac = (double)(float)((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80617b98) / FLOAT_80617b90);
            *(float *)(entity + 0x1a4) = (float)((double)FLOAT_80617bb8 + frac);
            *(int *)(entity + 0x7c) = __we643_entity_model_cache_alloc(frac, 0, 0, param_4, param_5,
                param_6, param_7, param_8, entity, arg10, 0x1e0, arg12, arg13, arg14, arg15, arg16);
            **(float **)(entity + 0x7c) = FLOAT_80617bdc;
            entity_register_for_update(entity, 3, -0x7fe008c8);
            *(float *)(entity + 0x1cc) = FLOAT_80617b58;
            *(float *)(entity + 0x1d8) = FLOAT_80617b58;
            *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x10;
            entity_set_render_type(entity, (int)(long long)&vsbo_fountain_tick);
            return;
        }
        if (cls < 0x4ec) {
            if (0x4e6 < cls) return;   // 0x4e5/0x4e6
            *(unsigned int *)(*(int *)(entity + 4) + 0x30) = *(unsigned int *)(*(int *)(entity + 4) + 0x30) | 1;
            return;
        }
        if (cls != 0x4f1) return;
        goto monitor_init;   // 0x4f1 shares the monitor-warp init path
    }
    else if (cls != 0x506) {
        if (cls < 0x506) {
            if (cls == 0x4fd) {
                *(int *)(entity + 0x1a0) = 0;
                *(float *)(entity + 0x1a4) = FLOAT_80617ae8;
                *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 4;
                *(float *)(entity + 0x25c) = FLOAT_80617aec;
                *(float *)(entity + 0x260) = FLOAT_80617ae8;
                *(float *)(entity + 0xd0) = FLOAT_80617ae8;
                render_instance_set_material((double)FLOAT_80617ae8, *(int **)(entity + 4), 1);
                entity_register_for_update(entity, 2, -0x7fe014dc);
                entity_register_for_update(entity, 0, -0x7fe01550);
                entity_register_for_update(entity, 3, -0x7fe013e4);
                return;
            }
            if (cls < 0x4fd) {
                if (cls != 0x4fb) return;
                FUN_80373b00();
                entity_register_for_update(entity, 8, -0x7fe00f3c);
                return;
            }
            if (cls == 0x4ff) {
                *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x14;
                entity_set_render_type(entity, (int)(long long)&vsbo_shard_settle);
                entity_register_for_update(entity, 0, -0x7fdff410);
                entity_register_for_update(entity, 3, -0x7fdff3cc);
                *(int *)(entity + 0x1a0) = 0x32;
                return;
            }
            if (0x4fe < cls) return;   // (0x4ff handled)
            // 0x4fe: shard-from-hit
            *(float *)(entity + 0x1a4) = FLOAT_80617ae8;
            *(int *)(*(int *)(entity + 4) + 0x30) = 3;
            *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x14;
            entity_register_for_update(entity, 7, -0x7fdff980);
            entity_register_for_update(entity, 0, -0x7fdff8c0);
            entity_register_for_update(entity, 3, -0x7fdff7dc);
            entity_set_render_type(entity, (int)(long long)&vsbo_shard_spawn_from_hit);
            return;
        }
        if (cls == 0x50b) return;
        if (0x50a < cls) {
            if (0x50f < cls) return;
            *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x40020000;
            *(int *)(entity + 0x38) = 0xd5;
            *(int *)(entity + 0x34) = 0xd5;
            *(int *)(entity + 0x3c) = 9;
            *(unsigned char *)(entity + 0x30) = 2;
            *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x20;
            return;
        }
        if (0x507 < cls) return;
        goto monitor_init;   // 0x507
    }
    // 0x506 (and 0x507 fallthrough): monitor screen
    entity_register_for_update(entity, 0, -0x7fe00334);

monitor_init:
    *(int *)(entity + 0x1d4) = 1;
    *(float *)(entity + 0xc0) = *(float *)(entity + 0xc0) + FLOAT_80617be4;
    {
        unsigned int r = __we639_rng();
        *(float *)(entity + 0xd0) =
            FLOAT_80617bec * ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80617b98) / FLOAT_80617b90) + FLOAT_80617b2c;
    }
    *(float *)(entity + 0x1a4) = FLOAT_80617bdc;
    *(float *)(entity + 0x1cc) = FLOAT_80617b50;
    *(float *)(entity + 0x1d8) = FLOAT_80617b50;
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x14;
    *(unsigned int *)(entity + 0x9c) = *(unsigned int *)(entity + 0x9c) | 0x9000018;
    entity_set_render_type(entity, (int)(long long)&vsbo_fragment_break_tick);
    *(unsigned char *)(entity + 0x32) = 2;
    entity_register_for_update(entity, 3, -0x7fe002ec);
    entity_register_for_update(entity, 8, -0x7fdfff7c);
    if (*(int *)DAT_8061278c == 0) {
        render_target_alloc_scratch(0x40, 0x40, (int *)DAT_8061278c);
    }
    *(int *)(*(int *)(entity + 4) + 0x30) = 1;
}

// ---------------------------------------------------------------------------
// 0x802015b8  semm_mission_end_handler
// op3 objective handler: format the win/lose end string into every player HUD, set
// the mission-complete flag, and run match_process_end.
// ---------------------------------------------------------------------------
int semm_mission_end_handler(unsigned long long param_1, double param_2, double param_3,
                             double param_4, double param_5, double param_6, double param_7,
                             unsigned long long param_8, int entity, int op, int trigger,
                             int arg12, int arg13, int arg14, int arg15, int arg16)
{
    unsigned int buf[65];
    if (op == 3) {
        int model = *(int *)(entity + 0x7c);
        char *fmt = (*(int *)(trigger + 4) == 0) || (*(int *)(*(int *)(trigger + 4) + 0x150) == 0)
            ? (char *)(intptr_t)get_string_by_id(0x15eb)
            : (char *)(intptr_t)get_string_by_id(0x15ea);
        FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     (int)buf, fmt, *(int *)(model + 0x70), arg12, arg13, arg14, arg15, arg16);
        for (int i = 0; i < session_get_state_flag(); i = i + 1) {
            hud_text_submit(i, 4, buf);
        }
        DAT_805015c4 = DAT_805015c4 | 0x10000;
        match_process_end(0, 0, 0, 0, 0, 0, 0, 0, 0);
        return -2;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80201694  semm_timer_reset
// ---------------------------------------------------------------------------
void semm_timer_reset(void)
{
    DAT_805c6290 = -1;
}

// ---------------------------------------------------------------------------
// 0x802016a4  semm_timer_expiry_tick
// ---------------------------------------------------------------------------
void semm_timer_expiry_tick(void)
{
    if (-1 < DAT_805c6290) {
        float elapsed = (float)((double)CONCAT44(0x43300000, DAT_80611928 - DAT_805c6294 ^ 0x80000000) - DOUBLE_80617c10);
        float budget  = DAT_805c6298 * (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617c10);
        if (budget < elapsed) {
            FUN_8022136c(DAT_805c6290);
            DAT_805015c4 = DAT_805015c4 | 0x10000;
            match_process_end(0, (double)elapsed, DOUBLE_80617c10, 0, 0, 0, 0, 0, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80201748  semm_timer_fraction
// 0-1 remaining fraction of the timer (1 when unarmed) for the HUD gauge.
// ---------------------------------------------------------------------------
double semm_timer_fraction(void)
{
    float frac = FLOAT_80617c1c;
    if ((-1 < DAT_805c6290) && (FLOAT_80617c0c < DAT_805c6298)) {
        frac = FLOAT_80617c18 -
            ((FLOAT_80617c18 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617c10)) *
             (float)((double)CONCAT44(0x43300000, DAT_80611928 - DAT_805c6294 ^ 0x80000000) - DOUBLE_80617c10)) / DAT_805c6298;
    }
    return (double)frac;
}

// ---------------------------------------------------------------------------
// 0x802017d0  semm_timer_arm
// ---------------------------------------------------------------------------
void semm_timer_arm(int *trigger)
{
    DAT_805c6290 = *(int *)(*trigger + 0xc);
    DAT_805c6298 = *(float *)(*trigger + 0x10);
    DAT_805c6294 = DAT_80611928;
    FUN_802212b4(DAT_805c6290);
}

// ---------------------------------------------------------------------------
// 0x80201818  semm_timer_complete
// ---------------------------------------------------------------------------
void semm_timer_complete(unsigned long long param_1, double param_2, double param_3, double param_4,
                         double param_5, double param_6, double param_7, unsigned long long param_8)
{
    FUN_80221074((double)FLOAT_80617c0c);
    DAT_805c6290 = -1;
}

// ---------------------------------------------------------------------------
// 0x80201854  strk_preload
// NEXT set-piece preload: alloc its 0x6d0 state block, preload 6 resources, clear 5
// sound banks + the projectile-AI banks.
// ---------------------------------------------------------------------------
void strk_preload(unsigned long long param_1, double param_2, double param_3, double param_4,
                  double param_5, double param_6, double param_7, unsigned long long param_8,
                  int arg9, int arg10, int arg11, int arg12, int arg13, int arg14, int arg15,
                  int arg16)
{
    DAT_806127a4 = __we643_mem_alloc(param_1, (unsigned long long)0, 0, 0, 0, 0, 0, param_8,
                                     0x6d0, 1, arg11, arg12, arg13, arg14, arg15, arg16);
    unsigned long long ctx = __we643_entity_preload_resources(param_1, param_2, param_3, param_4,
        param_5, param_6, param_7, param_8, 0x3a1);
    ctx = __we643_entity_preload_resources(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0x3b3);
    ctx = __we643_entity_preload_resources(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0x3a3);
    ctx = __we643_entity_preload_resources(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0x3a8);
    ctx = __we643_entity_preload_resources(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0x3a4);
    __we643_entity_preload_resources(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0x745);
    sound_clear_bank_ref(0x81);
    sound_clear_bank_ref(0xd30);
    sound_clear_bank_ref(0x631);
    proj_ai_release_sound_banks();
    sound_clear_bank_ref(0x649);
    sound_clear_bank_ref(0x64a);
    player_ai_projectile_clear_sound_banks(0x1e);
}

// ---------------------------------------------------------------------------
// 0x802018e4  strk_ambient_tick
// NEXT set-piece per-frame ambient sound + LED colour driver.
// ---------------------------------------------------------------------------
void strk_ambient_tick(void)
{
    float masterPos[3];   // local_1c/18/14
    masterPos[0] = DAT_803dfcc8; masterPos[1] = DAT_803dfccc; masterPos[2] = DAT_803dfcd0;
    unsigned long long h = (unsigned long long)__we640_sound_update_3d_source(
        (double)FLOAT_80617c20, (double)FLOAT_80617c24, (double)FLOAT_80617c18,
        DAT_806127a4 + 0x460, (int)(intptr_t)masterPos, 0x62f, 0);
    FUN_8020231c(h, (double)FLOAT_80617c24, (double)FLOAT_80617c18, 0, 0, 0, 0, 0);

    // LED colour from an expr-stack value on the +0xc scene node
    if ((*(char *)(DAT_80611b1c + 0xc) != '\0')) {
        int rd = (int)(intptr_t)scene_node_get_render_data(0xc);
        if ((rd != 0) && (*(int *)(rd + 4) != 0)) {
            int *node = *(int **)(*(int *)(rd + 4) + 4);
            if (node != (int *)0) {
                int count = *node;
                int off = 0;
                int base = count * -0xc0;
                while (count > 0) {
                    int rec = (int)node + off + base;
                    if ((rec != 0) && (*(int *)(rec + 0xa4) != 0)) {
                        double v = (double)__we643_expr_evaluate_stack();
                        unsigned int c = (unsigned int)(FLOAT_80617c28 * (float)((double)FLOAT_80617c18 - v));
                        c = ((c & 0xff) + 0x10) * 0x1000000 | 0x101000;
                        *(unsigned int *)(DAT_806127a4 + 0x1ac) = c;
                        *(unsigned int *)(DAT_806127a4 + 0x1b8) = c;
                        break;
                    }
                    off = off + 0xc0;
                    count = count - 1;
                }
            }
        }
    }

    if ((*(char *)(DAT_80611b1c + 7) == '\0') && (*(char *)(DAT_80611b1c + 6) == '\0') &&
        (*(int *)(DAT_806127a4 + 0x464) < 0) && (*(int *)(DAT_806127a4 + 0x468) < 0)) {
        // "open" state: 0x630 triple loop
        float a[3], b[3], c[3];
        a[0] = DAT_803dfcec; a[1] = DAT_803dfcf0; a[2] = DAT_803dfcf4;
        b[0] = DAT_803dfcf8; b[1] = DAT_803dfcfc; b[2] = DAT_803dfd00;
        c[0] = DAT_803dfd04; c[1] = DAT_803dfd08; c[2] = DAT_803dfd0c;
        scalar_step_toward_snap((double)*(float *)(DAT_806127a4 + 0x450), (double)FLOAT_80617c34, (float *)(DAT_806127a4 + 0x44c));
        __we640_sound_update_3d_source((double)*(float *)(DAT_806127a4 + 0x44c), (double)FLOAT_80617c38, (double)FLOAT_80617c3c, DAT_806127a4 + 0x454, (int)(intptr_t)a, 0x630, 0);
        __we640_sound_update_3d_source((double)*(float *)(DAT_806127a4 + 0x44c), (double)FLOAT_80617c38, (double)FLOAT_80617c30, DAT_806127a4 + 0x458, (int)(intptr_t)b, 0x630, 0);
        __we640_sound_update_3d_source((double)*(float *)(DAT_806127a4 + 0x44c), (double)FLOAT_80617c38, (double)FLOAT_80617c18, DAT_806127a4 + 0x45c, (int)(intptr_t)c, 0x630, 0);
    } else {
        // "closed" state: 0x645 double loop
        float a[3], b[3];
        a[0] = DAT_803dfcd4; a[1] = DAT_803dfcd8; a[2] = DAT_803dfcdc;
        b[0] = DAT_803dfce0; b[1] = DAT_803dfce4; b[2] = DAT_803dfce8;
        __we640_sound_update_3d_source((double)FLOAT_80617c20, (double)FLOAT_80617c2c, (double)FLOAT_80617c30, DAT_806127a4 + 0x464, (int)(intptr_t)a, 0x645, 0);
        __we640_sound_update_3d_source((double)FLOAT_80617c20, (double)FLOAT_80617c2c, (double)FLOAT_80617c18, DAT_806127a4 + 0x468, (int)(intptr_t)b, 0x645, 0);
    }
    semm_timer_expiry_tick();
}

// ---------------------------------------------------------------------------
// 0x80201b8c  strk_level_init
// NEXT set-piece level init (gated on flag 0x40 clear).
// ---------------------------------------------------------------------------
void strk_level_init(unsigned long long param_1, double param_2, double param_3,
                     unsigned long long param_4, unsigned long long param_5,
                     unsigned long long param_6, unsigned long long param_7,
                     unsigned long long param_8)
{
    if ((DAT_805015c0 & 0x40) != 0) return;

    float scale[3];   // local_20/1c/18
    float spawn[3];   // local_2c/28/24
    float vel[3];     // local_38/34/30
    scale[0] = DAT_803dfd10; scale[1] = DAT_803dfd14; scale[2] = DAT_803dfd18;

    DAT_806127ac = -1;
    DAT_806127a8 = 1;
    memset_wrapper(DAT_806127a4, 0, 0x6cc);
    *(int *)(DAT_806127a4 + 0x460) = -1;
    *(int *)(DAT_806127a4 + 0x464) = -1;
    *(int *)(DAT_806127a4 + 0x468) = -1;
    *(int *)(DAT_806127a4 + 0x46c) = -1;
    *(int *)(DAT_806127a4 + 0x454) = -1;
    *(int *)(DAT_806127a4 + 0x458) = -1;
    *(int *)(DAT_806127a4 + 0x45c) = -1;

    unsigned long long ctx = param_1;
    if (*(int *)(DAT_80611d78 + 0x14) != 0) {
        ctx = __we643_entity_create_attachment(ctx, param_2, param_3, param_4, param_5, param_6,
                                               param_7, param_8, *(int *)(DAT_80611d78 + 0x14));
    }
    if ((1 < DAT_80612610) && (*(int *)(DAT_80611d78 + 0x1430) != 0)) {
        ctx = __we643_entity_create_attachment(ctx, param_2, param_3, param_4, param_5, param_6,
                                               param_7, param_8, *(int *)(DAT_80611d78 + 0x1430));
    }
    ctx = __we639_level_actors_register(ctx, param_2, param_3, param_4, param_5, param_6,
                                        (int)param_7, (int)param_8, 0, &DAT_80456c40, 9);
    ctx = __we639_level_actors_register(ctx, param_2, param_3, param_4, param_5, param_6,
                                        (int)param_7, (int)param_8, 1, &DAT_80456d18, 9);
    __we639_level_resource_pool_setup(ctx, param_2, param_3, param_4, param_5, param_6,
                                      (int)param_7, (int)param_8);
    proj_register_trail_def();
    FUN_8036fcf4(2);
    particle_handle_clear((unsigned int *)(DAT_806127a4 + 0x3f0));
    semm_timer_reset();

    static const int kZones[] = {0x550,0x557,0x556,0x554,0x552,0x55d,0x55f,0x55a,0x562,0x564,
                                 0x5a3,0x5a1,0x5a9,0x5ad,0x678,0x51e};
    for (unsigned int z = 0; z < sizeof(kZones)/sizeof(kZones[0]); z = z + 1) {
        int zone = DAT_80611b8c + soundpad_find_zone_by_id(kZones[z]) * 0x20;
        *(unsigned int *)(zone + 4) = *(unsigned int *)(zone + 4) | 0x20000;
    }

    FUN_802215a8(0xb, 0, 2, 2, 2);
    FUN_802215a8(7, 0, 2, 2, 2);
    FUN_802215a8(8, 0, 2, 2, 2);
    FUN_802215a8(9, 0, 2, 2, 2);
    FUN_802215a8(10, 0, 2, 2, 2);
    FUN_802215a8(0xd, 0, 2, 2, 2);

    // blood spray (0x326)
    __we639_particle_def_create((double)FLOAT_80617c40, (double)FLOAT_80617c18, DAT_806127a4 + 0x18c, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617c18, (double)FLOAT_80617c0c, (double)FLOAT_80617c0c, (double)FLOAT_80617c44, 0x4f0000ff, 0x4f0000ff, 0x202020ff);
    particle_def_set_size_start_end((double)FLOAT_80617c0c, (double)FLOAT_80617c48, (double)FLOAT_80617c4c, (double)FLOAT_80617c50);
    particle_def_set_velocity_ranges((double)FLOAT_80617c0c, (double)FLOAT_80617c54, (double)FLOAT_80617c58, (double)FLOAT_80617c0c, (double)FLOAT_80617c18, (double)FLOAT_80617c58);
    particle_def_set_color_end((double)FLOAT_80617c0c, (double)FLOAT_80617c44, (double)FLOAT_80617c5c, (double)FLOAT_80617c60);
    __we639_particle_emitter_create((double)FLOAT_80617c64, (double)FLOAT_80617c68, (double)FLOAT_80617c6c, DAT_806127a4, DAT_806127a4 + 0x18c, 0, 0x4200000);
    particle_emitter_set_color_range((double)FLOAT_80617c70, (double)FLOAT_80617c0c, (double)FLOAT_80617c74, (double)FLOAT_80617c0c);
    particle_emitter_set_lifetime_range((double)FLOAT_80617c70, (double)FLOAT_80617c0c);

    spawn[0] = DAT_803dfd1c; *(int *)&spawn[1] = DAT_803dfd20; *(int *)&spawn[2] = DAT_803dfd24;
    vel[0] = DAT_803dfd28; *(int *)&vel[1] = DAT_803dfd2c; *(int *)&vel[2] = DAT_803dfd30;
    particle_emitter_spawn(0, DAT_806127a4, 0, spawn, vel);
    spawn[0] = FLOAT_80617c78; particle_emitter_spawn(0, DAT_806127a4, 0, spawn, vel);
    spawn[0] = FLOAT_80617c7c; spawn[1] = FLOAT_80617c80; spawn[2] = FLOAT_80617c84;
    particle_emitter_spawn(0, DAT_806127a4, 0, spawn, vel);
    spawn[2] = FLOAT_80617c88; particle_emitter_spawn(0, DAT_806127a4, 0, spawn, vel);
    spawn[0] = FLOAT_80617c8c; spawn[1] = FLOAT_80617c80; spawn[2] = FLOAT_80617c84;
    particle_emitter_spawn(0, DAT_806127a4, 0, spawn, vel);
    spawn[2] = FLOAT_80617c88; particle_emitter_spawn(0, DAT_806127a4, 0, spawn, vel);

    // smoke (0x34f)
    __we639_particle_def_create((double)FLOAT_80617c90, (double)FLOAT_80617c18, DAT_806127a4 + 600, 0, 0, 3, 0x34f, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617c18, (double)FLOAT_80617c0c, (double)FLOAT_80617c0c, (double)FLOAT_80617c94, 0x3f3f4fff, 0x3f3f4fff, 0xf0f0fff);
    particle_def_set_size_start_end((double)FLOAT_80617c0c, (double)FLOAT_80617c48, (double)FLOAT_80617c4c, (double)FLOAT_80617c50);
    particle_def_set_velocity_ranges((double)FLOAT_80617c34, (double)FLOAT_80617c54, (double)FLOAT_80617c98, (double)FLOAT_80617c0c, (double)FLOAT_80617c18, (double)FLOAT_80617c98);
    particle_def_set_color_end((double)FLOAT_80617c0c, (double)FLOAT_80617c44, (double)FLOAT_80617c5c, (double)FLOAT_80617c60);
    particle_def_set_drag((double)FLOAT_80617c9c);
    __we639_particle_emitter_create((double)FLOAT_80617c64, (double)FLOAT_80617c94, (double)FLOAT_80617c98, DAT_806127a4 + 0x84, DAT_806127a4 + 600, 0, 0x200001);
    particle_emitter_set_color_range((double)FLOAT_80617ca0, (double)FLOAT_80617c0c, (double)FLOAT_80617ca4, (double)FLOAT_80617c0c);
    particle_emitter_set_lifetime_range((double)FLOAT_80617ca0, (double)FLOAT_80617c0c);

    // dust (0x326)
    __we639_particle_def_create((double)FLOAT_80617c18, (double)FLOAT_80617c18, DAT_806127a4 + 0x324, 0, 1, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617c18, (double)FLOAT_80617c0c, (double)FLOAT_80617c0c, (double)FLOAT_80617c44, 0x606f60ff, 0x606f60ff, 0x101010ff);
    particle_def_set_size_start_end((double)FLOAT_80617c0c, (double)FLOAT_80617c48, (double)FLOAT_80617c4c, (double)FLOAT_80617c50);
    particle_def_set_velocity_ranges((double)FLOAT_80617c0c, (double)FLOAT_80617c54, (double)FLOAT_80617ca8, (double)FLOAT_80617c0c, (double)FLOAT_80617c18, (double)FLOAT_80617ca8);
    particle_def_set_color_end((double)FLOAT_80617c0c, (double)FLOAT_80617c44, (double)FLOAT_80617c5c, (double)FLOAT_80617c60);
    __we639_particle_emitter_create((double)FLOAT_80617ca8, (double)FLOAT_80617cac, (double)FLOAT_80617c6c, DAT_806127a4 + 0x108, DAT_806127a4 + 0x324, 3, 0x4200006);
    particle_emitter_set_color_range((double)FLOAT_80617ca0, (double)FLOAT_80617c0c, (double)FLOAT_80617cb0, (double)FLOAT_80617c0c);
    particle_emitter_set_lifetime_range((double)FLOAT_80617ca0, (double)FLOAT_80617c0c);
    particle_emitter_set_scale((double)scale[0]);

    if (scene_node_get_render_data(0xf) != (void *)0) {
        particle_entity_disable_all();
    }
}

// ===========================================================================
// batch_644  (0x8020231c - 0x80202f98)
// ZCSC 'monitor bank' spectator-cam set-piece + GTSI team anim-event driver.
// Continuation of the STRK/NEXT block above; reuses the STRK 0x6d0 state block
// DAT_806127a4 and its captured-material / active flags DAT_806127ac/DAT_806127a8.
//
// This prop is a bank of security "monitor" screens: a team of GTSI/ZCSC actors
// plays scripted anim clips whose events toggle render states and spawn one-shot
// effects; a set of Z*C* camera actors are the monitor-cam targets chosen by
// game-mode gate; the chosen view is rendered into a scratch render-target that is
// then drawn as TV static (5x3 sprite grid + scanline overlay) on the screen mesh.
//
// Non-matching notes: 4CC ids are class words (GTSI 'GTSI', ORN2, CRW1/CRWM, KARF,
// LDKL, Z1C2..Z4C4). PPC FP-spill frames FUN_803826f0/8038273c (open/close, already
// declared above) and the render-callback spill FUN_803826d8/FUN_80382724 are opaque
// and modelled as plain prologue/epilogue markers. int<->double 0x43300000 bias math
// is written as plain float math. collision_set_flags/render_instance_draw_pass are
// void in generated_decls but the ROM threads their return value as the running
// collision-strip context, so they are routed through file-local typed aliases that
// return the context. spawn_pickup_dispatch is void in decls (its wide-ABI args are
// dropped). The render callbacks / spectator user-data get the function address cast
// to int as the ROM does.
// ---------------------------------------------------------------------------

// raw data symbols new to this batch (file-local)
extern int   DAT_80611748;   // active viewport/framebuffer descriptor (ptr)
extern int  *DAT_80610a20;   // material table base (ptr-to-ptr)
extern int   DAT_80402c44;   // KARF viewport flags word
extern int   DAT_80402c40;   // KARF spectator mode-data block base

// raw FP-spill / restore frames new to this batch (file-local)
extern int  FUN_803826d8(void);   // FP spill frame open (render callback)
extern void FUN_80382724(void);   // FP spill frame close (render callback)
extern void FUN_8038058c(void);   // restore default view/render state (spectator)
extern void FUN_803803e4(void);   // KARF spectator extra setup step
extern void FUN_80358fc4(int a, int b, int c, float *dst, float *src);  // apply impulse to bone

// collision_set_flags is void in generated_decls; the ROM returns the collision
// strip/context handle that the draw passes are threaded through. Route through a
// typed alias that yields the context.
typedef unsigned long long (*ColSetFlagsCtxFn)(int *, unsigned short);
static const ColSetFlagsCtxFn __we644_collision_set_flags_ctx =
    (ColSetFlagsCtxFn)&collision_set_flags;

// render_instance_draw_pass is void in generated_decls; the ROM returns the running
// context. Typed alias.
typedef unsigned long long (*DrawPassCtxFn)(unsigned long long, double, int, int, int);
static const DrawPassCtxFn __we644_render_instance_draw_pass =
    (DrawPassCtxFn)&render_instance_draw_pass;

// ---------------------------------------------------------------------------
// 0x802026ec  zcsc_crw1_anim_state
// Helper for zcsc_anim_event_tick: on the GTSI team actor, when it is playing clip
// 0x73b and an anim event fires, set its render-instance state word (+0x94=0x3a)
// and clear the +0x54c byte.
// ---------------------------------------------------------------------------
void zcsc_crw1_anim_state(int actor)
{
    int *clip;
    int inst;
    if ((actor != 0) && (__we638_anim_event_scan_active() != 0) &&
        (clip = __we638_anim_active_clip_slot(actor), *clip == 0x73b)) {
        inst = *(int *)(actor + 0x7c);
        *(int *)(inst + 0x94) = 0x3a;
        *(unsigned char *)(inst + 0x54c) = 0;
    }
}

// ---------------------------------------------------------------------------
// 0x8020231c  zcsc_anim_event_tick
// Per-frame anim-event driver for the ZCSC/GTSI monitor-bank set-piece.
//   * GTSI team actor  : run zcsc_crw1_anim_state (clip 0x73b).
//   * ZCSC global actor (FUN_8027ea98): clip 0x72f -> state 0; clip 0x735 -> state 8
//     (+ sound slot); clip 0x753 -> state 0x41 then, on the impulse anim event,
//     seed a randomised spark direction (normalized, clamped) and apply it to bone
//     +100 of the actor via FUN_80358fc4; a later event clears the state.
//   * ORN2/ZCSC bot (0x4353435a) on clip 0x776: either notify a pickup slot switch
//     (object_slot_switch_notify) or spawn a pickup (ORN2 0x4f524e32), and set flag
//     0x1000000.
//   * CRW1 shield bot (0x43525731) on clip 0x714: start the shield effect and set a
//     glow flag by anim-event index (NNH1..NNH4 / group SM1G).
// FP-spill wrapped (FUN_803826f0/8038273c). The incoming FP registers that the ROM
// spills are surfaced as the double params.
// ---------------------------------------------------------------------------
void zcsc_anim_event_tick(unsigned long long param_1, double param_2, double param_3,
                          double param_4, double param_5, double param_6, double param_7,
                          unsigned long long param_8)
{
    int gtsi;
    int *actorRec;
    int g;
    int bot;
    int *clip;
    int inst;
    double frand;

    FUN_803826f0();
    gtsi = aiactor_find_player_by_team(0x49475453);   // 'GTSI'
    zcsc_crw1_anim_state(gtsi);

    actorRec = (int *)FUN_8027ea98();
    if ((actorRec != (int *)0) && (g = *actorRec, g != 0)) {
        if ((anim_is_clip_active(g, 0x72f) != 0) && (__we638_anim_event_scan_active() != 0)) {
            actorRec[0x25] = 0;
            *(unsigned char *)(actorRec + 0x155) = 0;
        }
        if ((anim_is_clip_active(g, 0x735) != 0) && (__we638_anim_event_scan_active() != 0)) {
            actorRec[0x25] = 8;
            *(unsigned char *)(actorRec + 0x153) = 0;
            object_sound_slot_accumulate((int)(intptr_t)actorRec, 1, 1000);
        }
        if (anim_is_clip_active(g, 0x753) != 0) {
            if (__we638_anim_event_scan_active() != 0) {
                actorRec[0x25] = 0x41;
                *(unsigned char *)(actorRec + 0x153) = 0;
                *(unsigned char *)((int)actorRec + 0x58d) = 1;
            }
            if (__we638_anim_event_scan_active() != 0) {
                float dir[3];   // local_38/34/30
                frand = game_frand();
                dir[1] = FLOAT_80617c18;
                dir[0] = FLOAT_80617cb4 * (float)((double)FLOAT_80617cb8 * frand - (double)FLOAT_80617c18);
                frand = game_frand();
                dir[2] = (float)((double)FLOAT_80617cb4 *
                                 (double)(float)((double)FLOAT_80617cb8 * frand - (double)FLOAT_80617c18));
                float lenSq = (float)((double)(dir[2] * dir[2]) +
                                      (double)(dir[0] * dir[0] + dir[1] * dir[1]));
                if (FLOAT_80617cbc < lenSq) {
                    float inv = 1.0f / SQRT(lenSq);
                    inv = inv * -(FLOAT_80617c94 * lenSq * inv * inv - FLOAT_80617ca8);
                    float scale = inv * -(FLOAT_80617c94 * lenSq * inv * inv - FLOAT_80617ca8);
                    float s = (float)((double)FLOAT_80617c18 * (double)scale);
                    dir[0] = (float)((double)dir[0] * (double)s);
                    dir[1] = (float)((double)dir[1] * (double)s);
                    dir[2] = (float)((double)dir[2] * (double)s);
                }
                FUN_80358fc4(0, 0, 0, (float *)(actorRec[0xaf] + 100), dir);
            }
            if (__we638_anim_event_scan_active() != 0) {
                actorRec[0x25] = 0;
                *(unsigned char *)((int)actorRec + 0x58d) = 0;
            }
        }
    }

    bot = aiactor_find_bot_by_linked_entity(0x4353435a);   // 'ZCSC'
    if (bot != 0) {
        clip = (int *)anim_active_clip_slot(bot);
        if (*clip == 0x776) {
            if (__we638_anim_event_scan_active() == 0) {
                if (__we638_anim_event_scan_active() != 0) {
                    int inst2 = *(int *)(bot + 0x7c);
                    object_sound_slot_accumulate(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c), 0x21, 0x14);
                    object_slot_switch_notify(param_1, (unsigned long long)param_2,
                                              (unsigned long long)param_3, (unsigned long long)param_4,
                                              (unsigned long long)param_5, (unsigned long long)param_6,
                                              (unsigned long long)param_7, param_8);
                    *(int *)(inst2 + 0x94) = 0x3a;
                    *(unsigned char *)(inst2 + 0x54c) = 0;
                }
            } else {
                FUN_8023c32c(0x4f524e32);   // 'ORN2'
                spawn_pickup_dispatch();
            }
        }
        if ((__we638_anim_event_scan_active() != 0) &&
            (clip = (int *)anim_active_clip_slot(bot), *clip == 0x776)) {
            *(unsigned int *)(bot + 0x28) = *(unsigned int *)(bot + 0x28) | 0x1000000;
        }
    }

    bot = aiactor_find_bot_by_linked_entity(0x43525731);   // 'CRW1'
    if ((bot != 0) && (clip = (int *)anim_active_clip_slot(bot), *clip == 0x714)) {
        character_shield_effect_start(*(int *)(bot + 0x7c) + 0xc68);
        if (__we638_anim_event_scan_active() != 0) {
            glow_flag_set_by_id(0x4e4e4831);        // 'NNH1'
        } else if (__we638_anim_event_scan_active() != 0) {
            glow_flag_set_by_id(0x4e4e4834);        // 'NNH4'
        } else if (__we638_anim_event_scan_active() != 0) {
            glow_flag_set_by_id(0x4e4e4833);        // 'NNH3'
        } else if (__we638_anim_event_scan_active() != 0) {
            glow_flag_set_by_id(0x4e4e4832);        // 'NNH2'
        } else if (__we638_anim_event_scan_active() != 0) {
            glow_flag_set_group(0x534d3147);        // 'SM1G'
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80202750  zcsc_monitor_target_select
// Choose the ZCSC monitor-cam target entity by game-mode gate (FUN_80220f30 with
// 7/8/9/10). For the active bracket, resolve two candidate camera actors (Z*C*
// class words) and, when both are present, rng-tie-break between them; otherwise
// return whichever is present. Returns the chosen entity handle (0 if none).
// ---------------------------------------------------------------------------
static int zcsc_pick(int a, int b)
{
    unsigned int r;
    double biased;
    if (a != 0) {
        if (b == 0) return a;
        r = __we639_rng();
        biased = (double)CONCAT44(0x43300000, r);
        if ((int)(FLOAT_80617c94 + (float)(biased - DOUBLE_80617cc8) / FLOAT_80617cc0) != 1) {
            return a;
        }
    }
    return b;
}

int zcsc_monitor_target_select(void)
{
    if (FUN_80220f30(7) != 0) {
        if (FUN_80220f30(8) != 0) {
            if (FUN_80220f30(9) != 0) {
                if (FUN_80220f30(10) != 0) {
                    return 0;
                }
                return zcsc_pick(FUN_8023c32c(0x34435a32), FUN_8023c32c(0x34435a34)); // 'Z2C4','Z4C4'
            }
            return zcsc_pick(FUN_8023c32c(0x33435a32), FUN_8023c32c(0x34435a34));     // 'Z2C3','Z4C4'
        }
        // bracket 8 clear: primary is the active view actor, tie against Z4C2
        return zcsc_pick(*(int *)(DAT_80611d78 + 0x14), FUN_8023c32c(0x32435a34));    // 'Z4C2'
    }
    return zcsc_pick(FUN_8023c32c(0x31435a32), FUN_8023c32c(0x31435a34));             // 'Z1C2','Z1C4'
}

// ---------------------------------------------------------------------------
// 0x80202998  zcsc_root_translation_zero
// Evaluate the model pose, save the entity root-node translation (+0x64/+0x68/+0x6c
// of the entity) from the pose matrix row (+0x30/+0x34/+0x38), then zero that row so
// the monitor model renders at the render-target origin.
// ---------------------------------------------------------------------------
void zcsc_root_translation_zero(int entity)
{
    float zero;
    model_pose_eval();
    zero = FLOAT_80617c0c;
    int row = *(int *)(*(int *)(entity + 4) + 0x10);
    *(int *)(entity + 0x64) = *(int *)(row + 0x30);
    *(int *)(entity + 0x68) = *(int *)(row + 0x34);
    *(int *)(entity + 0x6c) = *(int *)(row + 0x38);
    *(float *)(row + 0x30) = zero;
    *(float *)(row + 0x34) = zero;
    *(float *)(row + 0x38) = zero;
}

// ---------------------------------------------------------------------------
// 0x80202a1c  zcsc_monitor_render_passes  (graphics)
// Render the ZCSC monitor collision instance in three passes with different blend
// modes (pass0 additive/mat1, pass1 blend/mat1, pass2 blend/mat6). Each pass sets
// the instance render-type word (+0x30), toggles instance flags (set 0x200001,
// clear 0x1000), and draws three sub-passes via render_instance_draw_pass. Restores
// the default blend state on exit.
// ---------------------------------------------------------------------------
void zcsc_monitor_render_passes(unsigned long long param_1, double param_2, int entity)
{
    unsigned long long ctx = __we644_collision_set_flags_ctx(*(int **)(entity + 4), 8);
    for (int pass = 0; pass < 3; pass = pass + 1) {
        if (pass == 1) {
            *(int *)(*(int *)(entity + 4) + 0x30) = 2;
            gx_set_blend_mode(2);
            gx_set_color_update_mode(0);
            gx_set_sub_material_id(1);
        } else if (pass < 1) {
            *(int *)(*(int *)(entity + 4) + 0x30) = 1;
            gx_set_blend_mode(4);
            gx_set_color_update_mode(0);
            gx_set_sub_material_id(1);
        } else {   // pass == 2
            *(int *)(*(int *)(entity + 4) + 0x30) = 4;
            gx_set_blend_mode(2);
            gx_set_color_update_mode(0);
            gx_set_sub_material_id(6);
        }
        *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200001;
        *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) & 0xffffefff;
        *(unsigned short *)(entity + 0x14) = 0;
        ctx = __we644_render_instance_draw_pass(ctx, param_2, *(int *)(entity + 4), 0, 0);
        ctx = __we644_render_instance_draw_pass(ctx, param_2, *(int *)(entity + 4), 1, 0);
        ctx = __we644_render_instance_draw_pass(ctx, param_2, *(int *)(entity + 4), 2, 0);
    }
    *(int *)(*(int *)(entity + 4) + 0x30) = 0;
    gx_set_blend_mode(4);
    gx_set_color_update_mode(0);
    gx_set_sub_material_id(1);
}

// ---------------------------------------------------------------------------
// 0x80202b7c  zcsc_ldkl_set_rendertype
// Resolve the LDKL entity (0x4c4b444c) and force its render-instance type word
// (+0x30) to 2.
// ---------------------------------------------------------------------------
void zcsc_ldkl_set_rendertype(void)
{
    int ldkl = FUN_8023c32c(0x4c4b444c);   // 'LDKL'
    if (ldkl != 0) {
        *(int *)(*(int *)(ldkl + 4) + 0x30) = 2;
    }
}

// ---------------------------------------------------------------------------
// 0x80202bb8  zcsc_spectator_restore_thunk
// Spectator user-data restore callback installed on the player-present path;
// tail-calls FUN_8038058c to restore the default view/render state.
// ---------------------------------------------------------------------------
void zcsc_spectator_restore_thunk(void)
{
    FUN_8038058c();
}

// ---------------------------------------------------------------------------
// 0x80202be4  zcsc_monitor_static_draw  (graphics)
// The monitor render-target callback. Fills the frame with the captured monitor
// material (DAT_806127ac) when valid + active, tiles a 5x3 grid of sprite 0x1b (TV
// static), then overlays a translucent scanline fill (0x7f6f000f). Reads the current
// viewport dims from DAT_80611748 (+0xc width, +0x20 height). FP-spill wrapped.
// ---------------------------------------------------------------------------
void zcsc_monitor_static_draw(void)
{
    unsigned short u0, u1;
    unsigned int uvScale;
    int matId;
    int cellW, cellH;

    FUN_803826d8();
    cellW = *(int *)(DAT_80611748 + 0xc) / 5;
    cellH = *(int *)(DAT_80611748 + 0x20) / 3;
    gx_set_color_update_mode(0);
    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);

    if ((DAT_806127ac != -1) && (DAT_806127a8 != 0)) {
        gx_set_active_material(DAT_806127ac);
        uvScale = (unsigned int)*(unsigned short *)(*DAT_80610a20 + DAT_806127ac * 0x58 + 0x4e) << 4;
        gx_draw_sprite_rect(0, 0, *(unsigned int *)(DAT_80611748 + 0xc),
                            *(unsigned int *)(DAT_80611748 + 0x20), 0, 0, uvScale, uvScale);
    }

    matId = sprite_get_texture_id(0x1b);
    gx_set_active_material(matId);
    matId = sprite_get_texture_id(0x1b);
    u0 = *(unsigned short *)(*DAT_80610a20 + matId * 0x58 + 0x4e);
    matId = sprite_get_texture_id(0x1b);
    u1 = *(unsigned short *)(*DAT_80610a20 + matId * 0x58 + 0x4e);
    gx_set_color_update_mode(0);
    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);

    for (int col = 0; col < 5; col = col + 1) {
        for (int rowi = 0; rowi < 3; rowi = rowi + 1) {
            gx_draw_sprite_rect(cellW * col, cellH * rowi, cellW * (col + 1), cellH * (rowi + 1),
                                0, 0, (unsigned int)u0 << 4, (unsigned int)u1 << 4);
        }
    }

    gx_set_color_update_mode(0);
    gx_set_sub_material_id(2);
    gx_fill_rect_clipped(0, 0, *(unsigned int *)(DAT_80611748 + 0xc),
                         *(unsigned int *)(DAT_80611748 + 0x20), 0x7f6f000f);
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x80202da0  zcsc_monitor_cam_tick
// Per-frame ZCSC monitor spectator-cam. Marks the RT active (DAT_806127a8). If a
// KARF player is present and zoomed (+0x7c/+0xaa0 set) and the match is running,
// bail. Otherwise resolve the CRWM bot + its spawn point; if the spawn point exists
// aim the spectator cam there (mode + flags 0x440000) and install zcsc_monitor_static_draw
// as the RT callback; when a KARF player is present instead, aim its view with the
// restore-thunk callback. When the spectator is active, bind the monitor collision
// strip's render callback (variant on entity class 0x3bd).
// ---------------------------------------------------------------------------
void zcsc_monitor_cam_tick(int entity)
{
    double dt = DOUBLE_80617c10;
    unsigned int *stripTable;
    int karfPlayer;
    int crwm;
    int spawnPt;

    DAT_806127a8 = 1;
    if ((entity_is_valid_ai_target(entity) != 0) && (match_is_not_paused() != 0)) {
        return;
    }
    karfPlayer = aiactor_find_player_by_team(0x4652414b);   // 'KARF'
    DAT_806127a8 = 0;

    if ((karfPlayer == 0) || (*(int *)(karfPlayer + 0x7c) == 0) ||
        (*(int *)(*(int *)(karfPlayer + 0x7c) + 0xaa0) == 0)) {
        crwm    = aiactor_find_bot_by_linked_entity(0x4352574d);   // 'CRWM'
        spawnPt = spawn_point_find_by_entity(0x4352574d);
        if (spawnPt != 0) {
            *(unsigned int *)(spawnPt + 4) = *(unsigned int *)(spawnPt + 4) | 0xa00;
            spectator_set_mode_if_ready(
                (double)(FLOAT_80617ca0 *
                         (FLOAT_80617c18 /
                          (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - dt))),
                spawnPt);
            spectator_set_flags(0, 0x440000);
            spectator_set_user_data((int)(intptr_t)&zcsc_monitor_static_draw);
        }
        if (crwm == 0) {
            DAT_806127a8 = 1;
        }
    } else {
        DAT_80402c44 = DAT_80402c44 | 0xa00;
        spectator_set_mode(
            (double)(FLOAT_80617ca0 *
                     (FLOAT_80617c18 /
                      (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617c10))),
            (int)(intptr_t)&DAT_80402c40, karfPlayer);
        spectator_set_flags(0, 0x440000);
        FUN_803803e4();
        spectator_set_user_data((int)(intptr_t)&zcsc_spectator_restore_thunk);
    }

    if (spectator_is_active()) {
        stripTable = (unsigned int *)collision_alloc_strip_table(*(int **)(entity + 4));
        if (*(short *)(entity + 0x94) == 0x3bd) {
            DAT_806127ac = stripTable[4];
        } else {
            DAT_806127ac = *stripTable;
        }
        if (*(short *)(entity + 0x94) == 0x3bd) {
            collision_set_flags(*(int **)(entity + 4), 0x80);
            collision_set_shape_params(*(int **)(entity + 4), 0xe, 0, 0xff, 0);
            stripTable[4] = spectator_get_render_callback();
        } else {
            *stripTable = spectator_get_render_callback();
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80202f98  zcsc_monitor_object_init
// ZCSC monitor object init: bind a 0x48 state block (at DAT_806127a4+0x3f4) to the
// entity (+0x7c), zero it, seed the swing/anim floats, start the scaled anim, set
// instance flags |0x60, and register the entity for update lists 3 (0x80203638) and
// 2 (0x8020358c).
// ---------------------------------------------------------------------------
void zcsc_monitor_object_init(int entity)
{
    int base = DAT_806127a4;
    unsigned int *stateBlock = (unsigned int *)(DAT_806127a4 + 0x3f4);
    *(unsigned int **)(entity + 0x7c) = stateBlock;
    memset_wrapper((int)stateBlock, 0, 0x48);
    *stateBlock = 0;
    *(float *)(base + 0x3f8) = FLOAT_80617cd0;
    *(float *)(base + 0x3fc) = FLOAT_80617c0c;
    *(float *)(base + 0x438) = FLOAT_80617c0c;
    anim_play_scaled((unsigned long long)(unsigned int)FLOAT_80617c0c, (double)FLOAT_80617cd4);
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x60;
    entity_register_for_update(entity, 3, -0x7fdfc9c8);
    entity_register_for_update(entity, 2, -0x7fdfca74);
}

// ===========================================================================
// batch_645  (0x80203044 - 0x80203fb0)
// ZCSC monitor set-piece continued: the pan/tilt "swivel head" sub-object and its
// four SMD1..SMD4 sub-camera sub-meshes, the player-controlled monitor console
// (menu-state pan/tilt with a servo loop), the articulated monitor arm state
// machine, plus the 'SLVE' glow/status-light prop. Appends to the ZCSC block above
// and reuses the STRK 0x6d0 state block DAT_806127a4 (+0x3f4.. monitor sub-state,
// +0x43c.. the 4-slot SMD handle table, +0x46c.. the persistent servo sound, +0x470
// pan smoothing).
//
// Non-matching notes: 4CC ids (ZCSC/PINC/SMD1..SMD4/SLVE class words); PPC FP-spill
// frames FUN_803826e8/FUN_80382734 and FUN_803826f0/FUN_8038273c (open/close) are
// opaque; int<->double 0x43300000 bias math is plain float math; the 0x800-entry
// sine LUT DAT_80583228 is indexed &0x7ff with a +0x200 quarter-phase. Helpers that
// are void in generated_decls but whose ROM return the code threads (scene_raycast,
// switch_slot_reset, input_poll_and_process, mtx4_set_translation's 3-arg current-
// matrix form, damage_dispatch_to_entity's wide FP ABI) are routed through file-local
// typed aliases (reusing __we639_scene_raycast / __we641_damage_dispatch_to_entity
// from earlier batches). Update-list callback addresses are the ROM's negative
// sign-extended constants written as &function.
// ---------------------------------------------------------------------------

// raw data / helper symbols new to this batch (file-local)
extern float FLOAT_806127a0;    // SMD gimbal pan accumulator (global)
extern int   DAT_80611c24;      // entity handle-table base (0x2e8 stride)
extern int   DAT_80456df0;      // monitor menu-state data block
extern int   FUN_80390324(double x);   // floor/truncate-to-int-as-double helper
extern void  FUN_8026bd98(void);       // SMD sub-cam render callback (installed by ptr)

// batch_645 tuning constants (file-local raw externs)
extern float FLOAT_80617cd8;    // pan snap unit
extern float FLOAT_80617cdc;    // pan wrap lower bound (-pi-ish)
extern float FLOAT_80617ce0;    // tilt input gain
extern float FLOAT_80617ce4;    // tilt clamp lo
extern float FLOAT_80617ce8;    // tilt clamp hi
extern float FLOAT_80617cec;    // servo sound range gain
extern float FLOAT_80617cf0;    // servo/loop sound pitch
extern float FLOAT_80617cf4;    // anim play speed
extern float FLOAT_80617cf8;    // arm-tip raycast length
extern float FLOAT_80617cfc;    // arm extend floor
extern float FLOAT_80617d00;    // angular-velocity decay
extern float FLOAT_80617d04;    // pan wrap span (2pi-ish)
extern float FLOAT_80617d08;    // pan rate deadzone
extern float FLOAT_80617d0c;    // pan ease amplitude
extern float FLOAT_80617d10;    // sine LUT index scale
extern float FLOAT_80617d14;    // pan ease rate
extern float FLOAT_80617d18;    // SMD yaw gain
extern float FLOAT_80617d1c;    // SLVE pulse amplitude
extern float FLOAT_80617d20;    // SLVE pulse bias
extern float FLOAT_80617d24;    // SLVE bob amplitude
extern float FLOAT_80617d28;    // SLVE bob divisor
extern float FLOAT_80617d2c;    // proximity distance divisor

extern float DAT_803dfd34; extern float DAT_803dfd38; extern float DAT_803dfd3c;  // monitor node pos offset
extern float DAT_803dfd40; extern float DAT_803dfd44; extern int   DAT_803dfd48;  // monitor aim base (z as raw word)
extern int   DAT_803dfd4c; extern int   DAT_803dfd50; extern int   DAT_803dfd54;  // servo sound colour words
extern unsigned int DAT_803dfd58; extern unsigned int DAT_803dfd5c;               // SMD1/SMD2 bone-def words
extern unsigned int DAT_803dfd60; extern unsigned int DAT_803dfd64;               // SMD3/SMD4 bone-def words

// input_poll_and_process is void in decls; the ROM returns the analog-stick deflection.
typedef double (*InputPollFn)(void);
static const InputPollFn __we645_input_poll = (InputPollFn)&input_poll_and_process;

// switch_slot_reset is void in decls; the ROM returns the fp/context handle threaded
// into damage_dispatch_to_entity.
typedef double (*SwitchSlotResetFn)(int);
static const SwitchSlotResetFn __we645_switch_slot_reset = (SwitchSlotResetFn)&switch_slot_reset;

// mtx4_set_translation's decls form is (float*,x,y,z); this TU uses the 3-arg
// "set current matrix translation" ROM form.
typedef void (*Mtx4SetTransCurFn)(double, double, double);
static const Mtx4SetTransCurFn __we645_mtx4_set_translation_cur = (Mtx4SetTransCurFn)&mtx4_set_translation;

// ---------------------------------------------------------------------------
// 0x80203044  zcsc_swivel_object_init
// Init the second ZCSC monitor sub-object (the pan/tilt "swivel head"): set its
// render-instance material (alpha 1), zero the +0xcc/+0xd0 angle/rate, set instance
// flags |0x60, register for update lists 3 (render zcsc_smd_gimbal_tick) and 2
// (physics zcsc_swivel_rotate_y), and clear the 4-slot SMD handle table.
// ---------------------------------------------------------------------------
void zcsc_swivel_object_init(int entity)
{
    render_instance_set_material((double)FLOAT_80617c0c, *(int **)(entity + 4), 1);
    *(float *)(entity + 0xcc) = FLOAT_80617c0c;
    *(float *)(entity + 0xd0) = FLOAT_80617c0c;
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x60;
    entity_register_for_update(entity, 3, -0x7fdfc544);   // 0x80203abc render
    entity_register_for_update(entity, 2, -0x7fdfc588);   // 0x80203a78 physics
    memset_wrapper(DAT_806127a4 + 0x43c, 0, 0x10);
}

// ---------------------------------------------------------------------------
// 0x80203a78  zcsc_swivel_rotate_y
// Physics update: set the bone-palette root matrix to a Y-rotation by the +0xcc
// angle and mark the instance dirty.
// ---------------------------------------------------------------------------
void zcsc_swivel_rotate_y(int entity)
{
    mtx4_set_rotation_y((double)*(float *)(entity + 0xcc), *(float **)(*(int *)(entity + 4) + 0x10));
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200000;
}

// ---------------------------------------------------------------------------
// 0x8020358c  zcsc_monitor_bone_update
// Render update-list callback for the monitor object: eval the model pose, transform
// the entity offset (+4) into a translation, set the current-matrix translation, then
// post-multiply every bone-palette matrix (count at +4 of the node record) by the
// accumulated transform. FP-spill wrapped.
// ---------------------------------------------------------------------------
void zcsc_monitor_bone_update(void)
{
    float local[3];
    float palette[23];
    int entity = FUN_803826f0();
    int inst   = *(int *)(entity + 0x7c);
    int count  = *(int *)(*(int *)(*(int *)(entity + 4) + 4) + 4);

    model_pose_eval();
    local[0] = FLOAT_80617c0c;
    local[1] = FLOAT_80617c0c;
    local[2] = *(float *)(inst + 4);
    mtx4_transform_vector_alias(*(float **)(*(int *)(entity + 4) + 0x10), local);
    __we645_mtx4_set_translation_cur((double)local[0], (double)local[1], (double)local[2]);

    int off = count << 6;
    while (count != 0) {
        count = count - 1;
        off = off - 0x40;
        float *m = (float *)(*(int *)(*(int *)(entity + 4) + 0x10) + off);
        mtx4_multiply(m, palette, m);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x802030d0  zcsc_monitor_input_tick
// Player-controlled monitor pan/tilt tick (installed as the menu-state update cb).
// On a large stick deflection with the recentre flag (+0x424) clear, play a 0x63b
// servo click and snap the pan target (+0x434). While the sim substep counter
// (DAT_806108e4) is nonzero: integrate pan velocity (+0x418/+0x41c) clamped to
// [FLOAT_80617c1c, FLOAT_80617c18], step +0x3f8 tilt toward the stick, clamp, and
// drive a 0x63c 3D servo-motor loop by angular speed. Then build the local matrix,
// place the render node (+8/+0xc/+0x10) at the bone, and normalize the aim vector
// (+0x38..+0x40). FP-spill wrapped.
// ---------------------------------------------------------------------------
void zcsc_monitor_input_tick(void)
{
    float src[3];       // local_ec/e8/e4  (position offset)
    float dst[3];       // local_f8/f4/f0  (aim base)
    int   sndParams[7]; // local_e0
    int   sndCol[3];    // local_c4/c0/bc
    unsigned char sndFlag;
    float palette[16];  // afStack_b0
    unsigned int frameNeg;
    float node[3];      // local_104/100/fc
    double stick, dt, tiltGain;
    unsigned long long fpctx = FUN_803826e8();
    int nodeRec = (int)((unsigned long long)fpctx >> 0x20);
    int blk = DAT_806127a4;

    src[0] = DAT_803dfd34; src[1] = DAT_803dfd38; src[2] = DAT_803dfd3c;
    dst[0] = DAT_803dfd40; dst[1] = DAT_803dfd44; *(int *)&dst[2] = DAT_803dfd48;

    int pinc = FUN_8023c32c(0x50494e43);   // 'PINC'
    stick = -__we645_input_poll();

    if ((*(int *)(blk + 0x424) == 0) && ((double)FLOAT_80617c98 < ABS(stick))) {
        sound_init_play_params((int)sndParams);
        sndFlag = 1;
        sndParams[0] = 0x63b;
        sndCol[0] = DAT_803dfd4c; sndCol[1] = DAT_803dfd50; sndCol[2] = DAT_803dfd54;
        (void)sndFlag; (void)sndCol;
        sound_play_request(sndParams);
        double snap = (double)(FLOAT_80617cd8 *
                               (float)((unsigned int)(float)stick & 0x80000000 | 0x3f800000) +
                               *(float *)(blk + 0x434));
        double w = FUN_80390324((double)((float)(snap - (double)FLOAT_80617cdc) / FLOAT_80617c48));
        *(float *)(blk + 0x434) = -(float)((double)FLOAT_80617c48 * (double)(float)w - snap);
        *(int *)(blk + 0x424) = 1;
    }

    if (DAT_806108e4 != 0) {
        frameNeg = DAT_806109c0 ^ 0x80000000;
        double baseTilt = (double)*(float *)(blk + 0x3f8);
        dt = (double)(FLOAT_806108ec *
                      (FLOAT_80617c18 / (float)((double)CONCAT44(0x43300000, frameNeg) - DOUBLE_80617c10)));
        double dtScaled = (double)(float)((double)FLOAT_80617c44 * dt);
        if (*(int *)(blk + 0x3f4) == 0) {
            tiltGain = -__we645_input_poll();
        } else {
            tiltGain = (double)FLOAT_80617c0c;
        }
        unsigned int buttons = input_get_buttons_pressed(*(int *)fpctx);
        *(unsigned int *)(blk + 0x420) = *(unsigned int *)(DAT_80611d74 + 0x20) & buttons;

        float v = (float)((double)(float)((double)FLOAT_80617ce0 * tiltGain) * dt);
        *(float *)(blk + 0x418) = *(float *)(blk + 0x418) + v;
        *(float *)(blk + 0x41c) = *(float *)(blk + 0x41c) + v;

        float a = *(float *)(blk + 0x418);
        float c = (FLOAT_80617c18 < a) ? FLOAT_80617c18 : a;
        float r = FLOAT_80617c1c;
        if ((FLOAT_80617c1c <= c) && (FLOAT_80617c18 < a)) r = FLOAT_80617c18; else if (FLOAT_80617c1c <= c) r = a;
        *(float *)(blk + 0x418) = r;

        a = *(float *)(blk + 0x41c);
        c = (FLOAT_80617c18 < a) ? FLOAT_80617c18 : a;
        r = FLOAT_80617c1c;
        if ((FLOAT_80617c1c <= c) && (FLOAT_80617c18 < a)) r = FLOAT_80617c18; else if (FLOAT_80617c1c <= c) r = a;
        *(float *)(blk + 0x41c) = r;

        scalar_step_toward_snap(tiltGain, (double)(float)((double)FLOAT_80617cb8 * dt),
                                (float *)(DAT_806127a4 + 0x470));
        *(float *)(blk + 0x3f8) = (float)(dtScaled * (double)*(float *)(DAT_806127a4 + 0x470) +
                                          (double)*(float *)(blk + 0x3f8));
        a = *(float *)(blk + 0x3f8);
        c = (FLOAT_80617ce8 < a) ? FLOAT_80617ce8 : a;
        r = FLOAT_80617ce4;
        if ((FLOAT_80617ce4 <= c) && (FLOAT_80617ce8 < a)) r = FLOAT_80617ce8; else if (FLOAT_80617ce4 <= c) r = a;
        *(float *)(blk + 0x3f8) = r;

        double delta = ABS((double)(float)((double)(float)((double)*(float *)(blk + 0x3f8) - baseTilt) / dtScaled));
        double spd = math_sqrt(delta);
        __we638_sound_update_3d_source((double)(float)((double)FLOAT_80617cec * spd), (double)FLOAT_80617cf0,
                               (double)(float)((double)FLOAT_80617c94 * delta + (double)FLOAT_80617c94),
                               DAT_806127a4 + 0x46c, (void *)(pinc + 100), 0x63c, 1);
    }

    entity_build_local_matrix(palette, (void *)pinc, 1, 0);
    int m = *(int *)(*(int *)(pinc + 4) + 0x10);
    node[0] = *(float *)(m + 0x30);
    node[2] = *(float *)(m + 0x38);
    node[1] = *(float *)(m + 0x34) + *(float *)(blk + 0x3fc);
    mtx4_transform_point_alias(palette, node);

    *(float *)(nodeRec + 8)   = node[0] + src[0];
    *(float *)(nodeRec + 0xc) = node[1] + src[1];
    *(float *)(nodeRec + 0x10)= node[2] + src[2];
    *(float *)(nodeRec + 0x38) = FLOAT_80617c6c * *(float *)(blk + 0x418) + dst[0];
    *(float *)(nodeRec + 0x3c) = FLOAT_80617c34 * *(float *)(blk + 0x41c) + dst[1];
    *(int *)(nodeRec + 0x40)   = *(int *)&dst[2];

    float ax = *(float *)(nodeRec + 0x38);
    float lenSq = *(float *)(nodeRec + 0x40) * *(float *)(nodeRec + 0x40) +
                  ax * ax + *(float *)(nodeRec + 0x3c) * *(float *)(nodeRec + 0x3c);
    if (FLOAT_80617cbc < lenSq) {
        float inv = 1.0f / SQRT(lenSq);
        inv = inv * -(FLOAT_80617c94 * lenSq * inv * inv - FLOAT_80617ca8);
        float s = FLOAT_80617c18 * inv * -(FLOAT_80617c94 * lenSq * inv * inv - FLOAT_80617ca8);
        *(float *)(nodeRec + 0x38) = ax * s;
        *(float *)(nodeRec + 0x3c) = *(float *)(nodeRec + 0x3c) * s;
        *(float *)(nodeRec + 0x40) = *(float *)(nodeRec + 0x40) * s;
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80203638  zcsc_monitor_arm_tick
// Physics update: state machine for the monitor's articulated arm (+0x7c state,
// word[0]). Deploy (state 1: play 0x63b open, then on clips 0x707/0x709/0x70a
// advance/play 0x642/0x644; on 0x70a raycast from the arm tip and
// damage_dispatch_to_entity + FUN_80221074 payload), retract (state<1 -> 1), settle
// (state<3: clip 0x708 -> 0, else play 0x641). Ramps the extend factor (word[2]),
// decays angular velocity (word[9]/word[10]), and drives a 0x643 3D loop. FP-spill
// wrapped.
// ---------------------------------------------------------------------------
void zcsc_monitor_arm_tick(void)
{
    float sweep[3];   // local_16c/168/164
    float axis[3];    // local_178/174/170
    float hitPos[3];  // afStack_184
    float hitNormal[3]; // afStack_190
    int   snd[7];     // local_100/130/160
    int   sndPos[3];
    unsigned char sndFlag;
    int   evt[16];    // local_90 block
    float palette[16]; // afStack_d0

    int entity = FUN_803826f0();
    unsigned int frameNeg = DAT_806109c0 ^ 0x80000000;
    int *state = *(int **)(entity + 0x7c);
    FLOAT_806127a0 = FLOAT_806127a0 * FLOAT_80617c44;
    double dt = (double)(float)((double)FLOAT_806108ec *
                                (double)(FLOAT_80617c18 /
                                         (float)((double)CONCAT44(0x43300000, frameNeg) - DOUBLE_80617c10)));
    int s = *state;

    if (s == 1) {
        if (state[0xb] == 0) {
            *state = 2;
            anim_play_scaled((double)FLOAT_80617cf4, (double)FLOAT_80617cd4);
            sound_init_play_params((int)snd);
            sndFlag = 1; snd[0] = 0x644;
            snd[4] = *(int *)(entity + 100); snd[5] = *(int *)(entity + 0x68); snd[6] = *(int *)(entity + 0x6c);
            (void)sndFlag;
            sound_play_request(snd);
        } else {
            if (anim_is_clip_active(entity, 0x70a) != 0) {
                FLOAT_806127a0 = FLOAT_806127a0 + FLOAT_80617cb4;
                sweep[0] = FLOAT_80617c34; sweep[1] = FLOAT_80617c0c; sweep[2] = FLOAT_80617c0c;
                int mnode = *(int *)(*(int *)(entity + 4) + 0x10);
                axis[0] = FLOAT_80617cf8 * *(float *)(mnode + 0x200);
                axis[1] = FLOAT_80617cf8 * *(float *)(mnode + 0x204);
                axis[2] = FLOAT_80617cf8 * *(float *)(mnode + 0x208);
                mtx4_transform_point_alias((float *)(mnode + 0x200), sweep);
                entity_build_local_matrix(palette, (void *)entity, 1, 0);
                mtx4_transform_point_alias(palette, sweep);
                mtx4_transform_vector_alias(palette, axis);
                int hit = __we639_scene_raycast(*(int *)(entity + 0x34), sweep, axis, 0,
                                                hitPos, hitNormal, (int *)0, 0x2000070, 0, (int *)0);
                if (hit != 0) {
                    double fpctx = __we645_switch_slot_reset((int)evt);
                    evt[0] = hit;
                    evt[2] = entity;
                    evt[3] = 4;      // local_78
                    evt[5] = 2;      // local_6c
                    *(float *)&evt[7] = FLOAT_80617c90;   // local_70
                    evt[9]  = (int)hitPos;   // local_68
                    evt[10] = (int)hitNormal;// local_64
                    evt[1]  = *(int *)(DAT_80611d78 + 0x14);  // local_8c
                    __we641_damage_dispatch_to_entity(fpctx, (double)FLOAT_80617c34, (double)FLOAT_806127a0,
                                                      (double)FLOAT_806108ec, 0, 0, 0, 0, evt);
                    FUN_80221074((double)FLOAT_80617c0c);
                }
            } else if (anim_is_clip_active(entity, 0x709) != 0) {
                if (anim_active_slot_userflags(*(int *)(entity + 4)) != 0) {
                    anim_play_scaled((double)FLOAT_80617cf4, (double)FLOAT_80617cd4);
                }
            } else if ((anim_is_clip_active(entity, 0x707) != 0) &&
                       (anim_active_slot_userflags(*(int *)(entity + 4)) != 0)) {
                anim_play_scaled((double)FLOAT_80617cf4, (double)FLOAT_80617cd4);
                sound_init_play_params((int)snd);
                sndFlag = 1; snd[0] = 0x642;
                snd[4] = *(int *)(entity + 100); snd[5] = *(int *)(entity + 0x68); snd[6] = *(int *)(entity + 0x6c);
                (void)sndFlag;
                sound_play_request(snd);
            }
            float ext = -(float)((double)FLOAT_80617cb4 * dt - (double)(float)state[2]);
            if (ext < FLOAT_80617cfc) ext = FLOAT_80617cfc;
            state[2] = (int)ext;
        }
    } else if (s < 1) {
        if ((-1 < s) && (state[0xb] != 0)) {
            *state = 1;
            anim_play_scaled((double)FLOAT_80617cf4, (double)FLOAT_80617cd4);
        }
    } else if (s < 3) {
        if (state[0xb] == 0) {
            if ((anim_is_clip_active(entity, 0x708) != 0) &&
                (anim_active_slot_userflags(*(int *)(entity + 4)) != 0)) {
                *state = 0;
            }
        } else {
            *state = 1;
            anim_play_scaled((double)FLOAT_80617cf4, (double)FLOAT_80617cd4);
            sound_init_play_params((int)snd);
            sndFlag = 1; snd[0] = 0x641;
            snd[4] = *(int *)(entity + 100); snd[5] = *(int *)(entity + 0x68); snd[6] = *(int *)(entity + 0x6c);
            (void)sndFlag;
            sound_play_request(snd);
        }
    }

    if (state[0xb] == 0) {
        float ext = (float)((double)FLOAT_80617c34 * dt + (double)(float)state[2]);
        if (FLOAT_80617c0c < ext) ext = FLOAT_80617c0c;
        state[2] = (int)ext;
    }
    (void)sndPos;
    state[0xb] = 0;
    state[9]  = (int)((float)state[9]  * FLOAT_80617d00);
    state[10] = (int)((float)state[10] * FLOAT_80617d00);
    __we638_sound_update_3d_source((double)(FLOAT_806127a0 - FLOAT_80617c34), (double)FLOAT_80617cf0,
                           (double)FLOAT_80617c18, entity + 0x22c,
                           (void *)(entity + 100), 0x643, 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80203abc  zcsc_smd_gimbal_tick
// Render update: eases the swivel-head pan (+0xd0 rate via the sine LUT, or snaps to
// rest on a direction flip / recentre), integrates +0xcc angle, then for each of the
// 4 SMD1..SMD4 sub-camera sub-meshes resolves/re-resolves its handle (installing the
// render cb FUN_8026bd98 on first bind), transforms the bone anchor, and aims it via
// math_atan2 + FUN_80390324 wrap into a Y-rotation (+0x70/+0xc4) with its world
// position written. FP-spill wrapped.
// ---------------------------------------------------------------------------
void zcsc_smd_gimbal_tick(void)
{
    float palette[17];   // afStack_74
    unsigned int slotDef[4];  // local_84
    int slotEnt[4];      // local_a0
    int slotBone[4];     // local_b0
    float pos[3];        // local_90/8c/88

    int entity = FUN_803826e8();
    int block  = DAT_806127a4;
    slotDef[0] = DAT_803dfd58; slotDef[1] = DAT_803dfd5c; slotDef[2] = DAT_803dfd60; slotDef[3] = DAT_803dfd64;

    float delta = *(float *)(DAT_806127a4 + 0x434) - *(float *)(entity + 0xcc);
    for (; delta <= FLOAT_80617cdc; delta = delta + FLOAT_80617c48) { }
    for (; FLOAT_80617d04 < delta; delta = delta - FLOAT_80617c48) { }

    float dt = FLOAT_806108ec * (FLOAT_80617c18 /
               (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617c10));

    if (((FLOAT_80617d08 < ABS((double)delta)) || (ABS((double)*(float *)(entity + 0xd0)) <= FLOAT_80617c0c)) ||
        ((float)((unsigned int)delta & 0x80000000 | 0x3f800000) ==
         (float)((unsigned int)*(float *)(entity + 0xd0) & 0x80000000 | 0x3f800000))) {
        if (FLOAT_80617c0c < ABS((double)delta)) {
            unsigned int idx = (unsigned int)(FLOAT_80617d10 * FLOAT_80617d14 * delta + FLOAT_80617c94);
            *(float *)(entity + 0xd0) = FLOAT_80617d0c * (&DAT_80583228)[idx & 0x7ff];
            if (ABS((double)*(float *)(entity + 0xd0)) < FLOAT_80617d08) {
                *(float *)(entity + 0xd0) = FLOAT_80617d08 *
                    (float)((unsigned int)delta & 0x80000000 | 0x3f800000);
            }
            *(float *)(entity + 0xcc) = *(float *)(entity + 0xd0) * dt + *(float *)(entity + 0xcc);
            *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x82;
        }
    } else {
        *(float *)(entity + 0xd0) = FLOAT_80617c0c;
        *(int *)(entity + 0xcc) = *(int *)(block + 0x434);
        *(int *)(block + 0x424) = 0;
    }

    entity_build_local_matrix(palette, (void *)entity, 1, 0);
    for (int i = 0; i < 4; i = i + 1) {
        int off = i * 4;
        unsigned int h = *(unsigned int *)(DAT_806127a4 + off + 0x43c);
        int ent;
        if (h == 0) {
            ent = 0;
        } else {
            ent = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int *)(ent + 0x2c) != h) {
                ent = 0;
                *(int *)(DAT_806127a4 + off + 0x43c) = 0;
            }
        }
        slotEnt[i] = ent;
        if (ent == 0) {
            ent = FUN_8023c32c((int)(char)i + 0x31 | 0x53444d00);   // 'SMD' + '1'+i
            slotEnt[i] = ent;
            if (ent != 0) {
                *(int *)(DAT_806127a4 + off + 0x43c) = *(int *)(ent + 0x2c);
                *(void (**)(void))(*(int *)(ent + 0x7c) + 0x74) = FUN_8026bd98;
            }
        } else {
            int inst = *(int *)(entity + 4);
            int *bp = *(int **)(inst + 4);
            slotBone[i] = bp[*bp * -0x30 + ((int)slotDef[i] >> 0x10) * 0x30 + 0x1b] +
                          (slotDef[i] & 0xffff) * 0x18;
            float *pb = (float *)slotBone[i];
            pos[0] = pb[0]; pos[1] = pb[1]; pos[2] = pb[2];
            mtx4_transform_point_alias(*(float **)(inst + 0x10), pos);
            double ang = (double)(float)(double)math_atan2((double)pos[0], (double)pos[2]);
            double w = FUN_80390324((double)((float)(ang - (double)FLOAT_80617cdc) / FLOAT_80617c48));
            int cam = slotEnt[i];
            float yaw = (FLOAT_80617d18 * -(float)((double)FLOAT_80617c48 * (double)(float)w - ang)) /
                        FLOAT_80617d04;
            *(float *)(cam + 0x70) = yaw;
            *(float *)(cam + 0xc4) = yaw;
            mtx4_transform_point_alias(palette, pos);
            *(float *)(cam + 100)  = pos[0];
            *(float *)(cam + 0x68) = pos[1];
            *(float *)(cam + 0x6c) = pos[2];
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80203e48  zcsc_smd_handle_remap
// Entity-destroy fixup: walk the 4-slot SMD handle table (DAT_806127a4+0x43c) and,
// for any slot whose currently-resolved entity == param_1 (being freed), rebind it to
// param_2's handle (+0x2c).
// ---------------------------------------------------------------------------
void zcsc_smd_handle_remap(int freed, int replacement)
{
    for (int i = 0; i < 4; i = i + 1) {
        int off = i * 4;
        unsigned int h = *(unsigned int *)(DAT_806127a4 + off + 0x43c);
        int ent;
        if (h == 0) {
            ent = 0;
        } else {
            ent = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int *)(ent + 0x2c) != h) {
                ent = 0;
                *(int *)(DAT_806127a4 + off + 0x43c) = 0;
            }
        }
        if (ent == freed) {
            *(int *)(DAT_806127a4 + off + 0x43c) = *(int *)(replacement + 0x2c);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80203eb8  zcsc_monitor_sound_reset
// Stop the monitor's persistent 3D sound handle (+0x46c) if active and reset it to
// -1; clear the pan-smoothing accumulator (+0x470). Menu-exit callback.
// ---------------------------------------------------------------------------
void zcsc_monitor_sound_reset(void)
{
    if (-1 < *(int *)(DAT_806127a4 + 0x46c)) {
        sound_stop_handle(*(unsigned int *)(DAT_806127a4 + 0x46c));
        *(int *)(DAT_806127a4 + 0x46c) = -1;
    }
    *(float *)(DAT_806127a4 + 0x470) = FLOAT_80617c0c;
}

// ---------------------------------------------------------------------------
// 0x80203f00  zcsc_monitor_enter_menu
// When a player triggers the monitor console: resolve the controlling player (via the
// trigger's +0x44/+0x150 link or the primary view actor) and, if in a valid non-0x100
// state, run one input tick then enter the monitor menu state.
// ---------------------------------------------------------------------------
void zcsc_monitor_enter_menu(int trigger)
{
    int *link;
    int player;
    if ((*(int *)(trigger + 0x44) == 0) ||
        (link = *(int **)(*(int *)(trigger + 0x44) + 0x150), link == (int *)0)) {
        player = *(int *)(DAT_80611d78 + 0x14);
    } else {
        player = *(int *)(DAT_80611d78 + *link * 0x141c + 0x14);
    }
    if ((player != 0) && (*(int *)(player + 0x150) != 0) &&
        (*(int *)(*(int *)(player + 0x150) + 0x114) != 0x100)) {
        zcsc_monitor_input_tick();
        player_enter_menu_state(*(unsigned int *)(player + 0x150),
                                (unsigned int)(unsigned int)(intptr_t)&DAT_80456df0,
                                (unsigned int)(intptr_t)&zcsc_monitor_input_tick,
                                2, 0x1b,
                                (unsigned int)(intptr_t)&zcsc_monitor_sound_reset);
    }
}

// ---------------------------------------------------------------------------
// 0x80203fb0  zcsc_slve_glow_tick
// Update for the 'SLVE' glow/status-light prop. Advances a phase counter (+0x1a0) and
// samples the sine LUT twice (different rates) for a brightness pulse (+0x1a4) and a
// vertical bob (+0x64 from +0xa8 base), sign-flipped for the SLVE class. Outside the
// FUN_80220f30(4) gate, sets a proximity intensity (+0xd0) from the squared distance
// to the nearest of up to 2 players; else forces full intensity.
// ---------------------------------------------------------------------------
void zcsc_slve_glow_tick(int entity)
{
    float sign = FLOAT_80617c18;
    if (*(int *)(entity + 0x60) == 0x534c5645) {   // 'SLVE'
        sign = FLOAT_80617c1c;
    }
    float ph = FLOAT_80617c48 *
               (float)((double)CONCAT44(0x43300000, *(unsigned int *)(entity + 0x1a0) ^ 0x80000000) - DOUBLE_80617c10);
    float bob = FLOAT_80617c18 +
                (&DAT_80583228)[((int)(FLOAT_80617d10 *
                    (ph / (FLOAT_80617ca0 * (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617c10)))
                    + FLOAT_80617c94) + 0x200) & 0x7ff];
    float pulse = FLOAT_80617d1c *
                  (&DAT_80583228)[((int)(FLOAT_80617d10 *
                      (ph / (FLOAT_80617cb8 * (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617c10)))
                      + FLOAT_80617c94) + 0x200) & 0x7ff] - FLOAT_80617d20;

    *(unsigned int *)(entity + 0x1a0) = *(unsigned int *)(entity + 0x1a0) + DAT_806108e4;
    *(float *)(entity + 0x1a4) = sign * pulse;
    *(float *)(entity + 100) = *(float *)(entity + 0xa8) + (FLOAT_80617d24 * sign * bob) / FLOAT_80617d28;

    if (FUN_80220f30(4) == 0) {
        float dx = *(float *)(entity + 100) - *(float *)(DAT_80611d78 + 0xa8);
        float dz = *(float *)(entity + 0x6c) - *(float *)(DAT_80611d78 + 0xb0);
        float d = (dx * dx + dz * dz) / FLOAT_80617d2c;
        float c = (FLOAT_80617c18 < d) ? FLOAT_80617c18 : d;
        float r = FLOAT_80617c0c;
        if ((FLOAT_80617c0c <= c) && (FLOAT_80617c18 < d)) r = FLOAT_80617c18; else if (FLOAT_80617c0c <= c) r = d;
        *(float *)(entity + 0xd0) = r;

        if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
            dx = *(float *)(entity + 100) - *(float *)(DAT_80611d78 + 0x14c4);
            dz = *(float *)(entity + 0x6c) - *(float *)(DAT_80611d78 + 0x14cc);
            float d2 = (dx * dx + dz * dz) / FLOAT_80617d2c;
            float c2 = (FLOAT_80617c18 < d2) ? FLOAT_80617c18 : d2;
            float r2 = FLOAT_80617c0c;
            if ((FLOAT_80617c0c <= c2) && (FLOAT_80617c18 < d2)) r2 = FLOAT_80617c18; else if (FLOAT_80617c0c <= c2) r2 = d2;
            if (r2 <= *(float *)(entity + 0xd0)) {
                float c3 = (FLOAT_80617c18 < d2) ? FLOAT_80617c18 : d2;
                float r3 = FLOAT_80617c0c;
                if ((FLOAT_80617c0c <= c3) && (FLOAT_80617c18 < d2)) r3 = FLOAT_80617c18; else if (FLOAT_80617c0c <= c3) r3 = d2;
                *(float *)(entity + 0xd0) = r3;
            }
        }
    } else {
        *(float *)(entity + 0xd0) = FLOAT_80617c18;
    }
}

// ===========================================================================
// batch_646  (0x80204254 - 0x80205044)
// ZCSC monitor set-piece FX layer: the electric-arc/lightning render, the 20-slot
// spark particle pool (tick + projected render), the steam-vent submesh emitter, a
// smoke-emitter toggle, an ambient 3D loop, a glow flicker, the XFKY render-type
// helper, and the 0x40x0x40 "scanline" monitor render-target drawer. Appends to the
// ZCSC block; reuses the STRK 0x6d0 block DAT_806127a4 (+0x3f0 smoke handle, +0x474..
// spark pool, +0x6c4 scanline RT id, +0x6c8 last-frame stamp).
//
// Non-matching notes: 4CC ids (XFKY class word); PPC FP-spill frames FUN_803826d0/
// FUN_8038271c, FUN_803826d4/FUN_80382720, FUN_803826e4/FUN_80382730,
// FUN_803826e8/FUN_80382734, FUN_803826f0/FUN_8038273c (open/close) are opaque;
// int<->double 0x43300000 bias math is plain float math; the 0x800-entry sine LUT
// DAT_80583228 is indexed &0x7ff with a +0x200 quarter-phase; DAT_cc008000 is the GX
// Command-Processor FIFO write port (volatile, colour words stored via (float) cast
// to match the established tree convention). sound_update_3d_source is void in decls
// but its ROM return (a "started" flag) is threaded, so it is routed through a
// file-local typed alias returning int. gx_set_sub_material_id/gx_set_blend_mode are
// called with (id)/(mode) and their return values saved/restored per decls.
// ---------------------------------------------------------------------------

// raw data / helper symbols new to this batch (file-local)
extern volatile float DAT_cc008000;   // GX Command Processor FIFO write port
extern int   DAT_806109c8;      // frame-time numerator a
extern int   DAT_806109c4;      // frame-time numerator b
extern int   DAT_8060f954;      // frame-time denominator a
extern int   DAT_8060f950;      // frame-time denominator b
extern int   DAT_80456eb0;      // scanline dash-width table (5 entries)
extern int   FUN_803826d0(void);   // FP spill frame open (arc render)
extern void  FUN_8038271c(void);   // FP spill frame close (arc render)
// ekun_debris_trail_quad_render_default lives in worm_ekun_state.cpp (not in decls).
extern void  ekun_debris_trail_quad_render_default(double, double, double, double, double, double,
                                                   unsigned int color, int flags);

// sound_update_3d_source's ROM return is a "started" flag threaded by this TU; the
// arg shape used here is (double,double,double, handle:int, pos:int, sample:int, flags:int).
typedef int (*Sound3dRetFn)(double, double, double, int, int, int, int);
static const Sound3dRetFn __we646_sound3d_ret = (Sound3dRetFn)&sound_update_3d_source;

// batch_646 tuning constants (file-local raw externs)
extern float FLOAT_80617d30; extern float FLOAT_80617d34; extern float FLOAT_80617d38;
extern float FLOAT_80617d3c; extern float FLOAT_80617d40; extern float FLOAT_80617d44;
extern float FLOAT_80617d48; extern float FLOAT_80617d4c; extern float FLOAT_80617d50;
extern float FLOAT_80617d54; extern float FLOAT_80617d58; extern float FLOAT_80617d5c;
extern float FLOAT_80617d60;

extern float DAT_803dfd68; extern int DAT_803dfd6c; extern int DAT_803dfd70;  // smoke spawn pos
extern float DAT_803dfd74; extern int DAT_803dfd78; extern int DAT_803dfd7c;  // smoke spawn vel
extern float DAT_803dfd80; extern int DAT_803dfd84; extern int DAT_803dfd88;  // vent spawn vel

// ---------------------------------------------------------------------------
// 0x80204c90  zcsc_ambient_loop_tick
// Advance a phase (+200 of the entity) by frame delta and drive a persistent 0x647
// 3D ambient loop; set instance flag 0x40 when the loop starts.
// ---------------------------------------------------------------------------
void zcsc_ambient_loop_tick(int entity)
{
    *(float *)(entity + 200) =
        FLOAT_80617cb8 * FLOAT_80617d5c *
        (float)((double)FLOAT_80617c18 /
                (double)(float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617c10)) *
        FLOAT_806108ec + *(float *)(entity + 200);
    int started = __we646_sound3d_ret((double)FLOAT_80617c20, (double)FLOAT_80617d2c, (double)FLOAT_80617c18,
                                      entity + 0x22c, entity + 100, 0x647, 0);
    if (started != 0) {
        *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x40;
    }
}

// ---------------------------------------------------------------------------
// 0x80204d30  zcsc_flicker_jitter
// Add an rng offset (scaled by FLOAT_80617c9c) about the +0xd0 base into +200: a
// per-frame flicker for the monitor glow.
// ---------------------------------------------------------------------------
void zcsc_flicker_jitter(int entity)
{
    unsigned int r = __we639_rng();
    *(float *)(entity + 200) =
        FLOAT_80617c9c * ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80617cc8) / FLOAT_80617cc0) +
        *(float *)(entity + 0xd0);
}

// ---------------------------------------------------------------------------
// 0x80204fd4  zcsc_xfky_set_rendertype
// Resolve the XFKY entity (0x58464b59) and set its render-instance type word to 1.
// ---------------------------------------------------------------------------
void zcsc_xfky_set_rendertype(void)
{
    int xfky = FUN_8023c32c(0x58464b59);   // 'XFKY'
    *(int *)(*(int *)(xfky + 4) + 0x30) = 1;
}

// ---------------------------------------------------------------------------
// 0x80205008  zcsc_bind_scanline_rt
// When the entity's render type is 1, allocate its collision strip table and copy the
// scanline render-target id (+0x6c4) into strip slot +0x70.
// ---------------------------------------------------------------------------
void zcsc_bind_scanline_rt(int entity)
{
    if ((*(int **)(entity + 4))[0xc] == 1) {
        int strip = collision_alloc_strip_table(*(int **)(entity + 4));
        *(int *)(strip + 0x70) = *(int *)(DAT_806127a4 + 0x6c4);
    }
}

// ---------------------------------------------------------------------------
// 0x80204b8c  zcsc_smoke_emitter_toggle
// Toggle the monitor smoke emitter (handle +0x3f0). Source flag (*param+0xc) 0 ->
// disable + clear +0x450; else destroy any live handle, respawn from def +0x108, set
// +0x450 = 1.
// ---------------------------------------------------------------------------
void zcsc_smoke_emitter_toggle(int *param_1)
{
    float pos[3];   // local_c/8/4
    float vel[3];   // local_18/14/10
    int handle;
    if (*(int *)(*param_1 + 0xc) == 0) {
        handle = *(int *)(DAT_806127a4 + 0x3f0);
        if (particle_handle_is_valid(&handle)) {
            handle = *(int *)(DAT_806127a4 + 0x3f0);
            particle_cached_disable((ParticleEmitter **)&handle);
        }
        *(float *)(DAT_806127a4 + 0x450) = FLOAT_80617c0c;
    } else {
        pos[0] = DAT_803dfd68; *(int *)&pos[1] = DAT_803dfd6c; *(int *)&pos[2] = DAT_803dfd70;
        vel[0] = DAT_803dfd74; *(int *)&vel[1] = DAT_803dfd78; *(int *)&vel[2] = DAT_803dfd7c;
        handle = *(int *)(DAT_806127a4 + 0x3f0);
        if (particle_handle_is_valid(&handle)) {
            handle = *(int *)(DAT_806127a4 + 0x3f0);
            particle_handle_destroy(&handle);
        }
        particle_emitter_spawn(DAT_806127a4 + 0x3f0, DAT_806127a4 + 0x108, 0, pos, vel);
        *(float *)(DAT_806127a4 + 0x450) = FLOAT_80617c18;
    }
}

// ---------------------------------------------------------------------------
// 0x80204d8c  zcsc_vent_emitter_tick
// Physics update for the steam-vent submesh (resolved via +0x2d8). 3-state open/close
// (+0x1a0): ease +0xd0, keep the cached steam emitter's drag/size in sync (or respawn
// it from def +0x84 at the submesh anchor), drive a 0xbfc 3D hiss loop, set the
// submesh open angle (+0x70). FP-spill wrapped.
// ---------------------------------------------------------------------------
void zcsc_vent_emitter_tick(void)
{
    float spawnVel[3];   // local_30/2c/28
    int entity = FUN_803826f0();
    int *state = *(int **)(entity + 0x7c);
    float *anchor = (float *)scene_node_get_submesh(*(int *)(entity + 0x34),
                                                     *(unsigned int *)(*(int *)(entity + 0x2d8) + 0x28));
    int s = *(int *)(entity + 0x1a0);
    int h;
    if (s == 1) {
        scalar_step_toward_snap((double)FLOAT_80617c0c,
            (double)(FLOAT_80617cf0 *
                     (FLOAT_80617c18 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617c10)) *
                     FLOAT_806108ec),
            (float *)(entity + 0xd0));
        h = *state;
        if (particle_handle_is_valid(&h)) {
            h = *state;
            particle_cached_set_drag((double)(*(float *)(entity + 0xd0) / FLOAT_80617cf0), (ParticleEmitter **)&h);
            h = *state;
            particle_cached_set_size_range((double)(*(float *)(entity + 0xd0) / FLOAT_80617cf0),
                                           (double)FLOAT_80617c18, (ParticleEmitter **)&h);
        }
    } else if ((s < 1) && (-1 < s)) {
        scalar_step_toward_snap((double)FLOAT_80617cf0,
            (double)(float)((double)FLOAT_80617cf0 *
                            (double)((FLOAT_80617c18 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617c10)) *
                                     FLOAT_806108ec)),
            (float *)(entity + 0xd0));
        h = *state;
        if (particle_handle_is_valid(&h)) {
            h = *state;
            particle_cached_set_drag((double)(*(float *)(entity + 0xd0) / FLOAT_80617cf0), (ParticleEmitter **)&h);
            h = *state;
            particle_cached_set_size_range((double)(*(float *)(entity + 0xd0) / FLOAT_80617cf0),
                                           (double)FLOAT_80617c18, (ParticleEmitter **)&h);
        } else {
            spawnVel[0] = DAT_803dfd80; *(int *)&spawnVel[1] = DAT_803dfd84; *(int *)&spawnVel[2] = DAT_803dfd88;
            particle_emitter_spawn((unsigned int)(intptr_t)state, DAT_806127a4 + 0x84, 0, anchor, spawnVel);
        }
    }
    int started = __we646_sound3d_ret((double)(*(float *)(entity + 0xd0) / FLOAT_80617cf0),
                                      (double)FLOAT_80617c9c, (double)FLOAT_80617c18,
                                      entity + 0x22c, entity + 100, 0xbfc, 0);
    if (started != 0) {
        *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) | 0x40;
    }
    *(float *)(entity + 0x70) = *(float *)(*(int *)(entity + 0x2d8) + 0x20) - *(float *)(entity + 0xd0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80204628  zcsc_spark_pool_tick
// Physics update for the 20-slot spark particle pool (DAT_806127a4+0x474, stride
// 0x1c). Gated on instance flag bit0. First tick (flag +0x478) seeds each slot with a
// box-random position and rng velocity scaled by FLOAT_80617d58, colour 0x7f5f5f7f;
// subsequent ticks integrate, bounce off the box walls, and set a distance-faded
// alpha (squared distance to view origin DAT_80611d54+0x27c), counting live sparks in
// slot[0]. FP-spill wrapped.
// ---------------------------------------------------------------------------
void zcsc_spark_pool_tick(void)
{
    int entity = FUN_803826e4();
    int blk = DAT_806127a4;
    if ((*(unsigned int *)(*(int *)(entity + 4) + 0x30) & 1) == 0) { FUN_80382730(); return; }

    int *sparkPool = (int *)(DAT_806127a4 + 0x474);
    if (*(int *)(DAT_806127a4 + 0x478) == 0) {
        // seed the box bounds (+0x47c..+0x490)
        *(float *)(DAT_806127a4 + 0x47c) = FLOAT_80617d40;
        *(float *)(blk + 0x480) = FLOAT_80617d44;
        *(float *)(blk + 0x484) = FLOAT_80617d48;
        *(float *)(blk + 0x488) = FLOAT_80617d4c;
        *(float *)(blk + 0x48c) = FLOAT_80617d50;
        *(float *)(blk + 0x490) = FLOAT_80617d54;
        int off = 0;
        for (int i = 0; i < 0x14; i = i + 1) {
            double lo, r;
            lo = (double)*(float *)(blk + 0x47c); r = game_frand();
            *(float *)((int)sparkPool + off + 0x20) = (float)((double)(float)((double)*(float *)(blk + 0x488) - lo) * r + lo);
            lo = (double)*(float *)(blk + 0x480); r = game_frand();
            *(float *)((int)sparkPool + off + 0x24) = (float)((double)(float)((double)*(float *)(blk + 0x48c) - lo) * r + lo);
            lo = (double)*(float *)(blk + 0x484); r = game_frand();
            *(float *)((int)sparkPool + off + 0x28) = (float)((double)(float)((double)*(float *)(blk + 0x490) - lo) * r + lo);
            for (int k = 0; k < 3; k = k + 1) {
                unsigned int rc = __we639_rng();
                float v = (float)((double)FLOAT_80617cb8 *
                          (double)(float)((double)(float)((double)(float)((double)CONCAT44(0x43300000, rc) - DOUBLE_80617cc8) / FLOAT_80617cc0) - (double)FLOAT_80617c94));
                *(float *)((int)sparkPool + off + 0x2c + k * 4) = (float)((double)v * (double)FLOAT_80617d58);
            }
            *(unsigned int *)((int)sparkPool + off + 0x38) = 0x7f5f5f7f;
            off = off + 0x1c;
        }
        *(int *)(blk + 0x478) = 1;
        *sparkPool = 0;
    } else {
        *sparkPool = 0;
        float invLo = FLOAT_80617d08;
        int off = 0;
        for (int i = 0; i < 0x14; i = i + 1) {
            float dt = (float)FLOAT_806108ec;
            float frame = FLOAT_80617c18 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80617c10);
            *(float *)((int)sparkPool + off + 0x20) += *(float *)((int)sparkPool + off + 0x2c) * frame * dt;
            *(float *)((int)sparkPool + off + 0x24) += *(float *)((int)sparkPool + off + 0x30) * frame * dt;
            *(float *)((int)sparkPool + off + 0x28) += *(float *)((int)sparkPool + off + 0x34) * frame * dt;
            // bounce each axis off the box walls
            int c = 0;
            for (int axis = 0; axis < 3; axis = axis + 1) {
                float p = *(float *)((int)sparkPool + off + c + 0x20);
                if ((*(float *)((int)sparkPool + c + 0x14) < p) || (p < *(float *)((int)sparkPool + c + 8))) {
                    *(float *)((int)sparkPool + off + c + 0x2c) = -*(float *)((int)sparkPool + off + c + 0x2c);
                }
                c = c + 4;
            }
            float dx = *(float *)((int)sparkPool + off + 0x20) - *(float *)(DAT_80611d54 + 0x27c);
            float dy = *(float *)((int)sparkPool + off + 0x24) - *(float *)(DAT_80611d54 + 0x280);
            float dz = *(float *)((int)sparkPool + off + 0x28) - *(float *)(DAT_80611d54 + 0x284);
            float d = (dz * dz + dx * dx + dy * dy) * invLo;
            float cl = (FLOAT_80617c18 < d) ? FLOAT_80617c18 : d;
            float r = FLOAT_80617c0c;
            if ((FLOAT_80617c0c <= cl) && (FLOAT_80617c18 < d)) r = FLOAT_80617c18; else if (FLOAT_80617c0c <= cl) r = d;
            *(unsigned int *)((int)sparkPool + off + 0x38) =
                (int)(FLOAT_80617d34 * (FLOAT_80617c18 - r)) & 0xff | 0x7f0f0f00;
            if ((*(unsigned int *)((int)sparkPool + off + 0x38) & 0x7f) != 0) {
                *sparkPool = *sparkPool + 1;
            }
            off = off + 0x1c;
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80204a00  zcsc_spark_pool_render  (graphics)
// Render the 20-slot spark pool as a projected sprite batch (sprite 0x28d), sized by
// the view scale (DAT_80611d54+0x268) and aspect ratio, one projected point per live
// spark (alpha nonzero) with a byteswapped colour. FP-spill wrapped.
// ---------------------------------------------------------------------------
void zcsc_spark_pool_render(void)
{
    int entity = FUN_803826e8();
    if ((*(unsigned int *)(*(int *)(entity + 4) + 0x30) & 1) != 0) {
        int *sparkPool = (int *)(DAT_806127a4 + 0x474);
        if (*(int *)(DAT_806127a4 + 0x474) != 0) {
            unsigned int oldMat = gx_set_sub_material_id(2);
            int oldBlend = gx_set_blend_mode(2);
            int tex = sprite_get_texture_id(0x28d);
            double aspect =
                (double)(((float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_80617c10) /
                          (float)((double)CONCAT44(0x43300000, DAT_8060f954) - DOUBLE_80617cc8)) /
                         ((float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80617c10) /
                          (float)((double)CONCAT44(0x43300000, DAT_8060f950) - DOUBLE_80617cc8)));
            gx_begin_projected_batch(aspect,
                                     (double)(FLOAT_80617cb8 * *(float *)(DAT_80611d54 + 0x268)),
                                     (double)FLOAT_80617c0c, (double)FLOAT_80617c0c,
                                     *sparkPool << 2, tex);
            int off = 0;
            for (int i = 0; i < 0x14; i = i + 1) {
                unsigned int col = *(unsigned int *)((int)sparkPool + off + 0x38);
                if ((col & 0x7f) != 0) {
                    unsigned int swapped = col << 0x18 | (col & 0xff00) << 8 | col >> 0x18 | col >> 8 & 0xff00;
                    gx_projtex_add_point((double)*(float *)((int)sparkPool + off + 0x20),
                                         (double)*(float *)((int)sparkPool + off + 0x24),
                                         (double)*(float *)((int)sparkPool + off + 0x28),
                                         (double)(FLOAT_80617cb8 * *(float *)(DAT_80611d54 + 0x268)),
                                         (double)FLOAT_80617c0c, (double)FLOAT_80617c0c, (float)swapped);
                }
                off = off + 0x1c;
            }
            gx_end_batch();
            gx_set_sub_material_id(oldMat);
            gx_set_blend_mode(oldBlend);
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80204254  zcsc_arc_quad_render  (graphics)
// Render the monitor's electric-arc / lightning FX: build a screen colour from the
// +0xd0 intensity, transpose the view matrix, draw ~17 additive billboard trail quads
// (ekun_debris_trail_quad_render_default) jittered along a sine-LUT path from the
// +0x1a8 base, plus one GX textured strip (sprite 0x325) written to the CP FIFO. The
// FIFO stores are structurally faithful (values pushed in order); non-matching. FP-
// spill wrapped.
// ---------------------------------------------------------------------------
void zcsc_arc_quad_render(void)
{
    int entity = FUN_803826d0();
    double intensity = (double)*(float *)(entity + 0x1a4);
    game_time_to_frames();
    float g = *(float *)(entity + 0xd0);
    float t = FLOAT_80617d30 * (FLOAT_80617c18 - g);
    unsigned int col =
        ((int)(t + FLOAT_80617c28 * g) & 0xff) << 8 |
        (int)(FLOAT_80617d34 * (FLOAT_80617c18 - g) + FLOAT_80617c28 * g) << 0x18 |
        ((int)(FLOAT_80617d34 * g + t) & 0xff) << 0x10;

    mtx4_transpose(*(float **)(DAT_80611d54 + 0x208), (float *)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;
    double baseX = (double)(*(float *)(entity + 100) + *(float *)(entity + 0x1a8));
    double baseY = (double)(*(float *)(entity + 0x68) + *(float *)(entity + 0x1ac));
    double baseZ = (double)(*(float *)(entity + 0x6c) + *(float *)(entity + 0x1b0));
    double amp   = (double)FLOAT_80617d38;
    double ampB  = (double)FLOAT_80617c68;
    gx_set_color_update_mode(0);
    gx_set_sub_material_id(2);
    gx_set_blend_mode(2);
    double frames = game_time_to_frames();
    double frameNeg = (double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000);

    unsigned int i2 = (unsigned int)(FLOAT_80617d10 * intensity + FLOAT_80617c94);
    unsigned int i3 = (unsigned int)(FLOAT_80617d10 * (double)(float)((double)FLOAT_80617cd8 + intensity) + FLOAT_80617c94);
    unsigned int off2 = (i2 + 0x200) * 4 & 0x1ffc; i2 = i2 & 0x7ff;
    unsigned int off3 = (i3 + 0x200) * 4 & 0x1ffc; i3 = i3 & 0x7ff;
    double span = (double)(float)((double)(float)((double)(float)((double)FLOAT_80617c18 /
                    (double)(float)(frameNeg - DOUBLE_80617c10)) * frames) * (double)FLOAT_80617d08);

    double px = baseX, py = baseY;
    int idx = -10;
    do {
        unsigned int r = __we639_rng();
        double phase = (double)(float)((double)CONCAT44(0x43300000, (unsigned int)idx ^ 0x80000000));
        float s = (float)(ampB * (double)(float)(phase - DOUBLE_80617c10));
        py = (double)(float)(baseY + (double)(float)(amp * (double)*(float *)((int)&DAT_80583228 + off2) +
                                                     (double)(s * *(float *)((int)&DAT_80583228 + off3))));
        px = (double)(float)(baseX + (double)(float)(amp * (double)(float)(&DAT_80583228)[i2] +
                                                     (double)(s * (float)(&DAT_80583228)[i3])));
        ekun_debris_trail_quad_render_default(baseX, baseY, baseZ, px, py, baseZ,
                                              col | (r & 0x1f) + 0x4f, 0);
        idx = idx + 1;
    } while (idx < 0xb);

    int tex = sprite_get_texture_id(0x325);
    gx_begin_textured_batch(2, 0x92, 0, 3, tex);
    float depthBias = (float)(span * (double)FLOAT_80617d08);
    // vertex 0
    DAT_cc008000 = (float)baseX; DAT_cc008000 = (float)baseY; DAT_cc008000 = (float)baseZ;
    DAT_cc008000 = (float)(col | 0x2f);
    DAT_cc008000 = (float)((double)FLOAT_80617c94 * (double)(depthBias + (float)(baseX - (double)*(float *)(entity + 0xa8))));
    DAT_cc008000 = (float)((double)FLOAT_80617c94 * span);
    // sine-LUT jittered endpoints for the other two verts
    float ex = (float)(baseX + (double)(float)(amp * (double)(float)(&DAT_80583228)[i2] +
                       (double)(float)((double)FLOAT_80617d3c * (double)(float)(&DAT_80583228)[i3])));
    float ey = (float)(baseY + (double)(float)(amp * (double)*(float *)((int)&DAT_80583228 + off2) +
                       (double)(float)((double)FLOAT_80617d3c * (double)*(float *)((int)&DAT_80583228 + off2))));
    // vertex 1
    DAT_cc008000 = ex; DAT_cc008000 = ey; DAT_cc008000 = (float)baseZ;
    DAT_cc008000 = (float)col;
    DAT_cc008000 = (float)((double)FLOAT_80617c94 * (double)(depthBias + (float)(ex - (double)*(float *)(entity + 0xa8))));
    DAT_cc008000 = (float)((double)FLOAT_80617c94 * (double)(float)(span + (double)(ey - *(float *)(entity + 0xac))));
    // vertex 2
    DAT_cc008000 = (float)px; DAT_cc008000 = (float)py; DAT_cc008000 = (float)baseZ;
    DAT_cc008000 = (float)col;
    DAT_cc008000 = (float)((double)FLOAT_80617c94 * (double)(depthBias + (float)(px - (double)*(float *)(entity + 0xa8))));
    DAT_cc008000 = (float)((double)FLOAT_80617c94 * (double)(float)(span + (double)(py - *(float *)(entity + 0xac))));
    gx_end_batch();
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// 0x80205044  zcsc_scanline_rt_draw  (graphics)
// Once per frame (guarded by frame stamp DAT_80611928 vs +0x6c8) redraw the 0x40x0x40
// scanline monitor render-target: clear to 0x7f, then draw rolling horizontal
// scanlines with sprite 0x333, choosing per-row a single bar / periodic dashes /
// half-split pattern from an animated counter, and rebind the default RT after.
// FP-spill wrapped.
// ---------------------------------------------------------------------------
void zcsc_scanline_rt_draw(void)
{
    int entity = FUN_803826d4();
    if ((*(int *)(*(int *)(entity + 4) + 0x30) == 1) && (DAT_80611928 != *(int *)(DAT_806127a4 + 0x6c8))) {
        double frames = game_time_to_frames();
        double frameNeg = (double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000);
        unsigned int counter = (unsigned int)(FLOAT_80617d60 *
            (float)((double)(FLOAT_80617c18 / (float)(frameNeg - DOUBLE_80617c10)) * frames));
        gx_set_color_update_mode(0);
        gx_set_sub_material_id(1);
        gx_fill_rect_clipped(0, 0, 0x40, 0x40, 0x7f);
        gx_set_color_update_mode(0);
        int tex = sprite_get_texture_id(0x333);
        gx_set_active_material(tex);
        double step = (double)FLOAT_80617d28;
        int v = counter << 4;
        int row = 1;
        while (row < 0x40) {
            int q  = (int)counter / 0x28;
            int qm = q - (q >> 0x1f);
            int r4 = ((q << 30) + (q >> 0x1f)) >> 0x1e;   // low 2 bits of the row-quotient
            int kind = r4 - (q >> 0x1f);
            if ((qm == (q >> 0x1f)) && (1 < (int)counter % 0x28)) {
                unsigned int w = *(unsigned int *)(&DAT_80456eb0 + ((int)counter % 5) * 4);
                gx_draw_sprite_rect(0, row, w, row + 2, 0, v + 8U, w << 4, v + 8U);
            } else if (((kind == 1) || (kind == 3)) && (1 < (int)counter % 0x28)) {
                int dash = (kind == 3) ? 2 : 5;
                for (int x = dash; x < 0x40; x = x + dash * 2) {
                    gx_draw_sprite_rect(x, row, x + dash, row + 2, x << 4, v + 8U, (x + dash) * 0x10, v + 8U);
                }
            } else if ((kind == 2) && ((counter & 1 ^ counter >> 0x1f) != counter >> 0x1f)) {
                int m = (int)counter % 0x28;
                unsigned int half = (unsigned int)((((m >> 1) + (int)((m < 0) && ((m & 1) != 0))) * 0x40) / 0x14);
                gx_draw_sprite_rect(0, row, half, row + 2, 0, v + 8U, half << 4, v + 8U);
            }
            counter = counter + 1;
            v = v + 0x10;
            row = (int)((double)(float)((double)(CONCAT44(0x43300000, row) ^ 0x80000000) - DOUBLE_80617c10) + step);
        }
        *(int *)(DAT_806127a4 + 0x6c8) = DAT_80611928;
        render_target_bind_default(*(int *)(DAT_806127a4 + 0x6c4));
    }
    FUN_80382720();
}
