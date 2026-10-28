// objective_state.cpp
// Objective/award slot state tracking driven by tracked entity handles, plus
// the aveditor light-zone descriptor build and per-frame zone-light update.
//
// Globals:
//   DAT_80611c24 : base of tracked-entity table (stride 0x2e8); a handle's
//                  high bits (>>0x16) index it, and +0x2c holds the handle for
//                  liveness validation.
//   FLOAT_8061ad00 : health/alive threshold for the tracked entity (+0x1cc).
//   DAT_805015bd   : game-mode selector byte (3 == story).
//   DAT_805c7c04/08 : aveditor slot count / array base (stride 0x8c).
//   DAT_80482060 / PTR_LAB_80482064 : objective-type id table (2 entries) and
//                  the paired handler jump table.

#include "common.h"

// ---- per-file externs ----
extern unsigned char DAT_805015bd;
extern int    DAT_80611c24;
extern int    DAT_80612614;       // active level id
extern int    DAT_805c7c04;
extern int    DAT_805c7c08;
extern float  FLOAT_8061ad00;
extern int    DAT_80482060;       // objective-type id table (first entry)
extern int    PTR_LAB_80482064;   // paired handler jump table base

extern void*  level_get_info_cached(int levelId);
extern int    FUN_803826e4(void);          // acquire tracked-record cursor
extern void   FUN_80382730(void);          // release tracked-record cursor
// countLeadingZeros is provided as a macro by common.h (do not redeclare)
extern unsigned int* aveditor_mode10_field_ptr(int cell, int index);
extern void   particle_zone_light_update(void);
extern int    aveditor_palette_brighten(int index);

// Notify hook fired when an objective slot changes state. Empty in this build.
void objective_state_touch(void) { }

// Build a light-zone descriptor from an aveditor slot record into the 7-word
// output: type code (from record +4), position words (+0xc/+0x10), two zeroed
// fields, and two brightened palette colors (from bytes +0 and +1).
void zone_light_build_descriptor(unsigned char* rec, unsigned int* out)
{
    int kind = *(int*)(rec + 4);
    unsigned int typeCode = 0;
    if (kind == 2) {
        typeCode = 2;
    } else if (kind < 2) {
        if (kind == 0) {
            typeCode = 0;
        } else if (kind >= -1) {
            typeCode = 3;
        }
    } else if (kind < 4) {
        typeCode = 1;
    }
    if (rec != 0) {
        out[0] = typeCode;
        out[1] = *(unsigned int*)(rec + 0xc);
        out[2] = *(unsigned int*)(rec + 0x10);
        out[3] = 0;
        out[4] = 0;
        out[5] = (unsigned int)aveditor_palette_brighten((unsigned int)rec[0]);
        out[6] = (unsigned int)aveditor_palette_brighten((unsigned int)rec[1]);
    }
}

// Per-frame update of aveditor mode-10 (light-zone) slots: for each slot whose
// mode (+0x80) is 10, iterate its fields (+0x70 count) and refresh the zone light.
void aveditor_update_zone_lights(void)
{
    FUN_803826e4();
    int off = 0;
    for (int i = 0; i < DAT_805c7c04; i++) {
        int slot = DAT_805c7c08 + off;
        if (*(int*)(slot + 0x80) == 10) {
            for (int f = 0; f < *(int*)(slot + 0x70); f++) {
                aveditor_mode10_field_ptr(slot, f);
                particle_zone_light_update();
            }
        }
        off += 0x8c;
    }
    FUN_80382730();
}

// Returns 2 (state changed) if the objective's tracked entity is dead/invalid;
// clears a stale handle. The tracked entity is validated via DAT_80611c24 and
// its alive value (+0x1cc) compared to FLOAT_8061ad00.
unsigned int objective_check_entity_dead(int rec)
{
    unsigned int handle = *(unsigned int*)(rec + 0x14);
    int ent;
    if (handle == 0) {
        ent = 0;
    } else {
        ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(unsigned int*)(ent + 0x2c) != handle) {
            ent = 0;
            *(unsigned int*)(rec + 0x14) = 0;
        }
    }
    if ((ent == 0 || FLOAT_8061ad00 < *(float*)(ent + 0x1cc)) && ent != 0) {
        return 0;
    }
    if (DAT_805015bd == 3) {
        *(unsigned int*)(rec + 0x20) = 1;
    }
    *(unsigned int*)(rec + 0x18) = 1;
    objective_state_touch();
    return 2;
}

// Look up the objective-type index for the record's type id (+0x20) in the
// 2-entry id table at DAT_80482060; returns -1 if not found.
int objective_lookup_type_index(int rec)
{
    int index = 0;
    int off = 0;
    int remaining = 2;
    do {
        if (*(int*)((int)&DAT_80482060 + off) == *(int*)(rec + 0x20)) {
            return index;
        }
        index++;
        off += 8;
        remaining--;
    } while (remaining != 0);
    return -1;
}

// Update a capture/hold objective's state from its tracked entity: resolves the
// entity, runs the type-specific handler from the jump table, and transitions
// the record's state word (+6) based on the slot type bits and story mode.
void objective_update_capture_state(void)
{
    int* rec = (int*)FUN_803826e4();
    unsigned int handle = (unsigned int)rec[5];
    int ent = 0;
    if (handle != 0) {
        ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(unsigned int*)(ent + 0x2c) != handle) {
            rec[5] = 0;
            ent = 0;
        }
    }
    int levelInfo = (int)level_get_info_cached(DAT_80612614);
    int newState = 0;
    int slotType = *(int*)(*(int*)(levelInfo + 0x130) + rec[0] * 0xb4 + 8);

    int typeIndex = objective_lookup_type_index(ent);
    int handlerResult = ((int(*)(int)) (&PTR_LAB_80482064)[typeIndex * 2])(ent);

    int expected = 1;
    unsigned int notFull = countLeadingZeros(4 - rec[6]);
    if ((1 << slotType & 0x18U) == 0) {
        if ((1 << slotType & 0x60U) != 0) {
            expected = 0;
        }
    } else {
        expected = 1;
    }

    if ((notFull >> 5) != 0 && expected != handlerResult) {
        rec[6] = 5;
        FUN_80382730();
        return;
    }
    if ((notFull >> 5) != 0 || *(float*)(ent + 0x1cc) <= FLOAT_8061ad00 ||
        expected != handlerResult) {
        FUN_80382730();
        return;
    }

    if (slotType == 5 || slotType == 3) {
        newState = 1;
    } else if (slotType < 5) {
        if (slotType > 2) {
            newState = 4;
        }
    } else if (slotType < 7) {
        newState = 4;
    }

    if (DAT_805015bd == 3) {
        rec[6] = 1;
        rec[8] = 1;
    } else {
        rec[6] = newState;
    }
    objective_state_touch();
    FUN_80382730();
}

// Returns 2 if the objective's tracked entity is alive and in AI state 3
// (e.g. surrendered/captured); marks the record state (+0x18) = 3.
unsigned int objective_check_entity_state3(int rec)
{
    unsigned int handle = *(unsigned int*)(rec + 0x14);
    int ent = 0;
    if (handle != 0) {
        ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(unsigned int*)(ent + 0x2c) != handle) {
            *(unsigned int*)(rec + 0x14) = 0;
            ent = 0;
        }
    }
    unsigned int result;
    if (ent == 0 || *(float*)(ent + 0x1cc) <= FLOAT_8061ad00 ||
        *(int*)(*(int*)(ent + 0x7c) + 0x10) != 3) {
        result = 0;
    } else {
        *(unsigned int*)(rec + 0x18) = 3;
        objective_state_touch();
        result = 2;
    }
    return result;
}

// ---------------------------------------------------------------------------
// batch_987 additions: objective/switch slot registration + switch/prop handlers.
// ---------------------------------------------------------------------------

extern int   DAT_80612db0;        // award/objective context base
extern float FLOAT_8061ad18;      // damage-pulse magnitude

extern int   entity_handle_resolve(unsigned int id);
extern void  switch_slot_reset(int slot);
extern void  damage_dispatch_to_entity(int* eventData);   // canonical decl (FP args via ABI)
extern void  switch_use_handler(void);
extern void  switch_item_unequip(void);
extern void  switch_item_equip_finalize(void);
extern void  prop_reset_active_state(int* rec);
extern unsigned int prop_is_state_idle(int obj);
extern void  prop_set_idle_if_valid(int* rec);
extern void  spawn_slot_build_descriptor(unsigned int, unsigned int, int* out);

// Forward decls for the two switch/prop dispatchers defined below.
void switch_activate_if_ready(int type, int entity);
void prop_dispatch_by_type(int type, int rec);
unsigned int switch_apply_damage_pulse(unsigned long long, double, double,
                                       unsigned long long, unsigned long long,
                                       unsigned long long, unsigned long long,
                                       unsigned long long, int rec);
void switch_update_slot_state(int* rec);

// Register a tracked objective slot in the award context (count +0x80, array
// +0x84 stride 0x28). Builds the slot descriptor, resolves the entity handle,
// and installs the per-type check handler pointer (slot word [4]).
void objective_register_tracked_slot(int rec, unsigned int arg)
{
    int idx = *(int*)(DAT_80612db0 + 0x80);
    *(int*)(DAT_80612db0 + 0x80) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x84) + idx * 0x28 + 4);
    spawn_slot_build_descriptor(rec, arg, slot);
    slot[6] = 5;
    int ent = entity_handle_resolve((int)*(short*)(rec + 0xc) + 1);
    if (ent != 0) {
        slot[5] = *(int*)(ent + 0x2c);
        int type = *(int*)(rec + 8);
        if (type < 7) {
            if (type == 0) {
                slot[4] = (int)&objective_check_entity_dead;
            } else if (type >= -1 && type > 2) {
                slot[4] = (int)&objective_update_capture_state;
            }
        } else if (type == 0x14 &&
                   *(unsigned short*)(ent + 0x94) < 0x8a8 &&
                   0x8a1 < *(unsigned short*)(ent + 0x94)) {
            slot[4] = (int)&objective_check_entity_state3;
        }
    }
}

// Apply a damage pulse to the objective's tracked entity when it is alive.
// (FP magnitude/direction args are passed via the PPC ABI; the C-level damage
// helper takes only the event record pointer.)
unsigned int switch_apply_damage_pulse(unsigned long long /*a1*/, double a2, double a3,
                                       unsigned long long /*a4*/, unsigned long long /*a5*/,
                                       unsigned long long /*a6*/, unsigned long long /*a7*/,
                                       unsigned long long /*a8*/, int rec)
{
    unsigned int handle = *(unsigned int*)(rec + 0x14);
    int ent;
    if (handle == 0) {
        ent = 0;
    } else {
        ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(unsigned int*)(ent + 0x2c) != handle) {
            ent = 0;
            *(unsigned int*)(rec + 0x14) = 0;
        }
    }
    if (ent != 0 && FLOAT_8061ad00 < *(float*)(ent + 0x1cc)) {
        objective_state_touch();
        int eventData[16];
        switch_slot_reset((int)eventData);
        eventData[10] = ent + 100;                                    // local_40
        *(float*)&eventData[9] =                                       // local_48
            (float)((double)FLOAT_8061ad18 + (double)*(float*)(ent + 0x1cc));
        eventData[0] = ent;
        (void)a2; (void)a3;
        damage_dispatch_to_entity(eventData);
    }
    return 0;
}

// Fire the switch-use handler when the entity's use-condition (AI +0x18) is met
// for the given switch type.
void switch_activate_if_ready(int type, int entity)
{
    unsigned int fire = 0;
    if (type == 0x14) {
        fire = (unsigned int)(*(int*)(*(int*)(entity + 0x7c) + 0x18) != 0);
    } else if (type < 0x14) {
        if (type == 0xb) {
            fire = countLeadingZeros(*(unsigned int*)(*(int*)(entity + 0x7c) + 0x18)) >> 5;
        }
    } else if (type < 0x16) {
        fire = 1;
    }
    if (fire != 0) {
        switch_use_handler();
    }
}

