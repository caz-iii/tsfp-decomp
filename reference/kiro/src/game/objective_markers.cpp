// Batch: batch_999 | Module: objective marker / zone resolution
// Address range: 0x8034f194 - 0x8034fd14
// Date: 2026-09-12
//
// The objective-marker resolution layer. Objective slots hold 16-bit "marker" ids that must be
// resolved to concrete runtime entity ids each time the level state changes. Throughout this
// module an id is decoded through one of three actor-record tables by range (the same decode
// used by objective_find_slot_by_entity in objective_state.cpp):
//     7999   <= id < 0x209e : zone-entity table   DAT_80611b94 (bias -0x3e800, stride 0x20)
//     0x209e <= id < 0x20b2 : secondary zone table DAT_80611b98 (bias -0x413c0, stride 0x20)
//     0      <= id < DAT_80611c14 : actor record   DAT_80611b8c (stride 0x20)
//     else                        : -1 (unresolved)
// objective_decode_marker_id centralizes that decode; the individual functions inline it (kept
// faithful to the decompiler).
//
//   objective_run_secondary_handlers 0x8034f194 run each active-wave slot's secondary handler
//                                               (objective_invoke_secondary) with 3 extra args.
//   objective_find_marker_record   0x8034f23c   find the team/marker record (DAT_805c7bd4, count
//                                               DAT_805c7bd8, stride 0x20) matching team+index.
//   objective_resolve_random_zone_markers 0x8034f30c resolve up to 10 slot markers to random
//                                               adjacent-zone entities (rng_counter_tick).
//   objective_run_secondary_simple 0x8034f5ac   like _run_secondary_handlers with no extra args
//                                               (the driver called by objective_notify_completion).
//   objective_resolve_nearest_marker 0x8034f644 pick the list entry nearest the query entity and
//                                               decode it (or decode the single entry when n==1).
//   objective_marker_not_in_list   0x8034f824   1 if `id` decodes to none of the list entries.
//   objective_reassign_markers_nearest 0x8034f904 reassign each slot marker to the nearest
//                                               resolved entity not already used.
//   objective_notify_completion    0x8034f9a0   per-tracker completion notify: resolve owner
//                                               record, run secondary handlers, reassign markers.
//   objective_resolve_marker_list  0x8034fa2c   decode a marker list into an output short array,
//                                               filling gaps from adjacent zones and padding 0xff.
//   objective_resolve_all_markers  0x8034fd14   top-level pass over all trackers: find record,
//                                               resolve randomly or by list, set the count word.
//
// NON-MATCHING: reproduces the decompiler's structure. Register save-pair values are expressed
// as explicit locals from the level-context accessors (Compile-Clean rule 3). Named helpers
// resolve through shared headers where defined; the soundpad/rng/tracker helpers and raw callees
// are declared file-locally (rule 5). The `undefined2` marker writes become `unsigned short`.

#include "common.h"

