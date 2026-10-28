// Batch: batch_977 | Module: arcade/challenge editor toolbar/softkey tooltip label builders
// Address range: 0x8034768c - 0x80347a88
// Date: 2026-09-12
//
// A family of per-toolbar-button tooltip builders for the arcade/challenge editor. Each one runs in
// the shared toolbar-render context (the r3..r10 register tail plus the 8 stack args carried through
// to the renderer) and formats a two-part "<caption>: <value>" tooltip by pairing a caption string
// id with a value string, then handing both to FUN_80387fb8 (the editor's printf-style label
// renderer, param_9 = dst buffer, param_10 = format string, param_11 = value string). The value is
// usually the current selection label (aveditor_get_selection_label, batch_976) or a grabbed
// palette-entry label; a few buttons switch their caption/value on whether any grid members are
// currently selected (aveditor_count_selected_members).
//
//   tip_selection_ceb        0x8034768c  selection label + caption 0xceb.
//   tip_selection_ced        0x803476e0  selection label + caption 0xced.
//   tip_palette_entry_cef    0x80347734  grabbed palette-entry label (+0x2f34) + caption 0xcef.
//   tip_selection_cf4        0x80347790  selection label + caption 0xcf4.
//   tip_member_or_multi_ceb  0x803477e4  1 member: its roster label+0xceb; else "multi"+0xcec.
//   tip_member_or_multi_ced  0x80347890  active card unlocked: roster label+0xced; else "multi"+0xcee.
//   tip_fixed_a1f_cec        0x8034793c  fixed "locked" label 0xa1f + caption 0xcec.
//   tip_member_or_multi_cef  0x80347994  active card unlocked: roster label+0xcef; else "multi"+0xcf0.
//   tip_pick_plural_caption  0x80347a40  choose caption 0xcf5/0xcf6 by selected-member parity.
//   tip_plural_cf2           0x80347a88  parity-picked value label + caption 0xcf2.
//
// NON-MATCHING: reproduces the decompiler's structure. The 16-parameter tails (r3..r10 registers +
// 8 stack args) are kept verbatim so the ABI-level call into FUN_80387fb8 lines up; only param_9..11
// carry meaning (dst / format / value). FUN_80387fb8 is declared file-locally with the 16-arg
// prototype matching this call site (Compile-Clean rule 5; the same address is declared with a
// shorter sprintf-style prototype in arcade_edit_build.cpp). aveditor_build_palette_entry_label is
// void in the shared header but its return is used here, so it is invoked through a function-pointer
// cast to capture the value without a conflicting redeclaration. tip_pick_plural_caption reproduces
// the decompiler's sign-test parity expression on the selected-member count verbatim.

#include "common.h"

