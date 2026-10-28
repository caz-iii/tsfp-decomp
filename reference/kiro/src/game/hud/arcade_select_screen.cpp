// Arcade / multiplayer session-setup player-select screen.
//
// Batch: batch_830
// Module: HUD screen with the standard lifecycle callbacks (init, build,
//   per-frame tick, resource preload) plus the player-slot / controller
//   assignment logic used when entering an arcade or multiplayer session.
//   Entry point arcsel_screen_build (0x802e12f0) is called by
//   session_setup_by_mode (0x801c1a3c); it dispatches to a per-mode menu
//   vtable based on the pending screen id in g_arcsel_pending_mode.
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns.

#include "common.h"

// ---------------------------------------------------------------------------
// Per-file externs for raw symbols (addresses reused with differing arity
// across the tree, so declared locally rather than via a shared header).
// ---------------------------------------------------------------------------

// HUD / screen framework helpers (named cross-module helpers resolve through
// the shared headers; only raw FUN_/DAT_ symbols are declared here).
extern int  hud_element_init_from_layout(int layoutId, int a, void *callback, int b);
extern void hud_clear_render_callback(void);
extern void hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void hud_set_context_property(int prop);
extern int  get_string_by_id(int id);
extern void hud_add_list_item(void);
extern int  hud_get_last_created_item(void);
extern void hud_item_set_locked(int item, int locked);
extern void hud_element_apply_center_xy(int v);
extern void hud_set_focus_by_id(int root, int id);
extern void hud_screen_push(void);
extern int  hud_find_child_by_id(int root, int id);
extern int  hud_get_focused_item(int root);
extern void hud_screen_tick_advance_if_open(int root);
extern int  hud_system_init(void);
extern void hud_style_push_element(int v);
extern int  hud_viewport_create_root(int a, void *rect, int b);
extern void hud_element_update(void);
extern void hud_viewport_set_camera_region(double x, double y, double w, double h);
extern void hud_viewport_set_bounds(double x, double y, double w, double h);
extern void hud_alloc_element_pool(u64 ctx, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6,
                                   u64 p7, u64 p8, int a, int b, int c,
                                   u32 r6, u32 r7, u32 r8, u32 r9, u32 r10);

// Input / controller helpers.
extern int  input_any_activity(int idx);
extern int  input_sort_controllers(void);
extern uint input_get_buttons_held(int idx);
extern int  input_find_controller_index(int idx);
extern int  input_get_controller_id(int idx);
extern int  input_set_pad_mapping(int slot, int id);

// Sound / player-slot / session helpers.
extern void sound_init_play_params(int params);
extern void sound_play_request(int *params);
extern int  player_slot_is_active(int slot);
extern int  player_slot_bind_entity(int slot, int id, int flags);
extern void player_set_active_count(int n);
extern void stats_merge_all_players(void);
extern void memcard_session_reset(int session);
extern int  memcard_session_begin_all(int session, void *vtable);

// Menu / mode helpers used by the build dispatcher.
extern void  select_list_init_empty(u32 *list, int a);
extern int   game_is_in_cutscene_mode(void);
extern u64   game_subsystems_update(u64 ctx, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6,
                                    u64 p7, u64 p8, void *state, u32 r4,
                                    u32 r6, u32 r7, u32 r8, u32 r9, u32 r10, u32 r11);
extern u64   sprite_load_all_visible(u64 ctx, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8);
extern u64   entity_preload_resources(u64 ctx, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6,
                                      u64 p7, u64 p8, int resId);
extern void  sprite_finalize_load(void);
extern void  text_set_font_table(int table);
extern void  stream_stub_3154(void);
extern void  stream_stub_3158(void);
extern u64   mode_reset_tracked_entity(void);
extern void  menu_options_pool_alloc(u64 ctx, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8);
extern void  menu_preview_slot_init_render(int n);
extern int   menu_award_feed_total(void);
extern uint  menu_record_snapshot_changed(void);
extern void **menu_screen_vtable_for_mode(void);
extern void  viewport_set_active(void *vp);
extern void  viewport_set_rect_active(int a, int b, int w, int h);
extern void  render2d_debug_freeze_check(void);
extern int   worm_objective_get_descriptor(int idx);
extern void *worm_mode_get_object(int id);
extern int   boot_should_advance(void);
extern int   boot_skip_requested(void);
extern void *memset_wrapper(int dst, int val, int len);
extern int   debug_log_formatted(u64 ctx, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6,
                                 u64 p7, u64 p8, const char *fmt, int arg0, int arg1,
                                 u32 a, u32 b, u32 c, u32 d, u32 e);

// Raw helper functions still awaiting their own decomp.
extern int  FUN_803826e8(void);
extern void FUN_80382734(void);
extern u64  FUN_803826f0(void);
extern u64  FUN_803826e4(void);
extern void FUN_803826d8(void);
extern void FUN_80382724(void);
extern void FUN_80382730(void);
extern void FUN_8038273c(void);
extern void FUN_80339aa4(void);
extern u64  FUN_802e5d68(u64 ctx, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8);
extern void FUN_802e7ed0(u64 ctx, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8,
                         int a, u32 r4, int b, u32 r6, u32 r7, u32 r8, u32 r9, u32 r10);
extern u64  FUN_80311ec0(u64 f1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8,
                         int hi, int lo, u32 c, u32 r6, u32 r7, u32 r8, u32 r9, u32 r10);
extern u64  FUN_80339f20(u64 f1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8,
                         int hi, int lo, u32 c, u32 r6, u32 r7, u32 r8, u32 r9, u32 r10);
extern uint FUN_80366414(void);
extern int *FUN_80339b90(int *node, int key);
extern void FUN_802e275c(void);
extern void FUN_802e67f8(void);
extern u64  FUN_80387fb8(u64 ctx, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6, u64 p7, u64 p8,
                         int dst, const char *fmt, int a0, int a1, u32 r7, u32 r8, u32 r9, u32 r10);
// FUN_802e1130 is arcsel_label_strings_build_ctx in this file (declared above).

