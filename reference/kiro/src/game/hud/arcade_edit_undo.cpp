// Batch: batch_976 | Module: arcade/challenge editor map undo/redo stack (record diff + apply)
// Address range: 0x80346ed0 - 0x80347614
// Date: 2026-09-12
//
// The undo/redo layer that sits directly on top of batch_975's panel ring-buffer allocator. When the
// editor mutates the working map it serializes the map into the 0x8000 arena (DAT_80612d94), diffs it
// against the previous serialized image to produce a compact run-list of changed (offset,length)
// spans, and pushes that diff as a record onto one of two singly-linked "history" lists: the undo
// list (head DAT_80612d8c) and the redo list (head DAT_80612d88). Each record is bump-allocated from
// the ring; its layout is { +0x00 prev-cursor, +0x04 span-count, +0x08 base-image ptr, +0x0c
// diff-spans ptr, +0x10 next-link, +0x14 changed-image ptr }. Applying a record walks its spans and
// memcpy's them back into the working buffer (via leaf_31e8 = memcpy). DAT_80612d80 is the ring
// write cursor; DAT_80612d84/d90/d94 are the three scratch arenas from panel_ring.cpp.
//
//   aveditor_undo_apply_record   0x80346ed0  rebuild the working image from the current undo record.
//   aveditor_diff_spans          0x8034701c  diff two byte images into a (offset,length) span list.
//   aveditor_span_total_len      0x80347134  sum the length fields of a span list.
//   aveditor_undo_push_record    0x80347164  serialize+diff the map and push a new history record.
//   ptr_is_nonnull               0x80347370  return (p != 0) as a bool.
//   aveditor_can_redo            0x8034737c  redo available? (undo-list head DAT_80612d8c nonnull).
//   aveditor_can_undo            0x803473a0  undo available? (redo-list head DAT_80612d88 nonnull).
//   aveditor_undo_dispatch       0x803473c4  undo/redo/checkpoint dispatcher keyed by a direction code.
//   aveditor_flush_dirty_map     0x8034757c  on the editor dirty flag, checkpoint + rebuild the screen.
//   aveditor_get_selection_label 0x80347614  resolve the label string for the current selection.
//
// NON-MATCHING: reproduces the decompiler's structure. The 8-argument (r3..r10) tails on the map
// serialize/deserialize calls are kept so the ABI-level call sites line up; the extra register
// params are leftover scratch and carry no meaning. Raw FUN_/DAT_ symbols are declared file-locally
// with call-site-accurate arity (Compile-Clean rule 5); named cross-module helpers
// (aveditor_serialize_map, aveditor_deserialize_map, leaf_31e8, panel_ring_alloc, list_*_next10,
// memset_wrapper, hud_screen_*, get_string_by_id, roster_slot_to_layout) resolve through the shared
// headers / the same file-local extern pattern already used across the arcade_edit_card_* files
// (rule 6). aveditor_deserialize_map is declared here with the proven 8-arg int prototype used in
// arcade_edit_card_slot_ui.cpp / arcade_edit_card_table.cpp.

#include "common.h"

