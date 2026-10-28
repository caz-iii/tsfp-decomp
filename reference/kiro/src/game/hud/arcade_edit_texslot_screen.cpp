// Batch: batch_921 | Module: arcade map-editor texset/weapon-set slot list-screen glue
// Address range: 0x803288c8 - 0x803290bc
// Date: 2026-09-12
//
// Screen glue for the arcade map-editor texset/weapon-set slot list: navigation, add-with-budget
// check, select, per-item draw (icon + label + type badge), exit focus, and the swatch renderer.
// These build on the batch_920 slot-record management (aveditor_add_texslot, aveditor_copy_to_scratch,
// aveditor_get/set_wset_flag, aveditor_any_texset_slot). Slot records live in the editor context
// DAT_80612be4 at +0x288 (stride 0xe4, count +0x280); the item's packed slot index is at item+0x188
// (high word = slot). Slot type is at +0x2bc (+700); +0x2e0 is the child-count / expanded flag.
//
//   aveditor_no_texset_slot          0x803288c8  1 if no slot is a texset (clz of any_texset_slot).
//   aveditor_texslot_nav_forward     0x803288f0  set flag bit 1, navigate to screen by +0x254 index.
//   aveditor_add_texslot_and_focus   0x80328940  add a slot, store its index, set mode 3, run exit.
//   aveditor_add_texslot_checked     0x80328990  budget/count check then add, else a back dialog.
//   aveditor_texslot_select          0x80328a50  set wset flag from the item, copy to scratch, push.
//   aveditor_texslot_can_expand      0x80328aac  1 if the item can expand (children closing + count).
//   aveditor_draw_texslot_item       0x80328b1c  draw one list item: icon quad, label, type badge.
//   aveditor_texslot_exit            0x80328f64  on exit: cache the slot index or compute focus rect.
//   aveditor_texslot_icon_index      0x80329068  resolve the icon index for a slot record.
//   aveditor_draw_texslot_swatch     0x803290bc  draw the slot swatch/palette item plus expand mark.
//
// Editor context (DAT_80612be4): +0x238 status flags (bit 1 = navigate-requested), +0x254 screen
// index, +0x25c sub-mode flags (bit 0x40000000 = focus-request), +0x280 slot count, +0x288 slot
// array, +0x2f10/0x2f12 cached selected slot. See batch_920 for the slot layout.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get correctly-typed per-file externs; named cross-module
// helpers keep their real ABI. Paired-single FP math in the draw path uses the CONCAT44 u32->double
// idiom (0x43300000 magic) reproduced verbatim.

#include "common.h"

