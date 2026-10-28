// Batch: batch_883 | Module: arcade map-editor screen build + cell/roster lookup
// Address range: 0x80312248 - 0x80312d94
// Date: 2026-09-11
//
// aveditor_build_screen is the arcade map-editor's full construction routine, called from
// session_setup_by_mode. It allocates the editor context and every pool (model pool, the
// 0x290 aux block DAT_80612cac, sprite/scene spatial trees, the HUD element pool, and the
// 0x12000/0x7c40/0x1b0 working buffers), builds the whole HUD style table (fonts, colors,
// spacing, elements), creates the root viewport, and then either deserializes the pending
// map (when the load buffer DAT_80612be8 begins with the 0xc77667 header tag) or loads the
// default palette. The remaining functions are the cell/roster lookup + selection helpers
// used across the editor, plus the post-delete reindexer.
//
//   aveditor_build_screen          0x80312248  full editor construction (pools + HUD + load).
//   aveditor_clear_pool_pointers   0x8031296c  null the editor pool globals on teardown.
//   aveditor_cell_type_is_selectable 0x80312988 predicate: cell type/subtype is selectable.
//   aveditor_build_cell_tooltip    0x803129c8  format the current cell's door/key/lock tooltip.
//   aveditor_is_selected_cell      0x80312bf0  id == selected cell and a cell selection active.
//   aveditor_find_cell_by_id       0x80312c1c  find (member,cell) owning a cell id.
//   aveditor_find_roster_slot      0x80312cb0  roster slot index by layout id, or -1.
//   aveditor_is_selected_roster    0x80312cf8  id == selected id and selection is roster (0x46).
//   aveditor_is_cell_selected      0x80312d24  combined cell/roster selection predicate.
//   aveditor_reindex_after_delete  0x80312d94  decrement/invalidate ids after an entry removal.
//
// NON-MATCHING: the decompiler renders aveditor_build_screen's dozens of allocator/HUD-style
// calls with the full PPC ABI spill list (8 FPR doubles + 8 GPR words) chained through every
// call's return; the real prototypes (via generated_decls.h) take only their true arguments,
// so those phantom pass-through params are dropped. hud_viewport_create_root / a few style
// setters are typed `void` in generated_decls.h but the builder stores their register return
// (e.g. DAT_80612bf4) - those are invoked through a typed function-pointer cast at the call
// site per Compile-Clean rule 6 rather than re-declaring. The FLOAT_* HUD-style constants are
// passed straight through as floats. Named helpers resolve via generated_decls.h; every raw
// FUN_/DAT_/FLOAT_ symbol gets a per-file extern below.

#include "common.h"
#include "hud.h"

extern "C" {

// editor context + pool globals
extern int   DAT_80612be4;    // editor context block
extern int*  DAT_80612be8;    // load/serialize buffer (first word = chunk tag)
extern int   DAT_80612ae8;
extern int   DAT_80612cac;    // 0x290 aux block
extern int   DAT_80612c00;
extern int   DAT_80612c0c;
extern int   DAT_80612c14;    // member list head
extern int   DAT_80612c18;
extern int   DAT_80612c30;
extern int   DAT_80612bf4;    // root viewport handle
extern unsigned int DAT_805d59dc; // config touched flag
extern float FLOAT_80612bf0;  // editor fade level

// select-list source data
extern int   DAT_805d5dbc;
extern int   DAT_805d59e0;

// HUD style layout constants
extern float FLOAT_8061a600, FLOAT_8061a604, FLOAT_8061a60c, FLOAT_8061a614;
extern float FLOAT_8061a644, FLOAT_8061a664, FLOAT_8061a668, FLOAT_8061a678;
extern float FLOAT_8061a69c, FLOAT_8061a6a4, FLOAT_8061a6a8, FLOAT_8061a6ac;
extern float FLOAT_8061a6b0, FLOAT_8061a6b4, FLOAT_8061a6b8, FLOAT_8061a6bc;
extern float FLOAT_8061a6c0, FLOAT_8061a6c4, FLOAT_8061a6c8, FLOAT_8061a6cc, FLOAT_8061a6d0;
extern float FLOAT_80611a64, FLOAT_80611a68, FLOAT_80611a6c, FLOAT_80611a70;

// scratch tooltip buffer + format strings
extern unsigned char s__s__s__s_8047fb28;
extern unsigned char DAT_80611184;

// raw FUN_ helpers (not in generated_decls.h)
int  FUN_803826f0(void);       // hud accessor push (active type)
void FUN_8038273c(void);       // hud accessor pop
int  FUN_803490b0(void);       // sub-init (model prep)
int  FUN_8031df2c(void);       // scene build helper
void FUN_80339f20(void);       // scene finalize
void FUN_80313888(int poolPtr, int mode); // init a pool
int  FUN_8033c7e8(void);       // post-reset hook
void FUN_80314480(void);       // subsystem init A
void FUN_803144fc(void);       // subsystem init B
void FUN_80314934(void);       // subsystem init C
int  FUN_80315130(void);       // recompute layout
void FUN_80315044(void);       // post-load layout
void FUN_80345504(void);       // finalize build
void FUN_8031cba4(int a, int b);// begin transition
int  FUN_80346e60(void);       // post-build hook A
void FUN_80320a44(void);       // post-build hook B
u32  FUN_8032b594(void);       // font animation callback
u32  FUN_8031a0d0(void);       // truncation callback
void FUN_80387fb8(int dst, unsigned char* fmt, int a, int b, int c); // sprintf-style

} // extern "C"

