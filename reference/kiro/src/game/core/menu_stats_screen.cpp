// Address: 0x802db898 | Name: menu_stats_screen | Date: 2026-09-11T08:20:00Z
//
// Front-end player-statistics / profile-stats screens (batch_824), continuation
// of the menu family from batch_817-823. Contains:
//   - the difficulty-completion summary render (easy/normal/hard columns with
//     mission counters, plus awards/weapons/characters/completion totals),
//   - the player-select screen (pick which profile's stats to view) with its
//     per-frame refresh and pop handlers,
//   - the detailed per-player stats text formatter (favourite weapon, K/D,
//     accuracy, time played, mode records), and
//   - the stats detail table render + its screen builder.
//
// These are render/formatter callbacks driven by the HUD screen system. The
// heavy floating-point expressions are the PowerPC int->float "magic bias"
// idiom (CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE_80619f00); kept verbatim
// as a non-matching translation of the text-placement arithmetic.
//
// Address range: 0x802db898 - 0x802dd73c

#include "common.h"

// ---- Per-file externs -------------------------------------------------------

extern int    DAT_805cc61c;       // active savegame slot / selected player
extern int    DAT_80611014;       // active player count
extern int    DAT_8060f948;       // language / region selector
extern int    DAT_806109c0;       // record-scale reference
extern int   *DAT_806119e0;       // layout centering context

extern float  FLOAT_80611674;     // row height
extern float  FLOAT_80611a64;
extern float  FLOAT_80611a68;
extern float  FLOAT_8060f958;
extern float  FLOAT_80611a5c;
extern float  FLOAT_80619eec;
extern float  FLOAT_80619ee8;
extern float  FLOAT_80619f0c;
extern float  FLOAT_80619f38;
extern float  FLOAT_80619f08;
extern float  FLOAT_80619f28;
extern float  FLOAT_80619f58;
extern float  FLOAT_80619f60;
extern float  FLOAT_80619f88;
extern float  FLOAT_80619fac;
extern float  FLOAT_80619fc4;
extern float  FLOAT_80619fe0;
extern float  FLOAT_80619ff8;
extern float  FLOAT_8061a010;
extern float  FLOAT_8061a024;
extern float  FLOAT_8061a04c;
extern float  FLOAT_8061a064;
extern float  FLOAT_8061a06c;
extern float  FLOAT_8061a070;
extern float  FLOAT_8061a110;
extern float  FLOAT_8061a140;
extern float  FLOAT_8061a144;
extern double DOUBLE_80619f00;

// Format string / label symbols (raw data addresses used by the varargs logger).
extern u8     DAT_80611058;
extern u8     DAT_8061105c;
extern u8     DAT_80611064;
extern u8     DAT_8061106c;
extern u8     DAT_80611074;   // s__d__s_80611074 style fmt
extern u8     DAT_8061107c;
extern u8     DAT_80611084;
extern u8     DAT_8061108c;
extern u8     s__d__s_80611074;
extern u8     s__d__s_8061107c;
extern u8     DAT_803f291a;   // award short-label table (stride 0x44)
extern u8     DAT_80476640;   // stats-row descriptor table (stride 0x88)
extern u8     DAT_80476648;   // stats-row value strings (stride 0x88)
extern int    DAT_80476e40;
extern int    DAT_80476ec8;
extern int    DAT_80476758;
extern int    DAT_804766d0;
extern int    DAT_80476b10;
extern int    DAT_80476868;
extern int    DAT_80476db8;
extern int    DAT_80477060;

// Screen vtable pointer used by the pop handler to detect the parent screen.
extern void  *PTR_menu_cheats_options_screen_build_80476600;

// Frame-actor scope thunks.
extern int      FUN_803826b4(void);
extern u64      FUN_803826c0(void);   // returns (float* ptr, hi) pair
extern int      FUN_803826dc(void);
extern uint     FUN_803825c0(double v);
extern void     FUN_80382700(void);
extern void     FUN_8038270c(void);
extern void     FUN_80382728(void);

// Text / string-builder helpers.
extern u64   FUN_8038b510(int *dst, int *src);
extern u64   FUN_8038b4a0(int dst, int src);
extern u64   FUN_80387fb8(...);
extern u64   debug_log_formatted(...);

// HUD builders.
extern int   get_string_by_id(int id);
extern int   hud_element_is_closing(int element);
extern int   hud_find_child_by_id(int element, int id);
extern void  hud_screen_set_handler_param_b(int element, int slot, int value);
extern int   hud_screen_pop_with_restore(void);
extern void  hud_element_init_from_layout(int flags, int titleStr, int a, void *callback);
extern void  hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void  hud_set_context_property(int prop);
extern void  hud_add_list_item(void);
extern void  hud_add_callback_item_default(int a);
extern void  hud_item_add_spacer(double h, int flags);
extern void  hud_add_spacer_item(double h, int flags, void *callback);
extern int   player_get_entity_ptr(int index);
extern void  hud_event_add_string(int str);
extern int   hud_get_last_created_item(void);
extern void  hud_item_on_value_change(void);
extern void  hud_element_apply_center_xy(int *ctx);
extern void  hud_screen_push(void);
extern void  hud_screen_register_handler_global(int slot, int str, int a, int b);
extern void  hud_marker_draw(int a, int x, int y, int h, int w);

