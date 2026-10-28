// Batch: batch_948 | Module: arcade custom-match game-options settings screen
// Address range: 0x8033435c - 0x8033576c
// Date: 2026-09-12
//
// The custom-match ("arcade custom game") options settings screen. Unlike the map-editor config
// screens (which key off the ctx+0x2f1c edit record), this screen operates on the active match
// category record reached through DAT_80612be4 + 0x11858 (the "current match slot" pointer). It
// builds the big scrollable options page for a match mode: per-mode sliders (score/frag/time
// limits, palette selection, team/handicap toggles), the shared option-flags sub-list, and a
// value selector for the primary limit; plus a family of small screen-enter wrappers that share a
// single commit/cleanup step.
//
//   match_option_is_disabled      0x8033435c  is match option bit `opt` unavailable for the slot.
//   match_editor_build_options    0x8033446c  build the shared "options" flag list (per-mode caps).
//   match_editor_build_screen     0x80334630  build the whole custom-match options screen.
//   match_editor_enter_from_grid  0x80335514  enter the editor from a grid slot (sets active slot).
//   match_editor_enter            0x80335594  enter the editor (no slot change).
//   match_editor_enter_alt        0x803355f4  enter the editor (alternate entry, no slot change).
//   match_editor_commit_cleanup   0x80335654  commit pending changes / run mode-specific cleanup.
//   match_editor_get_value_notify 0x803356e4  read a linked value (+0x24), notifying on dirty.
//   match_editor_refresh          0x80335730  commit + re-push the screen.
//   match_editor_enter_or_pop     0x8033576c  commit, then push a fresh screen or pop the current.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get correctly-typed per-file externs; already-named
// cross-module helpers are re-declared here with their real ABI so the call-site types match.

#include "common.h"

extern "C" {

// -- editor / match globals --
extern int  DAT_80612be4;   // editor context base
extern int  DAT_80612d00;   // active match-grid slot object (0 if none)
extern int  DAT_80612d0c;   // mode flavour flag (1 = variant A option tables)
extern int  DAT_80612cfc;   // "editing existing" flag (affects exit label / entry-or-pop)
extern int  DAT_806119e0;   // shared position/anim descriptor arg

// -- per-mode option string/flag tables (indexed by mode record) --
extern unsigned char DAT_80480f20;   // variant-A caps table (byte-indexed)
extern unsigned char DAT_80480f28;   // variant-B caps table (byte-indexed)
extern int  DAT_80480f10;             // per-mode "options header" string ids (stride 0x13*4)
extern int  DAT_80480f1c;             // per-mode value-selector label string ids
extern unsigned int DAT_80480f30;     // per-mode config flag words (stride 0x4c)
extern int  DAT_80481424;             // variant-A option-pair string id table
extern int  DAT_8048148c;             // variant-B option-pair string id table
extern int  DAT_804814d0;             // extra "locked" option-pair string id
extern short DAT_80477bc8;            // palette-entry -> string id table (stride 0xc4)
extern unsigned char DAT_804816ea;    // grid-slot capability flags (stride 0x44)

// -- compiler runtime save/restore helpers emitted for the callee-saved GPR frame --
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);

// -- geometry / animation float constants --
extern float FLOAT_8061aa14;  // callback-item size
extern float FLOAT_8061aa1c;  // layout coefficient
extern float FLOAT_8061aa18;  // scroll overflow offset
extern float FLOAT_8061aa20;  // layout coefficient
extern float FLOAT_8061aa24;  // layout coefficient
extern float FLOAT_8061aa28;  // common header/text/slider size (0 baseline)
extern float FLOAT_8061aa40;  // anim base position
extern float FLOAT_8061aa68;  // anim position bias
extern float FLOAT_8061aa90;  // padding-item size
extern float FLOAT_8061aaa8;  // slider size (variant A)
extern float FLOAT_8061aac4;  // slider size (variant B)
extern float FLOAT_8061aac8;  // sub-list content offset
extern float FLOAT_8061aacc;  // anim duration coefficient
extern double DOUBLE_8061aa48; // int->double normalisation bias (0x4330...)
extern float FLOAT_80611a5c;  // screen scroll coefficient
extern float FLOAT_80611a64;  // screen layout coefficient
extern float FLOAT_80611a68;  // anim position coefficient
extern float FLOAT_80611a6c;  // screen top Y
extern float FLOAT_80611a70;  // anim position offset

