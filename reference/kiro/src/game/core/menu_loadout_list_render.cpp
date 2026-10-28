// menu_loadout_list_render.cpp - loadout list population + rendering.
//
// This batch covers the back half of the loadout/gamemode-select screen: it
// resolves the per-slot option table, populates the DAT_805cc744.. staging
// arrays, refreshes the on-screen child widgets, and draws each list row
// (label, team-colour swatch, selection cursor). Several helpers here are the
// GX/text draw callbacks invoked per widget by the HUD layout engine.
//
//   menu_slot_option_table_resolve   0x802ca6cc - resolve option-table ptr for a slot.
//   menu_loadout_populate_staging    0x802ca7b8 - fill DAT_805cc744.. staging arrays.
//   menu_loadout_resolve_weapon_idx  0x802ca910 - weapon_def_find_index per staged slot.
//   menu_loadout_refresh_children    0x802ca970 - enable/disable child widgets per slot.
//   menu_loadout_draw_scissored_icon 0x802caa90 - draw a sprite clipped to a scissor rect.
//   menu_loadout_item_render         0x802cab70 - draw one loadout list row (label+bg).
//   menu_loadout_swatch_render       0x802caf70 - draw the team-colour swatch for a row.
//   menu_loadout_team_cycle          0x802cb18c - advance the team index for a slot.
//   menu_loadout_cursor_render       0x802cb200 - draw the selection cursor markers.
//   menu_loadout_can_edit_teams      0x802cb700 - predicate: teams editable this mode.
//
// Address range: 0x802ca6cc - 0x802cb753
// Batch: batch_804

#include "common.h"

// ---- frame register-pair accessors (compiler-emitted; non-matching) -------
// The HUD layout engine hands each draw callback a register pair: the low
// 32 bits are the row/item index, the high 32 bits are the widget context
// pointer. Modeled as u64-returning file-local externs and split at the call
// site. FUN_803825c0 is the float->fixed scale helper (double in, int out).
extern "C" {
    extern unsigned long long FUN_803826c4(void); // item render ctx pair
    extern unsigned long long FUN_803826d8(void); // swatch render ctx pair
    extern unsigned long long FUN_803826f0(void); // cursor render ctx pair
    extern int  FUN_803826e8(void);               // active slot index (populate)
    extern int  FUN_803825c0(double v);           // float -> fixed scale
    extern void FUN_80382734(void);               // populate epilogue barrier
    extern void FUN_80382710(void);               // item render epilogue barrier
    extern void FUN_80382724(void);               // swatch render epilogue barrier
    extern void FUN_8038273c(void);               // cursor render epilogue barrier
    extern int  hud_screen_current_element(void); // __save_gpr_25 -> current screen node
}

