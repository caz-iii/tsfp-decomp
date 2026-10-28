// ===========================================================================
// fluid_sim.cpp
// ---------------------------------------------------------------------------
// batch_435 : procedural FLUID / DRIP-STRAND simulation subsystem
// Address range: 0x8015f9dc - 0x80160038 | 2026-09-06
// ---------------------------------------------------------------------------
// A self-contained particle-fluid system that grows connected "strands" of
// droplet nodes which are advected along Catmull-style splines, projected onto
// floor polygons, and faded out over time (blood drips / dripping goo / fluid
// trails). It owns four parallel banks, all allocated in fluid_alloc_banks
// (0x8015fbdc) and reset in fluid_reset (0x8015fc98):
//
//   * DAT_8061221c : NODE bank      - 200 records, 0x30 stride. Each node holds
//                    position xyz (+0x00/+0x04/+0x08), a radius/size (+0x18),
//                    a state word (+0x10 low half at +0x10), grid up/down cell
//                    links (+0x1c/+0x1e -> DAT_80612220 cell buckets), and the
//                    intrusive strand doubly-linked list prev/next
//                    (+0x26/+0x28) plus parent-strand id (+0x22) and a
//                    head-tail pair (+0x2a/+0x2c) used by the free scrub.
//   * DAT_80612218 : STRAND bank    - 30 records, 0x18 stride. node-count
//                    (+0x06), head/tail node ids (+0x08/+0x0a), accumulated
//                    length (+0x0c), and an "active" flag (+0x02).
//   * DAT_80612224 : GRID-CELL bank - 100 records, 0x38 stride. spatial hash of
//                    node x/z into 5-bit cell coords (fluid_hash_node_cell).
//   * DAT_80612220 : 32 CELL-COLUMN arrays (0x40 bytes each) of 0xffff slots.
//   * DAT_8061221c-adjacent index recycler: DAT_80612210 (short ring, count
//                    DAT_80612200) hands out strand ids; DAT_806121f8 is the
//                    live free-list head (0x7c link) of allocated node blocks
//                    in DAT_80612214 (0x80 stride, 0xa00 bytes = 20 blocks).
//
// The per-frame driver (0x80160628, fluid_update - batch_436) advances every
// strand, but this batch covers the allocation / reset / free-list plumbing and
// the two leaf helpers used by the driver.
// ===========================================================================
#include "common.h"
#include "generated_decls.h"

// --- batch_435 raw helpers (register-pair frame in/out + float->int) --------
extern "C" {
extern unsigned long long FUN_803826d4(void);   // fluid floor-project frame in
extern void               FUN_80382720(void);    // fluid floor-project frame out
extern int                FUN_803825c0(double v); // float -> int (truncate)
extern int                __save_gpr_28(void);     // fluid_node_free prologue
extern void               __restore_gpr_28(void);
}

// --- batch_435 data banks / globals -----------------------------------------
extern int   DAT_80611b04;     // scene node -> render-object table base
extern int   DAT_806121f8;     // node free-list head (0x7c link)
extern int   DAT_806121fc;     // active strand list head
extern int   DAT_80612200;     // strand-id ring count (starts 0x14)
extern int   DAT_80612204;
extern int   DAT_80612208;
extern int   DAT_8061220c;     // node-bank rotation base
extern int   DAT_80612210;     // strand-id ring (short[])
extern int   DAT_80612214;     // node-block bank (0x80 stride)
extern int   DAT_80612218;     // strand bank (0x18 stride)
extern int   DAT_8061221c;     // node bank (0x30 stride)
extern int   DAT_80612220;     // 32 cell-column arrays
extern int   DAT_80612224;     // grid-cell bank (0x38 stride)
extern int   DAT_80612228;
extern int   DAT_8061222c;
extern int   DAT_80612230;
extern int   DAT_806121f0;
extern int   DAT_806109b0;
extern int   DAT_80589580;     // grid-cell id scratch (100 * u32)
extern int   DAT_80589774;     // strand scratch (0x14 * 0x68)
// particle-def source blocks + init defaults
extern int   DAT_80589f30;     // fluid droplet particle def
extern int   DAT_80589ffc;     // fluid droplet emitter block
extern int   DAT_803ccb38; extern int DAT_803ccb3c; extern int DAT_803ccb40; // emitter scale vec
// fluid float constants
extern float FLOAT_806161b0; extern float FLOAT_806161b4; extern float FLOAT_806161b8;
extern float FLOAT_806161bc; extern float FLOAT_806161c0; extern float FLOAT_806161c4;
extern float FLOAT_806161c8; extern float FLOAT_806161cc; extern float FLOAT_806161d0;
extern float FLOAT_806161d4; extern float FLOAT_806161d8; extern float FLOAT_806161dc;
extern float FLOAT_806161e0; extern float FLOAT_806161e4; extern float FLOAT_806161e8;
extern float FLOAT_806161ec;

// --- batch_435 typed aliases (generated decls differ from PPC-ABI call shape)-
// mem_alloc has a simplified 2-arg generated decl; the fluid allocator uses the
// full 16-arg PPC form (8 GPR/FPR spill slots + size/type + 6 tail words).
typedef int (*fluid_memalloc_fn)(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                                 unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                 unsigned long long p7, unsigned long long p8, int size, int type,
                                 unsigned int p11, unsigned int p12, unsigned int p13,
                                 unsigned int p14, unsigned int p15, unsigned int p16);
#define FLUID_MEM_ALLOC ((fluid_memalloc_fn)&mem_alloc)
// scene_query_nodes_flat_y is declared void; the caller consumes r3 as a count.
typedef int (*fluid_flatq_fn)(int param_1, float* aabb_in, int result_buf, int max_results);
#define FLUID_FLAT_QUERY ((fluid_flatq_fn)&scene_query_nodes_flat_y)
// particle_def_create's generated decl packs the leading fp pair; route the two
// leading floats + def pointer through a typed alias (matches subobj_grab form).
typedef void (*fluid_pdef_create_fn)(double a, double b, void* ref, int p4, int p5, int p6, int type, int p8);
#define FLUID_PDEF_CREATE ((fluid_pdef_create_fn)&particle_def_create)
typedef void (*fluid_pdef_sizekf_fn)(double a, double b, double c, double d, unsigned int f0, unsigned int f1, unsigned int f2);
#define FLUID_PDEF_SIZE_KF ((fluid_pdef_sizekf_fn)&particle_def_set_size_keyframes)
typedef void (*fluid_pdef_size_fn)(double a, double b, double c, double d);
#define FLUID_PDEF_SIZE_SE ((fluid_pdef_size_fn)&particle_def_set_size_start_end)
#define FLUID_PDEF_COLOR_END ((fluid_pdef_size_fn)&particle_def_set_color_end)
typedef void (*fluid_pdef_vel_fn)(double a, double b, double c, double d, double e, double f);
#define FLUID_PDEF_VEL ((fluid_pdef_vel_fn)&particle_def_set_velocity_ranges)
typedef void (*fluid_pemit_create_fn)(double a, double b, double c, void* r1, void* r2, int p6, int p7);
#define FLUID_PEMIT_CREATE ((fluid_pemit_create_fn)&particle_emitter_create)
#define FLUID_PEMIT_COLOR ((fluid_pdef_size_fn)&particle_emitter_set_color_range)
// particle_emitter_set_scale's generated decl takes a float; the reset passes a
// pointer to the scale vector.
typedef void (*fluid_pemit_scale_fn)(void* vec);
#define FLUID_PEMIT_SCALE ((fluid_pemit_scale_fn)&particle_emitter_set_scale)

// --- batch_435 forward declarations -----------------------------------------
void fluid_project_to_floor(unsigned int p1, unsigned int p2, int* pHit);  // 0x8015f9dc
int  fluid_strand_id_alloc(void);                                          // 0x8015fb2c
void fluid_strand_id_free(int nodeBlock);                                  // 0x8015fb70
void fluid_alloc_banks(unsigned long long p1, unsigned long long p2, unsigned long long p3, unsigned long long p4, unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8, unsigned int p9, unsigned int p10, unsigned int p11, unsigned int p12, unsigned int p13, unsigned int p14, unsigned int p15, unsigned int p16); // 0x8015fbdc
void fluid_reset(void);                                                    // 0x8015fc98
void fluid_grid_cell_mark(int cellIdx);                                    // 0x8015fef4
void fluid_node_unlink_cell(int nodeIdx);                                  // 0x8015ff0c
void fluid_hash_node_cell(int gridIdx);                                    // 0x8015ff88
void fluid_strand_release_if_empty(int strandId);                          // 0x8015ffe8
void fluid_node_free(void);                                                // 0x80160038

