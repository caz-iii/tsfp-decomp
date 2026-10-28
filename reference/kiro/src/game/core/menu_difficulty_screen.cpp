// ============================================================================
// menu_difficulty_screen.cpp
// Batch: batch_819
// Address range: 0x802d90d0 - 0x802d99e4
// Date: 2026-09-11T05:35:00Z
//
// Front-end difficulty-select + difficulty-record screens and the award-
// availability cache, continuation of the mode-select family (batch_817/818).
// Contains:
//   - a per-row difficulty-record marker/award render callback,
//   - the difficulty-select screen build (easy/normal/hard, award-gated locks),
//   - a "set mode 4 + push" enter handler,
//   - the default-focus resolver (last unlocked difficulty),
//   - the difficulty-record grid build,
//   - the screen-vtable dispatcher keyed on DAT_805cc608,
//   - a challenge-record snapshot pair (seed + changed test), and
//   - the big award-availability cache rebuild that fills DAT_805ce3xx tables.
//
// Layout:
//   menu_difficulty_record_row_render 0x802d90d0  per-row marker + award render
//   menu_difficulty_screen_build      0x802d9410  difficulty-select screen build
//   menu_difficulty_push_mode4        0x802d95bc  set mode 4, UI sound, push
//   menu_difficulty_last_unlocked     0x802d9634  find last unlocked difficulty
//   menu_difficulty_focus_default     0x802d96d0  default focus (0 or unlocked?)
//   menu_difficulty_record_build      0x802d9728  difficulty-record grid build
//   menu_screen_vtable_for_mode       0x802d98b8  screen vtable dispatch
//   menu_record_snapshot_seed         0x802d9960  seed challenge-record snapshot
//   menu_record_snapshot_changed      0x802d9998  test snapshot changed (sign)
//   menu_award_cache_rebuild          0x802d99e4  rebuild award-availability cache
//
// NON-MATCHING: incoming paired-single / FP spill regs (in_f31/in_ps31_1) are
// modelled as locals; the frame-actor thunk pairs (FUN_803826dc head returns a
// (focus,element) register pair split by shifts; FUN_803826e4 head, and the
// FUN_80382728/FUN_80382730 tails) and the __save_gpr_28/__restore_gpr_28
// prologue helpers are declared extern; CONCAT44 magic-bias int->float
// conversions are kept verbatim. award_grant_to_combined and hud_add_list_item
// are declared void in the generated headers but their return values are
// consumed here, so they are called through address-bound function pointers.
// ============================================================================

#include "common.h"
#include "menu.h"     // pulls generated_decls.h
#include "hud.h"
#include "gx.h"
#include "sprite.h"
#include "text.h"
#include "sound.h"

// ---- frame / prologue helper thunks ---------------------------------------
extern "C" unsigned long long FUN_803826dc(void); // frame-actor head (focus,element) pair
extern "C" int       FUN_803826e4(void);           // frame-actor head (element ptr)
extern "C" void      FUN_80382728(void);           // frame-actor tail (record-row / grid)
extern "C" void      FUN_80382730(void);           // frame-actor tail (cache rebuild)
extern "C" unsigned int FUN_803825c0(double intensity); // fade/alpha 0..255
extern "C" unsigned long long __save_gpr_28(void);
extern "C" void      __restore_gpr_28(void);

// ---- void-but-return-used helpers via address-bound function pointers ------
typedef int (*add_item_fp)(void);
static const add_item_fp __md_add_item = (add_item_fp)0x8003ea8c;      // hud_add_list_item
typedef int (*award_combine_fp)(int, int, int);
static const award_combine_fp __md_award_combine = (award_combine_fp)0x801c0d10; // award_grant_to_combined

// Named helpers (hud_*, gx_*, sprite_*, sound_*, award_*, stats_get_challenge_record,
// get_string_by_id, menu_mode_enter_with_sound) resolve via generated_decls.h.
// countLeadingZeros is a macro in common.h.