// ---- named cross-module helpers (declared per-file; common.h omits decls) --
extern "C" {
    extern void* level_get_info_by_id(int level_id);
    extern unsigned int weapon_type_is_projectile(int type);
    extern int  weapon_def_find_index(int type, int subtype);
    extern int  weapon_def_get_hud_icon(unsigned int index);
    extern int  hud_screen_node_is_valid(void);
    extern int* hud_screen_find_child_by_id(int element, int target_id);
    extern int* hud_find_nth_child_by_id(int element, int target_id, int occurrence);
    extern int  player_ai_is_story_mode(int game_mode);
    extern int  menu_slot_is_extra_bot(int slot);
    extern int  mode_feature_enabled_check(void);
    extern int  mode_team_count(void);
    extern int  mode_variant_count_remaining(void);
    extern void* game_get_settings_ptr(void);

    extern int* menu_slot_get_weapon_setting(int slot);
    extern int* menu_map_option_table_build(void);
    extern int  menu_get_child_slot_index(int element);
    extern int  menu_get_active_slot_index(void);
    extern void menu_settings_pull_defaults(void);
    extern void menu_settings_push_defaults(void);

    extern void text_set_font_table(int table);
    extern unsigned int gx_set_sub_material_id(int id);
    extern void sprite_draw_gradient_quad(float x1, float y1, float x2, float y2,
                                          unsigned int base_color, unsigned int a_top,
                                          unsigned int a_bottom);
    extern void sprite_draw_gradient_quad_alpha(float x1, float y1, float x2, float y2,
                                                unsigned int base_color, unsigned int a_top,
                                                unsigned int a_bottom);
    extern int  get_string_by_id(int id);
    extern unsigned char* text_string_to_upper(unsigned int str);
    extern void text_apply_alpha_fade(double x, unsigned char* str, int a, int b);
    extern void text_draw_char_shadowed(double x, double y, unsigned char* str,
                                        unsigned int color, unsigned int shadow);
    extern void text_set_color_rgb(double r, double g, double b);
    extern int* hud_get_focused_item(int element);
    extern void hud_draw_pulsing_box(int x0, int x1, int y0, int y1);
    extern void gx_get_scissor_rect(int* x0, int* y0, int* x1, int* y1);
    extern void gx_set_scissor_rect(unsigned int a, unsigned int b, int x1, int y1);
    extern unsigned int sprite_get_texture_id(int index);
    extern void hud_sprite_draw(void);
    extern void color_scale_lo(double scale, unsigned int color);
    extern void gx_set_active_material(int index);
    extern void gx_draw_sprite_rect(unsigned int a, unsigned int b, unsigned int x1i,
                                    unsigned int y1i, unsigned int u0, unsigned int v0,
                                    unsigned int u1, unsigned int v1);
    extern int  hud_screen_all_children_closing(int screen);
    extern void cursor_draw_at_size(double y_pos);
}

// ---- raw data symbols -----------------------------------------------------
extern int   DAT_805cc614;      // active gamemode id (secondary copy)
extern int   DAT_80612614;      // active gamemode id (primary; 0x66 == online)
extern char  DAT_805cc68c;      // current level/map id byte
extern char  DAT_805cc68d;      // submode selector byte
extern unsigned char DAT_805cc68e; // gamemode-family selector byte (0x13 == story)
extern unsigned char DAT_8048f97c; // static per-slot option table base
extern int   DAT_80612244;      // input-repeat / two-column layout flag
extern int   DAT_80612af8;      // last-populated active slot index
extern int   DAT_80612afc;      // current active slot index
extern int   DAT_8048dbe8;      // "extra bot" label string id
extern int*  DAT_80610a20;      // active material/texture table base ptr

// Staging arrays (stride 0x1c per slot). Declared as int; accessed via the
// Ghidra-style (&DAT_x)[i*7] pattern (7 ints == 0x1c bytes).
extern int   DAT_805cc744;      // slot weapon-type column
extern int   DAT_805cc748;      // slot team/colour column
extern int   DAT_805cc754;      // slot flag column
extern int   DAT_805cc758;      // slot resolved weapon-def index column
extern int   DAT_805cc75c;      // slot subtype column

extern int   DAT_803f2c90;      // team colour table (stride 5 ints, base)
extern int   DAT_803f2c94;      // team colour table: texture-id column

extern const float FLOAT_80611674;
extern const float FLOAT_80611a68;
extern const float FLOAT_80619eec;
extern const float FLOAT_80619ee8;
extern const float FLOAT_80619f0c;
extern const float FLOAT_80619f38;
extern const float FLOAT_80619f58;
extern const float FLOAT_80619f6c;
extern const float FLOAT_80619f84;
extern const float FLOAT_80619fac;
extern const float FLOAT_80619fb0;
extern const float FLOAT_80619fb4;
extern const float FLOAT_80619fb8;
extern const float FLOAT_80619fbc;
extern const float FLOAT_80619fc0;
extern const float FLOAT_80619fc4;
extern const float FLOAT_80619fc8;
extern const float FLOAT_80619fcc;
extern const float FLOAT_80619fd0;
extern const double DOUBLE_80619f00;

