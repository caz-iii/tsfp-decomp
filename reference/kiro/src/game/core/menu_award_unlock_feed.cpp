// ============================================================================
// menu_award_unlock_feed.cpp
// Batch: batch_820
// Address range: 0x802d9b30 - 0x802d9fec
// Date: 2026-09-11T06:05:00Z
//
// Award-unlock notification feed, the direct consumer of the availability cache
// built by menu_award_cache_rebuild (batch_819). Each builder diffs one award
// category's live check against its cached value; on a newly-unlocked entry it
// appends a (label-id, icon/value) pair to the notification feed at
// DAT_805ce7bc (label ids) / DAT_805ce7e4 (values), capped at 10 entries. An
// aggregator sums the per-category counts and the tick handler either rebuilds
// the on-screen items or advances to the next screen once the feed is exhausted.
//
// Layout:
//   menu_award_feed_add_slot     0x802d9b30  diff per-slot award (award_check_slot2)
//   menu_award_feed_add_idrange  0x802d9be0  diff id-range awards
//   menu_award_feed_add_map      0x802d9c90  diff per-map completion (weapon icon)
//   menu_award_feed_add_byid     0x802d9d50  diff award-by-id (objective descriptor)
//   menu_award_feed_stub_zero_a  0x802d9df8  returns 0 (empty category)
//   menu_award_feed_stub_zero_b  0x802d9e00  returns 0 (empty category)
//   menu_award_feed_add_modifier 0x802d9e08  diff modifier-slot awards
//   menu_award_feed_add_generic  0x802d9eb8  diff generic awards
//   menu_award_feed_total        0x802d9f68  sum of all category counts
//   menu_award_feed_tick         0x802d9fec  advance feed / rebuild or push
//
// NON-MATCHING: the frame-actor head thunks (FUN_803826d0 / FUN_803826d8) return
// the current feed count in r3 and are modelled returning int; the tail thunks
// (FUN_8038271c / FUN_80382724) close the frame-actor scope. The individual
// builders are decompiled as void by Ghidra but their r3 result (the running
// feed count) is consumed by the aggregator, so they are defined here returning
// int. Feed writes use the raw DAT_805ce7bc/DAT_805ce7e4 base symbols with a
// word stride, matching the original.
// ============================================================================

#include "common.h"
#include "menu.h"     // pulls generated_decls.h
#include "hud.h"

// ---- frame-actor head/tail thunks (per-file externs) ----------------------
extern "C" int  FUN_803826d0(void);   // frame-actor head: current feed count (r3)
extern "C" int  FUN_803826d8(void);   // frame-actor head: current feed count (r3)
extern "C" void FUN_8038271c(void);   // frame-actor tail
extern "C" void FUN_80382724(void);   // frame-actor tail

// Named helpers (award_*, weapon_def_get_hud_icon, worm_objective_get_descriptor,
// hud_screen_rebuild_items, hud_screen_push, menu_screen_vtable_for_mode) resolve
// via generated_decls.h.

// ---- notification-feed backing storage (raw base symbols, word stride) -----
extern "C" int DAT_805ce7bc;   // feed: label id per entry (word stride)
extern "C" int DAT_805ce7e4;   // feed: value/icon per entry (word stride)

// ---- award-availability caches (populated by menu_award_cache_rebuild) -----
extern "C" int DAT_805ce388;   // per-slot award-check cache (16 x u32)
extern "C" int DAT_805ce3c8;   // per-map completion cache (0x96 x u32)
extern "C" int DAT_805ce620;   // generic-award cache (0x46 x u32)
extern "C" int DAT_805ce738;   // id-range-valid cache (10 x u32)
extern "C" int DAT_805ce760;   // award-by-id cache (7 x u32)
extern "C" int DAT_805ce77c;   // modifier-slot cache (16 x u32)

// ---- descriptor/label tables the feed pulls its display data from ----------
extern "C" int   DAT_803f31dc;   // mode table: label id (stride 0x3c)
extern "C" int   DAT_80474680;   // map-completion descriptor: map id (stride 0x30)
extern "C" char  DAT_8047467d;   // map-completion descriptor: param byte (stride 0x30)
extern "C" short DAT_80491278;   // id-range label short table (stride 2)
extern "C" short DAT_8044ee14;   // modifier label short table (stride 0x1c)
extern "C" int   DAT_8043f294;   // generic-award label table (stride 0x54)

