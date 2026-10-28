// arcade_edit_settings.cpp
// Arcade map-editor "custom game" settings screen: the scrollable options list
// (level select, game mode, weapon set, bot count, time/score limit, teams,
// handicap sliders) plus its exit/back confirm flow, and the sibling weapon-set
// confirm sub-screen.
//
// The screen shares the editor context base DAT_80612be4. Relevant fields:
//   +0x200  weapon-set selection (baseline DAT_806111b0)
//   +0x204  selected level index
//   +0x208  bot count            (baseline DAT_805d59ac)
//   +0x210  weapon-enable bitmask
//   +0x214  score/kill limit
//   +0x218  teams-enabled flag    +0x220 teams-mode value
//   +0x21c  time limit
//   +0x224  handicap/misc mode
//   +0x238  screen event/dirty flag bits
//   +0x25c  screen state bits
//   +0x11860 saved weapon-slot baseline block   +0x11908 saved bot baseline block
//
// Address: 0x8031e704..0x8031efc8 | Batch: batch_900 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern int  DAT_80612be4;      // editor screen context base
extern int  DAT_805d59a0;      // game-mode / weapon-set selector
extern int  DAT_805d59a4;      // linked value for a settings list
extern int  DAT_805d59a8;      // linked value for a settings list
extern int  DAT_805d59ac;      // baseline bot-count for dirty compare
extern int  DAT_806111b0;      // baseline weapon-set for dirty compare
extern int  DAT_806111b8;      // format-arg placeholder ("%s %s %s")
extern int  DAT_80612ca0;      // preview reload guard
extern int  DAT_80612c98;      // working preview count
extern int  DAT_805d6180;      // handicap slider A backing store
extern int  DAT_805d6260;      // handicap slider B backing store
extern int  DAT_805d6358;      // per-entry value display block base
extern int  DAT_806119e0;      // layout metric (pos-y arg)

extern short DAT_8046ac7c;     // game-mode string-id table (short, stride 0x44)
extern short DAT_80491278;     // weapon-set string-id table (short, stride 2)
extern int   DAT_8047ff84;     // list string-id table (int, stride 8)
extern int   DAT_8047ffdc;     // list string-id table (int, stride 8)
extern int   DAT_8047fff8;     // index table into level-def name array (stride 0x24)
extern int   DAT_803f2de4;     // level-def name string-id array (stride 0x20)
extern int   DAT_803e0a48;     // preview spawn transform template (unused fields here)

extern float FLOAT_8061a830;
extern float FLOAT_8061a834;
extern float FLOAT_8061a838;
extern float FLOAT_8061a83c;
extern float FLOAT_8061a844;
extern float FLOAT_80611a5c;
extern float FLOAT_80611a64;
extern float FLOAT_80611a6c;

extern int  s__s__s_806111b8;  // "%s %s %s" format string literal

// ---- helpers defined elsewhere (raw, per-file externs) ----
extern unsigned long long FUN_80345c2c(int arg);   // channel silence / title-context accessor
extern int  get_string_by_id(int stringId);
extern int  level_def_flag_d(int mode);            // returns weapon-set for a mode
extern void aveditor_set_current_weapon_set(unsigned int set);
extern void aveditor_reload_all_previews(unsigned long long ctx, double a2, double a3, double a4,
                                         unsigned long long a5, unsigned long long a6,
                                         unsigned long long a7, unsigned long long a8);
extern unsigned long long aveditor_commit_preview_count(void);
extern void aveditor_latch_level_change(void);
extern void aveditor_ensure_models_loaded(unsigned int ctx, unsigned long long a2,
                                          unsigned long long a3, unsigned long long a4,
                                          unsigned long long a5, unsigned long long a6,
                                          unsigned long long a7, unsigned long long a8);
extern void asel_reset_screen(int arg);
extern void avanim_play_move_sound(int soundId);
extern void input_reset_pad(int pad);
extern int  hud_element_has_unsaved_changes(int screen);
extern unsigned long long hud_screen_pop_current(void);
extern void hud_screen_rebuild_items(void);
extern void hud_create_yes_no_dialog(int screen, int msg, int yesCb, int noCb);

// event/hint dispatcher: run the indexed handler from PTR_FUN_80481ba4
extern void FUN_80348238(int index);
extern void FUN_80326dc0(int applyToStreams);

// dirty-check helpers (bots baseline / weapon-slot baseline compares)
extern int  FUN_8032016c(void);   // bots config differs from saved baseline
extern int  FUN_8032021c(void);   // weapon-slot config differs from saved baseline

// list-builder primitives
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern void FUN_80387fb8(unsigned long long ctx, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int titleCb, int fmt,
                         int s1, int s2, int r7, int r8, int r9, int r10);