// Text render / measure.
extern u64   text_get_char_width(byte *str);
extern u64   text_draw_char(double x, double y, byte *str, uint color);
extern void  text_draw_char_drop_shadow(double x, double y, byte *str, uint color, uint shadow);
extern void  text_set_font_table(int table);
extern byte *text_string_to_upper(uint str);
extern void  text_apply_alpha(double a, byte *str, int b);
extern void  text_set_color_rgb(double r, double g, double b);
extern void  time_format_long(double a, double b, double c, double d, double e, u64 f, u64 g, u64 h);

// GX / cursor.
extern uint  color_scale_lo(double a, int color);
extern uint  color_fade_team_b(double a, int team);
extern void  gx_set_scissor_rect(int x, int y, int w, int h);
extern void  gx_set_sub_material_id(int id);
extern ushort cursor_get_width(int id);
extern void  cursor_draw_at_size(double x, double y);

// Stats accessors.
extern u64   stats_compute_player_completion(void);
extern int   stats_iterate_awards_wrapper(void);
extern int   stats_iterate_awards_wrapper2(void);
extern int   stats_get_total_awards(void);
extern int   stats_compute_wrapper(void);
extern int   stats_compute_awards_wrapper2(void);
extern int   stats_get_total_weapons(void);
extern int   stats_count_unlocked_weapons(int slot);
extern int   stats_get_total_chars(void);
extern int   stats_count_unlocked_chars(int slot);
extern u8   *stats_get_player_ptr(int slot);
extern u8   *stats_get_award_record(int slot, int index);
extern int   weapon_def_get_hud_icon(int index);
extern int   weapon_def_find_index(int id, int a);
extern int   award_grant_to_combined(int a, int b, int c);
extern void  worm_stat_scale_time(double t, float *out);

// Detailed formatter/render forward decls (defined below, referenced by address).
void menu_stats_format_detail(u64 param_1, double param_2, double param_3, u64 param_4,
                              u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                              int param_9, int param_10, int param_11, u8 *param_12,
                              u8 *param_13, int param_14, int param_15, int param_16);
void menu_stats_summary_render(int param_1, int param_2, float *param_3, int param_4,
                               int param_5, int param_6, int param_7, int param_8);
void menu_stats_detail_table_render(int param_1, int param_2, int param_3);
void menu_stats_detail_table_render_active(int param_1, int param_2, int param_3);

// ---------------------------------------------------------------------------

// 0x802db898 - Push the detailed player-stats screen for the active slot.
void menu_stats_push_detail(u64 param_1, double param_2, double param_3, u64 param_4,
                            u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                            int param_9, int param_10, int param_11, u8 *param_12,
                            u8 *param_13, int param_14, int param_15, int param_16)
{
    menu_stats_format_detail(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             DAT_805cc61c, param_10, param_11, param_12, param_13,
                             param_14, param_15, param_16);
    hud_screen_push();
}

// 0x802db8d4 - Push the current screen (thin wrapper).
void menu_stats_screen_push(void)
{
    hud_screen_push();
}

