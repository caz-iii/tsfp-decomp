// ============================================================================
// menu_loadout_settings_sync.cpp
// Batch: batch_813
// Address range: 0x802d45fc - 0x802d4e0c
// Date: 2026-09-11T02:35:00Z
//
// Match-settings screen backing store + HUD sync. The loadout/match-options
// screen edits a large settings struct (the game settings block returned by
// game_get_settings_ptr). These four functions:
//   * menu_loadout_settings_init      (0x802d45fc) zero/seed the per-team and
//                                      per-channel sub-tables and the option
//                                      defaults, then tail-call the first slot
//                                      initialiser via a jump table.
//   * menu_loadout_settings_to_items  (0x802d483c) push each settings field out
//                                      to its HUD child item (by child id).
//   * menu_loadout_items_to_settings  (0x802d4b24) read the HUD child items back
//                                      into the settings struct, sync staging,
//                                      then set the confirm-handler enable flag.
//   * menu_loadout_refresh_lock       (0x802d4e0c) re-sync then lock/unlock the
//                                      Start item based on bot count vs the
//                                      mode's required score-type player count.
//
// NON-MATCHING: __save_gpr_28 / frame-actor thunk pairs return a 64-bit pair
// split into hi (screen elem) / lo (flag) by shifts. The settings struct is
// addressed as an int array (puVar7[i]) plus a per-weapon-set offset block.
// ============================================================================

#include "common.h"
#include "menu.h"    // pulls generated_decls.h (named cross-module helpers)
#include "hud.h"

// ---- frame / prologue thunks ----------------------------------------------
extern "C" long long          __save_gpr_28(void);
extern "C" void               __restore_gpr_28(void);
extern "C" unsigned long long FUN_803826e4(void);   // pair: hi=screen elem, lo=flag
extern "C" int               *FUN_803826e8(void);    // settings struct ptr
extern "C" void               FUN_80382730(void);     // scope-end
extern "C" void               FUN_80382734(void);     // scope-end (init tail)

// Named cross-module helpers (hud_find_child_by_id, game_get_settings_ptr,
// menu_loadout_count_active_bots, gamemode_get_score_type, hud_item_set_locked,
// menu_get_active_slot_index, menu_loadout_populate_staging,
// hud_screen_set_handler_param_b, memset_wrapper) resolve through
// generated_decls.h, pulled by the shim includes above. Not redeclared here.
//   hud_find_child_by_id returns int* -> results are cast to int for the
//   +0xc8 (=200) value-slot writes below.
//   game_get_settings_ptr returns void*.

// int-returning wrapper over hud_find_child_by_id (which returns int*); the HUD
// child pointers are used as opaque int handles for the +200 value-slot writes.
static inline int find_child_i(int screen, int id)
{
    return (int)(size_t)hud_find_child_by_id(screen, id);
}

// ---- raw ROM globals -------------------------------------------------------
extern "C" int DAT_80612ad0;   // options-vs-loadout flag (0 = use profile set)
extern "C" unsigned char DAT_805cc68d;   // profile weapon-set index
extern "C" char DAT_805cc68e;  // mode sub-variant byte
extern "C" int DAT_80611014;   // active human player count
extern "C" int DAT_80612ab0;
extern "C" int DAT_80612ab4;
extern "C" int DAT_80611000;

// per-slot jump table used by the init tail-call
extern "C" unsigned char PTR_LAB_804764a4;

// forward decls
void menu_loadout_settings_init(void);
void menu_loadout_settings_to_items(void);
void menu_loadout_items_to_settings(int a0, int a1, int *settings);
void menu_loadout_refresh_lock(int screen);

