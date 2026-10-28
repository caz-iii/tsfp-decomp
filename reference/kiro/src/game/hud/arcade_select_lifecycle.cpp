// Arcade select-screen lifecycle: HUD style definition, resource loading, and
// the per-player screen construct / show / hide / reset / close entry points.
//
// Module: top-level owner of the arcade character/level select screens. Builds
//   the HUD style tree (arcsel_style_define), loads the gamemode textures and
//   sound banks (arcsel_load_resources), allocates and initializes the up-to-4
//   per-player screen objects (arcsel_construct -> arcsel_screens_init, which
//   seeds the wind ripples from arcade_select_prompt_wind.cpp), and drives the
//   open/close state transitions. Subsystem: graphics (HUD).
//
// Non-matching decomp. Behavior-faithful C++ with C-like patterns.
//
// Address map (this file):
//   0x802f28e8  arcsel_style_define        build HUD style (font/spacing/element)
//   0x802f2afc  arcsel_load_resources      load gamemode textures + sound banks
//   0x802f2b8c  arcsel_screens_init        per-player screen state init
//   0x802f2e54  arcsel_construct           top-level construct
//   0x802f2ed8  arcsel_hide_all            hide all active screens
//   0x802f2f30  arcsel_screen_reset_flags  reset one screen's flags
//   0x802f2fb4  arcsel_screen_is_active    query one screen's active flag
//   0x802f2fe8  arcsel_screen_show         activate/show one screen
//   0x802f3098  arcsel_screen_open         full show sequence (prompts + push)
//   0x802f3200  arcsel_screen_close        close/dismiss one screen

#include "common.h"

// ---- Per-file externs: HUD style builder ----
extern void hud_style_create_font(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h,
                                  int i, u32 j, u32 k, u32 l, u32 m, u32 n, u32 o, u32 p);
extern void hud_style_font_set_resource(int id);
extern void hud_style_font_set_color(double a, double b, double c, double d);
extern void hud_style_font_set_scale(double a, double b);
extern void hud_style_font_set_offset(double a, double b);
extern void hud_style_font_set_shadow(double a, double b);
extern void hud_style_font_set_shadow_color(double a, double b, double c, double d);
extern void hud_style_font_set_style(int a, int b, int c);
extern void hud_style_font_set_margins(double a, double b, double c, double d, double e);
extern void hud_style_font_set_animation(int a, void *fn0, int b, void *fn1, int c, void *fn2);
extern void hud_style_font_set_truncation_params(void *a, void *b);
extern u64  hud_style_font_set_alignment(int a);
extern u64  hud_style_font_set_truncation(void *fn);
extern void hud_style_create_spacing(u64 a, double b, double c, double d, double e,
                                     u64 f, u64 g, u64 h, int i, u32 j,
                                     u32 k, void *l, u32 m, void *n, u32 o, u32 p);
extern void hud_style_spacing_set_margin(int idx, u32 a, u32 b);
extern void hud_style_spacing_set_padding(int idx, u32 a);
extern void hud_style_spacing_set_gap(int idx, u32 a);
extern void hud_style_spacing_set_alignment(u32 a, int b, int c);
extern void hud_style_create_element(u64 a, double b, double c, double d, double e,
                                     u64 f, u64 g, u64 h, int i, u32 j,
                                     u32 k, void *l, u32 m, void *n, u32 o, u32 p);
extern void hud_style_element_set_variants(int a, int b, int c);
extern void hud_style_reset_counters(void);
extern void hud_style_push_element(int a);

// ---- Per-file externs: drawing callbacks referenced as style function ptrs ----
extern void arcsel_draw_row_frame(void);
extern void arcsel_draw_row_bar(void);
extern void arcsel_fx_draw_panel_clamped(void);
extern void arcsel_draw_flash_gated(int obj);      // 0x802f22a0 (this module)
extern void arcsel_render_prompt_bar(void);        // 0x802f22e8 (this module)

// ---- Per-file externs: resource + engine helpers ----
extern int  stats_is_tracking_active(void);
extern void gamemode_find_in_table(int id);
extern u64  sprite_load_texture_set(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h);
extern void sound_clear_bank_ref(int bank);

