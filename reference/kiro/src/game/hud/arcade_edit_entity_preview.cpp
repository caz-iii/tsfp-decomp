// Batch: batch_910 | Module: arcade map-editor entity/weather preview + weapon palette list
// Address range: 0x80322f2c - 0x80323a30
// Date: 2026-09-12
//
// The editor's live 3D preview of the currently-selected palette entry (spawns/updates a preview
// entity, handling the special weather entity 0x798 and the "twin" entities 0x46/0x47), plus the
// weapon/palette scroll-list builders that populate the editor's item picker.
//
//   aveditor_commit_selected_type   0x80322f2c  commit the selected type: set edit type + tool params.
//   aveditor_unpack_color3          0x80322f70  unpack a packed 0xRRGGBBxx color into 3 float [0..1].
//   aveditor_update_entity_preview  0x80322fdc  spawn/update the live preview entity for a type.
//   aveditor_tick_entity_preview    0x80323660  screen tick: update the preview when not closing.
//   aveditor_screen_close_if_done2  0x803236a4  screen tick: finalize when all children are closing.
//   aveditor_group_has_variant      0x803236d0  does a palette group contain any placeable variant?
//   aveditor_build_weapon_sublist   0x80323750  build one weapon/palette group sub-list (per entry).
//   aveditor_build_weapon_list_a    0x803238b8  build 4 (mode 1) or 2 sub-lists.
//   aveditor_build_weapon_list_b    0x80323980  build 3 (mode 1) or 2 sub-lists.
//   aveditor_build_weather_palette  0x80323a30  build the full weapon/weather palette list (by table).
//
// NON-MATCHING notes:
//  * 0x80322fdc / 0x80323750 / 0x80323a30 open with the GPR-save runtime helper
//    (bl 0x803826f0 / 0x803826d0 / 0x803826ec, restored by 0x8038273c / 0x8038271c / 0x80382738).
//    Ghidra folds the function's incoming register(s) into that helper's fake return and, for the
//    FP-heavy 0x80322fdc, models the saved f30/f31 as phantom in_ps*/in_f* locals. Recovered ABI:
//      - aveditor_update_entity_preview(int type)          [r3 = type index]
//      - aveditor_build_weapon_sublist(int *group)         [r3 = group record ptr]
//      - aveditor_build_weather_palette(int context)       [r3 = caller context ptr]
//    The phantom double param_2..8 / PS locals are dropped; the entity-spawn colour/scale values
//    are rebuilt from the float pool as in the original.
//  * 0x80322fdc's int->float 0x43300000 magic-double idioms are written as plain (float)(int); its
//    two "twin entity" spawn (0x46/0x47) and per-mesh scale loops are preserved.
//  * 0x80323660 reads the type from screen+0x188 and passes it to update_entity_preview; the
//    decompiler's forwarded param_1..8 are junk from the shared-helper modelling and are dropped.
//
// Named cross-module helpers are declared locally (arcade_edit_options.cpp convention) with their
// real ABI; only common.h is pulled for shared types/intrinsics. Raw FUN_/DAT_/FLOAT_ get
// correctly-typed per-file externs below.

#include "common.h"

