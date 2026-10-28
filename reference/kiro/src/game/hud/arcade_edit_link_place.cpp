// Batch: batch_972 | Module: arcade/challenge editor pad-node link placement + endpoint solving
// Address range: 0x80343c4c - 0x8034545c
// Date: 2026-09-12
//
// The arcade/challenge editor's link-placement driver, built directly on top of batch_971's
// node-vertex spatial-query subsystem (node_vert_at_coord / node_global_vert_base /
// node_nearest_vert_by_edgeclass / node_pick_two_nearest_verts / coord_rotate_to_world_offset).
// When the user drops a link/connector between pad grid cells, these routines walk the node grid,
// resolve the two vertex endpoints of the link, honor the node's rotation, and mark the placement
// valid once both endpoints resolve to real global vertex indices (>1000).
//
// Node grid layout (unchanged from batch_971): a node descriptor table at DAT_8047c15c.. holds the
// per-node extents (x count +0x14, y count +0x18, z count +0x1c) and the per-z-layer cell-flag plane
// pointers at +0x20; each cell flag word carries bit 0x400000 (vertex present), 0x1000 (walkable),
// 0x40 (edge), 0x800000 (blocked). Direction codes 1..4 map to -Y/+X/+Y/-X unit steps (grid_cell_in_direction),
// and the higher-level pick driver switches on edge-direction codes 1..14.
//
//   node_count_flagged_verts    0x80343c4c  count vertices carrying the 0x400000 flag across a node grid.
//   coord_rotate_ccw_wrap       0x80343cfc  copy an int[3] coord to short, rotate CCW, write back.
//   node_place_companion_vert   0x80343d60  walk down a grid column placing the companion link vertex.
//   grid_cell_in_direction      0x803441d0  step a coord one cell in a direction code, read that cell flag.
//   node_link_adjacent_cells    0x80344344  try all four directions, placing links to adjacent cells.
//   node_link_all_flagged_verts 0x80344528  scan a whole node grid, link every flagged vertex.
//   node_probe_vert_below       0x8034463c  height-gate, then grid-lookup + roster hit-test one vertex.
//   pad_compute_link_endpoints  0x80344710  switch on edge direction, solve both link endpoint verts.
//   node_find_or_hittest_pair   0x803450bc  find a coord in the node table, else roster-hittest two offsets.
//   link_endpoints_valid        0x8034545c  true when both endpoint vertex indices are real (>1000).
//
// Callers: aveditor_place_all_segments, map_build_connectivity, and the editor cursor placement path.
//
// NON-MATCHING: reproduces the decompiler's structure. Paired-single register spills (in_f26..f31 /
// in_ps26_1..ps31_1) and the incoming-register doubles (dVar/extraout_f1) are expressed as plain
// float/double parameters and locals per Compile-Clean rule 3; every branch predicate is encoded
// literally (rule 2). The int->double magic-constant conversions (CONCAT44 with 0x43300000 minus
// DOUBLE_8061ac00) and the quarter-turn rotation arithmetic are reproduced verbatim. Scope push/pop
// helpers (FUN_803826dc/e0/f0 push, FUN_80382728/3c pop) and __save_gpr_25/28 thread the node record
// through the spilled high word. Raw FUN_/DAT_/FLOAT_ symbols are declared file-locally with
// call-site-accurate arity (rule 5); named cross-module helpers and the batch_971 node-vertex
// routines resolve through the shared headers (rule 6). CONCAT44/ABS come from common.h.

#include "common.h"