// 0x802db900 - Render the difficulty-completion summary: three columns (easy
// 0x3ce / normal 0x3cf / hard 0x3d0) of mission counters, plus awards, weapons,
// characters, and overall completion totals. Column values come from the
// stats_iterate_awards_wrapper* iterators; unlocked entries render in the bright
// colour (0x7878787f) versus dim (0x2828287f).
void menu_stats_summary_render(int param_1, int param_2, float *param_3, int param_4,
                               int param_5, int param_6, int param_7, int param_8)
{
    // Incoming paired-single FP spill registers modelled as locals (non-matching).
    double in_f7 = 0, in_f8 = 0;
    u64 extraout_f1 = 0, extraout_f1_00 = 0;
    int extraout_r4 = 0;

    int element = FUN_803826b4();
    float *layout = *(float **)(element + 0x24);
    uint colX = (uint)(*layout * FLOAT_80611a68 + *param_3);
    uint colYneg = (uint)-(layout[2] * FLOAT_80611a68 - param_3[2]);
    uint rowY = (uint)(param_3[1] + FLOAT_80611a64);
    u64 completion = stats_compute_player_completion();
    int rowH = (int)FLOAT_80611674;

    uint colBright = color_scale_lo((double)*(float *)(element + 0xf0), 0x7878787f);
    uint colDim    = color_scale_lo((double)*(float *)(element + 0xf0), 0x2828287f);

    int easyDone   = stats_iterate_awards_wrapper();
    int normalDone = stats_iterate_awards_wrapper();
    int hardDone   = stats_iterate_awards_wrapper();
    int easyTotal   = stats_iterate_awards_wrapper2();
    int normalTotal = stats_iterate_awards_wrapper2();
    int hardTotal   = stats_iterate_awards_wrapper2();

    // The original emits a long sequence of magic-bias float conversions to place
    // each label/value. The layout math is preserved conceptually: three
    // difficulty rows, each with a "done / total" pair coloured by unlock state,
    // followed by award/weapon/character counters and the completion percentage.
    byte *s;
    double w;
    uint color;

    s = (byte *)get_string_by_id(0x3cf);
    text_get_char_width(s);

    // Difficulty column headers + per-row counters.
    color = (easyDone != 0 || easyTotal != 0) ? colBright : colDim;
    s = (byte *)get_string_by_id(0x3ce);
    w = (double)text_get_char_width(s);
    (void)w;
    text_draw_char((double)(int)colX, (double)(int)rowY, s, color);

    color = (easyDone != 0) ? colBright : colDim;
    s = (byte *)get_string_by_id(0x3ce);
    debug_log_formatted(text_draw_char((double)(int)colX, (double)(int)rowY, s, color),
                        (double)(int)rowY, 0, DOUBLE_80619f00, (double)FLOAT_80619eec,
                        (double)FLOAT_80619ff8, in_f7, in_f8,
                        &DAT_80611058, easyDone, 0, param_4, param_5, param_6, param_7, param_8);

    color = (normalDone != 0 || normalTotal != 0) ? colBright : colDim;
    s = (byte *)get_string_by_id(0x3cf);
    debug_log_formatted(text_draw_char((double)(int)colX, (double)(int)(rowY + rowH), s, color),
                        (double)(int)(rowY + rowH), 0, DOUBLE_80619f00, (double)FLOAT_80619eec,
                        (double)FLOAT_8061a110, in_f7, in_f8,
                        &DAT_80611058, normalDone, 0, param_4, param_5, param_6, param_7, param_8);

    color = (hardDone != 0 || hardTotal != 0) ? colBright : colDim;
    s = (byte *)get_string_by_id(0x3d0);
    debug_log_formatted(text_draw_char((double)(int)colX, (double)(int)(rowY + rowH * 2), s, color),
                        (double)(int)(rowY + rowH * 2), 0, DOUBLE_80619f00, (double)FLOAT_80619eec,
                        (double)FLOAT_80619f28, in_f7, in_f8,
                        &DAT_80611058, hardDone, 0, param_4, param_5, param_6, param_7, param_8);

    // Award total row (with cursor glyphs) + completion counters.
    uint rowY2 = rowY + rowH;
    ushort cw = cursor_get_width(0x16);
    (void)cw;
    s = (byte *)get_string_by_id(0x3fc);
    w = (double)text_get_char_width(s);
    uint cursorX = (uint)(((double)(int)colX + w));
    FUN_803825c0((double)(FLOAT_80619f58 * *(float *)(element + 0xf0)));
    cursor_draw_at_size((double)(int)(cursorX + 3), (double)(int)(rowY2 + 5));
    cursor_draw_at_size((double)(int)cursorX, (double)(int)(rowY2 + 4));

    int totalAwards = stats_get_total_awards();
    int unlockedAwards = stats_compute_wrapper();
    s = (byte *)debug_log_formatted(extraout_f1, (double)(int)(rowY2 + 4), (double)(int)cursorX, DOUBLE_80619f00,
                                    (double)FLOAT_80619eec, (double)FLOAT_80619ff8, in_f7, in_f8,
                                    &DAT_8061105c, unlockedAwards, (u64)totalAwards, cursorX + 3,
                                    param_5, param_6, param_7, param_8);
    w = (double)text_get_char_width(s);
    text_draw_char((double)(int)colYneg, (double)(int)rowY2, s, colBright);

    rowY2 += rowH;
    FUN_803825c0((double)(FLOAT_80619f58 * *(float *)(element + 0xf0)));
    cursor_draw_at_size((double)(int)(cursorX + 3), (double)(int)(rowY2 + 5));
    cursor_draw_at_size((double)(int)cursorX, (double)(int)(rowY2 + 4));
    FUN_803825c0((double)(FLOAT_80619f58 * *(float *)(element + 0xf0)));
    cursor_draw_at_size((double)(int)(cursorX - 0xb + 3), (double)(int)(rowY2 + 5));
    cursor_draw_at_size((double)(int)(cursorX - 0xe), (double)(int)(rowY2 + 4));

    totalAwards = stats_get_total_awards();
    unlockedAwards = stats_compute_awards_wrapper2();
    s = (byte *)debug_log_formatted(extraout_f1_00, (double)(int)(rowY2 + 4), (double)(int)cursorX, DOUBLE_80619f00,
                                    (double)FLOAT_80619eec, (double)FLOAT_80619ff8, in_f7, in_f8,
                                    &DAT_8061105c, unlockedAwards, (u64)totalAwards, cursorX - 0xb,
                                    param_5, param_6, param_7, param_8);
    w = (double)text_get_char_width(s);
    text_draw_char((double)(int)colYneg, (double)(int)rowY2, s, colBright);

    // Weapons unlocked.
    rowY2 += rowH;
    s = (byte *)get_string_by_id(0x3fa);
    text_get_char_width(s);
    int totalWeapons = stats_get_total_weapons();
    int unlockedWeapons = stats_count_unlocked_weapons(DAT_805cc61c);
    s = (byte *)debug_log_formatted(text_draw_char((double)(int)colYneg, (double)(int)rowY2, s, colBright),
                                    (double)(int)rowY2, DOUBLE_80619f00, (double)FLOAT_8061a010,
                                    (double)FLOAT_80619eec, (double)FLOAT_80619ff8, in_f7, in_f8,
                                    &DAT_8061105c, unlockedWeapons, (u64)totalWeapons, cursorX - 0xb,
                                    param_5, param_6, param_7, param_8);
    text_draw_char((double)(int)colYneg, (double)(int)rowY2, s, colBright);

    // Characters unlocked.
    rowY2 += rowH;
    s = (byte *)get_string_by_id(0x3fb);
    text_get_char_width(s);
    int totalChars = stats_get_total_chars();
    int unlockedChars = stats_count_unlocked_chars(DAT_805cc61c);
    s = (byte *)debug_log_formatted(text_draw_char((double)(int)colYneg, (double)(int)rowY2, s, colBright),
                                    (double)(int)rowY2, DOUBLE_80619f00, (double)FLOAT_8061a010,
                                    (double)FLOAT_80619eec, (double)FLOAT_80619ff8, in_f7, in_f8,
                                    &DAT_8061105c, unlockedChars, (u64)totalChars, cursorX - 0xb,
                                    param_5, param_6, param_7, param_8);
    text_draw_char((double)(int)colYneg, (double)(int)rowY2, s, colBright);

    // Overall completion percentage.
    s = (byte *)get_string_by_id(0x3f9);
    text_get_char_width(s);
    text_draw_char((double)(int)colYneg, (double)(int)(rowY2 + rowH), s, colBright);
    s = (byte *)debug_log_formatted(completion, (double)(int)(rowY2 + rowH), DOUBLE_80619f00,
                                    (double)FLOAT_8061a010, (double)FLOAT_80619eec, (double)FLOAT_80619ff8,
                                    in_f7, in_f8, &DAT_80611064, extraout_r4, 0, cursorX - 0xb,
                                    param_5, param_6, param_7, param_8);
    text_draw_char((double)(int)colYneg, (double)(int)(rowY2 + rowH), s, colBright);

    FUN_80382700();
}