// ---- 0x802ca6cc: resolve option-table pointer for a slot ------------------
int* menu_slot_option_table_resolve(int slot)
{
    if (slot == 0)
        return (int*)0;

    if (slot != 1)
    {
        if (slot < 0xc)
            return (int*)(&DAT_8048f97c + (slot + 0x1b) * 0xa4);
        return menu_slot_get_weapon_setting(slot);
    }

    bool useMapTable = false;
    if (DAT_805cc614 != 10)
    {
        if (DAT_805cc614 < 10)
        {
            if (DAT_805cc614 < 9)
                goto done_flag;
        }
        else if (DAT_805cc614 > 0xd || DAT_805cc68c != 'd')
        {
            goto done_flag;
        }
        useMapTable = true;
    }
done_flag:
    if (useMapTable)
        return menu_map_option_table_build();

    int info = (int)level_get_info_by_id((int)DAT_805cc68c);
    if (info == 0)
        info = (int)level_get_info_by_id(0x30);
    return (int*)(&DAT_8048f97c + *(int*)(info + 8) * 0xa4);
}

// ---- 0x802ca7b8: fill the staging arrays from the option table ------------
void menu_loadout_populate_staging(void)
{
    int slot = FUN_803826e8();
    int* table = menu_slot_option_table_resolve(slot);

    if (slot < 1)
    {
        int off = 0;
        for (int i = 0; i < 10; ++i)
        {
            *(int*)((char*)&DAT_805cc754 + off) = 0xffffffff;
            *(int*)((char*)&DAT_805cc744 + off) = 0xffffffff;
            off += 0x1c;
        }
    }
    else
    {
        int src = 0;
        int off = 0;
        for (int i = 0; i < 10; ++i)
        {
            int type = (int)*(short*)((char*)table + src + 4);
            if (type < 0)
            {
                *(int*)((char*)&DAT_805cc754 + off) = 0xffffffff;
                *(int*)((char*)&DAT_805cc744 + off) = 0xffffffff;
            }
            else
            {
                if (weapon_type_is_projectile(type) == 0)
                    *(int*)((char*)&DAT_805cc75c + off) = 0xffffffff;
                else
                    *(int*)((char*)&DAT_805cc75c + off) =
                        (int)*(short*)((char*)table + src + 10);
                *(int*)((char*)&DAT_805cc754 + off) = 0xffffffff;
                *(int*)((char*)&DAT_805cc744 + off) =
                    (int)*(short*)((char*)table + src + 4);
            }
            src += 8;
            off += 0x1c;
        }
    }

    int remaining = mode_variant_count_remaining();
    int off = remaining * 0x1c;
    for (int i = remaining; i < 10; ++i)
    {
        *(int*)((char*)&DAT_805cc754 + off) = 0;
        *(int*)((char*)&DAT_805cc744 + off) = 0xffffffff;
        off += 0x1c;
    }

    menu_settings_pull_defaults();
    FUN_80382734();
}

// ---- 0x802ca910: resolve weapon-def index for each staged slot ------------
void menu_loadout_resolve_weapon_idx(void)
{
    int off = 0;
    for (int i = 0; i < 10; ++i)
    {
        int idx = weapon_def_find_index(*(int*)((char*)&DAT_805cc744 + off),
                                        *(int*)((char*)&DAT_805cc75c + off));
        *(int*)((char*)&DAT_805cc758 + off) = idx;
        off += 0x1c;
    }
}