// Toggle a switch's equipped item based on the switch state (+0x18). Referenced
// by trigger_setup_type18 (level_slot_dispatch.cpp).
void switch_toggle_equipped_item(int entity, int rec)
{
    if (entity != 0 && *(int*)(rec + 0x1c) != 0) {
        int ai = *(int*)(entity + 0x7c);
        if (*(int*)(rec + 0x18) == 0) {
            if (*(int*)(ai + 0xd8) != 0 && *(int*)(ai + 0xd8) == *(int*)(rec + 0x1c)) {
                *(unsigned int*)(ai + 8) = *(unsigned int*)(ai + 8) & 0xfffffffe;
                *(unsigned int*)(ai + 8) = *(unsigned int*)(ai + 8) | 0x20;
                switch_item_unequip();
            }
        } else {
            if ((*(unsigned int*)(ai + 8) & 0x20) != 0) {
                *(unsigned int*)(ai + 8) = *(unsigned int*)(ai + 8) & 0xffffffdf;
                *(unsigned int*)(ai + 8) = *(unsigned int*)(ai + 8) | 1;
            }
            switch_item_equip_finalize();
        }
    }
}

// Prop handler: reset the prop's active state.
unsigned int prop_reset_slot(int rec)
{
    prop_reset_active_state(*(int**)(rec + 0x7c));
    return 0;
}

// Prop handler: toggle between idle and active state.
unsigned int prop_toggle_idle_state(int rec)
{
    int* prop = *(int**)(rec + 0x7c);
    unsigned int idle = prop_is_state_idle(rec);
    if (idle == 0) {
        prop_set_idle_if_valid(prop);
    } else {
        prop_reset_active_state(prop);
    }
    return 0;
}

// Prop handler: force the prop to idle if valid.
unsigned int prop_set_idle_slot(int rec)
{
    prop_set_idle_if_valid(*(int**)(rec + 0x7c));
    return 0;
}

// Dispatch a prop handler by type after resolving the tracked entity.
void prop_dispatch_by_type(int type, int rec)
{
    unsigned int handle = *(unsigned int*)(rec + 0x14);
    int ent = 0;
    if (handle != 0) {
        ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(unsigned int*)(ent + 0x2c) != handle) {
            *(unsigned int*)(rec + 0x14) = 0;
            ent = 0;
        }
    }
    if (type == 0x15) {
        prop_toggle_idle_state(ent);
    } else if (type < 0x15) {
        if (type == 0xb) {
            prop_reset_slot(ent);
        } else if (type > 10 && type > 0x13) {
            prop_set_idle_slot(ent);
        }
    }
}

// Update a switch/prop objective slot's state each frame: resolve tracked
// entity, look up the slot type from level info (+0x128 table, +0x84 field),
// and route to the switch (type 0x43) or prop (types 0x35-0x37) dispatcher.
// Installed as a slot handler; the slot record pointer arrives via r28 save.
void switch_update_slot_state(int* rec)
{
    unsigned int handle = (unsigned int)rec[5];
    int ent = 0;
    if (handle != 0) {
        ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(unsigned int*)(ent + 0x2c) != handle) {
            rec[5] = 0;
            ent = 0;
        }
    }
    int levelInfo = (int)level_get_info_cached(DAT_80612614);
    int slotType = *(int*)(*(int*)(levelInfo + 0x128) + rec[2] * 0x8c + 0x84);
    int slotDesc = *(int*)(levelInfo + 0x130) + rec[0] * 0xb4;
    objective_state_touch();
    if (slotType == 0x43) {
        switch_activate_if_ready(*(int*)(slotDesc + 8), ent);
    } else if (slotType < 0x43 && slotType < 0x38 && 0x34 < slotType) {
        prop_dispatch_by_type(*(int*)(slotDesc + 8), (int)rec);
    }
    objective_state_touch();
}

// Register a switch/prop objective slot in the award context (count +0x88,
// array +0x8c stride 0x24). Builds descriptor, resolves entity, installs the
// per-type handler pointer.
void objective_register_switch_slot(int rec, unsigned int arg)
{
    int idx = *(int*)(DAT_80612db0 + 0x88);
    *(int*)(DAT_80612db0 + 0x88) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x8c) + idx * 0x24);
    spawn_slot_build_descriptor(rec, arg, slot);
    int ent = entity_handle_resolve((int)*(short*)(rec + 0xc) + 1);
    if (ent != 0) {
        slot[5] = *(int*)(ent + 0x2c);
        switch (*(int*)(rec + 8)) {
        case 0:
            slot[4] = (int)&switch_apply_damage_pulse;
            break;
        case 0xb:
        case 0x14:
        case 0x15:
        case 0x18:
            slot[4] = (int)&switch_update_slot_state;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// batch_988 additions: use/capture/waypoint slot registration + handlers.
// Address range 0x8034afc4 - 0x8034b50c. Continues the objective slot
// registration system: each objective_register_* builds a spawn-slot
// descriptor in the award context (DAT_80612db0) and installs a per-type
// handler function pointer at slot word [4]; the small handlers below are
// those installed pointers.
// ---------------------------------------------------------------------------

// Entity/zone resolution tables used by objective_find_slot_by_entity.
extern int DAT_80611cd0;    // slot table base (stride 0xd8)
extern int DAT_80611cd4;    // slot table count
extern int DAT_80611b8c;    // actor record table base
extern int DAT_80611b94;    // zone-entity table base (biased -0x3e800)
extern int DAT_80611b98;    // secondary zone-entity table base (biased -0x413c0)
extern int DAT_80611c14;    // actor record table count

// Waypoint/marker scan globals used by slot_check_waypoint_timer.
extern int DAT_80588610;    // waypoint pointer array base
extern int DAT_80612098;    // waypoint pointer array count
extern int DAT_80611928;    // current level tick
extern int DAT_806109c0;    // waypoint hold duration (ticks)

extern int  soundpad_resolve_entity_to_zone(int entityId);
extern int  level_validate_waypoint_flags(void);
extern int  match_session_get_cached_history_count(void);
extern void aiactor_dispatch_owner_death(unsigned long long, double, double, double,
                                         unsigned long long, unsigned long long,
                                         unsigned long long, unsigned long long);

// Forward decls for handlers installed by the register_* functions below.
unsigned int slot_activate_use_flag(int rec);
unsigned int slot_mark_captured_by_entity(int rec);
int          objective_find_slot_by_entity(int entityId);
int          slot_complete_if_triggered(int rec);
unsigned int slot_check_waypoint_timer(int rec);

// Handler: if the slot's use flag (bit 0x10 of +0xc) is set, latch state
// (+0x18) = 1 and notify. Returns 2 on state change, 0 otherwise.
unsigned int slot_activate_use_flag(int rec)
{
    unsigned int result;
    if ((*(unsigned char*)(rec + 0xc) & 0x10) == 0) {
        result = 0;
    } else {
        *(unsigned int*)(rec + 0x18) = 1;
        objective_state_touch();
        result = 2;
    }
    return result;
}

// Register a "use" objective slot (award context count +0x80, array +0x84,
// stride 0x28). Resolves the slot's map zone id (or the raw entity id when
// below 0x1f41) into slot word [5], and installs slot_activate_use_flag for
// type-7 slots.
void objective_register_use_slot(int rec, unsigned int arg)
{
    int idx = *(int*)(DAT_80612db0 + 0x80);
    *(int*)(DAT_80612db0 + 0x80) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x84) + idx * 0x28 + 4);
    spawn_slot_build_descriptor(rec, arg, slot);
    int entityId = *(int*)(rec + 0x10);
    if (entityId < 0x1f41) {
        slot[5] = entityId;
    } else {
        int zone = soundpad_resolve_entity_to_zone(entityId);
        slot[5] = (int)*(short*)(zone + 8);
    }
    if (*(int*)(rec + 8) == 7) {
        slot[4] = (int)&slot_activate_use_flag;
    }
}

// Find the active slot record whose resolved entity matches entityId. Each of
// the DAT_80611cd4 entries (stride 0xd8) stores a raw handle at +0x3c which is
// resolved through one of three tables (zone-entity, secondary zone-entity, or
// actor record) depending on its id range. Returns the matching slot base, or 0.
int objective_find_slot_by_entity(int entityId)
{
    int off = 0;
    int remaining = DAT_80611cd4;
    if (remaining > 0) {
        do {
            int raw = *(int*)(DAT_80611cd0 + off + 0x3c);
            int resolved;
            if (raw >= 7999 && raw < 0x209e) {
                resolved = *(int*)(DAT_80611b94 + raw * 0x20 + -0x3e800);
            } else if (raw >= 0x209e && raw < 0x20b2) {
                resolved = *(int*)(DAT_80611b98 + raw * 0x20 + -0x413c0);
            } else if (raw >= 0 && raw < DAT_80611c14) {
                resolved = *(int*)(DAT_80611b8c + raw * 0x20);
            } else {
                resolved = -1;
            }
            if (entityId == resolved) {
                return *(int*)(DAT_80611cd0 + off);
            }
            off += 0xd8;
            remaining--;
        } while (remaining != 0);
    }
    return 0;
}

// Handler: mark the objective's tracked entity slot as captured by OR-ing bits
// 0x3 into the resolved slot's flag word (+0x28).
unsigned int slot_mark_captured_by_entity(int rec)
{
    int slot = objective_find_slot_by_entity(*(int*)(rec + 0x14));
    if (slot != 0) {
        objective_state_touch();
        *(unsigned int*)(slot + 0x28) = *(unsigned int*)(slot + 0x28) | 3;
    }
    return 0;
}

// Register a "capture" objective slot (award context count +0x88, array +0x8c,
// stride 0x24). Resolves the zone/entity id into slot word [5], clears the
// capture bits on the currently-matching slot, and installs
// slot_mark_captured_by_entity for type-0x10 slots.
void objective_register_capture_slot(int rec, unsigned int arg)
{
    int idx = *(int*)(DAT_80612db0 + 0x88);
    *(int*)(DAT_80612db0 + 0x88) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x8c) + idx * 0x24);
    spawn_slot_build_descriptor(rec, arg, slot);
    int entityId = *(int*)(rec + 0x10);
    if (entityId < 0x1f41) {
        slot[5] = entityId;
    } else {
        int zone = soundpad_resolve_entity_to_zone(entityId);
        slot[5] = (int)*(short*)(zone + 8);
    }
    int found = objective_find_slot_by_entity(slot[5]);
    if (found != 0) {
        *(unsigned int*)(found + 0x28) = *(unsigned int*)(found + 0x28) & 0xfffffffc;
    }
    if (*(int*)(rec + 8) == 0x10) {
        slot[4] = (int)&slot_mark_captured_by_entity;
    }
}

// Handler: if the slot's trigger flag (bit 2 of +0xc) is set, latch state
// (+0x18) = 3 and notify. Returns the pre-transition trigger state.
int slot_complete_if_triggered(int rec)
{
    int triggered = (*(unsigned char*)(rec + 0xc) & 2) != 0;
    if (triggered) {
        *(unsigned int*)(rec + 0x18) = 3;
        objective_state_touch();
    }
    return triggered;
}

