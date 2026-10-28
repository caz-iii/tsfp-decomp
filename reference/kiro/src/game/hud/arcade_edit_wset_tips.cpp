// Batch: batch_979 | Module: arcade/challenge editor weapon-set / match-config tooltip builders (cont.)
// Address range: 0x80347e58 - 0x80348110
// Date: 2026-09-12
//
// The continuation of batch_977/978's toolbar tooltip family, covering the weapon-set backup, texture
// slot, match-clone and match-config buttons. Each builder resolves a value (an accessor return or a
// pointer into an editor/match table) plus a caption string id and renders a two-part label via
// FUN_80387fb8 (param_9 = dst / param_10 = format / param_11 = value). tip_wset_slot_field is a shared
// match-block-field formatter used by two thin wrappers (grabbed slot 1 / slot 2); it pushes a scope
// (FUN_803826f0), locates the match block for a flag mask (match_find_block_by_flag), reads a field-4
// descriptor string and renders it, then pops the scope (FUN_8038273c).
//
//   tip_delete_backup_ceb   0x80347e58  delete-backup slot label + caption 0xceb.
//   tip_wset_texslot_cf4    0x80347eac  weapon-set flag -> texture-slot label + caption 0xcf4.
//   tip_wset_row_cf1        0x80347f04  weapon-set row record ptr (ctx+0x81c8 stride 0xb4) + caption 0xcf1.
//   tip_clone_buffer_ceb    0x80347f6c  match-clone buffer slot label + caption 0xceb.
//   tip_flags_plus20_cf4    0x80347fc0  shared: (flags + 0x20) value + caption 0xcf4.
//   tip_editor_flags_cf4    0x80348010  wrapper: editor flags at ctx+0x11858 -> tip_flags_plus20_cf4.
//   tip_session_flags_cf4   0x80348040  wrapper: match_session_get_flags -> tip_flags_plus20_cf4.
//   tip_wset_slot_field     0x80348074  shared match-block field-4 label formatter (scope push/pop).
//   tip_wset_slot1_cf1      0x803480e8  wrapper: flag mask 1, caption 0xcf1.
//   tip_wset_slot2_ceb      0x80348110  wrapper: flag mask 2, caption 0xceb.
//
// NON-MATCHING: reproduces the decompiler's structure. The 16-parameter (r3..r10 + 8 stack args)
// tails are kept so the ABI-level calls line up; only a few params carry meaning. Where the value is
// produced in a float/return register that overwrites r3 (extraout_f1 in the decompiler) the leading
// carrier param is reused as-is. Raw FUN_/DAT_ symbols (FUN_80387fb8, FUN_803826f0/FUN_8038273c) are
// declared file-locally with call-site-accurate arity (Compile-Clean rule 5); named helpers resolve
// through the shared headers (rule 6). tip_wset_slot_field reproduces the decompiler's uninitialised
// register read (extraout_r4 flag mask, local block row/col scratch) faithfully.

#include "common.h"