extern "C" {

// -- editor context + preview state --
extern int   DAT_80612be4;    // editor context base (preview entity ptrs at +0x4150 / +0x4154)
extern int  *DAT_80612d68;    // preview panel state block
extern int   DAT_805d59a0;    // active palette variant index
extern int   DAT_806109c0;    // grid dimension A
extern int   DAT_806109c4;    // grid dimension B
extern int   DAT_806119e0;    // pos-y metric id

// -- palette entry tables --
extern int   DAT_80477bcc;    // palette entry color base (stride 0xc4)
extern int   DAT_80477bd4;    // palette entry value table (stride 0x31 ints; variant at +DAT_805d59a0*7)
extern int   DAT_80477c80;    // palette entry flags base (stride 0x31 ints; bits 1/2 = variant)
extern int   DAT_80477c84;    // palette entry tag base (stride 0xc4)
extern short DAT_80477bca;    // per-type category id: (&DAT_80477bca)[type*0x62]
extern int   DAT_80477860;    // per-category callback table: (&DAT_80477860)[cat*0x18]

// -- preview material template block (weather/plane instance) --
extern float DAT_805e2a3c;    // preview material scratch block base (0x54 bytes)
extern float DAT_805e2a40; extern float DAT_805e2a44; extern float DAT_805e2a48;
extern float DAT_805e2a4c; extern float DAT_805e2a50; extern float DAT_805e2a54;
extern float DAT_805e2a58; extern float DAT_805e2a5c; extern float DAT_805e2a60;
extern float DAT_805e2a64; extern float DAT_805e2a68; extern float DAT_805e2a6c;
extern float DAT_805e2a70; extern float DAT_805e2a74; extern float DAT_805e2a78;
extern float DAT_805e2a7c; extern float DAT_805e2a80; extern float DAT_805e2a84;

// -- spawn descriptor template --
extern int   DAT_803e0a78; extern int DAT_803e0a7c; extern int DAT_803e0a80;
extern int   DAT_803e0a84; extern int DAT_803e0a88; extern int DAT_803e0a8c;

// -- float pool constants --
extern float FLOAT_8061a894;  // 0.5
extern float FLOAT_8061a8a0;  // clamp ceiling
extern float FLOAT_8061a8a4;  // text pad
extern float FLOAT_8061a8c0;  // 0.0 / padding metric
extern float FLOAT_8061a8c8;  // preview grid cell divisor / initial scale
extern float FLOAT_8061a8cc;  // panel base X
extern float FLOAT_8061a8d0;  // panel width
extern float FLOAT_8061a8d4;  // panel gap / color scale
extern float FLOAT_8061a8dc;  // scroll overflow base
extern float FLOAT_8061a8e0;  // scroll term A
extern float FLOAT_8061a8e4;  // scroll term B
extern float FLOAT_8061a8e8;  // scroll term C
extern float FLOAT_8061a8f8;  // color denom (255)
extern float FLOAT_8061a8fc;  // material param
extern float FLOAT_8061a900;  // material param
extern float FLOAT_8061a904;  // material param
extern float FLOAT_8061a908;  // scale base
extern float FLOAT_8061a90c;  // scale offset
extern float FLOAT_8061a910;  // scale factor
extern float FLOAT_8061a914;  // list item metric X
extern float FLOAT_8061a918;  // list item metric Y
extern float FLOAT_80611a5c;  // layout scale
extern float FLOAT_80611a64;  // layout unit
extern float FLOAT_80611a6c;  // layout base

// -- raw / cross-module helpers (typed to match the call sites) --
extern void  aveditor_set_current_edit_type(int screen);         // batch_909
extern void  aveditor_set_tool_params_by_cat(void);              // batch_909
extern int   aveditor_get_palette_entry_value(int type);         // batch_908
extern int  *aveditor_get_loadout_palette_record(int index);     // batch_885
extern void  screen_exit_clear_mode(void);
extern int   hud_element_is_closing(int element);
extern int   hud_screen_all_children_closing(int screen);
extern void  weather_slot_destroy(int *slot);
extern void  entity_destroy_with_event(void *entity);
extern void  avanim_play_move_sound(int soundId);
extern int   FUN_80340b98(int a);
extern void  FUN_8033ced0(int a, int b, int c, int d);
extern void  FUN_8033cf64(int a, int b, unsigned int c, int d, int e);
extern void  FUN_8033cff4(void);
extern void  FUN_8033f224(void);
extern double FUN_8033ce4c(int type);
extern int   entity_spawn_with_activation(double a1, double a2, double a3, double a4, double a5,
                                          double a6, double a7, double a8, int flag, int type,
                                          int *descA, int *descB, int e, int f);
extern void  render_instance_set_material(double a, int *mesh, int b);
extern void  memset_wrapper(void *dst, int val, int n);
extern void  mtx4_set_scale(double x, double y, double z);
extern void  get_string_by_id(int stringId);
extern void  hud_begin_scroll_region(void);
extern void  hud_begin_sub_list(int a, int b, int c);
extern void  hud_add_item_internal(double x, double y);
extern void  hud_set_last_child_tag(int tag);
extern void  hud_end_sub_list(void);
extern void  hud_end_scroll_region(void);
extern void  hud_add_padding_item(double pad);
extern void  hud_element_init_from_layout(int layout, int a, int b, void (*cb)(void));
extern void  hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void  hud_set_context_property(int prop);
extern void  hud_element_scroll_h_overflow(double overflow, double a, double b, int c);
extern void  hud_element_apply_scroll(void);
extern void  hud_element_set_pos_y(double y, int metric);
extern void  hud_element_finalize_frame(void);

} // extern "C"