// ---- 0x802ca970: enable/disable child widgets for the active slot ---------
void menu_loadout_refresh_children(void)
{
    int element = hud_screen_current_element();
    if (hud_screen_node_is_valid() == 0)
        return;

    element = (int)hud_screen_find_child_by_id(element, -0x7fb89c30);
    int slotIdx = menu_get_child_slot_index(element);

    char* settings = (char*)game_get_settings_ptr();
    *(int*)(settings + 0x6a8) = slotIdx;
    settings = (char*)game_get_settings_ptr();
    DAT_80612afc = *(int*)(settings + 0x6a8);

    if (DAT_80612afc != DAT_80612af8)
    {
        menu_get_active_slot_index();
        menu_loadout_populate_staging();
        menu_get_active_slot_index();
        menu_loadout_resolve_weapon_idx();
    }
    DAT_80612af8 = DAT_80612afc;

    int staged = 0;
    for (int row = 0; row < 10; ++row)
    {
        for (int col = 1; col < 4; ++col)
        {
            int child = (int)hud_find_nth_child_by_id(element, row, col);
            if (child != 0)
            {
                *(unsigned int*)(child + 0x28) &= 0xfffffffb;
                if (*(int*)((char*)&DAT_805cc744 + staged) < 0 ||
                    (DAT_805cc68e == '\x13' &&
                     player_ai_is_story_mode(*(int*)((char*)&DAT_805cc744 + staged)) != 0))
                {
                    *(unsigned int*)(child + 0x28) |= 4;
                }
            }
        }
        staged += 0x1c;
    }

    menu_settings_push_defaults();
}

// ---- 0x802caa90: draw a sprite clipped to a scissor rect ------------------
void menu_loadout_draw_scissored_icon(double scale, float* rect)
{
    int sx0, sy0, sx1, sy1;
    gx_get_scissor_rect(&sx0, &sy0, &sx1, &sy1);

    gx_set_scissor_rect((unsigned int)(int)rect[0], (unsigned int)(int)rect[1],
                        (int)rect[2], (int)rect[3]);
    FUN_803825c0((double)(float)((double)FLOAT_80619fac * scale));
    sprite_get_texture_id(0x140);
    hud_sprite_draw();

    gx_set_scissor_rect((unsigned int)sx0, (unsigned int)sy0, sx1, sy1);
}