// -- raw helpers not individually named (this module) --
extern int   FUN_803826d0(void);          // fetch editor screen element (variant C)
extern void  FUN_8038271c(void);           // finalize screen (variant C)
extern int   FUN_803392d4(int slot);       // slot: needs mode-cleanup prompt?
extern void  FUN_80339250(int slot);       // notify slot value changed
extern void  FUN_80339370(void);           // finalize commit / dirty flush
extern void  FUN_80348238(int promptId);   // queue a modal prompt
extern unsigned char *FUN_80336c44(int record, int which); // fetch a per-axis slider target ptr

// -- already-named cross-module helpers (real ABI) --
extern int   get_string_by_id(int stringId);
extern int   match_cap_apply_all(void);
extern int   match_cat_is_locked_pair(int a, int b);
extern int   match_mode_type_is_eligible(int a, int b, int c);
extern int   popcount32(unsigned int x);
extern void  match_editor_screen_enter(void);           // close/enter callback (used as fn ptr)
extern unsigned char *aveditor_palette_item_texture_root(void);
extern unsigned int aveditor_cell_type_is_selectable(int cellRecord);
extern int   aveditor_build_cell_tooltip(unsigned long long a, double b, double c, double d,
                                         double e, double f, double g, unsigned long long h);
extern unsigned int aveditor_count_placed_for_type(int typeId);
extern int   aveditor_is_cell_selected(int cellIndex);
extern unsigned long long aveditor_get_entry_position(int cellIndex);
extern double match_selection_row_height(void);
extern double hud_get_content_height(void);

extern void  hud_screen_push(void);
extern void  hud_screen_pop_current(void);
extern void  hud_element_init_from_layout(int layoutId, int a, int b, void *closeCb);
extern void  hud_set_item_padding_mode(double pad, int a, int b, int c);
extern void  hud_set_context_property(int prop);
extern void  hud_element_scroll_h_overflow(double amount, double a, double b, int c);
extern void  hud_begin_scroll_region(void);
extern void  hud_begin_scroll_with_item(void);
extern void  hud_add_list_item(void);
extern unsigned long long hud_end_scroll_region(void);
extern void  hud_item_add_header(double size, int flags);
extern void  hud_set_last_child_tag(int tag);
extern void  hud_event_add_pair(void);
extern void  hud_item_set_linked_value(int *value);
extern void  hud_item_add_slider(double size, double range, int a, int target, int steps,
                                 int label, int flags);
extern void  hud_item_add_value_selector(int a, int *value, int lo, int hi, int step,
                                         int label, int flags);
extern void  hud_begin_sub_list(int a, int b, int cols);
extern void  hud_add_item_internal(double a, double b);
extern void  hud_end_sub_list(void);
extern void  hud_item_add_text_display(double size, int flags);
extern void  hud_item_set_value_display(int value);
extern void  hud_add_padding_item(double size);
extern void  hud_add_callback_item(double size);
extern void  hud_element_apply_scroll(void);
extern void  hud_element_set_pos_y(double y, int descriptor);
extern void  hud_element_finalize_frame(void);
extern void  avanim_snapshot_current(void);
extern void  avanim_begin_to(double x, double y, double z, double dur, int flags);

} // extern "C"

// The option-availability tables are addressed in the decomp as *(uint*)(idx*0x10 + <neg base>).
// The negative constants are the sign-extended low halfwords of a real 0x80481xxx base; recover the
// true byte addresses here. -0x7fb7ec24 == 0x80481_3dc, -0x7fb7ec2c == 0x80481_3d4.
#define OPT_TABLE_A ((unsigned int *)0x804813dcu)   // team/solo availability (idx*0x10)
#define OPT_TABLE_B ((unsigned int *)0x804813d4u)   // handicap-variant availability (idx*0x10 + h*4)

