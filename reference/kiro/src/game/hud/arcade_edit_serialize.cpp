// Batch: batch_981 | Module: arcade/challenge editor record (de)serialization
// Address range: 0x80348570 - 0x80348be4
// Date: 2026-09-12
//
// These routines walk the challenge/arcade-editor record buffer and marshal its tagged sub-records
// in and out of the editor context (DAT_80612be4) and various scratch/save buffers. The record is a
// flat tag+length stream located via challenge_record_find_section(record, 0xc77668); the section
// header stores the total byte length at word 0x8f. Individual records are identified by tag word:
//
//   0xc7766f  cell record   (stride 0x8c)   -> challenge_serialize_cells
//   0xc7766d  member record (stride 0xb4)   -> challenge_serialize_members
//   0xc77673  node blob      (variable)     -> challenge_copy_node_blobs
//   0xc7766c  mode descriptor(stride 0x20)  -> challenge_serialize_mode_descs
//
// The copy helpers are memcpy variants: leaf_31e8(dst,src,len) and FUN_8038b4cc(dst,src,len) copy in
// opposite directions (save vs load), selected by the param_3 direction mask (bit0 = save, bit1 =
// load). challenge_write_string_field resolves a string id, clears the destination field and copies
// the string in, then advances the caller's id cursor. challenge_export_text_dump is the large
// text-export path: it reserves ~0x9240 bytes of stack scratch, serializes the member + cell tables,
// and formats each entry through FUN_80388098 ("%s %s") into the export buffer.
//
// NON-MATCHING: reproduces the decompiler's structure and pointer arithmetic verbatim. The
// __save_gpr_28 / __restore_gpr_28 spill carriers and the 16/17-parameter formatter tails are kept so
// the ABI-level calls line up; only the meaningful args carry names. Raw FUN_/DAT_ symbols are
// declared file-locally with call-site-accurate arity (Compile-Clean rule 5); named helpers
// (challenge_record_find_section, get_string_by_id, aveditor_type_record_ptr,
// aveditor_get_node_scratch, hud_screen_pop_current, leaf_31e8, and the batch_980 helpers
// soundpad_nop_wrapper / arcade_edit_slot_record / arcade_edit_noop) resolve through shared headers.

#include "common.h"