// get_string_by_id and this batch's own functions (aveditor_find_cell_by_id,
// aveditor_find_roster_slot, aveditor_is_selected_roster/_cell) are declared in
// generated_decls.h (via hud.h) at C++ linkage and must NOT be re-declared here.

// hud_viewport_create_root is typed void() in generated_decls.h but the builder stores its
// register return into DAT_80612bf4; invoke it through a typed cast (Compile-Clean rule 6).
static inline int viewport_create_root_ret(int cb, int b, int c)
{
    return ((int(*)(int,int,int))(void*)hud_viewport_create_root)(cb, b, c);
}

// ---------------------------------------------------------------------------
// 0x80312248  aveditor_build_screen
// ---------------------------------------------------------------------------
void aveditor_build_screen(void)
{
    FUN_803826f0();
    unsigned int savedTouched = DAT_805d59dc;
    DAT_80612ae8 = 0;

    aveditor_alloc_context();
    aveditor_enable_preview_mode();
    aveditor_reset_config_fields();
    FUN_803490b0();
    aveditor_create_model_pool(0, 0, 0, 0, 0, 0, 0, 0);

    DAT_80612cac = mem_alloc(0x290, 1);
    memset_wrapper(DAT_80612cac, 0, 0x28c);
    scene_build_spatial_tree();
    FUN_8031df2c();
    FUN_80339f20();
    sprite_finalize_load();
    hud_alloc_element_pool(0);
    hud_system_init();

    // ---- HUD style: font 0 (default body) ----
    hud_style_create_font(0, -1);
    hud_style_font_set_resource(0x2580);
    hud_style_font_set_sizes(0, 0);
    hud_style_font_set_scale(FLOAT_8061a678, FLOAT_8061a678);
    hud_style_font_set_color(FLOAT_8061a69c, FLOAT_8061a6a4, FLOAT_8061a69c, FLOAT_8061a6a4);
    hud_style_font_set_shadow_color(FLOAT_8061a604, FLOAT_8061a60c, FLOAT_8061a6a8, FLOAT_8061a614);
    hud_style_font_set_style(0x12, 3, 4);
    hud_style_font_set_margins(FLOAT_8061a6a8, FLOAT_8061a6a4, FLOAT_8061a664, FLOAT_8061a664, FLOAT_8061a664);
    hud_style_font_set_bounds(FLOAT_8061a6ac, FLOAT_8061a6b0, 1, 0);
    hud_style_font_set_wrap_mode((int)aveditor_check_add_allowed);
    hud_style_font_set_truncation((int)FUN_8031a0d0);

    // ---- font 2 (from 0) ----
    hud_style_create_font(2, 0);
    hud_style_font_set_resource(0x2380);
    hud_style_font_set_sizes(0, 0);
    hud_style_font_set_color(FLOAT_8061a678, FLOAT_8061a69c, FLOAT_8061a6a4, FLOAT_8061a69c);
    hud_style_font_set_shadow_color(FLOAT_8061a604, FLOAT_8061a60c, FLOAT_8061a6a8, FLOAT_8061a614);
    hud_style_font_set_bounds(FLOAT_8061a6b4, FLOAT_8061a6b8, 0, 0);

    // ---- font 1 (from 2) ----
    hud_style_create_font(1, 2);
    hud_style_font_set_sizes(1, 1);
    hud_style_font_set_color(FLOAT_8061a6a4, FLOAT_8061a69c, FLOAT_8061a69c, FLOAT_8061a69c);
    hud_style_font_set_bounds(FLOAT_8061a6b4, FLOAT_8061a6b8, 1, 0);

    // ---- font 3 (from 0) ----
    hud_style_create_font(3, 0);
    hud_style_font_set_resource(0x2180);
    hud_style_font_set_shadow_color(FLOAT_8061a604, FLOAT_8061a60c, FLOAT_8061a600, FLOAT_8061a604);
    hud_style_font_set_color(FLOAT_8061a604, FLOAT_8061a614, FLOAT_8061a604, FLOAT_8061a614);
    hud_style_font_set_bounds(FLOAT_8061a6bc, FLOAT_8061a604, 1, 0);
    hud_style_font_set_animation(0, 0, 0, 0, 0, 0);

    // ---- font 4 (from 3) then 5 (from 0) ----
    hud_style_create_font(4, 3);
    hud_style_create_font(5, 0);
    hud_style_font_set_resource(0x2580);
    hud_style_font_set_sizes(2, 2);
    hud_style_font_set_shadow_color(FLOAT_8061a604, FLOAT_8061a60c, FLOAT_8061a600, FLOAT_8061a604);
    hud_style_font_set_color(FLOAT_8061a69c, FLOAT_8061a6a4, FLOAT_8061a69c, FLOAT_8061a6a4);

    // ---- font 6 (from 5) then 7 (from 0) ----
    hud_style_create_font(6, 5);
    hud_style_font_set_sizes(2, 2);
    hud_style_font_set_shadow_color(FLOAT_8061a604, FLOAT_8061a60c, FLOAT_8061a600, FLOAT_8061a604);
    hud_style_font_set_color(FLOAT_8061a69c, FLOAT_8061a6a4, FLOAT_8061a69c, FLOAT_8061a6a4);
    hud_style_create_font(7, 0);
    hud_style_font_set_color(FLOAT_8061a644, FLOAT_8061a614, FLOAT_8061a614, FLOAT_8061a614);
    hud_style_font_set_bounds(FLOAT_8061a6c0, FLOAT_8061a6a4, 0, 0);
    hud_style_font_set_animation(0, (int)FUN_8032b594, 0, 0, 0, 0);

    // ---- colors ----
    hud_style_create_color(0, -1);
    hud_style_color_set_rgba(FLOAT_8061a668, FLOAT_8061a668, FLOAT_8061a668, FLOAT_8061a668);

    // ---- spacing 0 ----
    hud_style_create_spacing(0, -1);
    hud_style_spacing_set_margin(0, 0x696e737f, 0x1414147f);
    hud_style_spacing_set_margin(1, 0x696e737f, 0x46323232);
    hud_style_spacing_set_margin(2, 0x696e737f, 0x46323232);
    hud_style_spacing_set_margin(3, 0xa0a0a78, 0x1e141450);
    hud_style_spacing_set_margin(4, 0xa0a0a50, 0x3c28281e);
    hud_style_spacing_set_padding(0, 0x1e2d4646);
    hud_style_spacing_set_padding(1, 0x2d4b697f);
    hud_style_spacing_set_padding(2, 0x69737f7f);
    hud_style_spacing_set_padding(3, 0x1e283237);
    hud_style_spacing_set_gap(3, 0x41);
    hud_style_spacing_set_gap(0, 0x7f7f7f7f);
    hud_style_spacing_set_gap(1, 0xf1e3250);
    hud_style_spacing_set_content_rect(0x23252a7f, 0x1c242f7f, 0x5030a7f, 0xc08107f);
    hud_style_spacing_set_transition(FLOAT_8061a6c4, 0x414b5a7f, 0x414b647f, 0x2828377f, 0x2828377f, 0x7f);
    hud_style_spacing_set_scroll(FLOAT_8061a6c8, 0x484a5a7f, 0x1a1b227f);
    hud_style_spacing_set_alignment(0x32283700, 0x41415000, 0x19141900);

    // ---- spacing 1 & 2 ----
    hud_style_create_spacing(1, 0);
    hud_style_spacing_set_content_rect(0x34343c7f, 0x34343c7f, 0x1a1b227f, 0x1a1b227f);
    hud_style_spacing_set_transition(FLOAT_8061a6c8, 0x42464c7f, 0x42464c7f, 0x34363e7f, 0x34363e7f, 0x7f);
    hud_style_create_spacing(2, 0);

    // ---- elements ----
    hud_style_create_element(0, -1);
    hud_style_element_set_variants(0, 0, 0);
    hud_style_element_set_variants(1, 1, 0);
    hud_style_element_set_variants(2, 2, 0);
    hud_style_element_set_variants(3, 3, 1);
    hud_style_element_set_variants(4, 4, 1);
    hud_style_element_set_variants(5, 5, 0);
    hud_style_element_set_variants(6, 6, 0);
    hud_style_element_set_color(0);
    hud_style_create_element(1, 0);
    hud_style_element_set_variants(0, 7, 2);
    hud_style_element_set_color(0);
    hud_style_reset_counters();
    hud_style_push_element(0);

    // ---- viewport + working buffers ----
    select_list_init_empty((u32*)&DAT_805d5dbc, 0);
    select_list_init_with_data(&DAT_805d59e0, 1, (int*)0x804879a0 /* -0x7fb80664 */, 5);
    DAT_80612bf4 = viewport_create_root(0, (int)&DAT_805d5dbc, (int)&DAT_805d59e0);
    hud_viewport_enable();
    DAT_80612c0c = mem_alloc(0x12000, 1);
    DAT_80612c00 = mem_alloc(0x7c40, 1);
    DAT_80612c30 = mem_alloc(0x1b0, 1);
    FUN_80313888(DAT_80612c30, 1);

    asel_reset_screen(0);
    FLOAT_80612bf0 = FLOAT_8061a604;
    FUN_8033c7e8();
    FUN_80314480();
    FUN_803144fc();
    FUN_80314934();
    FUN_80315130();

    *(u32*)(DAT_80612be4 + 0x25c) = *(u32*)(DAT_80612be4 + 0x25c) & 0x200;
    if (*DAT_80612be8 == 0xc77667) {
        aveditor_deserialize_map();
        aveditor_load_default_palette_if_missing(DAT_80612be8);
        FUN_80315130();
        FUN_80315044();
        *(u32*)(DAT_80612be4 + 0x25c) = *(u32*)(DAT_80612be4 + 0x25c) & 0xfffffdff;
    } else {
        aveditor_init_default_palette();
    }
    FUN_80345504();
    FUN_8031cba4(-1, -1);
    text_set_font_table(0x15);
    player_set_active_count(8);
    FUN_80346e60();
    FUN_80320a44();
    hud_viewport_update_scroll_pos();

    float scissor[4];
    scissor[0] = FLOAT_80611a70 + FLOAT_80611a68;
    scissor[1] = FLOAT_80611a6c;
    scissor[2] = FLOAT_8061a6cc * FLOAT_80611a68 + FLOAT_80611a70;
    scissor[3] = FLOAT_8061a6d0 * FLOAT_80611a64 + FLOAT_80611a6c;
    hud_viewport_set_scissor_rect(scissor);
    FUN_8031cba4(-1, -1);
    DAT_805d59dc = savedTouched;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8031296c  aveditor_clear_pool_pointers
// ---------------------------------------------------------------------------
void aveditor_clear_pool_pointers(void)
{
    DAT_80612c0c = 0;
    DAT_80612c00 = 0;
    DAT_80612c30 = 0;
    DAT_80612c14 = 0;
    DAT_80612c18 = 0;
}

// ---------------------------------------------------------------------------
// 0x80312988  aveditor_cell_type_is_selectable
// ---------------------------------------------------------------------------
unsigned int aveditor_cell_type_is_selectable(int record)
{
    int type = *(int*)(record + 0x84);
    if ((type != 0x46) && ((0x45 < type || (0x37 < type)) || (type < 0x35))) {
        return countLeadingZeros(0xb - *(int*)(record + 0x80)) >> 5;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x803129c8  aveditor_build_cell_tooltip
// ---------------------------------------------------------------------------
void aveditor_build_cell_tooltip(void)
{
    int row = FUN_803826f0();
    unsigned char* scratch = (unsigned char*)0x80585fb4; // -0x7fa2a04c
    memset_wrapper((int)scratch, 0, 0x80);

    int kind = *(int*)(DAT_80612be4 + row * 0x8c + 0xdd98);
    if (kind == 0x46) {
        int slot = aveditor_find_roster_slot(row);
        if (slot != -1) {
            int base = slot * 0xc;
            unsigned char flags = *(unsigned char*)(DAT_80612be4 + base + 0x3079);
            int s1 = get_string_by_id((flags & 1) == 0 ? 0x9a2 : 0x9a3);
            int s2 = get_string_by_id((flags & 8) == 0 ? 0x9a5 : 0x9a4);
            int s3 = get_string_by_id((flags & 4) == 0 ? 0x9a7 : 0x9a6);
            FUN_80387fb8((int)scratch, &s__s__s__s_8047fb28, s1, s2, s3);
            FUN_8038273c();
            return;
        }
    } else if ((kind < 0x46) && (kind < 0x38) && (0x34 < kind)) {
        int rec = aveditor_find_cell_by_id((int*)0, row);
        if (rec != 0) {
            unsigned int f = *(unsigned int*)(rec + 0x2c);
            int s1 = get_string_by_id((f & 0x80) == 0 ? 0x98d : 0x98e);
            int s2 = get_string_by_id((f & 8) == 0 ? 0x990 : 0x98f);
            FUN_80387fb8((int)scratch, &DAT_80611184, s1, s2, 0);
            FUN_8038273c();
            return;
        }
    }
    if ((*(int*)(DAT_80612be4 + row * 0x8c + 0xdd94) == 0xb) &&
        (aveditor_find_roster_slot(row) != -1)) {
        int slot = aveditor_find_roster_slot(row);
        unsigned char flags = *(unsigned char*)(DAT_80612be4 + slot * 0xc + 0x3079);
        get_string_by_id((flags & 2) == 0 ? 0x9a3 : 0x9a2);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80312bf0  aveditor_is_selected_cell
// ---------------------------------------------------------------------------
unsigned int aveditor_is_selected_cell(int id)
{
    if ((id == *(int*)(DAT_80612be4 + 0x2f38)) && (*(int*)(DAT_80612be4 + 0x2f34) != -1)) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80312c1c  aveditor_find_cell_by_id
// ---------------------------------------------------------------------------
int aveditor_find_cell_by_id(int* outMember, int cellId)
{
    int member = DAT_80612c14;
    while (true) {
        if (member == 0) {
            if ((*(int*)(DAT_80612be4 + 0x2f34) == 0x46) || (aveditor_is_selected_cell(cellId) == 0)) {
                return 0;
            }
            return DAT_80612be4 + 0x2f34;
        }
        for (int cell = *(int*)(member + 0x3c); cell != 0; cell = *(int*)(cell + 0x14)) {
            if (cellId == *(int*)(cell + 4)) {
                if (outMember != nullptr) {
                    *outMember = member;
                }
                return cell;
            }
        }
        member = *(int*)(member + 0x40);
    }
}

// ---------------------------------------------------------------------------
// 0x80312cb0  aveditor_find_roster_slot
// ---------------------------------------------------------------------------
int aveditor_find_roster_slot(int layoutId)
{
    int index = 0;
    int off = 0;
    unsigned int remaining = (unsigned int)*(unsigned short*)(DAT_80612be4 + 0x306e);
    while (true) {
        if (remaining == 0) {
            return -1;
        }
        if (*(short*)(DAT_80612be4 + off + 0x307a) == layoutId) {
            break;
        }
        index = index + 1;
        off = off + 0xc;
        remaining = remaining - 1;
    }
    return index;
}

// ---------------------------------------------------------------------------
// 0x80312cf8  aveditor_is_selected_roster
// ---------------------------------------------------------------------------
unsigned int aveditor_is_selected_roster(int id)
{
    if ((id == *(int*)(DAT_80612be4 + 0x2f38)) && (*(int*)(DAT_80612be4 + 0x2f34) == 0x46)) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80312d24  aveditor_is_cell_selected
// ---------------------------------------------------------------------------
unsigned int aveditor_is_cell_selected(int id)
{
    if (aveditor_find_cell_by_id((int*)0, id) != 0) {
        return 1;
    }
    if ((aveditor_find_roster_slot(id) < 0) && (aveditor_is_selected_roster(id) == 0)) {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x80312d94  aveditor_reindex_after_delete
// ---------------------------------------------------------------------------
void aveditor_reindex_after_delete(int removedId)
{
    for (int member = DAT_80612c14; member != 0; member = *(int*)(member + 0x40)) {
        for (int cell = *(int*)(member + 0x3c); cell != 0; cell = *(int*)(cell + 0x14)) {
            int id = *(int*)(cell + 4);
            if (removedId < id) {
                *(int*)(cell + 4) = id - 1;
            } else if (id == removedId) {
                *(int*)(cell + 4) = -1;
            }
        }
    }
    int off = 0;
    for (int i = 0; i < (int)(unsigned int)*(unsigned short*)(DAT_80612be4 + 0x306e); i = i + 1) {
        int slot = DAT_80612be4 + off + 0x3070;
        short id = *(short*)(slot + 10);
        if (removedId < id) {
            *(short*)(slot + 10) = id - 1;
        } else if (id == removedId) {
            *(short*)(slot + 10) = -1;
        }
        off = off + 0xc;
    }
}
