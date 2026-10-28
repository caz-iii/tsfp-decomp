// Batch: batch_982 | Module: arcade editor card-slot list + preview actor setup
// Address range: 0x80348ca8 - 0x803490b0
// Date: 2026-09-12
//
// This block sits just after the record-serialization family (batch_981) and covers the arcade card
// browse screen plus the preview-actor plumbing the editor uses to instance level actors for the
// previews:
//
//   arcade_edit_load_card_strings  0x80348ca8  Load the current card's header strings: resolves the
//                                              card slot record, seeds an id cursor, then calls the
//                                              three string writers through the
//                                              PTR_challenge_write_header_strings_80481c1c table and
//                                              clears the pending action. Called from
//                                              arcade_edit_load_slot_record.
//   arcade_build_card_slot_list    0x80348d24  Build the 10-entry scrolling card-slot list HUD
//                                              element (caption 0x959), one hud_add_list_item per
//                                              arcade_edit_slot_record.
//   aveditor_screen_push           0x80348e64  Thin hud_screen_push wrapper.
//   aveditor_rebalance_node_order  0x80348e90  Walk the node table, find the lowest-order node whose
//                                              mode config allows reordering, decrement each node's
//                                              order key by its multiplier, then compact the node
//                                              scratch by the winning node's byte span.
//   aveditor_preload_level_entities 0x80348f88 Preload entity resources for the preview level: for
//                                              level 0 only, preload 0x1d entity ids from the level's
//                                              base id; iterate 6 level defs.
//   arcade_set_sound_bank          0x80348ff0  Clear the previous sound-bank ref and latch the new
//                                              bank id into DAT_80612dac.
//   arcade_actor_table_ptr         0x80349020  Return the preview-actor descriptor table (&DAT_80481f60, stride 0x18).
//   arcade_actor_table_field4      0x8034902c  Read word at +4 of table entry index.
//   arcade_actor_switch_by_index   0x80349068  level_actor_switch on the table entry's actor id.
//   arcade_actor_pool_init         0x803490b0  Allocate the actor tracking buffer, register the
//                                              table's 6 actors and set up the level resource pool.
//
// NON-MATCHING: reproduces the decompiler's structure. Named cross-module helpers are called through
// their canonical (shared-header) prototypes rather than the decompiler's ABI-widened 16-arg tails,
// since those extra register args are Ghidra artifacts, not real parameters (Compile-Clean rule 1/6).
// Raw FUN_/DAT_/FLOAT_ symbols are declared file-locally with call-site arity/type (rule 5).

#include "common.h"

