// Batch: batch_870 | Module: arcade-select map editor budget / paste / geometry
// Address range: 0x8030583c - 0x80306ce0
// Date: 2026-09-11
//
// The "budget & connectivity" pass of the arcade character-select / map editor
// screen, plus the clipboard paste-buffer lifecycle and screen enter/exit hooks.
// All of these operate on the active screen context (DAT_80612be4) and the live
// member linked list (DAT_80612c14, next @+0x40; each node's cell chain @+0x3c,
// chained via [+5]/[0x14]).
//
//   map_score_walk_full      0x8030583c  walk the whole member list, summing a
//                                        "cost" score: per-node base cost from the
//                                        entdesc table (&DAT_8047c048, 0x140 stride)
//                                        plus per-cell contributions, marking each
//                                        cell type seen in the dedup table
//                                        (&DAT_805d5bc8). Type 0x45 and types < 10
//                                        are special-cased.
//   map_score_current_entry  0x803058f0  the single-entry variant for the current
//                                        selection (ctx+0x2f34): dedup + cost for one
//                                        cell type, with the 0x32..0x36 / 0x45 special
//                                        cases returning fixed costs.
//   map_build_connectivity   0x8030598c  the big geometry/connectivity builder: copies
//                                        the piece transform matrix, transforms each
//                                        cell to world space, partitions cells into
//                                        two lists by entdesc flag bits, then flood-
//                                        fills connected regions (soundpad_flood_fill_
//                                        connected) to assign region ids.
//   map_count_high_types     0x80305fd8  count member cells whose type > 9.
//   map_update_budget        0x8030602c  compute normalized budget metrics (weighted
//                                        sums / counts vs per-category limits), find
//                                        the max, store the two headline figures at
//                                        ctx+0x270 / +0x274, and set/clear the
//                                        over-budget flag (0x400000) at ctx+0x25c.
//   paste_buffer_reserve     0x80306b28  size the paste/undo buffer from the current
//                                        member+cell counts and reserve it via
//                                        FUN_8030f830; set/clear the alloc-fail flag
//                                        (0x10000) at ctx+0x25c.
//   paste_buffer_commit      0x80306c04  drain every member's cell chain
//                                        (FUN_80314040) then commit the paste
//                                        (aselgrid_commit_paste).
//   paste_buffer_free        0x80306c5c  free the paste member chain (ctx+0x2f7c) and
//                                        null the head pointer.
//   arcade_edit_screen_enter 0x80306c90  screen-enter hook: push transitions, init
//                                        sub-widgets, optionally rebuild the empty
//                                        selection list.
//   arcade_edit_screen_exit  0x80306ce0  screen-exit hook: tear down widgets, navigate
//                                        away (hud_screen_navigate_to), reset the empty
//                                        selection list and input repeat timers.
//
// NON-MATCHING: the PPC int->float "magic bias" idiom
// ((double)CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE_8061a5e0) is preserved
// verbatim as the unsigned-int -> float conversion it encodes. map_score_walk_full /
// map_score_current_entry take the linked-list head as an explicit int parameter.
// Named cross-module helpers (mtx4_*, member_list_*, entdesc_flag_*, stream_*,
// soundpad_flood_fill_connected, hud_screen_navigate_to, select_list_init_empty,
// hud_input_reset_repeat_timers, memset_wrapper) resolve through the shared headers;
// every FUN_/DAT_/FLOAT_/DOUBLE_ raw symbol gets a per-file extern below.

#include "common.h"
#include "hud.h"