// Handler for waypoint-timed objectives: scans the waypoint pointer array
// (DAT_80588610, count DAT_80612098) for a waypoint bound to this slot's entity
// (+0x14) whose ready flag (bit 2 of +0x14 or +0x1c depending on game mode) is
// set. On first match starts a hold timer (state 7, deadline +0x1c); when the
// timer expires it completes the objective (state 3). Returns 1 on completion.
unsigned int slot_check_waypoint_timer(int rec)
{
    if ((*(int*)(rec + 0x18) != 7 || *(int*)(rec + 0x1c) < DAT_80611928) &&
        level_validate_waypoint_flags() != 0) {
        int off = 0;
        int remaining = DAT_80612098;
        if (remaining > 0) {
            do {
                int wp = *(int*)((int)&DAT_80588610 + off);
                if (*(int*)(wp + 0xbf0) != 0 &&
                    *(int*)(*(int*)(wp + 0xbf0) + 8) == *(int*)(rec + 0x14)) {
                    unsigned int flags;
                    if (DAT_805015bd == '\n') {
                        flags = *(unsigned int*)(wp + 0x14);
                    } else {
                        flags = *(unsigned int*)(wp + 0x1c);
                    }
                    if ((flags & 4) != 0) {
                        if (*(int*)(rec + 0x18) == 7) {
                            *(unsigned int*)(rec + 0x18) = 3;
                            *(unsigned int*)(rec + 0x1c) = 0;
                            objective_state_touch();
                            return 1;
                        }
                        *(unsigned int*)(rec + 0x18) = 7;
                        *(int*)(rec + 0x1c) = DAT_80611928 + DAT_806109c0;
                        return 0;
                    }
                }
                off += 4;
                remaining--;
            } while (remaining != 0);
        }
    }
    return 0;
}

// Register a "waypoint" objective slot (award context count +0x80, array +0x84,
// stride 0x28). Computes the slot's target id from a session-history offset plus
// the record's local index (+0xe), and installs the trigger handler
// (slot_complete_if_triggered for type 0xb) or the timed handler
// (slot_check_waypoint_timer for type 0x14).
void objective_register_waypoint_slot(int rec, unsigned int arg)
{
    int idx = *(int*)(DAT_80612db0 + 0x80);
    *(int*)(DAT_80612db0 + 0x80) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x84) + idx * 0x28 + 4);
    spawn_slot_build_descriptor(rec, arg, slot);
    int base = match_session_get_cached_history_count();
    slot[5] = (int)*(short*)(rec + 0xe) + base + 1;
    if (*(int*)(rec + 8) == 0xb) {
        slot[4] = (int)&slot_complete_if_triggered;
    } else if (*(int*)(rec + 8) == 0x14) {
        slot[4] = (int)&slot_check_waypoint_timer;
    }
}

// Handler: on tracked owner death, dispatch the owner-death event to the AI
// actor system then latch the "killed" bit (2) into the slot flags (+0xc).
// FP/wide args are passed through to the actor dispatcher via the PPC ABI.
unsigned int slot_on_owner_death(unsigned long long a1, double a2, double a3, double a4,
                                 unsigned long long a5, unsigned long long a6,
                                 unsigned long long a7, unsigned long long a8, int rec)
{
    aiactor_dispatch_owner_death(a1, a2, a3, a4, a5, a6, a7, a8);
    *(unsigned char*)(rec + 0xc) = *(unsigned char*)(rec + 0xc) | 2;
    objective_state_touch();
    return 0;
}

// Handler: latch the "completed" bit (4) into the slot flags (+0xc) and notify.
unsigned int slot_set_completed_flag(int rec)
{
    *(unsigned char*)(rec + 0xc) = *(unsigned char*)(rec + 0xc) | 4;
    objective_state_touch();
    return 0;
}

// ---------------------------------------------------------------------------
// batch_989 additions: death/completion slot registration + spatial trigger-zone
// occupancy subsystem. Address range 0x8034b50c - 0x8034b924.
//
// The trigger-zone slots live in the award context at +0xb0 (array, stride
// 0x58). Each zone record holds a subslot count (+4) and per-subslot entity
// handles (+8, stride 4) plus a fill-flag word per subslot (+0x30). A zone's
// world-space AABB is derived from the resolved zone entity's position/scale;
// entities are tested for containment each frame and, when a zone's occupancy
// rule is met, the owning objective record's state is advanced.
// ---------------------------------------------------------------------------

extern float  FLOAT_8061ad1c;   // zone unit scale
extern float  FLOAT_8061ad20;   // zone height offset
extern double DOUBLE_8061ad10;  // 0x43300000 bias constant for int->float

extern int  DAT_805c7c10;       // trigger-zone descriptor table base (stride 0xb4)
extern int  DAT_80612610;       // active player count
extern int  DAT_80611d78;       // player instance table base (stride 0x141c, +0x14 = inst)

extern int  entity_pool_find_type1(int entityId);
extern int  slot_find_player_instance(void);
extern int  FUN_803826d8(void);   // acquire zone-record cursor (returns rec ptr)
extern void FUN_80382724(void);   // release zone-record cursor

// Forward decls for the zone helpers defined below.
int          zone_slot_record_by_index(int index);
unsigned int zone_all_subslots_filled(int zoneRec);
int          point_in_box(float* p, float* boxMin, float* boxMax);
bool         entity_in_zone_box(int entity, float* boxMin, float* boxMax);
void         zone_slot_set_state(int rec, int partial);
int          zone_check_entity_occupancy(int entity, float* boxMin, float* boxMax,
                                         int* rec, int zoneRec, int subIdx);

// Register a "death/completion" objective slot (award ctx count +0x88, array
// +0x8c, stride 0x24). Computes target id from session history + record +0xe;
// installs slot_on_owner_death (type 2) or slot_set_completed_flag (type 0x11).
void objective_register_death_slot(int rec, unsigned int arg)
{
    int idx = *(int*)(DAT_80612db0 + 0x88);
    *(int*)(DAT_80612db0 + 0x88) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x8c) + idx * 0x24);
    spawn_slot_build_descriptor(rec, arg, slot);
    int base = match_session_get_cached_history_count();
    slot[5] = (int)*(short*)(rec + 0xe) + base + 1;
    if (*(int*)(rec + 8) == 2) {
        slot[4] = (int)&slot_on_owner_death;
    } else if (*(int*)(rec + 8) == 0x11) {
        slot[4] = (int)&slot_set_completed_flag;
    }
}

// Compute the world-space AABB of a trigger zone from its resolved zone entity.
// Resolves entityId -> zone -> zone entity, then builds boxMin from the zone
// entity's scaled position (+0xc/+0x10/+0x14) with the height component (+0x10)
// taken directly, and boxMax = boxMin + fixed extents. Returns true if resolved.
bool zone_compute_bounds(int entityId, float* boxMin, float* boxMax)
{
    int zone = soundpad_resolve_entity_to_zone(entityId);
    if (zone != 0) {
        int zent = soundpad_resolve_entity_to_zone((int)*(short*)(zone + 8));
        float extY = FLOAT_8061ad20;
        float extX = FLOAT_8061ad1c;
        float posY = *(float*)(zent + 0x10);
        double sz = (double)CONCAT44(0x43300000,
                        (int)(*(float*)(zent + 0x14) / FLOAT_8061ad1c) ^ 0x80000000)
                    - DOUBLE_8061ad10;
        boxMin[0] = FLOAT_8061ad1c *
                    (float)((double)CONCAT44(0x43300000,
                        (int)(*(float*)(zent + 0xc) / FLOAT_8061ad1c) ^ 0x80000000)
                        - DOUBLE_8061ad10);
        boxMin[1] = posY;
        boxMin[2] = extX * (float)sz;
        boxMax[0] = extX + boxMin[0];
        boxMax[1] = extY + boxMin[1];
        boxMax[2] = extX + boxMin[2];
    }
    return zone != 0;
}

// Return the trigger-zone slot record for a subslot index (award ctx +0xb0,
// stride 0x58).
int zone_slot_record_by_index(int index)
{
    return *(int*)(DAT_80612db0 + 0xb0) + index * 0x58;
}

// Return the trigger-zone slot record for the entity's pool type-1 record.
int zone_slot_record_by_entity(int entityId)
{
    int rec = entity_pool_find_type1(entityId);
    return *(int*)(DAT_80612db0 + 0xb0) + *(int*)(rec + 8) * 0x58;
}

// Returns 1 if every subslot of the zone record is filled (all +0x30 flags
// non-zero), else 0. (Counts filled subslots and compares to the total.)
unsigned int zone_all_subslots_filled(int zoneRec)
{
    int count = *(int*)(zoneRec + 4);
    int filled = 0;
    int off = 0;
    int remaining = count;
    if (count > 0) {
        do {
            if (*(int*)(zoneRec + off + 0x30) != 0) {
                filled++;
            }
            off += 4;
            remaining--;
        } while (remaining != 0);
    }
    return countLeadingZeros(count - filled) >> 5;
}

// Strict point-in-box test: true iff boxMin < p < boxMax on all 3 axes.
int point_in_box(float* p, float* boxMin, float* boxMax)
{
    if (boxMin[0] < p[0] && p[0] < boxMax[0] &&
        boxMin[1] < p[1] && p[1] < boxMax[1] &&
        boxMin[2] < p[2] && p[2] < boxMax[2]) {
        return 1;
    }
    return 0;
}

// Test whether an entity's world position (base +0x64/0x68/0x6c plus offset
// +0x1a8/0x1ac/0x1b0) lies strictly inside the zone box.
bool entity_in_zone_box(int entity, float* boxMin, float* boxMax)
{
    float p[3];
    p[0] = *(float*)(entity + 0x64) + *(float*)(entity + 0x1a8);
    p[1] = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
    p[2] = *(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0);
    return point_in_box(p, boxMin, boxMax) != 0;
}

// Set a zone objective record's state word (+0x18). In story mode
// (DAT_805015bd == 3) always 1; otherwise 1 when partial occupancy, else 2.
void zone_slot_set_state(int rec, int partial)
{
    if (DAT_805015bd != '\x03') {
        int state = 2;
        if (partial != 0) {
            state = 1;
        }
        *(int*)(rec + 0x18) = state;
        return;
    }
    *(int*)(rec + 0x18) = 1;
}

// For one entity, if it is inside the zone box, mark the subslot filled and
// advance the objective state depending on the zone's occupancy rule (zone
// descriptor +8): rule 1 = any-entity, rule 2 = all-subslots-filled. In story
// mode a newly filled subslot also latches the record's +8 flag. Returns
// nonzero only when the objective completes (mirrors caller's early-out).
int zone_check_entity_occupancy(int entity, float* boxMin, float* boxMax,
                                int* rec, int zoneRec, int subIdx)
{
    int zoneDesc = DAT_805c7c10 + *rec * 0xb4;
    if (entity_in_zone_box(entity, boxMin, boxMax)) {
        int subFlag = zoneRec + subIdx * 4;
        if (*(int*)(subFlag + 0x30) == 0 && DAT_805015bd == '\x03') {
            rec[8] = 1;
        }
        *(int*)(subFlag + 0x30) = 1;
        int rule = *(int*)(zoneDesc + 8);
        if (rule == 1) {
            objective_state_touch();
            zone_slot_set_state((int)rec, 0);
        } else if (rule == 2 && zone_all_subslots_filled(zoneRec) != 0) {
            objective_state_touch();
            zone_slot_set_state((int)rec, 1);
        }
    }
    return 0;
}