// forward prototypes for the intra-module functions referenced before their definitions
unsigned int match_option_is_disabled(int *slot, int opt);
void match_editor_build_options(void);
void match_editor_commit_cleanup(void);

// -----------------------------------------------------------------------------

// Is match option bit `opt` unavailable for the given match slot? `slot` == 0 means the active
// slot (DAT_80612be4+0x11858). Maps the slot's mode code (record+3, table at +0xdd98) to one of
// three option groups (0/1/2), then tests the corresponding availability bitmask; returns 1 when
// the option is NOT set (disabled), 0 otherwise.
unsigned int match_option_is_disabled(int *slot, int opt)
{
    if (slot == (int *)0) {
        slot = *(int **)(DAT_80612be4 + 0x11858);
    }

    int mode = *(int *)(DAT_80612be4 + *(short *)(slot + 3) * 0x8c + 0xdd98);
    int group;
    if (mode == 0x43) {
        group = 1;
    } else if (mode < 0x43) {
        if ((0x37 < mode) || (mode < 0x35)) {
            return 0;
        }
        group = 2;
    } else {
        if (0x4c < mode) {
            return 0;
        }
        if (mode < 0x49) {
            return 0;
        }
        group = 0;
    }

    int kind = *slot;
    if (kind == 2) {
        if (((1 << opt) & OPT_TABLE_A[group * 4]) == 0) {
            return 1;
        }
    } else if ((kind < 2) && (0 < kind)) {
        unsigned int handicap = (unsigned int)(*(unsigned char *)(slot + 0x2c) >> 1) & 1;
        if (((1 << opt) & OPT_TABLE_B[group * 4 + handicap]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Build the shared "options" flag list for the current mode: one on/off pair per enabled option bit
// (that is not disabled for the slot), using the variant-A or variant-B string tables. Only emitted
// when the mode exposes more than one capability.
void match_editor_build_options(void)
{
    int *slot = (int *)__save_gpr_25();

    int caps = match_cap_apply_all();
    if (1 < caps) {
        get_string_by_id(0xb0f);
        hud_begin_scroll_with_item();
        hud_item_add_header((double)FLOAT_8061aa28, 0x800);
        hud_set_last_child_tag(0xfa);

        int modeRow = *(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4);

        if (DAT_80612d0c == 1) {
            int opt = 0;
            int strOff = 0;
            do {
                if (((1 << opt) & (int)(&DAT_80480f20)[modeRow * 0x13]) != 0) {
                    if (match_option_is_disabled((int *)0, opt) == 0) {
                        get_string_by_id(*(int *)((int)&DAT_80481424 + strOff));
                        hud_event_add_pair();
                    }
                }
                opt = opt + 1;
                strOff = strOff + 4;
            } while (opt < 0x1a);
        } else {
            int opt = 0;
            int strOff = 0;
            do {
                if (((1 << opt) & (int)(&DAT_80480f28)[modeRow * 0x13]) != 0) {
                    if (match_option_is_disabled((int *)0, opt) == 0) {
                        get_string_by_id(*(int *)((int)&DAT_8048148c + strOff));
                        hud_event_add_pair();
                    }
                }
                opt = opt + 1;
                strOff = strOff + 4;
            } while (opt < 0x1a);

            int locked = match_cat_is_locked_pair(*slot, slot[1]);
            if (locked != 0) {
                int state = *(int *)(DAT_80612be4 + *(short *)((int)slot + 0xe) * 0xe4 + 700);
                if ((state == 2) || (state == 4) || (state == 3)) {
                    get_string_by_id(DAT_804814d0);
                    hud_event_add_pair();
                }
            }
        }

        hud_item_set_linked_value(slot + 2);
        hud_end_scroll_region();
    }

    __restore_gpr_25();
}

// Build the whole custom-match options screen for the active slot: title, palette-overflow scroll,
// per-mode sliders/selectors (score/frag/time limits, palette, teams), the shared options list, a
// primary value selector, the exit callback row, then finalize + optionally animate to the selected
// grid cell.
void match_editor_build_screen(void)
{
    int screen = FUN_803826d0();
    int slotPtr = *(int *)(DAT_80612be4 + 0x11858);
    int titleId = 0xd1e;
    int modeRow = *(int *)(slotPtr + 4);
    int cfgOff  = modeRow * 0x4c;

    unsigned int caps;
    if (DAT_80612d0c == 1) {
        titleId = 0xd20;
        caps = (&DAT_80480f20)[modeRow * 0x13];
    } else {
        caps = (&DAT_80480f28)[modeRow * 0x13];
    }

    int limitRec = DAT_80612be4 + *(short *)(slotPtr + 0xc) * 0x8c + 0xdd14;

    titleId = get_string_by_id(titleId);
    hud_element_init_from_layout(0x8400, titleId, 0, (void *)match_editor_screen_enter);
    hud_set_item_padding_mode((double)FLOAT_8061aa28, 1, 1, 1);
    hud_set_context_property(0x20000);

    double c6c = (double)FLOAT_80611a6c;
    double c64 = (double)FLOAT_80611a64;
    double base = (double)FLOAT_8061aa28;
    double k1c = (double)FLOAT_8061aa1c;
    double k5c = (double)FLOAT_80611a5c;
    double overflow =
        -(double)(float)(k5c * (double)(float)((double)(float)(k1c * c64 +
                 (double)(float)((double)FLOAT_8061aa20 * c64 +
                 (double)(float)((double)FLOAT_8061aa24 * c64 + c6c))) - c6c) -
                 (double)FLOAT_8061aa18);
    hud_element_scroll_h_overflow(overflow, base, base, 0);

    int listTitle = (DAT_80612d0c == 1) ? 0xb0d : 0xb0e;
    get_string_by_id(listTitle);
    hud_begin_scroll_region();
    hud_add_list_item();
    hud_end_scroll_region();

    // Palette / limit slider (skipped for certain race/coop modes with valid child slots).
    if ((*(unsigned int *)(&DAT_80480f30 + cfgOff) & 2) == 0) {
        int sp = *(int *)(DAT_80612be4 + 0x11858);
        if (((*(int *)(sp + 4) != 9) && (*(int *)(sp + 4) != 0xc)) ||
            ((*(short *)(sp + 0xc) < 0) && (*(short *)(sp + 0xe) < 0))) {
            get_string_by_id((&DAT_80480f10)[modeRow * 0x13]);
            hud_begin_scroll_with_item();
            if (*(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) < 0) {
                int child = (int)*(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xe);
                if (-1 < child) {
                    int rec = DAT_80612be4 + child * 0xe4;
                    hud_item_add_slider((double)FLOAT_8061aa28, (double)FLOAT_8061aaa8, 0,
                                        rec + 0x288, 0x1f, rec + 0x2a8, 0x2000);
                }
            } else {
                hud_item_add_slider((double)FLOAT_8061aa28, (double)FLOAT_8061aaa8, 0,
                                    limitRec, 0x1f, limitRec + 0x78, 0x2000);
            }
            hud_set_last_child_tag(0xef);
            hud_end_scroll_region();
        }
    }

    // Mode 9: two sliders (limit + axis).
    if (*(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4) == 9) {
        get_string_by_id(0x9d2);
        hud_begin_scroll_with_item();
        hud_item_add_slider((double)FLOAT_8061aa28, (double)FLOAT_8061aaa8, 0,
                            limitRec, 0x1f, limitRec + 0x78, 0x2000);
        hud_set_last_child_tag(0xf0);
        hud_end_scroll_region();

        get_string_by_id(0x9d3);
        hud_begin_scroll_with_item();
        unsigned char *tgt = FUN_80336c44(limitRec, 0);
        hud_item_add_slider((double)FLOAT_8061aa28, (double)FLOAT_8061aac4, 0,
                            (int)tgt, 0x3f, limitRec + 0x20, 0);
        hud_set_last_child_tag(0xf1);
        hud_end_scroll_region();
    }

    // Mode 7: axis slider + a two-entry toggle header.
    if (*(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4) == 7) {
        get_string_by_id(0x9d0);
        hud_begin_scroll_with_item();
        unsigned char *tgt = FUN_80336c44(limitRec, 0);
        hud_item_add_slider((double)FLOAT_8061aa28, (double)FLOAT_8061aac4, 0,
                            (int)tgt, 0x3f, limitRec + 0x20, 0);
        hud_set_last_child_tag(0xf2);
        hud_end_scroll_region();

        get_string_by_id(0xb6d);
        hud_begin_scroll_with_item();
        hud_item_add_header((double)FLOAT_8061aa28, 0x800);
        hud_set_last_child_tag(0xf3);
        get_string_by_id(0xb6e);
        hud_event_add_pair();
        get_string_by_id(0xb6f);
        hud_event_add_pair();
        hud_item_set_linked_value(
            (int *)(DAT_80612be4 + *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd90));
        hud_end_scroll_region();
    }

    // Mode 0xd: palette-based picker with a per-entry linked header + a text display.
    if (*(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4) == 0xd) {
        unsigned char *pal = aveditor_palette_item_texture_root();
        int rec = DAT_80612be4 + *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd14;
        if (*(int *)(rec + 0x84) < 0) {
            *(int *)(rec + 0x84) = **(int **)(pal + 4);
        }

        get_string_by_id(0xb72);
        hud_begin_scroll_with_item();
        hud_begin_sub_list(0, 0, 2);
        double rh = match_selection_row_height();
        double ch = hud_get_content_height();
        hud_add_item_internal((double)(float)(ch - (double)FLOAT_8061aac8), rh);
        rh = match_selection_row_height();
        hud_add_item_internal((double)FLOAT_8061aa28, rh);
        hud_set_last_child_tag(0xff);
        hud_end_sub_list();
        hud_end_scroll_region();

        get_string_by_id(0xb73);
        hud_begin_scroll_with_item();
        hud_item_add_header((double)FLOAT_8061aa28, 0x800);
        hud_set_last_child_tag(0xf4);
        int off = 0;
        for (int i = 0; i < *(int *)(pal + 8); i = i + 1) {
            get_string_by_id((int)*(short *)(&DAT_80477bc8 +
                             *(int *)(*(int *)(pal + 4) + off) * 0xc4));
            hud_event_add_pair();
            off = off + 4;
        }
        hud_item_set_linked_value((int *)(rec + 0x84));
        hud_add_padding_item((double)FLOAT_8061aa90);

        get_string_by_id(0xb75);
        hud_add_list_item();
        hud_item_add_text_display((double)FLOAT_8061aa28, 0);
        hud_set_last_child_tag(0xf5);
        hud_item_set_value_display(rec + 0x24);
        hud_end_scroll_region();
    }

    // Mode 10: two two-row sub-lists.
    if (*(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4) == 10) {
        get_string_by_id(0xb69);
        hud_begin_scroll_with_item();
        hud_begin_sub_list(0, 0, 2);
        double rh = match_selection_row_height();
        double ch = hud_get_content_height();
        hud_add_item_internal((double)(float)(ch - (double)FLOAT_8061aac8), rh);
        rh = match_selection_row_height();
        hud_add_item_internal((double)FLOAT_8061aa28, rh);
        hud_set_last_child_tag(0x100);
        hud_end_sub_list();
        hud_end_scroll_region();

        get_string_by_id(0xb6b);
        hud_begin_scroll_with_item();
        hud_begin_sub_list(0, 0, 2);
        rh = match_selection_row_height();
        ch = hud_get_content_height();
        hud_add_item_internal((double)(float)(ch - (double)FLOAT_8061aac8), rh);
        rh = match_selection_row_height();
        hud_add_item_internal((double)FLOAT_8061aa28, rh);
        hud_set_last_child_tag(0x101);
        hud_end_sub_list();
        hud_end_scroll_region();
    }

    // Optional cell tooltip row when a selectable child cell is set.
    double tipA = base;
    double tipB = k1c;
    double tipC = k5c;
    double tipD = c64;
    double tipE = c6c;
    if ((-1 < *(short *)(slotPtr + 0xc)) &&
        (aveditor_cell_type_is_selectable(DAT_80612be4 + *(short *)(slotPtr + 0xc) * 0x8c + 0xdd14) != 0)) {
        int tip = aveditor_build_cell_tooltip(0, base, tipA, tipB, tipC, tipD, tipE, 0);
        if (tip == 0) {
            get_string_by_id(0xb29);
        }
        get_string_by_id(0x98c);
        hud_begin_scroll_with_item();
        hud_add_list_item();
        hud_end_scroll_region();
    }

    // Modes 7/4/5: handicap toggle header.
    int m = *(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4);
    if ((m == 7) || (m == 4) || (m == 5)) {
        get_string_by_id(0xaaa);
        hud_begin_scroll_with_item();
        hud_item_add_header((double)FLOAT_8061aa28, 0x800);
        hud_set_last_child_tag(0xf6);
        get_string_by_id(0xbab);
        hud_event_add_pair();
        get_string_by_id(0xbac);
        hud_event_add_pair();
        hud_item_set_linked_value(
            (int *)(DAT_80612be4 + *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd9c));
        hud_end_scroll_region();
    }

    // Mode 3: a two-row sub-list.
    if (*(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4) == 3) {
        get_string_by_id(0xb7b);
        hud_begin_scroll_with_item();
        hud_begin_sub_list(0, 0, 2);
        double rh = match_selection_row_height();
        double ch = hud_get_content_height();
        hud_add_item_internal((double)(float)(ch - (double)FLOAT_8061aac8), rh);
        rh = match_selection_row_height();
        hud_add_item_internal((double)FLOAT_8061aa28, rh);
        hud_set_last_child_tag(0x102);
        hud_end_sub_list();
        hud_end_scroll_region();
    }

    // Mode 0xe: three axis sliders.
    if (*(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4) == 0xe) {
        get_string_by_id(0xb7d);
        hud_begin_scroll_with_item();
        unsigned char *t0 = FUN_80336c44(limitRec, 0);
        hud_item_add_slider((double)FLOAT_8061aa28, (double)FLOAT_8061aac4, 0,
                            (int)t0, 0x3f, limitRec + 0x20, 0);
        hud_set_last_child_tag(0xf7);
        hud_end_scroll_region();

        get_string_by_id(0xb7e);
        hud_begin_scroll_with_item();
        unsigned char *t1 = FUN_80336c44(limitRec, 1);
        hud_item_add_slider((double)FLOAT_8061aa28, (double)FLOAT_8061aac4, 0,
                            (int)t1, 0x3f, limitRec + 0x24, 0);
        hud_set_last_child_tag(0xf8);
        hud_end_scroll_region();

        get_string_by_id(0xb7f);
        hud_begin_scroll_with_item();
        unsigned char *t2 = FUN_80336c44(limitRec, 2);
        hud_item_add_slider((double)FLOAT_8061aa28, (double)FLOAT_8061aac4, 0,
                            (int)t2, 0x3f, limitRec + 0x28, 0);
        hud_set_last_child_tag(0xf9);
        hud_end_scroll_region();
    }

    if (0 < popcount32(caps)) {
        match_editor_build_options();
    }

    // Primary value selector (score/frag limit) or a "locked" placeholder sub-list.
    if (((*(unsigned int *)(&DAT_80480f30 + cfgOff) & 1) == 0) &&
        (match_cat_is_locked_pair(**(int **)(DAT_80612be4 + 0x11858),
                                  (*(int **)(DAT_80612be4 + 0x11858))[1]) == 0)) {
        int *slot = *(int **)(DAT_80612be4 + 0x11858);
        if (match_mode_type_is_eligible(slot[0], slot[1], slot[2]) != 0) {
            slot = *(int **)(DAT_80612be4 + 0x11858);
            if (*slot == 1) {
                if (slot[1] == 4) {
                    get_string_by_id(0x984);
                    hud_begin_scroll_with_item();
                    int label = get_string_by_id(*(int *)(&DAT_80480f1c + cfgOff));
                    hud_item_add_value_selector(0, (int *)(*(int *)(DAT_80612be4 + 0x11858) + 0x10),
                                                1, 600, 1, label, 0x60);
                    hud_set_last_child_tag(0xfb);
                    hud_end_scroll_region();
                } else if (slot[1] == 0xd) {
                    int rec = DAT_80612be4 + *(short *)(slot + 3) * 0x8c + 0xdd14;
                    unsigned int placed = aveditor_count_placed_for_type(*(int *)(rec + 0x84));
                    // Original guarded these writes on a redundant compare; both branches clamp
                    // the stored count to `placed` when the current value exceeds it.
                    if ((int)placed < *(int *)(rec + 0x20)) {
                        *(unsigned int *)(rec + 0x20) = placed;
                    }
                    get_string_by_id(0x984);
                    hud_begin_scroll_with_item();
                    int label = get_string_by_id(0);
                    hud_item_add_value_selector(0, (int *)(rec + 0x20), 0, (int)placed, 1, label, 0);
                    hud_set_last_child_tag(0xfc);
                    hud_end_scroll_region();
                } else {
                    get_string_by_id(0x984);
                    hud_begin_scroll_with_item();
                    int label = get_string_by_id(*(int *)(&DAT_80480f1c + cfgOff));
                    hud_item_add_value_selector(0, (int *)(*(int *)(DAT_80612be4 + 0x11858) + 0x10),
                                                0, 1000000, 1, label, 0);
                    hud_set_last_child_tag(0xfd);
                    hud_end_scroll_region();
                }
            } else {
                get_string_by_id(0xb64);
                hud_begin_scroll_with_item();
                int label = get_string_by_id(*(int *)(&DAT_80480f1c + cfgOff));
                hud_item_add_value_selector(0, (int *)(*(int *)(DAT_80612be4 + 0x11858) + 0x10),
                                            1, 10000, 1, label, 0);
                hud_set_last_child_tag(0xfe);
                hud_end_scroll_region();
            }
        }
    } else {
        get_string_by_id(0xb28);
        hud_begin_scroll_region();
        hud_begin_sub_list(0, 0, 2);
        double rh = match_selection_row_height();
        double ch = hud_get_content_height();
        hud_add_item_internal((double)(float)(ch - (double)FLOAT_8061aac8), rh);
        int locked = match_cat_is_locked_pair(**(int **)(DAT_80612be4 + 0x11858),
                                              (*(int **)(DAT_80612be4 + 0x11858))[1]);
        if (locked == 0) {
            if (*(int *)(*(int *)(DAT_80612be4 + 0x11858) + 4) == 0xb) {
                rh = match_selection_row_height();
                hud_add_item_internal((double)FLOAT_8061aa28, rh);
                hud_set_last_child_tag(0x104);
            } else {
                rh = match_selection_row_height();
                hud_add_item_internal((double)FLOAT_8061aa28, rh);
                hud_set_last_child_tag(0x105);
            }
        } else {
            rh = match_selection_row_height();
            hud_add_item_internal((double)FLOAT_8061aa28, rh);
            hud_set_last_child_tag(0x103);
        }
        hud_end_sub_list();
        hud_end_scroll_region();
    }

    hud_add_callback_item((double)FLOAT_8061aa14);
    int exitId = (DAT_80612cfc != 0) ? 0xb02 : 0xb03;
    get_string_by_id(exitId);
    hud_add_list_item();
    hud_element_apply_scroll();
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061aa1c * FLOAT_80611a64 +
                   FLOAT_8061aa20 * FLOAT_80611a64 +
                   FLOAT_8061aa24 * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    hud_element_finalize_frame();
    *(unsigned int *)(screen + 0x214) = *(unsigned int *)(screen + 0x214) | 4;

    // If a grid cell is selected, animate the view toward its entry position.
    int cell = (int)*(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc);
    if ((-1 < cell) && (aveditor_is_cell_selected(cell) != 0)) {
        unsigned long long pos = aveditor_get_entry_position(
            (int)*(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc));
        float depth = (float)(FLOAT_8061aa40 * (FLOAT_8061aa68 + *(float *)(screen + 0x4c)));
        *(unsigned int *)(screen + 0x214) = *(unsigned int *)(screen + 0x214) | 9;
        avanim_snapshot_current();

        short px = (short)((unsigned long long)pos >> 0x30);
        short py = (short)((unsigned long long)pos >> 0x20);
        short flags = (short)((unsigned long long)pos >> 0x10);
        avanim_begin_to(
            (double)(FLOAT_8061aa40 +
                     (float)((double)CONCAT44(0x43300000, (int)px ^ 0x80000000) - DOUBLE_8061aa48)),
            (double)(FLOAT_8061aa40 +
                     (float)((double)CONCAT44(0x43300000, (int)py ^ 0x80000000) - DOUBLE_8061aa48)),
            (double)(float)((double)depth * (double)FLOAT_80611a68 + (double)FLOAT_80611a70),
            (double)(FLOAT_8061aacc * FLOAT_80611a64 + FLOAT_80611a6c),
            (int)flags);
    }

    FUN_8038271c();
}

// Enter the options editor from a grid slot: resolve the slot's active mode record, run the shared
// cleanup, clear the "editing existing" flag and point the active-slot pointer at it, then push.
void match_editor_enter_from_grid(void)
{
    int rec = DAT_80612be4 +
              *(int *)(DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220 + 0x4270) * 0xb4 + 0x81a8;
    match_editor_commit_cleanup();
    DAT_80612cfc = 0;
    *(int *)(DAT_80612be4 + 0x11858) = rec;
    hud_screen_push();
}

// Enter the options editor (no slot change): cleanup, clear editing flag, push.
void match_editor_enter(void)
{
    match_editor_commit_cleanup();
    DAT_80612cfc = 0;
    hud_screen_push();
}

// Enter the options editor (alternate entry, no slot change): cleanup, clear editing flag, push.
void match_editor_enter_alt(void)
{
    match_editor_commit_cleanup();
    DAT_80612cfc = 0;
    hud_screen_push();
}

// Commit pending edits / run mode-specific cleanup: for a capability-flagged active slot that still
// needs its cleanup prompt, queue the prompt (once, gated by ctx+0x238 bit5), then flush the commit.
void match_editor_commit_cleanup(void)
{
    if ((DAT_80612d00 != 0) &&
        (((&DAT_804816ea)[(unsigned int)*(unsigned char *)(
             DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220 + 0x41e8) * 0x44] & 1) != 0)) {
        int needsPrompt = FUN_803392d4(DAT_80612d00);
        if ((needsPrompt != 0) && ((*(unsigned int *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
            FUN_80348238(0x17);
            *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x20;
        }
    }
    FUN_80339370();
}

// Read a linked value at record+0x24; when the record is flagged dirty (record+0x42 bit0) and a
// match slot is active, notify the slot of the change first.
unsigned int match_editor_get_value_notify(int record)
{
    if ((DAT_80612d00 != 0) && ((*(unsigned char *)(record + 0x42) & 1) != 0)) {
        FUN_80339250(DAT_80612d00);
    }
    return *(unsigned int *)(record + 0x24);
}

// Commit + re-push the screen.
void match_editor_refresh(void)
{
    match_editor_commit_cleanup();
    hud_screen_push();
}

// Commit, then either push a fresh screen or pop back: when we are editing an existing entry
// (DAT_80612cfc set) and the active grid slot has no capability byte, pop the current screen;
// otherwise push a new one.
void match_editor_enter_or_pop(void)
{
    int slotBase = DAT_80612be4 + *(int *)(DAT_80612d00 + 0xa4) * 0x220;
    match_editor_commit_cleanup();
    if ((DAT_80612cfc == 0) || (*(char *)(slotBase + 0x41e8) != '\0')) {
        hud_screen_push();
    } else {
        hud_screen_pop_current();
    }
}