// ---- raw scalar ROM globals -----------------------------------------------
extern "C" int  DAT_805cc608;   // active screen-kind selector (vtable dispatch)
extern "C" int  DAT_805cc614;   // active game mode id
extern "C" int  DAT_805cc628;   // active challenge/objective index
extern "C" int  DAT_805cc630;   // active record/difficulty index
extern "C" unsigned char DAT_805cc68e; // sub-mode selector byte
extern "C" unsigned char DAT_805cc6e8; // difficulty-screen flag
extern "C" int  DAT_80612b1c;   // record-changed scratch flag
extern "C" int  DAT_80612b20;   // challenge-record snapshot word
extern "C" int *DAT_80610a20;   // material/sprite table base (array of records)
extern "C" int *DAT_806119e0;   // layout center context (array of ptrs)

// award-availability cache tables (filled by menu_award_cache_rebuild)
extern "C" int  DAT_805ce388;   // per-slot award-check cache (16 x u32)
extern "C" int  DAT_805ce3c8;   // per-map completion cache (0x96 x u32)
extern "C" int  DAT_805ce620;   // generic-award cache (0x46 x u32)
extern "C" int  DAT_805ce738;   // id-range-valid cache (10 x u32)
extern "C" int  DAT_805ce760;   // award-by-id cache (7 x u32)
extern "C" int  DAT_805ce77c;   // modifier-slot cache (16 x u32)

// map-completion descriptor table (stride 0x30) used by the cache rebuild
extern "C" int  DAT_80474680;   // map id (stride 0x30)
extern "C" char DAT_8047467d;   // map param byte (stride 0x30)

// record/difficulty label + detail short tables (shared with siblings)
extern "C" short DAT_803f2918;  // record table: label short (stride 0x44 bytes)

// ---- screen-vtable pointer tables (data) ----------------------------------
extern "C" void *PTR_FUN_8047659c;
extern "C" void *PTR_menu_match_settings_screen_init_804764cc;
extern "C" void *PTR_menu_objective_award_list_build_8047651c;
extern "C" void *PTR_menu_challenge_list_build_804764ec;
extern "C" void *PTR_FUN_804771b8;
extern "C" void *PTR_FUN_80476610;
extern "C" void *PTR_FUN_804771e8;
extern "C" void *PTR_menu_mode_select_screen_build_8047654c;

// ---- float / double ROM constants -----------------------------------------
extern "C" float  FLOAT_80619f5c, FLOAT_80619eec, FLOAT_80619f38, FLOAT_80611a68;
extern "C" float  FLOAT_8060f958, FLOAT_80611a5c, FLOAT_8061a060, FLOAT_80619f0c;
extern "C" float  FLOAT_8061a11c, FLOAT_80619fec, FLOAT_80619f08, FLOAT_80619f8c;
extern "C" float  FLOAT_80619f84;
extern "C" double DOUBLE_80619f00; // int->float magic-bias constant