// forward prototype for this file's own function used before definition
void aveditor_build_weapon_sublist(int *group);
void aveditor_update_entity_preview(int type);

// -----------------------------------------------------------------------------

// Commit the currently-selected type (screen+0x188): apply it as the edit type, refresh the tool
// parameters, and leave clear mode.
void aveditor_commit_selected_type(int screen)
{
    if (*(int *)(screen + 0x188) >= 0) {
        aveditor_set_current_edit_type(screen);
        aveditor_set_tool_params_by_cat();
        screen_exit_clear_mode();
    }
}

// Unpack a packed color word (0xRRGGBBxx: R=bits 24-31, G=bits 8-15, B=bits 16-23) into three
// normalized floats in out[0..2], each divided by 255.
void aveditor_unpack_color3(unsigned int packed, float *out)
{
    float denom = FLOAT_8061a8f8;
    out[0] = (float)(packed >> 0x18)        / denom;
    out[1] = (float)(packed >> 0x10 & 0xff) / denom;
    out[2] = (float)(packed >> 0x08 & 0xff) / denom;
}

// Spawn or update the live 3D preview entity for the selected palette type. Out-of-range types tear
// down the existing preview entities; valid types (0..0x4f) (re)spawn a preview instance, wiring up
// its material template, and for the weather twins (0x46/0x47) a second companion entity. On a type
// change the panel geometry is rebuilt. Finally the preview meshes are scaled to the type's factor.
// (NON-MATCHING: GPR-save spill dropped; single int type recovered from r3.)
void aveditor_update_entity_preview(int type)
{
    int ctx = DAT_80612be4;

    if (type < 0 || type > 0x4f) {
        int ent = *(int *)(ctx + 0x4150);
        if (ent != 0) {
            if ((*(short *)(ent + 0x94) == 0x798) && (*(int **)(ent + 0x7c) != (int *)0x0)) {
                weather_slot_destroy(*(int **)(ent + 0x7c));
            }
            entity_destroy_with_event(*(void **)(ctx + 0x4150));
            *(int *)(ctx + 0x4150) = 0;
            if (*(void **)(ctx + 0x4154) != (void *)0x0) {
                entity_destroy_with_event(*(void **)(ctx + 0x4154));
                *(int *)(ctx + 0x4154) = 0;
            }
        }
        return;
    }

    *DAT_80612d68 = 1;
    DAT_80612d68[0x24] = -1;
    ((float *)DAT_80612d68)[0xe] = FLOAT_8061a8c8;
    DAT_80612d68[0x23] = 0;
    DAT_80612d68[0x21] = *(int *)(ctx + 0x4150);
    DAT_80612d68[0x20] = 0x244;

    if ((*(int *)(ctx + 0x4150) != 0) &&
        (*(unsigned short *)(*(int *)(ctx + 0x4150) + 0x94) !=
         (unsigned int)aveditor_get_palette_entry_value(type))) {
        DAT_80612d68[0x21] = 0;
    }
    if ((*(int *)(ctx + 0x4150) != 0) &&
        (*(short *)(*(int *)(ctx + 0x4150) + 0x94) != 0x798)) {
        FUN_80340b98(0);
    }

    if (type != DAT_80612d68[0x22]) {
        avanim_play_move_sound(0x157e);
        DAT_80612d68[0x22] = type;

        int ent = *(int *)(ctx + 0x4150);
        if (ent != 0) {
            int *slot;
            if ((*(short *)(ent + 0x94) == 0x798) &&
                (slot = *(int **)(ent + 0x7c), slot != (int *)0x0)) {
                weather_slot_destroy(slot);
                slot[5] = 0;
            }
            entity_destroy_with_event(*(void **)(ctx + 0x4150));
            *(int *)(ctx + 0x4150) = 0;
        }
        if (*(void **)(ctx + 0x4154) != (void *)0x0) {
            entity_destroy_with_event(*(void **)(ctx + 0x4154));
            *(int *)(ctx + 0x4154) = 0;
        }

        float baseX = FLOAT_8061a8cc * FLOAT_80611a64 + FLOAT_80611a6c;
        int halfB = (DAT_806109c4 >> 1) - 1;
        float span = (FLOAT_8061a8d0 * FLOAT_80611a64 + FLOAT_80611a6c) - baseX - FLOAT_8061a8d4;
        FUN_8033ced0((DAT_806109c4 - (halfB >> 1)) + 3,
                     (int)(FLOAT_8061a8a4 + span * FLOAT_8061a894 + baseX), 0xb4, (int)span);

        baseX = FLOAT_8061a8cc * FLOAT_80611a64 + FLOAT_80611a6c;
        halfB = (DAT_806109c4 >> 1) - 1;
        span = (FLOAT_8061a8d0 * FLOAT_80611a64 + FLOAT_80611a6c) - baseX - FLOAT_8061a8d4;
        FUN_8033cf64((DAT_806109c4 - (halfB >> 1)) + 3,
                     (int)(FLOAT_8061a8a4 + span * FLOAT_8061a894 + baseX),
                     (unsigned int)halfB, (int)span, (DAT_806109c0 >> 3));
    }

    if ((*(int *)(ctx + 0x4150) == 0) && (aveditor_get_palette_entry_value(type) != -1)) {
        int descA[3] = { DAT_803e0a78, DAT_803e0a7c, DAT_803e0a80 };
        int descB[3] = { DAT_803e0a84, DAT_803e0a88, DAT_803e0a8c };
        float col[3];

        int spawn = entity_spawn_with_activation(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1,
                                                 aveditor_get_palette_entry_value(type),
                                                 descA, descB, 0, 0);
        *(int *)(ctx + 0x4150) = spawn;
        render_instance_set_material((double)FLOAT_8061a8c0,
                                     *(int **)(*(int *)(ctx + 0x4150) + 4), 1);

        if (((&DAT_80477c80)[DAT_80612d68[0x22] * 0x31] & 6) == 0) {
            aveditor_unpack_color3(*(unsigned int *)(&DAT_80477bcc + DAT_80612d68[0x22] * 0xc4), col);
        } else {
            aveditor_unpack_color3(0x7f7f7f7f, col);
        }
        col[0] *= FLOAT_8061a8d4;
        col[1] *= FLOAT_8061a8d4;
        col[2] *= FLOAT_8061a8d4;

        *(float **)(*(int *)(ctx + 0x4150) + 0x254) = &DAT_805e2a3c;
        memset_wrapper(&DAT_805e2a3c, 0, 0x54);

        float sb = FLOAT_8061a908;
        DAT_805e2a3c = sb * col[0];
        DAT_805e2a40 = sb * col[1];
        DAT_805e2a44 = sb * col[2];
        DAT_805e2a48 = FLOAT_8061a910 * col[0] + FLOAT_8061a90c;
        DAT_805e2a6c = FLOAT_8061a8fc;
        DAT_805e2a70 = FLOAT_8061a8c0;
        DAT_805e2a74 = FLOAT_8061a8c0;
        DAT_805e2a78 = FLOAT_8061a900;
        DAT_805e2a7c = FLOAT_8061a8c0;
        DAT_805e2a80 = FLOAT_8061a8c0;
        DAT_805e2a84 = FLOAT_8061a904;
        if (DAT_805e2a48 > FLOAT_8061a8a0) DAT_805e2a48 = FLOAT_8061a8a0;

        float g = FLOAT_8061a910 * col[1] + FLOAT_8061a90c;
        if (g > FLOAT_8061a8a0) g = FLOAT_8061a8a0;
        DAT_805e2a4c = g;
        float b = FLOAT_8061a910 * col[2] + FLOAT_8061a90c;
        if (b > FLOAT_8061a8a0) b = FLOAT_8061a8a0;
        DAT_805e2a50 = b;
        DAT_805e2a54 = FLOAT_8061a8c0;
        DAT_805e2a58 = FLOAT_8061a8c0;
        DAT_805e2a5c = FLOAT_8061a8c0;
        DAT_805e2a60 = FLOAT_8061a8c0;
        DAT_805e2a64 = FLOAT_8061a8c0;
        DAT_805e2a68 = FLOAT_8061a8c0;

        if ((type == 0x46) || (type == 0x47)) {
            int twinType = *(int *)(type * 0xc4 + DAT_805d59a0 * 0x1c + (int)0x80477bd8);
            int twin = entity_spawn_with_activation(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1,
                                                    twinType, descA, descB, 0, 0);
            *(int *)(ctx + 0x4154) = twin;
            render_instance_set_material((double)FLOAT_8061a8c0,
                                         *(int **)(*(int *)(ctx + 0x4154) + 4), 1);
            *(float **)(*(int *)(ctx + 0x4154) + 0x254) = &DAT_805e2a3c;
        }
    }

    // scale the primary preview meshes to the type's scale factor
    if ((*(int *)(ctx + 0x4150) != 0) &&
        ((*(unsigned int *)(*(int *)(ctx + 0x4150) + 0x28) & 0x100) == 0)) {
        float base = FLOAT_8061a8c8;
        if ((unsigned)(type - 0x11) < 6) {
            int *rec = aveditor_get_loadout_palette_record(type - 0x11);
            base = ((float *)rec)[4];
        }
        float scale = base * (float)FUN_8033ce4c(type);
        int meshCount = *(int *)(*(int *)(*(int *)(*(int *)(ctx + 0x4150) + 4) + 4) + 4);
        for (int i = 0; i < meshCount; i++) {
            mtx4_set_scale((double)scale, (double)scale, (double)scale);
        }
    }
    // scale the twin preview meshes
    if ((*(int *)(ctx + 0x4154) != 0) &&
        ((*(unsigned int *)(*(int *)(ctx + 0x4154) + 0x28) & 0x100) == 0)) {
        float scale = FLOAT_8061a8c8 * (float)FUN_8033ce4c(type);
        int meshCount = *(int *)(*(int *)(*(int *)(*(int *)(ctx + 0x4154) + 4) + 4) + 4);
        for (int i = 0; i < meshCount; i++) {
            mtx4_set_scale((double)scale, (double)scale, (double)scale);
        }
    }

    FUN_8033cff4();
}

