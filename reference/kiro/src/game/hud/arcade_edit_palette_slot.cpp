// Batch: batch_917 | Module: arcade map-editor palette-slot / weapon-set picker screen glue
// Address range: 0x8032711c - 0x803278b4
// Date: 2026-09-12
//
// The per-frame tick and screen-entry/exit glue for the arcade map-editor palette-slot picker
// (the 0x40-swatch grid built in batch_915) and its weapon-set assignment modes. This layer:
//   - configures the picker soft-keys for the current edit/assign mode,
//   - binds the grid's confirm child to either a back dialog (empty list) or a push handler,
//   - runs the big per-frame tick that focuses the view on the selected slot, mirrors the live
//     RGB into the active record, and on confirm either enters a sub-picker, copies a slot,
//     range-fills a gradient of slots, or hides the picker,
//   - and the small push/enter/exit callbacks for the id / secondary-id sub-pickers.
//
//   aveditor_config_palette_softkey  0x8032711c  set the picker soft-key handler params by mode.
//   aveditor_bind_palette_confirm    0x803271d8  bind the grid confirm child (dialog vs push).
//   aveditor_palette_slot_tick       0x8032724c  per-frame palette-slot picker tick + confirm.
//   aveditor_palette_push_cb         0x803276d0  push-screen confirm callback.
//   aveditor_palette_push_cb2        0x803276fc  push-screen confirm callback (variant 2).
//   aveditor_palette_push_cb3        0x80327728  push-screen confirm callback (variant 3).
//   aveditor_commit_palette_exit     0x80327754  on exit: confirm sound if dirty, copy to default.
//   aveditor_enter_id_picker         0x803277bc  enter the id sub-picker (init entry, set flag).
//   aveditor_enter_wset_picker       0x80327830  enter the wset sub-picker (clear flag, init slot).
//   aveditor_snapshot_record_rgb     0x803278b4  snapshot the record RGB into the live block.
//
// Editor context (DAT_80612be4): +0x11858 -> a struct whose +0xc short is the active piece index,
// used to reach the per-piece record block at ctx + idx*0x8c + 0xdd14 (record accessed via
// FUN_80336ba8, which returns a pointer just past the record head; the RGB triple lives at the
// tail). +0x25c state flags: bit 0x1000000 = input-locked/assign-active, 0x10000000 = id-picker
// active. +0x238 sound-latch (bit 0x20). +0x254 sub-mode. +0x2f18 active edit record.
// DAT_805d6630 = working palette (0x40 words); DAT_805d6730 = HSV scratch; DAT_805d6740 = RGB
// block; DAT_805d674c = live palette-slot bytes (byte0 = selected slot index / wset id).
//
// Raw FUN_/DAT_/PTR_/DOUBLE_ symbols get correctly-typed per-file externs; named cross-module
// helpers (and the batch_914/916 aveditor_* helpers) are re-declared with their real ABI.

#include "common.h"

