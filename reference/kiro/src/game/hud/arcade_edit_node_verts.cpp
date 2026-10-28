// Batch: batch_971 | Module: arcade/challenge editor pad-node vertex spatial queries + coord rotation
// Address range: 0x80342bc8 - 0x80343b20
// Date: 2026-09-12
//
// The arcade/challenge editor's pad-node vertex spatial-query subsystem: box-overlap collection,
// nearest-vertex search, exact-coord lookup, global vertex indexing, plus the grid-coord rotation
// helper (referenced by batch_970's aveditor_relink_type21_cell). These back the map serialize /
// deserialize and connectivity-build passes. Node geometry lives in the vertex/index buffer tables
// (DAT_805d09ec vertex buffers, DAT_805d0938 index buffers; count at +4, 0x20-byte records at +0xc,
// each carrying a float xyz at +0xc/+0x10/+0x14). The node-list is walked through +0x40 (next) with
// the head at DAT_80612c14; a node's descriptor index is at +0x10 and its rotation at +0xc.
// coord_rotate_steps rotates a short[3] grid coord by N quarter-turns about the descriptor extents;
// node_global_vert_base accumulates the running global vertex base for a node by summing the vertex
// counts of the preceding blocked/type-6 nodes; node_verts_in_box collects every node vertex inside a
// world-space box into an int out-array (global indices); node_find_nearest_vert_dedup finds the
// nearest vertex within a box while skipping already-linked nodes; node_vert_index_to_coord maps a
// global vertex/index number back to a grid coord (the deserialize inverse); node_nearest_vert_by_edgeclass
// and node_pick_two_nearest_verts do edge-class-shaped box nearest searches (one/two results);
// coord_rotate_to_world_offset rotates a coord into a world offset; pad_pair_vert_indices resolves the
// two global vertex indices for a pair of flagged nodes; node_vert_at_coord returns the node vertex
// exactly at a grid coord.
// Callers: aveditor_serialize_map, aveditor_deserialize_map, aveditor_place_all_segments,
// map_build_connectivity, and the higher-level FUN_80344710 pick driver.
//
//   node_verts_in_box              0x80342bc8  collect node vertices inside a box into an out array.
//   coord_rotate_steps             0x80342db4  rotate a short[3] grid coord by N quarter-turns.
//   node_find_nearest_vert_dedup   0x80342e08  nearest vertex in a box, skipping already-linked nodes.
//   node_vert_index_to_coord       0x803430b8  map a global vertex/index number back to a grid coord.
//   node_global_vert_base          0x803432f8  accumulate the running global vertex base for a node.
//   node_nearest_vert_by_edgeclass 0x80343408  nearest vertex with a per-edge-class shaped box.
//   coord_rotate_to_world_offset   0x80343718  rotate a coord producing a world-space offset.
//   pad_pair_vert_indices          0x803437cc  resolve the two global vertex indices for two flagged nodes.
//   node_pick_two_nearest_verts    0x80343864  pick the two nearest vertices in a box.
//   node_vert_at_coord             0x80343b20  find the node vertex exactly at a grid coord.
//
// NON-MATCHING: reproduces the decompiler's structure. The paired-single register spills (in_f26..f31 /
// in_ps26_1..ps31_1 and the extraout_f1 argument carried in a paired-single lane) are expressed as
// plain float/double parameters and locals (Compile-Clean rule 3); the box-test bodies preserve the
// original comparison order. The int->double magic-constant conversions (CONCAT44 with 0x43300000 /
// DOUBLE_8061ac00) and the quarter-turn rotation index arithmetic are reproduced verbatim. The scope
// helpers (FUN_803826d8/e4/e8/f0 push, FUN_80382724/30/34/3c pop) and __save_gpr_28/__restore_gpr_28
// thread the node record through the spilled high word. Raw FUN_/DAT_/FLOAT_ symbols are declared
// file-locally with call-site-accurate arity (Compile-Clean rule 5). CONCAT44 comes from common.h.

#include "common.h"