// ---- 0x802cab70: draw one loadout list row (background + label) -----------
void menu_loadout_item_render(unsigned int a, unsigned int b, float* rect)
{
    unsigned long long pair = FUN_803826c4();
    int ctx = (int)(pair >> 0x20);
    int self = (int)pair;
    unsigned int bgColor = 0x23374b00;

    float x0 = FLOAT_80619fb0 + rect[0];
    float y0 = FLOAT_80619f38 + rect[1];
    float y1 = rect[3] - FLOAT_80619f38;
    double scale = (double)*(float*)(ctx + 0xf0);
    unsigned int w = (unsigned int)(rect[2] - FLOAT_80619f38);
    int slot = *(int*)(self + 0x14);

    int alphaTop = FUN_803825c0((double)(float)((double)FLOAT_80619f0c * scale));
    int alphaBot = FUN_803825c0((double)(float)((double)FLOAT_80619f58 * scale));

    int extraBot = 0;
    if (DAT_80612614 != 0x66)
        extraBot = menu_slot_is_extra_bot(slot);

    text_set_font_table(*(int*)(*(int*)(ctx + 0x1c) + 0x4c));
    gx_set_sub_material_id(1);

    if (DAT_80612614 != 0x66)
    {
        char* settings = (char*)game_get_settings_ptr();
        if (*(int*)(settings + 0x6a8) == 0 || mode_variant_count_remaining() <= slot)
        {
            bgColor = 0x1e1e1e00;
        }
        else if (extraBot == 0)
        {
            if (DAT_805cc68e == '\x13' &&
                player_ai_is_story_mode((&DAT_805cc744)[slot * 7]) != 0)
            {
                bgColor = 0x1e1e1e00;
            }
            else if ((&DAT_805cc744)[slot * 7] >= 0 && mode_feature_enabled_check() != 0)
            {
                int teams = mode_team_count();
                int team = (&DAT_805cc748)[slot * 7] - ((&DAT_805cc748)[slot * 7] / teams) * teams;
                bgColor = (&DAT_803f2c90)[team * 5];
            }
        }
        else
        {
            bgColor = 0x7f642d00;
        }
    }

    unsigned int bgAlpha = (unsigned int)(FLOAT_80619fb4 * *(float*)(ctx + 0xf0));
    sprite_draw_gradient_quad(x0, y0, (float)w, y1, bgColor, 5, bgAlpha);

    if (((&DAT_805cc744)[slot * 7] >= 0 && slot < mode_variant_count_remaining()) || extraBot != 0)
    {
        unsigned char* label;
        if (extraBot == 0)
        {
            int icon = weapon_def_get_hud_icon((&DAT_805cc758)[slot * 7]);
            label = (unsigned char*)get_string_by_id(icon);
        }
        else
        {
            label = (unsigned char*)get_string_by_id((int)DAT_8048dbe8);
        }
        if (DAT_80612614 != 0x66)
            label = text_string_to_upper((unsigned int)label);

        text_apply_alpha_fade((double)((float)w - (FLOAT_80619fb8 + rect[0])), label, 0, 1);
        text_draw_char_shadowed((double)(FLOAT_80619fbc + rect[0]),
                                (double)(rect[1] + (float)(int)bgAlpha),
                                label, (alphaTop & 0xff) | 0x6e6e6e00,
                                (alphaBot & 0xff) | 0x2000);

        if (extraBot != 0)
        {
            float box[4];
            box[0] = -(FLOAT_80619fc0 * FLOAT_80611a68 -
                       (FLOAT_80619eec * ((*(float*)(ctx + 0xa4) + *(float*)(ctx + 0xac)) - rect[2])
                        + rect[2]));
            box[2] = FLOAT_80619f6c * FLOAT_80611a68 + box[0];
            box[1] = rect[1];
            box[3] = rect[3];
            menu_loadout_draw_scissored_icon((double)*(float*)(ctx + 0xf0), box);
        }
        double c = (double)FLOAT_80619ee8;
        text_set_color_rgb(c, c, c);
    }

    if ((int)hud_get_focused_item(ctx) == self)
    {
        hud_draw_pulsing_box((int)rect[0], (int)(rect[2] - FLOAT_80619f38),
                             (int)rect[1], (int)rect[3]);
    }

    FUN_80382710();
}

// ---- 0x802caf70: draw the team-colour swatch for a row --------------------
void menu_loadout_swatch_render(unsigned int a, unsigned int b, float* rect)
{
    unsigned long long pair = FUN_803826d8();
    int ctx = (int)(pair >> 0x20);
    int slot = *(int*)((int)pair + 0x14);

    gx_set_sub_material_id(1);

    if ((&DAT_805cc744)[slot * 7] >= 0 && slot < mode_variant_count_remaining())
    {
        float x0 = FLOAT_80619fc4 + rect[0];
        float x1 = rect[2] - FLOAT_80619fc4;
        float y0 = FLOAT_80619f38 + rect[1];
        float y1 = rect[3] - FLOAT_80619f38;
        int teams = mode_team_count();
        int team = (&DAT_805cc748)[slot * 7] - ((&DAT_805cc748)[slot * 7] / teams) * teams;

        if ((&DAT_803f2c94)[team * 5] == -1)
        {
            sprite_draw_gradient_quad_alpha(x0, y0, x1, y1, (&DAT_803f2c90)[team * 5],
                                            (unsigned int)(FLOAT_80619f0c * *(float*)(ctx + 0xf0)),
                                            (unsigned int)(FLOAT_80619f58 * *(float*)(ctx + 0xf0)));
        }
        else
        {
            int tex = sprite_get_texture_id((&DAT_803f2c94)[team * 5]);
            color_scale_lo((double)*(float*)(ctx + 0xf0), (&DAT_803f2c90)[team * 5]);
            gx_set_active_material(tex);
            int mat = *DAT_80610a20 + tex * 0x58;
            gx_draw_sprite_rect((unsigned int)(int)x0, (unsigned int)(int)y0,
                                (unsigned int)(int)x1, (unsigned int)(int)y1, 0, 0,
                                (unsigned int)*(unsigned short*)(mat + 0x4e) << 4,
                                (unsigned int)*(unsigned short*)(mat + 0x52) << 4);
        }

        if (hud_screen_all_children_closing(ctx) != 0 &&
            (int)hud_get_focused_item(ctx) == (int)pair)
        {
            hud_draw_pulsing_box((int)rect[0], (int)(rect[2] - FLOAT_80619f38),
                                 (int)rect[1], (int)rect[3]);
        }
    }

    FUN_80382724();
}