extern void hud_element_init_from_layout(int layoutId, int a, int b, void *cb);
extern void hud_set_item_padding_mode(double h, int a, int b, int c);
extern void hud_set_context_property(int flags);
extern void hud_element_scroll_h_overflow(double x, double a, double b, int c);
extern void hud_begin_scroll_with_item(void);
extern void hud_item_add_header(double h, unsigned int locked);
extern void hud_set_last_child_tag(int tag);
extern void hud_event_add_pair(void);
extern void hud_item_set_linked_value(int *value);
extern void hud_end_scroll_region(void);
extern void hud_add_list_item(void);
extern void hud_item_add_text_display(double h, int a);
extern void hud_item_set_value_display(int valuePtr);
extern void hud_item_add_slider(double h, double w, int a, int *backing, int max, int b, int c);
extern void hud_element_apply_scroll(void);
extern void hud_element_finalize_frame(void);
extern void hud_element_set_pos_y(double y, int metric);

extern int  level_def_is_valid_arc(int index);
extern void level_def_name_string(int index);
extern int  weapon_always_enabled_stub(void);
extern void weapon_get_name_string(int weaponId);

// builds the "game mode" sub-list header block (0x2e)
extern void FUN_80327d78(void);

// 0x8031e704: apply the level's default weapon set, reset preview counters, reload
// every preview model, then reset pad 0. Called when the level selection changes.
void aveditor_apply_weapon_set_reload(unsigned long long a1, double a2, double a3, double a4,
                                      unsigned long long a5, unsigned long long a6,
                                      unsigned long long a7, unsigned long long a8)
{
    unsigned char set;
    unsigned long long ctx;

    set = (unsigned char)level_def_flag_d(DAT_805d59a0);
    aveditor_set_current_weapon_set((unsigned int)set);
    FUN_80326dc0(0);
    DAT_80612ca0 = 0;
    ctx = aveditor_commit_preview_count();
    aveditor_reload_all_previews(ctx, a2, a3, a4, a5, a6, a7, a8);
    input_reset_pad(0);
    return;
}

// 0x8031e754: keep the teams-mode value (+0x220) consistent with the teams-enabled
// flag (+0x218): when teams are off, clear the mode; when first turned on, set -1.
void aveditor_sync_teams_toggle(void)
{
    if (*(int *)(DAT_80612be4 + 0x218) == 0)
    {
        *(int *)(DAT_80612be4 + 0x220) = 0;
        return;
    }
    if (*(int *)(DAT_80612be4 + 0x220) != 0)
    {
        return;
    }
    *(int *)(DAT_80612be4 + 0x220) = -1;
    return;
}

// 0x8031e788: predicate - true when the bot-count field (+0x208) differs from the
// saved baseline DAT_805d59ac.
bool aveditor_bots_count_changed(void)
{
    return DAT_805d59ac != *(int *)(DAT_80612be4 + 0x208);
}

// 0x8031e7ac: settings-screen exit/back handler. Latch any pending level change,
// clear the pending bit, and if the config is unchanged (no unsaved edits, bot
// count unchanged, bot/weapon baselines match) leave without a save prompt;
// otherwise fire the "settings changed" event (0x1a). Then pop the screen and
// reload the models.
void aveditor_settings_exit_handler(unsigned long long a1, unsigned long long a2,
                                    unsigned long long a3, unsigned long long a4,
                                    unsigned long long a5, unsigned long long a6,
                                    unsigned long long a7, unsigned long long a8, int screen)
{
    unsigned long long ctx;

    aveditor_latch_level_change();
    *(unsigned int *)(DAT_80612be4 + 0x25c) = *(unsigned int *)(DAT_80612be4 + 0x25c) & 0xfffffeff;

    if ((hud_element_has_unsaved_changes(screen) == 0) &&
        (!aveditor_bots_count_changed()) &&
        (FUN_8032016c() == 0) &&
        (FUN_8032021c() == 0))
    {
        // config unchanged - skip the event
    }
    else if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)
    {
        FUN_80348238(0x1a);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }

    aveditor_sync_teams_toggle();
    ctx = hud_screen_pop_current();
    aveditor_ensure_models_loaded((unsigned int)ctx, a2, a3, a4, a5, a6, a7, a8);
    return;
}

// 0x8031e858: rebuild the settings item list when the screen reports unsaved edits.
void aveditor_settings_dirty_rebuild(int screen)
{
    if (hud_element_has_unsaved_changes(screen) != 0)
    {
        hud_screen_rebuild_items();
    }
    return;
}

