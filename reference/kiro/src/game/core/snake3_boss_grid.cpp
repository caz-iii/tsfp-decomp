// =============================================================================
// snake3_boss_grid.cpp - the 5x7 arena navigation grid used by the snake3 boss.
//
// The snake3 boss ("morph serpent 3") moves between discrete cells of a fixed
// 5-row x 7-column board that is baked into the boss control block when the
// fight is reset. Each cell is a 10-float (0x28-byte) axis-aligned rectangle
// record; a row is 7 cells = 0x118 bytes; the whole grid is 5 rows.
//
// Per-cell layout (float offsets unless noted):
//   +0x00 minX   +0x04 minY   +0x08 maxX   +0x0c maxY
//   +0x10 centerX +0x14 centerY
//   +0x18 halfW  +0x1c halfH
//   +0x20 flagA(u32)  +0x24 flagB(u32)   (edge/corner classification + occupancy)
//
// Within the boss control block (ctx), the grid cell array begins at +0x96c,
// the runtime occupancy grid at +0x990 (one u32 per cell, same 0x118/0x28
// strides), and a set of eight tracked "endpoint" records begins at +0x119c
// (0x168-byte stride) with a pair of resolved (row,col) targets stored at
// +0x118c and +0x1194.
//
// Geometry constants (data at 0x806199a8..0x80619ad8, big-endian floats):
//   FLOAT_80619aa8 = 3.2205  cell width  step (X)
//   FLOAT_80619aac = 3.2     cell height step (Y)
//   FLOAT_80619ab0 = 1.61    half width
//   FLOAT_80619ab4 = 1.6     half height
//   FLOAT_80619ab8 = 0.5     midpoint factor
//   FLOAT_80619abc = 13.0 / FLOAT_80619ac0 = 10.05  alternate row spans
//   FLOAT_80619ac4 = 9.2  / FLOAT_80619ac8 = 7.6     alternate column spans
//   FLOAT_80619acc = 4.2207 / FLOAT_80619ad0 = 4.2   merge thresholds
//   FLOAT_80619ad4 = 0.9375 edge inset
//
// Functions (all subsystem: game):
//   * snake3_grid_locate_cell     (0x802b04d0) - map a world (x,y) to (row,col).
//   * snake3_grid_cell_init       (0x802b0580) - fill one cell rect from (x,y).
//   * snake3_grid_build           (0x802b05e8) - build the whole 5x7 grid plus
//     the edge/corner "extra" cells, then classify each cell's size flags.
//   * snake3_grid_neighbor_down   (0x802b0b2c) - adjacency: step toward row 0/4.
//   * snake3_grid_neighbor_right  (0x802b0ba0) - adjacency: step to column 6.
//   * snake3_grid_neighbor_up     (0x802b0bfc) - adjacency: step to row 2.
//   * snake3_grid_neighbor_left   (0x802b0c58) - adjacency: step to column 3.
//   * snake3_grid_mark_reachable  (0x802b0cb4) - resolve two endpoints and flag a
//     1-ring of reachable cells around every active endpoint record.
//   * snake3_grid_coord_compare   (0x802b0e3c) - classify two (row,col) coords
//     into a direction/relationship code.
//   * snake3_grid_pick_free_neighbor (0x802b0ea0) - collect the free cells in a
//     5x5 window around a coord and pick a directed or random one, returning its
//     world center and squared distance from the source.
//
// Address range: 0x802b04d0 - 0x802b0e9f
// Subsystem: game (snake3 boss navigation grid)
// =============================================================================

#include "common.h"