// Per-frame trigger-zone occupancy update for the current objective record:
// resolves the zone box for each subslot entity, then tests every relevant
// character (all player instances in mode 10, or waypoint-bound characters in
// story mode 3) for containment via zone_check_entity_occupancy.
void zone_update_occupancy(void)
{
    int* rec = (int*)FUN_803826d8();
    int subIdx = 0;
    int subOff = 0;
    int zoneRec = *(int*)(DAT_80612db0 + 0xb0) + rec[2] * 0x58;
    int subCount = *(int*)(zoneRec + 4);
    float boxMin[3];
    float boxMax[3];
    while (subIdx < subCount) {
        int subEntity = *(int*)(*(int*)(DAT_80612db0 + 0xb0) + rec[2] * 0x58 + subOff + 8);
        if (zone_compute_bounds(subEntity, boxMin, boxMax)) {
            if (DAT_805015bd == 10) {
                int pOff = 0;
                for (int p = 0; p < DAT_80612610; p++) {
                    int inst = *(int*)(DAT_80611d78 + pOff + 0x14);
                    if (*(int*)(*(int*)(inst + 0x150) + 0x114) == 0x1000) {
                        inst = slot_find_player_instance();
                    }
                    if (zone_check_entity_occupancy(inst, boxMin, boxMax,
                                                    rec, zoneRec, subIdx) != 0) {
                        objective_state_touch();
                        FUN_80382724();
                        return;
                    }
                    pOff += 0x141c;
                }
            } else if (DAT_805015bd == 3) {
                int wOff = 0;
                for (int w = 0; w < DAT_80612098; w++) {
                    int* wp = *(int**)((int)&DAT_80588610 + wOff);
                    if (wp[0x21] == 1) {
                        int inst = wp[0];
                        bool skip = false;
                        if (*(int*)(inst + 0x150) != 0) {
                            int kind = *(int*)(*(int*)(inst + 0x150) + 0x114);
                            if (kind == 0x10) {
                                skip = true;
                            } else if (kind == 0x1000) {
                                inst = slot_find_player_instance();
                            }
                        }
                        if (!skip &&
                            zone_check_entity_occupancy(inst, boxMin, boxMax,
                                                        rec, zoneRec, subIdx) != 0) {
                            objective_state_touch();
                            FUN_80382724();
                            return;
                        }
                    }
                    wOff += 4;
                }
            }
        }
        subIdx++;
        subOff += 4;
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// batch_990 additions: zone-slot registration (dedup into +0xb0 table) plus the
// counter/timer objective subsystem. Address range 0x8034bae4 - 0x8034c050.
//
// Counter objectives live in a table at award context +0xa0 (count +0x9c,
// stride 0x10): word[0] = slot/zone id, +4 = counter value, +8 = flag bits,
// +0xc = pointer to the level objective descriptor. The register_* functions
// find-or-create a table entry for the record's slot id (+0xc) and install a
// per-type comparator/action handler at slot word [4]; the small handlers below
// are those installed pointers.
// ---------------------------------------------------------------------------

extern int FUN_803826e8(void);   // acquire objective cursor A (rec hi / levelInfo lo)
extern void FUN_80382734(void);  // release objective cursor A
extern int FUN_803826f0(void);   // acquire objective cursor B (rec hi / levelInfo lo)
extern void FUN_8038273c(void);  // release objective cursor B
extern int  FUN_8035471c(void);  // predicate: a related level objective exists
extern void memset_wrapper(int dst, int val, int len);

// Forward decls for the counter handlers defined below.
bool counter_reached_target(int rec);
bool counter_exceeds_target(int rec);
bool counter_below_target(int rec);
unsigned int counter_set_flag_bit1(int rec);
unsigned int counter_clear_flag_bit1(int rec);
unsigned int counter_reset_value(int rec);

// Register a trigger-zone objective slot with de-duplication. Builds the slot
// descriptor (award ctx count +0x80, array +0x84, stride 0x28), then find-or-
// creates a zone record in the +0xb0 table (count +0xac, stride 0x58) keyed by
// the record's zone id (+0xc). A new zone record copies the subslot count
// (level info +0x128 table, +0x20) and per-subslot ids (+0x28), zeroing the
// fill words. Installs zone_update_occupancy for type 1/2 slots.
void objective_register_zone_slot(void)
{
    long long cur = ((long long(*)(void))FUN_803826e8)();
    int rec = (int)((unsigned long long)cur >> 0x20);
    int levelInfo = (int)cur;

    int idx = *(int*)(DAT_80612db0 + 0x80);
    *(int*)(DAT_80612db0 + 0x80) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x84) + idx * 0x28 + 4);
    spawn_slot_build_descriptor(rec, levelInfo, slot);

    slot[2] = -1;
    int zoneId = (int)*(short*)(rec + 0xc);
    int count = *(int*)(DAT_80612db0 + 0xac);
    int off = 0;
    for (int i = 0; i < count; i++) {
        if (zoneId == *(int*)(*(int*)(DAT_80612db0 + 0xb0) + off)) {
            slot[2] = i;
        }
        off += 0x58;
        count = *(int*)(DAT_80612db0 + 0xac);
    }
    if (slot[2] == -1) {
        int* zrec = (int*)(*(int*)(DAT_80612db0 + 0xb0) + count * 0x58);
        int subCount = *(int*)(*(int*)(levelInfo + 0x128) + zoneId * 0x8c + 0x20);
        slot[2] = count;
        zrec[0] = zoneId;
        zrec[1] = subCount;
        memset_wrapper((int)(zrec + 2), 0, 0x28);
        memset_wrapper((int)(zrec + 0xc), 0, 0x28);
        int srcOff = 0;
        int dstOff = 0;
        for (int s = 0; s < subCount; s++) {
            *(unsigned int*)((int)zrec + dstOff + 8) =
                (unsigned int)*(unsigned short*)(*(int*)(levelInfo + 0x128) +
                                                 zoneId * 0x8c + srcOff + 0x28);
            dstOff += 4;
            srcOff += 8;
        }
        *(int*)(DAT_80612db0 + 0xac) = *(int*)(DAT_80612db0 + 0xac) + 1;
    }
    if (*(int*)(rec + 8) == 1 || *(int*)(rec + 8) == 2) {
        slot[4] = (int)&zone_update_occupancy;
    }
    FUN_80382734();
}

// Register a bare objective slot (award ctx count +0x88, array +0x8c, stride
// 0x24) with only its descriptor; no handler installed.
void objective_register_empty_slot(unsigned int rec, unsigned int arg)
{
    int idx = *(int*)(DAT_80612db0 + 0x88);
    *(int*)(DAT_80612db0 + 0x88) = idx + 1;
    spawn_slot_build_descriptor(rec, arg, (int*)(*(int*)(DAT_80612db0 + 0x8c) + idx * 0x24));
}

// Counter handler: fires when the slot's target (+0x14) is <= the counter value
// stored in the +0xa0 table entry (+4). Returns true when it fires.
bool counter_reached_target(int rec)
{
    bool fire = *(int*)(rec + 0x14) <=
                *(int*)(*(int*)(DAT_80612db0 + 0xa0) + *(int*)(rec + 8) * 0x10 + 4);
    if (fire) {
        objective_state_touch();
    }
    return fire;
}

// Counter handler: fires when the counter value (+4) exceeds the target (+0x14).
bool counter_exceeds_target(int rec)
{
    bool fire = *(int*)(*(int*)(DAT_80612db0 + 0xa0) + *(int*)(rec + 8) * 0x10 + 4) <
                *(int*)(rec + 0x14);
    if (fire) {
        objective_state_touch();
    }
    return fire;
}

// Counter handler: fires when the target (+0x14) is strictly below the counter
// value (+4).
bool counter_below_target(int rec)
{
    bool fire = *(int*)(rec + 0x14) <
                *(int*)(*(int*)(DAT_80612db0 + 0xa0) + *(int*)(rec + 8) * 0x10 + 4);
    if (fire) {
        objective_state_touch();
    }
    return fire;
}

// Register a counter objective slot (award ctx count +0x80, array +0x84, stride
// 0x28) with find-or-create of the counter table entry (+0xa0). A new entry
// records the slot id (+0xc), zeroes the value/flags, sets the "primary" bit 4
// when the level descriptor marks it (+0x88==1), sets bit 8 when a related
// objective exists (FUN_8035471c), and stores the descriptor pointer. The slot
// deadline (+5) is record's +0x10 scaled by the tick rate. Installs a comparator
// by type: 0xc reached-target, 0x18 exceeds-target, 0x19 below-target.
void objective_register_counter_slot(void)
{
    long long cur = ((long long(*)(void))FUN_803826f0)();
    int rec = (int)((unsigned long long)cur >> 0x20);
    int levelInfo = (int)cur;

    int idx = *(int*)(DAT_80612db0 + 0x80);
    *(int*)(DAT_80612db0 + 0x80) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x84) + idx * 0x28 + 4);
    spawn_slot_build_descriptor(rec, levelInfo, slot);

    slot[2] = -1;
    int slotId = (int)*(short*)(rec + 0xc);
    int off = 0;
    for (int i = 0; i < *(int*)(DAT_80612db0 + 0x9c); i++) {
        if (slotId == *(int*)(*(int*)(DAT_80612db0 + 0xa0) + off)) {
            slot[2] = i;
        }
        off += 0x10;
    }
    if (slot[2] == -1) {
        int n = *(int*)(DAT_80612db0 + 0x9c);
        int base = *(int*)(DAT_80612db0 + 0xa0);
        slot[2] = n;
        *(int*)(base + n * 0x10) = slotId;
        *(int*)(base + n * 0x10 + 4) = 0;
        *(int*)(base + n * 0x10 + 8) = 0;
        if (*(int*)(*(int*)(levelInfo + 0x128) + slotId * 0x8c + 0x88) == 1) {
            int e = base + n * 0x10;
            *(unsigned int*)(e + 8) = *(unsigned int*)(e + 8) | 4;
        }
        int related = FUN_8035471c();
        int e = base + n * 0x10;
        *(unsigned int*)(e + 8) = *(unsigned int*)(e + 8) | (~-(unsigned int)(related != 0) & 8);
        *(int*)(base + n * 0x10 + 0xc) = *(int*)(levelInfo + 0x128) + slotId * 0x8c;
        *(int*)(DAT_80612db0 + 0x9c) = n + 1;
    }
    slot[5] = *(int*)(rec + 0x10) * DAT_806109c0;
    int type = *(int*)(rec + 8);
    if (type == 0x18) {
        slot[4] = (int)&counter_exceeds_target;
    } else if (type < 0x18) {
        if (type == 0xc) {
            slot[4] = (int)&counter_reached_target;
        }
    } else if (type < 0x1a) {
        slot[4] = (int)&counter_below_target;
    }
    FUN_8038273c();
}

// Counter handler: OR flag bit 2 into the counter table entry (+8).
unsigned int counter_set_flag_bit1(int rec)
{
    int e = *(int*)(DAT_80612db0 + 0xa0) + *(int*)(rec + 8) * 0x10;
    *(unsigned int*)(e + 8) = *(unsigned int*)(e + 8) | 2;
    objective_state_touch();
    return 0;
}

// Counter handler: clear flag bit 2 in the counter table entry (+8).
unsigned int counter_clear_flag_bit1(int rec)
{
    int e = *(int*)(DAT_80612db0 + 0xa0) + *(int*)(rec + 8) * 0x10;
    *(unsigned int*)(e + 8) = *(unsigned int*)(e + 8) & 0xfffffffd;
    objective_state_touch();
    return 0;
}

// Counter handler: reset the counter value (+4) to 0.
unsigned int counter_reset_value(int rec)
{
    *(int*)(*(int*)(DAT_80612db0 + 0xa0) + *(int*)(rec + 8) * 0x10 + 4) = 0;
    objective_state_touch();
    return 0;
}

