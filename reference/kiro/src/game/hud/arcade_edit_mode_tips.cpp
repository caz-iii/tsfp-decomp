// Batch: batch_978 | Module: arcade/challenge editor mode-config tooltip builders (cont.)
// Address range: 0x80347ae0 - 0x80347df4
// Date: 2026-09-12
//
// The continuation of batch_977's toolbar tooltip builders, covering the match-mode / team-config
// buttons plus a texture-slot label. Most are thin builders that pair a caption string id with a
// value string and render a two-part label via FUN_80387fb8 (the editor's printf-style renderer,
// param_9 = dst / param_10 = format / param_11 = value). The centrepiece is tip_mode_desc_field, a
// shared mode-descriptor formatter: for a non-teamplay mode it just renders the field label; for a
// teamplay class it first composes a "<count> <teamname> " prefix into a stack buffer (via the
// strcpy/strlen helpers FUN_8038b510 / FUN_8038b5c8) and renders that ahead of the field. The three
// tip_mode_* wrappers call it with a different caption id and pull the count/team pair from the
// editor context (DAT_80612be4 + 0x2f10 / +0x2f20).
//
//   tip_plural_cf3       0x80347ae0  parity-picked value label + caption 0xcf3.
//   tip_plural_cf4       0x80347b38  parity-picked value label + caption 0xcf4.
//   tip_fixed_971_cf2    0x80347b90  fixed value 0x971 + caption 0xcf2.
//   tip_fixed_971_cf3    0x80347be8  fixed value 0x971 + caption 0xcf3.
//   tip_mode_desc_field  0x80347c40  mode-descriptor field formatter (teamplay-aware, two-part).
//   tip_mode_pair_2f10   0x80347d28  wrapper: count/team from +0x2f10/+0x2f12, caption 0xcef.
//   tip_mode_pair_2f20_ceb 0x80347d58 wrapper: count/team packed in +0x2f20, caption 0xceb.
//   tip_mode_pair_2f20_ced 0x80347d8c wrapper: count/team packed in +0x2f20, caption 0xced.
//   tip_mode_pair_2f20_cf4 0x80347dc0 wrapper: count/team packed in +0x2f20, caption 0xcf4.
//   tip_texslot_cf1      0x80347df4  current texture-slot label + caption 0xcf1.
//
// NON-MATCHING: reproduces the decompiler's structure. The 16-parameter (r3..r10 + 8 stack args)
// tails are kept so the ABI-level calls into FUN_80387fb8 / tip_mode_desc_field line up; only a few
// params carry meaning. tip_mode_desc_field recovers its mode index / high-word from the
// __save_gpr_28 register-spill (declared file-local returning u64, per the tree convention) and its
// float-register carrier (extraout_f1) exactly as the decompiler modelled them. Raw FUN_/DAT_
// symbols (FUN_80387fb8, FUN_8038b510, FUN_8038b5c8, __save_gpr_28/__restore_gpr_28) are declared
// file-locally with call-site-accurate arity (Compile-Clean rule 5); named helpers
// (get_string_by_id, match_mode_is_teamplay_class, aveditor_texslot_ptr, memset_wrapper,
// tip_pick_plural_caption) resolve through the shared headers (rule 6).

#include "common.h"