// ---- feed cursor / drive state --------------------------------------------
extern "C" int DAT_80612b1c;   // feed advance cursor (bumped by tick)

// ============================================================================
// 0x802d9b30 - diff per-slot awards, append newly-unlocked to the feed
// ============================================================================
int menu_award_feed_add_slot(void)
{
    int  count;
    int  live;
    int  wordOff;
    int  idx;
    int  cacheOff;
    int  tableOff;

    count = FUN_803826d0();
    wordOff  = count << 2;
    idx      = 0;
    tableOff = 0;
    cacheOff = 0;
    do {
        live = award_check_slot2();
        if ((*(int *)((int)&DAT_805ce388 + cacheOff) != live) && (count < 10)) {
            int label = *(int *)((int)&DAT_803f31dc + tableOff);
            count = count + 1;
            *(int *)(&DAT_805ce7bc + wordOff) = 0xe34;
            *(int *)(&DAT_805ce7e4 + wordOff) = label;
            wordOff = wordOff + 4;
        }
        idx      = idx + 1;
        cacheOff = cacheOff + 4;
        tableOff = tableOff + 0x3c;
    } while (idx < 0x10);
    FUN_8038271c();
    return count;
}

// ============================================================================
// 0x802d9be0 - diff id-range awards, append newly-unlocked to the feed
// ============================================================================
int menu_award_feed_add_idrange(void)
{
    int   count;
    int   live;
    int   wordOff;
    int   idx;
    int   cacheOff;
    int   tableOff;

    count = FUN_803826d0();
    wordOff  = count << 2;
    idx      = 0;
    tableOff = 0;
    cacheOff = 0;
    do {
        live = award_validate_id_range(idx + 1);
        if ((*(int *)((int)&DAT_805ce738 + cacheOff) != live) && (count < 10)) {
            short label = *(short *)((int)&DAT_80491278 + tableOff);
            count = count + 1;
            *(int *)(&DAT_805ce7bc + wordOff) = 0xe37;
            *(int *)(&DAT_805ce7e4 + wordOff) = (int)label;
            wordOff = wordOff + 4;
        }
        idx      = idx + 1;
        cacheOff = cacheOff + 4;
        tableOff = tableOff + 2;
    } while (idx < 10);
    FUN_8038271c();
    return count;
}

// ============================================================================
// 0x802d9c90 - diff per-map completion, append newly-unlocked (weapon icon)
// ============================================================================
int menu_award_feed_add_map(void)
{
    int  count;
    int  live;
    int  wordOff;
    unsigned int idx;
    int  cacheOff;
    int  descOff;

    count = FUN_803826d0();
    wordOff  = count << 2;
    idx      = 0;
    cacheOff = 0;
    descOff  = 0;
    do {
        live = award_check_map_completion(*(int *)((int)&DAT_80474680 + descOff),
                                          (int)(char)(&DAT_8047467d)[descOff]);
        if ((*(int *)((int)&DAT_805ce3c8 + cacheOff) != live) && (count < 10)) {
            *(int *)(&DAT_805ce7bc + wordOff) = 0xe35;
            int icon = weapon_def_get_hud_icon(idx);
            *(int *)(&DAT_805ce7e4 + wordOff) = icon;
            count = count + 1;
            wordOff = wordOff + 4;
        }
        idx      = idx + 1;
        descOff  = descOff + 0x30;
        cacheOff = cacheOff + 4;
    } while (idx < 0x96);
    FUN_8038271c();
    return count;
}

