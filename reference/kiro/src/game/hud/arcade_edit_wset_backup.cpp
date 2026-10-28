// Batch: batch_919 | Module: arcade map-editor weapon-set edit backup/restore + screen builders
// Address range: 0x80327f88 - 0x80328490
// Date: 2026-09-12
//
// The edit-session backup/restore for the arcade map-editor weapon-set picker, two more screen
// builders (the wset edit screen with its config panels, and the simple palette-grid picker), the
// two config sub-list builders forward-declared by batch_918, and a few small accessors. On entry
// to an edit session a 0x14-byte snapshot of the live palette-slot state is allocated and copied;
// on exit the live state is compared against the snapshot and a revert sound is played if it
// changed. The wset edit screen adds a callback item, the batch_918 colour-source / toggle panels,
// the two sub-list panels below, and a trailing action list item.
//
//   aveditor_build_toggle_sublist       0x80327f88  build the labeled-pair toggle sub-list (0xa8).
//   aveditor_build_value_sublist        0x80328058  build the 3-wide value sub-list panel (0x961).
//   aveditor_free_wset_backup           0x80328144  free the edit-session backup buffer.
//   aveditor_save_wset_backup           0x8032816c  alloc a 0x14 backup + copy the source in.
//   aveditor_wset_exit_check            0x803281b0  on exit: revert sound if changed, dirty-check.
//   aveditor_build_wset_edit_screen     0x80328220  build the wset edit screen (panels + list).
//   aveditor_build_palette_picker_screen 0x80328354 build the simple palette-grid picker screen.
//   aveditor_wset_table_lookup          0x80328474  lookup a value from the DAT_80480e80 table.
//   aveditor_get_wset_flag              0x80328488  read the global wset byte DAT_80612cc4.
//   aveditor_set_wset_flag              0x80328490  write the global wset byte DAT_80612cc4.
//
// DAT_80612cb8 = edit-session backup buffer pointer (0x14 bytes). DAT_80480e80 = static int table.
// Editor context (DAT_80612be4): +0x238 sound-latch (bit 0x20). See batch_917/918 for +0x25c and
// the layout float constants.
//
// Raw FUN_/DAT_/PTR_/FLOAT_ symbols get correctly-typed per-file externs; named cross-module
// helpers (and the batch_915..918 aveditor_* helpers) are re-declared with their real ABI.

#include "common.h"

extern "C" {

extern int DAT_80612be4;    // editor context base
extern int DAT_80612cb8;    // edit-session backup buffer pointer (0x14 bytes)
extern unsigned char DAT_80612cc4;  // global wset flag byte
extern int DAT_806119e0;    // layout Y-position anchor
extern int DAT_805d59a0;    // level-def flag arg
extern unsigned int DAT_805d674c;   // live palette-slot bytes
extern int DAT_80480e80;    // static int lookup table

extern void *PTR_FUN_80480e70;   // revert-confirm entry vtable

// layout float constants
extern float FLOAT_8061a92c;   // clamp min / small pad
extern float FLOAT_8061a930;   // item padding (0.5)
extern float FLOAT_8061a960;   // picker pos term 0
extern float FLOAT_8061a964;   // picker pos term 1
extern float FLOAT_8061a968;   // picker pos term 2
extern float FLOAT_8061a974;   // callback-item size
extern float FLOAT_8061a988;   // scroll offset bias
extern float FLOAT_8061a98c;   // sub-list width A
extern float FLOAT_8061a990;   // sub-list height
extern float FLOAT_8061a994;   // sub-list width B
extern float FLOAT_8061a998;   // sub-list width C
extern float FLOAT_8061a99c;   // edit-screen Y pos
extern float FLOAT_80611a5c;   // layout scale
extern float FLOAT_80611a64;   // layout unit
extern float FLOAT_80611a6c;   // layout base offset

// FUN_80348238: sound/effect dispatcher (0xa = wset-revert sound).
extern void FUN_80348238(int index);

// GPR spill helpers (register-passed screen frame)
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);