// Raw data symbols.
extern int   DAT_806109c0;          // frame-rate-ish scale (used as *0x1e timeout)
extern int   DAT_806109c4;          // screen width source
extern int   DAT_806109c8;          // screen height source
extern int   DAT_80611014;          // active player/controller count
extern int   DAT_805cc618;          // idle timeout counter
extern int   DAT_805cc608;          // pending screen mode selector
extern char  DAT_806110aa;          // demo/attract page index
extern int   DAT_805cc614;
extern int   DAT_805cc61c;
extern int   DAT_805cc620;
extern int   DAT_805cc628;
extern int   DAT_805cc630;
extern int   DAT_805cc634;
extern int   DAT_805cc638;
extern int   DAT_805cc740;
extern int   DAT_805cc6e0;
extern float DAT_805cc6d8;
extern float DAT_805cc6e4;
extern char  DAT_805cc68c;
extern char  DAT_803f31e4;
extern int   DAT_80611004;
extern int   DAT_80611008;
extern int   DAT_8061100c;
extern int   DAT_80611010;
extern int   DAT_80612ab0;
extern int   DAT_80612ab4;
extern int   DAT_80612ab8;
extern int   DAT_80612abc;
extern int   DAT_80612ac0;
extern int   DAT_80612ac4;
extern int   DAT_80612acc;
extern int   DAT_80612ae0;
extern int   DAT_80612ae8;
extern int   DAT_80612aec;
extern int   DAT_80612b34;
extern int   DAT_80612b38;
extern int   DAT_80612bd4;
extern int  *DAT_80612be8;
extern int   DAT_80612614;
extern int   DAT_805cc60c;
extern int   DAT_805c7ae0;
extern int   DAT_806119e0;
extern int   DAT_806119ec;
extern int   DAT_80611a54;
extern int   DAT_80611870;
extern int   DAT_805cc748;
extern int   DAT_805cc74c;
extern int   DAT_805cc750;
extern int   DAT_805cc53c;
extern int   DAT_805cc540;
extern int   DAT_805cc544;
extern int  *DAT_805cc56c;
extern int   DAT_805cc584;
extern int   DAT_805cc588;
extern int  *DAT_805cc5ac;
extern char  DAT_805015bd;
extern int   DAT_805015c0;
extern char  DAT_805cebac;      // select-list root storage base
extern char  DAT_805ced94;      // player-label string buffer base
extern char  DAT_805cef14;      // viewport descriptor
extern short DAT_80491278;      // string-id table (player labels)
extern short DAT_8046ac7c;      // string-id table (records)
extern int   DAT_804744f8;      // entity preload id table
extern char  DAT_80501570;
extern char  DAT_805015c0_pad;
extern int   DAT_80561c88;
extern int   DAT_80561c8c;
extern int   DAT_80561c90;
extern int   DAT_80561c94;
extern int   DAT_80561c98;
extern int   DAT_80561c9c;
extern int   DAT_80561ca0;
extern int   DAT_80561ca4;

extern float FLOAT_80619f38;
extern float FLOAT_80619f08;
extern float FLOAT_80619ee8;
extern float FLOAT_80619eec;
extern float FLOAT_80619f44;
extern float FLOAT_806108e8;
extern float FLOAT_8060f958;
extern double DOUBLE_80619f00;

extern void *PTR_hud_options_menu_build_804771e8;
extern void *PTR_mode_attract_screen_init_80476380;
extern void *PTR_menu_results_compact_build_804765f0;
extern void *PTR_menu_difficulty_record_build_8047659c;
extern void *PTR_menu_results_screen_build_804765e0;

extern const char s__s__d_806110a4[];  // "%s %d"
extern const char s__s__s_8061104c[];  // "%s %s"

// Vtable-callback list pointer (a menu screen "vtable" is an array of void*).
typedef void **MenuVTable;

// Forward declarations for functions in this translation unit referenced
// before their definition.
void arcsel_playercount_confirm(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                                int screen, u32 p10, u32 p11, u32 p12,
                                u32 p13, u32 p14, u32 p15, u32 p16);
extern bool FUN_8039eadc(void);
// arcsel_label_strings_build (0x802e1130) is called from arcsel_screen_build;
// the original consumed its return value as the running context register.
u64 arcsel_label_strings_build_ctx(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                   u64 param_5, u64 param_6, u64 param_7, u64 param_8);

// ---------------------------------------------------------------------------
// arcsel_playercount_list_build (0x802e0bd4)
//   Builds the 4-item, locked player-count list on the select screen.
// ---------------------------------------------------------------------------
void arcsel_playercount_list_build(int screen)
{
    int item;

    hud_element_init_from_layout(0x8000, 0, (void *)arcsel_playercount_confirm, 0);
    hud_clear_render_callback();
    hud_set_item_padding_mode((double)FLOAT_80619f38, 1, 1, 1);
    hud_set_context_property(0x32200);
    *(int *)(screen + 0x214) = 3;

    get_string_by_id(0x419);
    hud_add_list_item();
    item = hud_get_last_created_item();
    hud_item_set_locked(item, 1);

    get_string_by_id(0x41a);
    hud_add_list_item();
    item = hud_get_last_created_item();
    hud_item_set_locked(item, 1);

    get_string_by_id(0x41b);
    hud_add_list_item();
    item = hud_get_last_created_item();
    hud_item_set_locked(item, 1);

    get_string_by_id(0x41c);
    hud_add_list_item();
    item = hud_get_last_created_item();
    hud_item_set_locked(item, 1);

    hud_element_apply_center_xy(DAT_806119e0);
    hud_set_focus_by_id(screen, DAT_80611014);
    DAT_80612ac0 = 0;
    *(int *)(screen + 0x3c) = 0xf;
}

// ---------------------------------------------------------------------------
// arcsel_playercount_confirm (0x802e0aa4)
//   List-item callback: plays a confirm sound, assigns the confirming
//   controller to pad slot 0, and binds inactive player slots to entities.
// ---------------------------------------------------------------------------
void arcsel_playercount_confirm(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                                int screen, u32 p10, u32 p11, u32 p12,
                                u32 p13, u32 p14, u32 p15, u32 p16)
{
    uint held;
    int  ctrlIdx;
    int  ctrlId;
    bool more;
    int  slot;
    u64  ret;
    int  soundParams[12];

    sound_init_play_params((int)soundParams);
    soundParams[0] = 0xd1e;
    sound_play_request(soundParams);

    DAT_805cc6e0 = -1;
    DAT_80611014 = *(int *)(screen + 0x188);
    DAT_80612ac0 = 1;
    DAT_805cc61c = 0;
    input_sort_controllers();

    slot = 3;
    do {
        held = input_get_buttons_held(slot);
        if ((held & 0x80) != 0) break;
        more = slot != 0;
        slot = slot - 1;
    } while (more);

    ctrlIdx = input_find_controller_index(slot);
    ctrlId  = input_get_controller_id(0);
    input_set_pad_mapping(0, slot);
    ret = (u64)input_set_pad_mapping(ctrlIdx, ctrlId);

    for (slot = 0; slot < DAT_80611014; slot = slot + 1) {
        if (!player_slot_is_active(slot)) {
            int label = get_string_by_id(0x395);
            int id = debug_log_formatted(ret, param_2, param_3, param_4, param_5, param_6,
                                         param_7, param_8, s__s__d_806110a4, label, slot + 1,
                                         p12, p13, p14, p15, p16);
            ret = (u64)player_slot_bind_entity(slot, id, 0);
        }
    }

    stats_merge_all_players();
    memcard_session_reset(DAT_80612ac4);
}