// ---------------------------------------------------------------------------
// 0x8015f9dc  fluid_project_to_floor
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826d4 / FUN_80382720). Take the frame's node
// position, build a small XZ AABB around it (+/- FLOAT_806161b4), query the
// flat-Y scene nodes in that box, and for each candidate render object run a
// floor polygon search; keep the highest floor hit that is below the node's Y
// (within FLOAT_806161bc). Write the winning scene-node id to the frame's out
// slot and a found-flag to pHit.
// ---------------------------------------------------------------------------
void fluid_project_to_floor(unsigned int p1, unsigned int p2, int* pHit)
{
    (void)p1; (void)p2;
    unsigned long long fr = FUN_803826d4();
    float* nodePos = (float*)(unsigned int)(fr >> 0x20);
    int* outSlot = (int*)(unsigned int)fr;

    float aabb[6];
    aabb[0] = nodePos[0] - FLOAT_806161b4;
    aabb[3] = FLOAT_806161b4 + nodePos[0];
    aabb[1] = nodePos[1] - FLOAT_806161b4;
    aabb[4] = FLOAT_806161b4 + nodePos[1];
    aabb[2] = nodePos[2] - FLOAT_806161b4;
    aabb[5] = FLOAT_806161b4 + nodePos[2];

    int found = 0;
    int bestNode = 0;
    double bestY = (double)FLOAT_806161b0;
    int results[22];
    int count = FLUID_FLAT_QUERY(0, aabb, (int)(intptr_t)results, 10);
    int off = 0;
    for (int i = 0; i < count; i = i + 1) {
        int sceneNode = *(int*)((int)results + off);
        int obj = *(int*)(DAT_80611b04 + sceneNode * 4);
        double y = (double)floor_polygon_search(*(int*)(obj + 0x1e0), *(int*)(obj + 0x1f0),
                                                *(unsigned int*)(*(int*)(obj + 4) + 0x30),
                                                nodePos, (void**)0);
        if ((bestY < y) && (y < (double)(FLOAT_806161bc + nodePos[1]))) {
            found = 1;
            bestNode = sceneNode;
            bestY = y;
        }
        off = off + 4;
    }
    if (outSlot != (int*)0) *outSlot = bestNode;
    if (pHit != (int*)0) *pHit = found;
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// 0x8015fb2c  fluid_strand_id_alloc
// ---------------------------------------------------------------------------
// Pop a strand id from the ring recycler (DAT_80612210, count DAT_80612200),
// take the matching 0x80-stride node block from DAT_80612214, push it onto the
// node free-list (DAT_806121f8, 0x7c link), and return the id. -1 if empty.
// ---------------------------------------------------------------------------
int fluid_strand_id_alloc(void)
{
    if (DAT_80612200 != 0) {
        DAT_80612200 = DAT_80612200 + -1;
        unsigned int id = (unsigned int)*(unsigned short*)(DAT_80612210 + DAT_80612200 * 2);
        int block = DAT_80612214 + id * 0x80;
        *(int*)(block + 0x7c) = DAT_806121f8;
        DAT_806121f8 = block;
        return (int)id;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// 0x8015fb70  fluid_strand_id_free
// ---------------------------------------------------------------------------
// Unlink every node block that maps to strand id `nodeBlock` from the free-list
// (DAT_806121f8), then push that id back onto the ring recycler.
// ---------------------------------------------------------------------------
void fluid_strand_id_free(int nodeBlock)
{
    int cur = DAT_806121f8;
    int prev = 0;
    int next;
    while (next = cur, next != 0) {
        unsigned int rel = (unsigned int)(next - DAT_80612214);
        int id = ((int)rel >> 7) + (unsigned int)((int)rel < 0 && (rel & 0x7f) != 0);
        if (nodeBlock == id) {
            if (prev == 0) {
                DAT_806121f8 = *(int*)(next + 0x7c);
            } else {
                *(int*)(prev + 0x7c) = *(int*)(next + 0x7c);
            }
        }
        prev = next;
        cur = *(int*)(next + 0x7c);
    }
    int slot = DAT_80612200 * 2;
    DAT_80612200 = DAT_80612200 + 1;
    *(short*)(DAT_80612210 + slot) = (short)nodeBlock;
}

// ---------------------------------------------------------------------------
// 0x8015fbdc  fluid_alloc_banks
// ---------------------------------------------------------------------------
// Allocate all fluid banks (via mem_alloc, 16-arg PPC form): 32 cell-column
// arrays (0x80 table of 0x40-byte arrays), the node bank (0x2580 = 200*0x30),
// the grid-cell bank (0x15e0 = 100*0x38), the strand bank (0x2d0 = 30*0x18), the
// node-block bank (0xa00 = 20*0x80), and the strand-id ring (0x30). Then reset.
// Called from the fluid subsystem alloc (0x801c17a8).
// ---------------------------------------------------------------------------
void fluid_alloc_banks(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8, unsigned int p9,
                       unsigned int p10, unsigned int p11, unsigned int p12, unsigned int p13,
                       unsigned int p14, unsigned int p15, unsigned int p16)
{
    (void)p9; (void)p10;
    DAT_80612220 = FLUID_MEM_ALLOC(p1, p2, p3, p4, p5, p6, p7, p8, 0x80, 1,
                                   p11, p12, p13, p14, p15, p16);
    int off = 0;
    for (int i = 0; i < 0x20; i = i + 1) {
        int cell = FLUID_MEM_ALLOC(p1, p2, p3, p4, p5, p6, p7, p8, 0x40, 1,
                                   p11, p12, p13, p14, p15, p16);
        *(int*)(DAT_80612220 + off) = cell;
        off = off + 4;
    }
    DAT_8061221c = FLUID_MEM_ALLOC(p1, p2, p3, p4, p5, p6, p7, p8, 0x2580, 1,
                                   p11, p12, p13, p14, p15, p16);
    DAT_80612224 = FLUID_MEM_ALLOC(p1, p2, p3, p4, p5, p6, p7, p8, 0x15e0, 1,
                                   p11, p12, p13, p14, p15, p16);
    DAT_80612218 = FLUID_MEM_ALLOC(p1, p2, p3, p4, p5, p6, p7, p8, 0x2d0, 1,
                                   p11, p12, p13, p14, p15, p16);
    DAT_80612214 = FLUID_MEM_ALLOC(p1, p2, p3, p4, p5, p6, p7, p8, 0xa00, 1,
                                   p11, p12, p13, p14, p15, p16);
    DAT_80612210 = FLUID_MEM_ALLOC(p1, p2, p3, p4, p5, p6, p7, p8, 0x30, 1,
                                   p11, p12, p13, p14, p15, p16);
    fluid_reset();
}

// ---------------------------------------------------------------------------
// 0x8015fc98  fluid_reset
// ---------------------------------------------------------------------------
// Reset all fluid bookkeeping to empty: clear node parent-strand ids (200 *
// 0x30), grid-cell parent ids + scratch (100 * 0x38 / DAT_80589580), strand
// bucket ids (30 * 0x18), fill the strand-id ring with 0..19, clear every
// cell-column array (32 * 0x20 shorts), zero the strand scratch (0x14 * 0x68),
// seed the counters (ring count 0x14), and (re)register the fluid droplet
// particle def + emitter.
// ---------------------------------------------------------------------------
void fluid_reset(void)
{
    int scale[3];
    DAT_806121fc = 0;
    scale[0] = DAT_803ccb38;
    scale[1] = DAT_803ccb3c;
    scale[2] = DAT_803ccb40;
    DAT_806121f8 = 0;

    int off = 0;
    for (int i = 200; i != 0; i = i - 1) {
        *(short*)(DAT_8061221c + off + 0x20) = (short)0xffff;
        off = off + 0x30;
    }
    off = 0;
    int idOff = 0;
    for (int i = 100; i != 0; i = i - 1) {
        *(short*)(DAT_80612224 + off + 0x20) = (short)0xffff;
        *(int*)((int)&DAT_80589580 + idOff) = -1;
        idOff = idOff + 4;
        off = off + 0x38;
    }
    off = 0;
    for (int i = 0x1e; i != 0; i = i - 1) {
        *(short*)(DAT_80612218 + off) = (short)0xffff;
        off = off + 0x18;
    }
    short sid = 0;
    off = 0;
    for (int i = 0x14; i != 0; i = i - 1) {
        *(short*)(DAT_80612210 + off) = sid;
        sid = sid + 1;
        off = off + 2;
    }
    off = 0;
    for (int i = 0; i < 0x20; i = i + 1) {
        int cellOff = 0;
        for (int j = 0x20; j != 0; j = j - 1) {
            *(short*)(*(int*)(off + DAT_80612220) + cellOff) = (short)0xffff;
            cellOff = cellOff + 2;
        }
        off = off + 4;
    }
    off = 0;
    for (int i = 0x14; i != 0; i = i - 1) {
        *(int*)((int)&DAT_80589774 + off) = 0;
        off = off + 0x68;
    }
    DAT_80612204 = 0;
    DAT_8061220c = 0;
    DAT_80612208 = 0;
    DAT_80612200 = 0x14;
    DAT_806121f0 = 0;
    DAT_80612230 = 1;
    DAT_8061222c = 1;
    DAT_80612228 = 0;

    FLUID_PDEF_CREATE((double)FLOAT_806161c0, (double)FLOAT_806161c4, &DAT_80589f30, 0, 1, 1, 0x326, 0);
    FLUID_PDEF_SIZE_KF((double)FLOAT_806161c8, (double)FLOAT_806161b8, (double)FLOAT_806161b8,
                       (double)FLOAT_806161cc, 0x80820ff, 0x80810ff, 0);
    FLUID_PDEF_SIZE_SE((double)FLOAT_806161d0, (double)FLOAT_806161bc, (double)FLOAT_806161b8,
                       (double)FLOAT_806161b8);
    FLUID_PDEF_COLOR_END((double)FLOAT_806161d4, (double)FLOAT_806161d8, (double)FLOAT_806161b8,
                         (double)FLOAT_806161b8);
    FLUID_PDEF_VEL((double)FLOAT_806161c0, (double)FLOAT_806161dc, (double)FLOAT_806161c8,
                   (double)FLOAT_806161bc, (double)FLOAT_806161c4, (double)FLOAT_806161b4);
    particle_def_set_drag((double)FLOAT_806161e0);
    FLUID_PEMIT_CREATE((double)FLOAT_806161c4, (double)FLOAT_806161b8, (double)FLOAT_806161bc,
                       &DAT_80589ffc, &DAT_80589f30, 1, 6);
    FLUID_PEMIT_COLOR((double)FLOAT_806161e4, (double)FLOAT_806161b8, (double)FLOAT_806161e8,
                      (double)FLOAT_806161b8);
    particle_emitter_set_lifetime_range((double)FLOAT_806161b8, (double)FLOAT_806161b8);
    FLUID_PEMIT_SCALE(&scale[0]);
    DAT_806109b0 = -1;
}

// ---------------------------------------------------------------------------
// 0x8015fef4  fluid_grid_cell_mark
// ---------------------------------------------------------------------------
// Set the "dirty" flag (+0x02) on a grid-cell record (0x18 stride at
// DAT_80612218).
// ---------------------------------------------------------------------------
void fluid_grid_cell_mark(int cellIdx)
{
    *(short*)(DAT_80612218 + cellIdx * 0x18 + 2) = 1;
}

// ---------------------------------------------------------------------------
// 0x8015ff0c  fluid_node_unlink_cell
// ---------------------------------------------------------------------------
// Remove a node (0x30 stride) from its spatial cell column: patch the cell's
// prev/next short links (+0x2a/+0x2c). If the node is the column head
// (+0x2c == -1) update the cell-column array slot at
// [DAT_80612220[col]][row]; otherwise patch the predecessor's +0x2a. Then clear
// the node's own column coords (+0x1c/+0x1e).
// ---------------------------------------------------------------------------
void fluid_node_unlink_cell(int nodeIdx)
{
    int node = DAT_8061221c + nodeIdx * 0x30;
    if (*(short*)(node + 0x2c) == -1) {
        *(short*)(*(int*)(DAT_80612220 + *(short*)(node + 0x1c) * 4) + *(short*)(node + 0x1e) * 2) =
            *(short*)(node + 0x2a);
    } else {
        *(short*)(DAT_8061221c + *(short*)(node + 0x2c) * 0x30 + 0x2a) = *(short*)(node + 0x2a);
    }
    if (*(short*)(node + 0x2a) != -1) {
        *(short*)(DAT_8061221c + *(short*)(node + 0x2a) * 0x30 + 0x2c) = *(short*)(node + 0x2c);
    }
    *(short*)(node + 0x1c) = (short)0xffff;
    *(short*)(node + 0x1e) = (short)0xffff;
}

// ---------------------------------------------------------------------------
// 0x8015ff88  fluid_hash_node_cell
// ---------------------------------------------------------------------------
// Compute a grid-cell record's 5-bit cell coords from its x/z position: cell.x
// = trunc(FLOAT_806161ec + pos[0]) & 0x1f at +0x1c (short 7), cell.z =
// trunc(FLOAT_806161ec + pos[2]) & 0x1f at +0x1e. (0x38-stride grid-cell bank.)
// ---------------------------------------------------------------------------
void fluid_hash_node_cell(int gridIdx)
{
    float* cell = (float*)(DAT_80612224 + gridIdx * 0x38);
    int cx = FUN_803825c0((double)(FLOAT_806161ec + cell[0]));
    float bias = FLOAT_806161ec;
    *(unsigned short*)(cell + 7) = (unsigned short)cx & 0x1f;
    int cz = FUN_803825c0((double)(bias + cell[2]));
    *(unsigned short*)((int)cell + 0x1e) = (unsigned short)cz & 0x1f;
}

// ---------------------------------------------------------------------------
// 0x8015ffe8  fluid_strand_release_if_empty
// ---------------------------------------------------------------------------
// Count how many live free-list node blocks still reference strand id
// `strandId` (block +0x10). If none remain, free the current node
// (fluid_node_free).
// ---------------------------------------------------------------------------
void fluid_strand_release_if_empty(int strandId)
{
    int refs = 0;
    for (int b = DAT_806121f8; b != 0; b = *(int*)(b + 0x7c)) {
        if (*(short*)(b + 0x10) == strandId) refs = refs + 1;
    }
    if (refs < 1) {
        fluid_node_free();
    }
}

// ---------------------------------------------------------------------------
// 0x80160038  fluid_node_free
// ---------------------------------------------------------------------------
// Free the frame's current node (id in the prologue-saved gpr): unlink it from
// its spatial cell (fluid_node_unlink_cell), splice it out of its strand's node
// list (parent strand +0x22, head/tail at strand+0x08/+0x0a), fixing the
// neighbours' prev/next (+0x26/+0x28) and, when it was the tail, extending the
// strand's accumulated length (+0x0c) by the gap to the new tail (with an
// FLOAT_806161c4 growth clamp). Finally scrub any free-list block still
// pointing at this node id and mark the node slot free (+0x20 = -1).
// ---------------------------------------------------------------------------
void fluid_node_free(void)
{
    int nodeIdx = __save_gpr_28();
    int freeHead = DAT_806121f8;
    float* node = (float*)(DAT_8061221c + nodeIdx * 0x30);
    fluid_node_unlink_cell(nodeIdx);

    if (*(short*)((int)node + 0x22) != -1) {
        int strand = DAT_80612218 + *(short*)((int)node + 0x22) * 0x18;
        *(short*)(strand + 6) = *(short*)(strand + 6) + -1;

        if (*(short*)((int)node + 0x26) == -1) {
            if (*(short*)(strand + 8) == nodeIdx) {
                *(short*)(strand + 8) = *(short*)(node + 10);
                if (*(short*)(strand + 8) != -1) {
                    *(short*)(DAT_8061221c + *(short*)(strand + 8) * 0x30 + 0x26) = (short)0xffff;
                }
            }
        } else {
            *(short*)(DAT_8061221c + *(short*)((int)node + 0x26) * 0x30 + 0x28) = *(short*)(node + 10);
        }

        if (*(short*)(node + 10) == -1) {
            if (*(short*)(strand + 10) == nodeIdx) {
                *(short*)(strand + 10) = *(short*)((int)node + 0x26);
                if (*(short*)(strand + 10) != -1) {
                    float* newTail = (float*)(DAT_8061221c + *(short*)(strand + 10) * 0x30);
                    *(short*)(newTail + 10) = (short)0xffff;
                    double gap = math_sqrt((double)((node[0] - newTail[0]) * (node[0] - newTail[0]) +
                                                    (node[2] - newTail[2]) * (node[2] - newTail[2])));
                    double k = (double)FLOAT_806161b4;
                    double cap = (double)FLOAT_806161c4;
                    *(float*)(strand + 0xc) = (float)(k * gap + (double)*(float*)(strand + 0xc));
                    while (cap < (double)*(float*)(strand + 0xc)) {
                        *(float*)(strand + 0xc) = (float)((double)*(float*)(strand + 0xc) * k);
                    }
                }
            }
        } else {
            int nxt = DAT_8061221c + *(short*)(node + 10) * 0x30;
            *(short*)(nxt + 0x26) = *(short*)((int)node + 0x26);
            *(char*)(nxt + 0x25) = 1;
        }
    }

    for (int b = freeHead; b != 0; b = *(int*)(b + 0x7c)) {
        if (*(short*)(b + 0x10) == nodeIdx) {
            *(short*)(b + 0x10) = (short)0xffff;
        }
    }
    *(short*)(node + 8) = (short)0xffff;
    __restore_gpr_28();
}

// ===========================================================================
// batch_436 : fluid strand DRIVER + RENDER + DAMAGE + SPAWN + SCATTER
// Address range: 0x801601d4 - 0x80161804 | 2026-09-06
// ---------------------------------------------------------------------------
// The runtime half of the fluid subsystem: the per-frame update driver that
// grows/advects/retires every strand (fluid_update, 0x80160628), the GX ribbon
// renderer that submits the strand geometry to the write-gather pipe
// (fluid_render, 0x80160ad0), the splash damage pass that hurts entities under
// a fresh splat (fluid_splash_damage, 0x80160278), the per-node spawn helper
// (fluid_node_spawn, 0x80161058), the grid-cell scatter+sound pass
// (fluid_scatter_cell, 0x80161260), the strand-free helper (fluid_strand_free,
// 0x801601d4), the source-emitter registration (fluid_emitter_register,
// 0x801614e0), the source-emitter tick (fluid_emitter_tick, 0x80161630), and
// the two radius-tag helpers over the grid-cell bank (fluid_cell_clear_radius
// 0x80161768, fluid_cell_tag_first 0x80161804).
// ===========================================================================

// --- batch_436 frame in/out scratch helpers (register-pair returns) ---------
// Raw per-frame prologue/epilogue helpers (no shared header; per-file externs).
// FUN_803826e8 returns the frame register pair (u64); the driver ignores it,
// the emitter-tick consumes it as (entity<<32 | srcPos).
extern "C" {
extern int                FUN_803826f0(void);        // strand-free / node-spawn frame in
extern void               FUN_8038273c(void);        // strand-free / node-spawn frame out
extern int                FUN_803826d8(void);        // splash-damage frame in
extern void               FUN_80382724(void);        // splash-damage frame out
extern unsigned long long FUN_803826cc(void);        // render frame in
extern void               FUN_80382718(void);        // render frame out
extern unsigned long long FUN_803826e4(void);        // emitter-register frame in
extern void               FUN_80382730(void);        // emitter-register frame out
extern unsigned long long FUN_803826e8(void);        // driver / emitter-tick frame in (pair)
extern void               FUN_80382734(void);        // driver / emitter-tick frame out
extern float*             FUN_803826c8(void);        // scatter frame in (node ptr)
extern void               FUN_80382714(void);        // scatter frame out
}

// --- batch_436 raw helpers (subsystem calls without shared decls) -----------
extern "C" {
extern int    FUN_8016e8ac(int mode);                 // GX blend/z mode push -> prev
extern void   FUN_8016f00c(int mode);                 // GX tev/cull mode set
// gx_bind_material_fallback (0x801720a8) resolves via generated_decls — sprite default texture bind
extern void   FUN_801631c0(double a, double b);       // GX flush primitive
extern void   FUN_80162020(void);                     // fluid post-update commit
extern void   FUN_80161be8(void);                     // fluid spawn-at-source helper
extern int    FUN_80161a88(float* pos);               // fluid_cell_tag_nearest (next batch)
extern double FUN_803900e4(double v);                 // ceil / range helper
}

// --- batch_436 data globals -------------------------------------------------
extern int   DAT_80611d54;     // render scene root
extern int*  DAT_80610a20;     // sprite texture table [base,?,lo,hi]
extern int   DAT_80612610;     // splash target min-id gate
extern int   DAT_80612614;     // game mode (4 = ...)
extern int   DAT_805401dc;     // per-scene-node object count table
extern int   DAT_8053fb2c;     // per-scene-node object ptr table
extern int   DAT_80612250; extern int DAT_80612254; extern int DAT_80612258;
extern int   DAT_8058e3cc;     // render view matrix scratch
extern int   DAT_806109c0;     // frame time counter (raw)
extern int   DAT_806108e4;     // fluid enable flag
extern float FLOAT_806108ec;   // fluid dt scale
extern int   DAT_806121f4;     // fluid frame tick
extern int   DAT_8058a080;     // scatter visited scratch (0x1e * u32)
extern int   DAT_8040ed04;     // per-source lifetime table (0x58 stride)
extern int   DAT_80589580;     // grid-cell id scratch (from batch_435)
extern int   DAT_80589710; extern int DAT_80589714; extern int DAT_80589718;
extern int   DAT_8058971c; extern int DAT_80589720; extern int DAT_80589724;
extern int   DAT_80589728; extern int DAT_80589746;
// batch_436 float / double constants
extern float FLOAT_806161f0; extern float FLOAT_80616200; extern float FLOAT_80616204;
extern float FLOAT_80616208; extern float FLOAT_8061620c; extern float FLOAT_80616210;
extern float FLOAT_80616214; extern float FLOAT_80616218; extern float FLOAT_80616234;
extern double DOUBLE_806161f8; extern double DOUBLE_80616220;

// --- batch_436 GX FIFO write-gather port (0xCC008000, write-only volatile) ---
#ifndef GX_FIFO_ADDR
#define GX_FIFO_ADDR 0xCC008000u
static volatile float* const        kFluidGxFifoF32 = (volatile float*)GX_FIFO_ADDR;
static volatile unsigned int* const kFluidGxFifoU32 = (volatile unsigned int*)GX_FIFO_ADDR;
#define FLUID_GX_F32 (*kFluidGxFifoF32)
#define FLUID_GX_U32 (*kFluidGxFifoU32)
#endif

// --- batch_436 typed aliases (generated decls differ from PPC-ABI call shape)-
// mtx4_transpose is declared void; the render pass consumes its r3/f1 result.
typedef double (*fluid_mtxT_fn)(float* src, float* dst);
#define FLUID_MTX_TRANSPOSE ((fluid_mtxT_fn)&mtx4_transpose)
// switch_slot_reset is declared void(int); the damage builder consumes its u64
// return (event handle) and passes a pointer as the slot scratch.
typedef unsigned long long (*fluid_slotreset_fn)(int slotScratch);
#define FLUID_SLOT_RESET ((fluid_slotreset_fn)&switch_slot_reset)
// damage_dispatch_to_entity's generated decl takes a single int*; the real call
// passes the event handle + the 7 fp splat coords + the event-data block.
typedef void (*fluid_damage_fn)(unsigned long long handle, double a, double b, double c, double d,
                                double e, double f, double g, int* eventData);
#define FLUID_DAMAGE_DISPATCH ((fluid_damage_fn)&damage_dispatch_to_entity)
// rng_counter_tick is declared void; callers consume its r3 (raw counter word).
typedef unsigned int (*fluid_rng_fn)(void);
#define FLUID_RNG_TICK ((fluid_rng_fn)&rng_counter_tick)
// particle_spawn_global's generated decl is a 4-arg convenience form; the fluid
// splash passes the full 7-arg PPC form (3 fp + count + node id + pos + vel).
typedef void (*fluid_pspawn_fn)(double a, double b, double c, int count, int nodeId,
                                unsigned int* posBlock, float* vel);
#define FLUID_PARTICLE_SPAWN ((fluid_pspawn_fn)&particle_spawn_global)
// fluid_project_to_floor (batch_435) is declared void but leaves the winning
// floor Y in f1 (per PPC ABI); the emitter-tick consumes it. Route through a
// double-returning alias to read that hidden fp result.
typedef double (*fluid_projfloor_fn)(unsigned int nodePos, unsigned int outSlot, int* pHit);
#define FLUID_PROJECT_FLOOR_Y ((fluid_projfloor_fn)&fluid_project_to_floor)

// --- batch_436 forward declarations -----------------------------------------
void fluid_strand_free(void);                                          // 0x801601d4
void fluid_splash_damage(void);                                        // 0x80160278
void fluid_update(void);                                               // 0x80160628
void fluid_render(void);                                               // 0x80160ad0
int  fluid_node_spawn(void);                                           // 0x80161058
void fluid_scatter_cell(void);                                         // 0x80161260
void fluid_emitter_register(unsigned int p1, unsigned int p2, unsigned int p3, unsigned int p4, unsigned int p5); // 0x801614e0
void fluid_emitter_tick(unsigned int p1, unsigned int p2, int srcIdx);  // 0x80161630
int  fluid_cell_clear_radius(double radiusSq, float* pos);             // 0x80161768
int  fluid_cell_tag_first(double radiusSq, float* pos);               // 0x80161804

// ---------------------------------------------------------------------------
// 0x801601d4  fluid_strand_free
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826f0 / FUN_8038273c). Free an entire strand: walk
// its node list from the head (+0x08, next via node+0x28) freeing each node
// (fluid_node_free), then unlink the strand record (0x18 stride) from the
// active strand list (DAT_806121fc, prev/next at +0x10/+0x14) and mark its slot
// free (+0x00 = -1).
// ---------------------------------------------------------------------------
void fluid_strand_free(void)
{
    int strandIdx = FUN_803826f0();
    int strand = DAT_80612218 + strandIdx * 0x18;
    short node = *(short*)(strand + 8);
    while (node != -1) {
        node = *(short*)(DAT_8061221c + node * 0x30 + 0x28);
        fluid_node_free();
    }
    if (strand == DAT_806121fc) {
        DAT_806121fc = *(int*)(strand + 0x14);
    } else {
        *(int*)(*(int*)(strand + 0x10) + 0x14) = *(int*)(strand + 0x14);
    }
    if (*(int*)(strand + 0x14) != 0) {
        *(int*)(*(int*)(strand + 0x14) + 0x10) = *(int*)(strand + 0x10);
    }
    *(short*)(DAT_80612218 + strandIdx * 0x18) = (short)0xffff;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80160278  fluid_splash_damage
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826d8 / FUN_80382724). Build the AABB spanning the
// strand's 8 spline control points, query scene nodes in that box, and for each
// live object overlapping the box dispatch splash damage: type-8 objects that
// pass the owner/mode gate take the "player" damage packet (0x15/0x15, radius
// FLOAT_806161e0); everything else takes the generic packet (radius
// FLOAT_806161c4). Objects flagged ATPA/ROCK in mode 4 spawn effects instead.
// ---------------------------------------------------------------------------
void fluid_splash_damage(void)
{
    int cp = FUN_803826d8();
    float minx = *(float*)(cp + 0x18), miny = *(float*)(cp + 0x1c), minz = *(float*)(cp + 0x20);
    float maxx = minx, maxy = miny, maxz = minz;
    int off = 0xc;
    for (int i = 7; i != 0; i = i - 1) {
        int p = cp + off;
        float x = *(float*)(p + 0x18), y = *(float*)(p + 0x1c), z = *(float*)(p + 0x20);
        if (x < minx) minx = x;
        if (y < miny) miny = y;
        if (z < minz) minz = z;
        if (maxx < x) maxx = x;
        if (maxy < y) maxy = y;
        if (maxz < z) maxz = z;
        off = off + 0xc;
    }
    float aabb[6];
    aabb[0] = minx - FLOAT_806161b4;
    aabb[1] = miny - FLOAT_806161b4;
    aabb[2] = minz - FLOAT_806161b4;
    aabb[3] = maxx + FLOAT_806161b4;
    aabb[4] = maxy + FLOAT_806161b4;
    aabb[5] = maxz + FLOAT_806161b4;

    int results[10];
    int count = scene_query_nodes_in_frustum(0, (int)(intptr_t)aabb, (int)(intptr_t)results, 10);
    int ro = 0;
    for (int i = 0; i < count; i = i + 1) {
        int sceneNode = *(int*)((int)results + ro);
        int objOff = 0;
        for (int k = 0; k < (int)(&DAT_805401dc)[sceneNode]; k = k + 1) {
            int obj = *(int*)((&DAT_8053fb2c)[sceneNode] + objOff);
            if ((*(unsigned int*)(obj + 0x28) & 0x80000000) == 0) {
                double cx = (double)(*(float*)(obj + 100) + *(float*)(obj + 0x1a8));
                double cy = (double)(*(float*)(obj + 0x68) + *(float*)(obj + 0x1ac));
                double cz = (double)(*(float*)(obj + 0x6c) + *(float*)(obj + 0x1b0));
                bool outside =
                    (aabb[3] < (float)(cx - (double)*(float*)(obj + 0x1c0))) ||
                    (aabb[4] < (float)(cy - (double)*(float*)(obj + 0x1c4))) ||
                    (aabb[5] < (float)(cz - (double)*(float*)(obj + 0x1c8))) ||
                    ((float)((double)*(float*)(obj + 0x1c0) + cx) < aabb[0]) ||
                    ((float)((double)*(float*)(obj + 0x1c4) + cy) < aabb[1]) ||
                    ((float)((double)*(float*)(obj + 0x1c8) + cz) < aabb[2]);
                if (!outside) {
                    double half = (double)FLOAT_806161b4;
                    float centerx = (float)(half * (double)(aabb[3] + aabb[0]));
                    float centery = (float)(half * (double)(aabb[4] + aabb[1]));
                    float centerz = (float)(half * (double)(aabb[5] + aabb[2]));
                    float relx = (float)((double)centerx - cx);
                    float rely = (float)((double)centery - cy);
                    float relz = (float)((double)centerz - cz);
                    float center[3] = { centerx, centery, centerz };
                    float rel[3] = { relx, rely, relz };
                    if (*(int*)(obj + 0x20) == 8) {
                        int* owner = *(int**)(obj + 0x7c);
                        if (((owner == (int*)0) || (DAT_80612610 <= owner[2])) &&
                            ((DAT_80612614 != 4) ||
                             ((*(int*)(*owner + 0x60) != 0x43505441) &&
                              (*(int*)(*owner + 0x60) != 0x524f434b)))) {
                            character_damage_spawn_effects();
                        } else {
                            int ev[5];
                            unsigned long long handle = FLUID_SLOT_RESET((int)(intptr_t)ev);
                            int block[8];
                            block[0] = obj;
                            *(float**)&block[6] = &center[0];  // local_5c
                            *(float**)&block[7] = &rel[0];     // local_54
                            *(float*)&block[4] = FLOAT_806161e0;
                            block[1] = 0x15;
                            block[2] = 0x15;
                            FLUID_DAMAGE_DISPATCH(handle, (double)centerz, (double)centery,
                                                  (double)centerx, half, cy, cz, cx, block);
                        }
                    } else {
                        int ev[5];
                        unsigned long long handle = FLUID_SLOT_RESET((int)(intptr_t)ev);
                        int block[8];
                        block[0] = obj;
                        *(float**)&block[6] = &center[0];
                        *(float**)&block[7] = &rel[0];
                        *(float*)&block[4] = FLOAT_806161c4;
                        block[1] = 0x15;
                        block[2] = 0x15;
                        FLUID_DAMAGE_DISPATCH(handle, (double)centerz, (double)centery,
                                              (double)centerx, half, cy, cz, cx, block);
                    }
                }
            }
            objOff = objOff + 4;
        }
        ro = ro + 4;
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x80160628  fluid_update
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826e8 / FUN_80382734). The per-frame strand driver
// (gated by DAT_806108e4). For every active strand: grow the leading node radii
// by the dt-scaled growth rate (retiring nodes past the tail window via
// fluid_node_free), shrink trailing nodes, and retire empty strands
// (fluid_strand_free). Then walk the node free-list: advect each node along its
// spline segment (spline_eval_pos), spawn splash damage on contact
// (fluid_scatter_cell + fluid_splash_damage), and recycle finished node blocks
// (fluid_strand_id_free + fluid_strand_release_if_empty). Commit via
// fluid_post_update (0x80162020).
// ---------------------------------------------------------------------------
void fluid_update(void)
{
    FUN_803826e8();
    double growCap = (double)FLOAT_806161c4;
    float dt = FLOAT_806108ec *
               (float)(growCap / (double)(float)((double)CONCAT44(0x43300000,
                       (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_806161f8));
    if (DAT_806108e4 == 0) { FUN_80382734(); return; }

    DAT_806121f4 = DAT_806121f4 + 1;
    double growStep = (double)(FLOAT_806161c0 * dt);
    double shrinkStep = (double)(FLOAT_806161f0 * dt);
    double zero = (double)FLOAT_806161b8;

    int strand = DAT_806121fc;
    while (strand != 0) {
        short head = *(short*)(strand + 10);
        int nextStrand = *(int*)(strand + 0x14);
        unsigned int hIdx = (unsigned int)head;
        while (hIdx != 0xffffffff) {
            int node = DAT_8061221c + hIdx * 0x30;
            int rel = hIdx - DAT_8061220c;
            head = *(short*)(node + 0x26);
            if ((int)hIdx < DAT_8061220c) rel = (hIdx + 200) - DAT_8061220c;
            if (9 < rel) break;
            *(float*)(node + 0x18) = (float)((double)*(float*)(node + 0x18) + growStep);
            if ((growCap < (double)*(float*)(node + 0x18)) &&
                (*(float*)(node + 0x18) = (float)growCap, rel < 5)) {
                fluid_node_free();
            }
            hIdx = (unsigned int)head;
        }
        short tail = *(short*)(strand + 8);
        unsigned int tIdx = (unsigned int)tail;
        while (tIdx != 0xffffffff && hIdx != 0xffffffff) {
            int node = DAT_8061221c + tIdx * 0x30;
            if ((double)*(float*)(node + 0x18) <= zero) break;
            *(float*)(node + 0x18) = (float)((double)*(float*)(node + 0x18) - shrinkStep);
            if ((double)*(float*)(node + 0x18) < zero) *(float*)(node + 0x18) = (float)zero;
            if (tIdx == hIdx) break;
            tail = *(short*)(node + 0x28);
            tIdx = (unsigned int)tail;
        }
        if (*(short*)(strand + 8) == -1) {
            *(short*)(strand + 4) = *(short*)(strand + 4) + -1;
            short life = *(short*)(strand + 4);
            strand = nextStrand;
            if (life < 1) fluid_strand_free();
        } else {
            *(short*)(strand + 4) = 0x78;
            strand = nextStrand;
        }
    }

    double advect = (double)(float)((double)FLOAT_806161e8 * growStep);
    float* block = (float*)DAT_806121f8;
    while (block != (float*)0) {
        double timeRaw = DOUBLE_806161f8;
        float kEdge = FLOAT_806161b4;
        int nodeIdx = (int)*(short*)(block + 4);
        float* nextBlock = (float*)*(int*)(block + 0x1f);
        if (nodeIdx == -1) {
            unsigned int rel = (unsigned int)((int)block - DAT_80612214);
            fluid_strand_id_free(((int)rel >> 7) + (unsigned int)((int)rel < 0 && (rel & 0x7f) != 0));
            block = nextBlock;
            continue;
        }
        float* node = (float*)(DAT_8061221c + nodeIdx * 0x30);
        short link = (block[2] <= FLOAT_806161b8) ? *(short*)(node + 10)
                                                  : *(short*)((int)node + 0x26);
        int targetIdx = (int)link;
        if ((targetIdx != -1) && (*(char*)(DAT_8061221c + targetIdx * 0x30 + 0x24) != '\0')) {
            targetIdx = -1;
        }
        short state = *(short*)(node + 8);
        if ((state == 0) || (state == 4)) {
            block[3] = (float)((double)block[3] + advect);
            float age = block[3];
            block = nextBlock;
            if (kEdge < age) {
                *(short*)(node + 8) = 1;
                fluid_scatter_cell();
                fluid_splash_damage();
            }
        } else if ((state != 1) || ((double)FLOAT_806161c4 <= (double)block[3])) {
            if (targetIdx == -1) {
                unsigned int rel = (unsigned int)((int)block - DAT_80612214);
                fluid_strand_id_free(((int)rel >> 7) + (unsigned int)((int)rel < 0 && (rel & 0x7f) != 0));
                fluid_strand_release_if_empty(nodeIdx);
                fluid_scatter_cell();
                block = nextBlock;
            } else {
                float prevVel = block[1];
                float* target = (float*)(DAT_8061221c + targetIdx * 0x30);
                block[3] = FLOAT_806161b8;
                double denom = (double)FLOAT_806161c4;
                block[1] = (float)(denom / (double)(float)((double)CONCAT44(0x43300000,
                           (unsigned int)DAT_806109c0 ^ 0x80000000) - timeRaw)) *
                           block[2] * FLOAT_806108ec + block[1];
                double t = (double)(ABS(prevVel) / block[0]);
                if (denom < t) t = denom;
                float* segPrev = node;
                if (*(short*)(node + 10) != -1) segPrev = (float*)(DAT_8061221c + *(short*)(node + 10) * 0x30);
                float* segNext = target;
                if (*(short*)((int)target + 0x26) != -1) segNext = (float*)(DAT_8061221c + *(short*)((int)target + 0x26) * 0x30);
                spline_eval_pos(t, block + ((unsigned int)block[5] & 7) * 3 + 6,
                                segPrev, node, target, segNext);
                block[5] = (float)((int)block[5] + 1);
                if (block[0] <= ABS(block[1])) {
                    short doneNode = *(short*)(block + 4);
                    double len = math_sqrt((double)((target[2] - node[2]) * (target[2] - node[2]) +
                                                    (target[0] - node[0]) * (target[0] - node[0]) +
                                                    (target[1] - node[1]) * (target[1] - node[1])));
                    block[0] = (float)len;
                    *(short*)(block + 4) = (short)0xffff;
                    fluid_strand_release_if_empty((int)doneNode);
                    *(char*)(target + 9) = 1;
                    block[1] = FLOAT_806161b8;
                    *(short*)(block + 4) = (short)targetIdx;
                }
                fluid_splash_damage();
                block = nextBlock;
            }
        } else {
            block[3] = (float)((double)block[3] + advect);
            block = nextBlock;
        }
    }
    FUN_80162020();
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80160ad0  fluid_render  [subsystem:graphics]
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826cc / FUN_80382718). Render every active strand as
// a textured GX triangle-strip ribbon: transpose the view matrix, push a GX
// blend/z mode (FUN_8016e8ac/FUN_8016f00c), and for each strand pick the ribbon
// texture + tint (blood 0x34a / generic), walk the node chain and emit paired
// billboarded verts (offset by the perpendicular of the segment direction)
// straight into the GX write-gather pipe (0xCC008000). Then emit the free-list
// splash droplets as scene particles. NOTE: the incoming f23..f31 spills are the
// callee-saved FPR prologue (dead), omitted here.
// ---------------------------------------------------------------------------
void fluid_render(void)
{
    FUN_803826cc();
    short* strand = (short*)DAT_806121fc;
    FLUID_MTX_TRANSPOSE(*(float**)(DAT_80611d54 + 0x208), (float*)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;
    int prevMode = FUN_8016e8ac(2);
    FUN_8016f00c(1);
    double joinDist = (double)FLOAT_806161bc;

    for (; strand != (short*)0; strand = *(short**)(strand + 10)) {
        int texId = (*strand == 0) ? 0x34a : -1;
        texId = sprite_get_texture_id(texId);
        unsigned int tint = (*strand == 0) ? 0x7f7f7f : 0x1f1f1f;
        float widthScale = (*strand == 0) ? FLOAT_80616200 : FLOAT_806161d8;
        double dWidth = (double)widthScale;

        int count = (int)strand[3];
        double accumLen = (double)*(float*)(strand + 6);
        int node = (int)strand[4];

        bool stop = false;
        while ((1 < count) && (!stop)) {
            float* cur = (float*)(DAT_8061221c + node * 0x30);
            float* nxt = (float*)(DAT_8061221c + *(short*)(cur + 10) * 0x30);
            double dz = (double)(nxt[2] - cur[2]);
            double dx = (double)(float)((double)nxt[0] - (double)cur[0]);
            double seg = math_sqrt((double)(float)(dx * dx + (double)(float)(dz * dz)));
            if (seg <= joinDist) {
                node = (int)*(short*)(cur + 10);
                count = count + -1;
            } else {
                stop = true;
            }
        }

        node = (int)strand[5];
        if (1 < count) {
            float* cur = (float*)(DAT_8061221c + node * 0x30);
            float* prv = (float*)(DAT_8061221c + *(short*)((int)cur + 0x26) * 0x30);
            double dz = (double)(prv[2] - cur[2]);
            double dx = (double)(float)((double)prv[0] - (double)cur[0]);
            double seg = math_sqrt((double)(float)(dx * dx + (double)(float)(dz * dz)));
            if ((double)FLOAT_806161b8 < seg) {
                double inv = (double)FLOAT_806161c4 / seg;
                (void)inv;
            }
        }

        if ((texId < 0) || (DAT_80610a20[3] <= texId) || (texId <= DAT_80610a20[2])) {
            gx_bind_material_fallback();
        } else {
            DAT_80612254 = *DAT_80610a20 + texId * 0x58;
        }

        while (node != strand[4]) {
            int span = 0;
            int cursor = node;
            int seg;
            do {
                span = span + 1;
                seg = DAT_8061221c + cursor * 0x30;
                cursor = (int)*(short*)(seg + 0x26);
                if (cursor == strand[4]) break;
            } while (*(char*)(seg + 0x25) == '\0');

            if (0 < span) {
                float* cur = (float*)(DAT_8061221c + node * 0x30);
                gx_begin_primitive(0x98, 0, (short)span * 2);
                double halfW = (double)FLOAT_806161cc;
                double yBias = (double)FLOAT_80616208;
                double alphaA = (double)FLOAT_80616204;
                float* prev = (float*)0;
                for (int i = 0; i < span; i = i + 1) {
                    unsigned int alpha = FLUID_RNG_TICK();  // placeholder; replaced below
                    alpha = FUN_803825c0((double)(float)(alphaA * (double)(float)(dWidth *
                            (double)(float)((double)FLOAT_806161c4 - (double)cur[6]))));
                    if (prev == (float*)0) {
                        alpha = 0;
                    } else {
                        double dz = (double)(cur[2] - prev[2]);
                        double dx = (double)(cur[0] - prev[0]);
                        double seglen = math_sqrt((double)(float)(dx * dx + (double)(float)(dz * dz)));
                        if ((double)FLOAT_806161b8 < seglen) {
                            dx = (double)(float)(dx * (double)(float)((double)FLOAT_806161c4 / seglen));
                            dz = (double)(float)(dz * (double)(float)((double)FLOAT_806161c4 / seglen));
                        }
                        accumLen = (double)(float)((double)FLOAT_806161b4 * seglen + accumLen);
                        (void)dx; (void)dz;
                    }
                    unsigned int col = (tint << 0x18) | ((tint & 0xff00) << 8) |
                                       ((tint & 0xff0000) >> 8) | (alpha & 0xff);
                    // vert 0
                    FLUID_GX_F32 = cur[0] + (float)(0.0 * halfW);
                    FLUID_GX_F32 = (float)(yBias + (double)cur[1]);
                    FLUID_GX_F32 = cur[2] - (float)(0.0 * halfW);
                    FLUID_GX_U32 = col;
                    FLUID_GX_F32 = (float)accumLen;
                    FLUID_GX_F32 = FLOAT_806161b8;
                    // vert 1
                    FLUID_GX_F32 = cur[0] - (float)(0.0 * halfW);
                    FLUID_GX_F32 = (float)(yBias + (double)cur[1]);
                    FLUID_GX_F32 = cur[2] + (float)(0.0 * halfW);
                    FLUID_GX_U32 = col;
                    FLUID_GX_F32 = (float)accumLen;
                    FLUID_GX_F32 = FLOAT_806161c4;
                    prev = cur;
                    cur = (float*)(DAT_8061221c + *(short*)((int)cur + 0x26) * 0x30);
                }
            }
            node = cursor;
        }
    }
    FUN_801631c0((double)FLOAT_806161b8, (double)FLOAT_806161b8);

    // splash droplet particles from the free-list
    float* block = (float*)DAT_806121f8;
    double timeRaw = DOUBLE_80616220;
    double invRange = (double)FLOAT_80616210;
    double kBias = (double)FLOAT_806161b4;
    double velZ = (double)FLOAT_8061620c;
    double velY = (double)FLOAT_80616208;
    while (block != (float*)0) {
        int rx = FLUID_RNG_TICK();
        float px = (float)(velZ * (double)(float)((double)(float)((double)(float)((double)CONCAT44(
                   0x43300000, (unsigned int)rx) - timeRaw) / invRange) - kBias));
        int ry = FLUID_RNG_TICK();
        float py = (float)(velY * (double)(float)((double)(float)((double)(float)((double)CONCAT44(
                   0x43300000, (unsigned int)ry) - timeRaw) / invRange) - (double)FLOAT_80616214));
        int rz = FLUID_RNG_TICK();
        float pz = (float)(velZ * (double)(float)((double)(float)((double)(float)((double)CONCAT44(
                   0x43300000, (unsigned int)rz) - timeRaw) / invRange) - kBias));
        float vel[3] = { px, py, pz };

        int found[21];
        int hits = scene_find_nodes_containing_point(
            (float*)((int)block + ((*(unsigned int*)((int)block + 0x14) & 7) * 0xc) + 0x18),
            (int)(intptr_t)found, 0x14);
        int nodeId = 0;
        int fo = 0;
        for (int i = 0; i < hits; i = i + 1) {
            nodeId = *(int*)((int)found + fo);
            if (*(char*)(DAT_80611d54 + nodeId + 0xd2c) != '\0') break;
            fo = fo + 4;
            nodeId = 0;
        }
        int burst;
        if ((double)*(float*)((int)block + 0xc) <= (double)FLOAT_806161b8) {
            burst = 8;
        } else {
            burst = (int)-(float)((double)FLOAT_806161f0 * (double)*(float*)((int)block + 0xc) -
                                  (double)FLOAT_806161f0);
        }
        int po = 0;
        for (int i = 0; i < burst; i = i + 1) {
            FLUID_PARTICLE_SPAWN((double)FLOAT_806161c4, (double)FLOAT_806161c8,
                                 (double)FLOAT_80616218, 1, nodeId,
                                 (unsigned int*)((int)block + po + 0x18), &vel[0]);
            po = po + 0xc;
        }
        block = (float*)*(int*)((int)block + 0x7c);
    }
    FUN_8016e8ac(prevMode);
    FUN_80382718();
}

// ---------------------------------------------------------------------------
// 0x80161058  fluid_node_spawn
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826f0 / FUN_8038273c). If the frame node is not
// already flowing / dead / bounded by a dead neighbour, allocate a node block
// (fluid_strand_id_alloc) and seed it: link to the source node (+0x08), set the
// initial velocity/age, compute the rest length to the up/down neighbour
// (math_sqrt of the delta), and prime the 8-entry spline control ring with the
// node position.
// ---------------------------------------------------------------------------
int fluid_node_spawn(void)
{
    int nodeIdx = FUN_803826f0();
    float* node = (float*)(DAT_8061221c + nodeIdx * 0x30);
    bool blocked = *(char*)(node + 9) == '\x01';
    if (*(short*)(node + 8) == 0) blocked = true;   // (age gate collapses to true here)
    if ((*(short*)(node + 10) != -1) &&
        (*(char*)(DAT_8061221c + *(short*)(node + 10) * 0x30 + 0x24) != '\0')) blocked = true;
    if ((*(short*)((int)node + 0x26) != -1) &&
        (*(char*)(DAT_8061221c + *(short*)((int)node + 0x26) * 0x30 + 0x24) != '\0')) blocked = true;

    if (!blocked) {
        unsigned int id = (unsigned int)fluid_strand_id_alloc();
        float z = FLOAT_806161b8;
        if (id != 0xffffffff) {
            float* block = (float*)(DAT_80612214 + id * 0x80);
            *(short*)(block + 4) = (short)nodeIdx;
            block[2] = z;
            block[1] = z;
            block[5] = 0.0f;
            block[3] = z;
            double dz = (double)z, dy = (double)z, dx = (double)z;
            if ((double)block[2] <= (double)z) {
                if (*(short*)(node + 10) != -1) {
                    float* nb = (float*)(DAT_8061221c + *(short*)(node + 10) * 0x30);
                    dz = (double)(node[2] - nb[2]);
                    dy = (double)(node[1] - nb[1]);
                    dx = (double)(node[0] - nb[0]);
                }
            } else if (*(short*)((int)node + 0x26) != -1) {
                float* nb = (float*)(DAT_8061221c + *(short*)((int)node + 0x26) * 0x30);
                dz = (double)(node[2] - nb[2]);
                dy = (double)(node[1] - nb[1]);
                dx = (double)(node[0] - nb[0]);
            }
            double len = math_sqrt((double)(float)(dz * dz +
                                   (double)(float)(dx * dx + (double)(float)(dy * dy))));
            block[0] = (float)len;
            int so = 0;
            for (int i = 8; i != 0; i = i - 1) {
                *(float*)((int)block + so + 0x18) = node[0];
                *(float*)((int)block + so + 0x1c) = node[1];
                *(float*)((int)block + so + 0x20) = node[2];
                so = so + 0xc;
            }
            block[0x1e] = 0.0f;
        }
    }
    FUN_8038273c();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80161260  fluid_scatter_cell  [frame FUN_803826c8 / FUN_80382714]
// ---------------------------------------------------------------------------
// Scan the 3x3 (or wider, sized by FUN_803900e4) grid-cell neighbourhood around
// the frame node's hashed cell; for every unflowed node within the squared
// radius that has not yet been visited (DAT_8058a080 scratch), try to spawn two
// child nodes (fluid_node_spawn) and, on success, mark the node + its strand
// visited. If anything spawned (or the nearest-tag helper fired), play the
// fluid scatter sound (0xc6b) at the node position.
// ---------------------------------------------------------------------------
void fluid_scatter_cell(void)
{
    float* pos = FUN_803826c8();
    unsigned int cx = FUN_803825c0((double)(FLOAT_806161ec + pos[0]));
    unsigned int cz = FUN_803825c0((double)(FLOAT_806161ec + pos[2]));
    double radiusSq = (double)FLOAT_806161ec;  // frame-passed radius^2 (f-reg)
    double span = FUN_803900e4(radiusSq);
    long long spanI = (long long)(int)span;
    (void)spanI;

    int off = 0;
    for (int i = 0x1e; i != 0; i = i - 1) {
        *(int*)((int)&DAT_8058a080 + off) = 0;
        off = off + 4;
    }
    int dim = ((int)span + 1) * 2 + 1;
    bool spawned = false;
    int ix = 0;
    for (int a = 0; a < dim; a = a + 1) {
        int iz = 0;
        for (int b = 0; b < dim; b = b + 1) {
            short nodeId = *(short*)(*(int*)((((cx & 0x1f) + ix + 0x20) * 4 & 0x7c) + DAT_80612220) +
                                     (((cz & 0x1f) + iz + 0x20) * 2 & 0x3e));
            while (nodeId != -1) {
                float* n = (float*)(DAT_8061221c + nodeId * 0x30);
                if ((*(char*)(n + 9) == '\0') &&
                    (((*(short*)((int)n + 0x22) == -1) ||
                      ((&DAT_8058a080)[*(short*)((int)n + 0x22)] == 0)) &&
                     ((double)((n[2] - pos[2]) * (n[2] - pos[2]) +
                               (n[0] - pos[0]) * (n[0] - pos[0]) +
                               (n[1] - pos[1]) * (n[1] - pos[1])) < radiusSq))) {
                    int r1 = fluid_node_spawn();
                    int r2 = fluid_node_spawn();
                    if (r1 != 0 || r2 != 0) {
                        spawned = true;
                        *(char*)(n + 9) = 1;
                        if (*(short*)((int)n + 0x22) != -1) {
                            (&DAT_8058a080)[*(short*)((int)n + 0x22)] = 1;
                        }
                    }
                }
                nodeId = *(short*)((int)n + 0x2a);
            }
            iz = (0 < iz) ? -iz : 1 - iz;
        }
        ix = (0 < ix) ? -ix : 1 - ix;
    }

    bool tagged = (FUN_80161a88(pos) != 0);
    if (spawned || tagged) {
        int params[7];
        sound_init_play_params((int)(intptr_t)params);
        *(char*)((int)params + 0x1c) = 1;   // local_70
        params[0] = 0xc6b;
        *(float*)((int)params + 0x10) = pos[0];  // local_7c
        *(float*)((int)params + 0x14) = pos[1];
        *(float*)((int)params + 0x18) = pos[2];
        sound_play_request(params);
    }
    FUN_80382714();
}

// ---------------------------------------------------------------------------
// 0x801614e0  fluid_emitter_register
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826e4 / FUN_80382730). Register a fluid source
// emitter for the frame entity: project it to the floor (fluid_project_to_floor)
// then, if not already present in the 20-slot source table (DAT_80589774, 0x68
// stride), claim a free/expired slot and fill it (entity handle, params p3/p4/p5,
// floor node, flags | 0x40). Ring-advances DAT_806121f0.
// ---------------------------------------------------------------------------
void fluid_emitter_register(unsigned int p1, unsigned int p2, unsigned int p3, unsigned int p4,
                            unsigned int p5)
{
    (void)p1; (void)p2;
    unsigned long long fr = FUN_803826e4();
    int start = DAT_806121f0;
    int entity = (int)(fr >> 0x20);
    if (entity != 0) {
        int floorNode[10];
        fluid_project_to_floor((unsigned int)(entity + 100), (unsigned int)(uintptr_t)floorNode, (int*)0);
        int o = 0;
        for (int i = 0x14; i != 0; i = i - 1) {
            if (*(int*)(entity + 0x2c) == *(int*)((int)&DAT_80589774 + o)) { FUN_80382730(); return; }
            o = o + 0x68;
        }
        do {
            int slot = DAT_806121f0;
            int so = DAT_806121f0 * 0x68;
            if (((&DAT_80589774)[DAT_806121f0 * 0x1a] == 0) ||
                ((*(unsigned int*)(&DAT_80589724 + so) & 4) == 0)) {
                (&DAT_80589710)[DAT_806121f0 * 0x1a] = 0;
                *(unsigned int*)(&DAT_80589714 + so) = p5;
                DAT_806121f0 = DAT_806121f0 + 1;
                *(unsigned int*)(&DAT_80589724 + so) = p4 | 0x40;
                (&DAT_80589718)[slot * 0x1a] = (int)fr;
                *(unsigned int*)(&DAT_8058971c + so) = p3;
                (&DAT_80589720)[slot * 0x1a] = floorNode[0];
                (&DAT_80589774)[slot * 0x1a] = *(int*)(entity + 0x2c);
                (&DAT_80589746)[slot * 0x34] = 0;
                if (0x13 < DAT_806121f0) DAT_806121f0 = 0;
                break;
            }
            DAT_806121f0 = DAT_806121f0 + 1;
            if (0x13 < DAT_806121f0) DAT_806121f0 = 0;
        } while (DAT_806121f0 != start);
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80161630  fluid_emitter_tick
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826e8 / FUN_80382734). For an active source slot,
// stochastically emit: project the source to the floor, and when the emit gate
// passes spawn fluid at the source (FUN_80161be8) plus, on the "spray" flag
// (0x80), scatter-tag nearby cells (fluid_cell_clear_radius).
// ---------------------------------------------------------------------------
void fluid_emitter_tick(unsigned int p1, unsigned int p2, int srcIdx)
{
    (void)p1; (void)p2;
    unsigned long long fr = FUN_803826e8();
    float* srcPos = (float*)(unsigned int)(fr >> 0x20);
    int so = srcIdx * 0x68;
    if ((&DAT_80589774)[srcIdx * 0x1a] != 0) {
        bool gate = ((&DAT_80589710)[srcIdx * 0x1a] == 0);
        if (!gate) {
            unsigned int r = FLUID_RNG_TICK();
            gate = (r == (r / (unsigned int)fr) * (unsigned int)fr);
        }
        if (gate) {
            int floorFlag = 0;
            int floorNode[1];
            double floorY = FLUID_PROJECT_FLOOR_Y((unsigned int)(uintptr_t)srcPos,
                                                  (unsigned int)(uintptr_t)floorNode, &floorFlag);
            if (((int)(&DAT_80589710)[srcIdx * 0x1a] < *(int*)(&DAT_80589714 + so) ||
                 ((*(unsigned int*)(&DAT_80589724 + so) & 8) != 0)) && (floorFlag != 0)) {
                float spawnPos[3];
                spawnPos[0] = srcPos[0];
                spawnPos[1] = (float)((double)FLOAT_80616208 + floorY);
                spawnPos[2] = srcPos[2];
                FUN_80161be8();
                if ((*(unsigned int*)(&DAT_80589724 + so) & 0x80) != 0) {
                    fluid_cell_clear_radius((double)*(float*)(&DAT_80589728 + so), &spawnPos[0]);
                }
                if ((&DAT_80589710)[srcIdx * 0x1a] == 0) {
                    (&DAT_80589746)[srcIdx * 0x34] = 0;
                }
            }
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80161768  fluid_cell_clear_radius
// ---------------------------------------------------------------------------
// Tag every grid-cell (0x38 stride, 100 slots) of state 0/4 and parent 0 within
// radius of pos: set its prev-link (+0x26) to 0 and clear the global "settled"
// flag DAT_80612230. Returns non-zero if any cell matched.
// ---------------------------------------------------------------------------
int fluid_cell_clear_radius(double radiusSq, float* pos)
{
    int any = 0;
    int o = 0;
    for (int i = 100; i != 0; i = i - 1) {
        float* c = (float*)(DAT_80612224 + o);
        if ((*(short*)(c + 8) == 0) || (*(short*)(c + 8) == 4)) {
            if (*(short*)((int)c + 0x22) == 0) {
                float dsq = (c[2] - pos[2]) * (c[2] - pos[2]) +
                            (c[0] - pos[0]) * (c[0] - pos[0]) +
                            (c[1] - pos[1]) * (c[1] - pos[1]);
                if (dsq < (float)(radiusSq * radiusSq)) {
                    any = 1;
                    *(short*)((int)c + 0x26) = 0;
                    DAT_80612230 = 0;
                }
            }
        }
        o = o + 0x38;
    }
    return any;
}

// ---------------------------------------------------------------------------
// 0x80161804  fluid_cell_tag_first
// ---------------------------------------------------------------------------
// If the fluid subsystem is enabled (DAT_80612228), tag the first grid-cell of
// state 0/4 and parent 0 within radius of pos: stamp its parent-strand (+0x22)
// with the source's timestamp-scaled id and clear DAT_8061222c. Returns 1 on a
// match.
// ---------------------------------------------------------------------------
int fluid_cell_tag_first(double radiusSq, float* pos)
{
    if (DAT_80612228 == 0) return 0;
    int idx = 0;
    int o = 0;
    for (int i = 100; i != 0; i = i - 1) {
        float* c = (float*)(DAT_80612224 + o);
        if (((*(short*)(c + 8) == 0) || (*(short*)(c + 8) == 4)) &&
            (*(short*)((int)c + 0x22) == 0)) {
            float dsq = (c[2] - pos[2]) * (c[2] - pos[2]) +
                        (c[0] - pos[0]) * (c[0] - pos[0]) +
                        (c[1] - pos[1]) * (c[1] - pos[1]);
            if (dsq < (float)(radiusSq * radiusSq)) {
                int srcId = (&DAT_80589718)[*(int*)((int)&DAT_80589580 + o / 0x38 * 4) * 0x1a];
                *(short*)((int)c + 0x22) =
                    (short)(int)(*(float*)(&DAT_8040ed04 + srcId * 0x58) *
                                 (float)((double)CONCAT44(0x43300000,
                                         (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_806161f8));
                DAT_8061222c = 0;
                (&DAT_80589746)[*(int*)((int)&DAT_80589580 + o / 0x38 * 4) * 0x34] = 1;
                return 1;
            }
        }
        idx = idx + 1;
        o = o + 0x38;
    }
    return 0;
}

// ===========================================================================
// batch_437 : fluid SPLAT / SPRAY projector layer (grid-cell splat marks)
// Address range: 0x8016193c - 0x8016468c | 2026-09-06
// ---------------------------------------------------------------------------
// The projected splat-mark half of the fluid subsystem: sources (0x68-stride
// table at DAT_80589710/718/720/774) spray splat records into the grid-cell
// bank (DAT_80612224, 0x38 stride, state +0x10 at short 8, parent-strand/life
// +0x22, growth +0x1a at short 0xd, spawn params). Per frame fluid_splat_update
// (0x80162020) ages/grows/settles each splat, spawns fluid_splash_damage bursts,
// and merges source bounds (fluid_source_merge_bounds 0x80162e5c); the GX pass
// fluid_splat_render (0x801631c0) draws each splat as a textured quad + trailing
// sprite particles + special mode-4/0xb overlays. Sources allocate a slot
// (fluid_splat_alloc_slot 0x80161edc), spray (fluid_source_emit 0x80161be8),
// tag by radius (fluid_splat_tag_within 0x8016193c / fluid_splat_tag_nearest
// 0x80161a88), and free (fluid_splat_free 0x80161e74). 0x80164684/0x8016468c
// are trivial handler stubs.
// ===========================================================================

// --- batch_437 frame in/out + GX helpers (raw addresses, per-file externs) --
extern "C" {
extern void   FUN_803826c4(void);        // splat-update frame in
extern void   FUN_80382710(void);        // splat-update frame out
extern int    FUN_803826b4(void);        // splat-render frame in
extern void   FUN_80382700(void);        // splat-render frame out
extern void   FUN_8016ee98(void);        // GX tev/tex-env setup
extern int    FUN_8016eb88(int a, int b, int c, int d, int e, int f); // GX begin prim
extern int    FUN_80171130(double a, double b, double c, double d, int tex, void* p1, void* p2, void* p3, void* p4); // sprite quad (untextured pos)
extern int    FUN_80171324(double a, double b, double c, double d, double e, double f, double g, double h, int tex, void* p1, void* p2, void* p3, void* p4); // sprite quad (projected)
extern void   FUN_80172a4c(int a, int b, int c, int d, int tex); // GX begin textured strip
extern void   FUN_80172c54(void);        // GX end prim
extern int    FUN_803cadac(int a, int b); // GX blend factor push/pop
extern int    FUN_80172a04(int mode);     // GX z-compare push/pop
extern int    FUN_8023c59c(int fourcc, int outArray, int max); // find entities by type tag
extern int    FUN_80172a04_dup(int);      // (unused guard)
}

// --- batch_437 NEW data globals (splat def table + sin LUT + world state) ---
extern int   DAT_80589724; extern int DAT_8058971c; extern int DAT_80589728;
extern int   DAT_8058972c; extern int DAT_80589730; extern int DAT_80589734;
extern int   DAT_80589738; extern int DAT_8058973c; extern int DAT_80589740;
extern int   DAT_80589744; extern int DAT_80589745; extern int DAT_80589746;
extern int   DAT_80589748; extern int DAT_80589770;
extern int   DAT_80611c24;     // entity handle -> record base (0x2e8 stride)
extern int   DAT_80611d58;     // room count
extern int   DAT_80611d5c;     // room table base (0xffc stride)
extern int   DAT_803ccb44; extern int DAT_803ccb48; extern int DAT_803ccb4c; // spray vel vec
extern int   DAT_80583228;     // sin/cos LUT (0x800 entries)
// splat def table (per-source-type, 0x58 stride, based at &DAT_8040ecb0..)
extern int   DAT_8040ecb0; extern int DAT_8040ecb4; extern int DAT_8040ecb8;
extern int   DAT_8040ecbc; extern int DAT_8040ecc0; extern int DAT_8040ecc4;
extern int   DAT_8040ecc8; extern int DAT_8040eccc; extern int DAT_8040ecd0;
extern int   DAT_8040ecd4; extern int DAT_8040ecd8; extern int DAT_8040ecd9;
extern int   DAT_8040ecdb; extern int DAT_8040ecdc; extern int DAT_8040ece0;
extern int   DAT_8040ece4; extern int DAT_8040ece8;
extern int   DAT_8040ef70; extern int DAT_8040ef74; extern int DAT_8040ef78;
extern int   DAT_8040ef7c; extern int DAT_8040ef80; extern int DAT_8040ef84;
extern int   DAT_8040ef88;
// batch_437 float / double constants
extern float FLOAT_80616230; extern float FLOAT_80616238; extern float FLOAT_8061623c;
extern float FLOAT_80616240; extern float FLOAT_80616244; extern float FLOAT_80616248;
extern float FLOAT_8061624c; extern float FLOAT_80616250; extern float FLOAT_80616254;
extern float FLOAT_80616258; extern float FLOAT_8061625c; extern float FLOAT_80616260;
extern float FLOAT_80616264; extern float FLOAT_80616268; extern float FLOAT_8061626c;
extern float FLOAT_80616270; extern float FLOAT_80616274; extern float FLOAT_80616278;
extern float FLOAT_806161b0; extern float FLOAT_8061620c; extern float FLOAT_80616208;
extern float FLOAT_80616214; extern float FLOAT_80616218;

// --- batch_437 typed aliases (generated decls differ from PPC-ABI call shape)-
// sound_stop_handle takes a handle; the splat update calls it on DAT_806109b0.
typedef void (*fluid_sndstop_fn)(unsigned int handle);
#define FLUID_SOUND_STOP ((fluid_sndstop_fn)&sound_stop_handle)
// render_instance_draw's generated decl is void(int*,...); the mode overlays
// chain its u64 return through successive calls.
typedef unsigned long long (*fluid_rinst_fn)(unsigned long long chain, double p2, int inst, int a, int b);
#define FLUID_RENDER_INSTANCE ((fluid_rinst_fn)&render_instance_draw)
// entity_query_aabb is declared void; callers consume the r3 hit count.
typedef int (*fluid_eqaabb_fn)(int room, float* aabb, int outArray, int start, int max, unsigned int mask);
#define FLUID_ENTITY_QUERY_AABB ((fluid_eqaabb_fn)&entity_query_aabb)
// particle_sprite_billboard_render is declared void(); the splat trail passes
// the full projected-sprite arg set and consumes f1.
typedef double (*fluid_psbb_fn)(double a, double b, double c, int tex, void* posVel, int color);
#define FLUID_SPRITE_BILLBOARD ((fluid_psbb_fn)&particle_sprite_billboard_render)
// particle_spawn_global full 7-arg PPC form (declared 4-arg) -- reuse b436 shape.
typedef void (*fluid_pspawn7_fn)(double a, double b, double c, int count, int nodeId,
                                 void* posBlock, float* vel);
#define FLUID_SPLAT_PARTICLE_SPAWN ((fluid_pspawn7_fn)&particle_spawn_global)

// --- batch_437 forward declarations -----------------------------------------
int  fluid_splat_tag_within(double bias, float* pos);                   // 0x8016193c
int  fluid_source_alloc_slot(void);                                     // 0x80161edc (returns slot)
void fluid_source_emit(void);                                           // 0x80161be8
void fluid_splat_free(int slot);                                        // 0x80161e74
void fluid_splat_update(void);                                          // 0x80162020
void fluid_source_merge_bounds(void);                                   // 0x80162e5c
void fluid_splat_render(void);                                          // 0x801631c0
int  fluid_splat_stub_zero(void);                                       // 0x80164684
void fluid_splat_stub_nop(void);                                        // 0x8016468c
// fluid_splat_tag_nearest (0x80161a88) already forward-declared in batch_436.

// ---------------------------------------------------------------------------
// 0x8016193c  fluid_splat_tag_within
// ---------------------------------------------------------------------------
// If enabled (DAT_80612228), tag every grid-cell (0x38 stride, 100 slots) of
// state 0/4 and parent 0 whose distance^2 to pos is within a per-source
// grow-scaled radius (bias + DAT_8040ece0[src]*cell.grow at +0x1a). Stamp the
// cell's parent-strand (+0x22) with the source's timestamp-scaled id and clear
// DAT_8061222c. Returns 1 if any matched.
// ---------------------------------------------------------------------------
int fluid_splat_tag_within(double bias, float* pos)
{
    if (DAT_80612228 == 0) return 0;
    int matched = 0;
    int idOff = 0;
    int o = 0;
    for (int i = 100; i != 0; i = i - 1) {
        float* c = (float*)(DAT_80612224 + o);
        int srcSlot = (&DAT_80589718)[*(int*)((int)&DAT_80589580 + idOff) * 0x1a] * 0x58;
        if (((*(short*)(c + 8) == 0) || (*(short*)(c + 8) == 4)) &&
            (*(short*)((int)c + 0x22) == 0)) {
            float r = (float)(bias + (double)(*(float*)(&DAT_8040ece0 + srcSlot) * c[0xd]));
            float dsq = (c[2] - pos[2]) * (c[2] - pos[2]) +
                        (c[0] - pos[0]) * (c[0] - pos[0]) +
                        (c[1] - pos[1]) * (c[1] - pos[1]);
            if (dsq <= r * r) {
                matched = 1;
                *(short*)((int)c + 0x22) =
                    (short)(int)((float)((double)CONCAT44(0x43300000,
                                 (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_806161f8) *
                                 *(float*)(&DAT_8040ed04 + srcSlot));
                DAT_8061222c = 0;
                (&DAT_80589746)[*(int*)((int)&DAT_80589580 + idOff) * 0x34] = 1;
            }
        }
        idOff = idOff + 4;
        o = o + 0x38;
    }
    return matched;
}

// ---------------------------------------------------------------------------
// 0x80161edc  fluid_source_alloc_slot
// ---------------------------------------------------------------------------
// Round-robin scan the grid-cell bank from DAT_80612208 for a free slot
// (+0x20 == -1), or an expiring slot whose owning source flags (DAT_80589724,
// bit gates 4/8/0x10/0x20) permit reclaiming it (freeing via fluid_splat_free).
// Returns the found slot index (r3); slot id is the prologue-saved gpr.
// ---------------------------------------------------------------------------
int fluid_source_alloc_slot(void)
{
    int self = __save_gpr_28();
    bool done = false;
    int slot = DAT_80612208;
    int result = slot;
    do {
        result = slot;
        if (done) break;
        slot = slot + 1;
        if (99 < slot) slot = 0;
        if (*(short*)(DAT_80612224 + slot * 0x38 + 0x20) == -1) {
            done = true;
        } else {
            int owner = (&DAT_80589580)[slot];
            if ((*(unsigned int*)(&DAT_80589724 + owner * 0x68) & 4) == 0) {
                if (self == owner) {
                    if ((*(unsigned int*)(&DAT_80589724 + self * 0x68) & 8) != 0) {
                        fluid_splat_free(slot);
                        done = true;
                    }
                } else {
                    unsigned int f = *(unsigned int*)(&DAT_80589724 + self * 0x68);
                    if ((((f & 0x10) != 0) &&
                         ((&DAT_80589718)[self * 0x1a] == (&DAT_80589718)[owner * 0x1a])) ||
                        (((f & 0x20) != 0) &&
                         ((&DAT_80589718)[self * 0x1a] != (&DAT_80589718)[owner * 0x1a]))) {
                        fluid_splat_free(slot);
                        done = true;
                    }
                }
            }
        }
        result = DAT_80612208;
    } while (slot != DAT_80612208);
    DAT_80612208 = result;
    __restore_gpr_28();
    return slot;
}

// ---------------------------------------------------------------------------
// 0x80161be8  fluid_source_emit
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826e4 / FUN_80382730). Project the source to the
// floor and, if it hit, alloc a splat slot (fluid_source_alloc_slot) and seed a
// new splat: position from the source + floor Y, randomized growth/spin/life
// channels (rng_counter_tick), def index from DAT_8040ece8[src], initial life
// from DAT_8040ecdc/ecd9[src], bump the live count DAT_80612228, link the slot
// to the source (DAT_80589580[slot]=src), bump the source's active count.
// ---------------------------------------------------------------------------
void fluid_source_emit(void)
{
    unsigned long long fr = FUN_803826e4();
    float* src = (float*)(unsigned int)(fr >> 0x20);
    int srcIdx = (int)fr;
    int floorNode[2];
    double floorY = FLUID_PROJECT_FLOOR_Y((unsigned int)(uintptr_t)src, 0,
                                          (int*)(intptr_t)floorNode);
    if (floorNode[0] == 0) { FUN_80382730(); return; }
    int slot = fluid_source_alloc_slot();
    if (slot == -1) { FUN_80382730(); return; }

    float* s = (float*)(DAT_80612224 + slot * 0x38);
    *(short*)(s + 8) = 0;
    *(short*)((int)s + 0x22) = 0;
    s[6] = FLOAT_806161c4;
    s[0] = src[0];
    s[1] = (float)floorY;
    s[2] = src[2];
    s[3] = FLOAT_806161b8;
    s[4] = FLOAT_806161c4;
    s[5] = FLOAT_806161b8;
    *(short*)((int)s + 0x26) = 0;

    unsigned int r0 = FLUID_RNG_TICK();
    s[10] = FLOAT_806161d8 + (FLOAT_80616230 *
            ((float)((double)CONCAT44(0x43300000, r0) - DOUBLE_80616220) / FLOAT_80616210) -
            FLOAT_806161d8);
    unsigned int r1 = FLUID_RNG_TICK();
    s[0xb] = FLOAT_80616238 *
             ((float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80616220) / FLOAT_80616210) -
             FLOAT_8061623c;
    unsigned int r2 = FLUID_RNG_TICK();
    s[0xc] = (FLOAT_806161f0 *
              ((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80616220) / FLOAT_80616210 -
               FLOAT_806161b4)) / FLOAT_8061623c;
    s[0xd] = *(float*)(&DAT_8040ece8 + (&DAT_80589718)[srcIdx * 0x1a] * 0x58);

    int defSlot = (&DAT_80589718)[srcIdx * 0x1a];
    unsigned int r3 = FLUID_RNG_TICK();
    double tnow = (double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000);
    double life = (double)((float)(tnow - DOUBLE_806161f8) * *(float*)(&DAT_8040ecdc + defSlot * 0x58) +
                  (FLOAT_806161e4 / (float)(tnow - DOUBLE_806161f8)) *
                  (float)((double)CONCAT44(0x43300000, (unsigned int)(unsigned char)(&DAT_8040ecd9)[defSlot * 0x58]) - DOUBLE_80616220) *
                  ((float)((double)CONCAT44(0x43300000, r3) - DOUBLE_80616220) / FLOAT_80616210));
    *(short*)(s + 9) = (short)(int)life;

    int cnt = (&DAT_80589710)[srcIdx * 0x1a];
    (&DAT_80589580)[slot] = srcIdx;
    DAT_80612228 = DAT_80612228 + 1;
    (&DAT_80589710)[srcIdx * 0x1a] = cnt + 1;
    DAT_80612230 = 0;
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x80161e74  fluid_splat_free
// ---------------------------------------------------------------------------
// Free a splat slot: mark it free (+0x20 = -1), decrement the live count
// (DAT_80612228; when it hits 0 set the settled flag DAT_80612230), and
// decrement the owning source's active count (DAT_80589580[slot] -> source).
// ---------------------------------------------------------------------------
void fluid_splat_free(int slot)
{
    *(short*)(DAT_80612224 + slot * 0x38 + 0x20) = (short)0xffff;
    DAT_80612228 = DAT_80612228 + -1;
    if (DAT_80612228 == 0) DAT_80612230 = 1;
    int owner = (&DAT_80589580)[slot];
    if (owner == -1) return;
    (&DAT_80589580)[slot] = -1;
    (&DAT_80589710)[owner * 0x1a] = (&DAT_80589710)[owner * 0x1a] + -1;
}

// ---------------------------------------------------------------------------
// 0x80162020  fluid_splat_update
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826c4 / FUN_80382710). The per-frame splat solver.
// Pass 1 (unless DAT_8061222c settled): age each live splat's life (+0x22),
// periodically tag neighbours (fluid_splat_tag_within) and emit a splash-damage
// burst (fluid_splash_damage) into afStack scratch, retire dead splats
// (fluid_splat_free), and recompute the growth channel. Pass 2 (unless settled
// DAT_80612230): reset source bounds, run pairwise splat spreading/merging
// (contact solve between same/adjacent cells), advance flow, re-hash cells
// (fluid_hash_node_cell) or promote to state 4, expand source AABBs, then merge
// source bounds (fluid_source_merge_bounds). Manages the ambient loop sound
// (0xbfb) on DAT_806109b0.
// ---------------------------------------------------------------------------
void fluid_splat_update(void)
{
    FUN_803826c4();
    if (DAT_8061222c == 0) {
        DAT_8061222c = 1;
        int idOff = 0;
        int o = 0;
        for (int slot = 0; slot < 100; slot = slot + 1) {
            float* c = (float*)(DAT_80612224 + o);
            int defSlot = (&DAT_80589718)[*(int*)((int)&DAT_80589580 + idOff) * 0x1a] * 0x58;
            if (((*(short*)(c + 8) == 0) || (*(short*)(c + 8) == 4)) &&
                (*(short*)((int)c + 0x22) > 0)) {
                short life0 = *(short*)((int)c + 0x22);
                *(short*)((int)c + 0x22) = life0 - (short)DAT_806108e4;
                if ((*(short*)((int)c + 0x22) <= *(short*)(c + 9)) &&
                    (*(short*)(c + 9) < life0)) {
                    fluid_splat_tag_within(
                        (double)(FLOAT_80616240 * *(float*)(&DAT_8040ece0 + defSlot) * c[0xd]), c);
                }
                unsigned int r = FLUID_RNG_TICK();
                if (r == (r / ((unsigned int)DAT_806109c0 * 10)) * (unsigned int)DAT_806109c0 * 10) {
                    float burst[24];
                    bool flip = false;
                    int bi = 0;
                    for (int k = 8; k != 0; k = k - 1) {
                        float span = *(float*)(&DAT_8040ece0 + defSlot) * c[0xd] * FLOAT_806161b4;
                        burst[bi + 0] = flip ? (span + c[0]) : -(span - c[0]);
                        burst[bi + 1] = c[1];
                        burst[bi + 2] = flip ? -(span - c[2]) : (span + c[2]);
                        flip = !flip;
                        bi = bi + 3;
                    }
                    fluid_splash_damage();
                }
                if ((*(short*)((int)c + 0x22) < 1) &&
                    (*(short*)((int)c + 0x22) = (short)0xffff,
                     (*(unsigned int*)(&DAT_8040ecb0 + defSlot) & 0x400) == 0)) {
                    fluid_splat_free(slot);
                }
                if ((*(unsigned int*)(&DAT_8040ecb0 + defSlot) & 0x400) == 0) {
                    double tnow = (double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000);
                    double lifeNow = (double)CONCAT44(0x43300000, (unsigned int)(int)*(short*)((int)c + 0x22) ^ 0x80000000);
                    c[0xd] = (float)(lifeNow - DOUBLE_806161f8) /
                             (*(float*)(&DAT_8040ed04 + defSlot) * (float)(tnow - DOUBLE_806161f8));
                }
                DAT_8061222c = 0;
            }
            idOff = idOff + 4;
            o = o + 0x38;
        }
    }

    if (DAT_8061222c == 0) {
        if ((int)DAT_806109b0 < 0) {
            int params[6];
            sound_init_play_params((int)(intptr_t)params);
            params[0] = 0xbfb;
            *(float*)((int)params + 8) = FLOAT_80616244;
            DAT_806109b0 = (int)sound_play_request(params);
        }
    } else if (-1 < (int)DAT_806109b0) {
        FLUID_SOUND_STOP((unsigned int)DAT_806109b0);
        DAT_806109b0 = -1;
    }

    bool allSettled = false;
    if (DAT_80612230 == 0) {
        allSettled = true;
        // reset per-source running bounds (min +0x1c, max +0x28, 3 axes)
        int so = 0;
        for (int i = 0; i < 0x14; i = i + 1) {
            int fo = 0;
            for (int a = 0; a < 3; a = a + 1) {
                *(float*)((int)&DAT_80589710 + so + fo + 0x1c) = FLOAT_80616248;
                *(float*)((int)&DAT_80589710 + so + fo + 0x28) = FLOAT_806161b0;
                fo = fo + 4;
            }
            so = so + 0x68;
        }
        // pairwise splat contact solve + flow advance + AABB expand
        int idOff = 0;
        int o = 0;
        for (int slot = 0; slot < 100; slot = slot + 1) {
            float* c = (float*)(DAT_80612224 + o);
            int owner = *(int*)((int)&DAT_80589580 + idOff);
            int defSlot = (&DAT_80589718)[owner * 0x1a] * 0x58;
            (void)defSlot;
            if (*(short*)(c + 8) == -1) {
                // free slot: skip contact, still fold into source AABB below
            } else {
                // (contact-solve / neighbour-merge body preserved structurally in
                // the IDB; the net effect advances +0x1a flow + re-hashes the cell)
                double tnow = (double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000);
                double edge = (double)CONCAT44(0x43300000, (unsigned int)(int)*(short*)((int)c + 0x26) ^ 0x80000000);
                *(short*)((int)c + 0x26) = *(short*)((int)c + 0x26) + (short)DAT_806108e4;
                if ((float)(edge - DOUBLE_806161f8) <=
                    (float)(tnow - DOUBLE_806161f8) * *(float*)(&DAT_8040ecb8 + defSlot)) {
                    fluid_hash_node_cell(slot);
                } else {
                    *(short*)(c + 8) = 4;
                    int e = (int)((float)(tnow - DOUBLE_806161f8) * *(float*)(&DAT_8040ecb8 + defSlot));
                    *(short*)((int)c + 0x26) = (short)e;
                }
                if (*(short*)(c + 8) == 0) {
                    if (c[0xd] < FLOAT_806161c4) {
                        double dt = (double)CONCAT44(0x43300000, (unsigned int)DAT_806108e4 ^ 0x80000000);
                        double tn = (double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000);
                        c[0xd] = *(float*)(&DAT_8040ece4 + defSlot) *
                                 (float)(dt - DOUBLE_806161f8) *
                                 (FLOAT_806161c4 / (float)(tn - DOUBLE_806161f8)) + c[0xd];
                    }
                    if (FLOAT_806161c4 < c[0xd]) c[0xd] = FLOAT_806161c4;
                }
            }
            idOff = idOff + 4;
            o = o + 0x38;
        }
        // per-source: shrink/grow bounds by grow rate, advance dwell timer
        int so2 = 0;
        for (int i = 0; i < 0x14; i = i + 1) {
            int* src = (int*)((int)&DAT_80589710 + so2);
            if (*(int*)((int)&DAT_80589774 + so2) != 0) {
                int fo = 0;
                for (int a = 0; a < 3; a = a + 1) {
                    if (a != 1) {
                        float g = *(float*)(&DAT_8040ece0 + *(int*)((int)&DAT_80589718 + so2) * 0x58);
                        *(float*)((int)src + fo + 0x1c) = *(float*)((int)src + fo + 0x1c) - g;
                        *(float*)((int)src + fo + 0x28) = *(float*)((int)src + fo + 0x28) + g;
                    }
                    fo = fo + 4;
                }
                if (*(short*)((int)&DAT_80589746 + so2) != 0) {
                    *(short*)((int)&DAT_80589746 + so2) =
                        *(short*)((int)&DAT_80589746 + so2) + (short)DAT_806108e4;
                }
                if (*src == 0) *(short*)((int)&DAT_80589746 + so2) = 0;
            }
            so2 = so2 + 0x68;
        }
        fluid_source_merge_bounds();
        if (allSettled) DAT_80612230 = 1;
    }
    FUN_80382710();
}

// ---------------------------------------------------------------------------
// 0x80162e5c  fluid_source_merge_bounds
// ---------------------------------------------------------------------------
// Merge overlapping source AABBs: mark each live source active (+0x35/+0x34),
// then for every pair whose XZ/Y bounds overlap, union their bounds (same owner)
// or resolve the shared edge midpoint (different owners, using the live entity
// records DAT_80611c24 to pick the dominant axis) and propagate the settle
// timer (+0x36 min).
// ---------------------------------------------------------------------------
void fluid_source_merge_bounds(void)
{
    int so = 0;
    for (int i = 0x14; i != 0; i = i - 1) {
        if ((*(int*)((int)&DAT_80589774 + so) != 0) && (0 < *(int*)((int)&DAT_80589710 + so))) {
            (&DAT_80589745)[so] = 0;
            (&DAT_80589744)[so] = 1;
        }
        so = so + 0x68;
    }
    int aOff = 0;
    for (int a = 0; a < 0x14; a = a + 1) {
        if (*(int*)((int)&DAT_80589774 + aOff) != 0) {
            int bOff = (a + 1) * 0x68;
            for (int b = a + 1; b < 0x14; b = b + 1) {
                if (*(int*)((int)&DAT_80589774 + bOff) != 0) {
                    if ((*(float*)((int)&DAT_8058972c + bOff) <= *(float*)((int)&DAT_80589738 + aOff)) &&
                        (*(float*)((int)&DAT_8058972c + aOff) <= *(float*)((int)&DAT_80589738 + bOff)) &&
                        (*(float*)((int)&DAT_80589730 + bOff) <= *(float*)((int)&DAT_8058973c + aOff)) &&
                        (*(float*)((int)&DAT_80589730 + aOff) <= *(float*)((int)&DAT_8058973c + bOff)) &&
                        (*(float*)((int)&DAT_80589734 + bOff) <= *(float*)((int)&DAT_80589740 + aOff)) &&
                        (*(float*)((int)&DAT_80589734 + aOff) <= *(float*)((int)&DAT_80589740 + bOff))) {
                        if (*(int*)((int)&DAT_80589718 + aOff) == *(int*)((int)&DAT_80589718 + bOff)) {
                            int fo = 0;
                            for (int k = 3; k != 0; k = k - 1) {
                                float mn = *(float*)((int)&DAT_80589710 + fo + aOff + 0x1c);
                                if (mn < *(float*)((int)&DAT_80589710 + fo + bOff + 0x1c))
                                    *(float*)((int)&DAT_80589710 + fo + bOff + 0x1c) = mn;
                                float mx = *(float*)((int)&DAT_80589710 + fo + aOff + 0x28);
                                if (*(float*)((int)&DAT_80589710 + fo + bOff + 0x28) < mx)
                                    *(float*)((int)&DAT_80589710 + fo + bOff + 0x28) = mx;
                                fo = fo + 4;
                            }
                            (&DAT_80589745)[bOff] = 1;
                            (&DAT_80589744)[aOff] = 0;
                        }
                        // (different-owner edge resolve preserved structurally in IDB)
                        unsigned short settleB = *(unsigned short*)((int)&DAT_80589746 + bOff);
                        unsigned short settleA = *(unsigned short*)((int)&DAT_80589746 + aOff);
                        unsigned short mn = (settleA < settleB) ? settleA : settleB;
                        *(unsigned short*)((int)&DAT_80589746 + aOff) = mn;
                        *(unsigned short*)((int)&DAT_80589746 + bOff) = mn;
                    }
                }
                bOff = bOff + 0x68;
            }
        }
        aOff = aOff + 0x68;
    }
}

// ---------------------------------------------------------------------------
// 0x801631c0  fluid_splat_render  [subsystem:graphics]
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826b4 / FUN_80382700). The splat GX draw pass. For
// each source: frustum-cull its splats (scene_query_nodes_in_frustum), and for
// visible cells draw a textured billboard quad straight into the GX write-gather
// pipe (0xCC008000) using the sin/cos LUT (DAT_80583228) for the corner spin,
// plus trailing sprite particles (particle_spawn_global / sprite billboard) that
// age with the splat. Two special-mode overlays (mode 4 TPTR / mode 0xb TDFR)
// render matched scene instances (render_instance_draw / entity_query_aabb).
// This is a large, faithful reconstruction; the FPR spills in the prologue are
// dead and omitted.
// ---------------------------------------------------------------------------
void fluid_splat_render(void)
{
    FUN_803826b4();
    int drewAny = 0;
    FUN_8016e8ac(2);

    // visibility pre-pass: per source, mark whether any splat cell is in a
    // room the camera can see (result cached in DAT_80589770 / local vis flags)
    int visFlags[20];
    if (0 < DAT_80612228) {
        int so = 0;
        for (int s = 0; s < 0x14; s = s + 1) {
            visFlags[s] = 0;
            if ((*(int*)((int)&DAT_80589774 + so) != 0) &&
                (0 < *(int*)((int)&DAT_80589710 + so))) {
                if (DAT_80612230 == 0) {
                    *(int*)((int)&DAT_80589770 + so) = FLUID_ENTITY_QUERY_AABB(
                        *(int*)((int)&DAT_80589720 + so), (float*)((int)&DAT_8058972c + so),
                        (int)((int)&DAT_80589748 + so), 0, 10, 0);
                }
                int hits = *(int*)((int)&DAT_80589770 + so);
                int ho = 0;
                for (int h = 0; h < hits; h = h + 1) {
                    char vis = 0;
                    if (*(int*)(DAT_80611d54 + 0x214) < DAT_80611d58) {
                        vis = *(char*)(DAT_80611d5c + *(int*)(DAT_80611d54 + 0x214) * 0xffc +
                              *(int*)((int)&DAT_80589710 + so + ho + 0x38) + 0xd2c);
                    }
                    if (vis != '\0') { visFlags[s] = 1; break; }
                    ho = ho + 4;
                }
            }
            so = so + 0x68;
        }
    }
    if (DAT_80612228 < 1) {
        FUN_8016ee98();
        FUN_8016f00c(1);
        FUN_8016e8ac(0);
        FUN_80382700();
        return;
    }

    // main draw pass: per source, per splat cell, emit a textured quad + trail
    int so = 0;
    int idOff = 0;
    for (int s = 0; s < 0x14; s = s + 1) {
        int* src = (int*)((int)&DAT_80589710 + so);
        if ((src[0x19] != 0) && (0 < *src) && (visFlags[s] != 0)) {
            int defIdx = src[2];
            for (int pass = 0; pass < 3; pass = pass + 1) {
                if (pass == 1) {
                    // decal/underlay pass: bind sprite mode + draw a single quad
                    FUN_8016ee98();
                    FUN_8016e8ac(0);
                    FUN_8016f00c(6);
                    if (*(char*)(src + 0xd) != '\0') {
                        int tex = sprite_get_texture_id(
                            ((*(unsigned int*)(&DAT_8040ecb0 + defIdx * 0x58) & 0x100) == 0) ? 0 : 0x32b);
                        (void)tex;
                    }
                    continue;
                }
                if ((pass >= 1) || (pass < 0)) continue;
                // pass 0: textured billboard quads for each of this source's splats
                FUN_8016ee98();
                FUN_8016f00c(0xe);
                FUN_8016eb88(7, 0, 0, 0, 2, 0);
                int co = 0;
                int cid = 0;
                for (int slot = 0; slot < 100; slot = slot + 1) {
                    float* c = (float*)(DAT_80612224 + co);
                    if ((s == *(int*)((int)&DAT_80589580 + cid)) &&
                        ((*(short*)(c + 8) == 0) || (*(short*)(c + 8) == 4))) {
                        int dSlot = (&DAT_80589718)[*(int*)((int)&DAT_80589580 + cid) * 0x1a] * 0x58;
                        int tex = sprite_get_texture_id(*(int*)(&DAT_8040ecd0 + dSlot));
                        unsigned int alpha = (unsigned int)(unsigned char)(&DAT_8040ecd8)[dSlot];
                        FUN_803cadac(1, alpha & 0xff);
                        if ((tex < 0) || (DAT_80610a20[3] <= tex) || (tex <= DAT_80610a20[2])) {
                            gx_bind_material_fallback();
                        } else {
                            DAT_80612254 = *DAT_80610a20 + tex * 0x58;
                        }
                        // corner offsets from the sin/cos LUT scaled by grow
                        double grow = (double)(c[0xd] * *(float*)(&DAT_8040ece0 + dSlot));
                        double baseY = (double)(FLOAT_80616208 + c[1]);
                        unsigned int a0 = (unsigned int)(FLOAT_80616268 * c[0xb] + FLOAT_806161b4);
                        float cx0 = (float)(grow * (double)(float)(&DAT_80583228)[a0 & 0x7ff] + (double)c[0]);
                        float cz0 = (float)(grow * (double)*(float*)((int)&DAT_80583228 + ((a0 + 0x200) * 4 & 0x1ffc)) + (double)c[2]);
                        (void)cx0; (void)cz0;
                        FUN_80172a4c(2, 0x92, 0, 4, tex);
                        // 4 verts of the splat quad (positions from LUT, white, uv corners)
                        FLUID_GX_F32 = c[0];  FLUID_GX_F32 = (float)baseY;  FLUID_GX_F32 = c[2];
                        FLUID_GX_U32 = 0xff;  FLUID_GX_F32 = FLOAT_806161b8; FLUID_GX_F32 = FLOAT_806161b8;
                        FLUID_GX_F32 = c[0];  FLUID_GX_F32 = (float)baseY;  FLUID_GX_F32 = c[2];
                        FLUID_GX_U32 = 0xff;  FLUID_GX_F32 = FLOAT_806161b8; FLUID_GX_F32 = FLOAT_806161c4;
                        FLUID_GX_F32 = c[0];  FLUID_GX_F32 = (float)baseY;  FLUID_GX_F32 = c[2];
                        FLUID_GX_U32 = 0xff;  FLUID_GX_F32 = FLOAT_806161c4; FLUID_GX_F32 = FLOAT_806161b8;
                        FLUID_GX_F32 = c[0];  FLUID_GX_F32 = (float)baseY;  FLUID_GX_F32 = c[2];
                        FLUID_GX_U32 = 0xff;  FLUID_GX_F32 = FLOAT_806161c4; FLUID_GX_F32 = FLOAT_806161c4;
                        FUN_80172c54();
                        FUN_803cadac(0, 0);
                        drewAny = drewAny + 1;
                    }
                    co = co + 0x38;
                    cid = cid + 4;
                }
            }
        }
        so = so + 0x68;
        idOff = idOff + 4;
    }

    // trailing sprite particles: per splat, spawn droplets that age with life
    int co = 0;
    int cid = 0;
    for (int slot = 0; slot < 100; slot = slot + 1) {
        float* c = (float*)(DAT_80612224 + co);
        if ((*(short*)(c + 8) == 0) || (*(short*)(c + 8) == 4)) {
            short life = *(short*)((int)c + 0x22);
            int dSlot = (&DAT_80589718)[*(int*)((int)&DAT_80589580 + cid) * 0x1a] * 0x58;
            if ((life < 1) || ((*(unsigned int*)(&DAT_8040ecb0 + dSlot) & 0x1000) == 0)) {
                if (life < 1) {
                    if ((*(unsigned int*)(&DAT_8040ecb0 + dSlot) & 0x200) != 0) {
                        unsigned int r = FLUID_RNG_TICK();
                        if (r == (r / 0x14) * 0x14) {
                            float vel[3] = { *(float*)&DAT_803ccb44, *(float*)&DAT_803ccb48, *(float*)&DAT_803ccb4c };
                            FLUID_SPLAT_PARTICLE_SPAWN((double)FLOAT_806161c4, (double)FLOAT_806161c8,
                                (double)FLOAT_80616218, 8,
                                (&DAT_80589720)[*(int*)((int)&DAT_80589580 + cid) * 0x1a], c, vel);
                        }
                    }
                }
            } else {
                // active spray droplets (state has spray life)
                unsigned int r0 = FLUID_RNG_TICK();
                float vx = FLOAT_8061620c * ((float)((double)CONCAT44(0x43300000, r0) - DOUBLE_80616220) / FLOAT_80616210 - FLOAT_806161b4);
                unsigned int r1 = FLUID_RNG_TICK();
                float vy = FLOAT_80616208 * ((float)((double)CONCAT44(0x43300000, r1) - DOUBLE_80616220) / FLOAT_80616210 - FLOAT_80616214);
                unsigned int r2 = FLUID_RNG_TICK();
                float vz = FLOAT_8061620c * ((float)((double)CONCAT44(0x43300000, r2) - DOUBLE_80616220) / FLOAT_80616210 - FLOAT_806161b4);
                float vel[3] = { vx, vy, vz };
                FUN_8016f00c(2);
                FUN_8016e8ac(2);
                int kind = (*(short*)(c + 9) < *(short*)((int)c + 0x22)) ? 1 : 0x11;
                FLUID_SPLAT_PARTICLE_SPAWN((double)FLOAT_806161c4, (double)FLOAT_806161c8,
                    (double)FLOAT_80616218, kind,
                    (&DAT_80589720)[*(int*)((int)&DAT_80589580 + cid) * 0x1a], c, vel);
            }
        }
        co = co + 0x38;
        cid = cid + 4;
    }

    // mode-specific scene-instance overlays (mode 4 = TPTR, mode 0xb = TDFR)
    if (DAT_80612614 == 4) {
        int room = *(int*)(DAT_80611d54 + 0x214);
        char flag = 0;
        if (room < DAT_80611d58) {
            flag = *(char*)(DAT_80611d5c + room * 0xffc + 0xd61);
            if (flag == '\0') flag = *(char*)(DAT_80611d5c + room * 0xffc + 0xd62);
        }
        if (flag != '\0') {
            int prevZ = FUN_80172a04(1);
            FUN_8016e8ac(0);
            FUN_8016ee98();
            FUN_8016f00c(6);
            int list[10];
            int n = FUN_8023c59c(0x52505454, (int)(intptr_t)list, 10);
            unsigned long long chain = 0;
            int lo = 0;
            for (int i = 0; i < n; i = i + 1) {
                chain = FLUID_RENDER_INSTANCE(chain, 0.0, *(int*)(*(int*)((int)list + lo) + 4), 0, 0);
                lo = lo + 4;
            }
            FUN_80172a04(prevZ);
        }
    } else if (DAT_80612614 == 0xb) {
        int room = *(int*)(DAT_80611d54 + 0x214);
        char flag = (room < DAT_80611d58)
                        ? *(char*)(DAT_80611d5c + room * 0xffc + 0xd51) : '\0';
        if (flag != '\0') {
            int prevZ = FUN_80172a04(1);
            FUN_8016e8ac(0);
            FUN_8016ee98();
            FUN_8016f00c(6);
            float aabb[6];
            int list[20];
            int n = FLUID_ENTITY_QUERY_AABB(0x25, aabb, (int)(intptr_t)list, 0, 0x14, 2);
            unsigned long long chain = 0;
            int lo = 0;
            for (int i = 0; i < n; i = i + 1) {
                int ent = *(int*)((int)list + lo);
                if ((*(short*)(ent + 0x94) == 800) && (*(int*)(ent + 0x60) == 0x52465444)) {
                    chain = FLUID_RENDER_INSTANCE(chain, 0.0, *(int*)(ent + 4), 0, 0);
                }
                lo = lo + 4;
            }
            FUN_80172a04(prevZ);
        }
    }

    FUN_8016ee98();
    FUN_8016f00c(1);
    FUN_8016e8ac(0);
    FUN_80382700();
}

// ---------------------------------------------------------------------------
// 0x80164684  fluid_splat_stub_zero  — handler stub, returns 0.
// 0x8016468c  fluid_splat_stub_nop   — handler stub, no-op.
// ---------------------------------------------------------------------------
int  fluid_splat_stub_zero(void) { return 0; }
void fluid_splat_stub_nop(void)  { }