extern "C" {

extern int DAT_80612be4;   // editor context base

// context / draw globals
extern int DAT_80612bf4;                 // screen-nav context
extern void **PTR_PTR_8047f988;          // screen table by +0x254 index
extern int DAT_805264b4;                 // default texture id
extern int *DAT_80610a20;                // weapon-def table base
extern int DAT_806111f8;                 // label format-string seed
extern int DAT_8051a820;                 // screen-root table base
extern int DAT_806119ec;                 // active screen-root stride index
extern float FLOAT_80612c64, FLOAT_80612c68;   // focus-view source terms
extern float FLOAT_80612c74, FLOAT_80612c78;   // focus-view result terms

// layout float / double constants
extern float FLOAT_8061a9a0, FLOAT_8061a9a4, FLOAT_8061a9a8, FLOAT_8061a9ac, FLOAT_8061a9b0;
extern float FLOAT_8061a9c0;
extern float FLOAT_80611a64, FLOAT_80611a68;
extern double DOUBLE_8061a9b8;   // 2^52 + 2^31 bias magic (u32->double)

// cross-module / raw helpers (countLeadingZeros is a common.h intrinsic macro)
extern void hud_screen_navigate_to(int ctx, void *screen, int arg);
extern int  aveditor_add_texslot(unsigned long long, unsigned long long, unsigned long long,
                                 unsigned long long, unsigned long long, unsigned long long,
                                 unsigned long long, unsigned long long);   // batch_920
extern void aveditor_copy_to_scratch(unsigned int record);                  // batch_920
extern unsigned char aveditor_get_wset_flag(void);                          // batch_919
extern void aveditor_set_wset_flag(unsigned char v);                        // batch_919
extern int  aveditor_any_texset_slot(void);                                 // batch_920
extern unsigned int aveditor_paste_fits_budget(int size);
extern int  get_string_by_id(int id);
extern void hud_create_back_dialog(int screen, int ctx, int arg);
extern void FUN_80387fb8(unsigned long long, unsigned long long, unsigned long long,
                         unsigned long long, unsigned long long, unsigned long long,
                         unsigned long long, unsigned long long, int ctx, char *fmt,
                         int a, int r12, int r13, int r14, int r15, int r16);
extern void hud_screen_push(void);
extern int  hud_screen_all_children_closing(int screen);
extern unsigned long long FUN_803826d4(void);   // get current item + parent (spill pair)
extern unsigned long long FUN_803826e4(void);    // get current item (spill pair)
extern int  aveditor_get_weapon_set(void);
extern void menu_slot_option_table_resolve(int set);
extern int  FUN_803825c0(double scale);           // set text scale
extern int  hud_element_set_anim_value(int el, int which);
extern int  weapon_def_find_index_frameactor(void);
extern int  sprite_get_texture_id(int idx);
extern void gx_set_active_material(int tex);
extern void gx_draw_sprite_quad(double x0, double y0, double x1, double y1, int u0, int v0,
                                unsigned int u1, unsigned int v1);
extern void hud_item_compute_colors(int el, int item, unsigned int *cA, unsigned int *cB);
extern void text_set_font_table(int table);
extern double text_get_char_width(unsigned char *s);
extern void text_truncate_ellipsis(void);
extern void text_draw_char_shadowed(double x, double y, unsigned char *s, unsigned int cA,
                                    unsigned int cB);
extern int  FUN_8038b510(int *dst, int *src);      // string copy
extern int  FUN_8038b4a0(int dst, int src);        // string append
extern void text_apply_alpha_fade(double avail, unsigned char *s, int a, int b);
extern void text_set_color_rgb(double r, double g, double b);
extern void hud_screen_remove_handler(int el, int slot);
extern void hud_screen_register_handler(int el, int slot, int str, int a, int b);
extern int  hud_screen_find_root(int screen);
extern void avanim_focus_view(float *rect, int el, int mode, int *arg);
extern void FUN_80382720(void);   // restore fpr spill
extern void FUN_80382730(void);   // restore fpr spill
extern void screen_exit_clear_mode(void);
extern unsigned int FUN_8032a5ec(int slot);        // slot has no palette entries?
extern unsigned int aveditor_get_slot_texset(int slot);
extern int  aveditor_palette_item_texture_id(int idx);
extern int  hud_item_is_focused(int item);
extern void aveditor_draw_palette_item(int focused, int rect, int tex, unsigned int color,
                                       unsigned int frame, int arg);
extern void FUN_8032da30(double x, double y, double alpha);   // draw expand chevron

// forward declarations for intra-file callees defined later
void aveditor_texslot_exit(int item);
char aveditor_texslot_can_expand(int item, int slot);
int  aveditor_texslot_icon_index(int slot);

// ---- 0x803288c8 : 1 if no slot is a texset ------------------------------------------------------
unsigned int aveditor_no_texset_slot(void)
{
    int any = aveditor_any_texset_slot();
    return countLeadingZeros((unsigned int)any) >> 5;
}

// ---- 0x803288f0 : request navigation to the screen for the current +0x254 index -----------------
void aveditor_texslot_nav_forward(void)
{
    *(unsigned int *)(DAT_80612be4 + 0x238) |= 1;
    hud_screen_navigate_to(DAT_80612bf4,
                           PTR_PTR_8047f988[*(int *)(DAT_80612be4 + 0x254)], -1);
}

// ---- 0x80328940 : add a slot, store its index on the item, set mode 3, run exit ------------------
void aveditor_add_texslot_and_focus(unsigned long long param_1, unsigned long long param_2,
                                    unsigned long long param_3, unsigned long long param_4,
                                    unsigned long long param_5, unsigned long long param_6,
                                    unsigned long long param_7, unsigned long long param_8,
                                    int item)
{
    int idx = aveditor_add_texslot(param_1, param_2, param_3, param_4,
                                   param_5, param_6, param_7, param_8);
    *(int *)(item + 0x188) = idx << 0x10;
    *(int *)(DAT_80612be4 + 0x254) = 3;
    aveditor_texslot_exit(item);
}

// ---- 0x80328990 : add a slot after a budget/count check, else raise a back dialog ---------------
void aveditor_add_texslot_checked(unsigned long long param_1, unsigned long long param_2,
                                  unsigned long long param_3, unsigned long long param_4,
                                  unsigned long long param_5, unsigned long long param_6,
                                  unsigned long long param_7, unsigned long long param_8,
                                  int screen, int param_10, int param_11, int r12,
                                  int r13, int r14, int r15, int r16)
{
    unsigned int fits = aveditor_paste_fits_budget(0x58);
    if (*(int *)(DAT_80612be4 + 0x280) > 0x31 || fits == 0) {
        if (fits == 0) {
            int msg = get_string_by_id(0x95b);
            hud_create_back_dialog(screen, msg, 0);
        } else {
            char *fmt = (char *)get_string_by_id(0xb55);
            FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                         DAT_80612be4, fmt, 0x32, r12, r13, r14, r15, r16);
            hud_create_back_dialog(screen, DAT_80612be4, 0);
        }
    } else {
        aveditor_add_texslot(param_1, param_2, param_3, param_4,
                             param_5, param_6, param_7, param_8);
    }
}