extern "C" {

// ---- ring/arena + history state (shared with panel_ring.cpp) ---------------
extern int DAT_80612d80;   // ring write cursor
extern int DAT_80612d84;   // 0x1fff0 diff-work arena base ptr
extern int DAT_80612d88;   // redo-list head
extern int DAT_80612d8c;   // undo-list head
extern int DAT_80612d94;   // 0x8000 serialized-image arena base ptr

// ---- named cross-module helpers --------------------------------------------
extern void memset_wrapper(int dst, int val, int len);
extern void leaf_31e8(unsigned dst, unsigned src, int len);           // memcpy(dst,src,len)
extern int  aveditor_serialize_map(void);
extern int  aveditor_get_record_capacity(int scratch);
extern void challenge_record_find_end(int* record);
extern int* panel_get_scratch_ptr(void);
extern int  panel_ring_alloc(int size);
extern void panel_ring_make_room(int size);
extern void panel_noop(void);
extern int  list_get_next10(int node);
extern int  list_link_next10(int node, int prev);
extern int  hud_screen_find_root(int screen);
extern void hud_screen_rebuild_items(void);
extern void** aveditor_get_screen_desc(void);
extern int  roster_slot_to_layout(unsigned slot);
extern int  get_string_by_id(int id);
extern int  aveditor_build_palette_entry_label(int* entry);

// The map serialize/deserialize helpers carry an 8-register (r3..r10) tail at the ABI level; declared
// with the same prototypes already proven to compile in arcade_edit_card_slot_ui.cpp / card_table.cpp.
extern unsigned long long aveditor_undo_ctx(void);                     // FUN_803826e4: {r3:image, r4:record}
extern unsigned long long aveditor_undo_ctx2(void);                    // FUN_803826d4: {r3:prev, r4:cur}
extern void aveditor_undo_ctx_begin(void);                             // FUN_80382730
extern void aveditor_undo_ctx_end(void);                              // FUN_80382720
extern int  aveditor_deserialize_map(unsigned long long f1, unsigned long long r4,
                                     unsigned long long r5, unsigned long long r6,
                                     unsigned long long r7, unsigned long long r8,
                                     unsigned long long r9, unsigned long long r10);

// ---- raw editor-state globals ----------------------------------------------
extern int   DAT_80612be4;     // editor context base
extern int   DAT_806119ec;     // active card/screen index
extern int   DAT_80612d94_id;  // (unused placeholder to keep symbol grouping)

// ---- forward decls within this TU ------------------------------------------
int  aveditor_diff_spans(int prev, int cur, int spans, int prev_len, int cur_len);
int  aveditor_span_total_len(int spans, int count);
int  aveditor_undo_apply_record(void);
int  ptr_is_nonnull(int p);
void aveditor_undo_dispatch(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                            unsigned long long a4, unsigned long long a5, unsigned long long a6,
                            unsigned long long a7, unsigned long long a8, int* record, int dir);

// ---------------------------------------------------------------------------
// 0x80346ed0 : rebuild the working image from the current undo record
//   Copies each diff span from the record's changed-image into the base image, then re-emits the
//   base image into the serialized (0x8000) arena and closes out the record.
// ---------------------------------------------------------------------------
int aveditor_undo_apply_record(void)
{
    int* base_image;
    int  record;
    int  spans;
    int  count;
    int  i;
    int  span_ofs;
    int  dst_ofs;
    unsigned long long ctx;

    ctx        = aveditor_undo_ctx();
    base_image = (int*)((unsigned long long)ctx >> 0x20);   // r3: base image ptr
    record     = (int)ctx;                                  // r4: record ptr
    dst_ofs    = 0;
    panel_noop();
    span_ofs = 0;
    count    = *(int*)(record + 4);                         // span count
    for (i = 0; i < count; i = i + 1) {
        spans = *(int*)(record + 0x14) + span_ofs;          // &changed-image spans[i]
        leaf_31e8((int)base_image + *(int*)spans, *(int*)(record + 0xc) + dst_ofs, *(int*)(spans + 4));
        span_ofs = span_ofs + 8;
        dst_ofs  = dst_ofs + *(int*)(spans + 4);
    }
    panel_noop();
    panel_noop();
    dst_ofs = 0;
    for (i = 0; i < *(int*)(record + 4); i = i + 1) {
        int rem = *(int*)(*(int*)(record + 0x14) + dst_ofs + 4);
        if (0 < rem) {
            do { rem = rem + -1; } while (rem != 0);
        }
        dst_ofs = dst_ofs + 8;
    }
    dst_ofs  = 0;
    span_ofs = 0;
    count    = *(int*)(record + 4);
    for (i = 0; i < count; i = i + 1) {
        spans = *(int*)(record + 0x14) + span_ofs;
        leaf_31e8(*(int*)(record + 0xc) + dst_ofs, DAT_80612d94 + *(int*)spans, *(int*)(spans + 4));
        span_ofs = span_ofs + 8;
        dst_ofs  = dst_ofs + *(int*)(spans + 4);
    }
    challenge_record_find_end(base_image);
    aveditor_undo_ctx_begin();
    return record;
}

// ---------------------------------------------------------------------------
// 0x8034701c : diff two byte images into a (offset,length) span list
//   Walks prev vs cur; whenever bytes differ (with a 0x20-byte minimum run to coalesce), records the
//   run start offset and length into the spans array. A trailing size-mismatch tail is appended.
// ---------------------------------------------------------------------------
int aveditor_diff_spans(int prev, int cur, int spans, int prev_len, int cur_len)
{
    int  i;
    int  written;
    int  span_ofs;
    int  limit;
    int  run;
    int* span;
    char* pc_cur;
    char* pc_prev;

    i        = 0;
    written  = 0;
    span_ofs = 0;
    limit    = cur_len;
    if (prev_len < cur_len) {
        limit = prev_len;
    }
    for (; run = 0, i < limit; i = i + 1) {
        span    = (int*)(spans + span_ofs);
        pc_cur  = (char*)(cur + i);
        pc_prev = (char*)(prev + i);
        for (; (i < limit && ((*pc_cur != *pc_prev) || (run < 0x20))); run = run + 1) {
            if (*span == -1) {
                *span = i;
            }
            i       = i + 1;
            pc_prev = pc_prev + 1;
            pc_cur  = pc_cur + 1;
            span[1] = span[1] + 1;
        }
        if (*span != -1) {
            written  = written + 1;
            span_ofs = span_ofs + 8;
        }
    }
    if (prev_len < cur_len) {
        *(int*)(spans + written * 8)     = prev_len;
        *(int*)(spans + written * 8 + 4) = cur_len - prev_len;
    }
    else if (cur_len < prev_len) {
        *(int*)(spans + written * 8)     = cur_len;
        *(int*)(spans + written * 8 + 4) = prev_len - cur_len;
    }
    return written;
}

// ---------------------------------------------------------------------------
// 0x80347134 : sum the length fields of a span list
// ---------------------------------------------------------------------------
int aveditor_span_total_len(int spans, int count)
{
    int total;
    int ofs;

    total = 0;
    ofs   = 0;
    if (0 < count) {
        do {
            total = total + *(int*)(spans + ofs + 4);
            ofs   = ofs + 8;
            count = count + -1;
        } while (count != 0);
    }
    return total;
}

// ---------------------------------------------------------------------------
// 0x80347164 : serialize+diff the current map and push a new history record
//   Builds the diff between the caller's previous image and the current serialized map into the
//   0x1fff0 work arena, then bump-allocates a record (header + copied images + span list) from the
//   ring and links it. leaf_31e8 = memcpy throughout.
// ---------------------------------------------------------------------------
int aveditor_undo_push_record(int cur_image, int cur_len, int prev_image, int prev_len)
{
    int   span_count;
    int   spans;
    int   i;
    int   ofs;
    int   scratch_len;
    int   scratch_copy;
    int   spanbytes;
    int   dst_ofs;
    int*  record;
    unsigned long long ctx;
    int*  scratch;
    (void)cur_image; (void)cur_len;

    ctx   = aveditor_undo_ctx2();
    spans = DAT_80612d84;
    memset_wrapper(DAT_80612d84, 0, 0x1fff0);
    ofs = 0;
    i   = 0x3ffe;
    do {
        *(int*)(spans + ofs) = -1;
        ofs = ofs + 8;
        i   = i + -1;
    } while (i != 0);
    span_count = aveditor_diff_spans((int)((unsigned long long)ctx >> 0x20), prev_image, spans,
                                     (int)ctx, prev_len);
    if (span_count != 0) {
        dst_ofs     = 0;
        scratch_len = aveditor_span_total_len(spans, span_count);
        scratch     = panel_get_scratch_ptr();
        scratch_copy = ((int (*)(int))challenge_record_find_end)((int)scratch); // strlen-style length probe
        spanbytes   = span_count * 8;
        panel_ring_make_room((scratch_copy + 4U & 0xfffffffc) +
                             (scratch_len + 3U & 0xfffffffc) + spanbytes + 0x18);
        record    = (int*)panel_ring_alloc(0x18);
        memset_wrapper((int)record, 0, 4);
        *record   = DAT_80612d80;
        record[2] = panel_ring_alloc(scratch_copy + 1);
        record[1] = span_count;
        record[5] = panel_ring_alloc(spanbytes);
        record[3] = panel_ring_alloc(scratch_len);
        scratch   = panel_get_scratch_ptr();
        leaf_31e8(record[2], (unsigned)scratch, scratch_copy + 1);
        leaf_31e8(record[5], spans, spanbytes);
        ofs = 0;
        for (i = 0; i < span_count; i = i + 1) {
            leaf_31e8(record[3] + dst_ofs, DAT_80612d94 + *(int*)(record[5] + ofs),
                      *(int*)(record[5] + ofs + 4));
            dst_ofs = dst_ofs + *(int*)(record[5] + ofs + 4);
            ofs     = ofs + 8;
        }
        panel_noop();
        panel_noop();
        ofs = 0;
        for (i = 0; i < span_count; i = i + 1) {
            int rem = *(int*)(record[5] + ofs + 4);
            if (0 < rem) {
                do { rem = rem + -1; } while (rem != 0);
            }
            ofs = ofs + 8;
        }
        record[4] = 0;
    }
    aveditor_undo_ctx_end();
    return DAT_80612d8c;
}

// ---------------------------------------------------------------------------
// 0x80347370 : return (p != 0) as a bool
// ---------------------------------------------------------------------------
int ptr_is_nonnull(int p)
{
    return (p != 0);
}

// ---------------------------------------------------------------------------
// 0x8034737c : redo available? (undo-list head nonnull)
// ---------------------------------------------------------------------------
int aveditor_can_redo(void)
{
    return ptr_is_nonnull(DAT_80612d8c);
}

// ---------------------------------------------------------------------------
// 0x803473a0 : undo available? (redo-list head nonnull)
// ---------------------------------------------------------------------------
int aveditor_can_undo(void)
{
    return ptr_is_nonnull(DAT_80612d88);
}

// ---------------------------------------------------------------------------
// 0x803473c4 : undo/redo/checkpoint dispatcher keyed by a direction code
//   dir ==  1 : redo   (pop undo-list, apply, re-link onto redo-list, re-deserialize)
//   dir ==  0 : checkpoint (serialize current map, push a fresh record)
//   dir  in 1..2 : undo (pop redo-list, apply, re-link onto undo-list, re-deserialize)
//   Finally zero-fills the tail of the working record and re-emits it into the 0x8000 arena.
// ---------------------------------------------------------------------------
void aveditor_undo_dispatch(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                            unsigned long long a4, unsigned long long a5, unsigned long long a6,
                            unsigned long long a7, unsigned long long a8, int* record, int dir)
{
    int  undo_head;
    int* redo_head;
    int  cap;
    int  spans;
    unsigned long long applied;

    undo_head = DAT_80612d8c;
    redo_head = (int*)DAT_80612d88;
    cap       = 0;
    if (dir == 1) {
        panel_noop();
        aveditor_serialize_map();
        challenge_record_find_end(record);
        DAT_80612d8c = list_get_next10(DAT_80612d8c);
        *(int*)(undo_head + 0x10) = 0;
        DAT_80612d88 = list_link_next10(DAT_80612d88, undo_head);
        applied = (unsigned long long)(unsigned)aveditor_undo_apply_record();
        aveditor_deserialize_map(applied, a2, a3, a4, a5, a6, a7, a8);
        cap = aveditor_get_record_capacity((int)record);
    }
    else if (dir < 1) {
        if (-1 < dir) {
            aveditor_serialize_map();
            cap = aveditor_get_record_capacity((int)record);
            if (redo_head != (int*)0) {
                if (DAT_80612d8c == 0) {
                    DAT_80612d80 = 0;
                }
                else {
                    DAT_80612d80 = *redo_head;
                }
                DAT_80612d88 = 0;
            }
            spans = aveditor_get_record_capacity(DAT_80612d94);
            spans = aveditor_undo_push_record((int)record, cap, DAT_80612d94, spans);
            DAT_80612d8c = list_link_next10(DAT_80612d8c, spans);
        }
    }
    else if (dir < 3) {
        aveditor_serialize_map();
        DAT_80612d88 = list_get_next10(DAT_80612d88);
        redo_head[4] = 0;
        DAT_80612d8c = list_link_next10(DAT_80612d8c, (int)redo_head);
        applied = (unsigned long long)(unsigned)aveditor_undo_apply_record();
        aveditor_deserialize_map(applied, a2, a3, a4, a5, a6, a7, a8);
        cap = aveditor_get_record_capacity((int)record);
    }
    memset_wrapper((int)record + cap, 0, 0x8000 - cap);
    leaf_31e8((unsigned)DAT_80612d94, (unsigned)(int)record, 0x7ffc);
}

// ---------------------------------------------------------------------------
// 0x8034757c : on the editor dirty flag, checkpoint + rebuild the screen
// ---------------------------------------------------------------------------
void aveditor_flush_dirty_map(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                              unsigned long long a4, unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8, int* record)
{
    int    root;
    void** desc;

    if ((*(unsigned int*)(DAT_80612be4 + 0x238) & 0x20) != 0) {
        root = hud_screen_find_root(*(int*)(0x8051a820 + DAT_806119ec * 0x1e0));
        aveditor_undo_dispatch(a1, a2, a3, a4, a5, a6, a7, a8, record, 0);
        if (root != 0) {
            desc = aveditor_get_screen_desc();
            if (*(void***)(root + 4) == desc) {
                hud_screen_rebuild_items();
            }
        }
        *(unsigned int*)(DAT_80612be4 + 0x238) = *(unsigned int*)(DAT_80612be4 + 0x238) & 0xffffffdf;
    }
}

// ---------------------------------------------------------------------------
// 0x80347614 : resolve the label string for the current selection
//   If nothing is grabbed (DAT+0x2f1c == 0) look up the roster/layout label for the highlighted
//   cell; otherwise build the palette-entry label for the grabbed item.
// ---------------------------------------------------------------------------
int aveditor_get_selection_label(void)
{
    int result;
    int cell;
    int layout;

    result = 0;
    cell   = DAT_80612be4 + *(int*)(DAT_80612be4 + 0x2f78) * 0xc + 0x3070;
    if (*(int**)(DAT_80612be4 + 0x2f1c) == (int*)0) {
        if (cell != 0) {
            layout = roster_slot_to_layout((unsigned)*(unsigned char*)(cell + 7));
            result = get_string_by_id((int)*(short*)(0x80477bc8 + layout * 0xc4));
        }
    }
    else {
        result = aveditor_build_palette_entry_label(*(int**)(DAT_80612be4 + 0x2f1c));
    }
    return result;
}

} // extern "C"