// ============================================================================
// 0x802d9d50 - diff award-by-id, append newly-unlocked (objective descriptor)
// ============================================================================
int menu_award_feed_add_byid(void)
{
    int  count;
    int  live;
    int  index;
    int  wordOff;
    int  cacheOff;

    count = FUN_803826d8();
    wordOff  = count << 2;
    index    = 0;
    cacheOff = 0;
    do {
        live = award_check_by_id(index);
        if ((*(int *)((int)&DAT_805ce760 + cacheOff) != live) && (count < 10)) {
            *(int *)(&DAT_805ce7bc + wordOff) = 0xe36;
            int desc = worm_objective_get_descriptor(index);
            count = count + 1;
            *(int *)(&DAT_805ce7e4 + wordOff) = *(int *)(desc + 0x1c);
            wordOff = wordOff + 4;
        }
        index    = index + 1;
        cacheOff = cacheOff + 4;
    } while (index < 7);
    FUN_80382724();
    return count;
}

// ============================================================================
// 0x802d9df8 - empty category (no entries)
// ============================================================================
int menu_award_feed_stub_zero_a(void)
{
    return 0;
}

// ============================================================================
// 0x802d9e00 - empty category (no entries)
// ============================================================================
int menu_award_feed_stub_zero_b(void)
{
    return 0;
}

// ============================================================================
// 0x802d9e08 - diff modifier-slot awards, append newly-unlocked to the feed
// ============================================================================
int menu_award_feed_add_modifier(void)
{
    int   count;
    int   live;
    int   wordOff;
    int   idx;
    int   cacheOff;
    int   tableOff;

    count = FUN_803826d0();
    wordOff  = count << 2;
    idx      = 0;
    tableOff = 0;
    cacheOff = 0;
    do {
        live = award_modifier_check_slot(idx);
        if ((*(int *)((int)&DAT_805ce77c + cacheOff) != live) && (count < 10)) {
            short label = *(short *)((int)&DAT_8044ee14 + tableOff);
            count = count + 1;
            *(int *)(&DAT_805ce7bc + wordOff) = 0xe33;
            *(int *)(&DAT_805ce7e4 + wordOff) = (int)label;
            wordOff = wordOff + 4;
        }
        idx      = idx + 1;
        cacheOff = cacheOff + 4;
        tableOff = tableOff + 0x1c;
    } while (idx < 0x10);
    FUN_8038271c();
    return count;
}

// ============================================================================
// 0x802d9eb8 - diff generic awards, append newly-unlocked to the feed
// ============================================================================
int menu_award_feed_add_generic(void)
{
    int  count;
    int  live;
    int  wordOff;
    int  idx;
    int  cacheOff;
    int  tableOff;

    count = FUN_803826d0();
    wordOff  = count << 2;
    idx      = 0;
    tableOff = 0;
    cacheOff = 0;
    do {
        live = award_grant_generic();
        if ((*(int *)((int)&DAT_805ce620 + cacheOff) != live) && (count < 10)) {
            int label = *(int *)((int)&DAT_8043f294 + tableOff);
            count = count + 1;
            *(int *)(&DAT_805ce7bc + wordOff) = 0x19a7;
            *(int *)(&DAT_805ce7e4 + wordOff) = label;
            wordOff = wordOff + 4;
        }
        idx      = idx + 1;
        cacheOff = cacheOff + 4;
        tableOff = tableOff + 0x54;
    } while (idx < 0x46);
    FUN_8038271c();
    return count;
}

// ============================================================================
// 0x802d9f68 - total notification count across all award categories
// ============================================================================
int menu_award_feed_total(void)
{
    int a = menu_award_feed_add_slot();
    int b = menu_award_feed_add_idrange();
    int c = menu_award_feed_add_map();
    int d = menu_award_feed_add_byid();
    int e = menu_award_feed_stub_zero_a();
    int f = menu_award_feed_stub_zero_b();
    int g = menu_award_feed_add_modifier();
    int h = menu_award_feed_add_generic();
    return a + b + c + d + e + f + g + h;
}

// ============================================================================
// 0x802d9fec - notification tick: advance the feed, rebuild or push next screen
// ============================================================================
void menu_award_feed_tick(void)
{
    int total;

    DAT_80612b1c = DAT_80612b1c + 3;
    total = menu_award_feed_total();
    if (DAT_80612b1c < total) {
        hud_screen_rebuild_items();
    } else {
        menu_screen_vtable_for_mode();
        hud_screen_push();
    }
}