// Register a counter objective slot that installs an action handler (award ctx
// count +0x88, array +0x8c, stride 0x24). Find-or-creates the counter table
// entry exactly like objective_register_counter_slot, then installs by type:
// 5 set-flag, 6 clear-flag, 0xf reset-value.
void objective_register_counter_action_slot(void)
{
    long long cur = ((long long(*)(void))FUN_803826f0)();
    int rec = (int)((unsigned long long)cur >> 0x20);
    int levelInfo = (int)cur;

    int idx = *(int*)(DAT_80612db0 + 0x88);
    *(int*)(DAT_80612db0 + 0x88) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x8c) + idx * 0x24);
    spawn_slot_build_descriptor(rec, levelInfo, slot);

    slot[2] = -1;
    int slotId = (int)*(short*)(rec + 0xc);
    int off = 0;
    for (int i = 0; i < *(int*)(DAT_80612db0 + 0x9c); i++) {
        if (slotId == *(int*)(*(int*)(DAT_80612db0 + 0xa0) + off)) {
            slot[2] = i;
        }
        off += 0x10;
    }
    if (slot[2] == -1) {
        int n = *(int*)(DAT_80612db0 + 0x9c);
        int base = *(int*)(DAT_80612db0 + 0xa0);
        slot[2] = n;
        *(int*)(base + n * 0x10) = slotId;
        *(int*)(base + n * 0x10 + 4) = 0;
        *(int*)(base + n * 0x10 + 8) = 0;
        if (*(int*)(*(int*)(levelInfo + 0x128) + slotId * 0x8c + 0x88) == 1) {
            int e = base + n * 0x10;
            *(unsigned int*)(e + 8) = *(unsigned int*)(e + 8) | 4;
        }
        int related = FUN_8035471c();
        int e = base + n * 0x10;
        *(unsigned int*)(e + 8) = *(unsigned int*)(e + 8) | (~-(unsigned int)(related != 0) & 8);
        *(int*)(base + n * 0x10 + 0xc) = *(int*)(levelInfo + 0x128) + slotId * 0x8c;
        *(int*)(DAT_80612db0 + 0x9c) = n + 1;
    }
    int type = *(int*)(rec + 8);
    if (type == 6) {
        slot[4] = (int)&counter_clear_flag_bit1;
    } else if (type < 6) {
        if (type > 4) {
            slot[4] = (int)&counter_set_flag_bit1;
        }
    } else if (type == 0xf) {
        slot[4] = (int)&counter_reset_value;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// batch_991 additions: the tally objective subsystem. Address range
// 0x8034c248 - 0x8034c870. Structurally parallel to the batch_990 counter
// subsystem but operating on a second table at award context +0x98 (count
// +0x94, stride 0x10, same layout: [0]=slot id, +4=tally value, +8=flags,
// +0xc=descriptor). These slots accumulate a running tally (add/subtract) and
// compare it to a per-slot target. FUN_8034c870 instead compares against the
// global scalar at +0xa4.
// ---------------------------------------------------------------------------

extern int FUN_803547a8(void);   // predicate: a related level objective exists (tally variant)

// Forward decls for the tally handlers defined below.
bool tally_reached_target(int rec);
bool tally_equals_target(int rec);
bool tally_exceeds_target(int rec);
bool tally_below_target(int rec);
unsigned int tally_add_value(int rec);
unsigned int tally_subtract_value(int rec);
unsigned int tally_reset_value(int rec);

// Tally comparator: fires when target (+0x14) <= tally value (+0x98 table +4).
bool tally_reached_target(int rec)
{
    bool fire = *(int*)(rec + 0x14) <=
                *(int*)(*(int*)(DAT_80612db0 + 0x98) + *(int*)(rec + 8) * 0x10 + 4);
    if (fire) {
        objective_state_touch();
    }
    return fire;
}

// Tally comparator: fires when tally value (+4) == target (+0x14).
bool tally_equals_target(int rec)
{
    bool fire = *(int*)(*(int*)(DAT_80612db0 + 0x98) + *(int*)(rec + 8) * 0x10 + 4) ==
                *(int*)(rec + 0x14);
    if (fire) {
        objective_state_touch();
    }
    return fire;
}

// Tally comparator: fires when tally value (+4) < target (+0x14).
bool tally_exceeds_target(int rec)
{
    bool fire = *(int*)(*(int*)(DAT_80612db0 + 0x98) + *(int*)(rec + 8) * 0x10 + 4) <
                *(int*)(rec + 0x14);
    if (fire) {
        objective_state_touch();
    }
    return fire;
}

// Tally comparator: fires when target (+0x14) < tally value (+4).
bool tally_below_target(int rec)
{
    bool fire = *(int*)(rec + 0x14) <
                *(int*)(*(int*)(DAT_80612db0 + 0x98) + *(int*)(rec + 8) * 0x10 + 4);
    if (fire) {
        objective_state_touch();
    }
    return fire;
}

// Register a tally objective slot (award ctx count +0x80, array +0x84, stride
// 0x28) with find-or-create of the tally table entry (+0x98, count +0x94). New
// entries mirror the counter table setup (slot id +0xc, zeroed value/flags,
// primary bit 4 when descriptor +0x88==1, related bit 8 via FUN_803547a8,
// descriptor pointer +0xc). Deadline (+5) = record +0x10. Installs comparator by
// type: 0xc reached, 0x17 equals, 0x18 exceeds, 0x19 below.
void objective_register_tally_slot(void)
{
    long long cur = ((long long(*)(void))FUN_803826f0)();
    int rec = (int)((unsigned long long)cur >> 0x20);
    int levelInfo = (int)cur;

    int idx = *(int*)(DAT_80612db0 + 0x80);
    *(int*)(DAT_80612db0 + 0x80) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x84) + idx * 0x28 + 4);
    spawn_slot_build_descriptor(rec, levelInfo, slot);

    slot[2] = -1;
    int slotId = (int)*(short*)(rec + 0xc);
    int off = 0;
    for (int i = 0; i < *(int*)(DAT_80612db0 + 0x94); i++) {
        if (slotId == *(int*)(*(int*)(DAT_80612db0 + 0x98) + off)) {
            slot[2] = i;
        }
        off += 0x10;
    }
    if (slot[2] == -1) {
        int n = *(int*)(DAT_80612db0 + 0x94);
        int base = *(int*)(DAT_80612db0 + 0x98);
        slot[2] = n;
        *(int*)(base + n * 0x10) = slotId;
        *(int*)(base + n * 0x10 + 4) = 0;
        *(int*)(base + n * 0x10 + 8) = 0;
        if (*(int*)(*(int*)(levelInfo + 0x128) + slotId * 0x8c + 0x88) == 1) {
            int e = base + n * 0x10;
            *(unsigned int*)(e + 8) = *(unsigned int*)(e + 8) | 4;
        }
        int related = FUN_803547a8();
        int e = base + n * 0x10;
        *(unsigned int*)(e + 8) = *(unsigned int*)(e + 8) | (~-(unsigned int)(related != 0) & 8);
        *(int*)(base + n * 0x10 + 0xc) = *(int*)(levelInfo + 0x128) + slotId * 0x8c;
        *(int*)(DAT_80612db0 + 0x94) = n + 1;
    }
    slot[5] = *(int*)(rec + 0x10);
    int type = *(int*)(rec + 8);
    if (type == 0x17) {
        slot[4] = (int)&tally_equals_target;
    } else if (type < 0x17) {
        if (type == 0xc) {
            slot[4] = (int)&tally_reached_target;
        }
    } else if (type == 0x19) {
        slot[4] = (int)&tally_below_target;
    } else if (type < 0x19) {
        slot[4] = (int)&tally_exceeds_target;
    }
    FUN_8038273c();
}

// Tally action: add the record's amount (+0x14) to the tally value (+4).
unsigned int tally_add_value(int rec)
{
    int e = *(int*)(DAT_80612db0 + 0x98) + *(int*)(rec + 8) * 0x10;
    *(int*)(e + 4) = *(int*)(e + 4) + *(int*)(rec + 0x14);
    objective_state_touch();
    return 0;
}

// Tally action: subtract the record's amount (+0x14) from the tally value (+4).
unsigned int tally_subtract_value(int rec)
{
    int e = *(int*)(DAT_80612db0 + 0x98) + *(int*)(rec + 8) * 0x10;
    *(int*)(e + 4) = *(int*)(e + 4) - *(int*)(rec + 0x14);
    objective_state_touch();
    return 0;
}

// Tally action: reset the tally value (+4) to 0.
unsigned int tally_reset_value(int rec)
{
    *(int*)(*(int*)(DAT_80612db0 + 0x98) + *(int*)(rec + 8) * 0x10 + 4) = 0;
    objective_state_touch();
    return 0;
}

// Register a tally objective slot that installs an accumulate/reset action
// (award ctx count +0x88, array +0x8c, stride 0x24). Find-or-creates the tally
// table entry exactly like objective_register_tally_slot, then installs by
// type: 3 add, 4 subtract, 0xf reset.
void objective_register_tally_action_slot(void)
{
    long long cur = ((long long(*)(void))FUN_803826f0)();
    int rec = (int)((unsigned long long)cur >> 0x20);
    int levelInfo = (int)cur;

    int idx = *(int*)(DAT_80612db0 + 0x88);
    *(int*)(DAT_80612db0 + 0x88) = idx + 1;
    int* slot = (int*)(*(int*)(DAT_80612db0 + 0x8c) + idx * 0x24);
    spawn_slot_build_descriptor(rec, levelInfo, slot);

    slot[2] = -1;
    int slotId = (int)*(short*)(rec + 0xc);
    int off = 0;
    for (int i = 0; i < *(int*)(DAT_80612db0 + 0x94); i++) {
        if (slotId == *(int*)(*(int*)(DAT_80612db0 + 0x98) + off)) {
            slot[2] = i;
        }
        off += 0x10;
    }
    if (slot[2] == -1) {
        int n = *(int*)(DAT_80612db0 + 0x94);
        int base = *(int*)(DAT_80612db0 + 0x98);
        slot[2] = n;
        *(int*)(base + n * 0x10) = slotId;
        *(int*)(base + n * 0x10 + 4) = 0;
        *(int*)(base + n * 0x10 + 8) = 0;
        if (*(int*)(*(int*)(levelInfo + 0x128) + slotId * 0x8c + 0x88) == 1) {
            int e = base + n * 0x10;
            *(unsigned int*)(e + 8) = *(unsigned int*)(e + 8) | 4;
        }
        int related = FUN_803547a8();
        int e = base + n * 0x10;
        *(unsigned int*)(e + 8) = *(unsigned int*)(e + 8) | (~-(unsigned int)(related != 0) & 8);
        *(int*)(base + n * 0x10 + 0xc) = *(int*)(levelInfo + 0x128) + slotId * 0x8c;
        *(int*)(DAT_80612db0 + 0x94) = n + 1;
    }
    slot[5] = *(int*)(rec + 0x10);
    int type = *(int*)(rec + 8);
    if (type == 4) {
        slot[4] = (int)&tally_subtract_value;
    } else if (type < 4) {
        if (type > 2) {
            slot[4] = (int)&tally_add_value;
        }
    } else if (type == 0xf) {
        slot[4] = (int)&tally_reset_value;
    }
    FUN_8038273c();
}

// Comparator against the global objective scalar (award ctx +0xa4): fires when
// the record's target (+0x14) is <= that scalar.
bool global_counter_reached(int rec)
{
    bool fire = *(int*)(rec + 0x14) <= *(int*)(DAT_80612db0 + 0xa4);
    if (fire) {
        objective_state_touch();
    }
    return fire;
}

// ============================================================================
// batch_992: global-counter comparators/actions, the global-counter and
// global-tally action-slot registration, and the roster/waypoint stat-level
// class registration slot. Continues the award-context slot system in the
// region immediately following global_counter_reached.
//
// The global objective scalar lives at award context +0xa4. The counter-slot
// table grows at +0x80 (count) / +0x84 (base, stride 0x28); the tally-action
// table grows at +0x88 (count) / +0x8c (base, stride 0x24). +0xa8 caches the
// "related objective exists" predicate result recomputed on each registration.
// ============================================================================
// ---- batch_992 per-file externs ----
extern int  FUN_803547c8(void);   // recompute "related level objective exists" (result cached at +0xa8)
extern void FUN_80353d78(unsigned long long, unsigned long long, unsigned long long,
                         unsigned long long, unsigned long long, unsigned long long,
                         unsigned long long, unsigned long long, int owner); // waypoint reached -> award
extern void FUN_80353dc4(int owner);                    // waypoint completion cleanup
extern void FUN_8038b510(int* dstText, int* srcTypeRecord);   // copy type-record display string
extern int* aveditor_type_record_ptr(int base, int flags);
extern int  text_wrap_to_width(void);
extern void statlvl_class_register(int a, int b, int x, int y, int z);
extern void statlvl_class_disarm(int classId);

// Comparator: fires when the global objective scalar (+0xa4) is below the
// record's target value (+0x14).
bool global_counter_below_target(int rec)
{
    bool fire = *(int*)(DAT_80612db0 + 0xa4) < *(int*)(rec + 0x14);
    if (fire) {
        objective_state_touch();
    }
    return fire;
}