// ===========================================================================
// 0x802d45fc  menu_loadout_settings_init
// Seed the match-settings struct: clear the per-team ready flags (6 teams x
// 0x14 stride) and per-channel option rows (0x12 rows x 10 cols), then write
// the default option values, and dispatch the first sub-init through a jump
// table indexed by slot.
// ===========================================================================
void menu_loadout_settings_init(void)
{
    int *settings = FUN_803826e8();
    unsigned char *base = (unsigned char *)settings;

    // clear per-team ready flags: 6 teams, each 0x14 shorts starting at +0x2d0
    int team = 0, hdrOff = 0, teamOff = 0;
    do {
        *(unsigned short *)(base + hdrOff + 0x134) = 0;
        int col = 0, n = 0x14;
        do {
            int fld = col + 0x2d0;
            col += 8;
            *(unsigned short *)(base + fld + teamOff) = 0xffff;
            n -= 1;
        } while (n != 0);
        team += 1;
        hdrOff += 0x44;
        teamOff += 0xa4;
    } while (team < 6);

    // seed per-channel option rows: 0x12 rows, 10 cols each (+0x700 / +0x6d8 / +0x6b0)
    int row = 0, rowOff = 0;
    do {
        unsigned int c = 0;
        int colOff = 0, cn = 10;
        do {
            if ((int)c < 3)
                *(int *)(base + colOff + 0x700 + rowOff) = 0;
            else if ((int)c < 7)
                *(int *)(base + colOff + 0x700 + rowOff) = 1;
            else {
                int sgn = (int)(c - 7) >> 0x1f;
                *(unsigned int *)(base + colOff + 0x700 + rowOff) = ((c - 7 & 1) ^ -sgn) + sgn;
            }
            int off = colOff + rowOff;
            unsigned int x = c ^ 6;
            *(int *)(base + off + 0x6d8) = 2;
            c += 1;
            colOff += 4;
            *(unsigned int *)(base + off + 0x6b0) = (((int)x >> 1) - (x & 6)) >> 0x1f;
            cn -= 1;
        } while (cn != 0);
        row += 1;
        rowOff += 0xa0;
    } while (row < 0x12);

    // default option scalars
    settings[0x45] = 0;
    settings[0x46] = 1;
    settings[0x47] = 2;
    settings[0x48] = 0xb;
    settings[0x49] = 0x10;
    settings[0x4b] = 0;
    settings[0x4c] = 0;
    settings[0x1aa] = 1;
    settings[0x1a9] = 0;
    settings[0] = 0;
    settings[1] = 1;
    settings[2] = 1;
    settings[3] = 2;
    settings[4] = 1;
    settings[5] = 1;
    settings[8] = 1;
    settings[6] = 2;
    settings[7] = 0;
    settings[0x1ab] = 1;

    // per-slot sub-init dispatch (jump table). Original treats the indirect
    // jump as a tail call for the first <10 slots; represented here as a call.
    unsigned int slot = 0;
    int tblOff = 0, subOff = 0;
    do {
        int *sub = (int *)(base + subOff + 0x24);
        memset_wrapper((int)sub, 0, 0x14);
        sub[0] = 0;
        sub[1] = 1;
        sub[4] = 4;
        if (slot < 10) {
            (**(void (**)(void))(&PTR_LAB_804764a4 + tblOff))();
            return;
        }
        slot += 1;
        subOff += 0x14;
        tblOff += 4;
    } while ((int)slot < 0xb);
    FUN_80382734();
}

// ===========================================================================
// 0x802d483c  menu_loadout_settings_to_items
// Push each settings field out to its HUD child item's value slot (+0xc8). The
// child id -> settings index mapping is fixed; a per-weapon-set offset block
// (base + wsIndex*5 + 9) supplies ids 3,5,6,7,10. Ids 0x10/0x11 come from the
// last two struct fields; id 0xe/0xf from separate globals. Finally two
// visibility flags are updated based on the current mode/team selection.
// ===========================================================================
void menu_loadout_settings_to_items(void)
{
    unsigned long long pair = FUN_803826e4();
    int screen = (int)(pair >> 0x20);
    int *settings = (int *)(unsigned int)pair;

    unsigned int ws = (DAT_80612ad0 == 0) ? (unsigned int)DAT_805cc68d : 0xb;
    int *wsBlock = settings + ws * 5 + 9;
    int item;

    item = find_child_i(screen, 0x10);
    if (item != 0) *(int *)(item + 200) = settings[0x1a9];
    item = find_child_i(screen, 0x11);
    if (item != 0) *(int *)(item + 200) = settings[0x1aa];
    item = find_child_i(screen, 0xe);
    if (item != 0) *(int *)(item + 200) = DAT_80612ab4;
    item = find_child_i(screen, 0xf);
    if (item != 0) *(int *)(item + 200) = DAT_80611000;
    item = find_child_i(screen, 0);
    if (item != 0) *(int *)(item + 200) = settings[0];
    item = find_child_i(screen, 1);
    if (item != 0) *(int *)(item + 200) = settings[1];
    item = find_child_i(screen, 2);
    if (item != 0) *(int *)(item + 200) = settings[2];
    item = find_child_i(screen, 3);
    if (item != 0) *(int *)(item + 200) = wsBlock[0];

    int itemAspect = find_child_i(screen, 4);
    if (itemAspect != 0) *(int *)(itemAspect + 200) = settings[8];

    int itemA = find_child_i(screen, 5);
    if (itemA != 0) *(int *)(itemA + 200) = wsBlock[1];
    itemA = find_child_i(screen, 6);
    if (itemA != 0) *(int *)(itemA + 200) = wsBlock[2];
    itemA = find_child_i(screen, 7);
    if (itemA != 0) *(int *)(itemA + 200) = wsBlock[3];

    int itemB = find_child_i(screen, 8);
    if (itemB != 0) *(int *)(itemB + 200) = settings[6];
    itemB = find_child_i(screen, 9);
    if (itemB != 0) *(int *)(itemB + 200) = settings[7];
    itemB = find_child_i(screen, 0xb);
    if (itemB != 0) *(int *)(itemB + 200) = settings[3];
    itemB = find_child_i(screen, 10);
    if (itemB != 0) *(int *)(itemB + 200) = wsBlock[4];
    itemB = find_child_i(screen, 0xc);
    if (itemB != 0) *(int *)(itemB + 200) = settings[4];
    itemB = find_child_i(screen, 0x12);
    if (itemB != 0) *(int *)(itemB + 200) = settings[0x1ab];
    itemB = find_child_i(screen, 0xd);
    if (itemB != 0) *(int *)(itemB + 200) = settings[5];

    int item10 = find_child_i(screen, 10);
    if (item10 != 0 && itemB != 0) {
        *(unsigned int *)(itemB + 0x28) &= 0xfffffffe;
        if (*(int *)(item10 + 200) != 0xb)
            *(unsigned int *)(itemB + 0x28) |= 1;
    }
    if (item != 0 && itemAspect != 0) {
        *(unsigned int *)(itemAspect + 0x28) &= 0xfffffffe;
        if (*(int *)(item + 200) == 0)
            *(unsigned int *)(itemAspect + 0x28) |= 1;
    }
    FUN_80382730();
}