// 0x802dc638 - Per-frame refresh of the player-select screen: mirror the focused
// row's stored slot into DAT_805cc61c and set the handler enable flag.
void menu_stats_player_select_refresh(int element)
{
    if (hud_element_is_closing(element) == 0) {
        int row = hud_find_child_by_id(element, 0);
        DAT_805cc61c = *(int *)(row + 200);
        if (*(int *)(element + 0x188) == 0) {
            hud_screen_set_handler_param_b(element, 0, 1);
        } else {
            hud_screen_set_handler_param_b(element, 0, 0);
        }
    }
}

// 0x802dc6b8 - Pop the stats screen; if the parent screen is not the cheats
// options screen, clear the active slot selection.
void menu_stats_screen_pop(void)
{
    int parent = hud_screen_pop_with_restore();
    if (*(void ***)(parent + 4) != (void **)&PTR_menu_cheats_options_screen_build_80476600) {
        DAT_805cc61c = 0;
    }
}

// 0x802dc700 - Build the player-select + stats-options screen: a player picker
// (single entry for 1P, selectable list for >1P) followed by stat-view option
// rows (0x406 / 0x405 / 0x407) and the summary render spacer.
void menu_stats_player_select_build(int element)
{
    int str = get_string_by_id(0x3d3);
    hud_element_init_from_layout(0x8000, str, 0, (void *)menu_stats_screen_pop);
    *(int *)(element + 0x214) = 8;
    hud_set_item_padding_mode((double)FLOAT_8061a070, 1, 1, 1);
    hud_set_context_property(0x22200);

    if (DAT_80611014 < 2) {
        get_string_by_id(0x395);
        hud_add_list_item();
        hud_item_add_spacer((double)FLOAT_80619f08, 0x400000);
        hud_event_add_string(player_get_entity_ptr(0));
    } else {
        get_string_by_id(0x395);
        hud_add_list_item();
        hud_item_add_spacer((double)FLOAT_80619f08, 0);
        for (int i = 0; i < DAT_80611014; i++) {
            hud_event_add_string(player_get_entity_ptr(i));
        }
        hud_get_last_created_item();
        hud_item_on_value_change();
    }

    hud_add_callback_item_default(0);
    get_string_by_id(0x406);
    hud_add_list_item();
    get_string_by_id(0x405);
    hud_add_list_item();
    get_string_by_id(0x407);
    hud_add_list_item();
    hud_add_callback_item_default(0);
    hud_add_spacer_item((double)FLOAT_80619fe0, 0, (void *)menu_stats_summary_render);
    hud_element_apply_center_xy(DAT_806119e0);
}