// Comparator: fires when the record's target value (+0x14) is below the global
// objective scalar (+0xa4).
bool global_counter_exceeds_target(int rec)
{
    bool fire = *(int*)(rec + 0x14) < *(int*)(DAT_80612db0 + 0xa4);
    if (fire) {
        objective_state_touch();
    }
    return fire;
}

// Register a global-counter comparator slot into the counter table at award
// context +0x80/+0x84 (stride 0x28). Installs a comparator against the global
// scalar selected by the record's objective type (+8): reached (0xc), below
// target (0x18), or exceeds target (0x19).
void objective_register_global_counter_slot(unsigned int recId, unsigned int levelInfo)
{
    int index = *(int*)(DAT_80612db0 + 0x80);
    int* slot = (int*)(DAT_80612db0 + 0x84);
    *(int*)(DAT_80612db0 + 0x80) = index + 1;
    slot = (int*)(*slot + index * 0x28 + 4);
    spawn_slot_build_descriptor(recId, levelInfo, slot);
    *(int*)(DAT_80612db0 + 0xa8) = FUN_803547c8();
    slot[5] = *(int*)((int)recId + 0x10);
    int type = *(int*)((int)recId + 8);
    if (type == 0x18) {
        slot[4] = (int)&global_counter_below_target;
    } else if (type < 0x18) {
        if (type == 0xc) {
            slot[4] = (int)&global_counter_reached;
        }
    } else if (type < 0x1a) {
        slot[4] = (int)&global_counter_exceeds_target;
    }
}

// Action: add the record's amount (+0x14) to the global objective scalar (+0xa4).
int global_counter_add_value(int rec)
{
    *(int*)(DAT_80612db0 + 0xa4) = *(int*)(DAT_80612db0 + 0xa4) + *(int*)(rec + 0x14);
    objective_state_touch();
    return 0;
}

// Action: subtract the record's amount (+0x14) from the global scalar (+0xa4).
int global_counter_subtract_value(int rec)
{
    *(int*)(DAT_80612db0 + 0xa4) = *(int*)(DAT_80612db0 + 0xa4) - *(int*)(rec + 0x14);
    objective_state_touch();
    return 0;
}

// Action: reset the global objective scalar (+0xa4) to zero.
int global_counter_reset_value(void)
{
    *(int*)(DAT_80612db0 + 0xa4) = 0;
    objective_state_touch();
    return 0;
}

// Register a global-counter action slot into the tally-action table at award
// context +0x88/+0x8c (stride 0x24). Installs an add/subtract/reset action
// against the global scalar selected by the record's objective type (+8):
// add (3), subtract (4), reset (0xf).
void objective_register_global_counter_action_slot(unsigned int recId, unsigned int levelInfo)
{
    int index = *(int*)(DAT_80612db0 + 0x88);
    int* slot = (int*)(DAT_80612db0 + 0x8c);
    *(int*)(DAT_80612db0 + 0x88) = index + 1;
    slot = (int*)(*slot + index * 0x24);
    spawn_slot_build_descriptor(recId, levelInfo, slot);
    *(int*)(DAT_80612db0 + 0xa8) = FUN_803547c8();
    slot[5] = *(int*)((int)recId + 0x10);
    int type = *(int*)((int)recId + 8);
    if (type == 4) {
        slot[4] = (int)&global_counter_subtract_value;
    } else if (type < 4) {
        if (type > 2) {
            slot[4] = (int)&global_counter_add_value;
        }
    } else if (type == 0xf) {
        slot[4] = (int)&global_counter_reset_value;
    }
}

// Waypoint handler: dispatch the "reached" award for the slot's owner and latch
// the slot state to 3 (reached). The eight leading register args are the ABI
// spill for FUN_80353d78's floating/vector argument block.
int waypoint_slot_reached(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                          unsigned long long a4, unsigned long long a5, unsigned long long a6,
                          unsigned long long a7, unsigned long long a8, int slotRec)
{
    FUN_80353d78(a1, a2, a3, a4, a5, a6, a7, a8, *(int*)(slotRec + 8));
    *(int*)(slotRec + 0x18) = 3;
    objective_state_touch();
    return 0;
}

// Waypoint handler: run completion cleanup for the slot's owner and latch the
// slot state to 8 (completed/cleared).
int waypoint_slot_completed(int slotRec)
{
    FUN_80353dc4(*(int*)(slotRec + 8));
    *(int*)(slotRec + 0x18) = 8;
    objective_state_touch();
    return 0;
}

// Register a roster/waypoint stat-level class slot. Resolves the objective
// record's roster-class id (+0xc), finds or creates a matching stat-level class
// entry (roster table at award ctx +0x13c, stride 0x8c; live count at +0x1950),
// copies its display string and arming flags, installs the reached/completed
// waypoint handler by type (+8: 0xd == completed), and registers the class with
// the stat-level tracker unless it was already present.
void objective_register_statlevel_class_slot(void)
{
    unsigned long long cur = (unsigned long long)(unsigned int)FUN_803826e8();
    int rec = (int)(cur >> 0x20);
    int levelInfo = (int)cur;
    int classIndex = -1;
    bool alreadyPresent = false;

    int slotIndex = *(int*)(DAT_80612db0 + 0x88);
    int* slot = (int*)(DAT_80612db0 + 0x8c);
    *(int*)(DAT_80612db0 + 0x88) = slotIndex + 1;
    slot = (int*)(*slot + slotIndex * 0x24);
    spawn_slot_build_descriptor((unsigned int)rec, (unsigned int)levelInfo, slot);

    int scan = 0;
    int off = 0;
    int remaining = *(int*)(DAT_80612db0 + 0x1950);
    if (0 < remaining) {
        do {
            if (*(int*)(DAT_80612db0 + off + 0x13c) == (int)*(short*)(rec + 0xc)) {
                alreadyPresent = true;
                classIndex = scan;
                break;
            }
            scan = scan + 1;
            off = off + 0x8c;
            remaining = remaining - 1;
        } while (remaining != 0);
    }

    if (classIndex == -1) {
        int* typeRec = aveditor_type_record_ptr(
            *(int*)(levelInfo + 0x128) + *(short*)(rec + 0xc) * 0x8c, 0);
        int live = *(int*)(DAT_80612db0 + 0x1950);
        FUN_8038b510((int*)(DAT_80612db0 + live * 0x8c + 0xb8), typeRec);
        text_wrap_to_width();
        *(int*)(DAT_80612db0 + live * 0x8c + 0xb4) = 5;
        *(int*)(DAT_80612db0 + live * 0x8c + 0x138) = live;
        *(int*)(DAT_80612db0 + live * 0x8c + 0x13c) = (int)*(short*)(rec + 0xc);

        int classBase = *(int*)(levelInfo + 0xd4);
        *(int*)(classBase + live * 0x38) = live;
        *(int*)(classBase + live * 0x38 + 0xc) = live;
        if (*(int*)(*(int*)(levelInfo + 0x128) + *(short*)(rec + 0xc) * 0x8c + 0x7c) == 0) {
            *(int*)(classBase + live * 0x38 + 0x10) = 0;
            *(int*)(classBase + live * 0x38 + 0x14) = 0;
            *(int*)(classBase + live * 0x38 + 0x18) = 0;
        } else {
            *(int*)(classBase + live * 0x38 + 0x10) = 1;
            *(int*)(classBase + live * 0x38 + 0x14) = 1;
            *(int*)(classBase + live * 0x38 + 0x18) = 1;
        }
        int arm = *(int*)(*(int*)(levelInfo + 0x128) + *(short*)(rec + 0xc) * 0x8c + 0x88);
        if (arm == 1) {
            *(int*)(classBase + live * 0x38 + 0x34) = 1;
        } else if ((arm < 1) && (-1 < arm)) {
            *(int*)(classBase + live * 0x38 + 0x34) = 0;
        }
        classIndex = *(int*)(DAT_80612db0 + 0x1950);
        *(int*)(DAT_80612db0 + 0x1950) = classIndex + 1;
    }

    slot[2] = classIndex;
    slot[6] = 5;
    if (*(int*)(rec + 8) == 0xd) {
        slot[4] = (int)&waypoint_slot_completed;
    } else {
        slot[4] = (int)&waypoint_slot_reached;
    }

    if (!alreadyPresent) {
        int classRec = *(int*)(levelInfo + 0xd4) + classIndex * 0x38;
        statlvl_class_register(classIndex, classIndex,
                               *(int*)(classRec + 0x10),
                               *(int*)(classRec + 0x14),
                               *(int*)(classRec + 0x18));
        if (*(int*)(*(int*)(levelInfo + 0xd4) + classIndex * 0x38 + 0x34) == 0) {
            statlvl_class_disarm(classIndex);
        }
    }
    FUN_80382734();
}

// ============================================================================
// batch_993: more award-context objective slot registrations + their per-frame
// handlers, continuing the region after the global-counter/roster slots.
// Covers a simple "latch state 1" handler, a broadcast-to-all-players HUD
// message handler, two zone-light animation handlers (mode-10 field vs record
// variants), and two entity-flag comparators (bit 2 of the entity's owner
// record +0x7c) plus their slot registrations.
// ============================================================================
// ---- batch_993 per-file externs ----
// 0x803826ec/0x80382738 are objective-cursor acquire/release variants that
// Ghidra mislabels as the __save_gpr_28/__restore_gpr_28 runtime glue; they
// follow the same convention as the other FUN_803826xx cursor helpers here
// (acquire returns the current objective record; release takes nothing).
extern int  FUN_803826ec(void);   // acquire objective cursor C (returns rec ptr)
extern void FUN_80382738(void);   // release objective cursor C
extern int  hud_text_submit(int player_idx, int type, unsigned int* text_buf);
extern unsigned int* aveditor_mode10_record_ptr(int cell);

// Handler: latch the slot's state (+0x18) to 1 and touch objective state.
// Returns 2 (handler-consumed sentinel used by the dispatch table).
int slot_latch_state1(int slotRec)
{
    *(int*)(slotRec + 0x18) = 1;
    objective_state_touch();
    return 2;
}

// Register a counter-table slot (award ctx +0x80/+0x84, stride 0x28) that
// installs the latch-state-1 handler unconditionally.
void objective_register_latch_slot(unsigned int recId, unsigned int levelInfo)
{
    int index = *(int*)(DAT_80612db0 + 0x80);
    int* slot = (int*)(DAT_80612db0 + 0x84);
    *(int*)(DAT_80612db0 + 0x80) = index + 1;
    slot = (int*)(*slot + index * 0x28 + 4);
    spawn_slot_build_descriptor(recId, levelInfo, slot);
    slot[4] = (int)&slot_latch_state1;
}

// Handler: broadcast the objective's text (+0x14) to every active player's HUD
// and latch the objective record state (+0x18) to 3 (completed/announced).
void objective_broadcast_text_to_players(void)
{
    int rec = FUN_803826f0();
    int off = 0;
    for (int i = 0; i < DAT_80612610; i = i + 1) {
        objective_state_touch();
        hud_text_submit(*(int*)(DAT_80611d78 + off + 4), 2, *(unsigned int**)(rec + 0x14));
        off = off + 0x141c;
    }
    *(int*)(rec + 0x18) = 3;
    FUN_8038273c();
}

// Register a tally-action-table slot (award ctx +0x88/+0x8c, stride 0x24) for
// the broadcast-text handler. Resolves the objective's type-record display
// string (+0xc index into levelInfo +0x128 table, stride 0x8c) into slot[5].
void objective_register_broadcast_text_slot(int recId, int levelInfo)
{
    int index = *(int*)(DAT_80612db0 + 0x88);
    int* slot = (int*)(DAT_80612db0 + 0x8c);
    *(int*)(DAT_80612db0 + 0x88) = index + 1;
    short typeId = *(short*)(recId + 0xc);
    slot = (int*)(*slot + index * 0x24);
    int typeTable = *(int*)(levelInfo + 0x128);
    spawn_slot_build_descriptor((unsigned int)recId, (unsigned int)levelInfo, slot);
    slot[5] = (int)aveditor_type_record_ptr(typeTable + typeId * 0x8c, 0);
    text_wrap_to_width();
    slot[4] = (int)&objective_broadcast_text_to_players;
}