extern "C" {

// ---- named cross-module helpers --------------------------------------------
extern unsigned int objective_invoke_secondary(int slot, int a2, int a3, int a4, int a5); // batch_998
extern int  worm_class_find_first_active_index(void);

// ---- raw callees (file-local) ----------------------------------------------
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int  FUN_803826d0(void);   // acquire objective descriptor cursor
extern void FUN_8038271c(void);   // release (803826d0)
extern int  FUN_803826d8(void);   // acquire tracker record cursor
extern void FUN_80382724(void);   // release (803826d8)
extern int  FUN_803826e4(void);   // acquire level context
extern void FUN_80382730(void);   // release (803826e4)
extern int  FUN_803826e8(void);   // acquire slot cursor
extern void FUN_80382734(void);   // release (803826e8)
extern int  soundpad_find_zone_by_id(int markerId);
extern unsigned int soundpad_query_adjacent_zones_simple(int zone, int* out, int max, int a4, int a5);
extern unsigned int rng_counter_tick(void);
extern int  tracker_find_record_by_owner(int owner);
extern void memset_wrapper(int dst, int value, int size);

// ---- data symbols ----------------------------------------------------------
extern int*  DAT_80612da0;   // wave dispatch table
extern int   DAT_805c7bc4;   // wave state table base (stride 0x60)
extern int   DAT_805c7bcc;   // tracker record table base (stride 0x10)
extern int   DAT_805c7bd0;   // tracker record count
extern int   DAT_805c7bd4;   // team/marker record table base (stride 0x20)
extern int   DAT_805c7bd8;   // team/marker record count
extern int   DAT_80611b8c;   // actor record table base (stride 0x20)
extern int   DAT_80611b94;   // zone-entity table base (bias -0x3e800)
extern int   DAT_80611b98;   // secondary zone-entity table base (bias -0x413c0)
extern int   DAT_80611c14;   // actor record count
extern float FLOAT_8061ad28; // nearest-marker max distance^2

// ---------------------------------------------------------------------------
// 0x8034f194 : run each active-wave slot's secondary handler with 3 extra args.
// The r25 save-pair carries the caller-supplied context.
// ---------------------------------------------------------------------------
void objective_run_secondary_handlers(unsigned int p1, unsigned int p2, unsigned int p3)
{
    (void)p1; (void)p2;
    int ctx = __save_gpr_25();
    unsigned int carry = 0;
    int wave = worm_class_find_first_active_index();
    if (*(int*)(DAT_80612da0 + wave * 4) != 0) {
        int off = 0;
        int* header = *(int**)(wave * 4 + DAT_80612da0);
        for (int i = 0; i < header[1]; i++) {
            header = *(int**)(wave * 4 + DAT_80612da0);
            carry = objective_invoke_secondary(*header + off, ctx, ctx, p3, carry);
            off = off + 0xc;
        }
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x8034f23c : find the team/marker record whose team code matches the slot's
// team byte (+8, with sign handling) and whose index (>>1) matches param_2.
// ---------------------------------------------------------------------------
unsigned int* objective_find_marker_record(int slot, int index)
{
    int off = 0;
    int remaining = DAT_805c7bd8;
    if (0 < DAT_805c7bd8) {
        do {
            short team = *(short*)(slot + 8);
            unsigned int code = *(unsigned int*)(DAT_805c7bd4 + off);
            unsigned int codeDec = code - 1;
            unsigned int lz = countLeadingZeros((code & 1 ^ -((int)code >> 0x1f)) + ((int)code >> 0x1f));
            unsigned int lzDec = countLeadingZeros((codeDec & 1 ^ -((int)codeDec >> 0x1f)) + ((int)codeDec >> 0x1f));
            if ((((team == 1) || ((team < 1 && (lz = lzDec, code = codeDec, -1 < team)))) &&
                 (index == (int)code >> 1)) && (lz >> 5 != 0)) {
                return (unsigned int*)(DAT_805c7bd4 + off);
            }
            off = off + 0x20;
            remaining = remaining - 1;
        } while (remaining != 0);
    }
    return (unsigned int*)0;
}

// ---------------------------------------------------------------------------
// 0x8034f30c : resolve up to 10 of a wave's markers to random entities chosen
// from the marker's adjacent-zone set. The descriptor cursor (r?) and wave state
// record (DAT_805c7bc4 + param_3*0x60) provide the source marker ids.
// ---------------------------------------------------------------------------
void objective_resolve_random_zone_markers(unsigned int p1, unsigned int p2, int waveIndex, int slot)
{
    (void)p1; (void)p2;
    int cur = FUN_803826d0();
    int state = DAT_805c7bc4 + waveIndex * 0x60;
    int scratchOff = 0;
    int idx = 0;
    int off = 0;
    int adj[34];
    do {
        int markerOff = off + 0xc;
        if (0 < *(short*)(cur + markerOff)) {
            short team = *(short*)(slot + 8);
            if (team == 1) {
                *(unsigned short*)(cur + markerOff) = *(unsigned short*)(state + off + 0x20);
            } else if ((team < 1) && (-1 < team)) {
                *(unsigned short*)(cur + markerOff) = *(unsigned short*)(state + off + 0x40);
            }
            int zone = soundpad_find_zone_by_id((int)*(short*)(cur + markerOff));
            unsigned int n = soundpad_query_adjacent_zones_simple(zone, adj, 0x14, 0, 0);
            if (n != 0) {
                unsigned short resolved;
                bool inRange = false;
                unsigned int r = rng_counter_tick();
                if ((7999 < adj[r - (r / n) * n]) &&
                    (r = rng_counter_tick(), adj[r - (r / n) * n] < 0x209e)) {
                    inRange = true;
                }
                if (inRange) {
                    r = rng_counter_tick();
                    resolved = (unsigned short)*(unsigned int*)(DAT_80611b94 + adj[r - (r / n) * n] * 0x20 + -0x3e800);
                } else {
                    inRange = false;
                    r = rng_counter_tick();
                    if ((0x209d < adj[r - (r / n) * n]) &&
                        (r = rng_counter_tick(), adj[r - (r / n) * n] < 0x20b2)) {
                        inRange = true;
                    }
                    if (inRange) {
                        r = rng_counter_tick();
                        resolved = (unsigned short)*(unsigned int*)(DAT_80611b98 + adj[r - (r / n) * n] * 0x20 + -0x413c0);
                    } else {
                        inRange = false;
                        r = rng_counter_tick();
                        if ((-1 < adj[r - (r / n) * n]) &&
                            (r = rng_counter_tick(), adj[r - (r / n) * n] < DAT_80611c14)) {
                            inRange = true;
                        }
                        if (inRange) {
                            r = rng_counter_tick();
                            resolved = (unsigned short)*(unsigned int*)(DAT_80611b8c + adj[r - (r / n) * n] * 0x20);
                        } else {
                            resolved = 0xffff;
                        }
                    }
                }
                *(unsigned short*)(cur + markerOff) = resolved;
            }
            int zoneOut = soundpad_find_zone_by_id((int)*(short*)(cur + markerOff));
            *(int*)(cur + scratchOff) = zoneOut;
            scratchOff = scratchOff + 4;
        }
        idx = idx + 1;
        off = off + 2;
    } while (idx < 10);
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// 0x8034f5ac : run each active-wave slot's secondary handler with no extra args,
// accumulating the "done" count returned by each handler. The wave index arrives
// via the r28 save-pair. Returns the accumulated count (used by callers to gate
// marker reassignment). The decompiler typed this void, but the call sites
// (objective_notify_completion / objective_resolve_all_markers) consume the
// return value; the int return is restored here (Compile-Clean rule 1).
// ---------------------------------------------------------------------------
int objective_run_secondary_simple(void)
{
    int wave = __save_gpr_28();
    int base = wave * 4;
    unsigned int carry = 0;
    if (*(int*)(DAT_80612da0 + base) != 0) {
        int off = 0;
        int* header = *(int**)(base + DAT_80612da0);
        for (int i = 0; i < header[1]; i++) {
            header = *(int**)(base + DAT_80612da0);
            carry = objective_invoke_secondary(*header + off, wave, 0, 0, carry);
            off = off + 0xc;
        }
    }
    __restore_gpr_28();
    return (int)carry;
}

// ---------------------------------------------------------------------------
// 0x8034f644 : if n != 1, pick the list entry nearest the query entity (squared
// distance vs FLOAT_8061ad28) and decode it; when n == 1 decode the single entry.
// ---------------------------------------------------------------------------
unsigned int objective_resolve_nearest_marker(int* list, int* queryPtr, int n)
{
    int query = *queryPtr;
    int best = 0;
    if (n != 1) {
        int off = 0;
        float bestDist = FLOAT_8061ad28;
        if (0 < n) {
            do {
                int rec = DAT_80611b8c + *(int*)((int)list + off) * 0x20;
                float dx = *(float*)(query + 100) - *(float*)(rec + 0xc);
                float dy = *(float*)(query + 0x68) - *(float*)(rec + 0x10);
                float dz = *(float*)(query + 0x6c) - *(float*)(rec + 0x14);
                float d2 = dz * dz + dx * dx + dy * dy;
                if (d2 < bestDist) {
                    best = *(int*)((int)list + off);
                    bestDist = d2;
                }
                off = off + 4;
                n = n - 1;
            } while (n != 0);
        }
    } else {
        best = *list;
    }
    if ((7999 < best) && (best < 0x209e)) {
        return *(unsigned int*)(DAT_80611b94 + best * 0x20 + -0x3e800);
    }
    if ((0x209d < best) && (best < 0x20b2)) {
        return *(unsigned int*)(DAT_80611b98 + best * 0x20 + -0x413c0);
    }
    if ((-1 < best) && (best < DAT_80611c14)) {
        return *(unsigned int*)(DAT_80611b8c + best * 0x20);
    }
    return 0xffffffff;
}

// ---------------------------------------------------------------------------
// 0x8034f824 : return 1 if `id` decodes to none of the `n` list entries, else 0.
// ---------------------------------------------------------------------------
unsigned int objective_marker_not_in_list(int id, int list, int n)
{
    int off = 0;
    if (0 < n) {
        do {
            int e = *(int*)(list + off);
            int decoded;
            if ((7999 < e) && (e < 0x209e)) {
                decoded = *(int*)(DAT_80611b94 + e * 0x20 + -0x3e800);
            } else if ((0x209d < e) && (e < 0x20b2)) {
                decoded = *(int*)(DAT_80611b98 + e * 0x20 + -0x413c0);
            } else if ((-1 < e) && (e < DAT_80611c14)) {
                decoded = *(int*)(DAT_80611b8c + e * 0x20);
            } else {
                decoded = -1;
            }
            if (id == decoded) {
                return 0;
            }
            off = off + 4;
            n = n - 1;
        } while (n != 0);
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x8034f904 : reassign each of a tracker's markers (+0xe count, +8 id array,
// +0x14 candidate lists) to the nearest resolved entity that is not already used.
// ---------------------------------------------------------------------------
void objective_reassign_markers_nearest(unsigned int p1, unsigned int p2, int n)
{
    (void)p1; (void)p2;
    int cur = FUN_803826d8();
    int rec = *(int*)(cur + 8);
    int idOff = 0;
    int listOff = 0;
    for (int i = 0; i < *(short*)(cur + 0xe); i++) {
        int* candidates = *(int**)(cur + listOff + 0x14);
        int notUsed = objective_marker_not_in_list((int)*(short*)(rec + idOff + 0xc), cur, n);
        if (notUsed != 0) {
            unsigned int resolved = objective_resolve_nearest_marker((int*)cur, candidates, n);
            *(short*)(rec + idOff + 0xc) = (short)resolved;
        }
        idOff = idOff + 2;
        listOff = listOff + 0x14;
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x8034f9a0 : per-tracker completion notify. For each tracker record, resolve
// its owner record, run the active-wave secondary handlers, and (while a wave is
// active) reassign that tracker's markers to nearest entities.
// ---------------------------------------------------------------------------
void objective_notify_completion(void)
{
    FUN_803826e8();
    int i = 0;
    int off = 0;
    unsigned char scratch[72];
    while (true) {
        if (DAT_805c7bd0 <= i) {
            break;
        }
        int owner = tracker_find_record_by_owner(*(int*)(DAT_805c7bcc + off));
        int active = objective_run_secondary_simple();
        (void)owner; (void)scratch;
        if (active == 0) {
            break;
        }
        objective_reassign_markers_nearest(0, (unsigned int)(unsigned long)scratch, active);
        i = i + 1;
        off = off + 0x10;
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x8034fa2c : decode a marker list into the output short array at param_3 (+0xc,
// stride 2), filling any gaps from adjacent zones and padding trailing slots with
// 0xffff up to the record's capacity (+10).
// ---------------------------------------------------------------------------
void objective_resolve_marker_list(unsigned int p1, unsigned int p2, int outArr, int count)
{
    (void)p1; (void)p2;
    int cur = FUN_803826d8();
    int rec = cur;
    unsigned int fillIdx = 0;
    int outOff = 0;
    int srcOff = 0;
    int local[30];
    for (unsigned int i = 0; (int)i < count; i++) {
        memset_wrapper((int)local, 0xff, 0x50);
        int e = *(int*)(cur + srcOff);
        unsigned short decoded;
        if ((7999 < e) && (e < 0x209e)) {
            decoded = (unsigned short)*(unsigned int*)(DAT_80611b94 + e * 0x20 + -0x3e800);
        } else if ((0x209d < e) && (e < 0x20b2)) {
            decoded = (unsigned short)*(unsigned int*)(DAT_80611b98 + e * 0x20 + -0x413c0);
        } else if ((-1 < e) && (e < DAT_80611c14)) {
            decoded = (unsigned short)*(unsigned int*)(DAT_80611b8c + e * 0x20);
        } else {
            decoded = 0xffff;
        }
        int slotOut = outOff + 0xc;
        *(unsigned short*)(outArr + slotOut) = decoded;
        if (*(short*)(rec + 8) == 0) {
            int zone = soundpad_find_zone_by_id((int)*(short*)(outArr + slotOut));
            unsigned int nAdj = soundpad_query_adjacent_zones_simple(zone, local, 0x14, 0, 0);
            bool advance = false;
            if (((int)nAdj <= (int)i) && (nAdj != 0)) {
                advance = true;
            }
            unsigned int pick = i;
            if (advance) {
                pick = fillIdx;
                fillIdx = fillIdx + 1;
            }
            bool valid = false;
            if ((nAdj != 0) && (-1 < (int)pick)) {
                valid = true;
            }
            if (valid) {
                int a = local[pick];
                if ((7999 < a) && (a < 0x209e)) {
                    decoded = (unsigned short)*(unsigned int*)(DAT_80611b94 + a * 0x20 + -0x3e800);
                } else if ((0x209d < a) && (a < 0x20b2)) {
                    decoded = (unsigned short)*(unsigned int*)(DAT_80611b98 + a * 0x20 + -0x413c0);
                } else if ((-1 < a) && (a < DAT_80611c14)) {
                    decoded = (unsigned short)*(unsigned int*)(DAT_80611b8c + a * 0x20);
                } else {
                    decoded = 0xffff;
                }
            } else {
                decoded = *(unsigned short*)(outArr + slotOut);
            }
            *(unsigned short*)(outArr + slotOut) = decoded;
        }
        outOff = outOff + 2;
        srcOff = srcOff + 4;
    }
    // Shift the resolved block up when the record's capacity (+10) exceeds count,
    // then pad the remainder with 0xffff.
    if (count < *(short*)(rec + 10)) {
        int dstOff = count << 1;
        int cap = (int)*(short*)(rec + 10);
        for (int j = count; j < cap; j++) {
            cap = (int)*(short*)(rec + 10);
            int slotOut = dstOff + 0xc;
            dstOff = dstOff + 2;
            *(unsigned short*)(outArr + slotOut) =
                *(unsigned short*)(outArr + (j - count) * 2 + 0xc);
        }
        int remaining = 10 - cap;
        int padOff = cap << 1;
        if (cap < 10) {
            do {
                int slotOut = padOff + 0xc;
                padOff = padOff + 2;
                *(unsigned short*)(outArr + slotOut) = 0xffff;
                remaining = remaining - 1;
            } while (remaining != 0);
        }
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x8034fd14 : top-level marker resolution. For each tracker record, find its
// marker record, then (unless a wave is active) resolve random zone markers;
// set the team's count word (+1, from the team byte), and resolve the marker list
// bounded to 10 entries.
// ---------------------------------------------------------------------------
void objective_resolve_all_markers(void)
{
    int ctx = FUN_803826e4();
    int off = 0;
    unsigned char scratch[72];
    for (int i = 0; i < DAT_805c7bd0; i++) {
        int tracker = DAT_805c7bcc + off;
        unsigned int* marker = objective_find_marker_record(tracker, ctx);
        int count = objective_run_secondary_simple();
        if (count == 0) {
            objective_resolve_random_zone_markers((unsigned int)(unsigned long)scratch,
                                                  (unsigned int)(unsigned long)marker,
                                                  ctx + -1, tracker);
        }
        short team = *(short*)(tracker + 8);
        if (team == 1) {
            *(unsigned short*)(marker + 1) = 1;
        } else if ((team < 1) && (-1 < team)) {
            *(unsigned short*)(marker + 1) = 0;
        }
        int bounded = 10;
        if (count < 0xb) {
            bounded = count;
        }
        objective_resolve_marker_list((unsigned int)(unsigned long)scratch,
                                      (unsigned int)(unsigned long)tracker,
                                      (int)marker, bounded);
        off = off + 0x10;
    }
    FUN_80382730();
}

} // extern "C"