// 0x802dc8bc - Format the detailed per-player stats text block into a set of
// string builders: favourite weapon, kills/deaths, accuracy, best times, and
// each game-mode's records. Reads the player stat record via stats_get_player_ptr.
void menu_stats_format_detail(u64 param_1, double param_2, double param_3, u64 param_4,
                              u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                              int param_9, int param_10, int param_11, u8 *param_12,
                              u8 *param_13, int param_14, int param_15, int param_16)
{
    double in_f31 = 0, in_ps31_1 = 0;
    u64 extraout_f1 = 0;
    float local_158;
    int textBuf[65];
    double local_50 = 0;
    (void)param_1; (void)in_f31; (void)in_ps31_1;

    int slot = FUN_803826dc();
    uint bestVal = 1;
    int bestIndex = -1;
    u64 acc = extraout_f1;

    u8 *rec  = stats_get_player_ptr(slot);
    u8 *rec2 = stats_get_player_ptr(slot);
    u32 *recW = (u32 *)stats_get_player_ptr(slot);

    int *out = (int *)debug_log_formatted(acc, param_2, param_3, param_4, param_5, param_6, param_7,
                                          param_8, &DAT_80611058, (int)*recW, param_11,
                                          (u64)param_12, (u64)param_13, param_14, param_15, param_16);
    FUN_8038b510((int *)&DAT_80476648, out);

    // Favourite weapon: the weapon slot with the highest use count (0..0x95).
    int off = 0;
    for (uint i = 0; i < 0x96; i++) {
        u8 *pr = stats_get_player_ptr(slot);
        if (bestVal <= *(uint *)(pr + off + 8)) {
            pr = stats_get_player_ptr(slot);
            bestVal = *(uint *)(pr + off + 8);
            bestIndex = (int)i;
        }
        off += 4;
    }

    int *favStr;
    if (bestIndex < 0) {
        favStr = (int *)get_string_by_id(0x3d8);
    } else {
        favStr = (int *)get_string_by_id(weapon_def_get_hud_icon(bestIndex));
    }
    acc = FUN_8038b510((int *)&DAT_80476e40, favStr);

    // Kills across the four "kill" weapon ids (0x5d/0x5e/0x5f/0x9b).
    int kills = *(int *)(rec + weapon_def_find_index(0x5d, -1) * 4 + 8)
              + *(int *)(rec + weapon_def_find_index(0x5e, -1) * 4 + 8)
              + *(int *)(rec + weapon_def_find_index(0x5f, -1) * 4 + 8)
              + *(int *)(rec + weapon_def_find_index(0x9b, -1) * 4 + 8);

    if (kills == 1) {
        int s = get_string_by_id(~-(uint)(DAT_8060f948 == 3) & 0x17b6);
        acc = FUN_80387fb8(acc, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                           -0x7fb89138, &DAT_8061106c, s, &DAT_80476640, &DAT_80476640,
                           param_14, param_15, param_16);
    } else if (kills < 1 && kills >= 0) {
        int *s = (int *)get_string_by_id(0x17a0);
        acc = FUN_8038b510((int *)&DAT_80476ec8, s);
    } else {
        int s = get_string_by_id(~-(uint)(DAT_8060f948 == 3) & 0x17b7);
        acc = FUN_80387fb8(acc, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                           -0x7fb89138, &s__d__s_80611074, kills, (void *)(intptr_t)s,
                           &DAT_80476640, param_14, param_15, param_16);
    }

    // Best times for the three time-record fields (0x91c / 0xba8 / 0xbb0),
    // formatted as h/m/s, plus the mode-record block at the end.
    for (int i = 0; ; i++) {
        int seconds;
        if (i == 1) {
            seconds = (int)*(float *)(rec2 + 0xba8);
            local_50 = (double)seconds;
        } else if (i < 1) {
            if (i < 0) { seconds = 0; }
            else { seconds = (int)*(float *)(rec2 + 0x91c); local_50 = (double)seconds; }
        } else {
            if (2 < i) { seconds = 0; }
            else { seconds = (int)*(float *)(rec2 + 0xbb0); local_50 = (double)seconds; }
        }
        (void)local_50;

        int minutes = seconds / 0x3c;
        textBuf[0] = 0;
        int hours = minutes / 0x3c;
        int *timeStr;

        if (seconds < 1) {
            timeStr = (i == 1) ? textBuf : (int *)get_string_by_id(0x3db);
        } else {
            if (0 < hours / 0x18) {
                int f = get_string_by_id(0x3dc);
                int r = debug_log_formatted(acc, param_2, param_3, param_4, param_5, param_6, param_7,
                                            param_8, &s__d__s_8061107c, hours / 0x18, f,
                                            (u64)param_12, (u64)param_13, param_14, param_15, param_16);
                acc = FUN_8038b4a0((int)textBuf, r);
            }
            if (0 < hours) {
                int f = get_string_by_id(0x3dd);
                int r = debug_log_formatted(acc, param_2, param_3, param_4, param_5, param_6, param_7,
                                            param_8, &s__d__s_8061107c, hours % 0x18, f,
                                            (u64)param_12, (u64)param_13, param_14, param_15, param_16);
                acc = FUN_8038b4a0((int)textBuf, r);
            }
            if (0 < minutes) {
                int f = get_string_by_id(0x3de);
                int r = debug_log_formatted(acc, param_2, param_3, param_4, param_5, param_6, param_7,
                                            param_8, &s__d__s_8061107c, minutes % 0x3c, f,
                                            (u64)param_12, (u64)param_13, param_14, param_15, param_16);
                acc = FUN_8038b4a0((int)textBuf, r);
            }
            int f = get_string_by_id(0x3df);
            int r = debug_log_formatted(acc, param_2, param_3, param_4, param_5, param_6, param_7,
                                        param_8, &s__d__s_8061107c, seconds % 0x3c, f,
                                        (u64)param_12, (u64)param_13, param_14, param_15, param_16);
            acc = FUN_8038b4a0((int)textBuf, r);
            timeStr = textBuf;
        }

        if (i == 1) {
            acc = FUN_8038b510((int *)&DAT_80476758, timeStr);
        } else if (i < 1 && i >= 0) {
            acc = FUN_8038b510((int *)&DAT_804766d0, timeStr);
        }

        if (2 < i + 1) {
            // Final block: aggregate mode records (kills, deaths, accuracy, etc).
            FUN_80387fb8(acc, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                         -0x7fb89578, &DAT_80611058, *(int *)(rec2 + 0x930), &DAT_80476640,
                         param_14, param_15, param_16, 0);
            if (*(int *)(rec2 + 0x930) == 0) {
                int *s = (int *)get_string_by_id(0x3ec);
                FUN_8038b510((int *)&DAT_80476b10, s);
            } else {
                int f = get_string_by_id(0x3f0);
                double ratio = (double)FLOAT_80619fac *
                               ((double)*(int *)(rec2 + 0x934) / (double)*(int *)(rec2 + 0x930));
                FUN_80387fb8((double)*(int *)(rec2 + 0x934), DOUBLE_80619f00, (double)FLOAT_80619fac,
                             param_4, param_5, param_6, param_7, param_8, -0x7fb894f0,
                             &s__d__s_80611074, (int)ratio, (void *)(intptr_t)f,
                             param_14, param_15, param_16);
            }

            worm_stat_scale_time((double)*(float *)(rec2 + 0xba4), &local_158);
            int f = get_string_by_id(0x3f4);
            FUN_80387fb8((double)local_158, param_2, param_3, param_4, param_5, param_6, param_7,
                         param_8, -0x7fb89820, &DAT_80611084, f, &DAT_80476640,
                         param_14, param_15, param_16);

            double dist = (double)*(float *)(rec2 + 0x91c);
            if (dist <= (double)FLOAT_80619f08) {
                int *s = (int *)get_string_by_id(0x3ec);
                acc = FUN_8038b510((int *)&DAT_80476868, s);
            } else {
                int ff = get_string_by_id(0x3f7);
                acc = FUN_80387fb8((double)(local_158 / (float)(dist / (double)FLOAT_8061a140)),
                                   param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   -0x7fb89798, &DAT_80611084, ff, &DAT_80476640,
                                   param_14, param_15, param_16);
            }

            static const int off_list[] = {0x910, 0x920, 0x924, 0x928, 0x92c, -1,
                                           0x954, 0x958, 0xb9c, 0xb94};
            static const int sel_list[] = {-0x7fb89710, -0x7fb89688, -0x7fb89600, -0x7fb890b0,
                                           -0x7fb89028, -0x7fb89468, -0x7fb893e0, -0x7fb89358,
                                           -0x7fb892d0, -0x7fb88f18};
            for (int k = 0; k < 10; k++) {
                int val;
                if (off_list[k] == -1) {
                    val = *(int *)(rec2 + 0x948) + *(int *)(rec2 + 0x94c);
                } else {
                    val = *(int *)(rec2 + off_list[k]);
                }
                acc = FUN_80387fb8(acc, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   sel_list[k], &DAT_80611058, val, &DAT_80476640,
                                   param_14, param_15, param_16, 0);
            }

            FUN_8038b510((int *)&DAT_80476db8, (int *)&DAT_8061108c);
            FUN_8038b510((int *)&DAT_80477060, (int *)&DAT_8061108c);
            FUN_80382728();
            return;
        }
    }
}