// ---------------------------------------------------------------------------
// arcsel_controller_locks_update (0x802e0d1c)
//   Per-frame: locks/unlocks the player list rows to match the number of
//   sorted controllers, and moves focus off any now-hidden row.
// ---------------------------------------------------------------------------
void arcsel_controller_locks_update(void)
{
    int root;
    int nCtrl;
    int row;
    int locked;
    int focused;

    root  = FUN_803826e8();
    nCtrl = input_sort_controllers();

    for (row = 0; row < 4; row = row + 1) {
        int item = hud_find_child_by_id(root, row + 1);
        locked = 0;
        if ((nCtrl <= row) || (DAT_805cc6e0 == 0)) {
            locked = 1;
        }
        hud_item_set_locked(item, locked);

        if ((row < nCtrl) || (row == 0)) {
            *(uint *)(item + 0x28) = *(uint *)(item + 0x28) & 0xfffffffb;
        } else {
            *(uint *)(item + 0x28) = *(uint *)(item + 0x28) | 4;
            focused = hud_get_focused_item(root);
            if (item == focused) {
                hud_set_focus_by_id(root, 1);
            }
        }
    }

    hud_screen_tick_advance_if_open(root);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// arcsel_screen_close (0x802e0dec)
//   Requests the select screen to close (pop).
// ---------------------------------------------------------------------------
void arcsel_screen_close(void)
{
    DAT_805cc614 = 1;
    hud_screen_push();
}

// ---------------------------------------------------------------------------
// arcsel_poll_input_timeout (0x802e0e28)
//   Runs the attract/idle timeout. Returns the attract page to advance to,
//   or 0xffffffff if input is active or the timeout is still counting down.
// ---------------------------------------------------------------------------
uint arcsel_poll_input_timeout(void)
{
    uint result;
    int  active;
    int  idx;

    if (DAT_806109c0 * 0x1e < 1) {
        return 0xffffffff;
    }

    for (idx = 0; idx < DAT_80611014; idx = idx + 1) {
        active = input_any_activity(idx);
        if (active != 0) {
            DAT_805cc618 = DAT_806109c0 * 0x1e;
            break;
        }
    }

    if (DAT_805cc618 == 0) {
        DAT_805cc608 = 1;
        DAT_806110aa = DAT_806110aa + 1;
        if (1 < DAT_806110aa) {
            DAT_806110aa = 1;
        }
        result = (uint)(unsigned char)DAT_806110aa;
        DAT_805cc618 = DAT_806109c0 * 0x1e;
    } else {
        result = 0xffffffff;
        DAT_805cc618 = DAT_805cc618 - 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// arcsel_preload_resources (0x802e108c)
//   Preloads sprites and the entity resource set used by the select screen.
// ---------------------------------------------------------------------------
void arcsel_preload_resources(u64 param_1, double param_2, double param_3, u64 param_4,
                              u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                              u32 p9, u32 p10, u32 p11, u32 p12,
                              u32 p13, u32 p14, u32 p15, u32 p16)
{
    uint inCutscene;
    u32  r4 = 0;
    int  i;
    int  off;
    u64  ret;

    ret = FUN_803826f0();

    inCutscene = game_is_in_cutscene_mode();
    if ((inCutscene != 0) && ((DAT_805015c0 & 0x200000) != 0)) {
        DAT_80611870 = 1;
        ret = game_subsystems_update(ret, param_2, param_3, param_4, param_5, param_6,
                                     param_7, param_8, &DAT_80501570, r4,
                                     p11, p12, p13, p14, p15, p16);
        DAT_80611870 = 0;
    }

    ret = sprite_load_all_visible(ret, param_2, param_3, param_4, param_5, param_6, param_7, param_8);

    off = 0;
    for (i = 0; i < 0x19; i = i + 1) {
        ret = entity_preload_resources(ret, param_2, param_3, param_4, param_5, param_6,
                                       param_7, param_8, *(int *)((int)&DAT_804744f8 + off));
        off = off + 0x10;
    }
    ret = entity_preload_resources(ret, param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0x43);
    entity_preload_resources(ret, param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0x385);

    FUN_80339aa4();
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// arcsel_label_strings_build (0x802e1130)
//   Builds the player / record label string tables shown on the screen.
// ---------------------------------------------------------------------------
u64 arcsel_label_strings_build_ctx(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                   u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    int label;
    int str;
    int i;
    int off;
    u64 ctx;
    u32 r7 = 0, r8 = 0, r9 = 0, r10 = 0;

    ctx = FUN_803826e4();

    if (DAT_805015bd != '\n') {
        i = 0;
        off = 0;
        do {
            if (i == 5) {
                str   = get_string_by_id(0x364);
                label = get_string_by_id(0x41d);
                ctx = FUN_80387fb8(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   (int)(&DAT_805ced94 + off), s__s__s_8061104c, label, str,
                                   r7, r8, r9, r10);
            } else {
                label = get_string_by_id(0x41d);
                ctx = FUN_80387fb8(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   (int)(&DAT_805ced94 + off), s__s__d_806110a4, label, i + 1,
                                   r7, r8, r9, r10);
            }
            i = i + 1;
            off = off + 0x40;
        } while (i < 6);

        DAT_805cc53c = get_string_by_id(0x356);
        DAT_805cc540 = get_string_by_id(0x41f);

        i = 0;
        off = 0;
        {
            int tblOff = 0;
            do {
                str = get_string_by_id((int)*(short *)((int)&DAT_80491278 + tblOff));
                i = i + 1;
                *(int *)((int)&DAT_805cc544 + off) = str;
                off = off + 4;
                tblOff = tblOff + 2;
            } while (i < 10);
        }

        off = 0;
        {
            int slotOff = 0;
            int n = 6;
            do {
                *(char **)((int)&DAT_805cc56c + off) = &DAT_805ced94 + slotOff;
                off = off + 4;
                slotOff = slotOff + 0x40;
                n = n - 1;
            } while (n != 0);
        }

        DAT_805cc584 = get_string_by_id(0x41f);

        i = 0;
        off = 0;
        {
            int tblOff = 0;
            do {
                str = get_string_by_id((int)*(short *)((int)&DAT_8046ac7c + tblOff));
                i = i + 1;
                *(int *)((int)&DAT_805cc588 + off) = str;
                off = off + 4;
                tblOff = tblOff + 0x44;
            } while (i < 9);
        }

        off = 0;
        {
            int slotOff = 0;
            int n = 6;
            do {
                *(char **)((int)&DAT_805cc5ac + off) = &DAT_805ced94 + slotOff;
                off = off + 4;
                slotOff = slotOff + 0x40;
                n = n - 1;
            } while (n != 0);
        }
    }

    FUN_80382730();
    return ctx;
}

// ---------------------------------------------------------------------------
// arcsel_screen_init (0x802e0efc)
//   Resets the select-screen state block and initializes the worm-mode
//   objective object states for the pending session.
// ---------------------------------------------------------------------------
void arcsel_screen_init(void)
{
    char *obj;
    bool wide;
    int  desc;
    int  slot;
    int  n;
    int  off;

    FUN_803826d8();
    DAT_80611014 = 1;
    select_list_init_empty((u32 *)&DAT_805cebac, 0);
    memset_wrapper((int)0x80561c08 - 0x800, 0, 0x13c); // memset of DAT_805cebac-ish block
    DAT_805cc608 = 0;
    DAT_805cc614 = 0;
    DAT_805cc630 = 0;
    DAT_805cc628 = 0;
    DAT_805cc620 = 0;
    DAT_805cc634 = 0;
    DAT_805cc68c = DAT_803f31e4;
    DAT_805cc638 = 1;
    DAT_80612ab0 = 0;
    DAT_80612ab4 = 0;
    DAT_80611004 = 1;
    wide = FUN_8039eadc();
    DAT_80611008 = (int)wide + 1;
    DAT_8061100c = 0xb;
    DAT_80611010 = 0x10;
    DAT_80612ab8 = 0;
    DAT_80612abc = 0;

    n = 10;
    off = 0;
    do {
        *(int *)((int)&DAT_805cc748 + off) = 0;
        *(int *)((int)&DAT_805cc74c + off) = 0;
        *(int *)((int)&DAT_805cc750 + off) = 0;
        n = n - 1;
        off = off + 0x1c;
    } while (n != 0);

    DAT_805cc6d8 = FLOAT_80619f08;
    DAT_805cc61c = 0;
    DAT_805cc740 = 0;
    stream_stub_3154();

    for (slot = 0; slot < 0x10; slot = slot + 1) {
        desc = worm_objective_get_descriptor(slot);
        off = 0;
        for (int e = 0; e < *(int *)(desc + 4); e = e + 1) {
            obj = (char *)worm_mode_get_object(*(int *)(desc + off + 8));
            memset_wrapper((int)(obj + 0xa8), 0, 0x14);
            if (e == 0) {
                *(int *)(obj + 0xa8) = 1;
            } else {
                *(int *)(obj + 0xa8) = 5;
                *(int *)(obj + 0xac) = *(int *)(desc + off + 4);
                *(int *)(obj + 0xb0) = -1;
            }
            off = off + 4;
        }
    }

    FUN_80382724();
}

// ---------------------------------------------------------------------------
// arcsel_screen_build (0x802e12f0)
//   Screen enter/build entry point (called by session_setup_by_mode).
//   Allocates the element pool, initializes HUD/viewport, then dispatches to
//   the per-mode menu vtable selected by g_arcsel_pending_mode.
// ---------------------------------------------------------------------------
void arcsel_screen_build(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                         u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    int   *node;
    uint   changed;
    int    total;
    u32    r4dummy = 0;
    u32    r6 = 0, r7 = 0, r8 = 0, r9 = 0, r10 = 0;
    MenuVTable vtable = 0;
    u64    ctx;

    ctx = FUN_803826f0();
    DAT_80611a54 = 1;
    DAT_80612b38 = 0;

    hud_alloc_element_pool(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                           0, 0x80470000, 0x805d0000, r6, r7, r8, r9, r10);
    ctx = hud_system_init();

    if (DAT_80612614 == 0x65) {
        ctx = arcsel_label_strings_build_ctx(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        if (DAT_80612be8 == (int *)0) {
            ctx = FUN_80311ec0(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                               (int)((u64)ctx >> 0x20), (int)ctx, 0x805d0000, r6, r7, r8, r9, r10);
        }
        ctx = FUN_80339f20(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                           (int)((u64)ctx >> 0x20), (int)ctx, 0x805d0000, r6, r7, r8, r9, r10);
    }

    ctx = FUN_802e5d68(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    FUN_802e7ed0(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 0, r4dummy, 0x805d0000, r6, r7, r8, r9, r10);

    DAT_80612acc = 0;
    DAT_80612ae8 = 1;
    hud_style_push_element(0);
    DAT_80612ac4 = hud_viewport_create_root(0, &DAT_805cebac, 0);
    stream_stub_3158();
    sprite_finalize_load();
    text_set_font_table(0x15);
    DAT_80612b34 = 0;
    DAT_805cc60c = DAT_805cc60c & 0xffff0000;

    ctx = mode_reset_tracked_entity();
    menu_options_pool_alloc(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    memset_wrapper((int)0x8061c920 - 0x100000, 0, 0x5b0); // memset of the menu options pool
    DAT_80612bd4 = 0;
    menu_preview_slot_init_render(4);

    switch (DAT_805cc608) {
    case 0:
    case 1:
        FUN_802e275c();
        break;
    case 2:
        vtable = (MenuVTable)&PTR_hud_options_menu_build_804771e8;
        break;
    case 3:
    case 4:
        if (DAT_80612614 == 0x6a) {
            total = menu_award_feed_total();
            vtable = (total == 0) ? (MenuVTable)&PTR_mode_attract_screen_init_80476380
                                  : (MenuVTable)&PTR_menu_results_compact_build_804765f0;
        } else {
            total = menu_award_feed_total();
            vtable = (total == 0) ? (MenuVTable)&PTR_menu_difficulty_record_build_8047659c
                                  : (MenuVTable)&PTR_menu_results_screen_build_804765e0;
        }
        break;
    case 5:
    case 10:
    case 0xe:
    case 0x10:
        vtable = menu_screen_vtable_for_mode();
        break;
    case 6:
    case 7:
        changed = menu_record_snapshot_changed();
        if ((changed == 0) && (menu_award_feed_total() == 0)) {
            vtable = menu_screen_vtable_for_mode();
        } else {
            vtable = (MenuVTable)&PTR_menu_results_screen_build_804765e0;
        }
        break;
    case 8:
        if (FUN_80366414() == 0) {
            vtable = menu_screen_vtable_for_mode();
        } else {
            DAT_805cc614 = 0xc;
        }
        break;
    case 9:
        node = FUN_80339b90(DAT_80612be8, 0xc77668);
        DAT_805c7ae0 = *node;
        vtable = menu_screen_vtable_for_mode();
        break;
    case 0xf:
        vtable = menu_screen_vtable_for_mode();
        break;
    }

    if ((vtable != 0) && (memcard_session_begin_all(DAT_80612ac4, vtable) == 0)) {
        hud_screen_push();
    }

    DAT_805cc618 = DAT_806109c0 * 0x1e;
    DAT_805cc6e4 = FLOAT_80619f08;
    player_set_active_count(8);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// arcsel_viewport_tick (0x802e1550)
//   Per-frame update: recomputes per-player viewport camera regions and
//   advances the screen transition / cursor animation.
// ---------------------------------------------------------------------------
void arcsel_viewport_tick(void)
{
    int    i;
    double dt;
    double w0;
    double region;
    double regionEnd;
    double h;

    FUN_803826f0();

    dt = (double)(FLOAT_806108e8 *
                  (FLOAT_80619ee8 / (float)((double)DAT_806109c0 - DOUBLE_80619f00)));
    w0 = (double)((float)((double)DAT_806109c4 - DOUBLE_80619f00) * FLOAT_8060f958);
    region = (double)(FLOAT_80619eec *
                      (float)((double)(float)((double)DAT_806109c4 - DOUBLE_80619f00) - w0));
    regionEnd = (double)(float)(w0 + region);
    h = DOUBLE_80619f00;

    for (i = 0; i < DAT_80611014; i = i + 1) {
        DAT_806119ec = i;
        hud_viewport_set_camera_region(region, (double)FLOAT_80619f08, regionEnd,
                                       (double)(float)((double)DAT_806109c8 - h));
        hud_viewport_set_bounds(region, (double)FLOAT_80619f08, regionEnd,
                                (double)(float)((double)DAT_806109c8 - h));
    }
    DAT_806119ec = 0;

    if (DAT_80612ae0 == 1) {
        DAT_80612aec = 0;
        if (boot_should_advance() == 0) {
            boot_skip_requested();
            hud_screen_push();
        } else {
            hud_screen_push();
        }
        DAT_80612ae0 = 2;
    } else if (DAT_80612ae0 == 2) {
        hud_element_update();
    } else {
        DAT_805cc6d8 = (float)((double)FLOAT_80619f44 * dt + (double)DAT_805cc6d8);
        DAT_80561c88 = (int)FLOAT_80619f08;
        DAT_805cc6e4 = (float)((double)DAT_805cc6e4 + dt);
        DAT_80561c8c = (int)FLOAT_80619f08;
        DAT_80561c90 = (int)FLOAT_80619f08;
        DAT_80561c94 = (int)FLOAT_80619f08;
        DAT_80561c98 = (int)FLOAT_80619f08;
        DAT_80561c9c = (int)FLOAT_80619ee8;
        DAT_80561ca0 = (int)FLOAT_80619f08;
        DAT_80561ca4 = (int)FLOAT_80619f08;
        if (FLOAT_80619ee8 < DAT_805cc6d8) {
            DAT_805cc6d8 = DAT_805cc6d8 - FLOAT_80619ee8;
        }
        if ((DAT_80612614 == 0x65) || (DAT_80612614 == 0x6a) || (DAT_80612614 == 0x6b)) {
            if (DAT_80612614 != 0x6b) {
                FUN_802e67f8();
            }
            render2d_debug_freeze_check();
        }
        hud_element_update();
    }

    viewport_set_active(&DAT_805cef14);
    viewport_set_rect_active(0, 0, DAT_806109c4, DAT_806109c8);
    FUN_8038273c();
}

// ===========================================================================
// Batch: batch_831  (append)
// Render + sub-mode state-build continuation of the arcade select screen.
//   arcsel_screen_render (0x802e181c) is the render counterpart to
//   arcsel_screen_build; it is called by session_render_scene_by_mode
//   (0x801c2584). arcsel_state_build (0x802e19e4) rebuilds the per-sub-mode
//   slot/loadout/award tables and is invoked on transitions.
// ===========================================================================

// ---- Additional per-file externs for batch_831 raw symbols ----
extern void  gx_set_sub_material_id(int id);
extern u64   render_target_reset_context(void);
extern void  viewport_set_depth_params_active(double a, double b, double c);
extern void  viewport_set_gx_active(void);
extern void  gx_set_scissor_rect(int x, int y, int w, int h);
extern void  hud_screen_render_viewport(int session, int a);
extern u64   menu_preview_render_if_active(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6,
                                           u64 p7, u64 p8, int node);
extern void *game_get_settings_ptr(void);
extern void  memcard_session_nop(void);
extern void  menu_award_cache_rebuild(void);
extern void  save_stub_noop(void);
extern int   save_get_data_ptr(void);
extern void  leaf_31e8(int dst, int src, int len);
extern short award_get_name(int idx, int table);
extern void  match_build_next_config(void);
extern int   weapon_type_is_projectile(int type);
extern int   menu_slot_tier_scale(uint id);
extern int   gamemode_find_in_table(int mode);
extern int   mode_feature_enabled_check(void);
extern int   menu_loadout_can_edit_teams(void);
extern int   menu_loadout_count_ready_humans(void);
extern int   menu_slot_is_extra_bot(int slot);
extern int   player_ai_is_story_mode(int id);
extern int   mode_team_count(void);
extern int   beam_fx_get_segment_count(void);
extern int   savegame_get_player_block(int idx);
extern void  match_start_cutscene_transition(u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6,
                                             u64 p7, u64 p8, int state, u32 r4,
                                             u32 r11, u32 r12, u32 r13, u32 r14, u32 r15, u32 r16);

// Raw helper functions awaiting their own decomp.
extern void FUN_802e6ec4(void);
extern void FUN_802e6f7c(void);

// Raw data symbols for batch_831.
extern int   DAT_803f2ddc;      // per-mode config table (stride 0x20)
extern int   DAT_803f2dd0;      // per-mode config table base
extern int  *DAT_80611748;      // scissor rect array
extern char  DAT_805cc68d;
extern char  DAT_805cc68e;
extern char  DAT_805cc68c;
extern char  DAT_805cc68f;
extern char  DAT_805cc640;
extern char  DAT_805cc641;
extern char  DAT_805cc642;
extern char  DAT_805cc643;
extern char  DAT_805cc644;
extern char  DAT_805cc654;
extern char  DAT_805cc662;
extern char  DAT_805cc670;
extern char  DAT_805cc67e;
extern int   DAT_805cc690;
extern int   DAT_805cc698;
extern int   DAT_805cc69c;
extern int   DAT_805cc6a0;
extern int   DAT_805cc6a8;
extern int   DAT_805cc6ac;
extern float DAT_805cc6c4;
extern int   DAT_805cc6cc;
extern char  DAT_805cc6d3;
extern char  DAT_805cc6d7;
extern int   DAT_805cc624;
extern int   DAT_805cc744;
extern int   DAT_805cc748;
extern int   DAT_805cc74c;
extern int   DAT_805cc750;
extern int   DAT_805cc754;
extern int   DAT_805cc75c;
extern int   DAT_805cc900;
extern char  DAT_806110b0;
extern int   DAT_803e084c;      // frame-count multiplier table
extern int   DAT_803e0868;      // float table (speed scales)
extern int   DAT_803e0820;      // aux param table
extern int   DAT_80476328;      // aux param table
extern int   DAT_80471c54;      // sub-mode -> character id table (stride 0x24)
extern float FLOAT_8061a178;
extern float FLOAT_8060fad0;
extern float FLOAT_8061a17c;

// ---------------------------------------------------------------------------
// arcsel_element_tree_render (0x802e17d4)
//   Recursively renders the HUD element preview tree rooted at `node`,
//   walking the sibling chain at +0x30 and children at +0x168.
// ---------------------------------------------------------------------------
u64 arcsel_element_tree_render(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                               u64 param_5, u64 param_6, u64 param_7, u64 param_8, int node)
{
    int child;
    u64 ctx;

    ctx = menu_preview_render_if_active(param_1, param_2, param_3, param_4, param_5, param_6,
                                        param_7, param_8, node);
    for (child = *(int *)(node + 0x168); child != 0; child = *(int *)(child + 0x30)) {
        ctx = arcsel_element_tree_render(ctx, param_2, param_3, param_4, param_5, param_6,
                                         param_7, param_8, child);
    }
    return ctx;
}

// ---------------------------------------------------------------------------
// arcsel_screen_render (0x802e181c)
//   Render entry point (called by session_render_scene_by_mode). Sets up GX
//   material/viewport/scissor, renders the element tree, and blits the HUD.
// ---------------------------------------------------------------------------
void arcsel_screen_render(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                          u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    gx_set_sub_material_id(1);
    if (DAT_80612614 == 0x65) {
        param_1 = render_target_reset_context();
    }
    arcsel_element_tree_render(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8, DAT_80612ac4);
    viewport_set_active(&DAT_805cef14);
    viewport_set_rect_active(0, 0, DAT_806109c4, DAT_806109c8);
    viewport_set_depth_params_active(
        (double)(FLOAT_8061a178 + FLOAT_8060fad0), -(double)(FLOAT_8061a178 + FLOAT_8060fad0),
        (double)FLOAT_8061a17c);
    viewport_set_gx_active();
    gx_set_scissor_rect(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
    if ((DAT_80612614 == 0x65) || (DAT_80612614 == 0x6a) || (DAT_80612614 == 0x6b)) {
        FUN_802e6ec4();
    }
    FUN_802e6f7c();
    hud_screen_render_viewport(DAT_80612ac4, 0);
}

// ---------------------------------------------------------------------------
// arcsel_render_noop (0x802e18dc)
//   Empty render hook (placeholder slot in the screen vtable).
// ---------------------------------------------------------------------------
void arcsel_render_noop(void)
{
}

// ---------------------------------------------------------------------------
// arcsel_set_flags (0x802e18e0)
//   ORs the given bits into the screen flag word DAT_805cc60c.
// ---------------------------------------------------------------------------
void arcsel_set_flags(uint bits)
{
    DAT_805cc60c = DAT_805cc60c | bits;
}

// ---------------------------------------------------------------------------
// arcsel_slot_flag_bits (0x802e18f8)
//   Computes slot flag bits from language state and the per-mode config table.
// ---------------------------------------------------------------------------
uint arcsel_slot_flag_bits(u32 unused, int modeIdx)
{
    uint flags = 0;
    if (DAT_805015bd != '\n') {
        flags = 0x20000;
    }
    if (*(int *)(&DAT_803f2ddc + modeIdx * 0x20) != 0) {
        return flags;
    }
    return flags | 0x2000000;
}

// ---------------------------------------------------------------------------
// arcsel_current_mode_id (0x802e1938)
//   Returns the active game-mode id, reading from the worm-mode object when
//   the screen sub-mode is 6/7, else the cached DAT_805cc68e.
// ---------------------------------------------------------------------------
uint arcsel_current_mode_id(void)
{
    char *obj;
    if ((DAT_805cc614 == 6) || (DAT_805cc614 == 7)) {
        obj = (char *)worm_mode_get_object(DAT_805cc628);
        return *(uint *)(obj + 4);
    }
    return (uint)(unsigned char)DAT_805cc68e;
}

// ---------------------------------------------------------------------------
// arcsel_current_character_id (0x802e198c)
//   Returns the active character/level id, worm-mode aware like the above.
// ---------------------------------------------------------------------------
int arcsel_current_character_id(void)
{
    char *obj;
    if ((DAT_805cc614 == 6) || (DAT_805cc614 == 7)) {
        obj = (char *)worm_mode_get_object(DAT_805cc628);
        return *(int *)(obj + 8);
    }
    return (int)DAT_805cc68c;
}

// ---------------------------------------------------------------------------
// arcsel_state_build (0x802e19e4)
//   Big per-sub-mode state builder. Dispatches on DAT_805cc614 (screen
//   sub-mode) to populate the loadout / award / player-slot tables
//   (DAT_805cc654 etc.) and the flag word DAT_805cc690 that drives the list.
// ---------------------------------------------------------------------------
void arcsel_state_build(void)
{
    short nameId;
    int   segCount;
    char *settings;
    int   modeOff;
    uint  flags;
    int  *pi;
    char *sp;
    int   i, j, k;
    char  c;
    int   nSlots;
    int   readyTeam[16];
    int   ammoSeen[30];

    FUN_803826d8();
    settings = (char *)game_get_settings_ptr();
    modeOff = (uint)(unsigned char)DAT_805cc68d * 0x14;
    memcard_session_nop();
    if ((DAT_805cc614 != 1) && (DAT_805cc614 != 0xe)) {
        menu_award_cache_rebuild();
    }
    nSlots = DAT_80611014;
    if (DAT_805cc614 != 0xf) {
        DAT_805cc640 = (char)DAT_80611014;
        DAT_805cc6cc = 0;
        DAT_805cc641 = DAT_805cc640;
    }

    switch (DAT_805cc614) {
    case 1:
        save_stub_noop();
        modeOff = save_get_data_ptr();
        leaf_31e8((int)0x805cc640, modeOff + 0x10, 0x98);
        DAT_805cc6d7 = DAT_806110b0;
        DAT_805cc690 = DAT_805cc690 | 0x10800000;
        for (i = 0; i < DAT_805cc640; i = i + 1) {
            nameId = award_get_name(i, (int)DAT_805cc68c);
            (&DAT_805cc654)[i] = (char)nameId;
        }
        DAT_806110b0 = DAT_806110b0 + 1;
        if (5 < DAT_806110b0) {
            DAT_806110b0 = 1;
        }
        break;
    case 2:
        DAT_805cc640 = 1;
        DAT_805cc641 = 1;
        DAT_805cc642 = 0;
        DAT_805cc68d = 0;
        DAT_805cc68f = 0;
        DAT_805cc690 = 0;
        DAT_805cc68c = 'e';
        break;
    case 4:
        DAT_805cc638 = 0;
        match_build_next_config();
        break;
    case 6:
    case 7: {
        settings = (char *)worm_mode_get_object(DAT_805cc628);
        DAT_805cc640 = (char)DAT_80611014;
        DAT_805cc6a8 = DAT_805cc628;
        DAT_805cc641 = DAT_805cc640;
        flags = arcsel_current_mode_id();
        DAT_805cc68e = (char)flags;
        uint modeBit = 0x8000000;
        DAT_805cc68d = (char)*(int *)(&DAT_803f2dd0 + (flags & 0xff) * 0x20);
        DAT_805cc68f = (char)*(int *)(settings + 0xc);
        if (DAT_805cc614 == 7) {
            modeBit = 0x40;
        }
        DAT_805cc690 = *(uint *)(settings + 0x10) | modeBit;
        DAT_805cc698 = *(int *)(settings + 0x90) * DAT_806109c0;
        DAT_805cc6a0 = *(int *)(settings + 0x94);
        DAT_805cc69c = *(int *)(settings + 0x98);
        DAT_805cc6ac = *(int *)(settings + 0x9c);
        DAT_805cc6c4 = FLOAT_80619ee8;
        DAT_805cc68c = (char)arcsel_current_character_id();
        DAT_80612ab0 = 0;
        memset_wrapper((int)&DAT_805cc654 + 0x24, 0xff, 0xe);
        flags = arcsel_current_mode_id();
        if (flags == 2) {
            if (**(int **)(settings + 0x6c) < 0) {
                nameId = award_get_name(0, (int)DAT_805cc68c);
                DAT_805cc654 = (char)nameId;
            } else {
                DAT_805cc654 = (char)**(int **)(settings + 0x6c);
            }
        } else {
            flags = arcsel_slot_flag_bits((uint)(unsigned char)DAT_805cc68d, (uint)(unsigned char)DAT_805cc68e);
            DAT_805cc690 = DAT_805cc690 | flags | 0x80000;
            memset_wrapper((int)ammoSeen, 0, 0x44);
            nSlots = 0;
            for (i = 0; (nSlots < 0xb) && (-1 < *(int *)(*(int *)(settings + 0x6c) + i)); i = i + 0x10) {
                (&DAT_805cc654)[nSlots] = (char)*(int *)(*(int *)(settings + 0x6c) + i);
                (&DAT_805cc662)[nSlots] = (char)*(int *)(*(int *)(settings + 0x6c) + i + 8);
                if (weapon_type_is_projectile(*(int *)(*(int *)(settings + 0x6c) + i)) != 0) {
                    (&DAT_805cc67e)[nSlots] = (char)*(int *)(*(int *)(settings + 0x6c) + i + 0xc);
                }
                (&DAT_805cc670)[nSlots] = (char)menu_slot_tier_scale((uint)(byte)(&DAT_805cc654)[nSlots]);
                if ((*(uint *)(settings + 0x10) & 1) != 0) {
                    ammoSeen[*(int *)(*(int *)(settings + 0x6c) + i + 8)] = 1;
                }
                nSlots = nSlots + 1;
            }
            DAT_805cc642 = (char)nSlots + -1;
            DAT_805cc643 = 0;
            if ((*(uint *)(settings + 0x10) & 1) != 0) {
                c = 1;
                j = 4;
                k = 0x10;
                do {
                    if (*(int *)((int)ammoSeen + j) != 0) {
                        (&DAT_805cc644)[(uint)(byte)DAT_805cc643] = c;
                        DAT_805cc643 = DAT_805cc643 + 1;
                    }
                    c = c + 1;
                    j = j + 4;
                    k = k - 1;
                } while (k != 0);
            }
        }
        break;
    }
    case 8:
        DAT_805cc640 = 1;
        DAT_805cc641 = 1;
        DAT_805cc642 = 0;
        DAT_805cc68d = 0;
        DAT_805cc68f = 0;
        DAT_805cc690 = 0;
        DAT_805cc68c = 'f';
        break;
    case 10:
        DAT_805cc640 = 1;
        DAT_805cc641 = 1;
        DAT_805cc642 = 0;
        DAT_805cc68c = '8';
        DAT_805cc68d = 10;
        DAT_805cc68e = 2;
        DAT_805cc68f = 0;
        DAT_805cc690 = 0;
        DAT_805cc698 = 0;
        DAT_805cc69c = 0;
        DAT_805cc6a0 = 0;
        for (i = 0; i < DAT_805cc640; i = i + 1) {
            nameId = award_get_name(i, (int)DAT_805cc68c);
            (&DAT_805cc654)[i] = (char)nameId;
        }
        break;
    case 0xb:
    case 0xc:
    case 0xd:
        DAT_805cc640 = 8;
        /* fallthrough */
    case 5:
    case 9: {
        DAT_805cc68c = (char)arcsel_current_character_id();
        flags = arcsel_current_mode_id();
        DAT_805cc68e = (char)flags;
        DAT_805cc690 = 0;
        DAT_805cc68f = 1;
        DAT_805cc634 = gamemode_find_in_table((int)DAT_805cc68c);
        if ((DAT_805cc614 == 5) && (DAT_805cc634 == -100)) {
            DAT_805cc6cc = 1;
        }
        if (DAT_805cc68e != 2) {
            DAT_805cc690 = (-(uint)(*(int *)(settings + modeOff + 0x28) != 0) & 2) | 0x18000;
            if ((DAT_805cc614 != 0xc) && (DAT_805cc614 != 0xd) && (DAT_805cc614 != 0xb) &&
                (DAT_805cc68d != 5)) {
                pi = (int *)game_get_settings_ptr();
                DAT_805cc690 = DAT_805cc690 | (-(uint)(*pi != 0) & 0x1000000);
            }
            sp = (char *)game_get_settings_ptr();
            DAT_805cc690 = DAT_805cc690 | (-(uint)(*(int *)(sp + 0x14) != 0) & 0x40000);
            DAT_805cc690 = DAT_805cc690 | (mode_feature_enabled_check() != 0);
        }
        if (DAT_805cc68e != 2) {
            sp = (char *)game_get_settings_ptr();
            DAT_805cc690 = DAT_805cc690 | (-(uint)(*(int *)(sp + 4) != 0) & 0x400);
        }
        DAT_805cc690 = DAT_805cc690 | (-(uint)(menu_loadout_can_edit_teams() != 0) & 0x80000);
        DAT_805cc69c = 0;
        DAT_805cc6a0 = 0;
        DAT_805cc698 = DAT_806109c0 * (&DAT_803e084c)[*(int *)(settings + modeOff + 0x2c)] * 0x3c;
        sp = (char *)game_get_settings_ptr();
        DAT_805cc6c4 = *(float *)(&DAT_803e0868 + *(int *)(sp + 0x6ac) * 4);
        if (((DAT_805cc68d == 9) || (DAT_805cc68d == 6) ||
             ((byte)(DAT_805cc68d - 1) < 2 || (DAT_805cc68d == 3))) &&
            ((DAT_805cc68e != 0xe) && (DAT_805cc68e != 0xd))) {
            sp = (char *)game_get_settings_ptr();
            DAT_805cc690 = DAT_805cc690 | (-(uint)(*(int *)(sp + 0x1c) != 0) & 4);
        }
        if (DAT_805cc68d == 2) {
            DAT_805cc6a0 = (&DAT_803e0820)[*(int *)(settings + modeOff + 0x30)];
            sp = (char *)game_get_settings_ptr();
            if (*(int *)(sp + 8) == 0) {
                DAT_805cc690 = DAT_805cc690 | 0x1000;
            }
        } else if (DAT_805cc68d < 2) {
            if ((DAT_805cc68d != 0) && (sp = (char *)game_get_settings_ptr(), *(int *)(sp + 8) == 0)) {
                DAT_805cc690 = DAT_805cc690 | 0x1000;
            }
        } else if (DAT_805cc68d == 9) {
            DAT_805cc69c = (&DAT_803e0820)[*(int *)(settings + modeOff + 0x30)];
            if (DAT_805cc68e == 9) {
                DAT_805cc690 = (DAT_805cc690 & 0xffffffef) | 0x20;
            } else if ((DAT_805cc68e != 10) && (DAT_805cc68e != 7) && (DAT_805cc68e != 0x12) &&
                       (DAT_805cc68e != 9)) {
                sp = (char *)game_get_settings_ptr();
                if (*(int *)(sp + 0x18) == 0) {
                    DAT_805cc690 = DAT_805cc690 | 0x10;
                } else {
                    sp = (char *)game_get_settings_ptr();
                    if (*(int *)(sp + 0x18) == 2) {
                        DAT_805cc690 = DAT_805cc690 | 0x20;
                    }
                }
            }
        }
        flags = arcsel_slot_flag_bits((uint)(unsigned char)DAT_805cc68d, (uint)(unsigned char)DAT_805cc68e);
        DAT_805cc690 = DAT_805cc690 | flags;
        if ((DAT_805cc68e == 7) || (DAT_805cc68e == 9)) {
            DAT_805cc6ac = (&DAT_80476328)[*(int *)(settings + modeOff + 0x34)];
            DAT_805cc69c = 0;
        } else {
            DAT_805cc6ac = 0;
        }
        if ((DAT_805cc690 & 1) != 0) {
            settings = (char *)game_get_settings_ptr();
            DAT_805cc690 = DAT_805cc690 | (-(uint)(*(int *)(settings + 0x20) != 0) & 8);
        }
        memset_wrapper((int)readyTeam, 0, 0x40);
        DAT_805cc643 = 0;
        if ((DAT_805cc690 & 1) != 0) {
            j = 0;
            k = DAT_80611014;
            if (0 < DAT_80611014) {
                do {
                    pi = (int *)((int)&DAT_805cc900 + j);
                    j = j + 4;
                    readyTeam[*pi] = 1;
                    k = k - 1;
                } while (k != 0);
            }
            i = 0;
            j = 0;
            do {
                if (menu_loadout_count_ready_humans() != 0) {
                    *(int *)((int)readyTeam + j) = 1;
                }
                i = i + 1;
                j = j + 4;
            } while (i < 4);
            c = 0;
            i = 0;
            k = 0x10;
            do {
                if (*(int *)((int)readyTeam + i) != 0) {
                    (&DAT_805cc644)[(uint)(byte)DAT_805cc643] = c + 1;
                    DAT_805cc643 = DAT_805cc643 + 1;
                }
                c = c + 1;
                i = i + 4;
                k = k - 1;
            } while (k != 0);
        }
        memset_wrapper((int)&DAT_805cc654 + 0x24, 0xff, 0xe);
        j = 0;
        for (i = 0; i < nSlots; i = i + 1) {
            if ((DAT_805cc614 == 9) && (DAT_805cc68e == 2)) {
                segCount = beam_fx_get_segment_count();
                (&DAT_805cc654)[i] = (char)segCount;
            } else {
                int blk = savegame_get_player_block(i);
                (&DAT_805cc654)[i] = (char)*(int *)(blk + 0x60);
                if (weapon_type_is_projectile(*(int *)(blk + 0x60)) != 0) {
                    (&DAT_805cc67e)[i] = (char)*(int *)(blk + 100);
                }
            }
            if ((DAT_805cc690 & 1) == 0) {
                c = 0;
            } else {
                c = (char)*(int *)((int)&DAT_805cc900 + j) + 1;
            }
            (&DAT_805cc662)[i] = c;
            j = j + 4;
            (&DAT_805cc670)[i] = 0;
        }
        if ((DAT_805cc614 == 0xc) || (DAT_805cc614 == 0xd) || (DAT_805cc614 == 0xb)) {
            DAT_805cc642 = 0;
        } else {
            i = 0;
            j = 0;
            k = DAT_80611014;
            do {
                pi = (int *)((int)&DAT_805cc744 + j);
                int ent = *(int *)((int)&DAT_805cc754 + j);
                if (ent == -1) {
                    ent = *(int *)((int)&DAT_805cc750 + j);
                }
                if (menu_slot_is_extra_bot(i) == 0) {
                    if (((DAT_805cc68e != 0x13) || (player_ai_is_story_mode(*pi) == 0)) &&
                        ((-1 < *pi) && (ent != 0))) {
                        (&DAT_805cc662)[k] = 0;
                        if ((DAT_805cc690 & 1) != 0) {
                            int tc = mode_team_count();
                            (&DAT_805cc662)[k] =
                                ((char)*(int *)((int)&DAT_805cc748 + j) -
                                 (char)(*(int *)((int)&DAT_805cc748 + j) / tc) * (char)tc) + 1;
                        }
                        (&DAT_805cc654)[k] = (char)*pi;
                        (&DAT_805cc67e)[k] = (char)*(int *)((int)&DAT_805cc75c + j);
                        if (menu_loadout_can_edit_teams() == 0) {
                            (&DAT_805cc670)[k] = (char)*(int *)((int)&DAT_805cc74c + j);
                        } else {
                            (&DAT_805cc670)[k] = (char)menu_slot_tier_scale((uint)(byte)(&DAT_805cc654)[k]);
                        }
                        k = k + 1;
                    }
                } else {
                    (&DAT_805cc662)[k] = 0;
                    if ((DAT_805cc690 & 1) != 0) {
                        int tc = mode_team_count();
                        (&DAT_805cc662)[k] =
                            ((char)*(int *)((int)&DAT_805cc748 + j) -
                             (char)(*(int *)((int)&DAT_805cc748 + j) / tc) * (char)tc) + 1;
                    }
                    (&DAT_805cc654)[k] = 0x5d;
                    (&DAT_805cc670)[k] = 8;
                    k = k + 1;
                }
                i = i + 1;
                j = j + 0x1c;
            } while (i < 10);
            DAT_805cc642 = (char)k - (char)DAT_80611014;
        }
        break;
    }
    case 0xe:
        DAT_805cc640 = 1;
        DAT_805cc641 = 1;
        DAT_805cc642 = 0;
        DAT_805cc6d3 = (char)DAT_805cc624;
        DAT_805cc68d = 10;
        DAT_805cc68e = 2;
        DAT_805cc68f = 0;
        DAT_805cc690 = 0xa0000000;
        DAT_805cc68c = (char)*(int *)(&DAT_80471c54 + (DAT_805cc624 & 0xff) * 0x24);
        for (i = 0; i < DAT_805cc640; i = i + 1) {
            nameId = award_get_name(i, (int)DAT_805cc68c);
            (&DAT_805cc654)[i] = (char)nameId;
        }
        break;
    case 0x10:
        modeOff = save_get_data_ptr();
        leaf_31e8((int)0x805cc640, modeOff + 0x10, 0x98);
        DAT_805cc690 = DAT_805cc690 | 0x800000;
        DAT_805cc6d7 = 1;
        DAT_805cc628 = DAT_805cc6a8;
        break;
    case 0x15:
        DAT_805cc638 = 2;
        match_build_next_config();
        break;
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// arcsel_commit_transition (0x802e2650)
//   Clears the pending-input flag, chooses the next sub-mode based on the
//   screen id, rebuilds state, and kicks the match cutscene transition.
// ---------------------------------------------------------------------------
void arcsel_commit_transition(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                              u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                              u32 p9, u32 p10, u32 p11, u32 p12,
                              u32 p13, u32 p14, u32 p15, u32 p16)
{
    u32 r4dummy = 0;

    DAT_805cc60c = DAT_805cc60c & 0xfffdffff;
    if (DAT_80612614 == 0x65) {
        DAT_805cc608 = DAT_805cc614;
    } else if (DAT_80612614 == 0x6a) {
        DAT_805cc614 = 0x15;
    } else {
        DAT_805cc614 = 5;
    }
    arcsel_state_build();
    match_start_cutscene_transition(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    (int)0x805cc640, r4dummy, p11, p12, p13, p14, p15, p16);
}

// ---- batch_831 raw symbols (cont.) ----
extern int DAT_805cef68;   // camera/viewport state block base
extern int DAT_80612b60;

// ---------------------------------------------------------------------------
// arcsel_camera_state_reset (0x802e26c8)
//   Zeroes the 0x57-dword camera/viewport state block at DAT_805cef68 and
//   sets the pending-region mask DAT_80612b60 to 0xffffffc0.
// ---------------------------------------------------------------------------
void arcsel_camera_state_reset(void)
{
    int off;
    int n;

    off = 0;
    n = 0x57;
    do {
        *(int *)((int)&DAT_805cef68 + off) = 0;
        off = off + 4;
        n = n - 1;
    } while (n != 0);
    DAT_80612b60 = 0xffffffc0;
}
