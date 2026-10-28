// Batch: batch_918 | Module: arcade map-editor palette-slot picker screen builder + config panels
// Address range: 0x80327944 - 0x80327ef4
// Date: 2026-09-12
//
// Builds the arcade map-editor palette-slot picker screen (the 0x40-swatch grid driven by the
// batch_917 tick) and the config sub-panels shown alongside it, plus the per-frame RGB sync that
// keeps the live colour block mirrored into the active/selected piece records. The main builder
// (aveditor_build_palette_slot_screen) lays out the screen: registers soft-keys, builds the grid
// region, and - when in the RGB-editing (non secondary-id) mode - appends the colour-source panel,
// a couple of toggle rows and the extra config sub-panels, then applies scroll + Y position.
//
//   aveditor_register_palette_softkeys 0x80327944  register the global soft-key labels by mode.
//   aveditor_build_palette_slot_screen 0x80327a38  build the whole palette-slot picker screen.
//   aveditor_write_record_rgb          0x80327bf8  copy the live RGB block into a record's +0x14.
//   aveditor_sync_active_rgb           0x80327c30  sync live RGB into the dirty active record.
//   aveditor_mark_palette_dirty        0x80327c90  flag the screen for rebuild.
//   aveditor_palette_sync_tick         0x80327cb4  per-frame: bind confirm + push RGB to members.
//   aveditor_palette_back_dialog       0x80327d38  back-dialog callback (string 0x970).
//   aveditor_build_palette_region      0x80327d78  build the palette-grid scroll sub-list.
//   aveditor_build_colorsource_panel   0x80327e48  build the 4-option colour-source panel.
//   aveditor_build_config_toggle       0x80327ef4  build a labeled-pair toggle config row.
//
// Editor context (DAT_80612be4): +0x25c state flags (bit 0x1000000 assign-active, 0x4000000
// active-record-dirty, 0x20000000 rebuild-request, 0x80 a sub-mode gate); +0x2f18 active edit
// record (RGB triple at +0x14). DAT_80612c14 = member list head (chain +0x40, selected = flag bit
// 2 at +0x14). DAT_805d6748 = live RGB block (r,g,b copied to/from record+0x14).
//
// Raw FUN_/DAT_/PTR_/FLOAT_ symbols get correctly-typed per-file externs; named cross-module
// helpers (and the batch_915/917 aveditor_* helpers) are re-declared with their real ABI.

#include "common.h"

extern "C" {

extern int DAT_80612be4;    // editor context base
extern int DAT_80612c14;    // member list head (chain via +0x40)
extern int DAT_806119e0;    // layout Y-position arg / anchor

// live RGB block (r,g,b triple copied to/from record+0x14)
extern unsigned int DAT_805d6748;

// modal picker vtable (RGB-editing path is "not the simple-value picker")
extern void *PTR_FUN_80480e30;

// layout float constants
extern float FLOAT_8061a92c;   // clamp min / small pad
extern float FLOAT_8061a930;   // item padding (0.5)
extern float FLOAT_8061a960;   // picker pos term 0
extern float FLOAT_8061a964;   // picker pos term 1
extern float FLOAT_8061a968;   // picker pos term 2
extern float FLOAT_8061a988;   // scroll offset bias
extern float FLOAT_8061a98c;   // sub-list width A
extern float FLOAT_8061a990;   // sub-list height
extern float FLOAT_8061a994;   // sub-list width B
extern float FLOAT_8061a998;   // sub-list width C
extern float FLOAT_80611a5c;   // layout scale
extern float FLOAT_80611a64;   // layout unit
extern float FLOAT_80611a6c;   // layout base offset

// -- raw sibling builders (own batch later); void(int,int) / void(void) --
extern void FUN_80327f88(unsigned int screen, int record);   // extra toggle sub-panel
extern void FUN_80328058(void);                              // trailing 3-wide sub-panel

// GPR spill helpers (register-passed screen frame)
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);

// -- named cross-module helpers (real ABI) --
extern int  get_string_by_id(int id);
extern void hud_screen_register_handler_global(int slot, int text, int a, int b);
extern int  hud_item_is_selectable(int screen, int id);
extern void hud_element_init_from_layout(int flags, int a, int b, void *cb);
extern void hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void hud_set_context_property(int prop);
extern void hud_element_scroll_h_overflow(double amount, double a, double b, int c);
extern void hud_element_apply_scroll(void);
extern void hud_element_set_pos_y(double y, int anchor);
extern void hud_create_back_dialog(unsigned int screen, int text, int arg);
extern void hud_begin_scroll_region(void);
extern void hud_begin_scroll_with_item(void);
extern void hud_begin_sub_list(int a, int b, int cols);
extern double hud_get_content_height(void);
extern void hud_add_item_internal(double w, double h);
extern void hud_set_last_child_tag(int tag);
extern void hud_end_sub_list(void);
extern void hud_end_scroll_region(void);
extern void hud_item_add_footer(double w, int flags);
extern void hud_event_add_string(int text);
extern void hud_item_set_value_display(int src);
extern void hud_item_add_labeled_pair(int a, int b, int left, int right);
extern int  hud_get_last_created_item(void);
extern void hud_item_set_toggle(int src, int item);