extern "C" {

// ---- editor context --------------------------------------------------------
extern int DAT_80612be4;      // editor context base
extern int DAT_8047c150;      // roster/member descriptor table base (0x140-byte stride)

// ---- named cross-module helpers --------------------------------------------
extern int  get_string_by_id(int id);
extern int  aveditor_get_selection_label(void);
extern int  aveditor_count_selected_members(void);
extern void aveditor_build_palette_entry_label(int* entry);   // header prototype (void); see cast below

// FUN_80387fb8: the editor's printf-style two-part label renderer. Declared with the full 16-arg
// call-site prototype (r3..r10 + 8 stack args); only param_9/param_10/param_11 carry meaning. Kept
// under the raw FUN_ name (declared with a shorter sprintf-style prototype in arcade_edit_build.cpp;
// same address, different signatures across files per Compile-Clean rule 5). A local macro maps the
// descriptive call name to it.
extern void FUN_80387fb8(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int dst, char* fmt,
                         int value, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                         unsigned p16);
#define tooltip_format_label FUN_80387fb8

// forward decl within this TU
int tip_pick_plural_caption(void);

// Capture the palette-entry label's return value (header decl is void) without redeclaring it.
static inline int palette_entry_label_r(int* entry)
{
    return ((int (*)(int*))aveditor_build_palette_entry_label)(entry);
}

// ---------------------------------------------------------------------------
// 0x8034768c : selection label + caption 0xceb
// ---------------------------------------------------------------------------
void tip_selection_ceb(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                       unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                       unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = aveditor_get_selection_label();
    caption = (char*)get_string_by_id(0xceb);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x803476e0 : selection label + caption 0xced
// ---------------------------------------------------------------------------
void tip_selection_ced(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                       unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                       unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = aveditor_get_selection_label();
    caption = (char*)get_string_by_id(0xced);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347734 : grabbed palette-entry label (ctx+0x2f34) + caption 0xcef
//   The label value is produced in a float/return register (extraout_f1 in the decompiler); the
//   renderer's leading register arg is overwritten with it, reproduced here as p1 = value carrier.
// ---------------------------------------------------------------------------
void tip_palette_entry_cef(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                           unsigned long long p4, unsigned long long p5, unsigned long long p6,
                           unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                           unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                           unsigned p16)
{
    int   value;
    char* caption;
    (void)p1; (void)p10; (void)p11;

    value   = palette_entry_label_r((int*)(DAT_80612be4 + 0x2f34));
    caption = (char*)get_string_by_id(0xcef);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347790 : selection label + caption 0xcf4
// ---------------------------------------------------------------------------
void tip_selection_cf4(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                       unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                       unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = aveditor_get_selection_label();
    caption = (char*)get_string_by_id(0xcf4);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x803477e4 : one member selected -> its roster label + 0xceb; else "multi" (0xa1d) + 0xcec
// ---------------------------------------------------------------------------
void tip_member_or_multi_ceb(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                             unsigned long long p4, unsigned long long p5, unsigned long long p6,
                             unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                             unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                             unsigned p16)
{
    int   value;
    char* caption;
    int   member;
    (void)p10; (void)p11;

    member = *(int*)(DAT_80612be4 + 0x2f18);
    if (aveditor_count_selected_members() == 0) {
        value   = get_string_by_id(*(int*)(&DAT_8047c150 + *(int*)(member + 0x10) * 0x140));
        caption = (char*)get_string_by_id(0xceb);
    }
    else {
        value   = get_string_by_id(0xa1d);
        caption = (char*)get_string_by_id(0xcec);
    }
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347890 : active card unlocked (+0x40 == 0) -> roster label + 0xced; else "multi" + 0xcee
// ---------------------------------------------------------------------------
void tip_member_or_multi_ced(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                             unsigned long long p4, unsigned long long p5, unsigned long long p6,
                             unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                             unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                             unsigned p16)
{
    int   value;
    char* caption;
    int   card;
    (void)p10; (void)p11;

    card = *(int*)(DAT_80612be4 + 0x2f7c);
    if (*(int*)(card + 0x40) == 0) {
        value   = get_string_by_id(*(int*)(&DAT_8047c150 + *(int*)(card + 0x10) * 0x140));
        caption = (char*)get_string_by_id(0xced);
    }
    else {
        value   = get_string_by_id(0xa1d);
        caption = (char*)get_string_by_id(0xcee);
    }
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x8034793c : fixed "locked" value (0xa1f) + caption 0xcec
// ---------------------------------------------------------------------------
void tip_fixed_a1f_cec(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                       unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                       unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = get_string_by_id(0xa1f);
    caption = (char*)get_string_by_id(0xcec);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347994 : active card unlocked (+0x40 == 0) -> roster label + 0xcef; else "multi" + 0xcf0
// ---------------------------------------------------------------------------
void tip_member_or_multi_cef(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                             unsigned long long p4, unsigned long long p5, unsigned long long p6,
                             unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                             unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                             unsigned p16)
{
    int   value;
    char* caption;
    int   card;
    (void)p10; (void)p11;

    card = *(int*)(DAT_80612be4 + 0x2f7c);
    if (*(int*)(card + 0x40) == 0) {
        value   = get_string_by_id(*(int*)(&DAT_8047c150 + *(int*)(card + 0x10) * 0x140));
        caption = (char*)get_string_by_id(0xcef);
    }
    else {
        value   = get_string_by_id(0xa1d);
        caption = (char*)get_string_by_id(0xcf0);
    }
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347a40 : choose caption 0xcf5 / 0xcf6 by selected-member count parity
//   Reproduces the decompiler's sign-tested parity expression on the count verbatim.
// ---------------------------------------------------------------------------
int tip_pick_plural_caption(void)
{
    unsigned int count;
    int          caption;

    count   = (unsigned int)aveditor_count_selected_members();
    caption = 0xcf6;
    if ((int)(((int)((count ^ 1)) >> 1) - ((count ^ 1) & count)) < 0) {
        caption = 0xcf5;
    }
    return caption;
}

// ---------------------------------------------------------------------------
// 0x80347a88 : parity-picked value label + caption 0xcf2
// ---------------------------------------------------------------------------
void tip_plural_cf2(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                    unsigned long long p4, unsigned long long p5, unsigned long long p6,
                    unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                    unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                    unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = get_string_by_id(tip_pick_plural_caption());
    caption = (char*)get_string_by_id(0xcf2);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

} // extern "C"