extern u64  FUN_803826cc(void);                    // construct scope enter (screens_init)
extern u64  FUN_803826f0(void);                    // scope enter (active-index fetch)
extern void FUN_8038273c(void);                    // scope leave
extern void FUN_80382718(void);                    // screens_init scope leave
extern void hud_system_init(void);
extern int  session_get_state_flag(void);
extern void select_list_init_empty(u32 *list, int a);
extern u32  hud_viewport_create_element(int idx, int a, void *list, int b);
extern void memset_wrapper(int dst, int val, int len);
extern int  worm_mode_get_index(void);
extern void render_target_alloc_scratch(int w, int h, int *out);
extern int  rng_counter_tick(void);
extern void arcsel_wind_seed_ripple(int base, int idx);   // 0x802f2138

extern u64  hud_alloc_element_pool(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h,
                                   int i, u32 j, u32 k, u32 l, u32 m, u32 n, u32 o, u32 p);
extern int  mem_alloc(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h,
                      int size, int flag, u32 k, u32 l, u32 m, u32 n, u32 o, u32 p);
extern void arcsel_label_strings_build(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h);

extern void hud_destroy_entity_elements(int handle);
extern int  arcsel_is_10player_split(void);
extern u32  camera_get_split_mask(void);
extern void camera_splitscreen_restore(void);
extern void camera_set_split_mask(u32 mask);
extern void viewport_set_rect_active(int x, int y, int w, int h);
extern int  arcsel_show_handicap_prompt(void);
extern int  arcsel_prompt_stub_zero(void);
extern int  arcsel_show_teams_prompt(void);
extern void hud_screen_push(void);
void arcsel_screen_hide(int param_1);              // 0x802f3718 (defined below, batch_845)

// ---- Additional externs for batch_845 transition/update functions ----
extern u64  FUN_803826e4(void);                    // transition scope enter (index fetch)
extern void FUN_80382730(void);                    // transition scope leave
extern void FUN_80300690(void);                    // stats-mode accept hook
extern u64  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern void sound_init_play_params(int params);
extern void sound_play_request(int *params);
extern void toggle_latch_update(int on);
extern void input_reset_pad(int idx);
extern void render_target_reset_context(void);
extern void camera_splitscreen_setup(void);
extern void viewport_set_rect(int *vp, int x, int y, int w, int h);
extern int  DAT_80611d54;      // viewport owner base
extern int  DAT_80611d74;      // per-frame context ptr (+4 slot)

// ---- Raw data symbols ----
extern int   DAT_805cf270;      // per-player viewport handle table
extern int   DAT_805cf280;      // per-player screen object table
extern int   DAT_805cfc04;      // per-player select-list storage
extern int   DAT_80611d78;      // per-player context array base
extern int   DAT_80611934;      // session sub-mode
extern int   DAT_806119ec;      // active screen index
extern int   DAT_80612610;      // demo/attract player count
extern int   DAT_80612614;      // current gamemode id
extern int   DAT_80612b98;      // arcsel state field
extern int   DAT_80612b9c;      // popup-active flag
extern int   DAT_80612bc8;      // damage-ring / split enable flag
extern int   DAT_80612bcc;      // arcsel selection mask
extern int   DAT_80612bd0;      // per-player open mask
extern int   DAT_80612bd4;      // per-player ready mask
extern uint  DAT_805015c0;      // global feature/mode flags
extern char  DAT_805015bd;      // language/demo mode byte
extern int   DAT_805015d8;      // active gamemode index
extern int   DAT_806109c4;      // screen width
extern int   DAT_806109c8;      // screen height

extern float FLOAT_8061a418;    // 0.0
extern float FLOAT_8061a420;
extern float FLOAT_8061a444;
extern float FLOAT_8061a460;
extern float FLOAT_8061a464;
extern float FLOAT_8061a494;
extern float FLOAT_8061a4dc;
extern float FLOAT_8061a4f0;
extern float FLOAT_8061a4f8;
extern float FLOAT_8061a4fc;
extern float FLOAT_8061a500;
extern double DOUBLE_8061a478;  // signed-int magic bias