// 0x8031e898: build the arcade custom-game settings menu. Populates, in order:
// title bar, level-select list (arcade levels 0..5), game-mode header + sub-list,
// game-mode list (9 modes + separator), weapon-set list (10 sets + separator),
// bot-count list (all valid weapons/bots), score-limit display, teams header +
// team-mode list, handicap list, per-team value displays, and two handicap sliders.
void aveditor_build_settings_menu(unsigned long long a1, unsigned long long a2,
                                  unsigned long long a3, unsigned long long a4,
                                  unsigned long long a5, unsigned long long a6,
                                  unsigned long long a7, unsigned long long a8)
{
    int item;
    int stringId;
    int idx;
    int off;
    int handicapAdvanced;
    unsigned long long titleCtx;
    double h;

    item = __save_gpr_25();
    handicapAdvanced = *(int *)(DAT_80612be4 + 0x21c);
    titleCtx = FUN_80345c2c(0);
    stringId = get_string_by_id(0x364);
    off = get_string_by_id(0x41d);
    FUN_80387fb8(titleCtx, a2, a3, a4, a5, a6, a7, a8, -0x7fa29c6c, (int)&s__s__s_806111b8,
                 off, stringId, 0, 0, 0, 0);

    hud_element_init_from_layout(0x8800, 0, 0, (void *)aveditor_settings_exit_handler);
    stringId = get_string_by_id(0xa7a);
    *(int *)(item + 0x200) = stringId;
    h = (double)FLOAT_8061a834;
    *(int *)(item + 0x214) = 5;
    hud_set_item_padding_mode(h, 1, 1, 1);
    hud_set_context_property(0x20000);
    hud_element_scroll_h_overflow(
        -(double)(FLOAT_80611a5c *
                  ((FLOAT_8061a830 * FLOAT_80611a64 +
                    FLOAT_8061a838 * FLOAT_80611a64 +
                    FLOAT_8061a83c * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                  FLOAT_8061a844),
        (double)FLOAT_8061a834, (double)FLOAT_8061a834, 0);

    // level-select list
    get_string_by_id(0xa5e);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a834, 0);
    hud_set_last_child_tag(0x2c);
    idx = 0;
    do
    {
        if (level_def_is_valid_arc(idx) != 0)
        {
            level_def_name_string(idx);
            hud_event_add_pair();
        }
        idx = idx + 1;
    } while (idx < 6);
    hud_item_set_linked_value((int *)(DAT_80612be4 + 0x204));
    hud_end_scroll_region();

    FUN_80327d78();

    // game-mode list (table of shorts, stride 0x44)
    get_string_by_id(0x3b0);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a834, 0);
    hud_set_last_child_tag(0x2f);
    idx = 0;
    off = 0;
    do
    {
        get_string_by_id((int)*(short *)((int)&DAT_8046ac7c + off));
        hud_event_add_pair();
        idx = idx + 1;
        off = off + 0x44;
    } while (idx < 9);
    hud_event_add_pair();
    hud_item_set_linked_value(&DAT_805d59a8);
    get_string_by_id(0xaad);
    hud_add_list_item();
    hud_end_scroll_region();

    // weapon-set list (table of shorts, stride 2)
    get_string_by_id(0x3b1);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a834, 0);
    hud_set_last_child_tag(0x30);
    idx = 0;
    off = 0;
    do
    {
        get_string_by_id((int)*(short *)((int)&DAT_80491278 + off));
        hud_event_add_pair();
        idx = idx + 1;
        off = off + 2;
    } while (idx < 10);
    hud_event_add_pair();
    hud_item_set_linked_value(&DAT_805d59a4);
    get_string_by_id(0xaae);
    hud_add_list_item();
    hud_end_scroll_region();

    // bot/weapon enable list (walk all weapon slots 1..0x2c)
    get_string_by_id(0xa60);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a834, 0);
    hud_set_last_child_tag(0x32);
    get_string_by_id(0x41f);
    hud_event_add_pair();
    get_string_by_id(0x18bc);
    hud_event_add_pair();
    idx = 1;
    do
    {
        if (weapon_always_enabled_stub() != 0)
        {
            weapon_get_name_string(idx);
            hud_event_add_pair();
        }
        idx = idx + 1;
    } while (idx < 0x2d);
    hud_item_set_linked_value((int *)(DAT_80612be4 + 0x210));
    hud_end_scroll_region();

    // score/kill limit display
    get_string_by_id(0xa61);
    hud_begin_scroll_with_item();
    get_string_by_id(0xa62);
    hud_add_list_item();
    hud_item_add_text_display((double)FLOAT_8061a834, 0);
    hud_set_last_child_tag(0x33);
    hud_item_set_value_display(DAT_80612be4 + 0x214);
    hud_end_scroll_region();

    // teams header + team-mode list (int table, stride 8)
    get_string_by_id(0xa63);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a834, (unsigned int)(handicapAdvanced != 0));
    hud_set_last_child_tag(0x35);
    idx = 0;
    off = 0;
    do
    {
        get_string_by_id(*(int *)((int)&DAT_8047ff84 + off));
        hud_event_add_pair();
        idx = idx + 1;
        off = off + 8;
    } while (idx < 0xb);
    hud_item_set_linked_value((int *)(DAT_80612be4 + 0x220));
    get_string_by_id(0xa64);
    hud_add_list_item();
    hud_set_last_child_tag(0x31);
    hud_item_add_text_display((double)FLOAT_8061a834, 0);
    hud_item_set_value_display(DAT_80612be4 + 0x21c);
    hud_end_scroll_region();

    // secondary teams list (int table, stride 8)
    get_string_by_id(0xa65);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a834, (unsigned int)(handicapAdvanced != 0));
    hud_set_last_child_tag(0x36);
    idx = 0;
    off = 0;
    do
    {
        get_string_by_id(*(int *)((int)&DAT_8047ffdc + off));
        hud_event_add_pair();
        idx = idx + 1;
        off = off + 8;
    } while (idx < 4);
    hud_item_set_linked_value((int *)(DAT_80612be4 + 0x224));
    hud_end_scroll_region();

    // per-team handicap value displays (14 entries)
    get_string_by_id(0xa6b);
    hud_begin_scroll_with_item();
    idx = 1;
    {
        int valueOff = 4;
        int tblOff = 0x24;
        do
        {
            get_string_by_id((int)*(short *)((int)&DAT_803f2de4 +
                                             *(int *)((int)&DAT_8047fff8 + tblOff) * 0x20));
            hud_add_list_item();
            hud_item_add_text_display((double)FLOAT_8061a834, 0);
            hud_item_set_value_display((int)&DAT_805d6358 + valueOff);
            idx = idx + 1;
            tblOff = tblOff + 0x24;
            valueOff = valueOff + 4;
        } while (idx < 0xf);
    }
    hud_end_scroll_region();

    // two handicap sliders
    get_string_by_id(0xa6a);
    hud_begin_scroll_with_item();
    hud_item_add_slider((double)FLOAT_8061a834, (double)FLOAT_8061a834, 0, &DAT_805d6260, 0xdf, 0, 0);
    hud_set_last_child_tag(0x37);
    hud_end_scroll_region();

    get_string_by_id(0x3d4);
    hud_begin_scroll_with_item();
    hud_item_add_slider((double)FLOAT_8061a834, (double)FLOAT_8061a834, 0, &DAT_805d6180, 0xdf, 0, 0);
    hud_set_last_child_tag(0x38);
    hud_end_scroll_region();

    hud_element_apply_scroll();
    hud_element_finalize_frame();
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061a830 * FLOAT_80611a64 +
                   FLOAT_8061a838 * FLOAT_80611a64 +
                   FLOAT_8061a83c * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    __restore_gpr_25();
    return;
}