// -- named cross-module helpers (real ABI) --
extern void mem_free_top(int ptr);
extern int  mem_alloc(long long a, long long b, long long c, long long d,
                      long long e, long long f, long long g, long long h,
                      int size, int i, int j, int k, int l, int m, int n, int o);
extern void leaf_31e8(int dst, int src, int len);   // block copy
extern int  get_string_by_id(int id);
extern void screen_exit_dirty_check(void);
extern void level_def_flag_d(int arg);
extern void hud_element_init_from_layout(int flags, int a, int b, void *cb);
extern void hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void hud_set_context_property(int prop);
extern void hud_element_scroll_h_overflow(double amount, double a, double b, int c);
extern void hud_element_apply_scroll(void);
extern void hud_element_set_pos_y(double y, int anchor);
extern void hud_add_callback_item(double size);
extern void hud_add_list_item(void);
extern void hud_begin_scroll_with_item(void);
extern void hud_begin_sub_list(int a, int b, int cols);
extern double hud_get_content_height(void);
extern void hud_add_item_internal(double w, double h);
extern void hud_set_last_child_tag(int tag);
extern void hud_end_sub_list(void);
extern void hud_end_scroll_region(void);
extern void hud_item_add_labeled_pair(int a, int b, int left, int right);
extern int  hud_get_last_created_item(void);
extern void hud_item_set_toggle(int src, int item);

extern unsigned int aveditor_wset_entry_equal(char *a, char *b);        // batch_916
extern int  aveditor_sync_active_rgb(void);                             // batch_918
extern void aveditor_build_colorsource_panel(unsigned int screen, int record); // batch_918
extern void aveditor_build_config_toggle(unsigned int screen, int record);     // batch_918
extern void aveditor_register_palette_softkeys(void);                  // batch_918
extern void aveditor_build_palette_grid(void);                         // batch_915
extern void aveditor_colorpicker_back(int screen);                     // batch_914

// forward: the exit callback used by the edit-screen builder
void aveditor_wset_exit_check(void);

// Build the labeled-pair toggle sub-list (2-wide, tag 0xa8) wired to record+0x10.
void aveditor_build_toggle_sublist(unsigned int screen, int record)
{
    (void)screen;
    get_string_by_id(0x96f);
    hud_begin_scroll_with_item();
    hud_begin_sub_list(0, 0, 2);
    hud_add_item_internal((double)FLOAT_8061a994, (double)FLOAT_8061a994);
    hud_add_item_internal((double)FLOAT_8061a92c, (double)FLOAT_8061a994);
    hud_item_add_labeled_pair(0, 0xb, 0, 0);
    hud_set_last_child_tag(0xa8);
    int item = hud_get_last_created_item();
    hud_item_set_toggle(record + 0x10, item);
    hud_end_sub_list();
    hud_end_scroll_region();
}

// Build the 3-wide value sub-list panel (string 0x961): one content-sized cell + two fixed cells.
void aveditor_build_value_sublist(void)
{
    get_string_by_id(0x961);
    hud_begin_scroll_with_item();
    hud_begin_sub_list(0, 0, 3);
    double h = hud_get_content_height();
    hud_add_item_internal((double)(float)(h - (double)FLOAT_8061a998), (double)FLOAT_8061a990);
    hud_add_item_internal((double)FLOAT_8061a98c, (double)FLOAT_8061a990);
    hud_add_item_internal((double)FLOAT_8061a98c, (double)FLOAT_8061a990);
    hud_end_sub_list();
    hud_end_scroll_region();
}

// Free the edit-session backup buffer and return its (now-stale) pointer.
int aveditor_free_wset_backup(void)
{
    mem_free_top(DAT_80612cb8);
    return DAT_80612cb8;
}

// Allocate a 0x14-byte backup buffer (top allocator) and copy the source snapshot into it.
void aveditor_save_wset_backup(long long a, long long b, long long c, long long d,
                               long long e, long long f, long long g, long long h,
                               unsigned int src, int p10, int p11, int p12,
                               int p13, int p14, int p15, int p16)
{
    (void)p10;
    DAT_80612cb8 = mem_alloc(a, b, c, d, e, f, g, h, 0x14, 0, p11, p12, p13, p14, p15, p16);
    leaf_31e8(DAT_80612cb8, (int)src, 0x14);
}