// Raw per-file symbol externs use C linkage. The batch's own function
// DEFINITIONS use C++ linkage (below, outside this block) to match the
// prototypes emitted into generated_decls.h.
extern "C" {

// --- PPC prologue/epilogue register-pair spill helpers ---
u64  __save_gpr_28(void);
void __restore_gpr_28(void);

// --- raw FUN_ helpers referenced from this module ---
u64  FUN_803826c0(void);                 // fetch active piece {base,index} reg pair
void FUN_8038270c(void);                 // screen-event finalizer (epilogue)
u32  FUN_803421a4(int a, int b);         // piece flag query A
u32  FUN_80341fcc(int a, int b);         // piece flag query B
int  FUN_803432f8(void);                 // base entity-id for the current screen
unsigned char FUN_8034202c(int a);       // piece has-paste-mode predicate
int  FUN_80342048(int a);                // paste-mode member count
int  FUN_80342bc8(int a, int* pKey, int outBuf); // resolve cell -> id list
int  FUN_80343c4c(int a);                // per-record aux size
int  FUN_8030f8f4(void);                 // per-record fixed size
int  FUN_8030f830(int size);             // reserve paste/undo buffer
int  FUN_8031424c(void);                 // rebuild grid placement tables
void FUN_80314040(int listHead, int cell);       // detach one cell from a member
void FUN_80313cf4(float* pMember);       // free a member chain
void FUN_8031cba4(int a, int b);         // screen transition push
void FUN_80340c58(void);                 // init sub-widget A
void FUN_80337e00(void);                 // init empty-context widget
void FUN_80345da8(int mode);             // screen mode set
void FUN_80340e78(void);                 // widget layout refresh
void FUN_80345c2c(int slot);             // release screen slot

// --- active screen context + globals ---
extern int DAT_80612be4;   // active screen context base
extern int DAT_80612c14;   // head of the current member linked list
extern int DAT_80612bd8;   // running placed-cell count
extern int DAT_80612bf4;   // parent screen id (navigate target)

// --- dedup / cost tables ---
extern int  DAT_8047c048;  // entdesc cost table base (0x140 stride)
extern char DAT_8047c15a;  // entdesc flags byte (0x140 stride)
extern int  DAT_805d5bc8;  // per-cell-type "seen" dedup table (4-byte stride)
extern char DAT_805375c8;  // flood-fill visited scratch (1-byte stride)
extern int  DAT_805d5dbc;  // empty-selection list anchor

// --- shared float/double conversion constants ---
extern double DOUBLE_8061a5e0; // magic-bias subtrahend (int->float)
extern float  FLOAT_8061a640;  // world scale X/Z
extern float  FLOAT_8061a644;  // world scale Y
extern float  FLOAT_8061a648;  // budget divisor: weight c038
extern float  FLOAT_8061a64c;  // budget scale: weight c03c
extern float  FLOAT_8061a650;  // budget divisor: weight c040
extern float  FLOAT_8061a654;  // budget divisor: member count
extern float  FLOAT_8061a658;  // budget divisor: placed-cell count
extern float  FLOAT_8061a65c;  // budget divisor: high-type count
extern float  FLOAT_8061a660;  // budget divisor: score

} // extern "C"

// ---------------------------------------------------------------------------
// 0x8030583c  map_score_walk_full
// Walk the whole member list, accumulating a cost score. Marks each cell type
// as "seen" in the dedup table; first sight of a type >= 10 (and != 0x45) adds a
// large cost, repeats of a seen type add a small cost.
// ---------------------------------------------------------------------------
int map_score_walk_full(int listHead)
{
    int  cellType;
    int* pCell;
    int  score;

    score = 0;
    if (listHead != 0) {
        for (; listHead != 0; listHead = *(int *)(listHead + 0x40)) {
            pCell = *(int **)(listHead + 0x3c);
            score = score + *(int *)(&DAT_8047c048 + **(int **)(listHead + 0x2c) * 0x140);
            for (; pCell != (int *)0x0; pCell = (int *)pCell[5]) {
                cellType = *pCell * 4;
                if ((*(int *)(&DAT_805d5bc8 + cellType) == 1) && (*pCell != 0x45)) {
                    score = score + 0x400;
                }
                else {
                    *(int *)(&DAT_805d5bc8 + cellType) = 1;
                    if ((*pCell != 0x45) && (9 < *pCell)) {
                        score = score + 0xa000;
                    }
                }
            }
        }
    }
    return score;
}

// ---------------------------------------------------------------------------
// 0x803058f0  map_score_current_entry
// The single-entry cost variant for the current selection (ctx+0x2f34).
// ---------------------------------------------------------------------------
u32 map_score_current_entry(void)
{
    int ctx;
    int cellType;

    ctx = DAT_80612be4;
    cellType = *(int *)(DAT_80612be4 + 0x2f34);
    if (cellType < 0) {
        return 0;
    }
    if ((*(int *)(&DAT_805d5bc8 + cellType * 4) == 1) && (cellType != 0x45)) {
        return 0x400;
    }
    *(int *)(&DAT_805d5bc8 + cellType * 4) = 1;
    cellType = *(int *)(ctx + 0x2f34);
    if ((cellType != 0x35) && (cellType != 0x36)) {
        if ((1 < (u32)(cellType - 0x32)) && (cellType != 0x34)) {
            return ~-(u32)(cellType == 0x45) & 0xa000;
        }
        return 0xfa;
    }
    return 300;
}