// ============================================================================
// 0x802d90d0 - per-row difficulty-record marker + award icon render callback
// ============================================================================
void menu_difficulty_record_row_render(unsigned int a, unsigned int b, float *rect)
{
    unsigned int ux;
    unsigned int umid;
    unsigned int uw;
    int      markerY;
    int      focusElem;
    int      granted;
    int      recordIdx;
    int      mat;
    int      matRec;
    int      sx0, sy0, sw, sh;
    double   halfW;
    double   baseX;
    double   spriteH;
    // paired-single FP spill inputs (non-matching, modelled as locals)
    double in_f31 = 0.0, in_ps31_1 = 0.0;

    (void)a; (void)b;
    (void)in_f31; (void)in_ps31_1;

    unsigned long long framePair = FUN_803826dc();
    focusElem = (int)((unsigned long long)framePair >> 0x20);
    granted   = __md_award_combine(DAT_805cc630, *(int *)((int)framePair + 0x14), 1);
    recordIdx = *(int *)((int)framePair + 0x14);

    ux   = (unsigned int)(FLOAT_80619f5c * (rect[3] - rect[1]));
    umid = (unsigned int)(FLOAT_80619eec * (rect[3] + rect[1]));
    halfW = (double)(FLOAT_80619f38 * FLOAT_80611a68);
    baseX = (double)*rect + halfW;
    uw = (unsigned int)(FLOAT_8060f958 * FLOAT_80611a68 *
        (float)((double)(CONCAT44(0x43300000, ux ^ 0x80000000)) - DOUBLE_80619f00) * FLOAT_80611a5c);

    gx_get_scissor_rect(&sx0, &sy0, &sw, &sh);
    int scX = (int)*(float *)(focusElem + 0xa4);
    int scY = (int)*(float *)(focusElem + 0xa8);
    int scX1 = (int)(*(float *)(focusElem + 0xa4) + *(float *)(focusElem + 0xac));
    int scY1 = (int)(*(float *)(focusElem + 0xa8) + *(float *)(focusElem + 0xb0));
    gx_set_scissor_rect(scX, scY, scX1, scY1);

    markerY = (int)-(FLOAT_80619eec *
        (float)((double)(CONCAT44(0x43300000, ux ^ 0x80000000)) - DOUBLE_80619f00) -
        (float)((double)(CONCAT44(0x43300000, umid ^ 0x80000000)) - DOUBLE_80619f00));
    hud_marker_draw(recordIdx + 1, (unsigned int)(int)baseX, markerY, uw, ux);

    if (granted != 0) {
        mat = sprite_get_texture_id(0x15a);
        unsigned int rx  = (unsigned int)((double)rect[2] - halfW);
        unsigned int rh  = (unsigned int)(FLOAT_8061a060 * (rect[3] - rect[1]));
        int rw = (int)(FLOAT_8060f958 * FLOAT_80611a68 *
            (float)((double)(CONCAT44(0x43300000, rh ^ 0x80000000)) - DOUBLE_80619f00) * FLOAT_80611a5c);
        gx_set_active_material(mat);
        matRec = *DAT_80610a20 + mat * 0x58;
        spriteH = (double)(FLOAT_80619eec *
            (float)((double)(CONCAT44(0x43300000, rh ^ 0x80000000)) - DOUBLE_80619f00));
        FUN_803825c0((double)(FLOAT_80619f0c * *(float *)(focusElem + 0xf0)));
        int topY = (int)((double)(float)((double)(CONCAT44(0x43300000, umid ^ 0x80000000)) - DOUBLE_80619f00) - spriteH);
        unsigned int botY = (unsigned int)((double)(float)((double)(CONCAT44(0x43300000, umid ^ 0x80000000)) - DOUBLE_80619f00) + spriteH);
        gx_draw_sprite_rect(rx - rw, topY, rx, botY, 0, 0,
                            (unsigned int)*(unsigned short *)(matRec + 0x4e) << 4,
                            (unsigned int)*(unsigned short *)(matRec + 0x52) << 4);
    }
    gx_set_scissor_rect(sx0, sy0, sw, sh);
    FUN_80382728();
}

// ============================================================================
// 0x802d9410 - difficulty-select screen build (easy / normal / hard)
// ============================================================================
void menu_difficulty_screen_build(void)
{
    int  screen;
    int  titleStr;
    int  item;
    unsigned int slot;
    unsigned int clz;
    bool focusHint;

    screen = (int)__save_gpr_28();
    hud_element_init_from_layout(0x800a000, 0, (int)&menu_mode_enter_with_sound, 0);
    titleStr = get_string_by_id(0x421);
    *(int *)(screen + 0x200) = titleStr;
    *(int *)(screen + 0x214) = 0x50;
    hud_set_item_padding_mode(FLOAT_8061a11c, 1, 1, 1);
    hud_set_margins(FLOAT_80619fec, FLOAT_80619f08, 1);
    hud_set_context_property(0x22200);

    get_string_by_id(0x3ce);
    hud_add_list_item();
    slot = award_check_slot1(DAT_805cc630);
    clz  = countLeadingZeros(slot);
    item = (int)hud_get_last_created_item();
    hud_item_set_locked(item, clz >> 5);

    get_string_by_id(0x3cf);
    hud_add_list_item();
    slot = award_check_slot1(DAT_805cc630);
    clz  = countLeadingZeros(slot);
    item = (int)hud_get_last_created_item();
    hud_item_set_locked(item, clz >> 5);

    get_string_by_id(0x3d0);
    hud_add_list_item();
    slot = award_check_slot1(DAT_805cc630);
    clz  = countLeadingZeros(slot);
    item = (int)hud_get_last_created_item();
    hud_item_set_locked(item, clz >> 5);

    hud_element_apply_center_xy(DAT_806119e0);
    focusHint = menu_difficulty_focus_default(DAT_805cc630);
    hud_set_focus_by_offset(screen, (unsigned int)focusHint);
    __restore_gpr_28();
}