extern void aveditor_snapshot_record_rgb(int screen);        // batch_917
extern void aveditor_enter_wset_picker(void);                // batch_917
extern void aveditor_bind_palette_confirm(int screen);       // batch_917
extern void aveditor_build_palette_grid(void);               // batch_915

// forward decls (defined below, referenced before definition by the builder)
int  aveditor_sync_active_rgb(void);
void aveditor_mark_palette_dirty(int screen);
void aveditor_write_record_rgb(int record);
void aveditor_build_palette_region(void);
void aveditor_build_colorsource_panel(unsigned int screen, int record);
void aveditor_build_config_toggle(unsigned int screen, int record);

// Register the palette screen's global soft-key labels. Edit mode (assign-active clear) sets the
// four prompt slots (0xa43/0xa5d/0xaac/0xa18); assign-active mode swaps in the assign labels.
void aveditor_register_palette_softkeys(void)
{
    if ((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x1000000) == 0) {
        hud_screen_register_handler_global(0, get_string_by_id(0xa43), 0, 0);
        hud_screen_register_handler_global(1, get_string_by_id(0xa5d), 0, 0);
        hud_screen_register_handler_global(2, get_string_by_id(0xaac), 0, 0);
        hud_screen_register_handler_global(3, get_string_by_id(0xa18), 0, 0);
    } else {
        hud_screen_register_handler_global(2, get_string_by_id(0xa19), 0, 0);
        hud_screen_register_handler_global(3, get_string_by_id(0xa1a), 0, 0);
        hud_screen_register_handler_global(1, get_string_by_id(0xa1b), 0, 0);
    }
}