// ---------------------------------------------------------------------------
// 0x8030598c  map_build_connectivity
// Build the world-space cell geometry for the active piece and flood-fill the
// connected regions, assigning region ids into the per-cell record table.
// ---------------------------------------------------------------------------
void map_build_connectivity(void)
{
    int*      pCellRec;
    bool      clampFlag;
    int       nIds;
    char*     pVisited;
    int*      piece;
    int       baseId;
    u32       flagA;
    u32       flagB;
    int       modeCount;
    int       cellCount;
    int       edgeCount;
    unsigned char hasMode;
    u32*      pEdge;
    int*      pCellChain;
    u32*      pEdgeRec;
    int       stride;
    int       accB;
    int       cellTblBase;
    int       edgeTblBase;
    int       nextRegion;
    int       i;
    int       accA;
    int       tmp;
    int       inner;
    int       innerCnt;
    u64        pieceRegPair;
    int       local_1028;
    unsigned short local_1024;
    float     afStack_1020[16];
    int       local_fe0[32];
    int       local_f60[32];
    int       local_ee0[32];
    u32       local_e60[640];
    int       local_460[2];
    u32       local_458;
    float     local_454[253];
    u32       local_60;
    u32       uStack_5c;
    u32       local_58;
    u32       uStack_54;
    u32       local_50;
    u32       uStack_4c;

    pieceRegPair = FUN_803826c0();
    piece = (int *)((u64)pieceRegPair >> 0x20);
    baseId = *piece;
    accB = 0;
    accA = 0;
    edgeTblBase = 0;
    tmp = 0;
    flagA = FUN_803421a4(baseId, piece[2]);
    clampFlag = false;
    if ((flagA != 0) && (flagB = FUN_80341fcc(*piece, piece[2]), flagB != 0)) {
        clampFlag = true;
    }
    nextRegion = FUN_803432f8();
    inner = (int)(intptr_t)stream_find_pending_by_name((const char *)0);
    inner = *(int *)(inner + 0x10);
    innerCnt = 1;
    i = 0;
    modeCount = *(int *)(inner + 4);
    stride = inner + 0xc;
    edgeCount = *(int *)(inner + 8);
    cellTblBase = modeCount * 0x20;
    cellCount = modeCount;
    if (0 < modeCount) {
        do {
            nIds = *(short *)(stride + i + 8) + 1;
            if (innerCnt < nIds) {
                innerCnt = nIds;
            }
            i = i + 0x20;
            cellCount = cellCount + -1;
        } while (cellCount != 0);
    }
    hasMode = FUN_8034202c(*piece);
    if ((hasMode != 0) || (flagA != 0)) {
        hasMode = FUN_8034202c(*piece);
        if (hasMode != 0) {
            modeCount = FUN_80342048(piece[2]);
        }
        if (((&DAT_8047c15a)[baseId * 0x140] & 1) == 0) {
            mtx4_copy((float *)((u32)*(unsigned char *)((int)piece + 7) * 0x40 + -0x7fb846e8), afStack_1020);
        }
        else {
            mtx4_copy((float *)((*(char *)((int)piece + 7) + 4) * 0x40 + -0x7fb846e8), afStack_1020);
        }
        local_60 = 0x43300000;
        uStack_5c = (int)*(char *)(piece + 1) ^ 0x80000000;
        uStack_54 = (int)*(char *)((int)piece + 5) ^ 0x80000000;
        uStack_4c = (int)*(char *)((int)piece + 6) ^ 0x80000000;
        local_58 = 0x43300000;
        local_50 = 0x43300000;
        (void)local_60; (void)local_58; (void)local_50;
        mtx4_write_translation(
            (double)(FLOAT_8061a640 *
                     (float)((double)CONCAT44(0x43300000, uStack_5c) - DOUBLE_8061a5e0)),
            (double)(FLOAT_8061a644 *
                     (float)((double)CONCAT44(0x43300000, uStack_54) - DOUBLE_8061a5e0)),
            (double)(FLOAT_8061a640 *
                     (float)((double)CONCAT44(0x43300000, uStack_4c) - DOUBLE_8061a5e0)),
            (int)afStack_1020);
        for (pCellChain = *(int **)((int)pieceRegPair + 0x3c); pCellChain != (int *)0x0;
             pCellChain = (int *)pCellChain[5]) {
            local_1028 = pCellChain[3];
            local_1024 = *(unsigned short *)(pCellChain + 4);
            nIds = FUN_80342bc8((int)pieceRegPair, &local_1028, (int)local_fe0);
            if (nIds != 0) {
                hasMode = entdesc_flag_bit1_cur(*pCellChain);
                if (hasMode == 0) {
                    hasMode = entdesc_flag_bit0_cur(*pCellChain);
                    if (hasMode != 0) {
                        inner = 0;
                        if (0 < nIds) {
                            do {
                                pCellRec = (int *)((int)local_fe0 + inner);
                                inner = inner + 4;
                                accA = accA + 1;
                                *(int *)((int)local_f60 + tmp) = *pCellRec - (nextRegion + -0x3e9);
                                tmp = tmp + 4;
                                nIds = nIds + -1;
                            } while (nIds != 0);
                        }
                    }
                }
                else {
                    inner = 0;
                    if (0 < nIds) {
                        do {
                            pCellRec = (int *)((int)local_fe0 + inner);
                            inner = inner + 4;
                            accB = accB + 1;
                            *(int *)((int)local_ee0 + edgeTblBase) = *pCellRec - (nextRegion + -0x3e9);
                            edgeTblBase = edgeTblBase + 4;
                            nIds = nIds + -1;
                        } while (nIds != 0);
                    }
                }
            }
        }
        baseId = 0;
        for (edgeTblBase = 0; edgeTblBase < modeCount; edgeTblBase = edgeTblBase + 1) {
            pCellChain = (int *)((int)local_460 + baseId);
            tmp = stride + baseId;
            *pCellChain = edgeTblBase + 0x3e9;
            *(int *)((int)local_460 + baseId + 4) = *(int *)(tmp + 4);
            *(short *)((int)&local_458 + baseId) = *(short *)(tmp + 8);
            *(int *)((int)local_454 + baseId) = *(int *)(tmp + 0xc);
            *(int *)((int)local_454 + baseId + 4) = *(int *)(tmp + 0x10);
            *(int *)((int)local_454 + baseId + 8) = *(int *)(tmp + 0x14);
            if (clampFlag) {
                flagA = *(u32 *)((int)local_460 + baseId + 4);
                if ((flagA & 0x40) == 0) {
                    *(u32 *)((int)local_460 + baseId + 4) = flagA & 0xfffffffe;
                }
            }
            mtx4_transform_point(afStack_1020, (float *)((int)local_454 + baseId));
            inner = 0;
            tmp = accA;
            if (0 < accA) {
                do {
                    if (*(int *)((int)local_f60 + inner) == *pCellChain) {
                        *(u32 *)((int)local_460 + baseId + 4) =
                            *(u32 *)((int)local_460 + baseId + 4) & 0xfffffffe;
                    }
                    inner = inner + 4;
                    tmp = tmp + -1;
                } while (tmp != 0);
            }
            inner = 0;
            tmp = accB;
            if (0 < accB) {
                do {
                    if (*(int *)((int)local_ee0 + inner) == *pCellChain) {
                        *(short *)((int)&local_458 + baseId) = (short)innerCnt;
                        innerCnt = innerCnt + 1;
                    }
                    inner = inner + 4;
                    tmp = tmp + -1;
                } while (tmp != 0);
            }
            baseId = baseId + 0x20;
        }
        baseId = 0;
        for (edgeTblBase = 0; edgeTblBase < edgeCount; edgeTblBase = edgeTblBase + 1) {
            pEdgeRec = (u32 *)(stride + cellTblBase + baseId);
            pEdge = (u32 *)((int)local_e60 + baseId);
            nextRegion = 0;
            inner = 0;
            *pEdge = *pEdgeRec | 0x10000;
            *(int *)((int)local_e60 + baseId + 4) = -1;
            *(int *)((int)local_e60 + baseId + 8) = -1;
            tmp = modeCount;
            if (0 < modeCount) {
                do {
                    if (*(u32 *)(stride + inner) == pEdgeRec[1]) {
                        *(int *)((int)local_e60 + baseId + 4) = nextRegion;
                    }
                    else if (*(u32 *)(stride + inner) == pEdgeRec[2]) {
                        *(int *)((int)local_e60 + baseId + 8) = nextRegion;
                    }
                    nextRegion = nextRegion + 1;
                    inner = inner + 0x20;
                    tmp = tmp + -1;
                } while (tmp != 0);
            }
            tmp = *(int *)((int)local_e60 + baseId + 4);
            if ((tmp == -1) || (inner = *(int *)((int)local_e60 + baseId + 8), inner == -1)) {
                *pEdge = *pEdge & 0xfffeffff;
            }
            else if ((((&local_458)[tmp * 8 + -1] & 1) == 0) ||
                     (((&local_458)[inner * 8 + -1] & 1) == 0)) {
                *pEdge = *pEdge & 0xfffeffff;
            }
            hasMode = FUN_8034202c(*piece);
            if ((hasMode != 0) && ((*pEdge & 2) != 0)) {
                flagB = (u32)piece[2];
                flagA = flagB & 0x8000000;
                if ((flagA == 0) || ((flagB & 0x1000000) != 0)) {
                    if (((flagB & 0x1000000) == 0) || (flagA != 0)) {
                        if ((flagA != 0) &&
                            (((flagB & 0x1000000) != 0 &&
                              ((pEdgeRec[2] == 0x3ec || (pEdgeRec[1] == 0x3ec)))))) {
                            *pEdge = *pEdge & 0xfffeffff;
                        }
                    }
                    else {
                        *pEdge = *pEdge & 0xfffffffd;
                    }
                }
                else {
                    *pEdge = *pEdge & 0xfffeffff;
                }
            }
            baseId = baseId + 0x14;
        }
        if (0 < accB + accA) {
            for (baseId = 0; baseId < innerCnt; baseId = baseId + 1) {
                clampFlag = false;
                accB = 0;
                accA = 0;
                i = -1;
                tmp = modeCount;
                if (0 < modeCount) {
                    do {
                        if ((baseId == *(short *)((int)&local_458 + accA)) &&
                            (i = accB, (*(u32 *)((int)local_460 + accA + 4) & 1) != 0)) break;
                        accB = accB + 1;
                        accA = accA + 0x20;
                        tmp = tmp + -1;
                        i = -1;
                    } while (tmp != 0);
                }
                if (-1 < i) {
                    memset_wrapper(-0x7fac8a38, 0, 0x20);
                    soundpad_flood_fill_connected(i, 0, modeCount + -1, (int)local_460, 0,
                                                  edgeCount, (int)local_e60);
                    pVisited = &DAT_805375c8;
                    tmp = 0;
                    i = modeCount;
                    if (0 < modeCount) {
                        do {
                            if (((*pVisited == '\0') &&
                                 (baseId == *(short *)((int)&local_458 + tmp))) &&
                                ((*(u32 *)((int)local_460 + tmp + 4) & 1) != 0)) {
                                if (!clampFlag) {
                                    innerCnt = innerCnt + 1;
                                }
                                clampFlag = true;
                                *(short *)((int)&local_458 + tmp) = (short)innerCnt + -1;
                            }
                            tmp = tmp + 0x20;
                            pVisited = pVisited + 1;
                            i = i + -1;
                        } while (i != 0);
                    }
                }
            }
        }
    }
    FUN_8038270c();
}