// ============================================================================
// 0x802d95bc - set mode 4, fire UI sound, stash index, push
// ============================================================================
void menu_difficulty_push_mode4(int screen)
{
    int params[13];

    DAT_805cc614 = 4;
    sound_init_play_params((int)params);
    params[0] = 0xd1e;
    sound_play_request(params);
    DAT_805cc630 = *(int *)(screen + 0x188);
    DAT_805cc6e8 = 1;
    hud_screen_push();
}

// ============================================================================
// 0x802d9634 - find the last unlocked difficulty index (focus resolution)
// ============================================================================
int menu_difficulty_last_unlocked(void)
{
    int last;
    int pass;
    int idx;

    __save_gpr_28();
    last = 0;
    pass = 0;
    do {
        idx = 0;
        do {
            if (award_check_slot1(idx) != 0) {
                last = idx;
            }
            idx = idx + 1;
        } while (idx < 0xd);
    } while ((0xb < last) && (pass = pass + 1, pass < 3));
    __restore_gpr_28();
    return last;
}

// ============================================================================
// 0x802d96d0 - default focus: index 0 is always focusable, else must be unlocked
// ============================================================================
bool menu_difficulty_focus_default(int index)
{
    if (index == 0) {
        return true;
    }
    return award_check_slot1(index) != 0;
}

// ============================================================================
// 0x802d9728 - difficulty-record grid build (13 rows, locked entries get icon)
// ============================================================================
void menu_difficulty_record_build(void)
{
    int  screen;
    int  titleStr;
    int  item;
    int  iconStr;
    int  focus;
    int  idx;
    int  off;

    // original uses FUN_803826dc() head; the element ptr is the low word
    screen = (int)FUN_803826dc();
    titleStr = get_string_by_id(0x360);
    *(int *)(screen + 0x200) = titleStr;
    *(int *)(screen + 0x214) = 0x50;
    titleStr = get_string_by_id(0x360);
    hud_element_init_from_layout(0x800e000, (void *)titleStr, 0, 0);
    hud_element_scroll_vertical((int)FLOAT_80619f08, 3);
    hud_set_margins(FLOAT_80619f38, FLOAT_80619f8c, 1);

    idx = 0;
    off = 0;
    do {
        get_string_by_id((int)*(short *)((int)&DAT_803f2918 + off));
        award_check_slot1(idx);
        item = __md_add_item();
        *(int *)(item + 0x1c) = 0xb;
        *(float *)(item + 0x40) = FLOAT_80619f84;
        if (award_check_slot1(idx) == 0) {
            int locked = (int)hud_get_last_created_item();
            hud_item_set_locked(locked, 1);
            iconStr = get_string_by_id(0x1626);
            int last = (int)hud_get_last_created_item();
            hud_item_set_icon((int *)last, iconStr);
        }
        idx = idx + 1;
        off = off + 0x44;
    } while (idx < 0xd);

    hud_element_apply_scroll();
    focus = menu_difficulty_last_unlocked();
    hud_set_focus_by_offset(screen, focus);
    hud_element_apply_center_xy(DAT_806119e0);
    FUN_80382728();
}

