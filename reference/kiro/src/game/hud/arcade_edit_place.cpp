// Batch: batch_879 | Module: arcade map-editor companion-segment placement + budget tally
// Address range: 0x8030d300 - 0x8030f5dc
// Date: 2026-09-11
//
// The arcade map-editor's placement engine: when a multi-cell record (door / wall /
// stair etc.) is dropped, these functions hit-test the target cells, find the
// companion grid segments, and commit the placement, plus the budget/tally helpers
// that count placed members by type and drive the capability hooks. All operate on
// the active screen context DAT_80612be4 and the global member list DAT_80612c14.
//
//   aveditor_compute_max_height        0x8030d300  scan members for the tallest stacked
//                                                  cell height (per-type table DAT_8047c164).
//   aveditor_link_picked_pair          0x8030d3a4  link the two picked companion records
//                                                  (FUN_8034202c both halves).
//   aveditor_place_companion_segments  0x8030d430  for each entry in the picked record's
//                                                  byte list, resolve+link the companion
//                                                  segment; returns the placement flags.
//   aveditor_place_all_segments        0x8030d7c8  the big per-direction placer: for every
//                                                  record entry, roster_hit_test_point the
//                                                  target cell in the relevant direction,
//                                                  find the companion segment, and append the
//                                                  placed sub-records (FUN_80344710).
//   aveditor_min_linked_index          0x8030efc0  minimum linked-partner index of a given
//                                                  type across all members; is-zero->1 form.
//   aveditor_special_placement_active  0x8030f088  1 when either special-placement predicate
//                                                  (FUN_803394e0 / FUN_8032a6b4) is set.
//   aveditor_query_capability          0x8030f0d0  map a capability index to its editor-state
//                                                  flag bit (piVar1[0x8e] mask table).
//   aveditor_tally_and_trigger         0x8030f1f4  tally placed member types (spawns, doors,
//                                                  keys), then fire the budget/hint hooks.
//   aveditor_sum_member_counts         0x8030f588  sum FUN_8030f8f4 over all members.
//   aveditor_sum_member_type_counts    0x8030f5dc  sum FUN_80343c4c(member.type) over members.
//
// NON-MATCHING: aveditor_place_companion_segments / _place_all_segments carry the PPC
// scratch-frame push/pop intrinsics (FUN_80382xxx) as raw externs; register-pair returns
// (FUN_803826xx) are read as int and split hi/lo; CONCAT44 int->float bias folds to plain
// float; the decompiler's leftover-register reads from void helpers are dropped. The
// unreachable-block warnings on the big placer are dead tail code the decompiler pruned.
// aveditor_place_companion_segments is typed to return u32 (its callers read the placement
// flags) though the decompiler shows void. Named helpers resolve via generated_decls.h;
// every raw FUN_/DAT_/FLOAT_/DOUBLE_ symbol gets a per-file extern below.

#include "common.h"
#include "hud.h"

extern "C" {

// scratch-frame push/pop intrinsics (register-pair returns carry the picked record ptrs)
u64  FUN_803826f0(void);
void FUN_8038273c(void);
u64  FUN_803826dc(void);
void FUN_80382728(void);
u64  FUN_803826b4(void);
void FUN_80382700(void);
void FUN_803826c8(void);
void FUN_80382714(void);

// raw FUN_ helpers
unsigned char FUN_8034202c(int record);                 // commit/link a record half
int  FUN_80313af4(double x, double y, double z);          // create/resolve a cell at coords
int  aselgrid_find_companion_segment(int cell, int x, int y, int z, u32 kind, u32 len);
u32  FUN_80342968(int record);                            // placement-conflict predicate
u32  roster_hit_test_point(double x, double y, double z); // cursor/cell hit mask
int  FUN_80344710(double x, double y, double z, float* rec, u32 kind, unsigned short* outPair);
u32  FUN_80341fcc(int record, u32 flags);                 // companion-required predicate
void FUN_80344528(void);                                  // clear companion scratch
int  FUN_803437cc(float* rec, int cell, unsigned short* outPair); // append placed sub-record
int  FUN_803394e0(void);                                  // special-placement predicate A
int  FUN_8032a6b4(void);                                  // special-placement predicate B
int* FUN_80339b90(int scratch, int key);                  // resolve editor-state block
int* FUN_8031d710(void);                                  // budget scratch block
int  FUN_803142f4(void);                                  // next budget bucket count
void FUN_80337758(void);                                  // fire door/key hint hook
void FUN_80328838(void);                                  // recompute budget totals
int  FUN_8030f8f4(void);                                  // per-member placed count
int  FUN_80343c4c(int type);                              // per-type placed count

// active screen context + globals
extern int   DAT_80612be4;   // active screen context base
extern int   DAT_80612be8;   // editor scratch base
extern int   DAT_80612c14;   // head of the current member linked list
extern unsigned char DAT_8047c164;   // per-type stack-height table

extern float FLOAT_8061a614;         // one grid unit
extern double DOUBLE_8061a5e0;       // int->float bias
extern double DOUBLE_8061a618;       // byte->float bias

} // extern "C"

