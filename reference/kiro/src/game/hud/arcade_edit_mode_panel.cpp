// Batch: batch_980 | Module: arcade/challenge editor mode-label panel + small action helpers
// Address range: 0x80348238 - 0x803484f8
// Date: 2026-09-12
//
// This file collects the arcade-editor "current mode" HUD panel and a handful of tiny action
// helpers that sit just past the tip-builder block (see arcade_edit_mode_tips.cpp). The two
// substantial functions are:
//
//   panel_dispatch_tip_selection 0x80348238  Grabs the panel scratch buffer, zeroes 0x200 bytes of
//                                            it, and dispatches to one of the tip-builder handlers
//                                            through the PTR_tip_selection_ceb_80481ba4 function-
//                                            pointer table (indexed by the selection id). The tip
//                                            builders in arcade_edit_mode_tips.cpp are the entries
//                                            of this table.
//   aveditor_draw_mode_label_panel 0x80348298 Called each frame from aveditor_screen_tick. When the
//                                            editor context reports "mode-2"/"mode-3" for both the
//                                            current and previously-latched mode, it composes the
//                                            mode label (panel_get_mode2_label / _mode3_label),
//                                            measures/scales the text and shows panel slot 8 with the
//                                            computed position; otherwise it hides slot 8. The
//                                            latched mode id lives in DAT_80611264.
//
// The remaining five are leaf/utility routines:
//   arcade_set_pending_action 0x803484b8  store param into DAT_80612d98.
//   arcade_edit_noop          0x803484c0  empty body.
//   arcade_edit_slot_record   0x803484c4  index -> record pointer (index*0x14 + table base).
//   soundpad_nop_wrapper      0x803484d8  thin wrapper over soundpad_stub_nop.
//   challenge_reset_and_find_section 0x803484f8  brackets a challenge_record_find_section lookup
//                                            with the editor push/pop guards and three soundpad nops.
//
// NON-MATCHING: reproduces the decompiler's structure. The mode-label panel keeps the decompiler's
// float expressions verbatim (they mix Ghidra's CONCAT44/int-to-double idioms); only a couple carry
// real geometric meaning (line count, x/y placement written into the DAT_805e2cxx panel-slot record).
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols are declared file-locally with call-site-accurate arity/type
// (Compile-Clean rule 5); named helpers resolve through the shared headers (rule 6).

#include "common.h"

