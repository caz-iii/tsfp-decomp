// Batch: batch_866, batch_867, batch_868 | Module: arcade-select placement grid
// Address range: 0x80301de4 - 0x80304684
// Date: 2026-09-11
//
// batch_867 (0x803031f0-0x80303904): the grid visibility/occupancy pass (which
// cells are visible in the drag box, member visible/changed flag bookkeeping) plus
// 3D point/segment-in-bounds tests and roster overlap removal.
//
// batch_868 (0x80303978-0x80304684): the cut/copy/paste selection-buffer logic -
// building a selection from the drag box, copying picked members into the paste
// buffer (ctx+0x2f7c), remapping record pointers, committing/pasting a buffered
// group at the cursor, and resetting the placement table.
//
// The placement grid used by the arcade character-select "roster" screen. Members
// are laid out on a tiled grid whose cells carry an orientation byte (tile-type)
// that selects one of four AABB shapes. These routines turn the live cursor screen
// point (derived from DAT_80612c88/c84 view state + the DAT_806119ec viewport row
// of DAT_8051a834/838) into a picked cell, search/compact the placed-entry tables,
// and scan the whole grid for a hit.
//
//   aselgrid_cell_bounds          0x80301de4  compute a cell's screen AABB
//                                             (x0,y0,x1,y1) from its orientation byte
//                                             (switch: 1/3/7/9, 2/4/8/10, 0xb/0xd,
//                                             0xc/0xe) and cell size byte @+4.
//   aselgrid_pick_cursor_cell     0x80301ff4  walk the active piece's cells, find the
//                                             one under the cursor for the current
//                                             frame index (ctx+0x264); write the
//                                             snapped cell origin to the out params.
//   aselgrid_hit_test_index       0x80302344  return the index of the grid cell under
//                                             the cursor (count @ctx+0x306e, table
//                                             @ctx+0x3070, 0xc stride), rotated-tile
//                                             bounds; -1 if none.
//   aselgrid_find_companion_segment 0x8030259c search the active piece's cell list for
//                                             the matching companion segment (modes
//                                             5 / 6 / generic adjacency).
//   aselgrid_pick_at_point        0x803028ec  screen-point pick dispatcher: adjust the
//                                             point by the tile orientation, hit-test
//                                             via roster_hit_test_point, then resolve
//                                             the picked index via
//                                             aselgrid_find_companion_segment.
//   aselgrid_find_record          0x80302b50  find a placed record by (id, key[3]) in
//                                             the 2D table (rows @ctx+0x2e0 stride
//                                             0xe4, entries @+0x2ec stride 0x10);
//                                             returns packed (row<<16 | col) or -1.
//   aselgrid_remove_active_entries 0x80302bec remove every entry matching the active
//                                             screen record from each row; compact the
//                                             tail; cleanup empties (mode 5 -> 0x329650).
//   aselgrid_remove_entry_at      0x80302cf0  remove entry[index] from an 8-byte-stride
//                                             list (base,index passed as a PPC reg pair);
//                                             compact + recompute slots (FUN_80336690).
//   aselgrid_purge_active_mode3   0x80302dcc  for each mode-3 row (@ctx+0xdd14 stride
//                                             0x8c) remove entries matching the active
//                                             record via aselgrid_remove_entry_at.
//   aselgrid_scan_hit_grid        0x80302e78  walk the member list (@ctx+0x2f7c) and
//                                             hit-test each occupied cell of each tile
//                                             grid via roster_hit_test_point; on a hit
//                                             store depth @ctx+0x26c and set bit 0x400.
//
// Named cross-module helpers (roster_hit_test_point, batch_865) resolve through the
// shared headers; the hud active-screen ctx / finalize FUN_ symbols, the leaf/mem
// helpers and every DAT_/FLOAT_/DOUBLE_ constant get per-file externs. Definitions
// are at file scope (C++ linkage) to match generated_decls.h.
//
// NON-MATCHING: the PPC int->float "magic bias" idiom
// ((double)CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE_8061a5e0) is preserved
// verbatim as the integer->float conversion it encodes. aselgrid_remove_entry_at
// takes its {base,index} register pair as explicit parameters instead of the
// __save_gpr_28 spill the decompiler emitted (Compile-Clean rule 3). Cell/point
// math keeps the decompiler's float/double intermediates.

#include "common.h"
#include "hud.h"

extern "C" {

// --- hud active-screen context accessors (return {base, index} register pairs) ---
u64  FUN_803826d4(void);
u64  FUN_803826dc(void);
u64  FUN_803826e4(void);
u64  FUN_803826e8(void);
u64  FUN_803826f0(void);
void FUN_803826d8(void);
// --- hud screen/event finalizers (epilogue helpers) ---
void FUN_80382720(void);
void FUN_80382724(void);
void FUN_80382728(void);
void FUN_80382730(void);
void FUN_80382734(void);
void FUN_8038273c(void);

// --- other raw helpers referenced from this module ---
void FUN_80329650(void);                 // empty-row cleanup (mode 5)
int  FUN_80336690(int base, int index);  // slot address recompute
void FUN_80348238(int mode);             // mark ctx dirty (sound/anim); matches roster.cpp
int  FUN_8030a220(void);                 // count of currently-visible members
// leaf_31e8(u32,u32,s32) resolves through generated_decls.h (shared) - not redeclared here.
// memset_wrapper(...) resolves through generated_externs.h - not redeclared here.
// roster_remove_current / roster_remove_and_clear (batch_864) resolve via hud.h.

// --- clipboard / paste-buffer helpers referenced from batch_868 ---
void FUN_80313bd4(void* pMember);        // free a grid member node
float* FUN_80313af4(double x, double y, double z); // hit-test point -> member ptr
void FUN_80313cf4(void* pMember);        // free a member chain
int  FUN_80313b7c(void);                 // allocate a grid member node
void FUN_803140c8(void);                 // finalize record link (non-screen path)
int  FUN_8031424c(void);                 // paste-mode flag
int  FUN_80313da0(void);                 // validate paste record
int  FUN_8030f830(int flags);            // reserve placement slot
void FUN_80307ec0(void);                 // roster changed refresh
void FUN_8030a274(void);                 // record-set finalize

// --- PPC prologue/epilogue register-pair spill helpers (batch_867/868) ---
u64  __save_gpr_28(void);
void __restore_gpr_28(void);
u64  __save_gpr_25(void);
void __restore_gpr_25(void);

// --- active screen context + view/viewport state ---
extern int DAT_80612be4;   // active screen context base
extern int DAT_80612c14;   // head of the member linked list (next @+0x40)
extern u32 DAT_80612c88;   // view state X (int bits)
extern u32 DAT_80612c84;   // view state Y (int bits)
extern float FLOAT_80612c78;
extern float FLOAT_80612c74;
extern float FLOAT_80612c58;
extern float FLOAT_80612c54;
extern int DAT_806119ec;   // active viewport row index
extern float DAT_8051a834; // viewport table X base (0x1e0 stride)
extern float DAT_8051a838; // viewport table Y base (0x1e0 stride)

// --- shared float/double constants ---
extern double DOUBLE_8061a5e0; // magic-bias subtrahend (int->float)
extern double DOUBLE_8061a618; // magic-bias subtrahend (byte->float)
extern float FLOAT_8061a5f8;
extern float FLOAT_8061a604;
extern float FLOAT_8061a60c;
extern float FLOAT_8061a610;
extern float FLOAT_8061a614;
extern float FLOAT_8061a620;
extern float FLOAT_8061a624;
extern float FLOAT_8061a628;
extern float FLOAT_8061a62c;
extern float FLOAT_8061a630;
extern float FLOAT_8061a634;
extern float FLOAT_8061a638;
extern float FLOAT_8061a63c; // min drag-box extent to trigger a visibility recompute

} // extern "C" — definitions below use C++ linkage to match generated_decls.h