// 0x8031ef20: weapon-set confirm sub-screen "apply": reset the select screen, latch
// the chosen set into +0x200, fire the changed event (0x1b) once, and pop.
void aveditor_weaponset_confirm_apply(void)
{
    asel_reset_screen(1);
    *(int *)(DAT_80612be4 + 0x200) = DAT_806111b0;
    if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)
    {
        FUN_80348238(0x1b);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    hud_screen_pop_current();
    return;
}

// 0x8031ef8c: weapon-set sub-screen "cancel": just pop back.
void aveditor_weaponset_pop(void)
{
    hud_screen_pop_current();
    return;
}

// 0x8031efac: predicate - true when the weapon-set field (+0x200) differs from the
// saved baseline DAT_806111b0.
bool aveditor_weaponset_changed(void)
{
    return DAT_806111b0 != *(int *)(DAT_80612be4 + 0x200);
}

// 0x8031efc8: weapon-set sub-screen back/exit handler. Fire the changed event on
// unsaved edits; if the set changed, either commit (set==1) and pop, or raise a
// yes/no "discard changes" dialog; otherwise pop directly.
void aveditor_weaponset_exit_handler(int screen)
{
    int msg;

    if ((hud_element_has_unsaved_changes(screen) != 0) &&
        ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0))
    {
        FUN_80348238(0x1b);
        *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }

    if (aveditor_weaponset_changed())
    {
        if (DAT_806111b0 == 1)
        {
            *(int *)(DAT_80612be4 + 0x200) = 1;
            if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)
            {
                FUN_80348238(0x1b);
                *(unsigned int *)(DAT_80612be4 + 0x238) =
                    *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
            }
            hud_screen_pop_current();
        }
        else
        {
            avanim_play_move_sound(0x1575);
            msg = get_string_by_id(0xa86);
            hud_create_yes_no_dialog(screen, msg, -0x7fce10e0, -0x7fce1074);
        }
    }
    else
    {
        hud_screen_pop_current();
    }
    return;
}
