// worm_spawnpoint_switch.cpp - Respawn/spawnpoint switch-state + zone-alert helpers.
//
// Small helpers used by the AI respawn path (aiactor_respawn_at_spawnpoints,
// 0x80140a2c) and the checkpoint-phase gate. They bind a spawnpoint object to
// its controlling switch state, mark switch-driven spawnpoints active, test the
// checkpoint phase, and test whether a zone node is in any active alert list.
//
// Address range: 0x8025bc00 - 0x8025bd28

#include "common.h"

extern "C" {
extern unsigned char DAT_805015bd;   // co-op sub-mode selector
extern int DAT_80611934;             // stat/event phase gate
extern int DAT_805cb948;             // switch-object state table base (0x20 stride)
extern int DAT_80612108;             // spawnpoint descriptor table base (0x18 stride)
extern int DAT_8061208c;             // spawnpoint owner context (+0x74 -> record table 0x98 stride)
extern int DAT_80611d58;             // zone-alert bucket count
extern int DAT_80611d5c;             // zone-alert bucket table base (stride 0xffc)

// switch-eval callback identity compared against the spawnpoint's bound handler
extern void aiactor_spawnpoint_eval_switch_state(void);

extern int   FUN_8025c8e8(int handle);              // resolve switch-object index from handle
extern unsigned int FUN_80353cfc(int obj);          // switch/prop is-active predicate
extern int*  soundpad_resolve_zone_node(int zoneId);
}