// Screen tick: while the screen isn't closing and a valid type is selected (screen+0x188), refresh
// the live preview entity. (NON-MATCHING: forwarded phantom float args dropped.)
void aveditor_tick_entity_preview(int screen)
{
    if (hud_element_is_closing(screen) != 0) {
        return;
    }
    int type = *(int *)(screen + 0x188);
    if (type >= 0) {
        aveditor_update_entity_preview(type);
    }
}

// Screen tick: finalize/tear down when all children are closing.
void aveditor_screen_close_if_done2(int screen)
{
    if (hud_screen_all_children_closing(screen) != 0) {
        FUN_8033f224();
    }
}

// Does a palette group contain at least one placeable variant entry? A group is {?, int *ids,
// int count} (ids at +4, count at +8). An entry qualifies if its flags carry bit 4, or its
// variant value-table slot is not -1.
int aveditor_group_has_variant(int *group)
{
    int count = group[2];
    int off = 0;
    if (count > 0) {
        do {
            int id = *(int *)(group[1] + off);
            unsigned int lz = countLeadingZeros((&DAT_80477c80)[id * 0x31]);
            if ((lz >> 5 & 4) != 0) {
                return 1;
            }
            if ((&DAT_80477bd4)[id * 0x31 + DAT_805d59a0 * 7] != -1) {
                return 1;
            }
            off += 4;
            count--;
        } while (count != 0);
    }
    return 0;
}