extern "C" {

extern int   DAT_80612be4;    // editor context base
extern int   DAT_80612c14;    // pad-node list head
extern int   DAT_805d09ec;    // vertex buffer table base (node*4)
extern int   DAT_805d0938;    // index buffer table base (node*4)
extern double DOUBLE_8061ac00;

// world-space voxel layout constants
extern float FLOAT_8061abf8;  // X cell size
extern float FLOAT_8061ac08;  // Y cell size
extern float FLOAT_8061ac0c;  // best-distance seed (large)
extern float FLOAT_8061ac10;  // X half offset
extern float FLOAT_8061ac14;  // Y half offset
extern float FLOAT_8061ac18;  // box slack
extern float FLOAT_8061ac1c;  // X center bias
extern float FLOAT_8061ac20;  // pick override value
extern float FLOAT_8061ac24;  // box expand

// named cross-module helpers
extern unsigned int aveditor_coord_is_valid(short* coord);
extern void  aveditor_coord_set_invalid(short* coord);
extern unsigned int aveditor_place_companion_segments(void);
extern unsigned int pad_node_blocked_or_type6(int node, unsigned int flags);
extern unsigned char pad_node_flag_bit1(int node);
extern unsigned int map_edge_class_from_flags(unsigned int flags);

// scope push/pop + save helpers with local call-site signatures
extern unsigned long long FUN_803826d8(void);   // push (returns node|lo pair)
extern unsigned long long FUN_803826e4(void);   // push
extern unsigned long long FUN_803826e8(void);   // push
extern unsigned long long FUN_803826f0(void);   // push
extern void  FUN_80382724(void);                 // pop
extern void  FUN_80382730(void);                 // pop
extern void  FUN_80382734(void);                 // pop
extern void  FUN_8038273c(void);                 // pop
extern unsigned long long __save_gpr_28(void);
extern void  __restore_gpr_28(void);

// forward decls within this TU
void coord_rotate_steps(short* coord, int node, int startRot, int steps);
int  node_global_vert_base(void);

// ---------------------------------------------------------------------------
// 0x80342db4 : rotate a short[3] grid coord by N quarter-turns
// ---------------------------------------------------------------------------
void coord_rotate_steps(short* coord, int node, int startRot, int steps)
{
    short saved;
    int   carry;

    if (steps < 1) {
        return;
    }
    do {
        saved = *coord;
        carry = startRot + 1 >> 0x1f;
        *coord = (short)(((short)*(int*)(*(int*)(node + startRot * 4 + 0x2c) + 0x18) + -1) - coord[1]);
        startRot = (carry * 4 | (unsigned int)((startRot + 1) * 0x40000000 + carry) >> 0x1e) - carry;
        coord[1] = saved;
        steps = steps - 1;
    } while (steps != 0);
}

// ---------------------------------------------------------------------------
// 0x803432f8 : accumulate the running global vertex base for a node
// ---------------------------------------------------------------------------
int node_global_vert_base(void)
{
    int  n;
    int  off;
    unsigned char bit1;
    unsigned int flags;
    int  node;
    unsigned long long gpr;

    gpr = __save_gpr_28();
    for (node = DAT_80612c14; (node != 0 && (node != (int)gpr)); node = *(int*)(node + 0x40)) {
        bit1 = pad_node_flag_bit1(*(int*)(node + 0x10));
        if (bit1 == 0) {
            flags = aveditor_place_companion_segments();
            pad_node_blocked_or_type6(*(int*)(node + 0x10), flags);
        } else {
            flags = aveditor_place_companion_segments();
            map_edge_class_from_flags(flags);
        }
    }
    if (node == (int)gpr) {
        off = 0;
        n = *(int*)(*(int*)(&DAT_805d09ec + *(int*)(node + 0x10) * 4) + 4);
        if (0 < n) {
            do {
                if ((int)((unsigned long long)gpr >> 0x20) ==
                    *(int*)(*(int*)(&DAT_805d09ec + *(int*)(node + 0x10) * 4) + 0xc + off)) {
                    break;
                }
                off = off + 0x20;
                n = n - 1;
            } while (n != 0);
        }
    }
    __restore_gpr_28();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80342bc8 : collect node vertices inside a box into an out array
// ---------------------------------------------------------------------------
void node_verts_in_box(unsigned int a, unsigned int b, int outArr)
{
    int   node;
    short* coord;
    int   vbuf;
    int   rot;
    int   base;
    int   outOff = 0;
    int   i;
    int   off;
    double magic = DOUBLE_8061ac00;
    double cellX = (double)FLOAT_8061abf8;
    double loX, hiX, loY, hiY, loZ, hiZ;
    unsigned long long gpr;
    short local[3];
    unsigned int nx, ny, nz;

    (void)a; (void)b;
    gpr   = FUN_803826e8();
    node  = (int)((unsigned long long)gpr >> 0x20);
    coord = (short*)gpr;
    local[0] = coord[0];
    local[1] = coord[1];
    local[2] = coord[2];

    vbuf = *(int*)(&DAT_805d09ec + *(int*)(node + 0x10) * 4);
    rot  = *(int*)(node + 0xc);
    if (rot != 0) {
        coord_rotate_steps(local, node, rot, 4 - rot);
    }
    nx = (int)local[0] ^ 0x80000000;
    nz = (int)local[2] ^ 0x80000000;
    ny = (int)local[1] ^ 0x80000000;

    loX = (double)(float)(cellX * (double)(float)((double)CONCAT44(0x43300000, nx) - magic));
    hiX = (double)(float)(cellX + loX);
    loY = (double)(float)((double)FLOAT_8061ac08 * (double)(float)((double)CONCAT44(0x43300000, nz) - magic));
    hiY = (double)(float)((double)FLOAT_8061ac08 + loY);
    loZ = (double)(float)(cellX * (double)(float)((double)CONCAT44(0x43300000, ny) - magic));
    hiZ = (double)(float)(cellX + loZ);

    base = node_global_vert_base();
    off  = 0;
    for (i = 0; i < *(int*)(*(int*)(&DAT_805d09ec + *(int*)(node + 0x10) * 4) + 4); i = i + 1) {
        int rec = vbuf + 0xc + off;
        if ((((loY < (double)*(float*)(rec + 0x10)) && ((double)*(float*)(rec + 0x10) < hiY)) &&
             (loX < (double)*(float*)(rec + 0xc))) &&
            ((((double)*(float*)(rec + 0xc) < hiX && (loZ < (double)*(float*)(rec + 0x14))) &&
              ((double)*(float*)(rec + 0x14) < hiZ)))) {
            *(int*)(outArr + outOff) = i + base;
            outOff = outOff + 4;
        }
        off = off + 0x20;
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80342e08 : nearest vertex in a box, skipping already-linked nodes
// ---------------------------------------------------------------------------
void node_find_nearest_vert_dedup(void)
{
    int   node;
    short* coord;
    int   iter;
    int   grid;
    int   ibuf;
    int   n;
    double best;
    unsigned long long gpr;
    short local[3];
    unsigned int nx, ny, nz;
    int   rot;
    int   off;

    gpr   = FUN_803826e4();
    node  = (int)((unsigned long long)gpr >> 0x20);
    coord = (short*)gpr;

    if (node != 0) {
        // skip nodes already linked in the +0x2f7c chain
        for (iter = *(int*)(DAT_80612be4 + 0x2f7c); iter != 0; iter = *(int*)(iter + 0x40)) {
            if (iter == node) {
                FUN_80382730();
                return;
            }
        }
        local[0] = coord[0];
        local[2] = coord[2];
        if (aveditor_coord_is_valid(local) != 0) {
            for (iter = DAT_80612c14; (iter != 0 && (iter != node)); iter = *(int*)(iter + 0x40)) {
                unsigned int f = aveditor_place_companion_segments();
                pad_node_blocked_or_type6(*(int*)(iter + 0x10), f);
            }
            rot  = *(int*)(node + 0xc);
            grid = *(int*)(node + rot * 4 + 0x2c);
            ibuf = *(int*)(&DAT_805d0938 + *(int*)(node + 0x10) * 4);
            n    = *(int*)(ibuf + 4);
            if ((((int)coord[0] < *(int*)(grid + 0x14)) && ((int)coord[1] < *(int*)(grid + 0x18))) &&
                ((int)coord[2] < *(int*)(grid + 0x1c))) {
                best = (double)FLOAT_8061ac0c;
                if (rot != 0) {
                    coord_rotate_steps(coord, node, rot, 4 - rot);
                }
                nx = (int)*coord ^ 0x80000000;
                nz = (int)coord[2] ^ 0x80000000;
                ny = (int)coord[1] ^ 0x80000000;
                {
                    float bx = FLOAT_8061abf8 * (float)((double)CONCAT44(0x43300000, nx) - DOUBLE_8061ac00);
                    float lox = bx - FLOAT_8061ac10;
                    float by = FLOAT_8061ac08 * (float)((double)CONCAT44(0x43300000, nz) - DOUBLE_8061ac00);
                    float loy = by - FLOAT_8061ac14;
                    float bz = FLOAT_8061abf8 * (float)((double)CONCAT44(0x43300000, ny) - DOUBLE_8061ac00);
                    float loz = bz - FLOAT_8061ac10;
                    if (0 < n) {
                        off = 0;
                        do {
                            int rec = ibuf + 0xc + off;
                            float vx = *(float*)(rec + 0xc);
                            if (((((lox < vx) && (vx < FLOAT_8061ac18 + FLOAT_8061abf8 + lox)) &&
                                  ((float)*(float*)(rec + 0x10) > loy &&
                                   ((*(float*)(rec + 0x10) < FLOAT_8061ac14 + FLOAT_8061ac08 + loy &&
                                     (*(float*)(rec + 0x14) > loz)))))) &&
                                 (*(float*)(rec + 0x14) < FLOAT_8061ac18 + FLOAT_8061abf8 + loz)) {
                                float dx = vx - (FLOAT_8061ac1c + bx);
                                float dy = *(float*)(rec + 0x10) - by;
                                float dz = *(float*)(rec + 0x14) - (FLOAT_8061ac1c + bz);
                                double d = (double)(dz * dz + dx * dx + dy * dy);
                                if (d < best) {
                                    best = d;
                                }
                            }
                            off = off + 0x20;
                            n = n - 1;
                        } while (n != 0);
                    }
                }
            }
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x803430b8 : map a global vertex/index number back to a grid coord
// ---------------------------------------------------------------------------
void node_vert_index_to_coord(unsigned int a, unsigned int b, int number)
{
    int   node;
    short* coord;
    int   accum = 0;
    int   iter;
    int   idx;
    int   buf;
    double magic = DOUBLE_8061ac00;
    float cellX = FLOAT_8061abf8;
    float cellY = FLOAT_8061ac08;
    unsigned long long gpr;

    (void)a; (void)b;
    gpr   = FUN_803826e4();
    node  = (int)((unsigned long long)gpr >> 0x20);
    coord = (short*)gpr;

    if (node == 0) {
        aveditor_coord_set_invalid(coord);
    } else if (number < 0x2329) {
        for (iter = *(int*)(node + 0x40); iter != 0; iter = *(int*)(iter + 0x40)) {
            unsigned int f = aveditor_place_companion_segments();
            if (pad_node_blocked_or_type6(*(int*)(iter + 0x10), f) != 0) {
                accum = accum + *(int*)(*(int*)(&DAT_805d09ec + *(int*)(iter + 0x10) * 4) + 4);
            }
        }
        idx = (number - accum) + -0x3e9;
        buf = *(int*)(&DAT_805d09ec + *(int*)(node + 0x10) * 4);
        if ((idx < 0) || (*(int*)(buf + 4) <= idx)) {
            aveditor_coord_set_invalid(coord);
        } else {
            buf = buf + 0xc + idx * 0x20;
            coord[0] = (short)(int)((double)CONCAT44(0x43300000,
                        (int)(*(float*)(buf + 0xc) / FLOAT_8061abf8) ^ 0x80000000) - DOUBLE_8061ac00);
            coord[1] = (short)(int)((double)CONCAT44(0x43300000,
                        (int)(*(float*)(buf + 0x14) / cellX) ^ 0x80000000) - magic);
            coord[2] = (short)(int)((double)CONCAT44(0x43300000,
                        (int)(*(float*)(buf + 0x10) / cellY) ^ 0x80000000) - magic);
            if (*(int*)(node + 0xc) != 0) {
                coord_rotate_steps(coord, node, 0, *(int*)(node + 0xc));
            }
        }
    } else {
        for (iter = DAT_80612c14; iter != node; iter = *(int*)(iter + 0x40)) {
            unsigned int f = aveditor_place_companion_segments();
            if (pad_node_blocked_or_type6(*(int*)(iter + 0x10), f) != 0) {
                accum = accum + *(int*)(*(int*)(&DAT_805d0938 + *(int*)(iter + 0x10) * 4) + 4);
            }
        }
        idx = (number - accum) + -0x2329;
        buf = *(int*)(&DAT_805d0938 + *(int*)(node + 0x10) * 4);
        if ((idx < 0) || (*(int*)(buf + 4) <= idx)) {
            aveditor_coord_set_invalid(coord);
        } else {
            buf = buf + 0xc + idx * 0x20;
            coord[0] = (short)(int)((double)CONCAT44(0x43300000,
                        (int)(*(float*)(buf + 0xc) / FLOAT_8061abf8) ^ 0x80000000) - DOUBLE_8061ac00);
            coord[1] = (short)(int)((double)CONCAT44(0x43300000,
                        (int)(*(float*)(buf + 0x14) / cellX) ^ 0x80000000) - magic);
            coord[2] = (short)(int)((double)CONCAT44(0x43300000,
                        (int)(*(float*)(buf + 0x10) / cellY) ^ 0x80000000) - magic);
            if (*(int*)(node + 0xc) != 0) {
                coord_rotate_steps(coord, node, 0, *(int*)(node + 0xc));
            }
        }
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80343408 : nearest vertex with a per-edge-class shaped box
// ---------------------------------------------------------------------------
void node_nearest_vert_by_edgeclass(double px, double py, double pz,
                                    unsigned int edgeFlags, float* out)
{
    unsigned int cls;
    int   node;
    int   vbuf;
    int   count;
    int   i;
    int   off;
    double best = (double)FLOAT_8061ac0c;
    double cell = (double)FLOAT_8061abf8;
    double loX, hiX, loY, hiY, loZ, hiZ, cx, cy, cz;
    unsigned char bit1;
    unsigned long long gpr;

    gpr   = FUN_803826d8();
    node  = (int)(gpr >> 0x20);
    cls   = (unsigned int)gpr & 0xffff;
    vbuf  = *(int*)(&DAT_805d09ec + node * 4);
    count = *(int*)(vbuf + 4);

    // per-edge-class box extents (px is the extra paired-single arg in the original)
    loX = (double)(float)(cell * px);
    hiX = (double)(float)(cell + loX);
    loY = (double)(float)((double)FLOAT_8061ac08 * py);
    hiY = (double)(float)((double)FLOAT_8061ac08 + loY);
    loZ = (double)(float)(cell * pz);
    hiZ = (double)(float)(cell + loZ);
    cx = loX + (double)FLOAT_8061ac1c;
    cy = loY;
    cz = loZ + (double)FLOAT_8061ac1c;
    (void)cls;

    bit1 = pad_node_flag_bit1(node);
    if (bit1 != 0) {
        count = (int)map_edge_class_from_flags(edgeFlags);
    }

    off = 0;
    for (i = 0; i < count; i = i + 1) {
        int rec = vbuf + 0xc + off;
        double vy = (double)*(float*)(rec + 0x10);
        double vx = (double)*(float*)(rec + 0xc);
        double vz = (double)*(float*)(rec + 0x14);
        if (((((loY < vy) && (vy < hiY)) && (loX < vx)) &&
             ((vx < hiX && (loZ < vz)))) &&
            ((vz < hiZ &&
              (best > (double)((float)(vz - cz) * (float)(vz - cz) +
                               (float)(vx - cx) * (float)(vx - cx) +
                               (float)(vy - loY) * (float)(vy - loY)))))) {
            double d = (double)((float)(vz - cz) * (float)(vz - cz) +
                                (float)(vx - cx) * (float)(vx - cx) +
                                (float)(vy - loY) * (float)(vy - loY));
            *out = *(float*)(rec + 0x10);
            best = d;
            bit1 = pad_node_flag_bit1(node);
            if (((bit1 != 0) && ((edgeFlags & 0x1000000) != 0)) && ((edgeFlags & 0x8000000) == 0)) {
                *out = FLOAT_8061ac20;
            }
        }
        off = off + 0x20;
    }
    (void)hiZ;
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x80343718 : rotate a coord producing a world-space offset
// ---------------------------------------------------------------------------
void coord_rotate_to_world_offset(int node, unsigned short* rotState, float* io)
{
    float cur;
    float prev;
    unsigned short r;
    int steps;
    int i = 0;

    r     = *rotState;
    steps = 4 - *(int*)(node + 0xc);
    cur   = *io;
    prev  = io[1];
    {
        float back = prev;
        if (0 < steps) {
            do {
                prev = cur;
                cur = (float)((double)CONCAT44(0x43300000,
                        *(unsigned int*)(*(int*)(node + (*(int*)(node + 0xc) + i) * 4 + 0x2c) + 0x18) ^ 0x80000000)
                        - DOUBLE_8061ac00) - back;
                if ((r & 1) == 0) {
                    cur = cur - FLOAT_8061ac10;
                }
                r = (unsigned short)((r < 4) ? (r + 1) : 1);
                i = i + 1;
                steps = steps - 1;
                back = prev;
            } while (steps != 0);
        }
    }
    *io = cur;
    io[1] = prev;
    io[2] = io[2];
    *rotState = r;
}

// ---------------------------------------------------------------------------
// 0x803437cc : resolve the two global vertex indices for two flagged nodes
// ---------------------------------------------------------------------------
void pad_pair_vert_indices(unsigned int a, unsigned int b, unsigned short* out)
{
    int nodeHi;
    int nodeLo;
    unsigned char bit1;
    unsigned long long gpr;

    (void)a; (void)b;
    gpr    = FUN_803826f0();
    nodeHi = (int)((unsigned long long)gpr >> 0x20);
    nodeLo = (int)gpr;

    if ((((nodeHi != 0) && (nodeLo != 0)) &&
         (bit1 = pad_node_flag_bit1(*(int*)(nodeHi + 0x10)), bit1 != 0)) &&
        (bit1 = pad_node_flag_bit1(*(int*)(nodeLo + 0x10)), bit1 != 0)) {
        out[0] = (unsigned short)node_global_vert_base();
        out[1] = (unsigned short)node_global_vert_base();
        *(int*)(out + 2) = 0;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80343864 : pick the two nearest vertices in a box
// ---------------------------------------------------------------------------
void node_pick_two_nearest_verts(double px, double py, double pz,
                                 unsigned int p4, unsigned int p5, unsigned short* out)
{
    float* nodef;
    unsigned int cls;
    int   count;
    int   vbase;
    int   best0 = 0;
    int   i, off, remain;
    float bestDist;
    double loX, hiX, loY, hiY, loZ, hiZ, cx, cy, cz;
    unsigned long long gpr;

    (void)p4; (void)p5;
    gpr   = FUN_803826e8();
    nodef = (float*)((unsigned long long)gpr >> 0x20);
    cls   = (unsigned int)gpr & 0xffff;
    count = *(int*)(*(int*)(&DAT_805d09ec + (int)nodef[4] * 4) + 4);
    vbase = *(int*)(&DAT_805d09ec + (int)nodef[4] * 4) + 0xc;

    if ((cls < 0xf) && (10 < cls)) {
        float fx = (float)(px - (double)*nodef) + FLOAT_8061ac20;
        float fy = (float)(py - (double)nodef[1]) + FLOAT_8061ac20;
        loX = (double)(FLOAT_8061abf8 * (fx - FLOAT_8061ac24));
        hiX = (double)(FLOAT_8061abf8 * (FLOAT_8061ac24 + fx));
        loY = (double)(FLOAT_8061ac08 * (float)(pz - (double)nodef[2]));
        hiY = (double)(FLOAT_8061ac08 * (FLOAT_8061ac10 + (float)(pz - (double)nodef[2])));
        loZ = (double)(FLOAT_8061abf8 * (fy - FLOAT_8061ac24));
        hiZ = (double)(FLOAT_8061abf8 * (FLOAT_8061ac24 + fy));
        cx  = (double)(FLOAT_8061abf8 * fx);
        cy  = loY;
        cz  = (double)(FLOAT_8061abf8 * fy);

        // pass 1: find the nearest vertex index
        bestDist = FLOAT_8061ac0c;
        off = 0;
        remain = count;
        for (i = 0; i < count; i = i + 1) {
            int rec = vbase + off;
            double vy = (double)*(float*)(rec + 0x10);
            if (((loY < vy) && (vy < hiY))) {
                double vx = (double)*(float*)(rec + 0xc);
                double vz = (double)*(float*)(rec + 0x14);
                if ((loX < vx) && (vx < hiX) && (loZ < vz) && (vz < hiZ)) {
                    float d = (float)(vz - cz) * (float)(vz - cz) +
                              (float)(vx - cx) * (float)(vx - cx) +
                              (float)(vy - cy) * (float)(vy - cy);
                    if (d < bestDist) {
                        best0 = i;
                        bestDist = d;
                    }
                }
            }
            off = off + 0x20;
        }
        out[0] = (unsigned short)node_global_vert_base();

        // pass 2: nearest vertex excluding best0 (result folded into node_global_vert_base)
        bestDist = FLOAT_8061ac0c;
        off = 0;
        for (i = 0; i < count; i = i + 1) {
            if (i != best0) {
                int rec = vbase + off;
                double vy = (double)*(float*)(rec + 0x10);
                if (((loY < vy) && (vy < hiY))) {
                    double vx = (double)*(float*)(rec + 0xc);
                    double vz = (double)*(float*)(rec + 0x14);
                    if ((loX < vx) && (vx < hiX) && (loZ < vz) && (vz < hiZ)) {
                        float d = (float)(vz - cz) * (float)(vz - cz) +
                                  (float)(vx - cx) * (float)(vx - cx) +
                                  (float)(vy - cy) * (float)(vy - cy);
                        if (d < bestDist) {
                            bestDist = d;
                        }
                    }
                }
            }
            off = off + 0x20;
        }
        out[1] = (unsigned short)node_global_vert_base();
        (void)remain; (void)cz;
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80343b20 : find the node vertex exactly at a grid coord
// ---------------------------------------------------------------------------
unsigned int node_vert_at_coord(int node, unsigned int cx, unsigned int cz, unsigned int cy)
{
    unsigned int result = 0;
    int   count;
    int   off = 0;
    float bx, by, bz, hiY, best;

    by = FLOAT_8061ac08 * (float)((double)CONCAT44(0x43300000, cy ^ 0x80000000) - DOUBLE_8061ac00);
    count = *(int*)(*(int*)(&DAT_805d09ec + node * 4) + 4);
    hiY = FLOAT_8061ac08 + by;
    bx = FLOAT_8061abf8 * (float)((double)CONCAT44(0x43300000, cx ^ 0x80000000) - DOUBLE_8061ac00);
    bz = FLOAT_8061abf8 * (float)((double)CONCAT44(0x43300000, cz ^ 0x80000000) - DOUBLE_8061ac00);
    best = FLOAT_8061ac0c;

    if (0 < count) {
        do {
            unsigned int* rec = (unsigned int*)(*(int*)(&DAT_805d09ec + node * 4) + 0xc + off);
            float vx = (float)rec[3];
            if (((((bx < vx) && (vx < FLOAT_8061abf8 + bx)) &&
                  ((float)rec[4] > by)) &&
                 (((float)rec[4] < hiY && ((float)rec[5] > bz)))) &&
                (((float)rec[5] < FLOAT_8061abf8 + bz &&
                  (((float)rec[4] < hiY))))) {
                float dvx = vx - (FLOAT_8061ac1c + bx);
                float dvz = (float)rec[5] - (FLOAT_8061ac1c + bz);
                float d = dvz * dvz + dvx * dvx + ((float)rec[4] - hiY) * ((float)rec[4] - hiY);
                if (d < best) {
                    result = rec[0];
                    best = d;
                }
            }
            off = off + 0x20;
            count = count - 1;
        } while (count != 0);
    }
    return result;
}

} // extern "C"