// ---- 0x80328a50 : select a slot: latch its index, copy it to the scratch, push the screen -------
void aveditor_texslot_select(int item)
{
    aveditor_set_wset_flag((unsigned char)*(int *)(item + 0x188));
    unsigned char flag = aveditor_get_wset_flag();
    aveditor_copy_to_scratch((unsigned int)(DAT_80612be4 + (unsigned int)flag * 0xe4 + 0x288));
    hud_screen_push();
}

// ---- 0x80328aac : can the item expand? (index valid + children closing + child count) -----------
char aveditor_texslot_can_expand(int item, int slot)
{
    bool ok = false;
    if (*(int *)(item + 0x188) >= 0 &&
        hud_screen_all_children_closing(item) != 0 &&
        *(int *)(DAT_80612be4 + slot * 0xe4 + 0x2e0) > 0) {
        ok = true;
    }
    return (char)(-1 - (int)!ok);
}

// ---- 0x80328b1c : draw one texset/weapon-set list item ------------------------------------------
// Draws the slot icon quad, the truncated label, a per-type badge (strings 0xbaf/0xbb0/0xba5..
// 0xba7/0xbae by slot type at +0x2bc), and if expandable focuses the view on the slot's bounds.
void aveditor_draw_texslot_item(int param_1, int param_2, float *rect)
{
    unsigned short w, h;
    int el, item, slot, iVar4, iVar7, iVar10;
    unsigned int cA, cB;
    int animA, animB;
    int tex;
    char canExpand;
    float *pf;
    double dTop, dBot, dLeft, dRight, dTerm;
    unsigned int lo, hi;
    float focus[3];

    (void)param_1; (void)param_2;

    unsigned long long spill = FUN_803826d4();
    el   = (int)((unsigned long long)spill >> 0x20);
    item = *(int *)(el + 0x188);
    slot = *(int *)((int)spill + 0x14);
    if (*(int *)(DAT_80612be4 + 0x280) < item) {
        item = item >> 0x10;
    }

    iVar4 = aveditor_get_weapon_set();
    menu_slot_option_table_resolve(iVar4);
    slot = slot * 0xe4;

    FUN_803825c0((double)(FLOAT_8061a9a0 * *(float *)(el + 0xf0)));
    animA = hud_element_set_anim_value(el, 0);
    animB = hud_element_set_anim_value(el, 1);

    iVar7 = weapon_def_find_index_frameactor();
    tex = DAT_805264b4;
    if (iVar7 != -1) {
        tex = weapon_def_find_index_frameactor();
        tex = sprite_get_texture_id(tex);
    }

    dTop  = (double)(FLOAT_8061a9a4 * FLOAT_80611a64 + rect[1]);
    dBot  = -(double)(FLOAT_8061a9a8 * FLOAT_80611a64 - rect[3]);
    dLeft = (double)*rect;
    iVar7 = *DAT_80610a20 + tex * 0x58;
    dTerm = (double)(float)(dBot - dTop);
    w = *(unsigned short *)(iVar7 + 0x4e);
    h = *(unsigned short *)(iVar7 + 0x52);
    dRight = (double)(float)(dLeft + dTerm);
    gx_set_active_material(tex);
    gx_draw_sprite_quad(dLeft, dTop, dRight, dBot, 0, 0, (unsigned int)w << 4, (unsigned int)h << 4);

    hud_item_compute_colors(el, (int)spill, &cA, &cB);
    text_set_font_table(4);
    dRight = (double)(float)((double)FLOAT_8061a9ac * (double)FLOAT_80611a68 + dRight);
    dLeft  = (double)(rect[1] + FLOAT_80611a64);
    {
        unsigned char label[132];
        double cw = text_get_char_width((unsigned char *)(DAT_80612be4 + slot + 0x288));
        if ((double)(float)((double)rect[2] - dRight) < cw) {
            text_set_font_table(5);
        }
        text_truncate_ellipsis();
        text_draw_char_shadowed(dRight, dLeft, label, cA, cB);
        FUN_8038b510((int *)label, (int *)&DAT_806111f8);
        switch (*(int *)(DAT_80612be4 + slot + 700)) {
            case 0:  FUN_8038b4a0((int)label, get_string_by_id(0xbaf)); break;
            case 1:  FUN_8038b4a0((int)label, get_string_by_id(0xbb0)); break;
            case 2:  FUN_8038b4a0((int)label, get_string_by_id(0xba5)); break;
            case 3:  FUN_8038b4a0((int)label, get_string_by_id(0xba7)); break;
            case 4:  FUN_8038b4a0((int)label, get_string_by_id(0xba6)); break;
            case -1: FUN_8038b4a0((int)label, get_string_by_id(0xbae)); break;
            default: break;
        }
        text_set_font_table(0x11);
        text_apply_alpha_fade((double)(float)((double)rect[2] - dRight), label, 0, 1);
        text_draw_char_shadowed(dRight,
                                (double)(float)((double)FLOAT_8061a9a4 * dTerm + dTop),
                                label, (unsigned int)animA, (unsigned int)animB);
        {
            double gray = (double)FLOAT_8061a9b0;
            text_set_color_rgb(gray, gray, gray);
        }
        hud_screen_remove_handler(el, 2);
        canExpand = aveditor_texslot_can_expand(el, item);
        if (canExpand != '\0') {
            hud_screen_register_handler(el, 2, get_string_by_id(0xba8), 0, 0);
            iVar10 = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if ((*(unsigned int *)(iVar10 + 0xc0) & 0x40) != 0) {
                int rec = DAT_80612be4 + item * 0xe4 + 0x288;
                pf = *(float **)(rec + 100);
                lo = (unsigned int)(int)*(short *)(rec + 0x68) ^ 0x80000000;
                focus[0] = *pf + (float)((double)CONCAT44(0x43300000, lo) - DOUBLE_8061a9b8);
                hi = (unsigned int)(int)*(short *)(rec + 0x6a) ^ 0x80000000;
                focus[1] = pf[1] + (float)((double)CONCAT44(0x43300000, hi) - DOUBLE_8061a9b8);
                focus[2] = pf[2];
                *(unsigned int *)(DAT_80612be4 + 0x25c) |= 0x40000000;
                avanim_focus_view(focus, el, 3, (int *)0);
            }
        }
    }
    FUN_80382720();
}