extern "C" {

// ---- node descriptor / grid-flag tables -----------------------------------
extern int    DAT_8047c15c;   // node descriptor: z-layer stride (per-node record, 0x140 apart)
extern int    DAT_8047c160;   // node descriptor: y count
extern int    DAT_8047c164;   // node descriptor: x count
extern int    PTR_DAT_8047c168; // node descriptor: per-z-layer flag-plane pointer table
extern int    DAT_8047c178;   // node descriptor: has-cells flag
extern int    DAT_805d09ec;   // vertex buffer table base (node*4)
extern int    DAT_805d0938;   // index buffer table base (node*4)
extern int    DAT_80612be4;   // editor context base (node/link table at +0x306e/+0x3070)

// ---- direction unit-step defaults (loaded when dir code is out of 1..4) ----
extern int    DAT_803e0b90;   // default step X
extern int    DAT_803e0b94;   // default step Y
extern int    DAT_803e0b98;   // default step Z
extern int    DAT_803e0b9c;   // adjacent-cell seed X
extern int    DAT_803e0ba0;   // adjacent-cell seed Y
extern int    DAT_803e0ba4;   // adjacent-cell seed Z
extern int    DAT_803e0ba8;   // world-offset seed X
extern int    DAT_803e0bac;   // world-offset seed Y
extern int    DAT_803e0bb0;   // world-offset seed Z

// ---- world-space voxel layout constants ------------------------------------
extern double DOUBLE_8061ac00; // int->double magic bias
extern float  FLOAT_8061ac08;  // Y cell size
extern float  FLOAT_8061ac10;  // Z half offset (one layer)
extern float  FLOAT_8061ac14;  // height tolerance
extern float  FLOAT_8061ac20;  // half-cell world offset
extern float  FLOAT_8061ac28;  // min height gate
extern float  FLOAT_8061ac2c;  // layer-1 z offset
extern float  FLOAT_8061ac30;  // min endpoint height delta
extern float  FLOAT_8061ac34;  // max endpoint height delta

// ---- named cross-module helpers --------------------------------------------
extern unsigned int  aveditor_grid_lookup(int hi, int coord, int lo);
extern unsigned int  aveditor_place_companion_segments(void);
extern void          aveditor_rotate_piece_ccw(int node, short* coord);
extern void          aveditor_rotate_piece_cw(int node, short* coord);
extern unsigned int  pad_node_flag_type6(int node, unsigned int flags);
extern unsigned int  pad_node_blocked_or_type6(int node, unsigned int flags);
extern unsigned char pad_node_flag_bit1(int node);
extern unsigned int  pad_link_matches_offset_type21(float* node, float* coord);
extern unsigned int  roster_hit_test_point(double x, double y, double z);

// ---- batch_971 node-vertex subsystem (resolve via shared headers) ----------
extern int           node_vert_at_coord(int node, unsigned int x, unsigned int y, unsigned int z);
extern unsigned short node_global_vert_base(void);
extern void          node_nearest_vert_by_edgeclass(double dx, double dy, double dz, int node,
                                                    unsigned int edgeClass, unsigned int seed,
                                                    float* outBest);
extern void          node_pick_two_nearest_verts(double dx, double dy, double dz, float* node,
                                                 unsigned int dir, unsigned short* out);
extern void          coord_rotate_to_world_offset(int node, short* rot, float* coord);

// ---- scope push/pop with local call-site signatures ------------------------
extern unsigned long long FUN_803826dc(void);   // push (returns node record | hi word)
extern unsigned long long FUN_803826f0(void);   // push
extern void  FUN_80382728(void);                 // pop
extern void  FUN_8038273c(void);                 // pop
extern unsigned long long __save_gpr_25(void);
extern void  __restore_gpr_25(void);
extern unsigned long long __save_gpr_28(void);
extern void  __restore_gpr_28(void);

// ---- forward decls within this TU ------------------------------------------
void coord_rotate_ccw_wrap(int* coord);
void node_place_companion_vert(void);
unsigned int grid_cell_in_direction(int desc, int* inCoord, int* outCoord, int dir);
void node_link_adjacent_cells(unsigned int a, unsigned int nodeCtx, int hi, int* coord);
int  node_probe_vert_below(void);
int  link_endpoints_valid(unsigned short* endpoints);

// ---------------------------------------------------------------------------
// 0x80343c4c : count vertices carrying the 0x400000 flag across a node grid
// ---------------------------------------------------------------------------
int node_count_flagged_verts(int node)
{
    int descOff;
    int count;
    int planeIdx;
    int zLayer;
    int yCell;
    int stride;
    int i;
    int rowBase;
    int cellIdx;

    descOff = node * 0x140;
    count = 0;
    planeIdx = 0;
    for (zLayer = 0; zLayer < *(int*)(&DAT_8047c164 + descOff); zLayer = zLayer + 1) {
        for (yCell = 0; yCell < *(int*)(&DAT_8047c160 + descOff); yCell = yCell + 1) {
            stride = *(int*)(&DAT_8047c15c + descOff);
            i = 0;
            rowBase = yCell * stride;
            if (0 < stride) {
                do {
                    cellIdx = i + rowBase;
                    i = i + 1;
                    count = count +
                            (-(*(unsigned int*)(*(int*)((int)&PTR_DAT_8047c168 + planeIdx + descOff) +
                                                cellIdx * 4) >> 0x16 & 1) & 2);
                    stride = stride + -1;
                } while (stride != 0);
            }
        }
        planeIdx = planeIdx + 4;
    }
    return count;
}

// ---------------------------------------------------------------------------
// 0x80343cfc : copy an int[3] coord to short, rotate CCW, write back
// ---------------------------------------------------------------------------
void coord_rotate_ccw_wrap(int* coord)
{
    int   node;   // arrives in r4
    short c0;
    short c1;
    short c2;

    node = 0;
    c0 = (short)*coord;
    c1 = (short)coord[1];
    c2 = (short)coord[2];
    aveditor_rotate_piece_ccw(node, &c0);
    *coord = (int)c0;
    coord[1] = (int)c1;
    coord[2] = (int)c2;
}

// ---------------------------------------------------------------------------
// 0x80343d60 : walk down a grid column placing the companion link vertex
// ---------------------------------------------------------------------------
void node_place_companion_vert(void)
{
    bool matched;
    int  hi;
    float* rec;
    unsigned int flags;
    unsigned short vbase;
    int iLayer;
    int iCol;
    int rowStep;
    unsigned int cell;
    int step;
    double px, py, pz;
    unsigned long long saved;

    // incoming grid position carried in paired-single lanes at entry
    double in_px = 0.0, in_py = 0.0, in_pz = 0.0;

    unsigned int c8, c4, c0, bc, b8, b4, b0, ac, a8;
    int a4, a0, nine_c;

    saved = __save_gpr_28();
    hi = (int)((unsigned long long)saved >> 0x20);
    rec = (float*)saved;
    px = (double)*rec;
    pz = (double)(rec[2] - FLOAT_8061ac10);
    py = (double)rec[1];
    matched = false;
    rec = (float*)aveditor_grid_lookup(hi, (int)rec, -0x7fc20000);
    flags = aveditor_place_companion_segments();
    flags = pad_node_flag_type6((int)rec[4], flags);
    while ((!matched && rec != (float*)0) && flags != 0) {
        nine_c = (int)(pz - (double)rec[2]);
        a4 = (int)(px - (double)*rec);
        a0 = (int)(py - (double)rec[1]);
        iLayer = nine_c + 1;
        rowStep = nine_c << 2;
        if (-1 < nine_c) {
            do {
                cell = *(unsigned int*)((a4 + a0 * *(int*)((int)rec[(int)rec[3] + 0xb] + 0x14)) * 4 +
                                        *(int*)((int)rec[(int)rec[3] + 0xb] + rowStep + 0x20));
                step = 0;
                if ((flags != 0) && ((cell & 0x400000) != 0)) {
                    step = 1;
                }
                if (((cell & 0x1000) == 0) && (step != 0)) {
                    matched = true;
                    break;
                }
                pz = (double)(float)(pz - (double)FLOAT_8061ac10);
                rowStep = rowStep + -4;
                iLayer = iLayer + -1;
            } while (iLayer != 0);
        }
        if (!matched) {
            rec = (float*)aveditor_grid_lookup(flags, rowStep, 0);
            flags = aveditor_place_companion_segments();
            flags = pad_node_flag_type6((int)rec[4], flags);
        }
    }
    if ((rec == (float*)0) ||
        (*(int*)(*(int*)(&DAT_805d09ec + (int)rec[4] * 4) + 4) != 0)) {
        if (matched) {
            bc = (unsigned int)(int)(px - (double)*rec);
            b8 = (unsigned int)(int)(py - (double)rec[1]);
            b4 = (unsigned int)(int)(pz - (double)rec[2]);
            b0 = bc; ac = b8; a8 = b4;
            (void)b0; (void)ac; (void)a8;
            coord_rotate_ccw_wrap((int*)&bc);
            node_vert_at_coord((int)rec[4], bc, b8, b4);
            vbase = node_global_vert_base();
            *(unsigned short*)(hi + 2) = vbase;
        }
        else if (rec != (float*)0) {
            float layerZ = rec[2];
            float plane = rec[(int)rec[3] + 0xb];
            int zc = *(int*)((int)plane + 0x1c);
            int zi = zc + -1;
            double bias0 = (double)(CONCAT44(0x43300000, (int)px) ^ 0x80000000);
            double bias1 = (double)(CONCAT44(0x43300000, (int)py) ^ 0x80000000);
            (void)bias1;
            unsigned int uz = (unsigned int)(layerZ + (float)(bias0 - DOUBLE_8061ac00));
            c8 = (unsigned int)((float)((double)CONCAT44(0x43300000, (int)px ^ 0x80000000) -
                                        DOUBLE_8061ac00) - *rec);
            c4 = (unsigned int)((float)((double)CONCAT44(0x43300000, (int)py ^ 0x80000000) -
                                        DOUBLE_8061ac00) - rec[1]);
            c0 = (unsigned int)((float)((double)(CONCAT44(0x43300000, uz) ^ 0x80000000) -
                                        DOUBLE_8061ac00) - layerZ);
            int rowStep2 = zi * 4;
            if (-1 < zi) {
                do {
                    cell = *(unsigned int*)(*(int*)((int)plane + rowStep2 + 0x20) +
                                            (c8 + c4 * *(int*)((int)plane + 0x14)) * 4);
                    c0 = (unsigned int)((float)((double)(CONCAT44(0x43300000, uz ^ 0x80000000)) -
                                                DOUBLE_8061ac00) - layerZ);
                    if (((cell & 0x1000) == 0) || ((cell & 0x400000) != 0)) break;
                    uz = uz - 1;
                    rowStep2 = rowStep2 + -4;
                    zc = zc + -1;
                } while (zc != 0);
            }
            coord_rotate_ccw_wrap((int*)&c8);
            node_vert_at_coord((int)rec[4], c8, c4, c0);
            vbase = node_global_vert_base();
            *(unsigned short*)(hi + 2) = vbase;
        }
        if (1000 < *(unsigned short*)(hi + 2)) {
            *(int*)(hi + 4) = 1;
        }
    }
    (void)in_px; (void)in_py; (void)in_pz;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x803441d0 : step a coord one cell in a direction code, read that cell flag
// ---------------------------------------------------------------------------
unsigned int grid_cell_in_direction(int desc, int* inCoord, int* outCoord, int dir)
{
    bool  inBounds;
    unsigned int cell;
    int   x;
    int   stepX;
    int   stepY;
    int   stepZ;

    cell = 0;
    inBounds = false;
    stepX = DAT_803e0b90;
    stepY = DAT_803e0b94;
    stepZ = DAT_803e0b98;
    if (dir == 3) {
        stepX = 0;  stepY = 1;  stepZ = 0;
    }
    else if (dir < 3) {
        if (dir == 1) {
            stepX = 0;  stepY = -1;  stepZ = 0;
        }
        else if (0 < dir) {
            stepX = 1;  stepY = 0;  stepZ = 0;
        }
    }
    else if (dir < 5) {
        stepX = -1;  stepY = 0;  stepZ = 0;
    }
    *outCoord = *inCoord + stepX;
    outCoord[1] = inCoord[1] + stepY;
    outCoord[2] = inCoord[2] + stepZ;
    x = *outCoord;
    if (((-1 < x) && (-1 < outCoord[1]) && (-1 < outCoord[2])) &&
        ((x < *(int*)(desc + 0x14) && (outCoord[1] < *(int*)(desc + 0x18))) &&
         (outCoord[2] < *(int*)(desc + 0x1c)))) {
        inBounds = true;
    }
    if (inBounds) {
        cell = *(unsigned int*)(*(int*)(desc + 0x20 + outCoord[2] * 4) +
                                (x + outCoord[1] * *(int*)(desc + 0x14)) * 4);
    }
    return cell;
}

// ---------------------------------------------------------------------------
// 0x80344344 : try all four directions, placing links to adjacent cells
// ---------------------------------------------------------------------------
void node_link_adjacent_cells(unsigned int a, unsigned int nodeCtx, int hi, int* coord)
{
    unsigned int cell;
    int  valid;
    float* ctx;
    unsigned short* rec;
    float plane;
    int  dir;
    unsigned long long scope;
    int  vbase;

    unsigned int c44, c40, c3c;
    unsigned int stack34, stack2c, stack24;
    (void)a; (void)nodeCtx;

    scope = FUN_803826dc();
    ctx = (float*)scope;
    dir = 1;
    plane = ctx[(int)ctx[3] + 0xb];
    do {
        c44 = DAT_803e0b9c;
        c40 = DAT_803e0ba0;
        c3c = DAT_803e0ba4;
        cell = grid_cell_in_direction((int)plane, coord, (int*)&c44, dir);
        rec = (unsigned short*)(hi + (unsigned int)*(unsigned short*)(hi + 0x20) * 8 + 0x28);
        *rec = (unsigned short)((unsigned long long)scope >> 0x20);
        rec[1] = 0;
        if (cell != 0) {
            if (((cell & 0x1000) != 0) && ((cell & 0x40) == 0)) {
                if (ctx[3] != 0.0) {
                    // CONCAT22((short)c44,(short)c40): pack c44 into the high 16 bits,
                    // c40 into the low 16 bits; rotate reads back ._0_2_ (low)->c44,
                    // ._2_2_ (high)->c40 (little-endian short pair).
                    unsigned int rot = ((unsigned int)(unsigned short)(short)c44 << 16) |
                                       (unsigned int)(unsigned short)(short)c40;
                    short r2 = (short)c3c;
                    aveditor_rotate_piece_cw((int)ctx, (short*)&rot);
                    c44 = (unsigned int)(unsigned short)(short)rot;
                    c40 = (unsigned int)(unsigned short)(short)(rot >> 16);
                    c3c = (unsigned int)(unsigned short)r2;
                }
                stack34 = c44 ^ 0x80000000;
                stack2c = c40 ^ 0x80000000;
                stack24 = c3c ^ 0x80000000;
                (void)stack34; (void)stack2c; (void)stack24;
                node_place_companion_vert();
            }
        }
        valid = link_endpoints_valid(rec);
        if (valid != 0) {
            rec[2] = 0;
            rec[3] = 1;
            *(short*)(hi + 0x20) = *(short*)(hi + 0x20) + 1;
        }
        dir = dir + 1;
    } while (dir < 5);
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x80344528 : scan a whole node grid, link every flagged vertex
// ---------------------------------------------------------------------------
void node_link_all_flagged_verts(void)
{
    bool done;
    int  hi;
    unsigned int vbase;
    int  desc;
    int  zStep;
    int  z;
    unsigned int y;
    unsigned int x;
    int  planeOff;
    unsigned long long scope;

    unsigned int l38, l34, l30;

    scope = FUN_803826dc();
    hi = (int)((unsigned long long)scope >> 0x20);
    desc = *(int*)(hi + *(int*)(hi + 0xc) * 4 + 0x2c);
    z = *(unsigned int*)(desc + 0x1c);
    zStep = z << 2;
    while (true) {
        done = z == 0;
        z = z - 1;
        int nextStep = zStep + -4;
        if (done) break;
        planeOff = zStep + 0x1c;
        for (y = 0; zStep = nextStep, (int)y < *(int*)(desc + 0x18); y = y + 1) {
            x = 0;
            while (true) {
                if (*(int*)(desc + 0x14) <= (int)x) break;
                if ((*(unsigned int*)(*(int*)(desc + planeOff) +
                                      (x + y * *(int*)(desc + 0x14)) * 4) & 0x400000) != 0) {
                    l38 = x;
                    l34 = y;
                    l30 = z;
                    coord_rotate_ccw_wrap((int*)&l38);
                    node_vert_at_coord(*(int*)(hi + 0x10), l38, l34, l30);
                    vbase = node_global_vert_base();
                    node_link_adjacent_cells(vbase, (unsigned int)hi, (int)scope, (int*)&l38);
                }
                x = x + 1;
            }
        }
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x8034463c : height-gate, then grid-lookup + roster hit-test one vertex
// ---------------------------------------------------------------------------
int node_probe_vert_below(void)
{
    int   node;
    unsigned int flags;
    float* rec;
    int   in_r5;   // arrives in r5
    double extraout;   // arrives in a paired-single lane
    unsigned long long scope;

    in_r5 = 0;
    extraout = 0.0;
    scope = FUN_803826f0();
    rec = (float*)scope;
    if (FLOAT_8061ac28 < -(float)((double)FLOAT_8061ac08 * (double)rec[2] - extraout)) {
        node = aveditor_grid_lookup((int)((unsigned long long)scope >> 0x20), (int)rec, in_r5);
        if (node != 0) {
            flags = aveditor_place_companion_segments();
            pad_node_flag_type6(*(int*)(node + 0x10), flags);
        }
        roster_hit_test_point((double)*rec, (double)rec[1], (double)(FLOAT_8061ac10 + rec[2]));
    }
    FUN_8038273c();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80344710 : switch on edge direction, solve both link endpoint verts
// ---------------------------------------------------------------------------
void pad_compute_link_endpoints(unsigned long long param_1, double bx, double by,
                                unsigned int param_4, unsigned int param_5,
                                unsigned short* out)
{
    unsigned char bit1;
    unsigned int flags1;
    unsigned int flags2;
    unsigned int flags3;
    float* rec;
    float* rec2;
    unsigned short vbase;
    int  i;
    float* p;
    unsigned short dir;
    unsigned int dirLow;
    float zSeed;
    double dx;
    double extraout;
    double in_f29 = 0.0;
    double in_f30 = 0.0;
    double in_f31 = 0.0;
    unsigned long long saved;
    unsigned long long uv;

    unsigned short f8, f6, f4, f2;
    float f0, ec, e8, e4, e0, dc, d8, d4, d0, cc, c8, c4, c0, bc, b8, b4, b0, ac, a8, a4;
    long long a0v, v90;
    double v98, v88;

    (void)param_1; (void)param_4; (void)param_5;
    extraout = 0.0;
    dx = extraout;

    saved = __save_gpr_25();
    rec = (float*)((unsigned long long)saved >> 0x20);
    dirLow = (unsigned int)saved;
    zSeed_init:
    zSeed = 0.0;
    bit1 = pad_node_flag_bit1((int)rec[4]);
    if (bit1 != 0) {
        zSeed = (float)aveditor_place_companion_segments();
    }
    flags1 = aveditor_place_companion_segments();
    flags1 = pad_node_blocked_or_type6((int)rec[4], flags1);
    if (flags1 == 0) goto done_lbl;
    *out = 0;
    out[1] = 0;
    switch (dirLow & 0xffff) {
    default:
        goto done_lbl;
    case 1:
    case 7:
        dir = 3;
        b8 = (float)((double)FLOAT_8061ac20 + dx);
        a8 = (float)((double)FLOAT_8061ac20 + bx);
        b4 = (float)(bx - (double)FLOAT_8061ac20);
        ac = b8;
        break;
    case 2:
    case 8:
        dir = 4;
        b4 = (float)((double)FLOAT_8061ac20 + bx);
        ac = (float)(dx - (double)FLOAT_8061ac20);
        b8 = (float)((double)FLOAT_8061ac20 + dx);
        a8 = b4;
        break;
    case 3:
    case 9:
        dir = 1;
        b8 = (float)((double)FLOAT_8061ac20 + dx);
        a8 = (float)(bx - (double)FLOAT_8061ac20);
        b4 = (float)((double)FLOAT_8061ac20 + bx);
        ac = b8;
        break;
    case 4:
    case 10:
        dir = 2;
        b4 = (float)((double)FLOAT_8061ac20 + bx);
        ac = (float)((double)FLOAT_8061ac20 + dx);
        b8 = (float)(dx - (double)FLOAT_8061ac20);
        a8 = b4;
        break;
    case 0xb:
    case 0xc:
    case 0xd:
    case 0xe:
        node_pick_two_nearest_verts(dx, bx, by, rec, dirLow, out);
        goto done_lbl;
    }
    b0 = (float)by;
    a4 = b0;
    flags1 = roster_hit_test_point((double)ac, (double)a8, (double)b0);
    flags2 = roster_hit_test_point((double)b8, (double)b4, (double)b0);
    if (((flags1 == 0) || (flags2 == 0) || ((flags1 & 0x4000) != 0)) ||
        (((flags2 & 0x4000) != 0) ||
         (flags3 = pad_link_matches_offset_type21(rec, &ac), (flags3 & 0xff) != 0)))
        goto done_lbl;

    dir = (unsigned short)saved;
    if (((flags1 & 0x1000) == 0) || ((flags1 & 0x40) != 0)) {
        d0 = (float)(dx - (double)*rec);
        cc = (float)(bx - (double)rec[1]);
        c8 = (float)(by - (double)rec[2]);
        f4 = dir;
        if (rec[3] != 0.0) {
            coord_rotate_to_world_offset((int)rec, (short*)&f4, &d0);
        }
        bit1 = pad_node_flag_bit1((int)rec[4]);
        if ((bit1 != 0) && ((flags1 & 0x400000) != 0)) {
            c8 = c8 + FLOAT_8061ac20;
        }
        node_nearest_vert_by_edgeclass((double)d0, (double)cc, (double)c8, (int)rec[4],
                                       (unsigned int)f4, (unsigned int)zSeed, &ec);
        uv = node_global_vert_base();
        *out = (unsigned short)((unsigned long long)uv >> 0x20);
    }
    else {
        flags1 = roster_hit_test_point((double)ac, (double)a8, (double)(a4 - FLOAT_8061ac10));
        if (((flags1 & 0xf00) == 0) ||
            (((((dirLow & 0xffff) != 1 || ((flags1 & 0x200) == 0)) &&
               (((dirLow & 0xffff) != 3 || ((flags1 & 0x800) == 0)))) &&
              ((((dirLow & 0xffff) != 2 || ((flags1 & 0x400) == 0)) &&
                (((dirLow & 0xffff) != 4 || ((flags1 & 0x100) == 0)))))))) goto done_lbl;
        c4 = (float)(dx - (double)*rec);
        c0 = (float)(bx - (double)rec[1]);
        bc = (float)(by - (double)rec[2]) - FLOAT_8061ac2c;
        f2 = dir;
        if (rec[3] != 0.0) {
            coord_rotate_to_world_offset((int)rec, (short*)&f2, &c4);
        }
        node_nearest_vert_by_edgeclass((double)c4, (double)c0, (double)bc, (int)rec[4],
                                       (unsigned int)f2, (unsigned int)zSeed, &ec);
        uv = node_global_vert_base();
        *out = (unsigned short)((unsigned long long)uv >> 0x20);
        if (*out < 0x3e9) goto done_lbl;
    }
    i = (int)uv;
    ec = FLOAT_8061ac08 * rec[2] + ec;
    rec = (float*)aveditor_grid_lookup((int)((unsigned long long)uv >> 0x20), i, (int)zSeed);
    rec2 = (float*)aveditor_grid_lookup((int)rec, i, (int)zSeed);
    if (rec2 != (float*)0) {
        in_f29 = (double)(float)((double)b0 - by);
        v98 = (double)(CONCAT44(0x43300000, (int)b8) ^ 0x80000000);
        v88 = (double)(CONCAT44(0x43300000, (int)b4) ^ 0x80000000);
        in_f31 = (double)((float)(v98 - DOUBLE_8061ac00) - *rec2);
        in_f30 = (double)((float)(v88 - DOUBLE_8061ac00) - rec2[1]);
    }
    p = &b8;
    flags1 = pad_link_matches_offset_type21(rec, p);
    if ((flags1 & 0xff) == 0) {
        if ((rec2 != (float*)0) && (bit1 = pad_node_flag_bit1((int)rec2[4]), bit1 != 0)) {
            uv = aveditor_place_companion_segments();
            p = (float*)uv;
            if ((uv & 0x800000000000000ULL) != 0) {
                v90 = (long long)(int)in_f30;
                (void)v90;
                a0v = (long long)(int)in_f29;
                (void)a0v;
                zSeed = rec2[(int)rec2[3] + 0xb];
                p = (float*)((int)zSeed + (int)in_f29 * 4);
                if ((*(unsigned int*)((int)p[8] +
                        ((int)in_f31 + (int)in_f30 * *(int*)((int)zSeed + 0x14)) * 4) &
                     0x800000) != 0) goto done_lbl;
            }
        }
        if ((((flags2 & 0x1000) != 0) && ((flags2 & 0x40) == 0)) || (b0 == rec[2])) {
            p = (float*)aveditor_place_companion_segments();
            flags1 = pad_node_flag_type6((int)rec[4], (unsigned int)p);
            if (flags1 != 0) {
                node_place_companion_vert();
                if (1000 < out[1]) {
                    node_probe_vert_below();
                }
                goto done_lbl;
            }
        }
        if (((flags2 & 0x1000) == 0) || ((flags2 & 0x40) != 0)) {
            f8 = dir;
            bit1 = pad_node_flag_bit1((int)rec[4]);
            uv = (unsigned long long)(unsigned int)(size_t)p;
            if (bit1 != 0) {
                uv = aveditor_place_companion_segments();
            }
            dirLow = (unsigned int)(uv >> 0x20);
            rec = (float*)aveditor_grid_lookup(dirLow, (int)uv, (int)zSeed);
            e8 = (float)(dx - (double)*rec);
            e4 = (float)(bx - (double)rec[1]);
            e0 = (float)(by - (double)rec[2]);
            if (rec[3] != 0.0) {
                coord_rotate_to_world_offset((int)rec, (short*)&f8, &e8);
            }
            node_nearest_vert_by_edgeclass((double)e8, (double)e4, (double)e0, (int)rec[4],
                                           (unsigned int)f8, dirLow, &f0);
            vbase = node_global_vert_base();
            out[1] = vbase;
            f0 = FLOAT_8061ac08 * rec[2] + f0;
            if (FLOAT_8061ac30 < ec - f0) {
                if (ec - f0 <= FLOAT_8061ac34) {
                    out[2] = 0;
                    out[3] = 0;
                }
                else {
                    i = node_probe_vert_below();
                    if (i != 0) {
                        out[2] = 0;
                        out[3] = 1;
                    }
                }
            }
        }
        else if (*(int*)(&DAT_8047c178 + (int)rec[4] * 0x140) != 0) {
            b0 = b0 - FLOAT_8061ac10;
            uv = roster_hit_test_point((double)b8, (double)b4, (double)b0);
            i = (int)(uv >> 0x20);
            rec = (float*)aveditor_grid_lookup(i, (int)uv, (int)zSeed);
            if (((i != 0) && ((uv & 0x100000000000ULL) == 0)) &&
                (flags1 = pad_link_matches_offset_type21(rec, &b8), (flags1 & 0xff) == 0)) {
                out[1] = 0;
                f6 = dir;
                if (((uv & 0xf0000000000ULL) != 0) &&
                    (((((dirLow & 0xffff) == 1 && ((uv & 0x80000000000ULL) != 0)) ||
                       ((((dirLow & 0xffff) == 3 && ((uv & 0x20000000000ULL) != 0)) ||
                         (((dirLow & 0xffff) == 2 && ((uv & 0x10000000000ULL) != 0)))))) ||
                      (((dirLow & 0xffff) == 4 && ((uv & 0x40000000000ULL) != 0)))))) {
                    dc = (float)(dx - (double)*rec);
                    d8 = (float)(bx - (double)rec[1]);
                    d4 = (float)(by - (double)rec[2]) - FLOAT_8061ac2c;
                    if (rec[3] != 0.0) {
                        coord_rotate_to_world_offset((int)rec, (short*)&f6, &dc);
                    }
                    node_nearest_vert_by_edgeclass((double)dc, (double)d8, (double)d4, (int)rec[4],
                                                   (unsigned int)f6, 0, &f0);
                    vbase = node_global_vert_base();
                    out[1] = vbase;
                    out[2] = 0;
                    out[3] = 0;
                }
                if (out[1] < 0x3e9) {
                    dc = (float)(dx - (double)*rec);
                    d8 = (float)(bx - (double)rec[1]);
                    d4 = (float)(by - (double)rec[2]) - FLOAT_8061ac10;
                    if (rec[3] != 0.0) {
                        coord_rotate_to_world_offset((int)rec, (short*)&f6, &dc);
                    }
                    node_nearest_vert_by_edgeclass((double)dc, (double)d8, (double)d4, (int)rec[4],
                                                   (unsigned int)f6, 0, &f0);
                    vbase = node_global_vert_base();
                    out[1] = vbase;
                    out[2] = 0;
                    out[3] = 1;
                }
                f0 = FLOAT_8061ac08 * rec[2] + f0;
                if (((1000 < out[1]) && (1000 < *out)) &&
                    (FLOAT_8061ac14 < ABS(ec - f0))) {
                    b0 = b0 + FLOAT_8061ac10;
                    i = node_probe_vert_below();
                    if (i != 0) {
                        out[2] = 0;
                        out[3] = 1;
                    }
                }
            }
        }
    }
done_lbl:
    __restore_gpr_25();
    if (false) goto zSeed_init;   // preserve label reachability without altering flow
}

// ---------------------------------------------------------------------------
// 0x803450bc : find a coord in the node table, else roster-hittest two offsets
// ---------------------------------------------------------------------------
void node_find_or_hittest_pair(void)
{
    short  cz;
    unsigned int leadZero;
    short* coord;
    int    parity;
    unsigned char bit1;
    int    off;
    unsigned int i;
    short* entry;
    double da, db, dc, dd, de, df;
    unsigned long long uv;
    unsigned int stack84;
    unsigned int xw;
    int a4, b6;

    uv = FUN_803826f0();
    coord = (short*)(uv >> 0x20);
    parity = (int)(unsigned int)uv >> 0x1f;
    off = 0;
    for (i = (unsigned int)*(unsigned short*)(DAT_80612be4 + 0x306e); i != 0; i = i - 1) {
        entry = (short*)(DAT_80612be4 + off + 0x3070);
        leadZero = countLeadingZeros((*(unsigned char*)(entry + 3) & 1) -
                                     (((unsigned int)uv & 1 ^ -parity) + parity));
        if ((((*coord == *entry) && (coord[1] == entry[1])) && (coord[2] == entry[2])) &&
            (leadZero >> 5 != 0)) goto done_lbl;
        off = off + 0xc;
    }
    if ((uv & 1) == 0) {
        xw = (unsigned int)*coord;
        cz = coord[2];
        unsigned int neg = xw ^ 0x80000000;
        db = (double)(FLOAT_8061ac20 +
                      (float)((double)CONCAT44(0x43300000, (int)coord[1] ^ 0x80000000) -
                              DOUBLE_8061ac00));
        de = (double)(FLOAT_8061ac20 + (float)((double)CONCAT44(0x43300000, neg) - DOUBLE_8061ac00));
        dd = (double)((float)((double)CONCAT44(0x43300000, neg) - DOUBLE_8061ac00) - FLOAT_8061ac20);
        dc = db;
        xw = neg;
    }
    else {
        xw = (unsigned int)coord[1];
        cz = coord[2];
        unsigned int neg = xw ^ 0x80000000;
        dd = (double)(FLOAT_8061ac20 +
                      (float)((double)CONCAT44(0x43300000, (int)*coord ^ 0x80000000) -
                              DOUBLE_8061ac00));
        db = (double)((float)((double)CONCAT44(0x43300000, neg) - DOUBLE_8061ac00) - FLOAT_8061ac20);
        dc = (double)(FLOAT_8061ac20 + (float)((double)CONCAT44(0x43300000, neg) - DOUBLE_8061ac00));
        de = dd;
        xw = neg;
    }
    stack84 = (int)cz ^ 0x80000000;
    da = (double)(float)((double)CONCAT44(0x43300000, stack84) - DOUBLE_8061ac00);
    df = (double)(float)((double)CONCAT44(0x43300000, stack84) - DOUBLE_8061ac00);
    a4 = aveditor_grid_lookup(xw, 0x43300000, (unsigned int)*coord);
    b6 = aveditor_grid_lookup(a4, 0x43300000, (unsigned int)*coord);
    if (((a4 != 0) && (b6 != 0)) &&
        ((bit1 = pad_node_flag_bit1(*(int*)(a4 + 0x10)), bit1 == 0 &&
          ((bit1 = pad_node_flag_bit1(*(int*)(b6 + 0x10)), bit1 == 0 && (a4 != b6)))))) {
        roster_hit_test_point(de, dc, da);
        roster_hit_test_point(dd, db, df);
    }
done_lbl:
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8034545c : true when both endpoint vertex indices are real (>1000)
// ---------------------------------------------------------------------------
int link_endpoints_valid(unsigned short* endpoints)
{
    int ok;

    ok = 0;
    if ((1000 < *endpoints) && (1000 < endpoints[1])) {
        ok = 1;
    }
    return ok;
}

} // extern "C"
