// Batch: batch_922 | Module: arcade map-editor texset/weapon-set slot ops (cursor/action list/delete)
// Address range: 0x80329170 - 0x80329760
// Date: 2026-09-12
//
// The rest of the arcade map-editor texset/weapon-set slot screen: selection-cursor renderers, the
// exit focus-bounds variants, the per-slot action sub-list builder, and the slot delete flow with
// its undo backup buffer. Builds on batch_920/921 (aveditor_texslot_ptr, aveditor_get_slot_texset,
// the slot record layout at ctx DAT_80612be4 +0x288 stride 0xe4, count +0x280).
//
//   aveditor_texslot_focus_bounds      0x80329170  compute the focus-view result terms from a slot.
//   aveditor_draw_texslot_cursor       0x8032924c  draw the plain selection cursor over the rect.
//   aveditor_texslot_cache_shallow     0x8032929c  cache the selected slot if its child count < 8.
//   aveditor_draw_texslot_expand_icon  0x803292fc  draw the expand-arrow sprite (0x28c) at the rect.
//   aveditor_draw_texslot_cursor_fitted 0x8032943c draw the cursor fitted to the 0x33 cursor size.
//   aveditor_build_texslot_actions     0x8032956c  build the 2-item slot action sub-list (tags vary).
//   aveditor_delete_texslot            0x80329650  remove the current slot: shift the array, dec count.
//   aveditor_save_delete_backup        0x803296f4  alloc a 0x20 delete-undo backup and copy the slot.
//   aveditor_free_delete_backup        0x80329738  free the delete-undo backup buffer.
//   aveditor_delete_texslot_confirmed  0x80329760  backup + delete the DAT_806111fc slot, rebuild, sound.
//
// Editor context (DAT_80612be4): +0x238 status flags (bit 1 = navigate, bit 0x20 = sound latch),
// +0x280 slot count, +0x288 slot array (stride 0xe4). Slot: +0x2bc(+700) type, +0x2e0 child count.
// DAT_80612cc0 = delete-undo backup buffer (0x20). DAT_806111fc = slot index for the confirm handler.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ get correctly-typed per-file externs; named helpers keep their real
// ABI. Paired-single FP paths reproduce the CONCAT44 u32->double (0x43300000 magic) idiom verbatim.

#include "common.h"