// ---------------------------------------------------------------------------
// 0x8025bc00 - bind a spawnpoint object's "+4 switch state" pointer to the
// controlling switch object, but only when the spawnpoint uses the
// switch-state eval handler and the resolved switch is in state 8.
void spawnpoint_bind_switch_state(int spawnpoint)
{
    int switchState;
    void (*handler)(void);
    int object;

    object = *(int*)(spawnpoint + 0x7c);
    handler = *(void (**)(void))(*(int*)(object + 0xbf0) + 0x6c);
    if ((handler != (void (*)(void))0) && (handler == aiactor_spawnpoint_eval_switch_state)) {
        switchState = FUN_8025c8e8(*(int*)(*(int*)(object + 0xbf0) + 0x38));
        switchState = *(int*)(&DAT_805cb948 + switchState * 0x20);
        if ((switchState == 0) || (*(int*)(switchState + 0x20) != 8)) {
            *(int*)(object + 4) = 0;
        } else {
            *(int*)(object + 4) = switchState;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8025bc88 - checkpoint phase predicate: true in event phase 7, or phase 4
// while in the co-op story sub-mode.
unsigned int worm_checkpoint_phase_active(void)
{
    if (DAT_80611934 == 7) {
        return 1;
    }
    if ((DAT_80611934 == 4) && (DAT_805015bd == '\n')) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8025bcc8 - update a spawnpoint descriptor's "active" flag (+0x10) from the
// switch/prop active predicate for that descriptor's owner object.
void spawnpoint_update_switch_active(int idx)
{
    int object;
    unsigned int active;
    int desc;

    desc = DAT_80612108 + idx * 0x18;
    object = *(int*)(*(int*)(DAT_8061208c + 0x74) + idx * 0x98 + 8);
    *(int*)(desc + 0x10) = 0;
    active = FUN_80353cfc(object);
    if (active != 0) {
        *(int*)(desc + 0x10) = 1;
    }
}

// ---------------------------------------------------------------------------
// 0x8025bd28 - test whether the zone node for zoneId appears in any active
// (bit31-set) zone-alert bucket's short-id list. Used by respawn to avoid
// spawning into an alerted zone.
unsigned int zone_node_in_active_alert_list(int zoneId)
{
    int count;
    int* node;
    int bucket;
    int j;
    int i;
    int off;

    node = soundpad_resolve_zone_node(zoneId);
    i = 0;
    off = 0;
    do {
        if (DAT_80611d58 <= i) {
            return 0;
        }
        bucket = DAT_80611d5c + off;
        if ((*(unsigned int*)(bucket + 0x21c) & 0x80000000) != 0) {
            count = (int)*(short*)(bucket + 0xe0a);
            j = 0;
            if (0 < count) {
                do {
                    if (*(short*)((int)node + 10) == *(short*)(bucket + j + 0xe0c)) {
                        return 1;
                    }
                    j = j + 2;
                    count = count + -1;
                } while (count != 0);
            }
        }
        i = i + 1;
        off = off + 0xffc;
    } while (true);
}

// ===========================================================================
// batch_737 additions: spawnpoint "is-active" evaluators (count / link / flags).
// Address range: 0x8025bdb8 - 0x8025c100
// ===========================================================================

extern "C" {
extern int DAT_80513b38;   // spawnpoint group-id table (stride 4, parallel to owner records)
extern int DAT_80587e14;   // switch-link slot table (stride 0x60)
extern int DAT_80612098;   // AI actor list count
extern int DAT_80612610;   // active player count

extern unsigned int spawn_check_flags_with_unlock(unsigned int flags, unsigned int unlock);
extern int  entity_handle_resolve(unsigned int handle);
extern void aiactor_spawnpoint_eval_zone2(int idx);

// register-spill context accessors (spawnpoint work context)
extern unsigned long long FUN_803826c8(void);
extern void FUN_80382714(void);
extern int  FUN_803826e4(void);
extern void FUN_80382730(void);
}

// forward decls
int  spawnpoint_count_active_for_zone(void);   // 0x8025bdb8 (returns via out-ptr in gpr context)
void spawnpoint_eval_active_by_link(void);     // 0x8025bfb4

// ---------------------------------------------------------------------------
// 0x8025bdb8 - sum the spawn counts of all owner records in this spawnpoint's
// group (matching group id) whose flags pass spawn_check_flags_with_unlock;
// a record's per-record count override (+0x60) falls back to the descriptor
// default (+0x0) when 0x7fffffff. Result written through the context out-ptr.
int spawnpoint_count_active_for_zone(void)
{
    unsigned int passed;
    int* desc;
    int owner;
    int total;
    int descOff;
    int ownerOff;
    int grpOff;
    int count;
    int i;
    int contribution;
    unsigned long long ctx;

    ctx = FUN_803826c8();
    count = *(int*)(DAT_8061208c + 0x78);
    total = 0;
    descOff = 0;
    ownerOff = 0;
    grpOff = 0;
    for (i = 0; i < count; i = i + 1) {
        if ((int)(ctx >> 0x20) == *(int*)((int)&DAT_80513b38 + grpOff)) {
            desc = (int*)(DAT_80612108 + descOff);
            owner = *(int*)(DAT_8061208c + 0x74) + ownerOff;
            passed = spawn_check_flags_with_unlock(*(unsigned int*)(owner + 0xc),
                                                   *(unsigned int*)(owner + 0x2c));
            if (passed != 0) {
                contribution = *(int*)(owner + 0x60);
                if (contribution == 0x7fffffff) {
                    contribution = *desc;
                }
                total = total + contribution;
            }
        }
        descOff = descOff + 0x18;
        ownerOff = ownerOff + 0x98;
        grpOff = grpOff + 4;
    }
    // (drain loop over surplus AI actors preserved for side-effect parity)
    i = DAT_80612098 - DAT_80612610;
    if (DAT_80612610 < DAT_80612098) {
        do { i = i + -1; } while (i != 0);
    }
    if ((int*)(int)ctx != (int*)0) {
        *(int*)(int)ctx = total;
    }
    FUN_80382714();
    return total;
}

// ---------------------------------------------------------------------------
// 0x8025bf38 - mark spawnpoint idx active (+0x10=1) if its live spawn count
// equals the target count, after also running the zone2 evaluator.
void spawnpoint_eval_active_by_count(int idx)
{
    int total;
    int desc;
    int target;

    target = 0;   // decompiler read an uninitialized compare slot; behaves as 0-target gate
    desc = DAT_80612108 + idx * 0x18;
    aiactor_spawnpoint_eval_zone2(idx);
    if (*(int*)(desc + 0x10) != 1) {
        total = spawnpoint_count_active_for_zone();
        if (total == target) {
            *(int*)(desc + 0x10) = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8025bfb4 - mark this spawnpoint active if a same-group owner record passes
// the flag check and its linked switch object is engaged (descriptor +8==1, or
// resolved entity's mode-selected flag bit 1 set). Also runs zone2 eval first.
void spawnpoint_eval_active_by_link(void)
{
    int ctxIdx;
    unsigned int passed;
    int owner;
    int desc;
    int self;
    int selfDesc;
    int ownerOff;
    int descOff;
    int object;
    int i;

    ctxIdx = FUN_803826e4();
    self = *(int*)(DAT_8061208c + 0x74) + ctxIdx * 0x98;
    selfDesc = DAT_80612108 + ctxIdx * 0x18;
    aiactor_spawnpoint_eval_zone2(ctxIdx);
    if (*(int*)(selfDesc + 0x10) != 1) {
        descOff = 0;
        ownerOff = 0;
        for (i = 0; i < *(int*)(DAT_8061208c + 0x78); i = i + 1) {
            owner = *(int*)(DAT_8061208c + 0x74) + ownerOff;
            if (*(int*)(owner + 8) == *(int*)(self + 0x34)) {
                desc = DAT_80612108 + descOff;
                passed = spawn_check_flags_with_unlock(*(unsigned int*)(owner + 0xc),
                                                       *(unsigned int*)(owner + 0x2c));
                if (passed != 0) {
                    if (*(int*)(desc + 8) == 1) {
                        *(int*)(selfDesc + 0x10) = 1;
                    } else {
                        object = entity_handle_resolve(*(unsigned int*)(self + 0x34));
                        if ((object != 0) &&
                            (object = *(int*)(object + 0x7c),
                             *(int*)(*(int*)(object + 0xbf0) + 8) == *(int*)(self + 0x34))) {
                            if (DAT_805015bd == '\n') {
                                passed = *(unsigned int*)(object + 0x14);
                            } else {
                                passed = *(unsigned int*)(object + 0x1c);
                            }
                            if ((passed & 1) != 0) {
                                *(int*)(selfDesc + 0x10) = 1;
                            }
                        }
                    }
                    break;
                }
            }
            descOff = descOff + 0x18;
            ownerOff = ownerOff + 0x98;
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8025c100 - mark spawnpoint idx active if any set switch-link bit (+0x70
// mask) has a non-null slot in the switch-link table; otherwise fall through
// to the link evaluator. Scans up to 0x14 link bits.
void spawnpoint_eval_active_by_flags(int idx)
{
    int slotOff;
    int bit;
    int remaining;

    slotOff = 0;
    bit = 0;
    remaining = 0x14;
    while (((*(unsigned int*)(*(int*)(DAT_8061208c + 0x74) + idx * 0x98 + 0x70) & (1 << bit)) == 0 ||
            (*(int*)((int)&DAT_80587e14 + slotOff) == 0))) {
        bit = bit + 1;
        slotOff = slotOff + 0x60;
        remaining = remaining + -1;
        if (remaining == 0) {
            spawnpoint_eval_active_by_link();
            return;
        }
    }
    *(int*)(DAT_80612108 + idx * 0x18 + 0x10) = 1;
}