// Build one weapon/palette group sub-list: emits the group label, then one HUD item per placeable
// variant entry (running its per-category init callback and tagging the item with its palette tag).
// Skipped entirely if the group has no placeable variant.
// (NON-MATCHING: GPR-save spill dropped; the group record pointer is recovered from r3.)
void aveditor_build_weapon_sublist(int *group)
{
    if (aveditor_group_has_variant(group) == 0) {
        return;
    }

    get_string_by_id(group[0]);
    hud_begin_scroll_region();
    hud_begin_sub_list(1, 1, 5 - (int)(*(int *)(DAT_80612be4 + 0x254) == 4));

    int off = 0;
    for (int i = 0; i < group[2]; i++) {
        int id = *(int *)(group[1] + off);
        typedef void (*cat_cb)(void);
        cat_cb cb = *(cat_cb *)(&DAT_80477860 + (short)(&DAT_80477bca)[id * 0x62] * 0x18);
        if (cb != (cat_cb)0) {
            cb();
        }
        if ((&DAT_80477bd4)[DAT_805d59a0 * 7 + id * 0x31] != -1) {
            hud_add_item_internal((double)FLOAT_8061a914, (double)FLOAT_8061a918);
            hud_set_last_child_tag(*(int *)(&DAT_80477c84 + id * 0xc4));
        }
        off += 4;
    }

    hud_end_sub_list();
    hud_end_scroll_region();
    hud_add_padding_item((double)FLOAT_8061a8c0);
}