// Handler: animate all zone lights of the objective's editor cell using the
// mode-10 field-pointer accessor (per-light index walk).
void zone_light_animate_by_field(int slotRec)
{
    int cell = DAT_805c7c08 + *(int*)(slotRec + 8) * 0x8c;
    for (int i = 0; i < *(int*)(cell + 0x70); i = i + 1) {
        aveditor_mode10_field_ptr(cell, i);
        particle_zone_light_update();
    }
    objective_state_touch();
}

// Handler: animate all zone lights of the objective's editor cell using the
// mode-10 record-pointer accessor (whole-cell variant).
void zone_light_animate_by_record(int slotRec)
{
    int cell = DAT_805c7c08 + *(int*)(slotRec + 8) * 0x8c;
    for (int i = 0; i < *(int*)(cell + 0x70); i = i + 1) {
        aveditor_mode10_record_ptr(cell);
        particle_zone_light_update();
    }
    objective_state_touch();
}

// Register a tally-action-table slot for a zone-light animation handler,
// selected by objective type (+8): field variant for type 0xf, record variant
// for type 0x13.
void objective_register_zone_light_slot(int recId, unsigned int levelInfo)
{
    int index = *(int*)(DAT_80612db0 + 0x88);
    int* slot = (int*)(DAT_80612db0 + 0x8c);
    *(int*)(DAT_80612db0 + 0x88) = index + 1;
    slot = (int*)(*slot + index * 0x24);
    spawn_slot_build_descriptor((unsigned int)recId, levelInfo, slot);
    int type = *(int*)(recId + 8);
    if (type == 0x13) {
        slot[4] = (int)&zone_light_animate_by_record;
    } else if ((type < 0x13) && (type == 0xf)) {
        slot[4] = (int)&zone_light_animate_by_field;
    }
}

// Comparator: resolve the entity handle in the record (+0x14), validate it
// against the tracked-entity table (stride 0x2e8, +0x2c holds the handle), and
// fire when the entity's owner record (+0x7c) has flag bit 2 set (+0xc & 2).
int entity_flag_bit2_set(int rec)
{
    unsigned int handle = *(unsigned int*)(rec + 0x14);
    int ent = 0;
    if ((handle != 0) &&
        (ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8, *(unsigned int*)(ent + 0x2c) != handle)) {
        *(int*)(rec + 0x14) = 0;
        ent = 0;
    }
    int result;
    if ((ent == 0) || (*(int*)(ent + 0x7c) == 0) ||
        ((*(unsigned int*)(*(int*)(ent + 0x7c) + 0xc) & 2) == 0)) {
        result = 0;
    } else {
        objective_state_touch();
        result = 1;
    }
    return result;
}

// Comparator: same handle validation as entity_flag_bit2_set, but fires when
// the owner-record flag bit 2 is CLEAR (inverse predicate).
int entity_flag_bit2_clear(int rec)
{
    unsigned int handle = *(unsigned int*)(rec + 0x14);
    int ent = 0;
    if ((handle != 0) &&
        (ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8, *(unsigned int*)(ent + 0x2c) != handle)) {
        *(int*)(rec + 0x14) = 0;
        ent = 0;
    }
    int result;
    if ((ent == 0) || (*(int*)(ent + 0x7c) == 0) ||
        ((*(unsigned int*)(*(int*)(ent + 0x7c) + 0xc) & 2) != 0)) {
        result = 0;
    } else {
        objective_state_touch();
        result = 1;
    }
    return result;
}

// Register a counter-table slot (+0x80/+0x84, stride 0x28) for an entity-flag
// comparator. Resolves the entity handle (record typeId +0xc, +1) into slot[5];
// installs the bit-set comparator for type 0xf or the bit-clear comparator for
// type 0xe.
void objective_register_entity_flag_slot(int recId, unsigned int levelInfo)
{
    int index = *(int*)(DAT_80612db0 + 0x80);
    int* slot = (int*)(DAT_80612db0 + 0x84);
    *(int*)(DAT_80612db0 + 0x80) = index + 1;
    slot = (int*)(*slot + index * 0x28 + 4);
    spawn_slot_build_descriptor((unsigned int)recId, levelInfo, slot);
    slot[6] = 5;
    int ent = entity_handle_resolve((int)*(short*)(recId + 0xc) + 1);
    if (ent != 0) {
        slot[5] = *(int*)(ent + 0x2c);
        int type = *(int*)(recId + 8);
        if (type == 0xf) {
            slot[4] = (int)&entity_flag_bit2_set;
        } else if ((type < 0xf) && (0xd < type)) {
            slot[4] = (int)&entity_flag_bit2_clear;
        }
    }
}

// ============================================================================
// batch_994: trigger-door entity-flag action handlers + their slot
// registration, plus stat-level class arm/disarm handlers and two sub-slot
// flag-toggle handlers keyed on the award-context sub-slot tables.
//
// Sub-slot tables in the award context:
//   +0x94 count / +0x98 base : primary sub-slot table (stride 0x10, id at +0,
//                              flags at +8).
//   +0x9c count / +0xa0 base : secondary sub-slot table (same layout).
//   +0x1950 count            : roster class count; roster table at +0x13c
//                              (class id) / +0x138 (class index), stride 0x8c.
// ============================================================================
// ---- batch_994 per-file externs ----
extern void trigger_close_if_idle(int trigger);
extern int  trigger_is_moving(int trigger);
extern void trigger_open_full(int trigger, int mode);
extern void statlvl_class_arm(int classIndex);

// Handler: resolve the record's entity handle (+0x14), open its trigger door
// (close-if-idle then set owner-record flag bit 1 on +0x7c +0xc).
int entity_trigger_set_flag(int rec)
{
    unsigned int handle = *(unsigned int*)(rec + 0x14);
    int ent = 0;
    if ((handle != 0) &&
        (ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8, *(unsigned int*)(ent + 0x2c) != handle)) {
        *(int*)(rec + 0x14) = 0;
        ent = 0;
    }
    int owner = *(int*)(ent + 0x7c);
    objective_state_touch();
    trigger_close_if_idle(ent);
    *(unsigned int*)(owner + 0xc) = *(unsigned int*)(owner + 0xc) | 2;
    return 0;
}

// Handler: resolve the record's entity handle and clear owner-record flag bit 1.
int entity_trigger_clear_flag(int rec)
{
    unsigned int handle = *(unsigned int*)(rec + 0x14);
    int ent = 0;
    if ((handle != 0) &&
        (ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8, *(unsigned int*)(ent + 0x2c) != handle)) {
        *(int*)(rec + 0x14) = 0;
        ent = 0;
    }
    int owner = *(int*)(ent + 0x7c);
    objective_state_touch();
    *(unsigned int*)(owner + 0xc) = *(unsigned int*)(owner + 0xc) & 0xfffffffd;
    return 0;
}

// Toggle a trigger door open/closed based on movement state: if idle, clear the
// owner flag bit 1 and open fully; otherwise close-if-idle.
void trigger_toggle_open_closed(int trigger)
{
    int owner = *(int*)(trigger + 0x7c);
    if (trigger_is_moving(trigger) == 0) {
        unsigned int flags = *(unsigned int*)(owner + 0xc);
        if ((flags & 2) != 0) {
            *(unsigned int*)(owner + 0xc) = flags & 0xfffffffd;
        }
        trigger_open_full(trigger, 1);
    } else {
        trigger_close_if_idle(trigger);
    }
}

// Handler: resolve the record's entity handle and toggle its trigger door.
int entity_trigger_toggle(int rec)
{
    unsigned int handle = *(unsigned int*)(rec + 0x14);
    int ent = 0;
    if ((handle != 0) &&
        (ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8, *(unsigned int*)(ent + 0x2c) != handle)) {
        *(int*)(rec + 0x14) = 0;
        ent = 0;
    }
    trigger_toggle_open_closed(ent);
    return 0;
}

// Register a tally-action-table slot (+0x88/+0x8c, stride 0x24) for a
// trigger-door action, keyed on objective type (+8): set flag (9), clear flag
// (0xa), toggle (0x15). Resolves the entity handle into slot[5].
void objective_register_trigger_slot(int recId, unsigned int levelInfo)
{
    int index = *(int*)(DAT_80612db0 + 0x88);
    int* slot = (int*)(DAT_80612db0 + 0x8c);
    *(int*)(DAT_80612db0 + 0x88) = index + 1;
    slot = (int*)(*slot + index * 0x24);
    spawn_slot_build_descriptor((unsigned int)recId, levelInfo, slot);
    int ent = entity_handle_resolve((int)*(short*)(recId + 0xc) + 1);
    if (ent != 0) {
        slot[5] = *(int*)(ent + 0x2c);
        int type = *(int*)(recId + 8);
        if (type == 10) {
            slot[4] = (int)&entity_trigger_clear_flag;
        } else if (type < 10) {
            if (8 < type) {
                slot[4] = (int)&entity_trigger_set_flag;
            }
        } else if (type == 0x15) {
            slot[4] = (int)&entity_trigger_toggle;
        }
    }
}