// ----------------------------------------------------------------------------
// 0x802f28e8  arcsel_style_define
// Builds the arcade-select HUD style tree: the font style (resource 0x1020,
// colors/scale/shadow/margins, and the row draw callbacks), the spacing style
// (margins/padding/gaps/alignment), and the element style, then pushes it.
// The 16 pass-through params are the style-builder context/handle tuple.
// ----------------------------------------------------------------------------
void arcsel_style_define(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                         u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                         u32 param_9, u32 param_10, u32 param_11, u32 param_12,
                         u32 param_13, u32 param_14, u32 param_15, u32 param_16) {
    hud_style_create_font(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                          0, 0xffffffff, param_11, param_12, param_13, param_14, param_15, param_16);
    hud_style_font_set_resource(0x1020);
    hud_style_font_set_color((double)FLOAT_8061a4f8, (double)FLOAT_8061a494,
                             (double)FLOAT_8061a4f8, (double)FLOAT_8061a494);
    hud_style_font_set_scale((double)FLOAT_8061a4fc, (double)FLOAT_8061a4fc);
    hud_style_font_set_offset((double)FLOAT_8061a418, (double)FLOAT_8061a418);
    hud_style_font_set_shadow((double)FLOAT_8061a494, (double)FLOAT_8061a494);
    hud_style_font_set_shadow_color((double)FLOAT_8061a418, (double)FLOAT_8061a500,
                                    (double)FLOAT_8061a500, (double)FLOAT_8061a420);
    hud_style_font_set_style(0x12, 3, 4);

    double m0 = (double)FLOAT_8061a4dc;
    double m1 = (double)FLOAT_8061a418;
    hud_style_font_set_margins((double)FLOAT_8061a4f0, m0, m0, m1, m1);

    void *cb_frame = (void *)&arcsel_draw_row_frame;
    void *cb_flash = (void *)&arcsel_draw_flash_gated;
    hud_style_font_set_animation(0, (void *)&arcsel_fx_draw_panel_clamped, 0,
                                 (void *)&arcsel_draw_flash_gated, 0, (void *)&arcsel_draw_row_frame);
    hud_style_font_set_truncation_params((void *)&arcsel_draw_row_bar, (void *)&arcsel_draw_row_bar);

    u64 sp = hud_style_font_set_alignment(1);
    hud_style_create_spacing(sp, m0, m0, m1, m1, param_6, param_7, param_8, 0, 0xffffffff,
                             0, cb_flash, 0, cb_frame, param_15, param_16);
    hud_style_spacing_set_margin(0, 0x7878787f, 0x1e1e1e7f);
    hud_style_spacing_set_margin(1, 0x7f7f7f7f, 0x5235a50);
    hud_style_spacing_set_margin(2, 0x7f7f7f7f, 0x7f7f7f7f);
    hud_style_spacing_set_margin(3, 0x2828287f, 0x2828287f);
    hud_style_spacing_set_margin(4, 0x28283250, 0xa0a1e40);
    hud_style_spacing_set_padding(0, 0x1e2d4646);
    hud_style_spacing_set_padding(1, 0x2d4b697f);
    hud_style_spacing_set_padding(2, 0x69737f7f);
    hud_style_spacing_set_padding(3, 0x1e283237);
    hud_style_spacing_set_gap(2, 0x120a1b7f);
    hud_style_spacing_set_gap(3, 0x7f);
    hud_style_spacing_set_gap(4, 0x14383c);
    hud_style_spacing_set_alignment(0x1e235a00, 0, 0);

    u64 el = hud_style_font_set_truncation((void *)&arcsel_render_prompt_bar);
    hud_style_create_element(el, m0, m0, m1, m1, param_6, param_7, param_8, 0, 0xffffffff,
                             0, cb_flash, 0, cb_frame, param_15, param_16);
    hud_style_element_set_variants(-1, 0, 0);
    hud_style_reset_counters();
    hud_style_push_element(0);
}

// ----------------------------------------------------------------------------
// 0x802f2afc  arcsel_load_resources
// When stats tracking is active, loads the gamemode's texture set(s) (a second
// set unless the 0x100000 feature flag is set), then clears the two arcade
// sound-bank references (0xd26/0xd27).
// ----------------------------------------------------------------------------
void arcsel_load_resources(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                           u64 param_5, u64 param_6, u64 param_7, u64 param_8) {
    if (stats_is_tracking_active() != 0) {
        gamemode_find_in_table(DAT_80612614);
        u64 tex = sprite_load_texture_set(param_1, param_2, param_3, param_4,
                                          param_5, param_6, param_7, param_8);
        if ((DAT_805015c0 & 0x100000) == 0) {
            tex = sprite_load_texture_set(tex, param_2, param_3, param_4,
                                          param_5, param_6, param_7, param_8);
        }
        sprite_load_texture_set(tex, param_2, param_3, param_4,
                                param_5, param_6, param_7, param_8);
    }
    sound_clear_bank_ref(0xd26);
    sound_clear_bank_ref(0xd27);
}