// On exit: if the backup differs from the live palette-slot state, play the revert sound (0xa)
// once, then run the standard dirty-check exit.
void aveditor_wset_exit_check(void)
{
    char *backup = (char *)aveditor_free_wset_backup();
    if ((aveditor_wset_entry_equal(backup, (char *)&DAT_805d674c) != 0) &&
        ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(10);
        *(unsigned int *)(DAT_80612be4 + 0x238) =
            *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    screen_exit_dirty_check();
}

// Build the weapon-set edit screen: sync the active RGB, flag rebuild, then lay out a callback item,
// the batch_918 colour-source and toggle panels, the two sub-list panels, another callback item and
// a trailing action list item (string 0xb03). Exit callback is aveditor_wset_exit_check.
void aveditor_build_wset_edit_screen(int screen)
{
    aveditor_sync_active_rgb();
    *(int *)(screen + 0x214) = 1;
    hud_element_init_from_layout(0x8400, 0, 0, (void *)&aveditor_wset_exit_check);
    hud_set_item_padding_mode((double)FLOAT_8061a930, 1, 1, 1);
    hud_set_context_property(0x20000);
    hud_element_scroll_h_overflow(
        -(double)(FLOAT_80611a5c *
                  ((FLOAT_8061a960 * FLOAT_80611a64 +
                    FLOAT_8061a964 * FLOAT_80611a64 +
                    FLOAT_8061a968 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                  FLOAT_8061a988),
        (double)FLOAT_8061a92c, (double)FLOAT_8061a92c, 0);
    hud_add_callback_item((double)FLOAT_8061a974);
    aveditor_build_colorsource_panel((unsigned int)screen, -0x7fa298b4);
    aveditor_build_config_toggle((unsigned int)screen, -0x7fa298b4);
    aveditor_build_toggle_sublist((unsigned int)screen, -0x7fa298b4);
    aveditor_build_value_sublist();
    hud_add_callback_item((double)FLOAT_8061a974);
    get_string_by_id(0xb03);
    hud_add_list_item();
    hud_element_apply_scroll();
    hud_element_set_pos_y((double)FLOAT_8061a99c, DAT_806119e0);
}

// Build the simple palette-grid picker screen. When the screen behind is NOT the revert-confirm
// entry (vtable == PTR_FUN_80480e70), toggle the level-def flag first, then lay out the grid.
// The exit callback is the colour-picker back handler. Screen ptr via the GPR-spill helper.
void aveditor_build_palette_picker_screen(void)
{
    int screen = __save_gpr_28();
    // countLeadingZeros(x)>>5 == (x == 0): true when the screen vtable IS the revert-confirm entry.
    if (*(int *)(screen + 4) == (int)&PTR_FUN_80480e70) {
        level_def_flag_d(DAT_805d59a0);
    }
    hud_element_init_from_layout(0x8400, 0, 0, (void *)&aveditor_colorpicker_back);
    hud_set_item_padding_mode((double)FLOAT_8061a930, 1, 1, 1);
    hud_set_context_property(0x20000);
    aveditor_register_palette_softkeys();
    aveditor_build_palette_grid();
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061a960 * FLOAT_80611a64 +
                   FLOAT_8061a964 * FLOAT_80611a64 +
                   FLOAT_8061a968 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    __restore_gpr_28();
}

// Look up a value from the static DAT_80480e80 int table by index.
int aveditor_wset_table_lookup(int index)
{
    return (&DAT_80480e80)[index];
}

// Read / write the global wset flag byte DAT_80612cc4.
unsigned char aveditor_get_wset_flag(void) { return DAT_80612cc4; }
void aveditor_set_wset_flag(unsigned char v) { DAT_80612cc4 = v; }

} // extern "C"