// Handler: find the roster class matching the record's class id (+8) in the
// roster table (+0x13c, stride 0x8c) and arm it via its class index (+0x138).
int roster_class_arm_by_id(int rec)
{
    int off = 0;
    int remaining = *(int*)(DAT_80612db0 + 0x1950);
    if (0 < remaining) {
        do {
            if (*(int*)(DAT_80612db0 + off + 0x13c) == *(int*)(rec + 8)) {
                objective_state_touch();
                statlvl_class_arm(*(int*)(DAT_80612db0 + off + 0x138));
                return 1;
            }
            off = off + 0x8c;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return 0;
}

// Handler: find the roster class matching the record's class id (+8) and disarm
// it via its class index (+0x138).
int roster_class_disarm_by_id(int rec)
{
    int off = 0;
    int remaining = *(int*)(DAT_80612db0 + 0x1950);
    if (0 < remaining) {
        do {
            if (*(int*)(DAT_80612db0 + off + 0x13c) == *(int*)(rec + 8)) {
                objective_state_touch();
                statlvl_class_disarm(*(int*)(DAT_80612db0 + off + 0x138));
                return 1;
            }
            off = off + 0x8c;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return 0;
}

// Handler: find the primary sub-slot (+0x94 count / +0x98 base, stride 0x10)
// whose id (+0) matches the record class id (+8) and set flag bit 2 (+8 |= 4).
int subslot_primary_set_flag(int rec)
{
    int off = 0;
    int remaining = *(int*)(DAT_80612db0 + 0x94);
    if (0 < remaining) {
        do {
            if (*(int*)(rec + 8) == *(int*)(*(int*)(DAT_80612db0 + 0x98) + off)) {
                objective_state_touch();
                int slot = *(int*)(DAT_80612db0 + 0x98) + off;
                *(unsigned int*)(slot + 8) = *(unsigned int*)(slot + 8) | 4;
                return 1;
            }
            off = off + 0x10;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return 0;
}

// Handler: same primary sub-slot search, but on the secondary table (+0x9c
// count / +0xa0 base). Returns 1 unconditionally at the tail (default hit).
int subslot_secondary_set_flag(int rec)
{
    int off = 0;
    int remaining = *(int*)(DAT_80612db0 + 0x9c);
    if (0 < remaining) {
        do {
            if (*(int*)(rec + 8) == *(int*)(*(int*)(DAT_80612db0 + 0xa0) + off)) {
                objective_state_touch();
                int slot = *(int*)(DAT_80612db0 + 0xa0) + off;
                *(unsigned int*)(slot + 8) = *(unsigned int*)(slot + 8) | 4;
                return 1;
            }
            off = off + 0x10;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return 1;
}

// Handler: find the primary sub-slot matching the record class id (+8) and
// clear flag bit 2 (+8 &= ~4).
int subslot_primary_clear_flag(int rec)
{
    int off = 0;
    int remaining = *(int*)(DAT_80612db0 + 0x94);
    if (0 < remaining) {
        do {
            if (*(int*)(rec + 8) == *(int*)(*(int*)(DAT_80612db0 + 0x98) + off)) {
                objective_state_touch();
                int slot = *(int*)(DAT_80612db0 + 0x98) + off;
                *(unsigned int*)(slot + 8) = *(unsigned int*)(slot + 8) & 0xfffffffb;
                return 1;
            }
            off = off + 0x10;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return 0;
}

// ============================================================================
// batch_995: secondary sub-slot clear handler, the arm/disarm meta-registration
// that selects a roster/sub-slot handler by editor sub-type, an award
// player-count threshold check + its slot registration, a squad "all reached"
// propagation handler + registration, the top-level objective slot dispatcher
// that walks the level's objective descriptors, and a small guarded helper that
// runs FUN_80351960 with a global flag forced set.
// ============================================================================
// ---- batch_995 per-file externs ----
// 0x803826dc/0x80382728 are objective-cursor acquire/release variants Ghidra
// labels as GPR glue; they match the FUN_803826xx cursor convention here.
extern int  FUN_803826dc(void);   // acquire objective cursor D (returns rec ptr)
extern void FUN_80382728(void);   // release objective cursor D
extern void FUN_80351960(void);   // rebuild/refresh objective descriptor set
extern int  entdesc_field1_cur(int typeIndex);
extern int  award_count_matching_players(int poolPtr, int filter);
extern int  award_collect_matching_players(unsigned char* poolPtr, int a, int b, int filter);
extern unsigned int* match_mode_get_config(int nMode);
extern void arcade_edit_delay_screen(int rec);
extern unsigned char* PTR_DAT_80477bd0;                            // entity-descriptor pool table (stride 0x31 words)
extern int  PTR_objective_register_switch_slot_804820d8;          // switch/tracked dispatch table (paired, base +4)
extern int  PTR_objective_register_tracked_slot_804820d4;         // switch/tracked dispatch table (paired, base +0)
extern unsigned int DAT_806108e4;                                 // "suppress side effects" global flag

// Handler: find the secondary sub-slot (+0x9c count / +0xa0 base, stride 0x10)
// matching the record class id (+8) and clear flag bit 2; tail returns 1.
int subslot_secondary_clear_flag(int rec)
{
    int off = 0;
    int remaining = *(int*)(DAT_80612db0 + 0x9c);
    if (0 < remaining) {
        do {
            if (*(int*)(rec + 8) == *(int*)(*(int*)(DAT_80612db0 + 0xa0) + off)) {
                objective_state_touch();
                int slot = *(int*)(DAT_80612db0 + 0xa0) + off;
                *(unsigned int*)(slot + 8) = *(unsigned int*)(slot + 8) & 0xfffffffb;
                return 1;
            }
            off = off + 0x10;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return 1;
}

// Register a tally-action slot (+0x88/+0x8c, stride 0x24) that installs an
// arm/set handler (type 0x16) or a disarm/clear handler (type 0x17), further
// specialized by the editor sub-type (type-record +0x80): roster (7), primary
// sub-slot (5), or secondary sub-slot (4).
void objective_register_arm_disarm_slot(void)
{
    unsigned long long cur = (unsigned long long)(unsigned int)FUN_803826f0();
    int rec = (int)(cur >> 0x20);
    int levelInfo = (int)cur;
    int index = *(int*)(DAT_80612db0 + 0x88);
    int* slot = (int*)(DAT_80612db0 + 0x8c);
    *(int*)(DAT_80612db0 + 0x88) = index + 1;
    slot = (int*)(*slot + index * 0x24);
    spawn_slot_build_descriptor((unsigned int)rec, (unsigned int)levelInfo, slot);

    if (*(int*)(rec + 8) == 0x16) {
        int sub = *(int*)(*(int*)(levelInfo + 0x128) + *(short*)(rec + 0xc) * 0x8c + 0x80);
        if (sub == 7) {
            slot[4] = (int)&roster_class_arm_by_id;
        } else if (sub == 5) {
            slot[4] = (int)&subslot_primary_set_flag;
        } else if (sub == 4) {
            slot[4] = (int)&subslot_secondary_set_flag;
        }
    } else if (*(int*)(rec + 8) == 0x17) {
        int sub = *(int*)(*(int*)(levelInfo + 0x128) + *(short*)(rec + 0xc) * 0x8c + 0x80);
        if (sub == 7) {
            slot[4] = (int)&roster_class_disarm_by_id;
        } else if (sub == 5) {
            slot[4] = (int)&subslot_primary_clear_flag;
        } else if (sub == 4) {
            slot[4] = (int)&subslot_secondary_clear_flag;
        }
    }
    FUN_8038273c();
}

// Handler: latch the slot record state (+0x18) to 1 (completed) and touch.
int award_slot_complete(int slotRec)
{
    objective_state_touch();
    *(int*)(slotRec + 0x18) = 1;
    return 1;
}

// Handler: award player-count threshold. Resolves the objective's entity
// descriptor pool (type-record +0x84 indexes PTR_DAT_80477bd0, stride 0x31
// words), counts players matching the descriptor filter, and completes the slot
// when the count reaches the configured threshold (type 9 uses the fixed
// +0x20 threshold; types 8 use the dynamic collect count).
void award_check_player_count(void)
{
    int* rec = (int*)FUN_803826e4();
    int levelInfo = (int)level_get_info_cached(DAT_80612614);
    int slotIndex = *rec;
    int slotTable = *(int*)(levelInfo + 0x130);
    int typeRec = *(int*)(levelInfo + 0x128) + rec[2] * 0x8c;
    int poolIndex = *(int*)(typeRec + 0x84);
    unsigned char* pool = (&PTR_DAT_80477bd0)[poolIndex * 0x31];
    int filter = entdesc_field1_cur(poolIndex);
    int matched = award_count_matching_players((int)pool, filter);
    int dynamic = award_collect_matching_players(pool, 0, 1, filter);
    int type = *(int*)(slotTable + slotIndex * 0xb4 + 8);
    if (type == 9) {
        if (*(int*)(typeRec + 0x20) <= matched) {
            award_slot_complete((int)rec);
            FUN_80382730();
            return;
        }
    } else if ((type < 9) && (7 < type) && (dynamic <= matched)) {
        award_slot_complete((int)rec);
        FUN_80382730();
        return;
    }
    (void)(matched - dynamic);
    FUN_80382730();
}

// Register a counter-table slot (+0x80/+0x84, stride 0x28) for the award
// player-count handler when the objective type (+8) is in [8, 9].
void objective_register_player_count_slot(int recId, unsigned int levelInfo)
{
    int index = *(int*)(DAT_80612db0 + 0x80);
    int* slot = (int*)(DAT_80612db0 + 0x84);
    *(int*)(DAT_80612db0 + 0x80) = index + 1;
    slot = (int*)(*slot + index * 0x28 + 4);
    spawn_slot_build_descriptor((unsigned int)recId, levelInfo, slot);
    slot[6] = 5;
    if ((*(int*)(recId + 8) < 10) && (7 < *(int*)(recId + 8))) {
        slot[4] = (int)&award_check_player_count;
    }
}

// Register a bare tally-action slot (+0x88/+0x8c, stride 0x24) with no handler
// (descriptor only).
void objective_register_bare_action_slot(unsigned int recId, unsigned int levelInfo)
{
    int index = *(int*)(DAT_80612db0 + 0x88);
    int* slot = (int*)(DAT_80612db0 + 0x8c);
    *(int*)(DAT_80612db0 + 0x88) = index + 1;
    spawn_slot_build_descriptor(recId, levelInfo, (int*)(*slot + index * 0x24));
}

// Handler: on an objective's linked entity dying, mark it state 5 and propagate
// state 5 to every squad member (squad table at award ctx +0 base, +0x1f count,
// stride 0x25e; per-member short id at +2, state at +0xca). For the killed
// entity's match config, if a flag (config[8] & 0x400) is clear and its vtable
// slot +0x10 returns nonzero, set entity flag bit 0x40.
void squad_propagate_death(void)
{
    int cursor = FUN_803826dc();
    int levelInfo = (int)level_get_info_cached(DAT_80612614);
    int ent = entity_pool_find_type1(
        *(int*)(*(int*)(levelInfo + 0x128) + *(int*)(cursor + 8) * 0x8c + 0x7c));
    objective_state_touch();
    *(int*)(ent + 0x18) = 5;
    int stateOff = 0;
    int squadOff = 0;
    for (int s = 0; s < *(int*)(DAT_80612db0 + 0x1f * 4); s = s + 1) {
        int memberOff = 0;
        short* squad = (short*)(*(int*)DAT_80612db0 + squadOff);
        for (int m = 0; m < *squad; m = m + 1) {
            int member = entity_pool_find_type1((int)*(short*)((int)squad + memberOff + 2));
            if (ent == member) {
                int mode = *(int*)(ent + 4);
                *(short*)((int)squad + memberOff + 0xca) = 5;
                *(int*)((int)DAT_80612db0 + stateOff + 4) = 5;
                unsigned int* cfg = match_mode_get_config(mode);
                int res;
                if (((cfg[8] & 0x400) == 0) &&
                    (res = (*(int(**)(int))(ent + 0x10))(ent), res != 0)) {
                    *(unsigned char*)(ent + 0xc) = *(unsigned char*)(ent + 0xc) | 0x40;
                }
            }
            memberOff = memberOff + 2;
        }
        stateOff = stateOff + 4;
        squadOff = squadOff + 0x25e;
    }
    FUN_80382728();
}

// Register a tally-action slot (+0x88/+0x8c, stride 0x24) that installs the
// squad death-propagation handler.
void objective_register_squad_death_slot(unsigned int recId, unsigned int levelInfo)
{
    int index = *(int*)(DAT_80612db0 + 0x88);
    int* slot = (int*)(DAT_80612db0 + 0x8c);
    *(int*)(DAT_80612db0 + 0x88) = index + 1;
    slot = (int*)(*slot + index * 0x24);
    spawn_slot_build_descriptor(recId, levelInfo, slot);
    slot[4] = (int)&squad_propagate_death;
}

// Top-level objective slot dispatcher: walk the level's objective descriptor
// list (base +0x130, count +0x12c/300, stride 0xb4) and for each descriptor
// dispatch to the register function selected by its category (+0: 1 == tracked
// table, 2 == switch table) and sub-index (+4) via the paired dispatch tables
// at 0x804820d4/d8. Finishes by scheduling the arcade-editor delay screen.
void objective_build_all_slots(void)
{
    int levelInfo = FUN_803826f0();
    int off = 0;
    for (int i = 0; i < *(int*)(levelInfo + 300); i = i + 1) {
        void (*fn)(int*, int) = 0;
        int* desc = (int*)(*(int*)(levelInfo + 0x130) + off);
        int category = *desc;
        if (category == 2) {
            fn = (void(*)(int*, int))(&PTR_objective_register_switch_slot_804820d8)[desc[1] * 2];
        } else if ((category < 2) && (0 < category)) {
            fn = (void(*)(int*, int))(&PTR_objective_register_tracked_slot_804820d4)[desc[1] * 2];
        }
        if (fn != 0) {
            fn(desc, levelInfo);
        }
        off = off + 0xb4;
    }
    arcade_edit_delay_screen(levelInfo);
    FUN_8038273c();
}

// Rebuild the objective descriptor set with the "suppress side effects" global
// (DAT_806108e4) forced to 1, restoring its prior value afterward.
void objective_rebuild_quiet(void)
{
    unsigned int saved = DAT_806108e4;
    DAT_806108e4 = 1;
    FUN_80351960();
    DAT_806108e4 = saved;
}