extern "C" {

// ---- named cross-module helpers (resolve via shared headers) ---------------
extern int*  challenge_record_find_section(int* record, int tag);
extern int   get_string_by_id(int id);
extern unsigned char* aveditor_type_record_ptr(int cell, int type);
extern unsigned char* aveditor_get_node_scratch(void);
extern void  hud_screen_pop_current(void);
extern void  leaf_31e8(unsigned int dst, unsigned int src, int len);
extern void  soundpad_nop_wrapper(void);                 // batch_980
extern int   arcade_edit_slot_record(int index);          // batch_980
extern void  arcade_edit_noop(void);                      // batch_980

// ---- raw callees (file-local, call-site arity/type) ------------------------
extern void  memset_wrapper(int dst, int val, int len);
extern void  FUN_8038b510(int* dst, int* src);            // copy name-string field
extern int   FUN_8038b5c8(int str);                       // string length / is-empty
extern void  FUN_8038b4cc(int dst, int src, int len);     // memcpy(dst,src,len)
extern unsigned long long __save_gpr_28(void);            // _savegpr_28 spill carrier
extern void  __restore_gpr_28(void);
extern unsigned long long FUN_803826d4(void);             // ctx accessor (returns record hi word)
extern unsigned long long FUN_803826d8(void);             // ctx accessor (export variant)
extern int   FUN_803826e4(void);                          // editor context pointer
extern void  FUN_80382720(void);                          // pop / end guard
extern void  FUN_80382724(void);                          // pop / end guard (export)
extern void  FUN_80382730(void);                          // pop / end guard (mode-desc)
extern void  FUN_80388098(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                          unsigned long long a4, unsigned long long a5, unsigned long long a6,
                          unsigned long long a7, unsigned long long a8, int dst, unsigned size,
                          char* fmt, int a12, int a13, unsigned a14, unsigned a15, unsigned a16);

// ---- data symbols ----------------------------------------------------------
extern int   DAT_80612be4;      // editor context base
extern int   DAT_80612be8;      // challenge record base
extern int   DAT_80612614;      // editor screen/state id
extern char  s__s__s_80611268[]; // "%s %s"
extern int   DAT_805d6340;       // saved title string field
extern int   DAT_805d6260;       // saved desc string field
extern int   DAT_805d6180;       // saved detail string field

// ---------------------------------------------------------------------------
// 0x80348570 : per-cell pass over the editor cell table (soundpad nops).
//   Iterates the *(ctx+0x81a4) cells at stride 0xb4; for cells whose linked type record
//   (short at +0x81b4) has a class of 7/9/0xe, iterates its type entries. The body is a
//   series of soundpad nops (a stubbed serialization pass in this build).
// ---------------------------------------------------------------------------
void aveditor_foreach_cell_nop(void)
{
    int cell_off;
    int i;
    int type_rec;
    int link;

    FUN_803826e4();
    cell_off = 0;
    for (i = 0; i < *(int*)(DAT_80612be4 + 0x81a4); i = i + 1) {
        soundpad_nop_wrapper();
        link = (int)*(short*)(DAT_80612be4 + cell_off + 0x81b4);
        if (link != -1) {
            type_rec = DAT_80612be4 + link * 0x8c + 0xdd14;
            soundpad_nop_wrapper();
            int cls = *(int*)(type_rec + 0x80);
            if ((cls == 7) || (cls == 9) || (cls == 0xe)) {
                for (int t = 0; t < (int)(unsigned)*(unsigned char*)(type_rec + 0x76); t = t + 1) {
                    aveditor_type_record_ptr(type_rec, t);
                    soundpad_nop_wrapper();
                }
            }
        }
        cell_off = cell_off + 0xb4;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80348664 : per-texslot nop pass (count at ctx+0x280).
// ---------------------------------------------------------------------------
void aveditor_foreach_texslot_nop(void)
{
    int i;
    __save_gpr_28();
    for (i = 0; i < *(int*)(DAT_80612be4 + 0x280); i = i + 1) {
        soundpad_nop_wrapper();
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x803486d0 : close the current editor card and pop the screen.
// ---------------------------------------------------------------------------
void arcade_edit_close_and_pop(int screen)
{
    arcade_edit_slot_record(*(int*)(screen + 0x188));
    arcade_edit_noop();
    hud_screen_pop_current();
}

// ---------------------------------------------------------------------------
// 0x8034871c : serialize cell records (tag 0xc7766f, stride 0x8c).
// ---------------------------------------------------------------------------
void challenge_serialize_cells(unsigned p1, int dst_base, unsigned dir)
{
    int   total;
    int*  record;
    int*  rec;
    int*  body;
    int   out_off;
    unsigned long long carrier;
    (void)p1;

    carrier = FUN_803826d4();
    record  = (int*)((unsigned long long)carrier >> 0x20);
    out_off = 0;
    total   = challenge_record_find_section(record, 0xc77668)[0x8f];
    for (rec = record + 2; rec < (int*)((int)record + total);
         rec = (int*)((int)body + rec[1])) {
        body = rec + 2;
        if (*rec == 0xc7766f) {
            unsigned dst = (unsigned)((int)carrier + out_off);
            if ((dir & 1) != 0) { leaf_31e8(dst, (unsigned)body, 0x8c); }
            if ((dir & 2) != 0) { FUN_8038b4cc((int)body, (int)dst, 0x20); }
            out_off = out_off + 0x8c;
        }
    }
    (void)dst_base;
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// 0x803487f0 : serialize member records (tag 0xc7766d, stride 0xb4).
//   Returns the number of member records processed (used by the export path).
// ---------------------------------------------------------------------------
int challenge_serialize_members(unsigned p1, int dst_base, unsigned dir)
{
    int   total;
    int*  record;
    int*  rec;
    int   out_off;
    unsigned long long carrier;
    (void)p1;

    carrier = FUN_803826d4();
    record  = (int*)((unsigned long long)carrier >> 0x20);
    out_off = 0;
    total   = challenge_record_find_section(record, 0xc77668)[0x8f];
    for (rec = record + 2; rec < (int*)((int)record + total);
         rec = (int*)((int)(rec + 2) + rec[1])) {
        if (*rec == 0xc7766d) {
            unsigned dst = (unsigned)((int)carrier + out_off);
            if ((dir & 2) != 0) {
                FUN_8038b4cc((int)(rec + 10),  (int)(dst + 0x20), 0x40);
                FUN_8038b4cc((int)(rec + 0x1a),(int)(dst + 0x60), 0x40);
            }
            if ((dir & 1) != 0) { leaf_31e8(dst, (unsigned)(rec + 2), 0xb4); }
            out_off = out_off + 0xb4;
        }
    }
    (void)dst_base;
    FUN_80382720();
    return out_off / 0xb4;
}

// ---------------------------------------------------------------------------
// 0x803488d4 : write a localized string field, advancing the caller's id cursor.
//   dst/len come from the caller; the spill carrier packs dst (hi) and len (lo).
// ---------------------------------------------------------------------------
void challenge_write_string_field(int dst, int len, int* id_cursor)
{
    int str;
    unsigned long long carrier;

    carrier = __save_gpr_28();
    (void)dst; (void)len;
    dst = (int)((unsigned long long)carrier >> 0x20);
    str = get_string_by_id(*id_cursor);
    FUN_8038b5c8(str);
    memset_wrapper(dst, 0, (unsigned)carrier);
    FUN_8038b4cc(dst, str, (unsigned)carrier - 1);
    *id_cursor = *id_cursor + 1;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80348948 : write the card header strings (title/desc/detail) + save copies.
// ---------------------------------------------------------------------------
void challenge_write_header_strings(unsigned p1, int* id_cursor)
{
    int* sect;
    (void)p1;

    sect = challenge_record_find_section((int*)DAT_80612be8, 0xc77668);
    challenge_write_string_field((int)sect + 0x62, 0x14, id_cursor);
    FUN_8038b510(&DAT_805d6340, (int*)((int)sect + 0x62));
    challenge_write_string_field((int)sect + 0x76, 0xe0, id_cursor);
    FUN_8038b510(&DAT_805d6260, (int*)((int)sect + 0x76));
    challenge_write_string_field((int)sect + 0x156, 0xe0, id_cursor);
    FUN_8038b510(&DAT_805d6180, (int*)((int)sect + 0x156));
}

// ---------------------------------------------------------------------------
// 0x803489ec : copy variable-size node blobs (tag 0xc77673) into node scratch.
// ---------------------------------------------------------------------------
void challenge_copy_node_blobs(void)
{
    int   total;
    int*  record;
    int*  rec;
    unsigned char* scratch;

    record = (int*)__save_gpr_28();
    total  = challenge_record_find_section(record, 0xc77668)[0x8f];
    for (rec = record + 2; rec < (int*)((int)record + total);
         rec = (int*)((int)(rec + 2) + rec[1])) {
        if (*rec == 0xc77673) {
            scratch = aveditor_get_node_scratch();
            leaf_31e8((unsigned)(rec + 2), (unsigned)scratch, rec[1]);
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80348a7c : export a text dump of the card (members + cells + type records).
//   Serializes the member/cell tables into stack scratch, then formats each member
//   ("%s %s" via FUN_80388098, string 0xb0c) into the export buffer; follows each
//   member's linked cell + type records.
// ---------------------------------------------------------------------------
void challenge_export_text_dump(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                                unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                unsigned long long p7, unsigned long long p8)
{
    int   count;
    int   i;
    int   out_off;
    int   caption;
    int*  ids;
    unsigned ctx_hi;
    unsigned long long carrier;
    (void)p1;

    unsigned char  members[64];         // auStack_9220
    unsigned char  export_buf[23296];   // auStack_91e0
    unsigned char  cells[118];          // auStack_36d8
    unsigned char  cell_class[10];      // abStack_3662 region
    int            cell_words[3478];    // aiStack_3658
    short          member_link[10];     // local_9234
    unsigned char  hdr[12];             // auStack_9240
    unsigned in_r8 = 0, in_r9 = 0, in_r10 = 0;

    carrier = FUN_803826d8();
    ctx_hi  = (unsigned)((unsigned long long)carrier >> 0x20);
    ids     = (int*)(unsigned)carrier;

    count = challenge_serialize_members(ctx_hi, (int)hdr, 1);   // 0x803487f0 (members, save)
    challenge_serialize_cells(ctx_hi, (int)cells, 1);           // 0x8034871c (cells, save)
    out_off = 0;
    for (i = 0; i < count; i = i + 1) {
        challenge_write_string_field((int)(members + out_off), 0x40, ids);
        caption = get_string_by_id(0xb0c);
        FUN_80388098(carrier, p2, p3, p4, p5, p6, p7, p8,
                     (int)(export_buf + out_off), 0x3f, s__s__s_80611268, caption,
                     (int)(members + out_off), in_r8, in_r9, in_r10);
        int link = (int)member_link[out_off / 2];
        if (link != -1) {
            challenge_write_string_field((int)(cells + link * 0x8c), 0x20, ids);
            int cls = cell_words[link * 0x23];
            if ((cls == 7) || (cls == 9) || (cls == 0xe)) {
                for (int t = 0; t < (int)(unsigned)cell_class[link * 0x8c]; t = t + 1) {
                    unsigned char* tr = aveditor_type_record_ptr((int)(cells + link * 0x8c), t);
                    challenge_write_string_field((int)tr, 0x40, ids);
                }
            }
        }
        out_off = out_off + 0xb4;
    }
    challenge_serialize_members(ctx_hi, (int)hdr, 2);
    challenge_serialize_cells(ctx_hi, (int)cells, 2);
    challenge_copy_node_blobs();
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x80348be4 : serialize mode-descriptor records (tag 0xc7766c, stride 0x20).
// ---------------------------------------------------------------------------
void challenge_serialize_mode_descs(void)
{
    int   record_lo;
    int*  record;
    int*  rec;
    int   desc_off;
    int   saved;
    unsigned long long carrier;

    carrier   = (unsigned long long)FUN_803826e4();
    record_lo = (int)((unsigned long long)carrier >> 0x20);
    record    = (int*)(unsigned)carrier;
    saved     = *record;
    rec       = (int*)(record_lo + 8);

    if (DAT_80612614 == 0x66) {
        desc_off = 0;
        for (int m = 0; m < *(int*)(DAT_80612be4 + 0x280); m = m + 1) {
            challenge_write_string_field(DAT_80612be4 + desc_off + 0x288, 0x20, record);
            desc_off = desc_off + 0xe4;
        }
    }
    *record = saved;
    for (; rec < (int*)(record_lo + 0x7ffc); rec = (int*)((int)(rec + 2) + rec[1])) {
        if (*rec == 0xc7766c) {
            challenge_write_string_field((int)(rec + 2), 0x20, record);
        }
    }
    FUN_80382730();
}

} // extern "C"
