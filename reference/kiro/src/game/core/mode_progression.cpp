// mode_progression.cpp - Game-mode / objective-progression helpers.
//
// A cluster of small helpers around the active level weapon-loadout record and
// the mode-progression lookup tables. mode_loadout_get_active_record resolves
// the current level's weapon-set record by dispatching on the active game mode
// (DAT_805cc614): worm mode reads an object slot, story/co-op modes build or
// look up a per-level record, arcade uses a fixed slot. The remaining helpers
// index small threshold/value tables (story, elapsed-time, generic value),
// cycle a mod-4 ring table, reset a tracked entity, and gate a mode feature by
// the mode-descriptor table + game settings.
//
// The batch_799 additions (0x802c7e48-0x802c7fa0) extend the cluster: a 2D
// mode-variant table lookup (row * 0x3c + mode-column), a wrapper + a "count
// remaining" computation over it, a team-count selector, a per-player scoring
// gate, and a first-run-latched HUD screen push.
//
// Address range: 0x802c7a28 - 0x802c7fa3

#include "common.h"

extern "C" {

// ---- game-mode / session globals ----
extern int   DAT_805cc614;   // active game mode id
extern int   DAT_805cc628;   // worm-mode object handle
extern char  DAT_805015bd;   // sub-mode selector
extern char  DAT_805cc68c;   // level id (co-op / story)
extern char  DAT_805cc68d;   // mode-descriptor index
extern unsigned char DAT_805cc68e;   // mode-feature descriptor index
extern int   DAT_805cc698;   // elapsed-time accumulator
extern int   DAT_805cc69c;   // story progress value (mode !=2)
extern int   DAT_805cc6a0;   // story progress value (mode ==2)
extern int   DAT_805cc6ac;   // generic mode value to index
extern int   DAT_80612614;   // active-state selector (0x65 running, 0x66 shutdown)
extern int   DAT_80612aac;   // mod-4 ring cursor
extern int   DAT_806109c0;   // frame-rate divisor (0x32 or 0x3c)

// mode-progression lookup tables
extern int   DAT_80476308;   // ring value table [4]
extern int   DAT_80476318;   // ring aux table   [4]
extern int   DAT_80476328;   // generic value table [12]
extern int   DAT_803e0820;   // story index table [11]
extern int   DAT_803e084c;   // time-threshold table [7]
extern int   DAT_803f2dd8;   // mode-descriptor table (stride 0x20), +0 = feature flag

// weapon-loadout record globals
extern int   DAT_805cc688;   // (co-op level id compare, read as char DAT_805cc68c)

// tracked-entity globals
extern int  *DAT_80612b34;   // tracked entity ptr (0 = none)
extern float FLOAT_80612b30; // tracked entity timer
extern float FLOAT_80619f08; // default timer value

// ---- named cross-module helpers ----
extern unsigned char *weapon_build_level_loadout(void);
extern unsigned char *worm_mode_get_object(int handle);
extern int   level_get_info_by_id(int levelId);
extern unsigned char *game_get_settings_ptr(void);
extern void  memset_wrapper(int dst, int val, unsigned int len);
extern void  entity_destroy_with_event(int *entity);

// ---- raw helpers (per-file, arity matches call site) ----
extern int   FUN_802ca260(void);   // resolve current-level slot index
extern unsigned char *FUN_802ca320(void);   // resolve high-slot record ptr

// The level weapon-loadout record array lives at 0x80469b08; records are 0x44
// bytes. The decompiler expresses "idx * 0x44 + -0x7fb956f8" against the sign-
// extended base; we keep that arithmetic literal via the base constant.
#define LOADOUT_RECORD_BASE (-0x7fb956f8)


// 0x802c7a28 - resolve the active level's weapon-loadout record pointer.
// Dispatches on the active game mode: worm mode (6) reads an object slot;
// story/co-op modes build a runtime record or look one up by level id; arcade
// modes (<10) index a fixed slot; otherwise a high-slot resolver is used.
unsigned char *mode_loadout_get_active_record(void)
{
    int slot;
    unsigned char *rec;

    slot = FUN_802ca260();
    if (DAT_805cc614 == 6) {
        rec = worm_mode_get_object(DAT_805cc628);
        if (*(int *)(rec + 0x24) == -1) {
            rec = rec + 0x28;
        } else {
            rec = (unsigned char *)(*(int *)(rec + 0x24) * 0x44 + LOADOUT_RECORD_BASE);
        }
    } else if ((slot == 0) || (DAT_805cc614 == 9 && DAT_805015bd == '\n')) {
        if ((DAT_805cc614 == 9) ||
            (((DAT_805cc614 == 0xb || DAT_805cc614 == 0xd) || DAT_805cc614 == 0xc) &&
             (DAT_805cc68c == 'd'))) {
            rec = weapon_build_level_loadout();
        } else {
            slot = level_get_info_by_id((int)DAT_805cc68c);
            rec = (unsigned char *)(*(int *)(slot + 4) * 0x44 + LOADOUT_RECORD_BASE);
        }
    } else if (slot < 10) {
        rec = (unsigned char *)((slot + 0xc) * 0x44 + LOADOUT_RECORD_BASE);
    } else {
        rec = FUN_802ca320();
    }
    return rec;
}

// 0x802c7b3c - read weapon slot [slotIndex] from the active loadout record.
// Slot -1 in the arcade level range (<0x65) is normalised to 0.
int mode_loadout_get_slot(int slotIndex)
{
    unsigned char *rec;
    int value;

    rec = mode_loadout_get_active_record();
    if (slotIndex < 6) {
        value = *(int *)(rec + slotIndex * 4 + 8);
        if ((value == -1) && (DAT_80612614 < 0x65)) {
            value = 0;
        }
    } else {
        value = 0;
    }
    return value;
}

// 0x802c7ba0 - advance a mod-4 ring cursor, returning the current value table
// entry and (optionally) writing the paired aux table entry through outAux.
int mode_cycle_ring_pair(int *outAux)
{
    int value;

    value = (&DAT_80476308)[DAT_80612aac];
    if (outAux != (int *)0) {
        *outAux = (&DAT_80476318)[DAT_80612aac];
    }
    DAT_80612aac = (DAT_80612aac + 1) & 3;
    return value;
}

// 0x802c7bf4 - map the current story progress value to an index in an 11-entry
// table (DAT_803e0820). Only meaningful for descriptor modes 9 and 2; returns
// 0 for other modes and -1 if no table entry matches.
int mode_story_index_lookup(void)
{
    int probe;
    int index;
    int i;

    probe = DAT_805cc69c;
    if ((DAT_805cc68d != 9) &&
        ((8 < DAT_805cc68d) || (probe = DAT_805cc6a0, DAT_805cc68d != 2))) {
        return 0;
    }
    index = 0;
    for (i = 0; i < 11; i++) {
        if (probe == (&DAT_803e0820)[i]) {
            return index;
        }
        index = index + 1;
    }
    return -1;
}

// 0x802c7c68 - map elapsed time to an index in a 7-entry threshold table
// (DAT_803e084c). First pass divides by (fpsDivisor * 60); if no match, a
// second pass uses an alternate divisor (0x32 when running at 60, else 0x3c).
// Returns -1 if neither pass matches.
int mode_time_threshold_index(void)
{
    int index;
    int i;
    int altDivisor;

    index = 0;
    for (i = 0; i < 7; i++) {
        if (DAT_805cc698 / (DAT_806109c0 * 0x3c) == (&DAT_803e084c)[i]) {
            return index;
        }
        index = index + 1;
    }

    altDivisor = 0x3c;
    if (DAT_806109c0 == 0x3c) {
        altDivisor = 0x32;
    }
    index = 0;
    for (i = 0; i < 7; i++) {
        if (DAT_805cc698 / (altDivisor * 0x3c) == (&DAT_803e084c)[i]) {
            return index;
        }
        index = index + 1;
    }
    return -1;
}

// 0x802c7d04 - map a generic mode value (DAT_805cc6ac) to an index in a
// 12-entry table (DAT_80476328). Returns -1 if no entry matches.
int mode_value_index_lookup(void)
{
    int index;
    int i;

    index = 0;
    for (i = 0; i < 12; i++) {
        if (DAT_805cc6ac == (&DAT_80476328)[i]) {
            return index;
        }
        index = index + 1;
    }
    return -1;
}

// 0x802c7d48 - fill a 4-byte mode-state word (0x80612b18) with a byte value.
void mode_fill_state_word(unsigned char value)
{
    memset_wrapper(-0x7f9ed4e8, value, 4);
}

// 0x802c7d78 - reset the tracked mode entity: destroy DAT_80612b34 (if any) and
// restore its timer. Skipped while the mode is shutting down (state 0x66).
void mode_reset_tracked_entity(void)
{
    if (DAT_80612614 != 0x66) {
        if (DAT_80612b34 != (int *)0) {
            entity_destroy_with_event(DAT_80612b34);
            DAT_80612b34 = (int *)0;
        }
        FLOAT_80612b30 = FLOAT_80619f08;
    }
}

// 0x802c7dc0 - constant accessor (returns 5).
int mode_get_const_five(void)
{
    return 5;
}

// 0x802c7dc8 - test whether the active mode's feature is enabled. Reads the
// mode-descriptor flag (DAT_803f2dd8, stride 0x20) indexed by DAT_805cc68e:
// flag 1 = always on; flag 2 = on when the per-mode settings entry is nonzero.
// Only enabled while the mode is running (state 0x65).
int mode_feature_enabled_check(void)
{
    unsigned char *settings;
    int result;
    int flag;

    settings = game_get_settings_ptr();
    result = 0;
    flag = *(int *)(&DAT_803f2dd8 + (unsigned int)DAT_805cc68e * 0x20);
    if (((flag == 1) ||
         ((flag == 2) &&
          (*(int *)(settings + (unsigned int)DAT_805cc68d * 0x14 + 0x24) != 0))) &&
        (DAT_80612614 == 0x65)) {
        result = 1;
    }
    return result;
}

// ---- batch_799 additions (0x802c7e48 - 0x802c7fa0) ----

extern int   DAT_805cc634;   // mode-variant row selector (for the 2D table)
extern int   DAT_80611014;   // elapsed count subtracted from the variant value
extern int   DAT_80612ac8;   // HUD screen-push first-run latch

// 2D mode-variant table at 0x803f31f0 (-0x7fc0ce10): row stride 0x3c, four
// mode-derived columns.
#define MODE_VARIANT_TABLE_BASE (-0x7fc0ce10)

extern int   player_ai_is_story_mode(int player);
extern void  hud_screen_push(void);

// 0x802c7e48 - look up a mode-variant value from the 2D table indexed by
// [row * 0x3c + column], where the column (0..3) is selected by the active game
// mode (DAT_805cc614). Returns 8 for row 0x10; the mode-5 / feature-0x10 case
// nudges the column to 1.
int mode_variant_table_lookup(int row)
{
    int column;

    if (row == 0x10) {
        return 8;
    }
    column = 0;
    if ((DAT_805cc614 == 5) && (DAT_805cc68e == '\x10')) {
        column = 1;
    }
    if (DAT_805cc614 != 0xc) {
        if (DAT_805cc614 < 0xc) {
            if (10 < DAT_805cc614) {
                column = 2;
            }
        } else if (DAT_805cc614 < 0xe) {
            column = 3;
        }
    }
    return *(int *)(row * 0x3c + column * 4 + MODE_VARIANT_TABLE_BASE);
}

// 0x802c7ed0 - variant lookup using the current variant row (DAT_805cc634).
int mode_variant_current(void)
{
    return mode_variant_table_lookup(DAT_805cc634);
}

// 0x802c7efc - count remaining: variant value minus the elapsed count
// (DAT_80611014); returns 10 while the mode is shutting down (state 0x66).
int mode_variant_count_remaining(void)
{
    int value;

    if (DAT_80612614 == 0x66) {
        value = 10;
    } else {
        value = mode_variant_current();
        value = value - DAT_80611014;
    }
    return value;
}

// 0x802c7f38 - team-count selector: 4 when the mode-feature descriptor
// (DAT_805cc68e) is 4, otherwise 2.
int mode_team_count(void)
{
    if (DAT_805cc68e != '\x04') {
        return 2;
    }
    return 4;
}

// 0x802c7f58 - per-player scoring/display gate: 0 for a story-mode player in
// mode-feature 0x13, otherwise 1.
int mode_player_scored(int player)
{
    if ((DAT_805cc68e == '\x13') && (player_ai_is_story_mode(player) != 0)) {
        return 0;
    }
    return 1;
}

// 0x802c7fa0 - push a HUD screen, latching a one-shot init flag on first call.
// (The screen context is set up by the caller via the frame register pair; the
// non-matching form calls the shared hud_screen_push().)
void mode_hud_screen_push_once(void)
{
    if (DAT_80612ac8 == 0) {
        DAT_80612ac8 = 1;
    }
    hud_screen_push();
}

} // extern "C"