// 0x802dcf4c - Render the detailed stats table: iterates the stat-row descriptor
// table (DAT_80476640/DAT_80476648, stride 0x88) drawing label + value pairs with
// drop shadows, then the award records block (up to 13 awards from DAT_803f291a).
// param_3 != 0 selects the "draw" pass (measure-only otherwise, to size columns).
void menu_stats_detail_table_render(int param_1, int param_2, int param_3)
{
    double in_f26 = 0, in_f27 = 0, in_f28 = 0, in_f29 = 0, in_f30 = 0, in_f31 = 0;
    double in_ps26_1 = 0, in_ps27_1 = 0, in_ps28_1 = 0, in_ps29_1 = 0, in_ps30_1 = 0, in_ps31_1 = 0;
    u64 in_f6 = 0, in_f7 = 0, in_f8 = 0;
    byte timeBuf[32];
    byte fmtBuf[128];
    (void)param_1; (void)param_2;
    (void)in_f26; (void)in_f27; (void)in_f28; (void)in_f29; (void)in_f30; (void)in_f31;
    (void)in_ps26_1; (void)in_ps27_1; (void)in_ps28_1; (void)in_ps29_1; (void)in_ps30_1; (void)in_ps31_1;

    u64 packed = FUN_803826c0();
    int element = (int)((u64)packed >> 0x20);
    float *layout = (float *)(uintptr_t)packed;

    double left = (double)*layout;
    double right = (double)layout[2];
    double topY = (double)layout[1];
    float midX = FLOAT_80619eec * (float)(left + right);
    double rowY = (double)midX;
    uint colX = (uint)midX;

    uint fade = FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(element + 0xf0))) & 0xff;
    uint colValue = fade | 0x64647f00;
    uint colLabel = fade | 0x32327f00;
    double maxW = (double)FLOAT_80619f08;

    if (param_3 != 0) {
        gx_set_scissor_rect((int)left, (int)((double)FLOAT_80619fc4 + topY), (int)right,
                            (int)(FLOAT_80619ee8 + layout[3]));
        gx_set_sub_material_id(1);
    }

    double indentY = (double)-(float)((double)*(float *)(element + 0x80) * (double)FLOAT_80611a64 - topY);

    // Stat-row descriptor table: labels + values, stride 0x88, 0x15 rows.
    int *pr = (int *)stats_get_player_ptr(DAT_805cc61c);
    uint row = (uint)(*pr < 1);
    int rowOff = row * 0x88;
    for (; (int)row < 0x15; row++) {
        text_set_font_table(0x15);
        if ((&DAT_80476648)[rowOff] == '\0') {
            if (row != 2) {
                uint sid = get_string_by_id(*(int *)(&DAT_80476640 + rowOff));
                byte *label = text_string_to_upper(sid);
                double w = (double)text_get_char_width(label);
                if (maxW < w) maxW = w;
                if (param_3 != 0) {
                    text_draw_char_drop_shadow(-(double)(float)((double)FLOAT_80619eec * w -
                                               (double)(int)colX), rowY, label, colValue, fade);
                }
                rowY = (double)(float)((double)FLOAT_80619f88 * (double)FLOAT_80611674 + rowY);
            }
        } else {
            byte *label = (byte *)get_string_by_id(*(int *)(&DAT_80476640 + rowOff));
            double w = (double)text_get_char_width(label);
            if (maxW < w) maxW = w;
            if (param_3 != 0) {
                text_draw_char_drop_shadow(-(double)(float)((double)FLOAT_80619eec * w -
                                           (double)(int)colX), rowY, label, colValue, fade);
            }
            double valY = (double)(float)((double)FLOAT_8061a06c * (double)FLOAT_80611674 + rowY);
            text_set_font_table(0x12);
            byte *value = &DAT_80476648 + rowOff;
            double vw = (double)text_get_char_width(value);
            if (maxW < vw) maxW = vw;
            uint valX = (uint)-(float)((double)FLOAT_80619eec * vw - (double)(int)colX);
            if (param_3 != 0) {
                int markerIdx = *(int *)(rowOff + -0x7fb899bc);
                u8 *pp;
                if (markerIdx < 0 ||
                    (pp = stats_get_player_ptr(DAT_805cc61c), pp[markerIdx + 0xbb4] == '\0')) {
                    text_draw_char_drop_shadow((double)(int)valX, valY, value, colLabel, fade);
                } else {
                    hud_marker_draw(3, valX - 0x20, (int)valY, (int)(FLOAT_8061a024 * FLOAT_8060f958), 0x10);
                    gx_set_sub_material_id(1);
                    uint teamCol = color_fade_team_b((double)*(float *)(element + 0xf0), 3);
                    text_draw_char_drop_shadow((double)(int)valX, valY, value, teamCol, fade);
                }
            }
            rowY = (double)(float)((double)FLOAT_8061a04c * (double)FLOAT_80611674 + valY);
        }
        rowOff += 0x88;
    }

    // Award records: up to 13 combined-award rows (times per medal tier).
    double baseX = (double)*layout;
    double col2 = (double)(float)((double)FLOAT_8061a064 * (double)((double)layout[2] - baseX) + baseX);
    double valX2 = (double)(float)(col2 + (double)FLOAT_80611a68);
    int awardOff = 0;
    for (int a = 0; a < 0xd; a++) {
        if (award_grant_to_combined(a, 0, 0) != 0) {
            u8 *arec = stats_get_award_record(DAT_805cc61c, a);
            uint sid = get_string_by_id((int)*(short *)(&DAT_803f291a + awardOff));
            byte *label = text_string_to_upper(sid);
            double w = (double)text_get_char_width(label);
            if (maxW < w) maxW = w;
            if (param_3 != 0) {
                text_draw_char_drop_shadow(-(double)(float)((double)FLOAT_80619eec * w -
                                           (double)(int)colX), rowY, label, colValue, fade);
            }
            double lineY = (double)(float)((double)FLOAT_8061a144 * (double)FLOAT_80611674 + rowY);

            // Three medal-time columns (record fields +0xc/+0x10/+0x14).
            const int timeFields[3] = {0xc, 0x10, 0x14};
            const int hdrIds[3] = {0x3ce, 0x3cf, 0x3d0};
            for (int t = 0; t < 3; t++) {
                int *hdr = (int *)get_string_by_id(hdrIds[t]);
                FUN_8038b510((int *)fmtBuf, hdr);
                FUN_8038b4a0((int)fmtBuf, -0x7f9eefac);
                double hw = (double)text_get_char_width(fmtBuf);
                text_draw_char_drop_shadow((double)((float)(col2 - hw) - FLOAT_80611a68), lineY,
                                           fmtBuf, colValue, fade);
                time_format_long((double)(float)((double)(int)DAT_806109c0 *
                                 (double)*(float *)(arec + timeFields[t])),
                                 DOUBLE_80619f00, (double)FLOAT_80619ee8, (double)FLOAT_80619eec,
                                 (double)FLOAT_80611a64, in_f6, in_f7, in_f8);
                if (timeBuf[0] == 0) {
                    int *empty = (int *)get_string_by_id(0xe38);
                    FUN_8038b510((int *)timeBuf, empty);
                }
                text_apply_alpha((double)(float)((double)layout[2] - valX2), timeBuf, 0);
                text_draw_char_drop_shadow(valX2, lineY, timeBuf, colLabel, fade);
                text_set_color_rgb((double)FLOAT_80619ee8, (double)FLOAT_80619ee8, (double)FLOAT_80619ee8);
                lineY = (double)(float)(lineY + (double)FLOAT_80611674);
            }
            rowY = (double)(float)((double)FLOAT_80619f88 * (double)FLOAT_80611674 + lineY);
        }
        awardOff += 0x44;
    }

    *(float *)(element + 0x58) = FLOAT_80611a5c * ABS((float)(rowY - indentY));
    FUN_8038270c();
}