// Build the whole palette-slot picker screen. In the RGB-editing path (screen vtable is not the
// simple-value picker) it also snapshots the record RGB and appends the colour-source panel, the
// two toggle rows and the trailing sub-panels; the confirm callback is the wset-picker entry.
void aveditor_build_palette_slot_screen(void)
{
    int screen = __save_gpr_28();
    bool notSimpleValue = (*(void ***)(screen + 4) != &PTR_FUN_80480e30);
    void *confirmCb = (void *)&aveditor_enter_wset_picker;
    if (notSimpleValue) {
        confirmCb = 0;
    } else {
        aveditor_snapshot_record_rgb(screen);
    }

    if (aveditor_sync_active_rgb() != 0) {
        aveditor_mark_palette_dirty(screen);
    }

    hud_element_init_from_layout(0x400, 0, 0, confirmCb);
    hud_set_item_padding_mode((double)FLOAT_8061a930, 1, 1, 1);
    hud_set_context_property(0x20000);
    aveditor_register_palette_softkeys();

    hud_element_scroll_h_overflow(
        -(double)(FLOAT_80611a5c *
                  ((FLOAT_8061a960 * FLOAT_80611a64 +
                    FLOAT_8061a964 * FLOAT_80611a64 +
                    FLOAT_8061a968 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                  FLOAT_8061a988),
        (double)FLOAT_8061a92c, (double)FLOAT_8061a92c, 0);

    if (!notSimpleValue) {
        aveditor_build_palette_region();
    }
    aveditor_build_palette_grid();
    if (!notSimpleValue) {
        aveditor_build_colorsource_panel(screen, -0x7fa298b4);
        aveditor_build_config_toggle(screen, -0x7fa298b4);
        FUN_80327f88((unsigned int)screen, -0x7fa298b4);
        FUN_80328058();
    }
    hud_element_apply_scroll();
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061a960 * FLOAT_80611a64 +
                   FLOAT_8061a964 * FLOAT_80611a64 +
                   FLOAT_8061a968 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    __restore_gpr_28();
}

// Copy the live RGB block (DAT_805d6748, r,g,b) into a member/piece record's +0x14 colour triple.
void aveditor_write_record_rgb(int record)
{
    unsigned int *src = (unsigned int *)&DAT_805d6748;
    unsigned int *dst = (unsigned int *)(record + 0x14);
    for (int i = 2; i != 0; --i) {
        dst[1] = src[1];
        dst[2] = src[2];
        src += 2;
        dst += 2;
    }
    dst[3] = src[3];
}

// If the active record exists and is flagged dirty (state bit 0x4000000), pull its +0x14 RGB into
// the live block. Returns 1 if the active record is set, else 0.
int aveditor_sync_active_rgb(void)
{
    int ctx = DAT_80612be4;
    if ((*(int *)(DAT_80612be4 + 0x2f18) != 0) &&
        ((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x4000000) != 0)) {
        unsigned int *src = (unsigned int *)(*(int *)(DAT_80612be4 + 0x2f18) + 0x14);
        unsigned int *dst = (unsigned int *)&DAT_805d6748;
        for (int i = 2; i != 0; --i) {
            dst[1] = src[1];
            dst[2] = src[2];
            src += 2;
            dst += 2;
        }
        dst[3] = src[3];
    }
    return (*(int *)(ctx + 0x2f18) != 0) ? 1 : 0;
}

// Flag the screen for rebuild: if the sub-mode gate bit 0x80 is set, request a rebuild (0x20000000)
// on the context; always set the screen's rebuild word (+0x214).
void aveditor_mark_palette_dirty(int screen)
{
    if ((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x80) != 0) {
        *(unsigned int *)(DAT_80612be4 + 0x25c) =
            *(unsigned int *)(DAT_80612be4 + 0x25c) | 0x20000000;
    }
    *(int *)(screen + 0x214) = 1;
}

// Per-frame sync: bind the grid confirm child, then push the live RGB into the active record and
// every selected member in the chain.
void aveditor_palette_sync_tick(int screen)
{
    int member = DAT_80612c14;
    if (hud_item_is_selectable(screen, 0x65) != 0) {
        aveditor_bind_palette_confirm(screen);
    }
    if (*(int *)(DAT_80612be4 + 0x2f18) != 0) {
        aveditor_write_record_rgb(*(int *)(DAT_80612be4 + 0x2f18));
    }
    while (member != 0) {
        if ((*(unsigned char *)(member + 0x14) & 2) != 0) {
            aveditor_write_record_rgb(member);
        }
        member = *(int *)(member + 0x40);
    }
}

// Back-dialog callback: pop the "back" confirm dialog (string 0x970). Bound as the empty-list
// confirm handler by aveditor_bind_palette_confirm.
void aveditor_palette_back_dialog(unsigned int screen)
{
    int text = get_string_by_id(0x970);
    hud_create_back_dialog(screen, text, 0);
}

// Build the palette-grid scroll sub-list region (2-wide, one sized swatch cell + one action cell
// tagged 0x2e). String id is register-passed (r6) by the caller.
void aveditor_build_palette_region(void)
{
    // NOTE: the string id arrives in r6 at the call site; the value is only fetched for its side
    // effect (localization) and is not otherwise used here.
    get_string_by_id(0);
    hud_begin_scroll_region();
    hud_begin_sub_list(0, 0, 2);
    double h = hud_get_content_height();
    hud_add_item_internal((double)(float)(h - (double)FLOAT_8061a98c), (double)FLOAT_8061a990);
    hud_add_item_internal((double)FLOAT_8061a92c, (double)FLOAT_8061a990);
    hud_set_last_child_tag(0x2e);
    hud_end_sub_list();
    hud_end_scroll_region();
}

// Build the "colour source" config panel: a footer item plus four labeled options (strings
// 0x968..0x96b, tags 0xa3..0xa6) wired to the value display at record+4.
void aveditor_build_colorsource_panel(unsigned int screen, int record)
{
    (void)screen;
    get_string_by_id(0x967);
    hud_begin_scroll_with_item();
    hud_item_add_footer((double)FLOAT_8061a92c, 0x4000);
    hud_event_add_string(get_string_by_id(0x968));
    hud_set_last_child_tag(0xa3);
    hud_event_add_string(get_string_by_id(0x969));
    hud_set_last_child_tag(0xa4);
    hud_event_add_string(get_string_by_id(0x96a));
    hud_set_last_child_tag(0xa5);
    hud_event_add_string(get_string_by_id(0x96b));
    hud_set_last_child_tag(0xa6);
    hud_item_set_value_display(record + 4);
    hud_end_scroll_region();
}

// Build a labeled-pair toggle config row (strings 0x96d/0x96e, tag 0xa7) wired to record+0xc.
void aveditor_build_config_toggle(unsigned int screen, int record)
{
    (void)screen;
    get_string_by_id(0x96c);
    hud_begin_scroll_with_item();
    int right = get_string_by_id(0x96e);
    int left = get_string_by_id(0x96d);
    hud_item_add_labeled_pair(0, 0xb, left, right);
    hud_set_last_child_tag(0xa7);
    int item = hud_get_last_created_item();
    hud_item_set_toggle(record + 0xc, item);
    hud_end_scroll_region();
}

} // extern "C"