extern "C" {

// ---- named cross-module helpers (canonical prototypes via shared headers) --
extern int  arcade_edit_slot_record(int index);
extern void arcade_set_pending_action(int action);
extern int  get_string_by_id(int id);
extern void hud_element_init_from_layout(int flags, void* data, int cancel_cb, int confirm_cb);
extern void hud_set_item_padding_mode(float amount, int mode, int set_before, int set_after);
extern void hud_set_context_property(int prop);
extern void hud_element_scroll_h_overflow(int direction, float speed_x, float speed_y, int param);
extern int* hud_add_callback_item(float height);
extern void hud_add_list_item(void);
extern void hud_element_apply_scroll(void);
extern void hud_element_finalize_frame(void);
extern void hud_element_set_pos_y(double y, int element);
extern void hud_screen_push(void);
extern unsigned char* aveditor_get_node_scratch(void);
extern unsigned int* match_mode_get_config(int mode);
extern int  aveditor_node_multiplier(int cell);
extern int  aveditor_total_nodes_for_mode(int type);
extern int  aveditor_node_bytes(int type, int count);
extern void leaf_31e8(unsigned int dst, unsigned int src, int len);
extern int  level_def_preload_base_id(int levelId);
extern void entity_preload_resources(int entity_type_id);
extern void sound_clear_bank_ref(int bank);
extern int  level_actor_switch(int nTargetActorId);
extern void level_actors_register(int nBank, int nCount);
extern void spawn_alloc_tracking_buffer(void);
extern void level_resource_pool_setup(void);

// ---- raw callees (file-local, call-site arity/type) ------------------------
extern int   FUN_803826f0(void);                      // editor push guard, returns screen id
extern int   FUN_803826e4(void);                      // editor context pointer
extern void  FUN_80382730(void);                      // pop guard
extern void  FUN_8038273c(void);                      // pop guard

// ---- data symbols ----------------------------------------------------------
extern void (*PTR_challenge_write_header_strings_80481c1c)(int record, int* id_cursor);  // 3-entry table
extern int   DAT_80612be8;    // challenge record base
extern int   DAT_80612be4;    // editor context base
extern int   DAT_80612dac;    // latched sound bank id
extern int   DAT_806119e0;    // list element handle
extern unsigned char DAT_80481f60[];   // preview-actor descriptor table (stride 0x18)
extern float FLOAT_8061ace4;
extern float FLOAT_8061ace8;
extern float FLOAT_8061acec;
extern float FLOAT_8061acf0;
extern float FLOAT_8061acf4;
extern float FLOAT_8061acf8;
extern float FLOAT_80611a5c;
extern float FLOAT_80611a64;
extern float FLOAT_80611a6c;

// ---------------------------------------------------------------------------
// 0x80348ca8 : load the current card header strings via the writer table.
// ---------------------------------------------------------------------------
void arcade_edit_load_card_strings(void)
{
    int   screen;
    int*  slot;
    int   id_cursor[6];
    int   i;
    int   tbl_off;

    screen = FUN_803826f0();
    slot   = (int*)arcade_edit_slot_record(screen);
    id_cursor[0] = *slot + 1;
    tbl_off = 0;
    for (i = 0; i < 3; i = i + 1) {
        (*(void (**)(int, int*))((int)&PTR_challenge_write_header_strings_80481c1c + tbl_off))
            (DAT_80612be8, id_cursor);
        tbl_off = tbl_off + 8;
    }
    arcade_set_pending_action(0xffffffff);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80348d24 : build the 10-entry scrolling card-slot list HUD element.
// ---------------------------------------------------------------------------
void arcade_build_card_slot_list(int element)
{
    int i;
    int slot;
    double row_h = (double)(FLOAT_80611a5c *
        ((FLOAT_8061acec * FLOAT_80611a64 + FLOAT_8061acf0 * FLOAT_80611a64 +
          FLOAT_8061acf4 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c));

    hud_element_init_from_layout(0x8800, 0, 0, 0);
    *(int*)(element + 0x200) = get_string_by_id(0x959);
    *(int*)(element + 0x214) = 5;
    hud_set_item_padding_mode(FLOAT_8061ace4, 1, 1, 1);
    hud_set_context_property(0x20000);
    hud_element_scroll_h_overflow((int)-(double)(row_h - FLOAT_8061ace8),
                                  FLOAT_8061ace4, FLOAT_8061ace4, 0);
    hud_add_callback_item(FLOAT_8061acf8);
    for (i = 0; i < 10; i = i + 1) {
        slot = arcade_edit_slot_record(i);
        get_string_by_id(*(int*)(slot + 8));
        hud_add_list_item();
    }
    hud_element_apply_scroll();
    hud_element_finalize_frame();
    hud_element_set_pos_y(row_h, DAT_806119e0);
}

// ---------------------------------------------------------------------------
// 0x80348e64 : push a HUD screen.
// ---------------------------------------------------------------------------
void aveditor_screen_push(void)
{
    hud_screen_push();
}

// ---------------------------------------------------------------------------
// 0x80348e90 : rebalance the node ordering and compact the node scratch.
// ---------------------------------------------------------------------------
void aveditor_rebalance_node_order(void)
{
    int   cell_off;
    int   i;
    int   cell;
    int   best_cell;
    int   best_off;
    unsigned char* scratch;
    unsigned int*  cfg;

    FUN_803826e4();
    best_cell = 0;
    scratch   = aveditor_get_node_scratch();
    best_off  = 0;
    cell_off  = 0;
    for (i = 0; i < *(int*)(DAT_80612be4 + 0xdd10); i = i + 1) {
        cell = DAT_80612be4 + cell_off + 0xdd14;
        cfg  = match_mode_get_config(*(int*)(cell + 0x80));
        if (cfg[0x12] == 0) {
            bool take = false;
            if (best_cell == 0) { take = true; }
            else if (*(short*)(cell + 0x74) < *(short*)(best_cell + 0x74)) { take = true; }
            if (take) {
                best_off  = (int)*(short*)(cell + 0x74);
                best_cell = cell;
            }
            int mult = aveditor_node_multiplier(cell);
            *(short*)(cell + 0x74) = (short)(*(short*)(cell + 0x74) - (short)mult);
        }
        cell_off = cell_off + 0x8c;
    }
    int total = aveditor_total_nodes_for_mode(*(int*)(best_cell + 0x80));
    int bytes = aveditor_node_bytes(*(int*)(best_cell + 0x80), total);
    leaf_31e8((unsigned int)scratch, (unsigned int)(scratch + best_off), bytes);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80348f88 : preload entity resources for the preview level.
// ---------------------------------------------------------------------------
void aveditor_preload_level_entities(void)
{
    int levelId;
    int base;
    int j;

    FUN_803826f0();
    levelId = 0;
    do {
        base = level_def_preload_base_id(levelId);
        if (levelId < 1) {
            for (j = 0; j < 0x1d; j = j + 1) {
                entity_preload_resources(base + j);
            }
        }
        levelId = levelId + 1;
    } while (levelId < 6);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80348ff0 : clear old sound-bank ref and latch the new bank id.
// ---------------------------------------------------------------------------
void arcade_set_sound_bank(int bank)
{
    sound_clear_bank_ref(bank);
    DAT_80612dac = bank;
}

// ---------------------------------------------------------------------------
// 0x80349020 : preview-actor descriptor table pointer.
// ---------------------------------------------------------------------------
unsigned char* arcade_actor_table_ptr(void)
{
    return &DAT_80481f60[0];
}

// ---------------------------------------------------------------------------
// 0x8034902c : read word at +4 of a table entry (stride 0x18).
// ---------------------------------------------------------------------------
int arcade_actor_table_field4(int index)
{
    unsigned char* tbl = arcade_actor_table_ptr();
    return *(int*)(tbl + index * 0x18 + 4);
}

// ---------------------------------------------------------------------------
// 0x80349068 : switch the level actor for a table index.
// ---------------------------------------------------------------------------
void arcade_actor_switch_by_index(int index)
{
    unsigned char* tbl = arcade_actor_table_ptr();
    level_actor_switch(*(int*)(tbl + index * 0x18));
}

// ---------------------------------------------------------------------------
// 0x803490b0 : allocate actor tracking buffer, register table actors, pool setup.
// ---------------------------------------------------------------------------
void arcade_actor_pool_init(void)
{
    spawn_alloc_tracking_buffer();
    (void)arcade_actor_table_ptr();
    level_actors_register(1, 6);
    (void)arcade_actor_table_ptr();
    level_resource_pool_setup();
}

} // extern "C"