// forward decls
void aveditor_compute_max_height(void);
void aveditor_link_picked_pair(void);
u32  aveditor_place_companion_segments(void);
int  aveditor_special_placement_active(void);

// ---------------------------------------------------------------------------
// 0x8030d300  aveditor_compute_max_height
// ---------------------------------------------------------------------------
void aveditor_compute_max_height(void)
{
    int maxH = -2;
    int node = DAT_80612c14;
    if (*(int *)(DAT_80612be4 + 0x21c) == 0) {
        for (; node != 0; node = *(int *)(node + 0x40)) {
            int h = (int)(*(float *)(node + 8) +
                          (float)((double)CONCAT44(0x43300000,
                                   *(int *)(&DAT_8047c164 + *(int *)(node + 0x10) * 0x140) - 1U ^ 0x80000000) -
                                  DOUBLE_8061a5e0));
            if (maxH < h) {
                maxH = h;
            }
        }
    }
    (void)maxH;
}

// ---------------------------------------------------------------------------
// 0x8030d3a4  aveditor_link_picked_pair
// ---------------------------------------------------------------------------
void aveditor_link_picked_pair(void)
{
    u64 frame = FUN_803826f0();
    int hi = (int)(frame >> 0x20);
    int lo = (int)frame;
    if ((hi != 0) && (lo != 0) &&
        (FUN_8034202c(*(int *)(hi + 0x10)) != 0)) {
        FUN_8034202c(*(int *)(lo + 0x10));
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8030d430  aveditor_place_companion_segments
// Returns the placement flags word (callers test bit 0x2000000).
// ---------------------------------------------------------------------------
u32 aveditor_place_companion_segments(void)
{
    float* rec = (float *)FUN_803826dc();
    aveditor_compute_max_height();
    int off = 0;
    int listBase = (int)rec[(int)rec[3] + 0xb];
    for (int i = 0; i < *(int *)(listBase + 0x44); i = i + 1) {
        unsigned char* pe = (unsigned char *)(listBase + off + 0x48);
        if ((pe[5] != 2) && (pe[5] != 3)) {
            unsigned char kind = *pe;
            if (kind == 6) {
                double bz = (double)CONCAT44(0x43300000, (u32)pe[3]);
                int cell = FUN_80313af4((double)rec[0], (double)rec[1],
                                        (double)((rec[2] + (float)(bz - DOUBLE_8061a618)) - FLOAT_8061a614));
                int seg = aselgrid_find_companion_segment(
                    cell, (int)rec[0], (int)rec[1],
                    (int)(rec[2] + (float)((double)CONCAT44(0x43300000, (u32)pe[3]) - DOUBLE_8061a618)),
                    (u32)*pe, (u32)pe[4]);
                if (((-1 < seg) && ((FUN_80342968((int)rec) & 0xff) == 0)) &&
                    (FUN_8034202c((int)rec[4]) != 0)) {
                    aveditor_link_picked_pair();
                }
            }
            else if ((kind < 6) && (4 < kind)) {
                int cell = FUN_80313af4((double)rec[0], (double)rec[1],
                                        (double)(FLOAT_8061a614 + rec[2] +
                                                 (float)((double)CONCAT44(0x43300000, (u32)pe[3]) - DOUBLE_8061a618)));
                int seg = aselgrid_find_companion_segment(
                    cell, (int)rec[0], (int)rec[1],
                    (int)(rec[2] + (float)((double)CONCAT44(0x43300000, (u32)pe[3]) - DOUBLE_8061a618)),
                    (u32)*pe, (u32)pe[4]);
                if (-1 < seg) {
                    // original: (FUN_80342968(cell)&0xff)==0 && aveditor_link_picked_pair()==0
                    if (((FUN_80342968(cell) & 0xff) == 0)) {
                        aveditor_link_picked_pair();
                        FUN_8034202c((int)rec[4]);
                    }
                }
            }
        }
        off = off + 6;
    }
    FUN_80382728();
    return *(u32 *)(DAT_80612be4 + 0x25c);
}

// ---------------------------------------------------------------------------
// 0x8030d7c8  aveditor_place_all_segments
// The big per-direction placer. rec = picked record; cell = active placement cell
// (both read from the scratch frame). For each record entry the target cell is
// hit-tested in the entry's direction and, on a companion match, the placed
// sub-records are appended.
// ---------------------------------------------------------------------------
void aveditor_place_all_segments(void)
{
    u64    frame = FUN_803826b4();
    float* rec   = (float *)(frame >> 0x20);
    int    cell  = (int)frame;
    int    listBase = (int)rec[(int)rec[3] + 0xb];
    double rx = (double)rec[0];
    double ry = (double)rec[1];

    aveditor_compute_max_height();

    int i = 0;
    int off = 0;
    for (;;) {
        if (*(int *)(listBase + 0x44) <= i) {
            break;
        }
        int e = listBase + off;
        unsigned char lenB = *(unsigned char *)(e + 0x49);
        unsigned char depB = *(unsigned char *)(e + 0x4a);
        unsigned char htB  = *(unsigned char *)(e + 0x4b);
        char state = *(char *)(e + 0x4d);
        unsigned char kind = *(unsigned char *)(e + 0x48);
        unsigned char runLen = *(unsigned char *)(e + 0x4c);

        double bz = (double)CONCAT44(0x43300000, (u32)htB);
        double ez = (double)(rec[2] + (float)(bz - DOUBLE_8061a618));
        double bx = (double)CONCAT44(0x43300000, (u32)lenB);
        double by = (double)CONCAT44(0x43300000, (u32)depB);
        double ex = (double)(float)(rx + (double)(float)(bx - DOUBLE_8061a618));
        double ey = (double)(float)(ry + (double)(float)(by - DOUBLE_8061a618));
        int zSeg = (int)(rec[2] + (float)((double)CONCAT44(0x43300000, (u32)htB) - DOUBLE_8061a618));

        if ((state == '\x01') && (1 < runLen)) {
            // linked run: hit-test then append each sub-cell
            u32 dirMask = 0;
            if (kind == 3) { dirMask = 1; }
            else if (kind == 1) { dirMask = 4; }
            else if ((kind != 0) && (kind < 3)) { dirMask = 8; }
            else if ((kind >= 3) && (kind < 5)) { dirMask = 2; }

            double hx = ex, hy = ey;
            if (kind == 1) { hy = (double)(float)(ey - (double)FLOAT_8061a614); }
            else if ((kind >= 3) && (kind < 5)) { hx = (double)(float)(ex - (double)FLOAT_8061a614); }

            u32 hit = roster_hit_test_point(hx, hy, ez);
            bool matched = (dirMask != 0) && ((hit & dirMask) != 0);
            if (matched) {
                int seg = aselgrid_find_companion_segment(
                    *(int *)(DAT_80612be4 + 0x2f80), (int)ex, (int)ey, zSeg, (u32)kind, (u32)runLen);
                if (-1 < seg && cell != 0) {
                    for (int k = 0; k < (int)runLen; k = k + 1) {
                        double d8 = (double)(CONCAT44(0x43300000, (((u32)lenB + (u32)runLen) - k) - 1U) ^ 0x80000000);
                        int added = FUN_80344710((double)(float)(rx + (double)(float)(d8 - DOUBLE_8061a5e0)),
                                                 ey, ez, rec, (u32)kind,
                                                 (unsigned short *)(cell + (u32)*(unsigned short *)(cell + 0x20) * 8 + 0x28));
                        if (added != 0) {
                            *(short *)(cell + 0x20) = *(short *)(cell + 0x20) + 1;
                        }
                    }
                }
            }
        }
        else if (state == '\x03') {
            // committed run: consume the run length (counters only)
            (void)runLen;
        }
        else if (state != '\x02') {
            // unlinked: resolve+link the companion for kind 5/6 records
            if (kind == 6) {
                int made = FUN_80313af4(rx, ry, (double)(float)(ez - (double)FLOAT_8061a614));
                roster_hit_test_point(ex, ey, (double)(float)(ez - (double)FLOAT_8061a614));
                if (cell != 0) {
                    u32 flags = aveditor_place_companion_segments();
                    if (FUN_80341fcc((int)rec[4], flags) != 0) {
                        FUN_80344528();
                    }
                }
                if ((made != 0) && (FUN_8034202c((int)rec[4]) != 0)) {
                    aveditor_link_picked_pair();
                }
                if (made != 0) {
                    int seg = aselgrid_find_companion_segment(
                        made, (int)rec[0], (int)rec[1],
                        (int)(rec[2] + (float)((double)CONCAT44(0x43300000, (u32)htB) - DOUBLE_8061a618)),
                        (u32)kind, (u32)runLen);
                    if (-1 < seg) {
                        FUN_80342968((int)rec);
                    }
                }
            }
            else if (kind < 6 && kind >= 5) {
                int made = FUN_80313af4(rx, ry, (double)(float)((double)FLOAT_8061a614 + ez));
                roster_hit_test_point(ex, ey, (double)(float)((double)FLOAT_8061a614 + ez));
                if ((made != 0) && (FUN_8034202c((int)rec[4]) != 0) &&
                    (rec[3] == *(float *)(made + 0xc))) {
                    int seg = aselgrid_find_companion_segment(
                        made, (int)rec[0], (int)rec[1],
                        (int)(rec[2] + (float)((double)CONCAT44(0x43300000, (u32)htB) - DOUBLE_8061a618)),
                        (u32)kind, (u32)runLen);
                    if ((-1 < seg) && (cell != 0)) {
                        int added = FUN_803437cc(rec, made,
                                                 (unsigned short *)(cell + (u32)*(unsigned short *)(cell + 0x20) * 8 + 0x28));
                        if (added != 0) {
                            *(short *)(cell + 0x20) = *(short *)(cell + 0x20) + 1;
                        }
                    }
                }
            }
        }
        i = i + 1;
        off = off + 6;
    }
    FUN_80382700();
}

// ---------------------------------------------------------------------------
// 0x8030efc0  aveditor_min_linked_index
// ---------------------------------------------------------------------------
u32 aveditor_min_linked_index(int param_1, int param_2)
{
    int best = param_1;
    int node = DAT_80612c14;
    if (param_1 != -1) {
        for (; node != 0; node = *(int *)(node + 0x40)) {
            for (int* e = *(int **)(node + 0x3c); e != (int *)0; e = (int *)e[5]) {
                int cand = best;
                if (param_2 == 7) {
                    if (((*e == 7) || (*e == 8)) && (best < e[6])) {
                        cand = best;   // keep min
                    }
                    else if ((*e == 7) || (*e == 8)) {
                        cand = e[6];
                    }
                }
                else if ((param_2 < 7) && (param_2 == 3)) {
                    int idx = e[6];
                    if (((*e == 5) || (*e == 6)) && (idx != -1)) {
                        cand = (best < idx) ? best : idx;
                    }
                }
                best = cand;
            }
        }
        return countLeadingZeros((u32)(param_1 - best)) >> 5;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8030f088  aveditor_special_placement_active
// ---------------------------------------------------------------------------
int aveditor_special_placement_active(void)
{
    if ((FUN_803394e0() != 0) || (FUN_8032a6b4() != 0)) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8030f0d0  aveditor_query_capability
// ---------------------------------------------------------------------------
u32 aveditor_query_capability(int param_1)
{
    int* blk = FUN_80339b90(DAT_80612be8, 0xc77668);
    u32 result;
    switch (param_1) {
    case 2:    result = blk[0x8e] & 2; break;
    case 3:    result = blk[0x8e] & 4; break;
    case 4:    result = blk[0x8e] & 8; break;
    case 5:    result = blk[0x8e] & 0x10; break;
    case 6:    result = blk[0x8e] & 0x20; break;
    case 7:    result = blk[0x8e] & 0x40; break;
    case 8:    result = blk[0x8e] & 0x80; break;
    case 9:    result = blk[0x8e] & 0x100; break;
    case 10:   result = blk[0x8e] & 0x200; break;
    case 0xc:  result = blk[0x8e] & 0x400; break;
    case 0xf:  result = blk[0x8e] & 0x800; break;
    case 0x10: result = blk[0x8e] & 0x1000; break;
    case 0x12: result = blk[0x8e] & 0x2000; break;
    case 0x13: result = blk[0x8e] & 0x4000; break;
    default:   result = 0; break;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8030f1f4  aveditor_tally_and_trigger
// ---------------------------------------------------------------------------
void aveditor_tally_and_trigger(void)
{
    int  spawnCount = 0;
    int  keyCount = 0;
    int  doorCount = 0;
    int  local_88[4];
    unsigned char auStack_78[28];
    u32  uStack_5c;
    unsigned char auStack_58[88];

    FUN_803826c8();
    int* budget = FUN_8031d710();
    memset_wrapper((int)auStack_58, 0, 0x20);
    memset_wrapper((int)auStack_78, 0, 0x20);
    for (int node = DAT_80612c14; node != 0; node = *(int *)(node + 0x40)) {
        for (int* e = *(int **)(node + 0x3c); e != (int *)0; e = (int *)e[5]) {
            int t = *e;
            if ((t != 8) && (t < 8)) {
                if (t == 5) {
                    if (e[6] != -1) {
                        (&uStack_5c)[e[6]] = 1;
                        keyCount = keyCount + 1;
                    }
                }
                else if (t < 5) {
                    if ((-1 < t) && (t == 0)) {
                        spawnCount = spawnCount + 1;
                    }
                }
                else if ((t < 7) && (e[6] != -1)) {
                    local_88[e[6] + 3] = 1;
                    doorCount = doorCount + 1;
                }
            }
        }
    }
    int off = 0;
    for (int i = 0; i < 4; i = i + 1) {
        FUN_803142f4();
        int v = FUN_803142f4();
        *(u32 *)((int)local_88 + off) = (u32)v;
        off = off + 4;
    }
    if (((0 < spawnCount) && (*(int *)(DAT_80612be4 + 0x200) != 0)) && (budget[1] == 0)) {
        aveditor_special_placement_active();
    }
    if (((keyCount != 0) && (doorCount != 0)) && (budget[0xc] == 0)) {
        FUN_80337758();
    }
    FUN_80328838();
    FUN_80382714();
    (void)auStack_58; (void)auStack_78;
}

// ---------------------------------------------------------------------------
// 0x8030f588  aveditor_sum_member_counts
// ---------------------------------------------------------------------------
int aveditor_sum_member_counts(void)
{
    int total = 0;
    for (int node = DAT_80612c14; node != 0; node = *(int *)(node + 0x40)) {
        total = total + FUN_8030f8f4();
    }
    return total;
}

// ---------------------------------------------------------------------------
// 0x8030f5dc  aveditor_sum_member_type_counts
// ---------------------------------------------------------------------------
int aveditor_sum_member_type_counts(void)
{
    int total = 0;
    for (int node = DAT_80612c14; node != 0; node = *(int *)(node + 0x40)) {
        total = total + FUN_80343c4c(*(int *)(node + 0x10));
    }
    return total;
}