extern "C" {

// ---- named cross-module helpers (resolve via shared headers) ---------------
extern int   panel_get_scratch_ptr(void);
extern int   get_string_by_id(int id);
extern void  panel_slot_show(int idx);
extern void  panel_slot_hide(int idx);
extern unsigned int aveditor_is_mode_2(int mode);
extern unsigned int aveditor_is_mode_3(int mode);
extern int   panel_get_mode2_label(void);
extern int   panel_get_mode3_label(void);
extern int*  challenge_record_find_section(int* record, int tag);
extern void  soundpad_stub_nop(void);
extern unsigned char* text_check_scale(double h, unsigned char* s);

// ---- named helpers not yet in generated_decls (declared to call site) ------
extern int   text_set_font_table(int table);      // returns previous table id
extern double text_count_lines(double h, char* s);
extern double text_get_char_width(unsigned char* s);

// ---- raw callees (file-local, call-site arity/type) ------------------------
extern void  memset_wrapper(int dst, int val, int len);
extern void  leaf_31e8(unsigned int a, unsigned int b, int c);
extern int   FUN_8038b5c8(int str);
extern int   FUN_803826e4(void);                  // returns editor context pointer
extern void  FUN_80382730(void);                  // panel pop / end-draw guard
extern void  FUN_803826f0(void);                  // editor push guard
extern void  FUN_8038273c(void);                  // editor pop guard

// ---- data symbols ----------------------------------------------------------
extern int*  PTR_tip_selection_ceb_80481ba4[];    // tip-builder dispatch table
extern unsigned int   DAT_80611264;               // latched current-mode id
extern int            DAT_80612d98;               // pending arcade action
extern int            DAT_80612be8;               // challenge record base
extern int            DAT_805e2cd0;               // panel-visible flag (0 == build)
extern float          DAT_805e2cd4;               // panel anchor x
extern float          DAT_805e2cd8;
extern float          DAT_805e2cdc;
extern float          DAT_805e2ce0;
extern float          DAT_805e2ce8;
extern float          DAT_805e2cec;
extern float          DAT_805e2cf0;
extern float          DAT_805e2cf4;
extern float          DAT_805e2cf8;
extern int            DAT_805e2d0c;
extern float          FLOAT_8061acc8;
extern float          FLOAT_8061accc;
extern float          FLOAT_8061acd0;
extern float          FLOAT_8061acd4;
extern float          FLOAT_80611a64;
extern float          FLOAT_80611a68;
extern float          FLOAT_80611a6c;
extern float          FLOAT_80611a70;
extern double         DOUBLE_8061acd8;

// ---------------------------------------------------------------------------
// 0x80348238 : dispatch to a tip builder through the selection table.
// ---------------------------------------------------------------------------
void panel_dispatch_tip_selection(int selection)
{
    int scratch;

    scratch = panel_get_scratch_ptr();
    memset_wrapper(scratch, 0, 0x200);
    ((void (*)(int))PTR_tip_selection_ceb_80481ba4[selection])(scratch);
}

// ---------------------------------------------------------------------------
// 0x80348298 : per-frame "current mode" label panel (slot 8).
// ---------------------------------------------------------------------------
void aveditor_draw_mode_label_panel(void)
{
    int   ctx;
    int   prev_font;
    unsigned int is_mode;
    unsigned char* scaled;
    int   len;
    char* label;
    double anchor;
    double y_pos;
    double width;

    ctx    = FUN_803826e4();
    anchor = (double)((FLOAT_8061acc8 * FLOAT_80611a68 + FLOAT_80611a70) - DAT_805e2cd4);

    // On first entry (or when the latch is unset) capture the current mode id.
    if (-1 < (int)(((int)(DAT_80611264 ^ 0xffffffff) >> 1) -
                   ((DAT_80611264 ^ 0xffffffff) & DAT_80611264))) {
        DAT_80611264 = *(unsigned int*)(ctx + 0x188);
    }

    if (DAT_805e2cd0 == 0) {
        label     = (char*)0;
        prev_font = text_set_font_table(5);
        y_pos = (double)(*(float*)(*(int*)(ctx + 0x194) + 0x68) * FLOAT_80611a64 +
                         *(float*)(ctx + 0x60) * FLOAT_80611a64 + FLOAT_80611a6c);

        is_mode = aveditor_is_mode_2(*(int*)(ctx + 0x188));
        if ((is_mode != 0) && (aveditor_is_mode_2(DAT_80611264) != 0)) {
            label = (char*)panel_get_mode2_label();
        }
        is_mode = aveditor_is_mode_3(*(int*)(ctx + 0x188));
        if ((is_mode != 0) && (aveditor_is_mode_3(DAT_80611264) != 0)) {
            label = (char*)panel_get_mode3_label();
        }

        if (label == (char*)0) {
            panel_slot_hide(8);
        }
        else {
            double lines = text_count_lines(anchor, label);
            scaled = text_check_scale(anchor, (unsigned char*)label);
            width  = text_get_char_width(scaled);
            width  = (double)(float)((double)FLOAT_8061accc * (double)(FLOAT_8061acd0 * FLOAT_80611a68) + width);
            len    = FUN_8038b5c8((int)scaled);
            leaf_31e8((unsigned int)label, (unsigned int)scaled, len);

            DAT_805e2cdc = (float)((double)DAT_805e2cd4 - width);
            DAT_805e2cd8 = (float)y_pos;
            DAT_805e2cf0 = (float)((double)(((long long)(int)lines) ^ 0x80000000LL) - DOUBLE_8061acd8)
                           * (FLOAT_8061acd4 * FLOAT_80611a64 + FLOAT_80611a6c);
            DAT_805e2ce0 = (float)y_pos;
            DAT_805e2ce8 = (float)y_pos;
            DAT_805e2cf8 = (float)y_pos;
            DAT_805e2cec = (float)width;
            DAT_805e2d0c = 1;
            DAT_805e2cf4 = DAT_805e2cdc;
            panel_slot_show(8);
        }
        text_set_font_table(prev_font);
    }
    else if (DAT_80611264 != *(unsigned int*)(ctx + 0x188)) {
        panel_slot_hide(8);
    }

    DAT_80611264 = *(unsigned int*)(ctx + 0x188);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x803484b8 : record a pending arcade editor action.
// ---------------------------------------------------------------------------
void arcade_set_pending_action(int action)
{
    DAT_80612d98 = action;
}

// ---------------------------------------------------------------------------
// 0x803484c0 : no-op.
// ---------------------------------------------------------------------------
void arcade_edit_noop(void)
{
}

// ---------------------------------------------------------------------------
// 0x803484c4 : index -> record pointer (stride 0x14 into a fixed table).
// ---------------------------------------------------------------------------
int arcade_edit_slot_record(int index)
{
    return index * 0x14 + -0x7fb7e268;
}

// ---------------------------------------------------------------------------
// 0x803484d8 : thin wrapper around the soundpad nop stub.
// ---------------------------------------------------------------------------
void soundpad_nop_wrapper(void)
{
    soundpad_stub_nop();
}

// ---------------------------------------------------------------------------
// 0x803484f8 : reset guards + locate a challenge record section.
// ---------------------------------------------------------------------------
void challenge_reset_and_find_section(void)
{
    FUN_803826f0();
    challenge_record_find_section((int*)DAT_80612be8, 0xc77668);
    soundpad_nop_wrapper();
    soundpad_nop_wrapper();
    soundpad_nop_wrapper();
    FUN_8038273c();
}

} // extern "C"