// 0x802dd714 - Render the detail table in "draw" mode (param_3 = 1).
void menu_stats_detail_table_render_active(int param_1, int param_2, int param_3)
{
    (void)param_2;
    menu_stats_detail_table_render(param_1, param_3, 1);
}

// 0x802dd73c - Build the detailed stats screen: run a measure pass of the table,
// lay out the player entity row + summary render spacer, center, and register the
// select (0x19c7) / back (0x19c8) handlers.
void menu_stats_detail_screen_build(int element)
{
    float sizes[4];
    sizes[0] = FLOAT_80619f08;
    sizes[1] = FLOAT_80619f08;
    sizes[2] = FLOAT_80619f08;
    sizes[3] = FLOAT_80619f08;
    menu_stats_detail_table_render((int)&sizes[0], 0, 0);

    int str = get_string_by_id(0x35b);
    hud_element_init_from_layout(0x200c, str, 0, 0);
    *(int *)(element + 0x214) = 8;
    player_get_entity_ptr(DAT_805cc61c);
    hud_add_list_item();
    hud_add_callback_item_default(1);

    *(float *)(element + 0x68) = FLOAT_80619f38 + *(float *)(element + 100);
    hud_add_spacer_item((double)FLOAT_80619f60, 1, (void *)menu_stats_detail_table_render_active);
    *(float *)(element + 0x6c) = FLOAT_80619f60 + *(float *)(element + 0x68);
    hud_element_apply_center_xy(DAT_806119e0);

    hud_screen_register_handler_global(0, get_string_by_id(0x19c7), 2, 0);
    hud_screen_register_handler_global(1, get_string_by_id(0x19c8), 0, 0);
}