extern "C" {

// --- grid geometry constants -------------------------------------------------
extern const float FLOAT_80619aa8;  // 3.2205  X step
extern const float FLOAT_80619aac;  // 3.2     Y step
extern const float FLOAT_80619ab0;  // 1.61    half width
extern const float FLOAT_80619ab4;  // 1.6     half height
extern const float FLOAT_80619ab8;  // 0.5     midpoint factor
extern const float FLOAT_80619abc;  // 13.0
extern const float FLOAT_80619ac0;  // 10.05
extern const float FLOAT_80619ac4;  // 9.2
extern const float FLOAT_80619ac8;  // 7.6
extern const float FLOAT_80619acc;  // 4.2207
extern const float FLOAT_80619ad0;  // 4.2
extern const float FLOAT_80619ad4;  // 0.9375

// --- frame-enter / frame-leave intrinsics ------------------------------------
// The PPC prologue helpers return the caller's stack/scratch pointers as a
// register pair (r3:r4); we model that as a 64-bit value whose high word is the
// scratch base pointer used by the function body.
extern unsigned long long FUN_803826c4(void);
extern unsigned long long FUN_803826d4(void);
extern void FUN_80382710(void);
extern void FUN_80382720(void);

extern unsigned int rng_counter_tick(void);

// -----------------------------------------------------------------------------
// snake3_grid_locate_cell - find which grid cell contains world point (x,y).
// Scans up to 5 rows (0x118 stride) then up to 7 columns (0x28 stride),
// testing the cell's min/max Y then min/max X. Writes (row,col) to outRowCol
// when a containing cell is found; leaves it untouched otherwise.
// -----------------------------------------------------------------------------
void snake3_grid_locate_cell(double x, double y, int cellsBase, int *outRowCol)
{
    bool foundCol = false;
    bool foundRow = false;
    int  col = 0;
    int  row = 0;
    int  rowOff = 0;

    while (row < 5 && !foundRow) {
        int rowBase = cellsBase + rowOff;
        if (((double)*(float *)(rowBase + 4) < y) &&
            (y < (double)*(float *)(rowBase + 0xc))) {
            foundRow = true;
            col = 0;
            int colOff = 0;
            while (col < 7 && !foundCol) {
                if (((double)*(float *)(rowBase + colOff) < x) &&
                    (x < (double)((float *)(rowBase + colOff))[2])) {
                    foundCol = true;
                }
                col = col + 1;
                colOff = colOff + 0x28;
            }
        }
        row = row + 1;
        rowOff = rowOff + 0x118;
    }

    if (!foundCol) {
        return;
    }
    outRowCol[0] = row - 1;
    outRowCol[1] = col - 1;
}

// -----------------------------------------------------------------------------
// snake3_grid_cell_init - initialize one 10-float cell rectangle from its
// bottom-left corner (x,y). Derives max corner via the fixed step, the center
// from the midpoint factor, and stores the fixed half-extents.
// -----------------------------------------------------------------------------
void snake3_grid_cell_init(double x, double y, float *cell)
{
    double stepX = (double)FLOAT_80619aa8;
    double stepY = (double)FLOAT_80619aac;
    float  halfW = FLOAT_80619ab0;
    float  halfH = FLOAT_80619ab4;
    float  mid   = FLOAT_80619ab8;

    cell[0] = (float)x;                       // minX
    cell[1] = (float)y;                       // minY
    cell[2] = (float)(stepX + x);             // maxX
    cell[3] = (float)(stepY + y);             // maxY
    cell[6] = halfW;                          // halfW
    cell[7] = halfH;                          // halfH
    cell[4] = mid * (cell[0] + cell[2]);      // centerX
    cell[5] = mid * (cell[1] + cell[3]);      // centerY
    cell[9] = 0.0f;                           // flagB
}

// -----------------------------------------------------------------------------
// snake3_grid_build - lay out the whole 5x7 grid plus the surrounding edge and
// corner "extra" cells, then classify each cell by its resulting width/height
// so wide/tall cells are marked and the usable cells are collected.
//
// cellsBase   : base of the 5x7 grid cell array (0x28 stride, 0x118 per row)
// usableList  : int[] filled with pointers to usable cells (flagA==0)
// extraCells  : base of the appended edge/corner cell records (0x28 stride)
// outLow/outHigh: two-int scratch outputs (bounds), reset to -1 here
// The origin (originX,originY) comes from the frame-enter scratch pointer; the
// original reads it from an incoming register pair, modelled here from the
// resolved base. The unrecoverable f1 origin is taken as 0 (documented).
// -----------------------------------------------------------------------------
void snake3_grid_build(double originX, double originY, int usableList, int extraCells,
                       int cellsBase, int *outLow, int *outHigh)
{
    unsigned long long framePair = FUN_803826c4();
    int   gridBase   = (int)(framePair >> 0x20);
    float *summary   = (float *)(unsigned int)framePair;

    double stepY   = (double)FLOAT_80619aac;
    double midF    = (double)FLOAT_80619ab8;
    double halfHd  = (double)FLOAT_80619ab4;

    int usableCount = 0;
    int usableOff   = 0;
    int extraCount  = 0;
    int extraOff    = 0;
    unsigned int rowIdx = 0;
    int rowOff  = 0;

    double curX = 0.0;   // origin X (from register scratch; modelled 0)
    double curY = originY;

    outLow[0]  = -1;
    outLow[1]  = -1;
    outHigh[0] = -1;
    outHigh[1] = -1;

    double lastX = curX;
    double maxY  = curY;

    do {
        double rowSpan;
        if (rowIdx == 2) {
            rowSpan = (double)FLOAT_80619abc;          // 13.0 (center row)
        } else if (((rowIdx & 1) ^ (rowIdx >> 0x1f)) == (rowIdx >> 0x1f)) {
            rowSpan = (double)FLOAT_80619ac0;          // 10.05 (even rows)
        } else {
            rowSpan = (double)FLOAT_80619aac;          // 3.2
        }

        double rowTop  = (double)(float)(curY + rowSpan);
        double rowCenY = (double)(float)(midF * rowSpan);
        double rowInY  = (double)(float)(rowTop - halfHd);
        unsigned int colIdx = 0;
        int colOff = 0;
        double colX = lastX;

        do {
            float *cell = (float *)(gridBase + rowOff + colOff);
            cell[0] = (float)colX;    // minX
            cell[1] = (float)curY;    // minY

            if (stepY == rowSpan) {
                if (colIdx == 0) {
                    snake3_grid_cell_init((double)(float)(colX - (double)FLOAT_80619ab4), curY,
                                          (float *)(extraCells + extraOff));
                    extraCount = extraCount + 1;
                    extraOff   = extraOff + 0x28;
                } else if (colIdx == 6) {
                    snake3_grid_cell_init((double)((float)((double)FLOAT_80619ac4 + colX) - FLOAT_80619ab4),
                                          curY, (float *)(extraCells + extraOff));
                    extraCount = extraCount + 1;
                    extraOff   = extraOff + 0x28;
                }
            }

            double colSpan;
            if ((colIdx == 2) || (colIdx == 4)) {
                colX = (double)(float)(colX + (double)FLOAT_80619ac8);   // 7.6
            } else if (((colIdx & 1) ^ (colIdx >> 0x1f)) == (colIdx >> 0x1f)) {
                colX = (double)(float)(colX + (double)FLOAT_80619ac4);   // 9.2
            } else {
                if (rowIdx == 0) {
                    snake3_grid_cell_init(colX, (double)(float)(curY - (double)FLOAT_80619ab4),
                                          (float *)(extraCells + extraOff));
                    extraCount = extraCount + 1;
                    extraOff   = extraOff + 0x28;
                }
                if (rowIdx == 4) {
                    snake3_grid_cell_init(colX, rowInY, (float *)(extraCells + extraOff));
                    extraCount = extraCount + 1;
                    extraOff   = extraOff + 0x28;
                }
                colX = (double)(float)(colX + (double)FLOAT_80619aa8);   // 3.2205
            }
            (void)colSpan;

            cell[2] = (float)colX;      // maxX
            colIdx  = colIdx + 1;
            cell[3] = (float)rowTop;    // maxY
            colOff  = colOff + 0x28;
            cell[6] = (float)(midF * (double)(cell[2] - cell[0]));  // halfW
            cell[7] = (float)rowCenY;                               // halfH
            cell[4] = (float)(midF * (double)(cell[0] + cell[2]));  // centerX
            cell[5] = (float)(midF * (double)(cell[1] + cell[3]));  // centerY
            cell[9] = 0.0f;
        } while ((int)colIdx < 7);

        rowIdx = rowIdx + 1;
        rowSpan = curY + rowSpan;
        curY    = (double)(float)rowSpan;
        maxY    = rowSpan;
        lastX   = colX;                 // final maxX from this row's last cell
        rowOff  = rowOff + 0x118;
    } while ((int)rowIdx < 5);

    // Classify each cell by resulting width/height; collect usable cells.
    float mergeW = FLOAT_80619acc;
    int classRowOff = 0;
    int classRow = 0;
    do {
        int classColOff = 0;
        int colsLeft = 7;
        do {
            float *cell = (float *)(gridBase + classRowOff + classColOff);
            if (cell[2] - cell[0] <= mergeW) {
                if (cell[3] - cell[1] <= FLOAT_80619ad0) {
                    usableCount = usableCount + 1;
                    cell[8] = 1;                       // flagA: usable
                    *(float **)(usableList + usableOff) = cell;
                    usableOff = usableOff + 4;
                } else {
                    cell[8] = 2;
                }
            } else if (cell[3] - cell[1] <= FLOAT_80619ad0) {
                cell[8] = 3;
            } else {
                cell[8] = 0;
            }
            classColOff = classColOff + 0x28;
            colsLeft = colsLeft - 1;
        } while (colsLeft != 0);
        classRow = classRow + 1;
        classRowOff = classRowOff + 0x118;
    } while (classRow < 5);

    // Nudge the boundary cell centers outward by the fixed insets.
    float half = FLOAT_80619ab8;
    int off, left;
    off = 0; left = 7;
    do { int o = off + 0x14; off = off + 0x28; *(float *)(gridBase + o) = *(float *)(gridBase + o) + half; left = left - 1; } while (left != 0);
    off = 0; left = 7;
    do { int o = off + 0x474; off = off + 0x28; *(float *)(gridBase + o) = *(float *)(gridBase + o) - half; left = left - 1; } while (left != 0);
    float insetY = FLOAT_80619ad4;
    off = 0; left = 5;
    do { int o = off + 0x10; off = off + 0x118; *(float *)(gridBase + o) = *(float *)(gridBase + o) + insetY; left = left - 1; } while (left != 0);
    off = 0; left = 5;
    do { int o = off + 0x100; off = off + 0x118; *(float *)(gridBase + o) = *(float *)(gridBase + o) - insetY; left = left - 1; } while (left != 0);

    // Fill the whole-grid summary rectangle at the scratch base.
    summary[0] = (float)curX;
    summary[1] = (float)originY;
    summary[2] = (float)lastX;                       // final maxX
    summary[3] = (float)maxY;
    float summHalf = FLOAT_80619ab8;
    summary[4] = summHalf * (summary[0] + summary[2]);
    summary[5] = summHalf * (summary[1] + summary[3]);
    summary[6] = summHalf * (summary[2] - summary[0]);
    summary[7] = summHalf * (summary[3] - summary[1]);
    summary[9] = 2;
    summary[8] = 4;

    // Append the four outer corner cells after the collected edge cells.
    snake3_grid_cell_init((double)(float)(curX - (double)FLOAT_80619ab0),
                          (double)(float)(originY - (double)FLOAT_80619ab4),
                          (float *)(extraCells + extraCount * 0x28));
    snake3_grid_cell_init((double)(float)(curX - (double)FLOAT_80619ab0),
                          (double)(float)(curY - (double)FLOAT_80619ab4),
                          (float *)(extraCells + (extraCount + 1) * 0x28));
    snake3_grid_cell_init((double)(float)(lastX - (double)FLOAT_80619ab0),
                          (double)(float)(originY - (double)FLOAT_80619ab4),
                          (float *)(extraCells + (extraCount + 2) * 0x28));
    snake3_grid_cell_init((double)(float)(lastX - (double)FLOAT_80619ab0),
                          (double)(float)(curY - (double)FLOAT_80619ab4),
                          (float *)(extraCells + (extraCount + 3) * 0x28));

    // Append pointers to the 14 edge/corner extra cells into the usable list.
    int listIdx = usableCount << 2;
    int eOff = 0;
    int eLeft = 14;
    do {
        int ptr = extraCells + eOff;
        eOff = eOff + 0x28;
        *(int *)(usableList + listIdx) = ptr;
        listIdx = listIdx + 4;
        eLeft = eLeft - 1;
    } while (eLeft != 0);

    FUN_80382710();
}

// -----------------------------------------------------------------------------
// snake3_grid_neighbor_down - adjacency step toward the near/far row.
// Returns a relationship code (0/-1/2) versus the target row selection.
// -----------------------------------------------------------------------------
int snake3_grid_neighbor_down(int cellsBase, int *fromCoord, int *outCenter, int *outCoord)
{
    int targetRow = 4;
    if (fromCoord[0] < 2) {
        targetRow = 0;
    }
    int rowBase = cellsBase + targetRow * 0x118;
    outCenter[0] = *(int *)(rowBase + fromCoord[1] * 0x28 + 0x10);
    outCenter[1] = *(int *)(rowBase + fromCoord[1] * 0x28 + 0x14);
    outCoord[0]  = targetRow;
    outCoord[1]  = fromCoord[1];
    if (fromCoord[0] < targetRow)  { return 0; }
    if (fromCoord[0] <= targetRow) { return -1; }
    return 2;
}

// -----------------------------------------------------------------------------
// snake3_grid_neighbor_right - adjacency step to the rightmost column (6).
// -----------------------------------------------------------------------------
int snake3_grid_neighbor_right(int cellsBase, int *fromCoord, int *outCenter, int *outCoord)
{
    outCenter[0] = *(int *)(cellsBase + fromCoord[0] * 0x118 + 0x100);
    outCenter[1] = *(int *)(cellsBase + fromCoord[0] * 0x118 + 0x104);
    outCoord[0]  = fromCoord[0];
    outCoord[1]  = 6;
    if (fromCoord[1] < 6) { return 3; }
    if (fromCoord[1] < 7) { return -1; }
    return 1;
}

// -----------------------------------------------------------------------------
// snake3_grid_neighbor_up - adjacency step to row 2 (the center band).
// -----------------------------------------------------------------------------
int snake3_grid_neighbor_up(int cellsBase, int *fromCoord, int *outCenter, int *outCoord)
{
    outCenter[0] = *(int *)(cellsBase + fromCoord[1] * 0x28 + 0x240);
    outCenter[1] = *(int *)(cellsBase + fromCoord[1] * 0x28 + 0x244);
    outCoord[0]  = 2;
    outCoord[1]  = fromCoord[1];
    if (fromCoord[0] < 2) { return 0; }
    if (fromCoord[0] < 3) { return -1; }
    return 2;
}

// -----------------------------------------------------------------------------
// snake3_grid_neighbor_left - adjacency step to column 3 (the center column).
// -----------------------------------------------------------------------------
int snake3_grid_neighbor_left(int cellsBase, int *fromCoord, int *outCenter, int *outCoord)
{
    outCenter[0] = *(int *)(cellsBase + fromCoord[0] * 0x118 + 0x88);
    outCenter[1] = *(int *)(cellsBase + fromCoord[0] * 0x118 + 0x8c);
    outCoord[0]  = fromCoord[0];
    outCoord[1]  = 3;
    if (fromCoord[1] < 3) { return 3; }
    if (fromCoord[1] < 4) { return -1; }
    return 1;
}

// -----------------------------------------------------------------------------
// snake3_grid_mark_reachable - resolve two world endpoints to grid coords, clear
// the occupancy grid, then flag a plus-shaped 1-ring of reachable cells around
// every active endpoint record.
//
// ctx is the boss control block base; the occupancy grid lives at ctx+0x990,
// the two resolved endpoint coord pairs at ctx+0x118c / ctx+0x1194, and the
// eight endpoint records at ctx+0x119c (0x168 stride: +0xec row, +0xf0 col,
// +0xf8 active flag).
// -----------------------------------------------------------------------------
void snake3_grid_mark_reachable(double x0, double y0, double x1, double y1, int ctx)
{
    snake3_grid_locate_cell(x0, y0, ctx + 0x96c, (int *)(ctx + 0x118c));
    snake3_grid_locate_cell(x1, y1, ctx + 0x96c, (int *)(ctx + 0x1194));

    // Clear the 5x7 occupancy grid at ctx+0x990.
    int rowOff = 0;
    int row = 0;
    do {
        int colOff = 0;
        int colsLeft = 7;
        do {
            int o = colOff + 0x990;
            colOff = colOff + 0x28;
            *(int *)(ctx + rowOff + o) = 0;
            colsLeft = colsLeft - 1;
        } while (colsLeft != 0);
        row = row + 1;
        rowOff = rowOff + 0x118;
    } while (row < 5);

    // For each active endpoint record, flag its cell and a plus-shaped 1-ring.
    int recOff = 0;
    int recLeft = 8;
    do {
        int rec = ctx + 0x119c + recOff;
        if (*(int *)(rec + 0xf8) != 0) {
            int recRow = *(int *)(rec + 0xec);
            int recCol = *(int *)(rec + 0xf0);
            *(int *)(ctx + recRow * 0x118 + recCol * 0x28 + 0x990) = 1;

            int r = recRow - 1;
            int rOff = r * 0x118;
            for (; r < recRow + 2; r = r + 1) {
                if ((-1 < r) && (r < 5)) {
                    *(int *)(ctx + rOff + recCol * 0x28 + 0x990) = 1;
                }
                rOff = rOff + 0x118;
            }
            int c = recCol - 1;
            int cOff = c * 0x28;
            for (; c < recCol + 2; c = c + 1) {
                if ((-1 < c) && (c < 7)) {
                    *(int *)(ctx + recRow * 0x118 + cOff + 0x990) = 1;
                }
                cOff = cOff + 0x28;
            }
        }
        recOff = recOff + 0x168;
        recLeft = recLeft - 1;
    } while (recLeft != 0);
}

// -----------------------------------------------------------------------------
// snake3_grid_coord_compare - classify the relationship between two (row,col)
// coordinates into a direction code:
//   same row  -> 1 (b col >= a col) or 3 (b col < a col)
//   same col  -> 2 or 0 depending on row order
//   otherwise -> -1 (not axis-aligned)
// -----------------------------------------------------------------------------
unsigned int snake3_grid_coord_compare(unsigned int *a, unsigned int *b)
{
    unsigned int bRow = b[0];
    unsigned int aRow = a[0];
    if (bRow == aRow) {
        unsigned int code = 1;
        if ((int)b[1] < (int)a[1]) {
            code = 3;
        }
        return code;
    }
    if (b[1] == a[1]) {
        return ~(-((unsigned int)(bRow < aRow) - ((int)bRow >> 0x1f)) - ((int)aRow >> 0x1f)) & 2;
    }
    return 0xffffffff;
}

// -----------------------------------------------------------------------------
// snake3_grid_pick_free_neighbor - collect the cells inside a clamped 5x5 window
// around the source coord, then pick a target cell: if directed==0, walk the
// window looking for the first axis-aligned non-blocked neighbor; otherwise (or
// on failure) pick a random unoccupied cell. Writes the chosen cell's world
// center to outCenter, its (row,col) to outCoord, and the squared distance from
// the source center to outDist2.
//
// The window base and the source coord come from the frame-enter scratch pair.
// -----------------------------------------------------------------------------
void snake3_grid_pick_free_neighbor(int arg0, int arg1, int *srcCoord, int *outCenter,
                                    int *outCoord, float *outDist2, int directed)
{
    (void)arg0; (void)arg1;
    unsigned long long framePair = FUN_803826d4();
    int  gridBase = (int)(framePair >> 0x20);
    int  srcRow   = *(int *)(unsigned int)framePair;
    int  srcCol   = ((int *)(unsigned int)framePair)[1];

    int rowLo = srcRow - 2;
    int rowHi = srcRow + 3;
    int colLo = srcCol - 2;
    int colHi = srcCol + 3;

    int windowCells[25];   // pointers to the cells inside the window
    int windowCoord[61];   // (row,col) pairs (2 ints per cell)

    int cellCount = 0;     // *4 offset into windowCells
    int coordOff  = 0;     // byte offset into windowCoord

    if (rowLo < 0) { rowLo = 0; rowHi = 5; }
    else if (5 < rowHi) { rowLo = 0; rowHi = 5; }
    if (colLo < 0) { colLo = 0; colHi = 5; }
    else if (7 < colHi) { colLo = 2; colHi = 7; }

    int rowOff = rowLo * 0x118;
    for (int r = rowLo; r < rowHi; r = r + 1) {
        int colOff = colLo * 0x28;
        int cOffLocal = coordOff;
        int c = colLo;
        int remaining = colHi - colLo;
        if (colLo < colHi) {
            do {
                *(int *)((int)windowCells + cellCount) = gridBase + rowOff + colOff;
                coordOff = cOffLocal + 8;
                cellCount = cellCount + 4;
                *(int *)((int)windowCoord + cOffLocal) = r;
                colOff = colOff + 0x28;
                *(int *)((int)windowCoord + cOffLocal + 4) = c;
                c = c + 1;
                remaining = remaining - 1;
                cOffLocal = coordOff;
            } while (remaining != 0);
        }
        rowOff = rowOff + 0x118;
    }

    int guard = 0;
    unsigned int pick = 0xffffffff;
    if (directed == 0) {
        int cellIdx = 0;
        int coordByte = 0;
        int scan = 0x19;
        unsigned int idx = 0;
        do {
            pick = idx;
            int cRow = *(int *)((int)windowCoord + coordByte);
            int cCol = *(int *)((int)windowCoord + coordByte + 4);
            int dRow = cRow - srcCoord[0];
            int dCol = cCol - srcCoord[1];
            if ((((1 < dRow) || (dRow < -1)) || (1 < dCol) || (dCol < -1)) &&
                (((srcCoord[0] == cRow) || (srcCoord[1] == cCol)) &&
                 ((*(int *)(*(int *)((int)windowCells + cellIdx) + 0x20) != 1) &&
                  (*(int *)(*(int *)((int)windowCells + cellIdx) + 0x24) != 1)))) {
                break;
            }
            idx = pick + 1;
            cellIdx = cellIdx + 4;
            coordByte = coordByte + 8;
            scan = scan - 1;
            pick = 0xffffffff;
        } while (scan != 0);
    }

    if (pick == 0xffffffff) {
        pick = rng_counter_tick();
        while (true) {
            pick = pick % 0x19;
            if (((*(int *)(windowCells[pick] + 0x20) != 1) &&
                 (*(int *)(windowCells[pick] + 0x24) != 1)) || (0x18 < guard)) {
                break;
            }
            pick = rng_counter_tick();
            guard = guard + 1;
        }
    }

    if (guard < 0x19) {
        int srcCX = srcCoord[0];
        int chosenRow = windowCoord[pick * 2];
        int chosenCol = windowCoord[pick * 2 + 1];
        if ((srcCX < 0) || (srcCoord[1] < 0)) {
            int cellPtr = gridBase + chosenRow * 0x118 + chosenCol * 0x28;
            outCenter[0] = *(int *)(cellPtr + 0x10);
            outCenter[1] = *(int *)(cellPtr + 0x14);
        } else {
            int srcColB = srcCoord[1];
            int colBytes = chosenCol * 0x28;
            int rowPtr   = gridBase + chosenRow * 0x118;
            int cellFlag = *(int *)(rowPtr + colBytes + 0x20);
            if (cellFlag == 2) {
                outCenter[0] = *(int *)(rowPtr + srcColB * 0x28 + 0x10);
                outCenter[1] = *(int *)(rowPtr + srcCoord[1] * 0x28 + 0x14);
            } else if (cellFlag == 3) {
                outCenter[0] = *(int *)(gridBase + srcCX * 0x118 + colBytes + 0x10);
                outCenter[1] = *(int *)(gridBase + srcCoord[0] * 0x118 + colBytes + 0x14);
            } else if ((chosenRow - srcCX) * (chosenRow - srcCX) <
                       (chosenCol - srcColB) * (chosenCol - srcColB)) {
                outCenter[0] = *(int *)(rowPtr + srcColB * 0x28 + 0x10);
                outCenter[1] = *(int *)(rowPtr + srcCoord[1] * 0x28 + 0x14);
            } else {
                outCenter[0] = *(int *)(gridBase + srcCX * 0x118 + colBytes + 0x10);
                outCenter[1] = *(int *)(gridBase + srcCoord[0] * 0x118 + colBytes + 0x14);
            }
        }
        outCoord[0] = chosenRow;
        outCoord[1] = chosenCol;

        int chosenPtr = gridBase + chosenRow * 0x118 + chosenCol * 0x28;
        int srcPtr    = gridBase + srcCoord[0] * 0x118 + srcCoord[1] * 0x28;
        float dy = *(float *)(srcPtr + 0x14) - *(float *)(chosenPtr + 0x14);
        float dx = *(float *)(srcPtr + 0x10) - *(float *)(chosenPtr + 0x10);
        *outDist2 = dx * dx + dy * dy;
    }

    FUN_80382720();
}

} // extern "C"