extern "C" {

extern int DAT_80612be4;   // editor context base
extern int DAT_80612cc0;   // delete-undo backup buffer (0x20 bytes)
extern unsigned int DAT_806111fc;   // slot index for the delete-confirm handler
extern int *DAT_80610a20;  // weapon-def / sprite table base

// focus-view terms + layout constants
extern float FLOAT_80612c64, FLOAT_80612c68;
extern float FLOAT_80612c74, FLOAT_80612c78;
extern float FLOAT_8061a9a4, FLOAT_8061a9c4, FLOAT_8061a9c8, FLOAT_8061a9cc, FLOAT_8061a9d0;
extern double DOUBLE_8061a9b8;

// cross-module / raw helpers
extern void screen_exit_clear_mode(void);
extern unsigned int aveditor_get_slot_texset(int slot);
extern void cursor_draw_stretched(double x0, double y0, double x1, double y1);
extern unsigned long long FUN_803826f0(void);   // get current item (spill; r4 = element)
extern int  sprite_get_texture_id(int idx);
extern void gx_set_active_material(int tex);
extern void gx_draw_sprite_quad(double x0, double y0, double x1, double y1, int u0, int v0,
                                unsigned int u1, unsigned int v1);
extern void FUN_8038273c(void);   // restore fpr spill
extern unsigned short cursor_get_width(int id);
extern unsigned short cursor_get_height(int id);
extern void hud_item_compute_colors(int el, int item, unsigned int *cA, unsigned int *cB);
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern void hud_begin_sub_list(int a, int b, int c);
extern double hud_get_content_height(void);
extern void hud_add_item_internal(double w, double h);
extern unsigned int FUN_8032a5ec(int slot);   // slot has no palette entries?
extern void hud_set_last_child_tag(int tag);
extern void hud_end_sub_list(void);
extern void FUN_8032c6fc(void);    // pre-delete cleanup (free slot resources)
extern void leaf_31e8(int dst, int src, int n);   // memcpy leaf
extern int  aveditor_texslot_ptr(unsigned int index);   // batch_920
extern int  mem_alloc(unsigned long long, unsigned long long, unsigned long long,
                      unsigned long long, unsigned long long, unsigned long long,
                      unsigned long long, unsigned long long, int size, int align,
                      int r11, int r12, int r13, int r14, int r15, int r16);
extern void mem_free_top(int p);
extern void hud_screen_rebuild_items(void);
extern void FUN_80348238(int screenIndex);   // build/register screen

// ---- 0x80329170 : compute the focus-view result terms from a slot's geometry --------------------
void aveditor_texslot_focus_bounds(int item)
{
    int packed = *(int *)(item + 0x188);
    if (packed >= 0) {
        float *pf = *(float **)(DAT_80612be4 + (packed >> 0x10) * -0xfff1c + packed * 0x10 + 0x2ec);
        FLOAT_80612c78 = -(FLOAT_80612c68 * FLOAT_8061a9a4 -
            ((float)((double)CONCAT44(0x43300000,
                *(unsigned int *)((int)pf[(int)pf[3] + 0xb] + 0x14) ^ 0x80000000)
                - DOUBLE_8061a9b8) * FLOAT_8061a9a4 + *pf));
        FLOAT_80612c74 = -(FLOAT_80612c64 * FLOAT_8061a9a4 -
            ((float)((double)CONCAT44(0x43300000,
                *(unsigned int *)((int)pf[(int)pf[3] + 0xb] + 0x18) ^ 0x80000000)
                - DOUBLE_8061a9b8) * FLOAT_8061a9a4 + pf[1]));
        screen_exit_clear_mode();
    }
}

// ---- 0x8032924c : draw the plain selection cursor over the item rect ----------------------------
void aveditor_draw_texslot_cursor(unsigned int param_1, int item, float *rect)
{
    (void)param_1;
    aveditor_get_slot_texset(*(int *)(item + 0x14) >> 0x10);
    cursor_draw_stretched((double)rect[0], (double)rect[1], (double)rect[2], (double)rect[3]);
}

// ---- 0x8032929c : cache the selected slot if its child count is < 8 -----------------------------
void aveditor_texslot_cache_shallow(int item)
{
    int packed = *(int *)(item + 0x188);
    if (packed >= 0) {
        if (*(int *)(DAT_80612be4 + (packed >> 0x10) * 0xe4 + 0x2e0) < 8) {
            *(short *)(DAT_80612be4 + 0x2f10) = (short)((unsigned int)packed >> 0x10);
            *(short *)(DAT_80612be4 + 0x2f12) = (short)*(int *)(item + 0x188);
        }
        screen_exit_clear_mode();
    }
}

// ---- 0x803292fc : draw the expand-arrow sprite (0x28c) fitted to the rect -----------------------
void aveditor_draw_texslot_expand_icon(unsigned int param_1, unsigned int param_2, float *rect)
{
    (void)param_1; (void)param_2;
    unsigned long long spill = FUN_803826f0();
    int el = (int)((unsigned long long)spill >> 0x20);
    int slot = *(int *)(el + 0x14);
    int tex = sprite_get_texture_id(0x28c);
    double dBot = (double)rect[3];
    double dTop = (double)rect[1];
    double dPad = (double)(FLOAT_8061a9c4 * (float)(dBot - dTop));
    gx_set_active_material(tex);
    aveditor_get_slot_texset(slot >> 0x10);
    tex = sprite_get_texture_id(0x28c);
    unsigned short h = *(unsigned short *)(*DAT_80610a20 + tex * 0x58 + 0x52);
    tex = sprite_get_texture_id(0x28c);
    gx_draw_sprite_quad((double)rect[0], (double)(float)(dTop + dPad),
                        (double)rect[2], (double)(float)(dBot - dPad), 0, 0,
                        (unsigned int)*(unsigned short *)(*DAT_80610a20 + tex * 0x58 + 0x4e) << 4,
                        (unsigned int)h << 4);
    FUN_8038273c();
}

// ---- 0x8032943c : draw the selection cursor fitted to the 0x33 cursor sprite size ---------------
void aveditor_draw_texslot_cursor_fitted(unsigned int param_1, unsigned int param_2, float *rect)
{
    (void)param_1; (void)param_2;
    unsigned long long spill = FUN_803826f0();
    unsigned int cA[3];
    unsigned int cB;

    unsigned short w = cursor_get_width(0x33);
    unsigned int wBits = (unsigned int)w ^ 0x80000000;
    double dW = (double)(float)((double)CONCAT44(0x43300000, wBits) - DOUBLE_8061a9b8);
    unsigned short h = cursor_get_height(0x33);
    unsigned int hBits = (unsigned int)h ^ 0x80000000;
    double dH = (double)(float)((double)CONCAT44(0x43300000, hBits) - DOUBLE_8061a9b8);
    double x0 = (double)((float)((double)(rect[2] - rect[0]) - dW) * FLOAT_8061a9a4 + rect[0]);
    double y0 = (double)((float)((double)(rect[3] - rect[1]) - dH) * FLOAT_8061a9a4 + rect[1]);
    hud_item_compute_colors((int)((unsigned long long)spill >> 0x20), (int)spill, cA, &cB);
    cursor_draw_stretched(x0, y0, (double)(float)(x0 + dW), (double)(float)(y0 + dH));
    FUN_8038273c();
}

// ---- 0x8032956c : build the 2-item slot action sub-list (tags depend on whether the slot is empty)
void aveditor_build_texslot_actions(void)
{
    int slot = __save_gpr_28();
    slot = DAT_80612be4 + slot * 0xe4 + 0x288;
    hud_begin_sub_list(0, 0, 2);
    double ch = hud_get_content_height();
    hud_add_item_internal((double)(float)(ch - (double)FLOAT_8061a9c8), (double)FLOAT_8061a9cc);
    hud_set_last_child_tag(FUN_8032a5ec(slot) != 0 ? 0xab : 0xb1);
    hud_add_item_internal((double)FLOAT_8061a9d0, (double)FLOAT_8061a9cc);
    hud_set_last_child_tag(FUN_8032a5ec(slot) != 0 ? 0xac : 0xb2);
    hud_end_sub_list();
    __restore_gpr_28();
}

// ---- 0x80329650 : remove the current slot: free its resources, shift the array down, dec count ---
void aveditor_delete_texslot(void)
{
    int slot = FUN_803826f0();
    int count = *(int *)(DAT_80612be4 + 0x280);
    if (*(int *)(DAT_80612be4 + slot * 0xe4 + 700) == 5 &&
        (*(unsigned int *)(DAT_80612be4 + 0x238) & 1) != 0) {
        *(unsigned int *)(DAT_80612be4 + 0x238) &= 0xfffffffe;
    }
    FUN_8032c6fc();
    leaf_31e8(DAT_80612be4 + slot * 0xe4 + 0x288,
              DAT_80612be4 + (slot + 1) * 0xe4 + 0x288,
              (count - (slot + 1)) * 0xe4);
    *(int *)(DAT_80612be4 + 0x280) = *(int *)(DAT_80612be4 + 0x280) - 1;
    FUN_8038273c();
}

// ---- 0x803296f4 : allocate a 0x20 delete-undo backup and copy the slot record in ----------------
void aveditor_save_delete_backup(unsigned long long param_1, unsigned long long param_2,
                                 unsigned long long param_3, unsigned long long param_4,
                                 unsigned long long param_5, unsigned long long param_6,
                                 unsigned long long param_7, unsigned long long param_8,
                                 unsigned int record, int param_10, int r11, int r12,
                                 int r13, int r14, int r15, int r16)
{
    (void)param_10;
    DAT_80612cc0 = mem_alloc(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             0x20, 0, r11, r12, r13, r14, r15, r16);
    leaf_31e8(DAT_80612cc0, (int)record, 0x20);
}

// ---- 0x80329738 : free the delete-undo backup buffer --------------------------------------------
int aveditor_free_delete_backup(void)
{
    mem_free_top(DAT_80612cc0);
    return DAT_80612cc0;
}

// ---- 0x80329760 : back-up + delete the DAT_806111fc slot, rebuild the list, play the sound -------
void aveditor_delete_texslot_confirmed(unsigned long long param_1, unsigned long long param_2,
                                       unsigned long long param_3, unsigned long long param_4,
                                       unsigned long long param_5, unsigned long long param_6,
                                       unsigned long long param_7, unsigned long long param_8,
                                       int item, int param_10, int r11, int r12,
                                       int r13, int r14, int r15, int r16)
{
    unsigned int rec = (unsigned int)aveditor_texslot_ptr(DAT_806111fc & 0xff);
    aveditor_save_delete_backup(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                                param_8, rec, param_10, r11, r12, r13, r14, r15, r16);
    aveditor_delete_texslot();
    *(int *)(item + 0x188) = -2;
    hud_screen_rebuild_items();
    if ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
        FUN_80348238(0x12);
        *(unsigned int *)(DAT_80612be4 + 0x238) |= 0x20;
    }
}

} // extern "C"
