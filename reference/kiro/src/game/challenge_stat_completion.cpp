// challenge_stat_completion.cpp
// Challenge / arcade stat-slot completion and per-challenge setting resolution.
// This cluster drives the stat-tracking backend used by arcade & challenge modes:
//   - it resolves a mode's active challenge record (challenge_record_find_section
//     against DAT_80612be8 with section key 0xc77668) and dispatches to the level
//     definition's completion callback (+0x4c);
//   - it walks the two stat-slot arrays hung off the mode context DAT_80612db0
//     (the 0x28-stride "objective" array at +0x80/+0x84 and the 0x24-stride "stat"
//     array at +0x88/+0x8c) to mark dependent slots, test completion, and set a
//     slot's outcome state (field +0xb4 per 0x8c-stride record);
//   - it resolves per-challenge numeric settings from three sources (the challenge
//     record's inline table, the mode loadout record, and the global game settings
//     block) via challenge_resolve_setting, feeding weapon/enable lookups.
//
// Address range: 0x80353b04 - 0x80353fd4  (batch_1008)
//
// Self-contained translation unit: raw FUN_/DAT_ symbols are declared file-locally
// with concrete types matching each call site; named cross-module helpers resolve
// at link time.

typedef unsigned int   undefined4;
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  byte;
typedef unsigned char  undefined;
typedef unsigned long long undefined8;
typedef void           code;

static inline unsigned int countLeadingZeros(unsigned int x) {
    unsigned int n = 0;
    if (x == 0) return 32;
    while ((x & 0x80000000u) == 0) { n++; x <<= 1; }
    return n;
}