extern "C" {

// ---- editor context --------------------------------------------------------
extern int DAT_80612be4;      // editor context base

// ---- named cross-module helpers --------------------------------------------
extern int  get_string_by_id(int id);
extern int  match_mode_is_teamplay_class(int pModeDesc);
extern int  aveditor_texslot_ptr(unsigned int index);
extern int  tip_pick_plural_caption(void);            // batch_977
extern void memset_wrapper(int dst, int val, int len);

// ---- raw callees (declared file-local with this call site's arity) ---------
extern void FUN_80387fb8(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int dst, char* fmt,
                         int value, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                         unsigned p16);
#define tooltip_format_label FUN_80387fb8
extern void FUN_8038b510(int* dst, int* src);          // copy a name string field
extern int  FUN_8038b5c8(int str);                     // string length / is-empty helper
extern void leaf_31e8(unsigned int a, unsigned int b, int c);  // batch_980: leaf text renderer
extern unsigned long long __save_gpr_28(void);         // _savegpr_ spill carrier (r28 pair)
extern void __restore_gpr_28(void);

// forward decl within this TU
void tip_mode_desc_field(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                         unsigned long long p4, unsigned long long p5, unsigned long long p6,
                         unsigned long long p7, unsigned long long p8, unsigned p9, unsigned p10,
                         int count, int caption_id, unsigned p13, unsigned p14, unsigned p15,
                         unsigned p16);

// ---------------------------------------------------------------------------
// 0x80347ae0 : parity-picked value label + caption 0xcf3
// ---------------------------------------------------------------------------
void tip_plural_cf3(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                    unsigned long long p4, unsigned long long p5, unsigned long long p6,
                    unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                    unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                    unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = get_string_by_id(tip_pick_plural_caption());
    caption = (char*)get_string_by_id(0xcf3);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347b38 : parity-picked value label + caption 0xcf4
// ---------------------------------------------------------------------------
void tip_plural_cf4(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                    unsigned long long p4, unsigned long long p5, unsigned long long p6,
                    unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                    unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                    unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = get_string_by_id(tip_pick_plural_caption());
    caption = (char*)get_string_by_id(0xcf4);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347b90 : fixed value 0x971 + caption 0xcf2
// ---------------------------------------------------------------------------
void tip_fixed_971_cf2(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                       unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                       unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = get_string_by_id(0x971);
    caption = (char*)get_string_by_id(0xcf2);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347be8 : fixed value 0x971 + caption 0xcf3
// ---------------------------------------------------------------------------
void tip_fixed_971_cf3(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                       unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                       unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = get_string_by_id(0x971);
    caption = (char*)get_string_by_id(0xcf3);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347c40 : mode-descriptor field formatter (teamplay-aware, two-part)
//   Resolves the mode descriptor at ctx + mode_index*0xe4 + 0x288. For a non-teamplay mode it renders
//   the field label (caption_id) with the descriptor as the value. For a teamplay class it composes a
//   "<count> <teamname> " prefix into a 0x2a-byte stack buffer, renders that (caption 0xbbf, value =
//   count+1) then renders the field label with the composed buffer as the value.
// ---------------------------------------------------------------------------
void tip_mode_desc_field(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                         unsigned long long p4, unsigned long long p5, unsigned long long p6,
                         unsigned long long p7, unsigned long long p8, unsigned p9, unsigned p10,
                         int count, int caption_id, unsigned p13, unsigned p14, unsigned p15,
                         unsigned p16)
{
    int   mode_hi;
    int   is_team;
    char* caption;
    int   len;
    int*  mode_desc;
    unsigned long long spill;
    unsigned char buf[0x2a];
    (void)p9; (void)p10;

    spill     = __save_gpr_28();
    mode_hi   = (int)((unsigned long long)spill >> 0x20);
    mode_desc = (int*)(DAT_80612be4 + (int)spill * 0xe4 + 0x288);
    is_team   = match_mode_is_teamplay_class((int)mode_desc);
    if (is_team == 0) {
        caption = (char*)get_string_by_id(caption_id);
        tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, mode_hi, caption, (int)mode_desc,
                             (unsigned)caption_id, p13, p14, p15, p16);
    }
    else {
        memset_wrapper((int)&buf, 0, 0x2a);
        FUN_8038b510((int*)&buf, mode_desc);
        len = FUN_8038b5c8((int)&buf);
        buf[len] = 0x20;                       // separating space
        caption = (char*)get_string_by_id(0xbbf);
        tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, (int)&buf + len + 1, caption,
                             count + 1, (unsigned)caption_id, p13, p14, p15, p16);
        caption = (char*)get_string_by_id(caption_id);
        tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, mode_hi, caption, (int)&buf,
                             (unsigned)caption_id, p13, p14, p15, p16);
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80347d28 : wrapper - count/team from +0x2f10 / +0x2f12, caption 0xcef
// ---------------------------------------------------------------------------
void tip_mode_pair_2f10(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                        unsigned long long p4, unsigned long long p5, unsigned long long p6,
                        unsigned long long p7, unsigned long long p8, unsigned p9)
{
    tip_mode_desc_field(p1, p2, p3, p4, p5, p6, p7, p8, p9,
                        (int)*(short*)(DAT_80612be4 + 0x2f10),
                        (int)*(short*)(DAT_80612be4 + 0x2f12), 0xcef, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// 0x80347d58 : wrapper - count/team packed in +0x2f20 (hi/lo), caption 0xceb
// ---------------------------------------------------------------------------
void tip_mode_pair_2f20_ceb(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                            unsigned long long p4, unsigned long long p5, unsigned long long p6,
                            unsigned long long p7, unsigned long long p8, unsigned p9)
{
    unsigned packed = *(unsigned*)(DAT_80612be4 + 0x2f20);
    tip_mode_desc_field(p1, p2, p3, p4, p5, p6, p7, p8, p9,
                        (int)(packed >> 0x10), (int)(packed & 0xffff), 0xceb, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// 0x80347d8c : wrapper - count/team packed in +0x2f20, caption 0xced
// ---------------------------------------------------------------------------
void tip_mode_pair_2f20_ced(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                            unsigned long long p4, unsigned long long p5, unsigned long long p6,
                            unsigned long long p7, unsigned long long p8, unsigned p9)
{
    unsigned packed = *(unsigned*)(DAT_80612be4 + 0x2f20);
    tip_mode_desc_field(p1, p2, p3, p4, p5, p6, p7, p8, p9,
                        (int)(packed >> 0x10), (int)(packed & 0xffff), 0xced, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// 0x80347dc0 : wrapper - count/team packed in +0x2f20, caption 0xcf4
// ---------------------------------------------------------------------------
void tip_mode_pair_2f20_cf4(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                            unsigned long long p4, unsigned long long p5, unsigned long long p6,
                            unsigned long long p7, unsigned long long p8, unsigned p9)
{
    unsigned packed = *(unsigned*)(DAT_80612be4 + 0x2f20);
    tip_mode_desc_field(p1, p2, p3, p4, p5, p6, p7, p8, p9,
                        (int)(packed >> 0x10), (int)(packed & 0xffff), 0xcf4, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// 0x80347df4 : current texture-slot label + caption 0xcf1
// ---------------------------------------------------------------------------
void tip_texslot_cf1(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                     unsigned long long p4, unsigned long long p5, unsigned long long p6,
                     unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                     unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                     unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = aveditor_texslot_ptr((*(int*)(DAT_80612be4 + 0x280) - 1U) & 0xff);
    caption = (char*)get_string_by_id(0xcf1);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ===========================================================================
// Batch: batch_980 | continuation: two more fixed-caption tip builders + a
// standalone plural-suffix tip. Same two-part label pattern as above.
//   Address: 0x80348138 | Name: tip_fixed_a7a_cf2
//   Address: 0x80348190 | Name: tip_fixed_a80_cf2
//   Address: 0x803481e8 | Name: tip_plural_suffix_cf7
//   Date: 2026-09-12
// ===========================================================================

// tip_plural_suffix_cf7 renders a single plural-suffix label: it fetches string
// 0xcf7, measures it (FUN_8038b5c8, already declared file-local above), and
// emits it via the leaf renderer leaf_31e8 (resolved through shared headers).

// ---------------------------------------------------------------------------
// 0x80348138 : fixed value 0xa7a + caption 0xcf2
// ---------------------------------------------------------------------------
void tip_fixed_a7a_cf2(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                       unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                       unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = get_string_by_id(0xa7a);
    caption = (char*)get_string_by_id(0xcf2);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80348190 : fixed value 0xa80 + caption 0xcf2
// ---------------------------------------------------------------------------
void tip_fixed_a80_cf2(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                       unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                       unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = get_string_by_id(0xa80);
    caption = (char*)get_string_by_id(0xcf2);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x803481e8 : standalone plural-suffix label (string 0xcf7) via leaf renderer
// ---------------------------------------------------------------------------
void tip_plural_suffix_cf7(unsigned int p1)
{
    unsigned int str;
    int          len;

    str = (unsigned int)get_string_by_id(0xcf7);
    len = FUN_8038b5c8((int)str);
    leaf_31e8(p1, str, len);
}

} // extern "C"