// Build the weapon sub-lists for list variant A: 4 groups in mode 1, 2 groups otherwise.
// (The original passes each group record; the group pointers are supplied by the caller's table.)
void aveditor_build_weapon_list_a(int mode, int *groups)
{
    int count = 0;
    if (mode == 1) {
        count = 4;
    } else if (mode >= 0 && mode < 1) {
        count = 2;
    }
    for (int i = 0; i < count; i++) {
        aveditor_build_weapon_sublist(groups + i * 3);
    }
}

// Build the weapon sub-lists for list variant B: 3 groups in mode 1, 2 groups otherwise.
void aveditor_build_weapon_list_b(int mode, int *groups)
{
    int count = 0;
    if (mode == 1) {
        count = 3;
    } else if (mode >= 0 && mode < 1) {
        count = 2;
    }
    for (int i = 0; i < count; i++) {
        aveditor_build_weapon_sublist(groups + i * 3);
    }
}

// Build the full weapon/weather palette scroll list: selects one of three group tables from the
// caller's context (context+0x4 layout id -> the weather table, else editor mode at +0x200 picks
// the 9- or 0x11-group table) and emits every group's sub-list, wrapped in scroll setup/teardown.
// (NON-MATCHING: GPR-save spill dropped; the context pointer is recovered from r3. The group table
// bases are the loader-relocated pointers the original reads via lis/addi.)
void aveditor_build_weather_palette(int context)
{
    hud_element_init_from_layout(0x840c, 0, 0, screen_exit_clear_mode);
    hud_set_item_padding_mode((double)FLOAT_8061a8c0, 0, 1, 1);
    hud_set_context_property(0x28000);

    float scrollTerms = (FLOAT_8061a8e0 * FLOAT_80611a64 + FLOAT_8061a8e4 * FLOAT_80611a64 +
                         FLOAT_8061a8e8 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c;
    hud_element_scroll_h_overflow(-(double)(FLOAT_80611a5c * scrollTerms - FLOAT_8061a8dc),
                                  (double)FLOAT_8061a8c0, (double)FLOAT_8061a8c0, 0);

    // group table base + count selected by the caller context / editor mode
    int layoutId = *(int *)(context + 4);
    int *table;
    int groupCount;
    if (layoutId == (int)0x80480c4c) {          // weather layout id (relocated pointer compare)
        table = (int *)0x804807e0;              // weather group table (+0x344 region)
        groupCount = 6;
    } else if (*(int *)(DAT_80612be4 + 0x200) != 0) {
        table = (int *)0x804807e0;              // mode-1 weapon table (+0x20c region)
        groupCount = 0x11;
    } else {
        table = (int *)0x804807e0;              // mode-0 weapon table (+0x2d8 region)
        groupCount = 9;
    }

    for (int i = 0; i < groupCount; i++) {
        aveditor_build_weapon_sublist(table + i * 3);
    }

    hud_element_apply_scroll();
    hud_element_set_pos_y((double)(FLOAT_80611a5c * scrollTerms), DAT_806119e0);
    hud_element_finalize_frame();
}