// ---- 0x802cb18c: advance the team index for a slot on input ---------------
void menu_loadout_team_cycle(int widget)
{
    int slot = *(int*)(*(int*)(widget + 0x194) + 0x14);
    if ((&DAT_805cc744)[slot * 7] >= 0 && (&DAT_805cc754)[slot * 7] == -1)
    {
        int teams = mode_team_count();
        int next = (&DAT_805cc748)[slot * 7] + 1;
        (&DAT_805cc748)[slot * 7] = next - (next / teams) * teams;
    }
}

// ---- 0x802cb200: draw the selection cursor markers ------------------------
// The number/placement of cursor markers depends on the widget's shape code
// (0..4) returned in the low half of the context pair. Layout uses a
// two-column offset when DAT_80612244 == 0.
void menu_loadout_cursor_render(unsigned int a, unsigned int b, float* rect)
{
    unsigned long long pair = FUN_803826f0();
    int shape = (int)pair;
    int ctx = (int)(pair >> 0x20);

    FUN_803825c0((double)(FLOAT_80619f0c * *(float*)(ctx + 0xf0)));

    int bias = (DAT_80612244 == 0) ? 1 : 0; // (DAT==0) subtracts one row
    double base = (double)rect[1] - DOUBLE_80619f00;

    if (shape == 2)
    {
        cursor_draw_at_size((double)(FLOAT_80619f38 + rect[0]));
        cursor_draw_at_size((double)(FLOAT_80619fc8 + rect[0]));
        cursor_draw_at_size((double)(FLOAT_80619fd0 + rect[0]));
    }
    else if (shape < 2)
    {
        if (shape == 0)
        {
            cursor_draw_at_size((double)(FLOAT_80619fc8 + rect[0]));
        }
        else if (shape >= 0)
        {
            cursor_draw_at_size((double)(FLOAT_80619f84 + rect[0]));
            cursor_draw_at_size((double)(FLOAT_80619fcc + rect[0]));
        }
    }
    else if (shape == 4)
    {
        cursor_draw_at_size((double)(FLOAT_80619f38 + rect[0]));
        cursor_draw_at_size((double)(FLOAT_80619f38 + rect[0]));
        cursor_draw_at_size((double)(FLOAT_80619fcc + rect[0]));
        cursor_draw_at_size((double)(FLOAT_80619fcc + rect[0]));
        cursor_draw_at_size((double)(FLOAT_80619fc8 + rect[0]));
    }
    else if (shape < 4)
    {
        cursor_draw_at_size((double)(FLOAT_80619f38 + rect[0]));
        cursor_draw_at_size((double)(FLOAT_80619f38 + rect[0]));
        cursor_draw_at_size((double)(FLOAT_80619fcc + rect[0]));
        cursor_draw_at_size((double)(FLOAT_80619fcc + rect[0]));
    }
    (void)base;
    (void)bias;

    FUN_8038273c();
}

// ---- 0x802cb700: predicate - are teams editable in this mode? -------------
int menu_loadout_can_edit_teams(void)
{
    char* settings = (char*)game_get_settings_ptr();
    if (*(int*)(settings + 0x10) != 0 && DAT_805cc68d != '\x05')
        return 1;
    return 0;
}