extern "C" {

// ---- named cross-module helpers (concrete signatures) ----
int   *challenge_record_find_section(int recordBase, int sectionKey);
undefined *level_def_record(uint levelId);
uint   sound_is_zone_slot_full(void);
void   player_ai_explosion_spawn(int kind, int a2, float *pos, float *dir, int a5, int owner, int a7);
void   statlvl_class_complete(double a1, undefined8 a2, undefined8 a3, undefined8 a4,
                              undefined8 a5, undefined8 a6, undefined8 a7, undefined8 a8);
void   statlvl_class_set_state4(int classPtr);
undefined *game_get_settings_ptr(void);
undefined *mode_loadout_get_active_record(void);

// ---- raw same-cluster callee (forward ref within this file) ----
int    FUN_80353e10(int settingIdx, int source);
int    FUN_80353e88(int settingIdx);

// ---- raw globals / tables ----
extern int  DAT_80612be8;    // active challenge/mode record base
extern int  DAT_80612db0;    // mode/stat context base (slot arrays hang off here)
extern int  DAT_80612ae8;    // "use loadout settings" flag
extern char DAT_805015bd;    // co-op / game mode selector
extern int  DAT_80610ba0;    // weapon/enable descriptor array base (stride 0x29c)
extern int  DAT_8043e23c;    // per-index descriptor row table (stride 0xf)
extern int  DAT_8043f298;    // weapon setting table (stride 0x54)
extern float FLOAT_8061ad00;
extern float DAT_803e0bd8, DAT_803e0bdc, DAT_803e0be0;   // default explosion dir vec

// the mode-completion callback compared against in challenge_any_slot_completed
extern void slot_set_completed_flag(void);

// ============================================================================
// 0x80353b04  challenge_spawn_zone_explosions
// When the entity's sound reverb strength (+0x1cc of its render node +0x14) is
// positive and its sound-zone slot is full, spawn two AI explosion effects at
// the entity's world position (types 0xb and 0x2c) using the default dir vec.
// ============================================================================
void challenge_spawn_zone_explosions(int param_1)
{
    uint  full;
    int   node;
    float pos_x, pos_y, pos_z;
    float dir_x = DAT_803e0bd8;
    undefined4 dir_y = *(undefined4 *)&DAT_803e0bdc;
    undefined4 dir_z = *(undefined4 *)&DAT_803e0be0;

    if ((FLOAT_8061ad00 < *(float *)(*(int *)(param_1 + 0x14) + 0x1cc)) &&
        (full = sound_is_zone_slot_full(), full != 0)) {
        node  = *(int *)(param_1 + 0x14);
        pos_x = *(float *)(node + 100)  + *(float *)(node + 0x1a8);
        pos_y = *(float *)(node + 0x68) + *(float *)(node + 0x1ac);
        pos_z = *(float *)(node + 0x6c) + *(float *)(node + 0x1b0);
        player_ai_explosion_spawn(0xb, 0, (float *)(param_1 + 0xa8), &dir_x, 0, node, 1);
        player_ai_explosion_spawn(0x2c, 0, &pos_x, &dir_x,
                                  *(int *)(param_1 + 0x14), *(int *)(param_1 + 0x14), 1);
    }
    (void)dir_y; (void)dir_z; (void)pos_y; (void)pos_z;
}

// ============================================================================
// 0x80353be4  challenge_invoke_level_callback
// Resolve the challenge record's level id (+0xa short) to its level def record
// and, if present, invoke the level's completion callback at +0x4c.
// ============================================================================
void challenge_invoke_level_callback(undefined4 param_1)
{
    int       *section;
    undefined *lvl;

    section = challenge_record_find_section(DAT_80612be8, 0xc77668);
    lvl = level_def_record((uint)*(ushort *)((int)section + 10));
    if (*(code **)(lvl + 0x4c) != (code *)0) {
        ((void (*)(undefined4))*(code **)(lvl + 0x4c))(param_1);
    }
}

// ============================================================================
// 0x80353c3c  challenge_mark_dependent_slots
// Given an entity (param_1 +0x7c -> obj, +0xbf0 -> group, +8 -> group id), mark
// every objective slot (0x28-stride, type 2) and stat slot (0x24-stride, type 2)
// whose target id matches the group id: OR bit 2 into their flag byte.
// ============================================================================
void challenge_mark_dependent_slots(int param_1)
{
    int groupId;
    int off;
    int rec;

    groupId = *(int *)(*(int *)(param_1 + 0x7c) + 0xbf0);
    if (groupId == 0) { return; }
    groupId = *(int *)(groupId + 8);
    if (groupId == 0) { return; }

    off = 0;
    for (int i = 0; i < *(int *)(DAT_80612db0 + 0x80); i = i + 1) {
        rec = *(int *)(DAT_80612db0 + 0x84) + off;
        if ((*(int *)(rec + 8) == 2) && (groupId == *(int *)(rec + 0x18))) {
            *(byte *)(rec + 0x10) = *(byte *)(rec + 0x10) | 2;
        }
        off = off + 0x28;
    }
    off = 0;
    for (int i = 0; i < *(int *)(DAT_80612db0 + 0x88); i = i + 1) {
        rec = *(int *)(DAT_80612db0 + 0x8c) + off;
        if ((*(int *)(rec + 4) == 2) && (groupId == *(int *)(rec + 0x14))) {
            *(byte *)(rec + 0xc) = *(byte *)(rec + 0xc) | 2;
        }
        off = off + 0x24;
    }
}

// ============================================================================
// 0x80353cfc  challenge_any_slot_completed
// Scan the stat-slot array for slots targeting param_1 (type 2) whose callback
// is slot_set_completed_flag. Return 1 as soon as one already has bit 4 set;
// otherwise return 1 iff none matched (CLZ(count)>>5), else 0.
// ============================================================================
uint challenge_any_slot_completed(int param_1)
{
    int  count;
    uint clz;
    int  off;
    int  rec;
    int  matched;

    matched = 0;
    count   = *(int *)(DAT_80612db0 + 0x88);
    off     = 0;
    if (0 < count) {
        do {
            rec = *(int *)(DAT_80612db0 + 0x8c) + off;
            if ((param_1 == *(int *)(rec + 0x14)) && (*(int *)(rec + 4) == 2)) {
                if (*(code **)(rec + 0x10) == (code *)slot_set_completed_flag) {
                    if ((*(byte *)(rec + 0xc) & 4) != 0) {
                        return 1;
                    }
                    matched = matched + 1;
                }
            }
            off   = off + 0x24;
            count = count + -1;
        } while (count != 0);
    }
    clz = countLeadingZeros((uint)matched);
    return clz >> 5;
}

// ============================================================================
// 0x80353d78  challenge_complete_stat_slot
// Mark stat class complete (variadic tail preserved from the ABI) and set the
// slot's outcome state (+0xb4 of its 0x8c-stride record) to 3 (completed).
// ============================================================================
void challenge_complete_stat_slot(undefined8 param_1, undefined8 param_2, undefined8 param_3,
                                  undefined8 param_4, undefined8 param_5, undefined8 param_6,
                                  undefined8 param_7, undefined8 param_8, int param_9)
{
    statlvl_class_complete((double)FLOAT_8061ad00, param_2, param_3, param_4,
                           param_5, param_6, param_7, param_8);
    *(undefined4 *)(DAT_80612db0 + param_9 * 0x8c + 0xb4) = 3;
    (void)param_1;
}

// ============================================================================
// 0x80353dc4  challenge_fail_stat_slot
// Put the slot's stat class into state 4 and set the slot outcome state (+0xb4)
// to 8 (failed).
// ============================================================================
void challenge_fail_stat_slot(int param_1)
{
    statlvl_class_set_state4(*(int *)(DAT_80612db0 + param_1 * 0x8c + 0x138));
    *(undefined4 *)(DAT_80612db0 + param_1 * 0x8c + 0xb4) = 8;
}

// ============================================================================
// 0x80353e10  challenge_lookup_setting
// Fetch setting[param_1] for source param_2. Source 5 reads the challenge
// record's inline short table (+0x44); other sources read the global game
// settings block (+0x13c + source*0x44 + idx*4).
// ============================================================================
int challenge_lookup_setting(int param_1, int param_2)
{
    int       *section;
    int        val;
    undefined *settings;

    section = challenge_record_find_section(DAT_80612be8, 0xc77668);
    if (param_2 == 5) {
        val = (int)*(short *)((int)section + param_1 * 2 + 0x44);
    } else {
        settings = game_get_settings_ptr();
        val = *(int *)(settings + param_1 * 4 + param_2 * 0x44 + 0x13c);
    }
    return val;
}

// ============================================================================
// 0x80353e88  challenge_resolve_setting
// Resolve setting[param_1] across the source hierarchy. -1 => 0. When not using
// loadout settings, source is the challenge record's mode id (+0x10 short): <10
// reads the static default table (0x80406c40), else falls to challenge_lookup_setting.
// When using loadout settings, source is game settings +0x6a4 (overridden by the
// challenge mode id in co-op mode '\n'): <10 reads the loadout record, else defers.
// ============================================================================
int challenge_resolve_setting(int param_1)
{
    int        val;
    undefined *settings;
    int       *section;

    if (param_1 == -1) {
        val = 0;
    } else if (DAT_80612ae8 == 0) {
        section = challenge_record_find_section(DAT_80612be8, 0xc77668);
        val = (int)*(short *)((int)section + 0x10);
        if (val < 10) {
            val = *(int *)(val * 0x44 + param_1 * 4 + -0x7fb953c0);
        } else {
            val = FUN_80353e10(param_1, val + -10);
        }
    } else {
        settings = game_get_settings_ptr();
        val = *(int *)(settings + 0x6a4);
        if (DAT_805015bd == '\n') {
            section = challenge_record_find_section(DAT_80612be8, 0xc77668);
            val = (int)*(short *)((int)section + 0x10);
        }
        if (val < 10) {
            settings = mode_loadout_get_active_record();
            val = *(int *)(settings + param_1 * 4 + 8);
        } else {
            val = FUN_80353e10(param_1, val + -10);
        }
    }
    return val;
}

// ============================================================================
// 0x80353f74  challenge_get_weapon_setting
// Resolve the weapon setting for entity class (+0xe short - 0x11); if nonzero,
// read the weapon table (DAT_8043f298, stride 0x54) and sign-normalize to
// {-1,0,1}-style flag via the two's-complement folding expression.
// ============================================================================
uint challenge_get_weapon_setting(int param_1)
{
    int  idx;
    uint v;

    idx = FUN_80353e88(*(short *)(param_1 + 0xe) + -0x11);
    v = 0xffffffff;
    if (idx != 0) {
        v = *(uint *)(&DAT_8043f298 + idx * 0x54);
    }
    return (uint)(((int)(v ^ 0xffffffff) >> 1) - ((v ^ 0xffffffff) & v)) >> 0x1f;
}

// ============================================================================
// 0x80353fd4  challenge_setting_flag_set
// True iff the resolved setting for entity class (+0xe short - 0x17) maps to a
// descriptor whose enable field (+0x14 of the 0x29c-stride DAT_80610ba0 record,
// indexed via the 0xf-stride DAT_8043e23c row table) is nonzero.
// ============================================================================
bool challenge_setting_flag_set(int param_1)
{
    int idx;
    int enable;

    enable = 0;
    idx = FUN_80353e88(*(short *)(param_1 + 0xe) + -0x17);
    if (idx != 0) {
        enable = *(int *)(DAT_80610ba0 + (&DAT_8043e23c)[idx * 0xf] * 0x29c + 0x14);
    }
    return (bool)(1 - (enable == 0));
}

} // extern "C"