extern "C" {

// ---- editor context --------------------------------------------------------
extern int DAT_80612be4;      // editor context base

// ---- named cross-module helpers --------------------------------------------
extern int           get_string_by_id(int id);
extern int           aveditor_free_delete_backup(void);
extern unsigned char aveditor_get_wset_flag(void);
extern int           aveditor_texslot_ptr(unsigned int index);
extern int           match_clone_buffer_free(void);
extern int           match_session_get_flags(void);
extern int           match_find_block_by_flag(unsigned char nMask, int* pRow, int* pCol);
extern int           match_desc_field4(int nKey);

// ---- raw callees (declared file-local with this call site's arity) ---------
extern void FUN_80387fb8(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int dst, char* fmt,
                         int value, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                         unsigned p16);
#define tooltip_format_label FUN_80387fb8
extern unsigned long long FUN_803826f0(void);   // scope push (cell/mode record in high word)
extern void               FUN_8038273c(void);   // scope pop

// forward decl within this TU
void tip_flags_plus20_cf4(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                          unsigned long long p4, unsigned long long p5, unsigned long long p6,
                          unsigned long long p7, unsigned long long p8, int flags, int dst,
                          unsigned ctx, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                          unsigned p16);
void tip_wset_slot_field(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                         unsigned long long p4, unsigned long long p5, unsigned long long p6,
                         unsigned long long p7, unsigned long long p8, unsigned p9, unsigned p10,
                         int caption_id, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                         unsigned p16);

// ---------------------------------------------------------------------------
// 0x80347e58 : delete-backup slot label + caption 0xceb
// ---------------------------------------------------------------------------
void tip_delete_backup_ceb(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                           unsigned long long p4, unsigned long long p5, unsigned long long p6,
                           unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                           unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                           unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = aveditor_free_delete_backup();
    caption = (char*)get_string_by_id(0xceb);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347eac : weapon-set flag -> texture-slot label + caption 0xcf4
// ---------------------------------------------------------------------------
void tip_wset_texslot_cf4(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                          unsigned long long p4, unsigned long long p5, unsigned long long p6,
                          unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                          unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                          unsigned p16)
{
    unsigned char flag;
    int   value;
    char* caption;
    (void)p10; (void)p11;

    flag    = aveditor_get_wset_flag();
    value   = aveditor_texslot_ptr((unsigned)flag);
    caption = (char*)get_string_by_id(0xcf4);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347f04 : weapon-set row record ptr (ctx+0x81c8, stride 0xb4) + caption 0xcf1
//   Pushes a scope (FUN_803826f0, dst index in high word), points at the (count-1)th row record and
//   renders it, then pops the scope.
// ---------------------------------------------------------------------------
void tip_wset_row_cf1(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                      unsigned long long p4, unsigned long long p5, unsigned long long p6,
                      unsigned long long p7, unsigned long long p8)
{
    int   dst;
    int   ctx;
    int   count;
    char* caption;
    unsigned long long scope;
    (void)p1;

    scope   = FUN_803826f0();
    dst     = (int)scope;
    ctx     = DAT_80612be4;
    count   = *(int*)(DAT_80612be4 + 0x81a4);
    caption = (char*)get_string_by_id(0xcf1);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, dst, caption,
                         ctx + (count - 1) * 0xb4 + 0x81c8, 0, 0, 0, 0, 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80347f6c : match-clone buffer slot label + caption 0xceb
// ---------------------------------------------------------------------------
void tip_clone_buffer_ceb(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                          unsigned long long p4, unsigned long long p5, unsigned long long p6,
                          unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                          unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                          unsigned p16)
{
    int   value;
    char* caption;
    (void)p10; (void)p11;

    value   = match_clone_buffer_free();
    caption = (char*)get_string_by_id(0xceb);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, p9, caption, value, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80347fc0 : shared - (flags + 0x20) value + caption 0xcf4
//   flags is param_9, dst is param_10; renders (flags + 0x20) as the value string id.
// ---------------------------------------------------------------------------
void tip_flags_plus20_cf4(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                          unsigned long long p4, unsigned long long p5, unsigned long long p6,
                          unsigned long long p7, unsigned long long p8, int flags, int dst,
                          unsigned ctx, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                          unsigned p16)
{
    char* caption;
    (void)ctx;

    caption = (char*)get_string_by_id(0xcf4);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, dst, caption, flags + 0x20,
                         p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80348010 : wrapper - editor flags at ctx+0x11858
// ---------------------------------------------------------------------------
void tip_editor_flags_cf4(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                          unsigned long long p4, unsigned long long p5, unsigned long long p6,
                          unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                          unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                          unsigned p16)
{
    (void)p10; (void)p11;
    tip_flags_plus20_cf4(p1, p2, p3, p4, p5, p6, p7, p8, *(int*)(DAT_80612be4 + 0x11858), p9,
                         (unsigned)DAT_80612be4, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80348040 : wrapper - match_session_get_flags
// ---------------------------------------------------------------------------
void tip_session_flags_cf4(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                           unsigned long long p4, unsigned long long p5, unsigned long long p6,
                           unsigned long long p7, unsigned long long p8, int p9, unsigned p10,
                           unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                           unsigned p16)
{
    int flags;
    (void)p10;
    flags = match_session_get_flags();
    tip_flags_plus20_cf4(p1, p2, p3, p4, p5, p6, p7, p8, flags, p9, p11, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80348074 : shared match-block field-4 label formatter (scope push/pop)
//   Pushes a scope, finds the match block for a flag mask (uninitialised r4 in the decompiler), reads
//   its (block+2) field-4 descriptor string and renders it with the given caption id, then pops.
// ---------------------------------------------------------------------------
void tip_wset_slot_field(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                         unsigned long long p4, unsigned long long p5, unsigned long long p6,
                         unsigned long long p7, unsigned long long p8, unsigned p9, unsigned p10,
                         int caption_id, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                         unsigned p16)
{
    int   dst;
    int   value;
    char* caption;
    unsigned char mask;
    int   block;
    int   col;
    unsigned long long scope;
    (void)p1; (void)p9; (void)p10;

    scope = FUN_803826f0();
    dst   = (int)scope;
    mask  = 0;                                   /* extraout_r4 flag mask (register carry) */
    match_find_block_by_flag(mask, &col, &block);
    value   = match_desc_field4(block + 2);
    value   = get_string_by_id(value);
    caption = (char*)get_string_by_id(caption_id);
    tooltip_format_label(p1, p2, p3, p4, p5, p6, p7, p8, dst, caption, value,
                         p12, p13, p14, p15, p16);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x803480e8 : wrapper - flag mask 1, caption 0xcf1
// ---------------------------------------------------------------------------
void tip_wset_slot1_cf1(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                        unsigned long long p4, unsigned long long p5, unsigned long long p6,
                        unsigned long long p7, unsigned long long p8, unsigned p9, unsigned p10,
                        unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                        unsigned p16)
{
    (void)p10; (void)p11;
    tip_wset_slot_field(p1, p2, p3, p4, p5, p6, p7, p8, p9, 1, 0xcf1, p12, p13, p14, p15, p16);
}

// ---------------------------------------------------------------------------
// 0x80348110 : wrapper - flag mask 2, caption 0xceb
// ---------------------------------------------------------------------------
void tip_wset_slot2_ceb(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                        unsigned long long p4, unsigned long long p5, unsigned long long p6,
                        unsigned long long p7, unsigned long long p8, unsigned p9, unsigned p10,
                        unsigned p11, unsigned p12, unsigned p13, unsigned p14, unsigned p15,
                        unsigned p16)
{
    (void)p10; (void)p11;
    tip_wset_slot_field(p1, p2, p3, p4, p5, p6, p7, p8, p9, 2, 0xceb, p12, p13, p14, p15, p16);
}

} // extern "C"