// ---------------------------------------------------------------------------
// 0x80305fd8  map_count_high_types
// Count member cells whose cell type is > 9.
// ---------------------------------------------------------------------------
int map_count_high_types(int listHead)
{
    int* pCell;
    int  count;

    count = 0;
    if (listHead != 0) {
        for (; listHead != 0; listHead = *(int *)(listHead + 0x40)) {
            pCell = *(int **)(listHead + 0x3c);
            for (; pCell != (int *)0x0; pCell = (int *)pCell[5]) {
                if (9 < *pCell) {
                    count = count + 1;
                }
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// 0x8030602c  map_update_budget
// Compute the normalized budget metrics for the current selection and the whole
// map, store the two headline values at ctx+0x270 / +0x274, and set the
// over-budget flag (0x400000) at ctx+0x25c when any category limit is exceeded.
// ---------------------------------------------------------------------------
void map_update_budget(void)
{
    u32    w;
    int    highCount;
    u32    w2;
    u32    memberCount;
    int    sumA;
    u32    curType;
    double mScore;
    double mW038;
    double mW03c;
    double mW040;
    double mCount;
    double mPlaced;
    double vMax;
    double vTmp;
    double vTmp2;
    double vScore;

    __save_gpr_28();
    w = member_list_sum_weight_c038(DAT_80612c14);
    mW038 = (double)((float)((double)CONCAT44(0x43300000, w ^ 0x80000000) - DOUBLE_8061a5e0) /
                     FLOAT_8061a648);
    w = member_list_sum_weight_c03c(DAT_80612c14);
    mW03c = (double)((float)((double)CONCAT44(0x43300000, w ^ 0x80000000) - DOUBLE_8061a5e0) *
                     FLOAT_8061a64c);
    w = member_list_sum_weight_c040(DAT_80612c14);
    mW040 = (double)((float)((double)CONCAT44(0x43300000, w ^ 0x80000000) - DOUBLE_8061a5e0) /
                     FLOAT_8061a650);
    w = member_list_count(DAT_80612c14);
    mCount = (double)((float)((double)CONCAT44(0x43300000, w ^ 0x80000000) - DOUBLE_8061a5e0) /
                      FLOAT_8061a654);
    mPlaced = (double)((float)((double)CONCAT44(0x43300000, (u32)DAT_80612bd8 ^ 0x80000000) -
                               DOUBLE_8061a5e0) / FLOAT_8061a658);
    highCount = map_count_high_types(DAT_80612c14);
    mScore = (double)((float)((double)CONCAT44(0x43300000,
                                               (u32)((u32)*(unsigned short *)(DAT_80612be4 + 0x306e) +
                                                     highCount) ^ 0x80000000) - DOUBLE_8061a5e0) /
                      FLOAT_8061a65c);
    memset_wrapper(-0x7fa2a2f8, 0, 0xb4);
    memset_wrapper(-0x7fa2a438, 0, 0x140);
    w = map_score_walk_full(DAT_80612c14);
    vScore = (double)((float)((double)CONCAT44(0x43300000, w ^ 0x80000000) - DOUBLE_8061a5e0) /
                      FLOAT_8061a660);
    vMax = mW03c;
    if (mW03c < mW038) {
        vMax = mW038;
    }
    vTmp = mW040;
    if ((mW040 < vMax) && (vTmp = mW03c, mW03c < mW038)) {
        vTmp = mW038;
    }
    vMax = vScore;
    if (vScore < vTmp) {
        vTmp = mW03c;
        if (mW03c < mW038) {
            vTmp = mW038;
        }
        vMax = mW040;
        if ((mW040 < vTmp) && (vMax = mW03c, mW03c < mW038)) {
            vMax = mW038;
        }
    }
    vTmp = mCount;
    if (mCount < vMax) {
        vMax = mW03c;
        if (mW03c < mW038) {
            vMax = mW038;
        }
        vTmp2 = mW040;
        if ((mW040 < vMax) && (vTmp2 = mW03c, mW03c < mW038)) {
            vTmp2 = mW038;
        }
        vTmp = vScore;
        if (vScore < vTmp2) {
            vMax = mW03c;
            if (mW03c < mW038) {
                vMax = mW038;
            }
            vTmp = mW040;
            if ((mW040 < vMax) && (vTmp = mW03c, mW03c < mW038)) {
                vTmp = mW038;
            }
        }
    }
    vMax = mPlaced;
    if (mPlaced < vTmp) {
        vMax = mW03c;
        if (mW03c < mW038) {
            vMax = mW038;
        }
        vTmp = mW040;
        if ((mW040 < vMax) && (vTmp = mW03c, mW03c < mW038)) {
            vTmp = mW038;
        }
        vTmp2 = vScore;
        if (vScore < vTmp) {
            vMax = mW03c;
            if (mW03c < mW038) {
                vMax = mW038;
            }
            vTmp2 = mW040;
            if ((mW040 < vMax) && (vTmp2 = mW03c, mW03c < mW038)) {
                vTmp2 = mW038;
            }
        }
        vMax = mCount;
        if (mCount < vTmp2) {
            vMax = mW03c;
            if (mW03c < mW038) {
                vMax = mW038;
            }
            vTmp = mW040;
            if ((mW040 < vMax) && (vTmp = mW03c, mW03c < mW038)) {
                vTmp = mW038;
            }
            vMax = vScore;
            if (vScore < vTmp) {
                vTmp = mW03c;
                if (mW03c < mW038) {
                    vTmp = mW038;
                }
                vMax = mW040;
                if ((mW040 < vTmp) && (vMax = mW03c, mW03c < mW038)) {
                    vMax = mW038;
                }
            }
        }
    }
    if (mScore < vMax) {
        mScore = mW03c;
        if (mW03c < mW038) {
            mScore = mW038;
        }
        vMax = mW040;
        if ((mW040 < mScore) && (vMax = mW03c, mW03c < mW038)) {
            vMax = mW038;
        }
        mScore = vScore;
        if (vScore < vMax) {
            vMax = mW03c;
            if (mW03c < mW038) {
                vMax = mW038;
            }
            mScore = mW040;
            if ((mW040 < vMax) && (mScore = mW03c, mW03c < mW038)) {
                mScore = mW038;
            }
        }
        vMax = mCount;
        if (mCount < mScore) {
            mScore = mW03c;
            if (mW03c < mW038) {
                mScore = mW038;
            }
            vTmp2 = mW040;
            if ((mW040 < mScore) && (vTmp2 = mW03c, mW03c < mW038)) {
                vTmp2 = mW038;
            }
            vMax = vScore;
            if (vScore < vTmp2) {
                mScore = mW03c;
                if (mW03c < mW038) {
                    mScore = mW038;
                }
                vMax = mW040;
                if ((mW040 < mScore) && (vMax = mW03c, mW03c < mW038)) {
                    vMax = mW038;
                }
            }
        }
        mScore = mPlaced;
        if (mPlaced < vMax) {
            mScore = mW03c;
            if (mW03c < mW038) {
                mScore = mW038;
            }
            vMax = mW040;
            if ((mW040 < mScore) && (vMax = mW03c, mW03c < mW038)) {
                vMax = mW038;
            }
            mPlaced = vScore;
            if (vScore < vMax) {
                mScore = mW03c;
                if (mW03c < mW038) {
                    mScore = mW038;
                }
                mPlaced = mW040;
                if ((mW040 < mScore) && (mPlaced = mW03c, mW03c < mW038)) {
                    mPlaced = mW038;
                }
            }
            mScore = mCount;
            if (mCount < mPlaced) {
                mScore = mW03c;
                if (mW03c < mW038) {
                    mScore = mW038;
                }
                vMax = mW040;
                if ((mW040 < mScore) && (vMax = mW03c, mW03c < mW038)) {
                    vMax = mW038;
                }
                mScore = vScore;
                if (vScore < vMax) {
                    vMax = mW03c;
                    if (mW03c < mW038) {
                        vMax = mW038;
                    }
                    mScore = mW040;
                    if ((mW040 < vMax) && (mScore = mW03c, mW03c < mW038)) {
                        mScore = mW038;
                    }
                }
            }
        }
    }
    *(float *)(DAT_80612be4 + 0x270) = (float)mScore;

    // --- whole-map figures (second selection: ctx+0x2f7c) ---
    w2 = member_list_sum_weight_c038(*(int *)(DAT_80612be4 + 0x2f7c));
    mW040 = (double)((float)((double)CONCAT44(0x43300000, w2 ^ 0x80000000) - DOUBLE_8061a5e0) /
                     FLOAT_8061a648);
    w2 = member_list_sum_weight_c03c(*(int *)(DAT_80612be4 + 0x2f7c));
    mW03c = (double)((float)((double)CONCAT44(0x43300000, w2 ^ 0x80000000) - DOUBLE_8061a5e0) *
                     FLOAT_8061a64c);
    w2 = member_list_sum_weight_c040(*(int *)(DAT_80612be4 + 0x2f7c));
    mW038 = (double)((float)((double)CONCAT44(0x43300000, w2 ^ 0x80000000) - DOUBLE_8061a5e0) /
                     FLOAT_8061a650);
    memberCount = member_list_count(*(int *)(DAT_80612be4 + 0x2f7c));
    w2 = *(u32 *)(DAT_80612be4 + 0x2f34);
    mCount = (double)((float)((double)CONCAT44(0x43300000, memberCount ^ 0x80000000) -
                             DOUBLE_8061a5e0) / FLOAT_8061a654);
    highCount = map_count_high_types(*(int *)(DAT_80612be4 + 0x2f7c));
    mPlaced = (double)((float)((double)CONCAT44(0x43300000,
                                                (u32)((w2 >> 0x1f ^ 1) + highCount) ^ 0x80000000) -
                              DOUBLE_8061a5e0) / FLOAT_8061a65c);
    w2 = map_score_current_entry();
    highCount = map_score_walk_full(*(int *)(DAT_80612be4 + 0x2f7c));
    w2 = highCount + w2;
    vScore = (double)((float)((double)CONCAT44(0x43300000, w2 ^ 0x80000000) - DOUBLE_8061a5e0) /
                      FLOAT_8061a660);
    vMax = mW03c;
    if (mW03c < mW040) {
        vMax = mW040;
    }
    vTmp = mW038;
    if ((mW038 < vMax) && (vTmp = mW03c, mW03c < mW040)) {
        vTmp = mW040;
    }
    vMax = vScore;
    if (vScore < vTmp) {
        vTmp = mW03c;
        if (mW03c < mW040) {
            vTmp = mW040;
        }
        vMax = mW038;
        if ((mW038 < vTmp) && (vMax = mW03c, mW03c < mW040)) {
            vMax = mW040;
        }
    }
    vTmp = mCount;
    if (mCount < vMax) {
        vMax = mW03c;
        if (mW03c < mW040) {
            vMax = mW040;
        }
        vTmp2 = mW038;
        if ((mW038 < vMax) && (vTmp2 = mW03c, mW03c < mW040)) {
            vTmp2 = mW040;
        }
        vTmp = vScore;
        if (vScore < vTmp2) {
            vMax = mW03c;
            if (mW03c < mW040) {
                vMax = mW040;
            }
            vTmp = mW038;
            if ((mW038 < vMax) && (vTmp = mW03c, mW03c < mW040)) {
                vTmp = mW040;
            }
        }
    }
    if (mPlaced < vTmp) {
        vMax = mW03c;
        if (mW03c < mW040) {
            vMax = mW040;
        }
        mPlaced = mW038;
        if ((mW038 < vMax) && (mPlaced = mW03c, mW03c < mW040)) {
            mPlaced = mW040;
        }
        vMax = vScore;
        if (vScore < mPlaced) {
            mPlaced = mW03c;
            if (mW03c < mW040) {
                mPlaced = mW040;
            }
            vMax = mW038;
            if ((mW038 < mPlaced) && (vMax = mW03c, mW03c < mW040)) {
                vMax = mW040;
            }
        }
        mPlaced = mCount;
        if (mCount < vMax) {
            vMax = mW03c;
            if (mW03c < mW040) {
                vMax = mW040;
            }
            mCount = mW038;
            if ((mW038 < vMax) && (mCount = mW03c, mW03c < mW040)) {
                mCount = mW040;
            }
            mPlaced = vScore;
            if (vScore < mCount) {
                vMax = mW03c;
                if (mW03c < mW040) {
                    vMax = mW040;
                }
                mPlaced = mW038;
                if ((mW038 < vMax) && (mPlaced = mW03c, mW03c < mW040)) {
                    mPlaced = mW040;
                }
            }
        }
    }
    *(float *)(DAT_80612be4 + 0x274) = (float)mPlaced;

    // --- over-budget test ---
    sumA = member_list_sum_weight_c038(*(int *)(DAT_80612be4 + 0x2f7c));
    highCount = member_list_sum_weight_c038(DAT_80612c14);
    if (highCount + sumA < 0x961) {
        sumA = member_list_sum_weight_c03c(*(int *)(DAT_80612be4 + 0x2f7c));
        highCount = member_list_sum_weight_c03c(DAT_80612c14);
        if (highCount + sumA < 0x401) {
            sumA = member_list_sum_weight_c040(*(int *)(DAT_80612be4 + 0x2f7c));
            highCount = member_list_sum_weight_c040(DAT_80612c14);
            if (highCount + sumA < 0x1131) {
                sumA = member_list_count(*(int *)(DAT_80612be4 + 0x2f7c));
                highCount = member_list_count(DAT_80612c14);
                if (((highCount + sumA < 0xc9) && ((int)(w + w2) < 0x1f4001)) &&
                    ((int)DAT_80612bd8 < 0x12d)) {
                    curType = *(u32 *)(DAT_80612be4 + 0x2f34);
                    memberCount = curType ^ 9;
                    sumA = map_count_high_types(*(int *)(DAT_80612be4 + 0x2f7c));
                    highCount = map_count_high_types(DAT_80612c14);
                    if ((int)(highCount +
                              (sumA - ((int)(((int)memberCount >> 1) - (memberCount & curType)) >> 0x1f)) +
                              (u32)*(unsigned short *)(DAT_80612be4 + 0x306e)) < 0x33) {
                        *(u32 *)(DAT_80612be4 + 0x25c) =
                            *(u32 *)(DAT_80612be4 + 0x25c) & 0xffbfffff;
                        goto LAB_over_budget_done;
                    }
                }
            }
        }
    }
    *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) | 0x400000;
LAB_over_budget_done:
    *(u32 *)(DAT_80612be4 + 0x278) = w + w2;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80306b28  paste_buffer_reserve
// Size the paste/undo buffer from the current member+cell counts and reserve it.
// ---------------------------------------------------------------------------
void paste_buffer_reserve(void)
{
    int member;
    int cell;
    int auxSize;
    u32 ok;
    int fixedSize;
    int cellBytes;

    __save_gpr_28();
    cellBytes = 0;
    fixedSize = 0;
    FUN_8031424c();
    member = *(int *)(DAT_80612be4 + 0x2f7c);
    if (member == 0) {
        *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) & 0xfffeffff;
    }
    else {
        for (; member != 0; member = *(int *)(member + 0x40)) {
            for (cell = *(int *)(member + 0x3c); cell != 0; cell = *(int *)(cell + 0x14)) {
                cellBytes = cellBytes + 0x1c;
            }
            auxSize = FUN_80343c4c(*(int *)(member + 0x10));
            cell = FUN_8030f8f4();
            fixedSize = (cell + auxSize) * 8 + fixedSize + 0x28;
        }
        ok = FUN_8030f830(cellBytes + fixedSize);
        if (ok == 0) {
            *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) | 0x10000;
        }
        else {
            *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) & 0xfffeffff;
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80306c04  paste_buffer_commit
// Drain each member's cell chain then commit the paste.
// ---------------------------------------------------------------------------
void paste_buffer_commit(void)
{
    int member;

    for (member = *(int *)(DAT_80612be4 + 0x2f7c); member != 0;
         member = *(int *)(member + 0x40)) {
        while (*(int *)(member + 0x3c) != 0) {
            FUN_80314040(member, *(int *)(member + 0x3c));
        }
    }
    aselgrid_commit_paste();
}

// ---------------------------------------------------------------------------
// 0x80306c5c  paste_buffer_free
// Free the paste member chain (ctx+0x2f7c) and null the head pointer.
// ---------------------------------------------------------------------------
void paste_buffer_free(void)
{
    FUN_80313cf4(*(float **)(DAT_80612be4 + 0x2f7c));
    *(int *)(DAT_80612be4 + 0x2f7c) = 0;
}

// ---------------------------------------------------------------------------
// 0x80306c90  arcade_edit_screen_enter
// Screen-enter hook: push transitions, init sub-widgets, rebuild empty context.
// ---------------------------------------------------------------------------
void arcade_edit_screen_enter(void)
{
    FUN_8031cba4(1, 2);
    FUN_80340c58();
    if (*(int *)(DAT_80612be4 + 0x1185c) == -1) {
        FUN_80337e00();
    }
    FUN_80345da8(4);
    FUN_80340e78();
}

// ---------------------------------------------------------------------------
// 0x80306ce0  arcade_edit_screen_exit
// Screen-exit hook: tear down widgets, navigate away, reset selection + input.
// ---------------------------------------------------------------------------
void arcade_edit_screen_exit(void)
{
    FUN_80345da8(5);
    if (*(int *)(DAT_80612be4 + 0x200) == 0) {
        FUN_80345c2c(4);
    }
    FUN_80345c2c(2);
    FUN_80345c2c(3);
    FUN_8031cba4(2, -1);
    FUN_8031cba4(-1, 1);
    FUN_80340e78();
    hud_screen_navigate_to(DAT_80612bf4, 0, -1);
    if (*(int *)(DAT_80612be4 + 0x1185c) == -1) {
        select_list_init_empty((u32 *)&DAT_805d5dbc, 0);
    }
    hud_input_reset_repeat_timers();
}