// ===========================================================================
// 0x802d4b24  menu_loadout_items_to_settings
// Inverse of the above: read each HUD child item value back into the settings
// struct (and mirror the aspect field into globals). Then re-run the to-items
// push, optionally sync loadout staging, and set the confirm-handler enable
// flag based on the currently focused sub-item's type.
// ===========================================================================
void menu_loadout_items_to_settings(int /*a0*/, int /*a1*/, int *settings)
{
    long long saved = __save_gpr_28();
    int screen = (int)((unsigned long long)saved >> 0x20);

    unsigned int ws = (DAT_80612ad0 == 0) ? (unsigned int)DAT_805cc68d : 0xb;
    int *wsBlock = settings + ws * 5 + 9;
    int item;

    item = find_child_i(screen, 0x10);
    if (item != 0) settings[0x1a9] = *(int *)(item + 200);
    item = find_child_i(screen, 0x11);
    if (item != 0) settings[0x1aa] = *(int *)(item + 200);
    item = find_child_i(screen, 0xe);
    if (item != 0) {
        DAT_80612ab0 = *(int *)(item + 200);
        DAT_80612ab4 = DAT_80612ab0;
        settings[0x45] = DAT_80612ab0;
    }
    item = find_child_i(screen, 0xf);
    if (item != 0) DAT_80611000 = *(int *)(item + 200);
    item = find_child_i(screen, 0);
    if (item != 0) settings[0] = *(int *)(item + 200);
    item = find_child_i(screen, 1);
    if (item != 0) settings[1] = *(int *)(item + 200);
    item = find_child_i(screen, 2);
    if (item != 0) settings[2] = *(int *)(item + 200);
    item = find_child_i(screen, 3);
    if (item != 0) wsBlock[0] = *(int *)(item + 200);
    item = find_child_i(screen, 4);
    if (item != 0) settings[8] = *(int *)(item + 200);
    item = find_child_i(screen, 5);
    if (item != 0) wsBlock[1] = *(int *)(item + 200);
    item = find_child_i(screen, 6);
    if (item != 0) wsBlock[2] = *(int *)(item + 200);
    item = find_child_i(screen, 7);
    if (item != 0) wsBlock[3] = *(int *)(item + 200);
    item = find_child_i(screen, 8);
    if (item != 0) settings[6] = *(int *)(item + 200);
    item = find_child_i(screen, 9);
    if (item != 0) settings[7] = *(int *)(item + 200);
    item = find_child_i(screen, 0xb);
    if (item != 0) settings[3] = *(int *)(item + 200);
    item = find_child_i(screen, 10);
    if (item != 0) wsBlock[4] = *(int *)(item + 200);
    item = find_child_i(screen, 0xc);
    if (item != 0) settings[4] = *(int *)(item + 200);
    item = find_child_i(screen, 0xd);
    if (item != 0) settings[5] = *(int *)(item + 200);
    item = find_child_i(screen, 0x12);
    if (item != 0) settings[0x1ab] = *(int *)(item + 200);

    menu_loadout_settings_to_items();

    if ((int)saved != 0) {
        menu_get_active_slot_index();
        menu_loadout_populate_staging();
    }

    int focusType;
    if (*(int *)(screen + 0x194) == 0 ||
        (focusType = *(int *)(*(int *)(screen + 0x194) + 0x14), focusType == 0x13) ||
        focusType == 0x11 || focusType == 0x10) {
        hud_screen_set_handler_param_b(screen, 0, 0);
    } else {
        hud_screen_set_handler_param_b(screen, 0, 1);
    }
    __restore_gpr_28();
}

// ===========================================================================
// 0x802d4e0c  menu_loadout_refresh_lock
// Refresh the settings items then lock or unlock the Start item (child id 0x13)
// depending on whether the human + bot player count meets the current mode's
// required score-type player count.
// ===========================================================================
void menu_loadout_refresh_lock(int screen)
{
    int humanCount = DAT_80611014;

    int *settings = (int *)game_get_settings_ptr();
    menu_loadout_items_to_settings(screen, 1, settings);

    int bots = menu_loadout_count_active_bots();
    int required = gamemode_get_score_type((unsigned int)DAT_805cc68e);
    int startItem;
    if (humanCount + bots < required) {
        startItem = find_child_i(screen, 0x13);
        hud_item_set_locked(startItem, 1);
    } else {
        startItem = find_child_i(screen, 0x13);
        hud_item_set_locked(startItem, 0);
    }
}