extern "C" {

extern int DAT_80612be4;    // editor context base
extern int DAT_80612bec;    // HUD redraw/tick request word
extern int DAT_806119ec;    // active viewport/screen-stack index
extern int DAT_8051a820;    // screen-stack array base (stride 0x1e0)

// palette / HSV / RGB state
extern unsigned int DAT_805d6630;   // working palette, 0x40 words
extern float DAT_805d6730;          // HSV scratch (h,s,v)
extern float DAT_805d6740;          // derived RGB block
extern float DAT_805d6748;          // RGB block tail (r,g,b triple copied to/from records)
extern unsigned int DAT_805d674c;   // live palette-slot bytes (byte0 = slot/wset id)

extern double DOUBLE_8061a948;      // u32->double bias constant (int->float idiom)

// modal picker vtables
extern void *PTR_FUN_80480e30;      // simple value picker
extern void *PTR_FUN_80480e60;      // wset secondary-id picker

// -- raw helpers --
// FUN_80336ba8: given the per-piece record block, return a pointer just past its head
// (the RGB triple sits at ret[-1]..ret[+2] in this file's copy loops).
extern unsigned int *FUN_80336ba8(int record_block);
// FUN_803826cc / FUN_80382718: GPR spill/restore prologue+epilogue helper pair (r20..r31).
extern int  FUN_803826cc(void);
extern void FUN_80382718(void);
// FUN_80348238: sound/effect dispatcher (0xb = range-fill confirm, 0xc = palette-commit confirm).
extern void FUN_80348238(int index);
// FUN_80327d38: "back" dialog callback (string 0x970); bound as the empty-list confirm handler.
extern void FUN_80327d38(unsigned int screen);

// -- named cross-module helpers (real ABI) --
extern int  get_string_by_id(int id);
extern void hud_screen_set_handler_param_a(int screen, int slot, int value);
extern void hud_screen_set_handler_param_b(int screen, int slot, int value);
extern int  hud_find_child_by_id(int screen, int id);
extern int  hud_item_is_selectable(int screen, int id);
extern int  hud_element_can_confirm(int screen);
extern int  hud_screen_find_root(int screen);
extern void hud_screen_push(void);
extern void hud_screen_mark_hidden(int screen);
extern void hud_input_reset_repeat_timers(void);
extern void hud_create_back_dialog(unsigned int screen, int text, int arg);
extern void avanim_focus_view(float *pos, int screen, int mode, int *arg);
extern void avanim_play_move_sound(int id);
extern void color_to_rgb_via_hsv(unsigned int packed, float *hsv_out);
extern void hsv_to_rgb(float *hsv, float *rgb_out);
extern unsigned int color_lerp_keep_lo(double t, unsigned int a, unsigned int b);
extern void leaf_31e8(int dst, int src, int len);   // block copy (palette -> default block)

extern int  aveditor_palette_changed(void);                     // batch_914
extern void aveditor_init_wset_entry(unsigned char *entry, int kind);   // batch_914
extern void aveditor_wset_back(int screen);                     // batch_916
extern void aveditor_wset_hide(int screen);                     // batch_916
extern void aveditor_draw_palette_swatch(void);                 // batch_915 (compared as a fn ptr)

// forward: the push callback used by aveditor_bind_palette_confirm
void aveditor_palette_push_cb3(void);

// Configure the palette-picker soft-key handler params for the current mode. In normal edit mode
// (assign-active flag clear) it sets the prompt string (0xa3c "edit" for mode 1 else 0xa43) and the
// A/confirm param slots; in assign-active mode it wires the extra B-slot instead of the prompt.
void aveditor_config_palette_softkey(int mode, int screen)
{
    if ((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x1000000) == 0) {
        int strId = (mode == 1) ? 0xa3c : 0xa43;
        int text = get_string_by_id(strId);
        hud_screen_set_handler_param_a(screen, 0, text);
        hud_screen_set_handler_param_b(screen, 2, mode);
        hud_screen_set_handler_param_b(screen, 3, mode);
    } else {
        hud_screen_set_handler_param_b(screen, 2, mode);
        hud_screen_set_handler_param_b(screen, 3, mode);
        hud_screen_set_handler_param_b(screen, 1, mode);
    }
}

// Bind the palette-grid confirm child (id 0x66): a push handler when the list child (id 0x65) has
// entries, otherwise the "back" dialog callback (empty list => nothing to confirm).
void aveditor_bind_palette_confirm(int screen)
{
    int list = hud_find_child_by_id(screen, 0x65);
    int confirm = hud_find_child_by_id(screen, 0x66);
    if (confirm != 0) {
        void (*cb)(void) = FUN_80327d38 ? (void (*)(void))0 : 0; // placeholder replaced below
        (void)cb;
        if (0 < **(int **)(list + 0x48)) {
            *(void **)(confirm + 0x38) = (void *)&aveditor_palette_push_cb3;
        } else {
            *(void **)(confirm + 0x38) = (void *)&FUN_80327d38;
        }
    }
}

// Per-frame tick for the palette-slot picker screen. Focuses the view on the active record's slot,
// mirrors the live RGB block into the active record (when not in the simple-value vtable), and on a
// confirm gesture dispatches by the screen root's flag word (+0xc0): enter the swatch sub-picker,
// copy a slot, gradient-fill a rectangular range of slots between the current and picked index, or
// hide the picker. The screen pointer comes from the GPR-spill helper (register-passed frame).
void aveditor_palette_slot_tick(void)
{
    int screen = FUN_803826cc();
    bool isSwatchScreen = false;

    // countLeadingZeros(x)>>5 == (x == 0): true when the screen vtable is the simple-value picker.
    unsigned int isSimpleValue = (*(int *)(screen + 4) == (int)&PTR_FUN_80480e30) ? 1u : 0u;

    if ((*(int *)(screen + 0x194) != 0) &&
        (*(void **)(*(int *)(screen + 0x194) + 0x50) == (void *)&aveditor_draw_palette_swatch)) {
        isSwatchScreen = true;
    }

    aveditor_config_palette_softkey(isSwatchScreen ? 0 : 1, screen);

    float *rec = *(float **)(DAT_80612be4 + 0x2f18);
    if (rec != 0) {
        float pos[3];
        pos[0] = rec[0];
        pos[1] = rec[1];
        pos[2] = rec[2];
        avanim_focus_view(pos, screen, 2, 0);
    }

    // Mirror the live RGB triple into the active piece record (unless the simple-value vtable).
    if (isSimpleValue != 0) {
        unsigned int *dst = FUN_80336ba8(DAT_80612be4 +
            *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd14);
        unsigned int *src = (unsigned int *)&DAT_805d6748;
        dst = dst - 1;
        for (int i = 2; i != 0; --i) {
            dst[1] = src[1];
            dst[2] = src[2];
            src += 2;
            dst += 2;
        }
        dst[3] = src[3];
    }

    if (hud_item_is_selectable(screen, 0x65) != 0) {
        aveditor_bind_palette_confirm(screen);
    }

    if (hud_element_can_confirm(screen) != 0) {
        int root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
        if (((*(unsigned int *)(root + 0xc0) & 0x20) != 0) &&
            ((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x1000000) == 0) &&
            (*(int *)(DAT_80612be4 + 0x254) != 4)) {
            aveditor_wset_back(screen);
        }

        if (isSwatchScreen) {
            if ((*(unsigned int *)(DAT_80612be4 + 0x25c) & 0x1000000) == 0) {
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if ((*(unsigned int *)(root + 0xc0) & 0x80) == 0) {
                    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                    if ((*(unsigned int *)(root + 0xc0) & 0x40) != 0) {
                        // regenerate RGB from the picked slot's HSV, then push the colour picker
                        color_to_rgb_via_hsv((&DAT_805d6630)[*(int *)(screen + 0x188)],
                                             &DAT_805d6730);
                        hsv_to_rgb(&DAT_805d6730, &DAT_805d6740);
                        *(unsigned char *)&DAT_805d674c = (unsigned char)*(int *)(screen + 0x188);
                        hud_screen_push();
                    }
                } else {
                    // enter assign-active: latch the picked slot and lock input for assignment
                    *(unsigned char *)&DAT_805d674c = (unsigned char)*(int *)(screen + 0x188);
                    *(unsigned int *)(DAT_80612be4 + 0x25c) =
                        *(unsigned int *)(DAT_80612be4 + 0x25c) | 0x1000000;
                    hud_screen_mark_hidden(screen);
                    DAT_80612bec = 0xf;
                    avanim_play_move_sound(0x1578);
                }
            } else {
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if ((*(unsigned int *)(root + 0xc0) & 0x20) == 0) {
                    if (*(int *)(screen + 0x188) < 0x40) {
                        root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                        if ((*(unsigned int *)(root + 0xc0) & 0x40) == 0) {
                            root = hud_screen_find_root(
                                *(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                            if ((*(unsigned int *)(root + 0xc0) & 0x80) != 0) {
                                // gradient-fill the rectangular block of palette slots between the
                                // current (byte0) and the picked index (screen+0x188)
                                unsigned int picked = *(unsigned int *)(screen + 0x188);
                                unsigned int cur = (unsigned int)(unsigned char)DAT_805d674c;
                                if (picked != cur) {
                                    unsigned int lo = (cur < picked) ? cur : picked;
                                    unsigned int loColor = (&DAT_805d6630)[lo];
                                    unsigned int hi = (cur < picked) ? picked : cur;
                                    unsigned int hiColor = (&DAT_805d6630)[hi];

                                    unsigned int cCol = (unsigned char)DAT_805d674c & 7;
                                    unsigned int pCol = picked & 7;
                                    unsigned int cRow = (unsigned int)((unsigned char)DAT_805d674c >> 3);
                                    unsigned int pRow = picked >> 3;

                                    unsigned int col0 = pCol, col1 = cCol;
                                    if (pCol < cCol) { col1 = cCol; col0 = pCol; }
                                    else { col0 = cCol; col1 = pCol; }
                                    // normalize column pair (min,max)
                                    if (col1 < col0) { unsigned int t = col0; col0 = col1; col1 = t; }

                                    unsigned int row0 = cRow, row1 = pRow;
                                    if (pRow < cRow) { row0 = pRow; row1 = cRow; }
                                    else { row0 = cRow; row1 = pRow; }

                                    int widthCols = (int)(col1 - col0) + 1;
                                    int count = widthCols * ((int)(row1 - row0) + 1);
                                    int idx = (int)(col0 + row0 * 8);
                                    int colInRow = 0;
                                    double bias = DOUBLE_8061a948;
                                    double denom = (double)(count - 1);
                                    for (int i = 0; i < count; ++i) {
                                        if (colInRow == widthCols) {
                                            colInRow = 0;
                                            idx = idx + (8 - widthCols);
                                        }
                                        double t = (double)i / denom;
                                        (void)bias;
                                        unsigned int c = color_lerp_keep_lo((float)t, hiColor, loColor);
                                        ++colInRow;
                                        (&DAT_805d6630)[idx] = c;
                                        ++idx;
                                    }
                                    if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
                                        FUN_80348238(0xb);
                                        *(unsigned int *)(DAT_80612be4 + 0x238) =
                                            *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
                                    }
                                    aveditor_wset_hide(screen);
                                    avanim_play_move_sound(0x1626);
                                }
                            }
                        } else {
                            // copy the current slot's colour into the picked slot
                            avanim_play_move_sound(0x157a);
                            (&DAT_805d6630)[*(int *)(screen + 0x188)] =
                                (&DAT_805d6630)[(unsigned char)DAT_805d674c];
                            aveditor_wset_hide(screen);
                        }
                    }
                } else {
                    avanim_play_move_sound(0x1577);
                    hud_input_reset_repeat_timers();
                    aveditor_wset_hide(screen);
                }
            }
        }
    }
    FUN_80382718();
}

// Push-screen confirm callbacks (three distinct entry points that just push the next screen).
void aveditor_palette_push_cb(void)  { hud_screen_push(); }
void aveditor_palette_push_cb2(void) { hud_screen_push(); }
void aveditor_palette_push_cb3(void) { hud_screen_push(); }

// On palette-picker exit: if the working palette changed, play the confirm sound once, then copy
// the working palette (0x100 bytes) into the default palette block.
void aveditor_commit_palette_exit(void)
{
    if ((aveditor_palette_changed() != 0) &&
        ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
        FUN_80348238(0xc);
        *(unsigned int *)(DAT_80612be4 + 0x238) =
            *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
    }
    leaf_31e8(0x805d6630, 0x80480d10, 0x100);
}

// Enter the id sub-picker: if the active piece record has no weapon-set entry yet (+0x7c == 0),
// initialize a default entry; set the id-picker-active flag (0x10000000) and push the screen.
void aveditor_enter_id_picker(void)
{
    int rec = DAT_80612be4 +
        *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd14;
    if (*(int *)(rec + 0x7c) == 0) {
        unsigned int *entry = FUN_80336ba8(rec);
        aveditor_init_wset_entry((unsigned char *)entry, 1);
    }
    *(unsigned int *)(DAT_80612be4 + 0x25c) =
        *(unsigned int *)(DAT_80612be4 + 0x25c) | 0x10000000;
    hud_screen_push();
}

// Enter the weapon-set sub-picker: clear the id-picker-active flag, initialize the live palette-slot
// wset entry to the default, mark the active record as having an entry (+0x7c = 1), and push.
void aveditor_enter_wset_picker(void)
{
    short idx = *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc);
    *(unsigned int *)(DAT_80612be4 + 0x25c) =
        *(unsigned int *)(DAT_80612be4 + 0x25c) & 0xefffffff;
    int rec = DAT_80612be4 + idx * 0x8c + 0xdd14;
    aveditor_init_wset_entry((unsigned char *)&DAT_805d674c, 1);
    if (*(int *)(rec + 0x7c) == 0) {
        *(int *)(rec + 0x7c) = 1;
    }
    hud_screen_push();
}

// On screen focus, snapshot the active piece record's RGB triple into the live RGB block, unless
// the screen behind this one is the secondary-id picker (which manages its own colour).
void aveditor_snapshot_record_rgb(int screen)
{
    if (*(void **)(*(int *)(*(int *)(screen + 0x2c) + 0x170)) != (void *)&PTR_FUN_80480e60) {
        unsigned int *src = FUN_80336ba8(DAT_80612be4 +
            *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd14);
        unsigned int *dst = (unsigned int *)&DAT_805d6748;
        src = src - 1;
        for (int i = 2; i != 0; --i) {
            dst[1] = src[1];
            dst[2] = src[2];
            src += 2;
            dst += 2;
        }
        dst[3] = src[3];
    }
}

} // extern "C"