// ----------------------------------------------------------------------------
// 0x802f2b8c  arcsel_screens_init
// Initializes the per-player screen objects: creates each viewport element,
// zeroes the 0x398-byte screen struct, allocates its scratch render target
// (sized from the player's camera split), seeds 10 RNG jitter offsets and 40
// wind ripples, then clears the unused player slots.
// ----------------------------------------------------------------------------
void arcsel_screens_init(void) {
    int  player;
    u32  vp;
    int  obj;
    int  ctx;
    uint sw;
    uint sh;
    int  slot9;
    int  slot8;
    int  slot7;
    double range = (double)FLOAT_8061a464;
    double half  = (double)FLOAT_8061a444;
    double zero  = (double)FLOAT_8061a418;
    double bias  = DOUBLE_8061a478;

    FUN_803826cc();
    DAT_80612b9c = 0;
    DAT_80612bcc = (int)0xffffffff;
    DAT_80612bc8 = 1;
    DAT_80612b98 = 0;
    hud_system_init();

    slot9 = 0;
    slot8 = 0;
    slot7 = 0;
    for (player = 0; player < session_get_state_flag(); player = player + 1) {
        obj = *(int *)((int)&DAT_805cf280 + slot8);
        ctx = DAT_80611d78 + slot7;
        select_list_init_empty((u32 *)((int)&DAT_805cfc04 + slot9), 0);
        vp = hud_viewport_create_element(player, 0, (void *)((int)&DAT_805cfc04 + slot9), 0);
        *(u32 *)((int)&DAT_805cf270 + slot8) = vp;
        *(uint *)(*(int *)((int)&DAT_805cf270 + slot8) + 0x40) =
            *(uint *)(*(int *)((int)&DAT_805cf270 + slot8) + 0x40) | 0x800000;
        memset_wrapper(obj, 0, 0x398);
        *(u32 *)(obj + 4) = 0xffffffff;

        if ((((DAT_805015c0 & 0x40) == 0) || (worm_mode_get_index() != 0x13)) &&
            (((stats_is_tracking_active() == 0) &&
              ((DAT_805015bd != '\n') || (DAT_80612610 < 2))) || (player != 0))) {
            ctx = *(int *)(ctx + 8);
            sw = *(uint *)(ctx + 0x254);
            sh = *(uint *)(ctx + 600);
        } else {
            sw = 0;
            sh = 0;
        }
        render_target_alloc_scratch(0x80 >> (sw & 0x3f), 0x80 >> (sh & 0x3f), (int *)(obj + 4));

        int i = 0;
        int off = 0;
        do {
            u32 r = (u32)rng_counter_tick();
            if ((r & 1) == 0) {
                u32 rv = (u32)rng_counter_tick();
                *(float *)(obj + off + 0x18) =
                    -((float)((double)CONCAT44(0x43300000, rv) - DOUBLE_8061a478) / FLOAT_8061a464);
            } else {
                u32 rv = (u32)rng_counter_tick();
                *(float *)(obj + off + 0x18) =
                    FLOAT_8061a460 *
                    ((float)((double)CONCAT44(0x43300000, rv) - DOUBLE_8061a478) / FLOAT_8061a464);
            }
            u32 rv2 = (u32)rng_counter_tick();
            i = i + 1;
            int off40 = off + 0x40;
            off = off + 4;
            *(float *)(obj + off40) =
                (float)(half * (double)(float)((double)(float)((double)CONCAT44(0x43300000, rv2) -
                                                               bias) / range) + half);
        } while (i < 10);

        *(float *)(obj + 0x68) = (float)zero;
        *(float *)(obj + 0x6c) = (float)zero;
        *(float *)(obj + 0x25c) = (float)zero;
        *(float *)(obj + 0x260) = (float)zero;
        *(u32 *)(obj + 0x264) = 0;
        int ri = 0;
        do {
            arcsel_wind_seed_ripple(obj, ri);
            ri = ri + 1;
        } while (ri < 0x28);

        slot9 = slot9 + 0x1e8;
        slot8 = slot8 + 4;
        slot7 = slot7 + 0x141c;
    }

    int remaining = 4 - player;
    int clr = player << 2;
    if (player < 4) {
        do {
            *(u32 *)((int)&DAT_805cf270 + clr) = 0;
            *(u32 *)((int)&DAT_805cf280 + clr) = 0;
            clr = clr + 4;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    FUN_80382718();
}

// ----------------------------------------------------------------------------
// 0x802f2e54  arcsel_construct
// Top-level construct: allocates the element pool, allocates one screen object
// per active player, defines the HUD style, initializes the screens, builds the
// label strings, and clears the ready/open masks.
// ----------------------------------------------------------------------------
void arcsel_construct(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                      u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                      u32 param_9, u32 param_10, u32 param_11, u32 param_12,
                      u32 param_13, u32 param_14, u32 param_15, u32 param_16) {
    u64 pool;
    int obj = 0;
    int slot = 0;

    FUN_803826f0();
    pool = hud_alloc_element_pool(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                  param_8, 0, 0, param_11, param_12, param_13, param_14,
                                  param_15, param_16);
    for (int p = 0; p < session_get_state_flag(); p = p + 1) {
        obj = mem_alloc(pool, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                        0x3a0, 1, param_11, param_12, param_13, param_14, param_15, param_16);
        *(int *)((int)&DAT_805cf280 + slot) = obj;
        slot = slot + 4;
    }
    arcsel_style_define(pool, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                        (u32)obj, 1, param_11, param_12, param_13, param_14, param_15, param_16);
    arcsel_screens_init();
    arcsel_label_strings_build(pool, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    DAT_80612bd4 = 0;
    DAT_80612bd0 = 0;
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// 0x802f2ed8  arcsel_hide_all
// Hides all active screens (unless the 0x20000000 feature flag suppresses it).
// ----------------------------------------------------------------------------
void arcsel_hide_all(void) {
    if ((DAT_805015c0 & 0x20000000) == 0) {
        for (int p = 0; p < session_get_state_flag(); p = p + 1) {
            arcsel_screen_hide(p);
        }
    }
}

// ----------------------------------------------------------------------------
// 0x802f2f30  arcsel_screen_reset_flags
// Resets one screen's state flags (keeps only bits 0xa7), destroys its entity
// elements, and re-arms the "closing" bit (4) when in sub-mode 8.
// ----------------------------------------------------------------------------
void arcsel_screen_reset_flags(int param_1) {
    uint *screen;
    if ((-1 < param_1) && (param_1 < 4)) {
        screen = (uint *)(&DAT_805cf280)[param_1];
        hud_destroy_entity_elements((&DAT_805cf270)[param_1]);
        DAT_806119ec = param_1;
        *screen = *screen & 0xa7;
        if (DAT_80611934 == 8) {
            *screen = *screen | 4;
        }
    }
}

// ----------------------------------------------------------------------------
// 0x802f2fb4  arcsel_screen_is_active
// Returns the "active" flag (bit 0) of screen `param_1`, or 0 if out of range.
// ----------------------------------------------------------------------------
uint arcsel_screen_is_active(int param_1) {
    if ((-1 < param_1) && (param_1 < 4)) {
        return *(uint *)(&DAT_805cf280)[param_1] & 1;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// 0x802f2fe8  arcsel_screen_show
// Activates the active screen: sets flag 1, zeroes its fade/scroll fields,
// restores the split-screen mask for 10-player mode, sets the fully-shown
// value when not in the base state, resets its flags, and arms the shown bit.
// ----------------------------------------------------------------------------
void arcsel_screen_show(void) {
    float zero;
    int idx;
    int r;
    u32 mask;
    uint *screen;

    idx = FUN_803826f0();
    zero = FLOAT_8061a418;
    if ((-1 < idx) && (idx < 4)) {
        screen = (uint *)(&DAT_805cf280)[idx];
        *screen = 1;
        screen[2] = (uint)zero;
        screen[0x94] = (uint)zero;
        r = arcsel_is_10player_split();
        if ((r != 0) && (idx == 0)) {
            mask = camera_get_split_mask();
            camera_splitscreen_restore();
            camera_set_split_mask(mask);
        }
        if (1 < session_get_state_flag()) {
            screen[2] = (uint)FLOAT_8061a494;
        }
        arcsel_screen_reset_flags(idx);
        *screen = *screen | 0x80;
    }
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// 0x802f3098  arcsel_screen_open
// Full show sequence: sets the full-screen viewport for worm mode, shows the
// screen, marks it fully shown (flags 0x100), and pushes the appropriate popup
// (handicap / teams / default) before arming the open bit (0x10).
// ----------------------------------------------------------------------------
void arcsel_screen_open(void) {
    int idx;
    uint *screen;

    idx = FUN_803826f0();
    if (-1 < idx) {
        if (idx < 4) {
            screen = (uint *)(&DAT_805cf280)[idx];
            if (((DAT_805015c0 & 0x40) != 0) && (DAT_805015d8 == 0x13)) {
                viewport_set_rect_active(0, 0, DAT_806109c4 + -1, DAT_806109c8 + -1);
            }
            arcsel_screen_show();
            screen[2] = (uint)FLOAT_8061a494;
            *screen = *screen | 0x100;
            if ((arcsel_show_handicap_prompt() == 0) || (DAT_805015bd == '\x03')) {
                if ((arcsel_prompt_stub_zero() == 0) || (DAT_805015bd == '\x03')) {
                    (void)arcsel_show_teams_prompt();
                    hud_screen_push();
                } else {
                    hud_screen_push();
                }
            } else {
                hud_screen_push();
            }
            *screen = *screen | 0x10;
        }
    }
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// 0x802f3200  arcsel_screen_close
// Closes/dismisses screen `param_1`: clears its bits in the open/ready masks,
// resets flag 0x20, arms flags 0x12, and pushes the screen unless it is already
// closing (bit 4).
// ----------------------------------------------------------------------------
void arcsel_screen_close(int param_1) {
    uint *screen;
    if (-1 < param_1) {
        if (param_1 < 4) {
            DAT_80612bd0 = DAT_80612bd0 & ~(1 << param_1);
            DAT_80612bd4 = DAT_80612bd4 & ~(1 << param_1);
            screen = (uint *)(&DAT_805cf280)[param_1];
            *screen = *screen & 0xffffffdf;
            *screen = *screen | 0x12;
            if ((*screen & 4) == 0) {
                hud_screen_push();
            }
        }
    }
}

// ============================================================================
// Transition / per-screen update entry points (batch_845).
// ============================================================================

// ----------------------------------------------------------------------------
// 0x802f329c  arcsel_transition_back
// "Back / cancel" transition. When invoked with no active index (all screens),
// re-shows any inactive screen, arms flag 0x20, resets fade, pushes each, and
// clears the open/ready masks. With a valid index it shows that screen and
// closes the others. Plays the transition sound (0xd6a) and latches input.
// ----------------------------------------------------------------------------
void arcsel_transition_back(void) {
    float zero_f;
    int idx;
    int i;
    uint *screen;
    double zero;
    int params_all[12];
    int params_one[20];

    idx = FUN_803826e4();
    if ((idx < 0) || (3 < idx)) {
        sound_init_play_params((int)params_all);
        params_all[0] = 0xd6a;
        sound_play_request(params_all);
        DAT_80612bcc = session_get_state_flag() + -1;
        zero = (double)FLOAT_8061a418;
        int off = 0;
        for (i = 0; i < session_get_state_flag(); i = i + 1) {
            screen = *(uint **)((int)&DAT_805cf280 + off);
            if (((*screen & 1) == 0) || ((*screen & 2) != 0)) {
                arcsel_screen_show();
            }
            *screen = *screen | 0x20;
            screen[2] = (uint)(float)zero;
            if (1 < session_get_state_flag()) {
                screen[2] = (uint)FLOAT_8061a494;
            }
            hud_screen_push();
            toggle_latch_update(1);
            off = off + 4;
        }
        DAT_80612bd0 = 0;
        DAT_80612bd4 = 0;
    } else {
        screen = (uint *)(&DAT_805cf280)[idx];
        if (((DAT_805015c0 & 0x40) != 0) && (DAT_805015d8 == 0x13)) {
            viewport_set_rect_active(0, 0, DAT_806109c4 + -1, DAT_806109c8 + -1);
        }
        sound_init_play_params((int)params_one);
        params_one[0] = 0xd6a;
        sound_play_request(params_one);
        DAT_80612bcc = idx;
        int off = 0;
        for (i = 0; i < session_get_state_flag(); i = i + 1) {
            zero_f = FLOAT_8061a418;
            if (((**(uint **)((int)&DAT_805cf280 + off) & 1) == 0) &&
                (arcsel_screen_show(), i != idx)) {
                arcsel_screen_close(i);
            }
            off = off + 4;
        }
        DAT_80612bd0 = DAT_80612bd0 & ~(1 << idx);
        DAT_80612bd4 = DAT_80612bd4 & ~(1 << idx);
        *screen = *screen | 0x20;
        screen[2] = (uint)zero_f;
        if (1 < session_get_state_flag()) {
            screen[2] = (uint)FLOAT_8061a494;
        }
        hud_screen_push();
        toggle_latch_update(1);
    }
    FUN_80382730();
}

// ----------------------------------------------------------------------------
// 0x802f34d4  arcsel_transition_select
// "Select / accept" transition for the active screen. Plays the sound, handles
// the worm-mode full-screen viewport, then branches on stats-tracking / demo
// state to either show the screen, hide+disable the sub-viewport, or run the
// stats accept hook. Finally arms flags 0x24 and clears the open mask.
// ----------------------------------------------------------------------------
void arcsel_transition_select(void) {
    float zero_f;
    int idx;
    int tracking;
    uint *screen;
    int params[18];

    idx = FUN_803826f0();
    if ((idx < 0) || (3 < idx)) goto done;
    screen = (uint *)(&DAT_805cf280)[idx];
    sound_init_play_params((int)params);
    params[0] = 0xd6a;
    sound_play_request(params);
    if (((DAT_805015c0 & 0x40) != 0) && (DAT_805015d8 == 0x13)) {
        viewport_set_rect((int *)(DAT_80611d54 + 0x224), 0, 0, DAT_806109c4 + -1, DAT_806109c8 + -1);
    }
    tracking = stats_is_tracking_active();
    if (tracking == 0) {
        if (DAT_805015bd == '\n') {
            if (DAT_80612610 < 2) goto label_default;
            DAT_80612bc8 = 0;
            if (idx < 1) {
                arcsel_screen_show();
                viewport_set_rect((int *)(*(int *)(DAT_80611d78 + idx * 0x141c + 8) + 0x224),
                                  0, 0, DAT_806109c4 + -1, DAT_806109c8 + -1);
                hud_screen_push();
            } else {
                arcsel_screen_hide(idx);
                int sub = *(int *)(DAT_80611d78 + idx * 0x141c + 8);
                *(uint *)(sub + 0x21c) = *(uint *)(sub + 0x21c) & 0xfffffffe;
                *screen = *screen | 0x200;
            }
        } else {
label_default:
            if ((*screen & 1) == 0) {
                arcsel_screen_show();
            } else {
                arcsel_screen_hide(idx);
                arcsel_screen_show();
            }
        }
        hud_screen_push();
    } else if (idx < 1) {
        arcsel_screen_show();
        viewport_set_rect((int *)(*(int *)(DAT_80611d78 + idx * 0x141c + 8) + 0x224),
                          0, 0, DAT_806109c4 + -1, DAT_806109c8 + -1);
        FUN_80300690();
    } else {
        arcsel_screen_hide(idx);
        int sub = *(int *)(DAT_80611d78 + idx * 0x141c + 8);
        *(uint *)(sub + 0x21c) = *(uint *)(sub + 0x21c) & 0xfffffffe;
        *screen = *screen | 0x200;
    }
    zero_f = FLOAT_8061a418;
    DAT_80612bd0 = 0;
    *screen = *screen | 0x24;
    screen[2] = (uint)zero_f;
done:
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// 0x802f3718  arcsel_screen_hide
// Hides screen `param_1`: clears its transient state bits (keeps only bits in
// 0xfffffe58's complement is cleared -> keep the high state, clear 0x1a7) and
// destroys its entity elements.
// ----------------------------------------------------------------------------
void arcsel_screen_hide(int param_1) {
    if ((-1 < param_1) && (param_1 < 4)) {
        *(uint *)(&DAT_805cf280)[param_1] = *(uint *)(&DAT_805cf280)[param_1] & 0xfffffe58;
        hud_destroy_entity_elements((&DAT_805cf270)[param_1]);
    }
}

// ----------------------------------------------------------------------------
// 0x802f3774  arcsel_screen_update
// Per-screen update tick: drives the open/close state machine. Handles the
// worm-mode split viewport, the "deselect" path (hide + restore split camera),
// the closing path (sound + reset + close), and the reopening path (sound +
// flags + reset + push). Resets the pad and render-target context at the end.
// ----------------------------------------------------------------------------
void arcsel_screen_update(void) {
    int idx;
    uint active;
    int split;
    uint *screen;
    int params_close[18];
    int params_reopen[12];

    idx = FUN_803826f0();
    if ((-1 < idx) && (idx < 4)) {
        screen = (uint *)(&DAT_805cf280)[idx];
        active = arcsel_screen_is_active(*(int *)(DAT_80611d74 + 4));
        if ((active != 0) && (((DAT_805015c0 & 0x40) != 0) && (DAT_805015d8 == 0x13))) {
            uint w = DAT_806109c4 - 1;
            viewport_set_rect_active(0, 0,
                                     ((int)w >> 1) + (int)((uint)((int)w < 0 && (w & 1) != 0)),
                                     DAT_806109c8 + -1);
        }
        if ((DAT_80612bcc < 0) && ((*screen & 4) == 0)) {
            arcsel_screen_hide(idx);
            DAT_80612bc8 = 1;
            split = arcsel_is_10player_split();
            if ((split != 0) && ((int)camera_get_split_mask() == (1 << idx))) {
                camera_splitscreen_setup();
            }
        } else if ((*screen & 2) == 0) {
            sound_init_play_params((int)params_close);
            params_close[0] = 0xd6a;
            sound_play_request(params_close);
            arcsel_screen_reset_flags(idx);
            arcsel_screen_close(idx);
        } else if ((*screen & 8) == 0) {
            sound_init_play_params((int)params_reopen);
            params_reopen[0] = 0xd6a;
            sound_play_request(params_reopen);
            *screen = *screen | 0x20;
            *screen = *screen & 0xfffffffd;
            arcsel_screen_reset_flags(idx);
            hud_screen_push();
        }
        input_reset_pad(idx);
        render_target_reset_context();
    }
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// 0x802f3914  arcsel_transition_finalize
// Once every screen has reached the "shown/committed" state (flag 0x20, and in
// sub-mode 7 also clear of flag 4), releases the selection latch: clears the
// selection mask, runs arcsel_screen_update on any screen still transitioning
// (flag 2), and unlatches input if any were processed.
// ----------------------------------------------------------------------------
void arcsel_transition_finalize(void) {
    int i;
    int off;
    int processed;

    __save_gpr_28();
    if ((-1 < DAT_80612bcc) || (DAT_80611934 == 8)) {
        off = 0;
        for (i = 0; i < session_get_state_flag(); i = i + 1) {
            if (((DAT_80611934 != 7) || ((**(uint **)((int)&DAT_805cf280 + off) & 4) == 0)) &&
                ((**(uint **)((int)&DAT_805cf280 + off) & 0x20) != 0)) break;
            off = off + 4;
        }
        if ((i == session_get_state_flag()) && (-1 < DAT_80612bcc)) {
            DAT_80612bcc = -1;
            processed = 0;
            int j = 0;
            off = 0;
            while (j < session_get_state_flag()) {
                if ((**(uint **)((int)&DAT_805cf280 + off) & 4) == 0) {
                    if ((**(uint **)((int)&DAT_805cf280 + off) & 2) != 0) {
                        arcsel_screen_update();
                    }
                    processed = processed + 1;
                }
                j = j + 1;
                off = off + 4;
            }
            if (processed != 0) {
                toggle_latch_update(0);
            }
        }
    }
    __restore_gpr_28();
}

// ----------------------------------------------------------------------------
// 0x802f3a28  arcsel_mark_all_dirty
// Marks every active screen dirty (flag 0x40) so it re-renders next frame.
// ----------------------------------------------------------------------------
void arcsel_mark_all_dirty(void) {
    int off = 0;
    FUN_803826f0();
    for (int i = 0; i < session_get_state_flag(); i = i + 1) {
        uint *screen = *(uint **)((int)&DAT_805cf280 + off);
        off = off + 4;
        *screen = *screen | 0x40;
    }
    FUN_8038273c();
}