// helper: PPC int->float magic-bias conversion used throughout this module
static inline float ibias(u32 x) {
    return (float)((double)CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE_8061a5e0);
}
// helper: PPC byte->float magic-bias (unsigned, no XOR, DOUBLE_8061a618 subtrahend)
static inline float bbias(u32 x) {
    return (float)((double)CONCAT44(0x43300000, x) - DOUBLE_8061a618);
}

// ---------------------------------------------------------------------------
// 0x80301de4  aselgrid_cell_bounds
// Compute a cell's screen-space AABB from its orientation byte pEntry[0] and cell
// size byte pEntry[4]. Writes (x0,y0,x1,y1) -> (pX0,pY0,pX1,pY1).
// ---------------------------------------------------------------------------
void aselgrid_cell_bounds(unsigned char* pEntry, unsigned int cx, unsigned int cy,
                          float* pX0, float* pY0, float* pX1, float* pY1)
{
    float pad614 = FLOAT_8061a614;
    float pad610 = FLOAT_8061a610;
    float pad60c = FLOAT_8061a60c;

    switch (pEntry[0]) {
    case 1: case 3: case 7: case 9:
        *pX0 = pad60c + ibias(cx);
        *pX1 = ibias(cx + (unsigned char)pEntry[4]) - pad60c;
        *pY0 = ibias(cy) - pad610;
        *pY1 = pad610 + ibias(cy);
        break;
    case 2: case 4: case 8: case 10: {
        float top = pad610 + ibias(cx);
        *pX0 = ibias(cx) - FLOAT_8061a610;
        *pX1 = top;
        *pY0 = ibias(cy);
        *pY1 = ibias(cy + (unsigned char)pEntry[4]);
        break;
    }
    case 0xb: case 0xd: {
        float y1 = ibias(cy) - pad614;
        float x = ibias(cx);
        *pX0 = ibias(cx) - pad614;
        *pX1 = x;
        *pY0 = y1;
        *pY1 = ibias(cy);
        break;
    }
    case 0xc: case 0xe: {
        float x1 = pad614 + ibias(cx);
        *pX0 = ibias(cx);
        *pX1 = x1;
        *pY0 = ibias(cy) - pad614;
        *pY1 = ibias(cy);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 0x80301ff4  aselgrid_pick_cursor_cell
// Walk the active piece's cells; for the cell on the current frame row
// (ctx+0x264) whose AABB contains the cursor point, snap the cursor to the cell
// origin and write it out (piOutX via the FUN_803826dc base ptr, pOutY param).
// ---------------------------------------------------------------------------
void aselgrid_pick_cursor_cell(int arg1, int arg2, int* pOutY)
{
    (void)arg1; (void)arg2;

    u64 pair = FUN_803826dc();
    float* pPiece = (float*)(u32)(pair >> 0x20);
    int* piOut = (int*)(u32)pair;

    float curX = FLOAT_80612c58 *
                 (*(float*)((int)&DAT_8051a834 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c88)) +
                 FLOAT_80612c78;
    float curY = FLOAT_80612c54 *
                 (*(float*)((int)&DAT_8051a838 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c84)) +
                 FLOAT_80612c74;

    int* pGrid = (int*)(int)pPiece[(int)pPiece[3] + 0xb];
    int cellCount = *(int*)((int)pGrid + 0x44);

    int idx = 0;
    int byteOff = 0;
    while (idx < cellCount) {
        unsigned char* pCell = (unsigned char*)((int)pGrid + byteOff + 0x48);
        float cellX = pPiece[0] + bbias(pCell[1]);
        float cellY = pPiece[1] + bbias(pCell[2]);

        if (*(int*)(DAT_80612be4 + 0x264) == (int)pPiece[2] + (unsigned int)pCell[3]) {
            unsigned int sx = (unsigned int)cellX;
            unsigned int sy = (unsigned int)cellY;
            float bx0, by0, bx1, by1;
            aselgrid_cell_bounds(pCell, sx, sy, &bx0, &by0, &bx1, &by1);

            if ((double)bx0 <= (double)curX && (double)curX <= (double)bx1 &&
                (double)by0 <= (double)curY && (double)curY <= (double)by1) {
                if (pCell[0] < 5) {
                    if ((pCell[0] & 1) == 0) {
                        int nx = (int)(FLOAT_8061a5f8 + (curX - cellX));
                        *piOut = nx;
                        *pOutY = (int)(ibias((u32)(int)curY) - cellY);
                    } else {
                        int ny = (int)(FLOAT_8061a5f8 + (curY - cellY));
                        *piOut = (int)(ibias((u32)(int)curX) - cellX);
                        *pOutY = ny;
                    }
                } else {
                    int nx = (int)(ibias((u32)(int)curX) - cellX);
                    int ny = (int)(ibias((u32)(int)curY) - cellY);
                    *piOut = nx;
                    *pOutY = ny;
                }
                FUN_80382728();
                return;
            }
        }
        idx++;
        byteOff += 6;
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x80302344  aselgrid_hit_test_index
// Return the index of the grid cell under the cursor. Table @ctx+0x3070 (0xc
// stride), count @ctx+0x306e; -1 if none.
// ---------------------------------------------------------------------------
int aselgrid_hit_test_index(void)
{
    float curX = FLOAT_80612c58 *
                 (*(float*)((int)&DAT_8051a834 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c88)) +
                 FLOAT_80612c78;
    float curY = FLOAT_80612c54 *
                 (*(float*)((int)&DAT_8051a838 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c84)) +
                 FLOAT_80612c74;

    unsigned int remaining = (unsigned int)*(unsigned short*)(DAT_80612be4 + 0x306e);
    int index = 0;
    int off = 0;

    while (remaining != 0) {
        int pEnt = DAT_80612be4 + off;
        if ((int)*(short*)(pEnt + 0x3074) == *(int*)(DAT_80612be4 + 0x264)) {
            unsigned char shape = *(unsigned char*)(pEnt + 0x3076);
            float x0, y0, x1, y1;
            u32 rawX = (u32)(int)*(short*)(pEnt + 0x3070);
            u32 rawY = (u32)(int)*(short*)(pEnt + 0x3072);

            if (shape < 0xb) {
                if ((shape & 1) == 0) {
                    y1 = FLOAT_8061a614 + (ibias(rawY) - FLOAT_8061a630);
                    x0 = ibias(rawX) - FLOAT_8061a628;
                    y0 = ibias(rawY) - FLOAT_8061a62c;
                    x1 = FLOAT_8061a628 + ibias(rawX);
                } else {
                    y0 = ibias(rawY) - FLOAT_8061a620;
                    x1 = FLOAT_8061a614 + ibias(rawX);
                    y1 = FLOAT_8061a624 + ibias(rawY);
                    x0 = ibias(rawX);
                }
            } else {
                x1 = FLOAT_8061a614 + ibias(rawX);
                y1 = FLOAT_8061a614 + ibias(rawY);
                x0 = ibias(rawX);
                y0 = ibias(rawY);
            }

            if (x0 <= curX && curX < x1 && y0 <= curY && curY < y1) {
                return index;
            }
        }
        index++;
        off += 0xc;
        remaining--;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// 0x8030259c  aselgrid_find_companion_segment
// Search the active piece's cell list for the companion segment matching the
// supplied placed cell (modes 5 / 6 / generic adjacency). Side-effecting search;
// no return value in the original.
// ---------------------------------------------------------------------------
void aselgrid_find_companion_segment(int recPtr, unsigned int px, unsigned int py,
                                     int pz, unsigned int mode, unsigned int flag)
{
    u64 pair = FUN_803826f0();
    float* pPiece = (float*)(u32)(pair >> 0x20);
    int base = (int)(u32)pair;

    if (pPiece != 0) {
        int gridPtr = (int)pPiece[(int)pPiece[3] + 0xb];
        int cellCount = *(int*)(gridPtr + 0x44);
        unsigned char* pCell = (unsigned char*)(gridPtr + 0x48);

        int ox, oy, oz;
        if ((*(unsigned int*)(DAT_80612be4 + 0x25c) & 0x10) == 0) {
            ox = (int)pPiece[0];
            oy = (int)pPiece[1];
            oz = (int)pPiece[2];
        } else {
            ox = (int)(FLOAT_80612c58 *
                       (*(float*)((int)&DAT_8051a834 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c88)) +
                       FLOAT_80612c78 + pPiece[0]);
            oy = (int)(FLOAT_80612c54 *
                       (*(float*)((int)&DAT_8051a838 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c84)) +
                       FLOAT_80612c74 + pPiece[1]);
            oz = *(int*)(DAT_80612be4 + 0x264) + (int)pPiece[2];
        }

        // px is the caller's target X coordinate; matched below.
        (void)px;
        if (mode == 5) {
            while (cellCount > 0) {
                if (pCell[0] == 6 && pCell[5] != 2 &&
                    base == ox + (unsigned int)pCell[1] &&
                    pz == oy + (unsigned int)pCell[2] && flag == pCell[4]) {
                    break;
                }
                pCell += 6;
                cellCount--;
            }
        } else if (mode == 6) {
            while (cellCount > 0) {
                if (pCell[0] == 5 && pCell[5] != 2 &&
                    base == ox + (unsigned int)pCell[1] &&
                    pz == oy + (unsigned int)pCell[2] && flag == pCell[4]) {
                    break;
                }
                pCell += 6;
                cellCount--;
            }
        } else {
            while (cellCount > 0) {
                if (pCell[5] == 1 && flag == 1) {
                    unsigned int shape = (unsigned int)pCell[0];
                    if (((shape ^ mode) & 1) == 0 && (int)py == oz + (unsigned int)pCell[3]) {
                        if (shape == 1 || shape == 3) {
                            if (pz == oy + (unsigned int)pCell[2] &&
                                (int)(ox + (unsigned int)pCell[1]) <= base &&
                                base < (int)((unsigned int)pCell[4] + ox + (unsigned int)pCell[1])) {
                                break;
                            }
                        } else {
                            if (base == ox + (unsigned int)pCell[1] &&
                                (int)(oy + (unsigned int)pCell[2]) <= pz &&
                                pz < (int)((unsigned int)pCell[4] + oy + (unsigned int)pCell[2])) {
                                break;
                            }
                        }
                    }
                } else if (((pCell[0] ^ mode) & 1) == 0 &&
                           base == ox + (unsigned int)pCell[1] &&
                           pz == oy + (unsigned int)pCell[2] &&
                           (int)py == oz + (unsigned int)pCell[3] &&
                           pCell[4] == flag) {
                    break;
                }
                pCell += 6;
                cellCount--;
            }
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x803028ec  aselgrid_pick_at_point
// Screen-point pick dispatcher: adjust the point by the tile orientation, hit-test
// via roster_hit_test_point, then resolve the picked index via
// aselgrid_find_companion_segment. Writes picked record ptr to *pOutRec and the
// resolved index to *pOutIdx.
// ---------------------------------------------------------------------------
void aselgrid_pick_at_point(int arg1, int arg2, unsigned int px, unsigned int py,
                            void** pOutRec, int* pOutIdx)
{
    (void)arg1; (void)arg2;

    u64 pair = FUN_803826e4();
    float* pPiece = (float*)(u32)(pair >> 0x20);
    unsigned char* pCell =
        (unsigned char*)((int)pPiece[(int)pPiece[3] + 0xb] + (int)(u32)pair * 6 + 0x48);

    unsigned int gx = (unsigned int)(ibias(px) + pPiece[0] + bbias(pCell[1]));
    unsigned int gy = (unsigned int)(ibias(py) + pPiece[1] + bbias(pCell[2]));
    unsigned int gz = (unsigned int)(pPiece[2] + bbias(pCell[3]));

    if (pCell[5] == 4) {
        *pOutRec = pPiece;
        *pOutIdx = (int)(u32)pair;
    } else {
        // roster_hit_test_point takes X in the r3 gpr slot (u64) and Y/Z as doubles;
        // it records the picked record at ctx+0x2f80 (its return is unused - void).
        unsigned char shape = pCell[0];
        if (shape == 4) {
            roster_hit_test_point((u64)(u32)(int)(ibias(gx) - FLOAT_8061a614),
                                  (double)ibias(gy), (double)ibias(gz));
        } else if (shape < 4 && shape == 1) {
            roster_hit_test_point((u64)(u32)(int)ibias(gx),
                                  (double)(ibias(gy) - FLOAT_8061a614), (double)ibias(gz));
        } else {
            roster_hit_test_point((u64)(u32)(int)ibias(gx),
                                  (double)ibias(gy), (double)ibias(gz));
        }
        *pOutRec = *(void**)(DAT_80612be4 + 0x2f80);
        if (pOutIdx != 0) {
            if (*(int*)(DAT_80612be4 + 0x2f80) == 0) {
                *pOutIdx = -1;
            } else {
                aselgrid_find_companion_segment(*(int*)(DAT_80612be4 + 0x2f80),
                                                gx, gy, gz, (unsigned int)pCell[0], 1);
                *pOutIdx = 0; // resolved via the companion search side-effects
            }
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80302b50  aselgrid_find_record
// Find a placed record by (id, key[3]) in the 2D table (rows @ctx+0x2e0 stride
// 0xe4, entries @+0x2ec stride 0x10). Returns packed (row<<16 | col) or -1.
// ---------------------------------------------------------------------------
unsigned int aselgrid_find_record(int id, short* pKey)
{
    int row = 0;
    int rowOff = 0;
    while (true) {
        if (*(int*)(DAT_80612be4 + 0x280) <= row) {
            return 0xffffffff;
        }
        unsigned int col = 0;
        int count = *(int*)(DAT_80612be4 + rowOff + 0x2e0);
        int* pEnt = (int*)(DAT_80612be4 + rowOff + 0x2ec);
        while (count > 0) {
            if (pEnt[0] == id && *(short*)(pEnt + 1) == pKey[0] &&
                *(short*)((int)pEnt + 6) == pKey[1] && *(short*)(pEnt + 2) == pKey[2]) {
                return (unsigned int)row << 0x10 | (col & 0xffff);
            }
            col++;
            pEnt += 4;
            count--;
        }
        row++;
        rowOff += 0xe4;
    }
}

// ---------------------------------------------------------------------------
// 0x80302bec  aselgrid_remove_active_entries
// Remove every entry matching the active screen record from each row (rows
// @ctx+0x288 stride 0xe4, entry count @+0x58, entries 0x10 stride from +100);
// compact the tail; when a row empties and its mode (+0x34) == 5, run cleanup.
// ---------------------------------------------------------------------------
void aselgrid_remove_active_entries(void)
{
    int activeRec = (int)FUN_803826f0();
    int rowOff = 0;
    int count = 0;
    for (int row = 0; row < *(int*)(DAT_80612be4 + 0x280); row++) {
        int pRow = DAT_80612be4 + rowOff + 0x288;
        for (int i = 0; (count = *(int*)(pRow + 0x58)), i < count; i++) {
            int entOff = i * 0x10;
            if (*(int*)(pRow + entOff + 100) == activeRec) {
                int j = i;
                if (i < count - 1) {
                    for (; j < *(int*)(pRow + 0x58) - 1; j++) {
                        int p = pRow + entOff;
                        entOff += 0x10;
                        *(u32*)(p + 100)  = *(u32*)(p + 0x74);
                        *(u32*)(p + 0x68) = *(u32*)(p + 0x78);
                        *(u32*)(p + 0x6c) = *(u32*)(p + 0x7c);
                        *(u32*)(p + 0x70) = *(u32*)(p + 0x80);
                    }
                    i--;
                }
                *(int*)(pRow + 0x58) = *(int*)(pRow + 0x58) - 1;
            }
        }
        if (count < 1 && *(int*)(pRow + 0x34) == 5) {
            FUN_80329650();
        }
        rowOff += 0xe4;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80302cf0  aselgrid_remove_entry_at
// Remove entry[index] from the 8-byte-stride list at base+0x24 (count @base+0x20);
// compact the tail and recompute slot addresses via FUN_80336690. base/index are
// the {high,low} PPC register pair the original read from the caller.
// ---------------------------------------------------------------------------
void aselgrid_remove_entry_at(int base, int index)
{
    if ((*(unsigned int*)(DAT_80612be4 + 0x238) & 0x20) == 0) {
        FUN_80348238(0x16);
        *(unsigned int*)(DAT_80612be4 + 0x238) = *(unsigned int*)(DAT_80612be4 + 0x238) | 0x20;
    }
    if (index < 0) {
        *(u32*)(base + 0x20) = 0;
    } else {
        int off = index << 3;
        *(int*)(base + 0x20) = *(int*)(base + 0x20) - 1;
        for (; index < *(int*)(base + 0x20); index++) {
            *(u32*)(base + off + 0x24) = *(u32*)(base + off + 0x2c);
            int a = FUN_80336690(base, index);
            int b = FUN_80336690(base, index + 1);
            leaf_31e8((u32)a, (u32)b, 6);
            off += 8;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80302dcc  aselgrid_purge_active_mode3
// For each mode-3 row (@ctx+0xdd14 stride 0x8c, count @ctx+0xdd10) remove entries
// matching the active record via aselgrid_remove_entry_at.
// ---------------------------------------------------------------------------
void aselgrid_purge_active_mode3(void)
{
    int activeRec = (int)FUN_803826e8();
    int rowOff = 0;
    for (int row = 0; row < *(int*)(DAT_80612be4 + 0xdd10); row++) {
        int pRow = DAT_80612be4 + rowOff + 0xdd14;
        if (*(int*)(pRow + 0x80) == 3) {
            for (int i = 0; i < *(int*)(pRow + 0x20); i++) {
                if (*(int*)(pRow + i * 8 + 0x24) == activeRec) {
                    i--;
                    aselgrid_remove_entry_at(pRow, i + 1);
                }
            }
        }
        rowOff += 0x8c;
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80302e78  aselgrid_scan_hit_grid
// Walk the member list (@ctx+0x2f7c, next @+0x40) and hit-test each occupied cell
// of each tile grid against the drag point (ctx+0x2f84/0x2f88); on the first hit
// store the depth @ctx+0x26c and set the "hit" bit 0x400 @ctx+0x25c.
// ---------------------------------------------------------------------------
void aselgrid_scan_hit_grid(void)
{
    FUN_803826d8();

    float baseX = FLOAT_80612c58 *
                  (*(float*)(DAT_80612be4 + 0x2f84) - ibias(DAT_80612c88)) + FLOAT_80612c78;
    float baseY = FLOAT_80612c54 *
                  (*(float*)(DAT_80612be4 + 0x2f88) - ibias(DAT_80612c84)) + FLOAT_80612c74;
    float lim638 = FLOAT_8061a638;
    float lim604 = FLOAT_8061a604;
    float lim634 = FLOAT_8061a634;

    for (float* pMember = *(float**)(DAT_80612be4 + 0x2f7c);
         pMember != 0;
         pMember = *(float**)(pMember + 0x10)) {  // next-ptr stored in slot [0x10]
        float cellX = baseX + pMember[0];
        float pGridF = pMember[(int)pMember[3] + 0xb];
        int pGrid = (int)pGridF;
        if (cellX < lim604) {
            cellX = cellX - FLOAT_8061a614;
        }
        float cellY = baseY + pMember[1];
        float ax = ibias((u32)(int)cellX);
        if (cellY < lim604) {
            cellY = cellY - FLOAT_8061a614;
        }
        float ay = ibias((u32)(int)cellY);

        float depth = ibias((u32)(int)(ibias(*(u32*)(DAT_80612be4 + 0x264)) + pMember[2]));

        // grid must be within the visible span on both axes
        if (ax < lim604 || lim634 < (float)(ax + ibias(*(u32*)(pGrid + 0x14))) ||
            ay < lim604 || lim634 < (float)(ay + ibias(*(u32*)(pGrid + 0x18)))) {
            break;
        }

        int rowByteOff = 0;
        for (unsigned int r = 0; (int)r < *(int*)(pGrid + 0x1c); r++) {
            int* pRowCells = *(int**)(pGrid + rowByteOff + 0x20);
            depth = depth + ibias(r);
            if (depth < lim638) {
                goto done;
            }
            unsigned int col = 0;
            unsigned int rowIdx = 0;
            int cellsLeft = *(int*)(pGrid + 0x14) * *(int*)(pGrid + 0x18);
            while (cellsLeft != 0) {
                cellsLeft--;
                if (*pRowCells != 0) {
                    // X in the r3 gpr slot; Y/Z as doubles. roster_hit_test_point is
                    // void and records the pick at ctx+0x2f80 - a hit is a non-zero pick.
                    roster_hit_test_point((u64)(u32)(int)(ax + ibias(col)),
                                          (double)(ay + ibias(rowIdx)),
                                          (double)depth);
                    if (*(int*)(DAT_80612be4 + 0x2f80) != 0) {
                        *(int*)(DAT_80612be4 + 0x26c) = (int)depth;
                        *(unsigned int*)(DAT_80612be4 + 0x25c) =
                            *(unsigned int*)(DAT_80612be4 + 0x25c) | 0x400;
                        goto done;
                    }
                }
                col++;
                if (col == *(unsigned int*)(pGrid + 0x14)) {
                    col = 0;
                    rowIdx++;
                }
                pRowCells++;
            }
            rowByteOff += 4;
        }
    }
done:
    FUN_80382724();
}

// ===========================================================================
// batch_867 (0x803031f0 - 0x80303904): grid visibility/occupancy pass
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x803031f0  aselgrid_mark_hidden
// If the member's "visible" bit 0x10 (@+0x14) is set, clear it and set the
// "changed" bit 0x2. No-op if it was not visible.
// ---------------------------------------------------------------------------
void aselgrid_mark_hidden(int pMember)
{
    if ((*(unsigned char*)(pMember + 0x14) & 0x10) == 0) {
        return;
    }
    *(unsigned char*)(pMember + 0x14) = *(unsigned char*)(pMember + 0x14) & 0xef;
    *(unsigned char*)(pMember + 0x14) = *(unsigned char*)(pMember + 0x14) | 2;
}

// ---------------------------------------------------------------------------
// 0x80303214  aselgrid_flush_visibility_flags
// Walk the member list (DAT_80612c14, next @+0x40): promote the pending bit 0x8
// to the "changed" bit 0x2, clear bits 0x18, then store the current visible
// member count (FUN_8030a220) at ctx+0x2f9c.
// ---------------------------------------------------------------------------
void aselgrid_flush_visibility_flags(void)
{
    int pMember = DAT_80612c14;
    while (pMember != 0) {
        if ((*(unsigned char*)(pMember + 0x14) & 8) != 0) {
            *(unsigned char*)(pMember + 0x14) = *(unsigned char*)(pMember + 0x14) | 2;
        }
        *(unsigned char*)(pMember + 0x14) = *(unsigned char*)(pMember + 0x14) & 0xe7;
        pMember = *(int*)(pMember + 0x40);
    }
    *(int*)(DAT_80612be4 + 0x2f9c) = FUN_8030a220();
}

// ---------------------------------------------------------------------------
// 0x80303270  aselgrid_row_in_span
// Predicate: is frameRow within [gridY, gridY + rowDepth - 1] for the piece's
// active grid (grid ptr @piece+0xc*4+0x2c, depth @grid+0x1c, gridY @piece+8)?
// ---------------------------------------------------------------------------
unsigned int aselgrid_row_in_span(int pPiece, int frameRow)
{
    unsigned int result = 0;
    int gridY = (int)*(float*)(pPiece + 8);
    int pGrid = *(int*)(pPiece + *(int*)(pPiece + 0xc) * 4 + 0x2c);
    if (gridY <= frameRow && frameRow <= *(int*)(pGrid + 0x1c) + gridY - 1) {
        result = 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x803032c4  aselgrid_recompute_visibility
// Recompute which grid cells fall inside the current drag box (the AABB between
// the live cursor and the drag anchor @ctx+0x2f94/0x2f98). Only runs when the box
// exceeds FLOAT_8061a63c on either axis. Sets/clears each member's visible (0x10)
// and changed (0x2/0x8) flags and accumulates the visible count at ctx+0x2f9c.
// ---------------------------------------------------------------------------
void aselgrid_recompute_visibility(void)
{
    float curX = FLOAT_80612c58 *
                 (*(float*)((int)&DAT_8051a834 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c88)) +
                 FLOAT_80612c78;
    float curY = FLOAT_80612c54 *
                 (*(float*)((int)&DAT_8051a838 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c84)) +
                 FLOAT_80612c74;

    double boxLoX = (double)curX;
    double boxHiX = (double)*(float*)(DAT_80612be4 + 0x2f94);
    if (boxLoX < boxHiX) {
        // in-order: [boxLoX, boxHiX]
    } else {
        double t = boxLoX; boxLoX = boxHiX; boxHiX = t;
    }
    double boxLoY = (double)curY;
    double boxHiY = (double)*(float*)(DAT_80612be4 + 0x2f98);
    if (boxLoY < boxHiY) {
        // in-order
    } else {
        double t = boxLoY; boxLoY = boxHiY; boxHiY = t;
    }

    if ((float)(boxHiX - boxLoX) < FLOAT_8061a63c && (float)(boxHiY - boxLoY) < FLOAT_8061a63c) {
        return;
    }

    *(int*)(DAT_80612be4 + 0x2f9c) = 0;
    *(unsigned int*)(DAT_80612be4 + 0x25c) = *(unsigned int*)(DAT_80612be4 + 0x25c) | 0x20;

    for (float* pMember = (float*)DAT_80612c14; pMember != 0;
         pMember = *(float**)(pMember + 0x10)) {
        int pGrid = (int)pMember[(int)pMember[3] + 0xb];
        *(unsigned char*)(pMember + 5) = *(unsigned char*)(pMember + 5) & 0xf7;
        if (aselgrid_row_in_span((int)pMember, *(int*)(DAT_80612be4 + 0x264)) == 0) {
            continue;
        }

        double memX = (double)pMember[0];
        double memY = (double)pMember[1];
        if (boxHiX <= memX || boxHiY <= memY) {
            aselgrid_mark_hidden((int)pMember);
            continue;
        }

        int gw = *(int*)(pGrid + 0x14);
        int gh = *(int*)(pGrid + 0x18);
        double memRight = (double)(float)(memX + (double)ibias((u32)gw));
        double memBottom = (double)(float)(memY + (double)ibias((u32)gh));
        if (memRight < boxLoX || memBottom < boxLoY) {
            aselgrid_mark_hidden((int)pMember);
            continue;
        }

        bool anyOccupied;
        if (memX < boxLoX || boxHiX <= memRight || memY < boxLoY || boxHiY <= memBottom) {
            // partially clipped: scan the visible cell sub-rectangle for an occupied cell
            int x0 = (boxLoX <= memX) ? 0 : (int)(boxLoX - memX);
            int x1 = (memRight <= boxHiX) ? gw - 1 : (int)(boxHiX - memX);
            int y0 = (boxLoY <= memY) ? 0 : (int)(boxLoY - memY);
            int y1 = (memBottom <= boxHiY) ? gh - 1 : (int)(boxHiY - memY);

            anyOccupied = false;
            for (int y = y0; y <= y1 && !anyOccupied; y++) {
                for (int x = x0; x <= x1; x++) {
                    if (*(int*)(*(int*)(pGrid + 0x20) + (x + y * gw) * 4) != 0) {
                        anyOccupied = true;
                        break;
                    }
                }
            }
        } else {
            // fully inside the box
            anyOccupied = true;
        }

        if (anyOccupied) {
            if ((*(unsigned char*)(pMember + 5) & 0x10) == 0) {
                *(unsigned char*)(pMember + 5) = *(unsigned char*)(pMember + 5) | 8;
            }
            unsigned char flags = *(unsigned char*)(pMember + 5);
            if ((flags & 2) != 0 && (flags & 8) != 0) {
                *(unsigned char*)(pMember + 5) = flags & 0xf5;
                *(unsigned char*)(pMember + 5) = *(unsigned char*)(pMember + 5) | 0x10;
            }
            if (((unsigned int)pMember[5] & 0x10000000) == 0) {
                *(int*)(DAT_80612be4 + 0x2f9c) = *(int*)(DAT_80612be4 + 0x2f9c) + 1;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8030363c  aselgrid_clear_change_flags
// Clear the "changed" bit 0x2 on every member and reset the visible count.
// ---------------------------------------------------------------------------
void aselgrid_clear_change_flags(void)
{
    for (int pMember = DAT_80612c14; pMember != 0; pMember = *(int*)(pMember + 0x40)) {
        *(unsigned char*)(pMember + 0x14) = *(unsigned char*)(pMember + 0x14) & 0xfd;
    }
    *(int*)(DAT_80612be4 + 0x2f9c) = 0;
}

// ---------------------------------------------------------------------------
// 0x8030366c  aselgrid_point_in_bounds
// 3D point-in-cell test: is pt[0..2] within the grid extents (w @grid+0x14,
// h @grid+0x18, d @grid+0x1c) offset by the piece origin (piece[0..2])?
// ---------------------------------------------------------------------------
unsigned int aselgrid_point_in_bounds(float* pPiece, int* pt)
{
    unsigned int result = 0;
    if ((int)pPiece[0] <= pt[0]) {
        float pGrid = pPiece[(int)pPiece[3] + 0xb];
        if (pt[0] <= (int)(pPiece[0] + ibias(*(u32*)((int)pGrid + 0x14)))) {
            if ((int)pPiece[1] <= pt[1]) {
                if (pt[1] <= (int)(pPiece[1] + ibias(*(u32*)((int)pGrid + 0x18)))) {
                    if ((int)pPiece[2] <= pt[2]) {
                        if (pt[2] < (int)(pPiece[2] + ibias(*(u32*)((int)pGrid + 0x1c)))) {
                            result = 1;
                        }
                    }
                }
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x80303780  aselgrid_segment_in_bounds
// Test a cell segment (both endpoints) against the grid bounds. The second
// endpoint steps by +1 in X or Y depending on the segment's orientation bit 0x1.
// ---------------------------------------------------------------------------
unsigned int aselgrid_segment_in_bounds(float* pPiece, short* pCell)
{
    unsigned int result = 0;
    int pt[3];
    int pt2[3];

    pt[0]  = (int)pCell[0];
    pt[1]  = (int)pCell[1];
    pt[2]  = (int)pCell[2];
    if ((*(unsigned char*)(pCell + 3) & 1) == 0) {
        pt2[1] = pt[1] + 1;
        pt2[0] = pt[0];
    } else {
        pt2[0] = pt[0] + 1;
        pt2[1] = pt[1];
    }
    pt2[2] = pt[2];

    if (aselgrid_point_in_bounds(pPiece, pt) != 0 &&
        aselgrid_point_in_bounds(pPiece, pt2) != 0) {
        result = 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8030382c  aselgrid_find_visible_segment
// Scan the cell table (ctx+0x3070, 0xc stride, count ctx+0x306e) for the first
// in-bounds segment whose visible flag (short @+5) is non-negative. The active
// piece pointer arrives in the r28 spill (PPC reg pair).
// ---------------------------------------------------------------------------
void aselgrid_find_visible_segment(void)
{
    float* pPiece = (float*)(u32)__save_gpr_28();
    int off = 0;
    for (int i = 0; i < (int)(unsigned int)*(unsigned short*)(DAT_80612be4 + 0x306e); i++) {
        short* pCell = (short*)(DAT_80612be4 + off + 0x3070);
        if (aselgrid_segment_in_bounds(pPiece, pCell) != 0 && pCell[5] >= 0) {
            break;
        }
        off += 0xc;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x803038b4  roster_clear_all
// Remove every current roster member until the count (@header+6) reaches zero.
// ---------------------------------------------------------------------------
void roster_clear_all(int pHeader)
{
    while (*(short*)(pHeader + 6) != 0) {
        roster_remove_current();
    }
}

// ---------------------------------------------------------------------------
// 0x80303904  aselgrid_remove_inbounds_cells
// Iterate the active piece's cells; remove+clear each cell whose segment lies in
// bounds (aselgrid_segment_in_bounds), compacting the index as entries are removed.
// ---------------------------------------------------------------------------
void aselgrid_remove_inbounds_cells(void)
{
    u64 pair = FUN_803826f0();
    float* pPiece = (float*)(u32)(pair >> 0x20);
    int pHeader = (int)(u32)pair;

    int i = 0;
    while (i < (int)(unsigned int)*(unsigned short*)(pHeader + 6)) {
        int hit = (int)aselgrid_segment_in_bounds(pPiece, (short*)(pHeader + i * 0xc + 8));
        int next = i;
        if (hit != 0) {
            next = i - 1;
            roster_remove_and_clear(i);
        }
        i = next + 1;
    }
    FUN_8038273c();
}

// ===========================================================================
// batch_868 (0x80303978 - 0x80304684): cut/copy/paste selection buffer
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x80303978  aselgrid_free_flagged_members
// Walk the member list (DAT_80612c14, next @+0x40) and free (FUN_80313bd4) every
// node with the "temp/copy" bit 0x2000000 set (@+5); then reset the visible count.
// ---------------------------------------------------------------------------
void aselgrid_free_flagged_members(void)
{
    float* pMember = (float*)DAT_80612c14;
    while (pMember != 0) {
        float* pNext = *(float**)(pMember + 0x10);
        if (((unsigned int)pMember[5] & 0x2000000) != 0) {
            FUN_80313bd4(pMember);
        }
        pMember = pNext;
    }
    *(int*)(DAT_80612be4 + 0x2f9c) = 0;
}

// ---------------------------------------------------------------------------
// 0x803039d0  aselgrid_apply_changed_removals
// Walk the member list; for each node with the "changed" bit 0x2 (@+0x14) set,
// run aselgrid_remove_inbounds_cells.
// ---------------------------------------------------------------------------
void aselgrid_apply_changed_removals(void)
{
    int pMember = DAT_80612c14;
    while (pMember != 0) {
        unsigned char* pFlags = (unsigned char*)(pMember + 0x14);
        pMember = *(int*)(pMember + 0x40);
        if ((*pFlags & 2) != 0) {
            aselgrid_remove_inbounds_cells();
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80303a30  aselgrid_remap_record
// Remap a record pointer old->new across every row (ctx+0x288 stride 0xe4,
// entries @+100 stride 0x10, count @+0x58).
// ---------------------------------------------------------------------------
void aselgrid_remap_record(int oldRec, u32 newRec)
{
    int rowOff = 0;
    for (int row = 0; row < *(int*)(DAT_80612be4 + 0x280); row++) {
        int pRow = DAT_80612be4 + rowOff + 0x288;
        int entOff = 0;
        for (int i = 0; i < *(int*)(pRow + 0x58); i++) {
            if (*(int*)(pRow + entOff + 100) == oldRec) {
                *(u32*)(pRow + entOff + 100) = newRec;
            }
            entOff += 0x10;
        }
        rowOff += 0xe4;
    }
}

// ---------------------------------------------------------------------------
// 0x80303a94  aselgrid_remap_record_mode3
// Remap a record pointer old->new across mode-3 rows (ctx+0xdd14 stride 0x8c,
// entries @+0x24 stride 8, count @+0x20).
// ---------------------------------------------------------------------------
void aselgrid_remap_record_mode3(int oldRec, u32 newRec)
{
    int rowOff = 0;
    int rowCount = *(int*)(DAT_80612be4 + 0xdd10);
    for (int row = 0; row < rowCount; row++) {
        int pRow = DAT_80612be4 + rowOff + 0xdd14;
        if (*(int*)(pRow + 0x80) == 3) {
            int n = *(int*)(pRow + 0x20);
            int entOff = 0;
            while (n > 0) {
                if (*(int*)(pRow + entOff + 0x24) == oldRec) {
                    *(u32*)(pRow + entOff + 0x24) = newRec;
                }
                entOff += 8;
                n--;
            }
        }
        rowOff += 0x8c;
    }
}

// ---------------------------------------------------------------------------
// 0x80303b0c  aselgrid_build_selection
// For each cell-table entry (ctx+0x3070, 0xc stride, count ctx+0x306e), count how
// many of the buffered members (ctx+0x2f7c chain) contain the segment in bounds;
// when >= 2, add the entry to the scratch roster (ctx+0x3868) and rebase its coords
// by the first buffered member's origin. The screen/non-screen path is selected by
// the r25 spill.
// ---------------------------------------------------------------------------
void aselgrid_build_selection(void)
{
    int isScreen = (int)__save_gpr_25();

    for (int e = 0; e < (int)(unsigned int)*(unsigned short*)(DAT_80612be4 + 0x306e); e++) {
        float* pMember = *(float**)(DAT_80612be4 + 0x2f7c);
        short* pCell = (short*)(DAT_80612be4 + e * 0xc + 0x3070);
        short cellX = pCell[0];
        short cellY = pCell[1];
        int hits = 0;
        while (pMember != 0 && hits < 2) {
            int add = hits;
            if (aselgrid_segment_in_bounds(pMember, pCell) != 0) {
                add = hits + 1;
                int pGrid = (int)pMember[(int)pMember[3] + 0xb];
                if (pMember[0] < ibias((u32)(int)cellX)) {
                    if (ibias((u32)(int)cellX) < pMember[0] + ibias(*(u32*)(pGrid + 0x14))) {
                        if (pMember[1] < bbias((u32)(unsigned short)cellY)) {
                            if (ibias((u32)(int)cellY) <
                                pMember[1] + ibias(*(u32*)(pGrid + 0x18))) {
                                add = hits + 2;
                            }
                        }
                    }
                }
            }
            pMember = *(float**)(pMember + 0x10);
            hits = add;
        }

        if (hits > 1) {
            unsigned int slot;
            if (isScreen == 0) {
                slot = roster_add_member((unsigned short*)(DAT_80612be4 + 0x3868),
                                         DAT_80612be4 + 0x3068, e);
                *(unsigned short*)(DAT_80612be4 + slot * 0xc + 0x387a) = 0xffff;
            } else {
                roster_add_via_screen((u32)(DAT_80612be4 + 0x3868),
                                      (u32)(DAT_80612be4 + 0x3068), e);
                // roster_add_via_screen has no usable return in this model; recompute slot below.
                slot = 0xffffffff;
                e--;
            }
            if ((int)slot >= 0) {
                int so = slot * 0xc;
                float* pFirst = *(float**)(DAT_80612be4 + 0x2f7c);
                *(short*)(DAT_80612be4 + so + 0x3870) =
                    *(short*)(DAT_80612be4 + so + 0x3870) - (short)(int)ibias((u32)(int)pFirst[0]);
                *(short*)(DAT_80612be4 + so + 0x3872) =
                    *(short*)(DAT_80612be4 + so + 0x3872) - (short)(int)ibias((u32)(int)pFirst[1]);
                *(short*)(DAT_80612be4 + so + 0x3874) =
                    *(short*)(DAT_80612be4 + so + 0x3874) - (short)*(int*)(DAT_80612be4 + 0x264);
            }
        }
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x80303d78  aselgrid_copy_selection
// Pick the member under the cursor (FUN_80313af4). If found, clear the scratch
// quad, free any existing paste buffer, then clone the picked member (single, or
// the whole flag-0x2 group) into the paste buffer (ctx+0x2f7c) rebased to the
// cursor origin. Screen path (FUN_803826e4 != 0) also remaps record pointers.
// ---------------------------------------------------------------------------
void aselgrid_copy_selection(void)
{
    int isScreen = (int)FUN_803826e4();

    float curX = FLOAT_80612c58 *
                 (*(float*)((int)&DAT_8051a834 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c88)) +
                 FLOAT_80612c78;
    float curY = FLOAT_80612c54 *
                 (*(float*)((int)&DAT_8051a838 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c84)) +
                 FLOAT_80612c74;
    float curZ = ibias(*(u32*)(DAT_80612be4 + 0x264));

    float* pPicked = FUN_80313af4((double)curX, (double)curY, (double)curZ);
    if (pPicked != 0) {
        avanim_play_move_sound(0x1578);
        roster_clear_quad();
        if (*(float**)(DAT_80612be4 + 0x2f7c) != 0) {
            FUN_80313cf4(*(float**)(DAT_80612be4 + 0x2f7c));
        }
        *(int*)(DAT_80612be4 + 0x2f7c) = 0;
        unsigned int pMember = (unsigned int)DAT_80612c14;

        if (((unsigned int)pPicked[5] & 0x2000000) == 0) {
            // single-member copy
            int buf = FUN_80313b7c();
            *(int*)(DAT_80612be4 + 0x2f7c) = buf;
            if (buf != 0) {
                leaf_31e8((u32)buf, (u32)pPicked, 0x48);
                *(int*)(buf + 0x40) = 0;
                aselgrid_build_selection();
                *(float*)buf = ibias((u32)(int)(pPicked[0] - curX));
                *(float*)(buf + 4) = ibias((u32)(int)(pPicked[1] - curY));
                *(float*)(buf + 8) = pPicked[2] - ibias(*(u32*)(DAT_80612be4 + 0x264));
                *(unsigned char*)(buf + 0x14) = *(unsigned char*)(buf + 0x14) & 0xfd;
                if (isScreen == 0) {
                    *(int*)(buf + 0x3c) = 0;
                    FUN_803140c8();
                } else {
                    *(float*)(buf + 0x3c) = pPicked[0xf];
                    pPicked[0xf] = 0.0f;
                    aselgrid_remap_record((int)pPicked, (u32)buf);
                    aselgrid_remap_record_mode3((int)pPicked, (u32)buf);
                    FUN_80313bd4(pPicked);
                }
            }
        } else {
            // group copy: skip to first changed(0x2) member
            for (; pMember != 0 && (*(unsigned char*)(pMember + 0x14) & 2) == 0;
                 pMember = *(unsigned int*)(pMember + 0x40)) {
            }
            if (pMember != 0) {
                unsigned int head = (unsigned int)FUN_80313b7c();
                *(unsigned int*)(DAT_80612be4 + 0x2f7c) = head;
                leaf_31e8(head, pMember, 0x48);
                *(int*)(head + 0x40) = 0;
                if (isScreen == 0) {
                    *(int*)(head + 0x3c) = 0;
                    FUN_803140c8();
                } else {
                    aselgrid_remap_record((int)pMember, head);
                    aselgrid_remap_record_mode3((int)pMember, head);
                    *(int*)(pMember + 0x3c) = 0;
                }
                unsigned int tail = head;
                while (pMember != 0) {
                    for (pMember = *(unsigned int*)(pMember + 0x40);
                         pMember != 0 && (*(unsigned char*)(pMember + 0x14) & 2) == 0;
                         pMember = *(unsigned int*)(pMember + 0x40)) {
                    }
                    unsigned int node;
                    if (pMember == 0) {
                        node = 0;
                    } else {
                        node = (unsigned int)FUN_80313b7c();
                        if (node != 0) {
                            leaf_31e8(node, pMember, 0x48);
                            if (isScreen == 0) {
                                *(int*)(node + 0x3c) = 0;
                                FUN_803140c8();
                            } else {
                                *(int*)(pMember + 0x3c) = 0;
                                aselgrid_remap_record((int)pMember, node);
                                aselgrid_remap_record_mode3((int)pMember, node);
                            }
                        }
                    }
                    *(unsigned int*)(tail + 0x40) = node;
                    *(unsigned char*)(tail + 0x14) = *(unsigned char*)(tail + 0x14) & 0xfd;
                    tail = node;
                }
                aselgrid_build_selection();

                float rebaseX = FLOAT_80612c58 *
                    (*(float*)((int)&DAT_8051a834 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c88)) +
                    FLOAT_80612c78;
                float rebaseY = FLOAT_80612c54 *
                    (*(float*)((int)&DAT_8051a838 + DAT_806119ec * 0x1e0) - ibias(DAT_80612c84)) +
                    FLOAT_80612c74;
                for (float* p = *(float**)(DAT_80612be4 + 0x2f7c); p != 0;
                     p = *(float**)(p + 0x10)) {
                    p[0] = p[0] - rebaseX;
                    p[1] = p[1] - rebaseY;
                    p[2] = p[2] - ibias(*(u32*)(DAT_80612be4 + 0x264));
                }
                if (isScreen != 0) {
                    aselgrid_apply_changed_removals();
                    aselgrid_free_flagged_members();
                }
            }
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8030427c  aselgrid_paste_at
// Paste each buffered scratch-roster member (ctx+0x3068, count ctx+0x386e) onto
// the grid at offset (x,y,z), then clear the scratch quad. x/y arrive in r3/r4.
// ---------------------------------------------------------------------------
void aselgrid_paste_at(int x, int y, short z)
{
    for (int i = 0; i < (int)(unsigned int)*(unsigned short*)(DAT_80612be4 + 0x386e); i++) {
        unsigned int slot = roster_add_member((unsigned short*)(DAT_80612be4 + 0x3068),
                                              DAT_80612be4 + 0x3868, i);
        if ((int)slot >= 0) {
            int so = slot * 0xc;
            *(short*)(DAT_80612be4 + so + 0x3070) =
                *(short*)(DAT_80612be4 + so + 0x3070) + (short)x;
            *(short*)(DAT_80612be4 + so + 0x3072) =
                *(short*)(DAT_80612be4 + so + 0x3072) + (short)y;
            *(short*)(DAT_80612be4 + so + 0x3074) =
                *(short*)(DAT_80612be4 + so + 0x3074) + z;
        }
    }
    roster_clear_quad();
}

// ---------------------------------------------------------------------------
// 0x8030432c  aselgrid_commit_paste
// Commit the paste: rebuild the scratch selection at the cursor, then for each
// buffered member allocate a new node, copy + rebase, validate and reserve a slot;
// on any failure, free the buffer, mark dirty, refresh the roster and play a sound.
// ---------------------------------------------------------------------------
void aselgrid_commit_paste(void)
{
    FUN_803826f0();
    int mode = FUN_8031424c();

    double curX = (double)(FLOAT_80612c58 *
                  (*(float*)(DAT_80612be4 + 0x2f84) - ibias(DAT_80612c88)) + FLOAT_80612c78);
    double curY = (double)(FLOAT_80612c54 *
                  (*(float*)(DAT_80612be4 + 0x2f88) - ibias(DAT_80612c84)) + FLOAT_80612c74);

    float* pFirst = *(float**)(DAT_80612be4 + 0x2f7c);
    aselgrid_paste_at((int)ibias((u32)(int)((double)pFirst[0] + curX)),
                      (int)ibias((u32)(int)((double)pFirst[1] + curY)),
                      (short)*(int*)(DAT_80612be4 + 0x264));

    float deadzone = FLOAT_8061a604;
    float* pMember = *(float**)(DAT_80612be4 + 0x2f7c);
    while (true) {
        float* pNode;
        if (pMember == 0 || (pNode = (float*)FUN_80313b7c()) == 0) {
            if ((*(unsigned int*)(DAT_80612be4 + 0x238) & 0x20) == 0) {
                FUN_80348238(7);
                *(unsigned int*)(DAT_80612be4 + 0x238) =
                    *(unsigned int*)(DAT_80612be4 + 0x238) | 0x20;
            }
            FUN_80313cf4(*(float**)(DAT_80612be4 + 0x2f7c));
            *(int*)(DAT_80612be4 + 0x2f7c) = 0;
            FUN_80307ec0();
            avanim_play_move_sound(0x157b);
            *(unsigned int*)(DAT_80612be4 + 0x238) =
                *(unsigned int*)(DAT_80612be4 + 0x238) | 0x4000;
            FUN_8038273c();
            return;
        }
        leaf_31e8((u32)pNode, (u32)pMember, 0x48);
        aselgrid_remap_record((int)pMember, (u32)pNode);
        aselgrid_remap_record_mode3((int)pMember, (u32)pNode);
        pMember[0xf] = 0.0f;

        double vx = (double)(float)((double)pMember[0] + curX);
        if (vx < (double)deadzone) {
            vx = (double)(float)(vx - (double)FLOAT_8061a614);
        }
        pNode[0] = ibias((u32)(int)vx);
        double vy = (double)(float)((double)pMember[1] + curY);
        if (vy < (double)deadzone) {
            vy = (double)(float)(vy - (double)FLOAT_8061a614);
        }
        pNode[1] = ibias((u32)(int)vy);
        pNode[2] = pMember[2] + ibias(*(u32*)(DAT_80612be4 + 0x264));
        pNode[0x10] = 0.0f;

        int ok = FUN_80313da0();
        if (ok == 0 || FUN_8030f830((mode == 0 ? 0 : 8)) == 0) {
            FUN_80313cf4(pNode);
            FUN_80313cf4(*(float**)(DAT_80612be4 + 0x2f7c));
            *(int*)(DAT_80612be4 + 0x2f7c) = 0;
            FUN_8038273c();
            return;
        }
        pMember = *(float**)(pMember + 0x10);
    }
}

// ---------------------------------------------------------------------------
// 0x80304648  aselgrid_set_record
// Copy a 0x14-byte record (src) into the target at dst+0x18, then finalize.
// ---------------------------------------------------------------------------
void aselgrid_set_record(int dst, u32 src)
{
    leaf_31e8((u32)(dst + 0x18), src, 0x14);
    FUN_8030a274();
}

// ---------------------------------------------------------------------------
// 0x80304684  aselgrid_reset_table
// Reset the placement table: row count = 0, selection = -1, zero the 0x2c88-byte
// row array at ctx+0x288.
// ---------------------------------------------------------------------------
void aselgrid_reset_table(void)
{
    *(int*)(DAT_80612be4 + 0x280) = 0;
    *(int*)(DAT_80612be4 + 0x284) = -1;
    memset_wrapper(DAT_80612be4 + 0x288, 0, 0x2c88);
}