// ---- 0x80328f64 : on exit, cache the selected slot index or compute the focus rect --------------
void aveditor_texslot_exit(int item)
{
    int packed = *(int *)(item + 0x188);
    if (packed >= 0) {
        int rec = DAT_80612be4 + (packed >> 0x10) * 0xe4;
        if (*(int *)(rec + 0x2e0) == 0) {
            *(short *)(DAT_80612be4 + 0x2f10) = (short)((unsigned int)packed >> 0x10);
            *(short *)(DAT_80612be4 + 0x2f12) = (short)*(int *)(item + 0x188);
        } else {
            float *pf = *(float **)(rec + (packed + (packed >> 0x10) * -0x10000) * 0x10 + 0x2ec);
            FLOAT_80612c78 = -(FLOAT_80612c68 * FLOAT_8061a9a4 -
                ((float)((double)CONCAT44(0x43300000,
                    *(unsigned int *)((int)pf[(int)pf[3] + 0xb] + 0x14) ^ 0x80000000)
                    - DOUBLE_8061a9b8) * FLOAT_8061a9a4 + *pf));
            FLOAT_80612c74 = -(FLOAT_80612c64 * FLOAT_8061a9a4 -
                ((float)((double)CONCAT44(0x43300000,
                    *(unsigned int *)((int)pf[(int)pf[3] + 0xb] + 0x18) ^ 0x80000000)
                    - DOUBLE_8061a9b8) * FLOAT_8061a9a4 + pf[1]));
        }
        screen_exit_clear_mode();
    }
}

// ---- 0x80329068 : resolve the icon index for a slot record --------------------------------------
int aveditor_texslot_icon_index(int slot)
{
    int rec = DAT_80612be4 + slot * 0xe4 + 0x288;
    unsigned int empty = FUN_8032a5ec(rec);
    if (empty == 0) {
        return *(int *)(rec + 0x28) + 0x38;
    }
    return 9;
}

// ---- 0x803290bc : draw the slot swatch/palette item plus the expand chevron ---------------------
void aveditor_draw_texslot_swatch(unsigned int param_1, unsigned int param_2, int rect)
{
    (void)param_1; (void)param_2;
    unsigned long long spill = FUN_803826e4();
    int slot = *(int *)((int)spill + 0x14) >> 0x10;
    unsigned int texset = aveditor_get_slot_texset(slot);
    int icon = aveditor_texslot_icon_index(slot);
    int tex  = aveditor_palette_item_texture_id(icon);
    int focused = hud_item_is_focused((int)spill);
    aveditor_draw_palette_item(focused, rect, tex, texset, 0xa0a0a20, 0);
    if (*(int *)(DAT_80612be4 + slot * 0xe4 + 0x2e0) < 1) {
        FUN_8032da30((double)(*(float *)(rect + 8) - FLOAT_8061a9c0),
                     (double)*(float *)(rect + 4),
                     (double)*(float *)((int)((unsigned long long)spill >> 0x20) + 0xf0));
    }
    FUN_80382730();
}

} // extern "C"