// ============================================================================
// 0x802d98b8 - screen-vtable dispatch by active screen-kind (DAT_805cc608)
// ============================================================================
void **menu_screen_vtable_for_mode(void)
{
    switch (DAT_805cc608) {
    case 4:
        return (void **)&PTR_FUN_8047659c;
    case 5:
        return (void **)&PTR_menu_match_settings_screen_init_804764cc;
    case 6:
    case 7:
        return (void **)&PTR_menu_objective_award_list_build_8047651c;
    case 8:
        return (void **)&PTR_menu_challenge_list_build_804764ec;
    case 9:
        break;
    case 10:
        return (void **)&PTR_FUN_804771b8;
    default:
        return (void **)0;
    case 0xe:
        return (void **)&PTR_FUN_80476610;
    case 0xf:
        return (void **)&PTR_FUN_804771e8;
    case 0x10:
        return (void **)&PTR_FUN_804771b8;
    }
    if (DAT_805cc68e != '\x02') {
        return (void **)&PTR_menu_match_settings_screen_init_804764cc;
    }
    return (void **)&PTR_menu_mode_select_screen_build_8047654c;
}

// ============================================================================
// 0x802d9960 - seed the challenge-record snapshot word
// ============================================================================
void menu_record_snapshot_seed(void)
{
    unsigned char *rec;
    rec = (unsigned char *)stats_get_challenge_record(0, DAT_805cc628);
    DAT_80612b20 = *(int *)(rec + 0xc);
}

// ============================================================================
// 0x802d9998 - test whether the challenge-record snapshot changed (sign bit)
// ============================================================================
unsigned int menu_record_snapshot_changed(void)
{
    unsigned int cur;
    unsigned char *rec;
    rec = (unsigned char *)stats_get_challenge_record(0, DAT_805cc628);
    cur = *(unsigned int *)(rec + 0xc) ^ DAT_80612b20;
    return ((int)cur >> 1) - (cur & *(unsigned int *)(rec + 0xc)) >> 0x1f;
}

// ============================================================================
// 0x802d99e4 - rebuild the award-availability caches (DAT_805ce3xx tables)
// ============================================================================
void menu_award_cache_rebuild(void)
{
    unsigned int v;
    int idx;
    int off;

    FUN_803826e4();
    DAT_80612b1c = 0;
    menu_record_snapshot_seed();

    // 16 per-slot award-check entries
    idx = 0; off = 0;
    do {
        v = award_check_slot2();
        idx = idx + 1;
        *(unsigned int *)((int)&DAT_805ce388 + off) = v;
        off = off + 4;
    } while (idx < 0x10);

    // 0x96 per-map completion entries (descriptor stride 0x30)
    idx = 0; off = 0;
    int descOff = 0;
    do {
        v = award_check_map_completion(*(int *)((int)&DAT_80474680 + descOff),
                                       (int)(char)(&DAT_8047467d)[descOff]);
        idx = idx + 1;
        *(unsigned int *)((int)&DAT_805ce3c8 + off) = v;
        off = off + 4;
        descOff = descOff + 0x30;
    } while (idx < 0x96);

    // 0x46 generic-award entries
    idx = 0; off = 0;
    do {
        v = (unsigned int)award_grant_generic();
        idx = idx + 1;
        *(unsigned int *)((int)&DAT_805ce620 + off) = v;
        off = off + 4;
    } while (idx < 0x46);

    // 10 id-range-valid entries
    idx = 0; off = 0;
    do {
        v = award_validate_id_range(idx + 1);
        idx = idx + 1;
        *(unsigned int *)((int)&DAT_805ce738 + off) = v;
        off = off + 4;
    } while (idx < 10);

    // 7 award-by-id entries
    idx = 0; off = 0;
    do {
        v = award_check_by_id(idx);
        idx = idx + 1;
        *(unsigned int *)((int)&DAT_805ce760 + off) = v;
        off = off + 4;
    } while (idx < 7);

    // 16 modifier-slot entries
    idx = 0; off = 0;
    do {
        v = award_modifier_check_slot(idx);
        idx = idx + 1;
        *(unsigned int *)((int)&DAT_805ce77c + off) = v;
        off = off + 4;
    } while (idx < 0x10);

    FUN_80382730();
}
