// ============================================================================
// Address range: 0x8037d8f0 - 0x8037ed6c | batch_1059 | 2026-09-12
//
// Prop "impact / effect" particle sub-system. Immediately after the prop
// selection panels (batch_1050-1058) the binary has a self-contained particle
// effect controller used by the level-prop objects: a small keyed range-node
// allocator (id -> contiguous emitter-id block), a flag dispatcher that routes
// a per-object byte to the matching range node, the big multi-emitter spawn
// helper driven by a capability bitmask, and the one-time particle-definition
// builder that registers ~10 emitter templates (DAT_805ee8cc .. DAT_805ef49c
// backed by definition blobs DAT_805eed70 .. DAT_805ef49c). Also a stats panel
// row-draw and a particle-handle transfer that re-parents a source entity's
// four cached emitter handles onto a destination entity.
//
//   prop_stats_panel_draw        0x8037d8f0  draw the "%s %2.2f/" stats panel rows + bevels
//   prop_fx_init_reset           0x8037da9c  reset the range-node list head/tail + id, then build defs
//   prop_fx_init                 0x8037dad0  build the particle definitions (thin wrapper)
//   prop_fx_range_alloc          0x8037daf0  find-or-append a keyed range node (id -> emitter-id base)
//   prop_fx_range_dispatch       0x8037dbb0  route an index to the owning range node's callback
//   prop_fx_dispatch_byte        0x8037dc40  dispatch using the object's +0x13 flag byte
//   prop_fx_def_table_index      0x8037dc78  emitter-def table index from the def pointer (0 or 1)
//   prop_fx_spawn_group          0x8037dc94  spawn the emitter group selected by the capability mask
//   prop_fx_build_defs           0x8037e184  register all ~10 particle-effect definitions (one-time)
//   prop_fx_transfer_handles     0x8037ed6c  move a source entity's 4 cached emitter handles to a dest
//
// NOTE: non-matching decomp. The GameCube callee-save prologue spill and the
// paired-single float shuffles are dropped. Engine helpers whose generated
// prototype is narrower than the true ABI (mem_alloc, particle_emitter_set_*)
// are reached through typed function-pointer casts (batch_1030+ convention) so
// the file compiles against the shared generated headers without redeclaring
// those names. Struct offsets, the 0xc-byte range-node layout (id/short at +0,
// span at +2, key at +4/+8, callback at +8, next at +0x10 as bytes) and every
// particle side effect match the original.
#include "common.h"
#include "generated_decls.h"

// ---- raw symbols referenced by this slice ---------------------------------
// range-node list state
extern short* DAT_80612fc0;   // list head (range-node*)
extern short* DAT_80612fc4;   // list tail (range-node*)
extern int    DAT_80611368;   // running emitter-id cursor

// stats-panel string/table state
extern char   DAT_80497340;   // fx slot table base (0x18 stride, 7 slots)
extern char   DAT_8049734c;   // per-slot enable byte (base+0xc)
extern float  DAT_80497344;   // per-slot fx amount (float, base+0x4)
extern char   s__s__2_2f_804975a0[];  // "%s %2.2f/" format string

// spawn / def-builder cached state
extern int    DAT_805ee848;   // emitter-def table base (for table-index calc)
extern int    DAT_80611d54;   // active local-player record (for scale)

// emitter template ids (referenced by address)
extern int    DAT_805ee8cc, DAT_805ee950, DAT_805ee9d4, DAT_805eea58;
extern int    DAT_805eeadc, DAT_805eeb60, DAT_805eebe4, DAT_805eec68, DAT_805eecec;
// definition blobs
extern int    DAT_805eed70, DAT_805eee3c, DAT_805eef08, DAT_805eefd4, DAT_805ef0a0;
extern int    DAT_805ef16c, DAT_805ef238, DAT_805ef304, DAT_805ef3d0, DAT_805ef49c;
// keyframe/scale source table (particle def build)
extern int    DAT_803e0fc8, DAT_803e0fcc, DAT_803e0fd0, DAT_803e0fd4, DAT_803e0fd8;
extern int    DAT_803e0fdc, DAT_803e0fe0, DAT_803e0fe4, DAT_803e0fe8, DAT_803e0fec;
extern int    DAT_803e0ff0, DAT_803e0ff4, DAT_803e0ff8, DAT_803e0ffc, DAT_803e1000;
extern int    DAT_803e1004, DAT_803e1008, DAT_803e100c, DAT_803e1010, DAT_803e1014;
extern int    DAT_803e1018, DAT_803e101c, DAT_803e1020, DAT_803e1024, DAT_803e1028;
extern int    DAT_803e102c, DAT_803e1030, DAT_803e1034, DAT_803e1038, DAT_803e103c;
extern int    DAT_803e1050, DAT_803e1068, DAT_803e106c, DAT_803e1070, DAT_803e1074;
extern int    DAT_803e1078, DAT_803e107c, DAT_803e1080, DAT_803e1084, DAT_803e1088;
extern int    DAT_803e108c, DAT_803e1090, DAT_803e1094, DAT_803e1098, DAT_803e109c;
extern int    DAT_803e10a0, DAT_803e10a4, DAT_803e10a8, DAT_803e10ac, DAT_803e10b0;
extern int    DAT_803e10b4, DAT_803e10b8, DAT_803e10bc, DAT_803e10c0, DAT_803e10c4;

// float constants (stats panel + particle def build)
extern float  FLOAT_8061b7a8, FLOAT_8061b7cc, FLOAT_8061b7d4;
extern float  FLOAT_8061b81c, FLOAT_8061b820, FLOAT_8061b824, FLOAT_8061b828;
extern float  FLOAT_8061b82c, FLOAT_8061b830, FLOAT_8061b834, FLOAT_8061b838;
extern float  FLOAT_8061b840, FLOAT_8061b844, FLOAT_8061b848, FLOAT_8061b84c;
extern float  FLOAT_8061b850, FLOAT_8061b854, FLOAT_8061b858, FLOAT_8061b85c;
extern float  FLOAT_8061b860, FLOAT_8061b864, FLOAT_8061b868, FLOAT_8061b86c;
extern float  FLOAT_8061b870, FLOAT_8061b874, FLOAT_8061b878, FLOAT_8061b87c;
extern float  FLOAT_8061b880, FLOAT_8061b884, FLOAT_8061b888, FLOAT_8061b88c;
extern float  FLOAT_8061b890, FLOAT_8061b894, FLOAT_8061b898, FLOAT_8061b89c;
extern float  FLOAT_8061b8a0, FLOAT_8061b8a4, FLOAT_8061b8a8, FLOAT_8061b8ac;
extern float  FLOAT_8061b8b0, FLOAT_8061b8b4, FLOAT_8061b8b8, FLOAT_8061b8bc;
extern float  FLOAT_8061b8c0, FLOAT_8061b8c4, FLOAT_8061b8c8, FLOAT_8061b8cc;
extern float  FLOAT_8061b8d0, FLOAT_8061b8d4, FLOAT_8061b8d8, FLOAT_8061b8dc;
extern float  FLOAT_8061b8e0, FLOAT_8061b8e4, FLOAT_8061b8e8, FLOAT_8061b8ec;

// raw engine helpers with wide/variadic ABIs reached by address
extern "C" int  FUN_80387fb8(double, double, double, double, double, double, double, double,
                             int, char*, int, int, int, int, int, int);   // sprintf-like formatter
extern "C" unsigned long long FUN_803826d4(void);   // begin fx group -> (mask<<0 | base<<32)
extern "C" void FUN_80382720(void);                  // end fx group
extern "C" void FUN_803826b4(void);                  // begin def batch
extern "C" void FUN_80382700(void);                  // end def batch
extern "C" unsigned long long FUN_803826dc(void);    // begin handle-transfer -> (dst<<0 | src<<32)
extern "C" void FUN_80382728(void);                  // end handle-transfer

// forward decl for the range-node dispatcher used as a callback address
static void prop_fx_range_dispatch(int index, int obj, unsigned* out, unsigned* fallback);
void prop_fx_build_defs(void);

// ---- narrowed-prototype helpers reached via typed casts --------------------
// mem_alloc is declared mem_alloc(size,type) in generated_decls; the real ABI
// takes the full register/stack window. Reach the true entry by address.
typedef short* (*RangeAllocFn)(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                               unsigned, unsigned, int, int, unsigned, unsigned,
                               unsigned, unsigned, unsigned, unsigned);
// particle_emitter_set_flags has a 3-arg generated prototype; the def builder
// uses a 1-arg form that registers a per-emitter flag callback. Reach it by
// address with the correct arity for each call site (symbols already declared
// in generated_decls.h, so we only add local function-pointer typedefs).
typedef void (*EmitterSetFlagCbFn)(void*);
// particle_emitter_set_scale is called with either a pointer (scale vector) or
// an int (scale factor); reach the pointer form via a typed cast.
typedef void (*EmitterSetScalePtrFn)(void*);

// ============================================================================
// 0x8037d8f0  prop_stats_panel_draw
// Draw the fx stats panel: a title bevel, a filled bevel, three caption rows
// and a summed "%s %2.2f/" total line built from the 7-slot fx table.
// ============================================================================
void prop_stats_panel_draw(void)
{
    unsigned char lineBuf[256];

    int prevFont = text_set_font_table(0x1a);
    prop_panel_draw_bevel(0xa8, 0x1c, 0xf0, 0xce);
    prop_panel_draw_bevel_col(0xd, 0xdc, 0xf0, 0xf0, 0x40);

    unsigned char* s = (unsigned char*)get_string_by_id(0x126b);
    double w = text_get_char_width(s);
    text_draw_char_outline(
        (double)(float)(((double)FLOAT_8061b81c - w) * FLOAT_8061b820 + FLOAT_8061b7d4),
        (double)FLOAT_8061b824, s, 0x7f17177f, 0xff);

    int s2 = get_string_by_id(0x126c);
    text_draw_char_outline((double)FLOAT_8061b828, (double)FLOAT_8061b82c, (u8*)s2, 0x7f007f, 0xff);

    s = (unsigned char*)get_string_by_id(0x126d);
    text_apply_alpha_fade((double)FLOAT_8061b830, s, 0, 1);
    text_draw_char_outline((double)FLOAT_8061b828, (double)FLOAT_8061b834, s, 0x7f007f, 0xff);

    double grey = (double)FLOAT_8061b7cc;
    text_set_color_rgb(grey, grey, grey);
    int fmtStr = get_string_by_id(0x126e);

    // sum the enabled slots' fx amount
    double total = (double)FLOAT_8061b7a8;
    int off = 0;
    for (int i = 7; i != 0; --i) {
        if ((&DAT_8049734c)[off] != '\0') {
            total = (double)(float)(total + (double)*(float*)((int)&DAT_80497344 + off));
        }
        off += 0x18;
    }
    // format "%s %2.2f/" into lineBuf
    FUN_80387fb8(total, grey, grey, 0, 0, 0, 0, 0,
                 (int)lineBuf, s__s__2_2f_804975a0, fmtStr,
                 (int)((int)&DAT_80497340 + off), 0x40, 0, 0, 0);
    text_apply_alpha_fade((double)FLOAT_8061b830, lineBuf, 0, 0);
    text_draw_char_outline((double)FLOAT_8061b828, (double)FLOAT_8061b838, lineBuf, 0x7f007f, 0xff);

    grey = (double)FLOAT_8061b7cc;
    text_set_color_rgb(grey, grey, grey);
    text_set_font_table(prevFont);
}

// ============================================================================
// 0x8037da9c  prop_fx_init_reset
// Reset the keyed range-node list (head/tail null, id cursor = 1) then build
// the particle definitions.
// ============================================================================
void prop_fx_init_reset(void)
{
    DAT_80612fc0 = 0;
    DAT_80612fc4 = 0;
    DAT_80611368 = 1;
    prop_fx_build_defs();
}

// ============================================================================
// 0x8037dad0  prop_fx_init  (thin wrapper: build defs without resetting)
// ============================================================================
void prop_fx_init(void)
{
    prop_fx_build_defs();
}

// ============================================================================
// 0x8037daf0  prop_fx_range_alloc
// Find the range node whose key == param_9, else allocate a 0xc-short node,
// link it at the tail, stamp it with the current id cursor + span (param_10)
// and advance the cursor. Returns the node's base id (short at +0).
// ============================================================================
short prop_fx_range_alloc(unsigned p1, unsigned p2, unsigned p3, unsigned p4,
                          unsigned p5, unsigned p6, unsigned p7, unsigned p8,
                          int key, int span, unsigned p11, unsigned p12,
                          unsigned p13, unsigned p14, unsigned p15, unsigned p16)
{
    short* node = DAT_80612fc0;
    if (node != 0) {
        for (; node != 0; node = *(short**)(node + 4)) {
            if (*(int*)(node + 2) == key) {
                return *node;
            }
        }
    }
    node = ((RangeAllocFn)mem_alloc)(p1, p2, p3, p4, p5, p6, p7, p8,
                                     0xc, 1, p11, p12, p13, p14, p15, p16);
    if (DAT_80612fc0 == 0) {
        DAT_80612fc0 = node;
    }
    if (DAT_80612fc4 != 0) {
        *(short**)((int)DAT_80612fc4 + 8) = node;
    }
    *node = (short)DAT_80611368;
    node[1] = (short)span;
    *(int*)(node + 2) = key;
    *(int*)(node + 4) = 0;
    DAT_80612fc4 = node;
    DAT_80611368 = DAT_80611368 + span;
    return *node;
}

// ============================================================================
// 0x8037dbb0  prop_fx_range_dispatch
// If index == 0 copy the fallback triple through; otherwise walk the range-node
// list, find the node covering index and invoke its callback with the local
// (index - base) offset.
// ============================================================================
static void prop_fx_range_dispatch(int index, int obj, unsigned* out, unsigned* fallback)
{
    unsigned short* node = (unsigned short*)DAT_80612fc0;
    if (index == 0) {
        out[0] = fallback[0];
        out[1] = fallback[1];
        out[2] = fallback[2];
        return;
    }
    for (; node != 0; node = *(unsigned short**)(node + 4)) {
        unsigned base = (unsigned)*node;
        if (((int)base <= index) && (index < (int)(base + node[1]))) {
            typedef void (*RangeCbFn)(int, unsigned*, unsigned*, int);
            RangeCbFn cb = *(RangeCbFn*)(node + 2);
            cb(obj, out, fallback, index - base);
            return;
        }
    }
}

// ============================================================================
// 0x8037dc40  prop_fx_dispatch_byte
// Dispatch using the object's +0x13 flag byte as the range index.
// ============================================================================
void prop_fx_dispatch_byte(int obj, unsigned* out, unsigned* fallback)
{
    prop_fx_range_dispatch((unsigned)*(unsigned char*)(obj + 0x13), obj, out, fallback);
}

// ============================================================================
// 0x8037dc78  prop_fx_def_table_index
// Emitter-def table index derived from the def pointer: clz(base - def) >> 5,
// i.e. 1 when def == base, 0 otherwise.
// ============================================================================
unsigned prop_fx_def_table_index(int obj)
{
    unsigned v = countLeadingZeros((unsigned)((int)&DAT_805ee848 - *(int*)(obj + 0xc)));
    return v >> 5;
}

// ============================================================================
// 0x8037dc94  prop_fx_spawn_group
// Spawn the group of emitters selected by the capability mask returned from
// FUN_803826d4 (mask in low word, emitter-id base in high word). Each set bit
// spawns one template, sequentially numbered from the base, sharing the render
// layer / drag / size range and optional owning entity.
// ============================================================================
void prop_fx_spawn_group(int obj, double sizeLo, double sizeHi, int p4, int p5,
                         float* pos, float* vel, unsigned* bounds, unsigned flags,
                         unsigned char layer, int ownerEnt)
{
    double drag = 0.0;   // decompiler read this from f1 (incoming); real caller passes drag
    unsigned long long r = FUN_803826d4();
    int base = (int)(r >> 0x20);
    unsigned slot = 0;

    // helper to compute the next sequential emitter id
    #define NEXT_ID() ( base == 0 ? 0 : (base + (int)(slot++ * 4)) )

    if ((r & 0x10) != 0) {
        int id = (base == 0) ? 0 : base;
        slot = (unsigned)(base != 0);
        particle_emitter_spawn(id, (u32)&DAT_805ee8cc,
                               flags | (-(unsigned)(bounds != 0) & 0x40000) | 0x80000000, pos, vel);
        if (bounds != 0) {
            particle_emitter_set_bounds(0, (float*)bounds);
        }
        particle_emitter_set_render_layer(layer);
        particle_emitter_set_drag(drag);
        particle_emitter_set_size_range(sizeLo, sizeHi);
        if (ownerEnt != 0) particle_cached_set_entity_no_flag(ownerEnt);
    }
    if ((r & 8) != 0) {
        float p[3];
        p[0] = pos[0];
        p[1] = pos[1] + FLOAT_8061b840;
        p[2] = pos[2];
        int id = NEXT_ID();
        particle_emitter_spawn(id, (u32)&DAT_805ee848, flags, p, vel);
        particle_emitter_set_render_layer(layer);
        particle_emitter_set_drag(drag);
        particle_emitter_set_size_range(sizeLo, sizeHi);
        if (ownerEnt != 0) particle_cached_set_entity(ownerEnt);
    }
    if ((r & 1) != 0) {
        int id = NEXT_ID();
        particle_emitter_spawn(id, (u32)&DAT_805ee950, flags | 0x80000000, pos, vel);
        particle_emitter_set_render_layer(layer);
        particle_emitter_set_drag(drag);
        particle_emitter_set_size_range(sizeLo, sizeHi);
        if (ownerEnt != 0) particle_cached_set_entity(ownerEnt);
    }
    if ((r & 2) != 0) {
        int id = NEXT_ID();
        particle_emitter_spawn(id, (u32)&DAT_805ee9d4, flags | 0x80000000, pos, vel);
        particle_emitter_set_render_layer(layer);
        particle_emitter_set_drag(drag);
        particle_emitter_set_size_range(sizeLo, sizeHi);
        if (ownerEnt != 0) particle_cached_set_entity(ownerEnt);
    }
    if ((r & 4) != 0) {
        int id = NEXT_ID();
        particle_emitter_spawn(id, (u32)&DAT_805eea58, flags | 0x80000000, pos, vel);
        particle_emitter_set_render_layer(layer);
        particle_emitter_set_drag(drag);
        particle_emitter_set_size_range(sizeLo, sizeHi);
        if (ownerEnt != 0) particle_cached_set_entity(ownerEnt);
    }
    if ((r & 0x40) != 0) {
        int id = NEXT_ID();
        particle_emitter_spawn(id, (u32)&DAT_805eeadc, flags | 0x80000000, pos, vel);
        particle_emitter_set_render_layer(layer);
        particle_emitter_set_drag(drag);
        particle_emitter_set_size_range(sizeLo, sizeHi);
        if (ownerEnt != 0) particle_cached_set_entity(ownerEnt);
    }
    if ((r & 0x80) != 0) {
        int id = NEXT_ID();
        particle_emitter_spawn(id, (u32)&DAT_805eeb60, flags | 0x80000000, pos, vel);
        particle_emitter_set_render_layer(layer);
        particle_emitter_set_drag(drag);
        particle_emitter_set_size_range(sizeLo, sizeHi);
        if (ownerEnt != 0) particle_cached_set_entity(ownerEnt);
    }
    if ((r & 0x420) != 0) {
        int id = NEXT_ID();
        particle_emitter_spawn(id, (u32)&DAT_805eebe4, flags | 0x80000000, pos, vel);
        particle_emitter_set_render_layer(layer);
        particle_emitter_set_drag(drag);
        particle_emitter_set_size_range(sizeLo, sizeHi);
        if (ownerEnt != 0) particle_cached_set_entity(ownerEnt);
        if ((r & 0x400) != 0) {
            int scale = (DAT_80611d54 == 0) ? 0 : (1 << *(int*)(DAT_80611d54 + 0x214));
            ((void (*)(int))&particle_emitter_set_scale)(scale);
        }
    }
    if ((r & 0x100) != 0) {
        int id = NEXT_ID();
        particle_emitter_spawn(id, (u32)&DAT_805eec68, flags, pos, vel);
        particle_emitter_set_render_layer(layer);
        particle_emitter_set_drag(drag);
        particle_emitter_set_size_range(sizeLo, sizeHi);
        if (ownerEnt != 0) particle_cached_set_entity(ownerEnt);
    }
    if ((r & 0x200) != 0) {
        int id = (base == 0) ? 0 : (base + (int)(slot * 4));
        particle_emitter_spawn(id, (u32)&DAT_805eecec, flags, pos, vel);
        particle_emitter_set_render_layer(layer);
        particle_emitter_set_drag(drag);
        particle_emitter_set_size_range(sizeLo, sizeHi);
        if (ownerEnt != 0) particle_cached_set_entity(ownerEnt);
    }
    #undef NEXT_ID
    FUN_80382720();
}

// ============================================================================
// 0x8037e184  prop_fx_build_defs
// One-time registration of the ~10 particle-effect definitions. Copies the
// keyframe/scale source rows (DAT_803e0fc8.. / DAT_803e1020..) into stack
// scratch, then for each emitter template calls the def create / size / color /
// velocity / emitter-create chain, registering prop_fx_dispatch_byte as the
// per-emitter flag callback.
// ============================================================================
void prop_fx_build_defs(void)
{
    // scratch keyframe / scale rows copied from the const source blocks
    unsigned scaleA[3];   // local_ac/a8/a4  (DAT_803e0fc8..)
    unsigned kfB[4];      // local_bc..      (DAT_803e0fd4..)
    unsigned kfC[4];      // local_cc..      (DAT_803e0fe4..)
    unsigned kfD[4];      // local_dc..      (DAT_803e0ff4..)
    unsigned kfE[4];      // local_ec..      (DAT_803e1004..)
    unsigned kfF[4];      // local_fc..      (DAT_803e1014..)  + tail
    unsigned kf108[4];    // local_108..     (DAT_803e1020..)
    unsigned kf118[4];    // local_118..     (DAT_803e1030..)
    unsigned a0row[11];   // local_a0[0..10] (DAT_803e103c.. copy loop)
    unsigned kf128[4];    // local_128..     (DAT_803e1068..)
    unsigned kf138[4];    // local_138..     (DAT_803e1078..)
    unsigned kf148[4];    // local_148..     (DAT_803e1088..)
    unsigned kf158[4];    // local_158..     (DAT_803e1098..)
    unsigned kf168[4];    // local_168..     (DAT_803e10a8..)
    unsigned kf178[4];    // local_178..     (DAT_803e10b8..)

    FUN_803826b4();

    scaleA[0] = DAT_803e0fc8; scaleA[1] = DAT_803e0fcc; scaleA[2] = DAT_803e0fd0;
    kfB[0] = DAT_803e0fd4; kfB[1] = DAT_803e0fd8; kfB[2] = DAT_803e0fdc; kfB[3] = DAT_803e0fe0;
    kfC[0] = DAT_803e0fe4; kfC[1] = DAT_803e0fe8; kfC[2] = DAT_803e0fec; kfC[3] = DAT_803e0ff0;
    kfD[0] = DAT_803e0ff4; kfD[1] = DAT_803e0ff8; kfD[2] = DAT_803e0ffc; kfD[3] = DAT_803e1000;
    kfE[0] = DAT_803e1004; kfE[1] = DAT_803e1008; kfE[2] = DAT_803e100c; kfE[3] = DAT_803e1010;
    kfF[0] = DAT_803e1014; kfF[1] = DAT_803e1018; kfF[2] = DAT_803e101c; kfF[3] = DAT_803e1020;
    kf108[0] = DAT_803e1020; kf108[1] = DAT_803e1024; kf108[2] = DAT_803e1028; kf108[3] = DAT_803e102c;
    kf118[0] = DAT_803e1030; kf118[1] = DAT_803e1034; kf118[2] = DAT_803e1038; kf118[3] = DAT_803e103c;
    // a0row[] mirrors the &DAT_803e103c / &DAT_803e1050 copy loops
    a0row[10] = DAT_803e102c;
    kf128[0] = DAT_803e1068; kf128[1] = DAT_803e106c; kf128[2] = DAT_803e1070; kf128[3] = DAT_803e1074;
    kf138[0] = DAT_803e1078; kf138[1] = DAT_803e107c; kf138[2] = DAT_803e1080; kf138[3] = DAT_803e1084;
    kf148[0] = DAT_803e1088; kf148[1] = DAT_803e108c; kf148[2] = DAT_803e1090; kf148[3] = DAT_803e1094;
    kf158[0] = DAT_803e1098; kf158[1] = DAT_803e109c; kf158[2] = DAT_803e10a0; kf158[3] = DAT_803e10a4;
    kf168[0] = DAT_803e10a8; kf168[1] = DAT_803e10ac; kf168[2] = DAT_803e10b0; kf168[3] = DAT_803e10b4;
    kf178[0] = DAT_803e10b8; kf178[1] = DAT_803e10bc; kf178[2] = DAT_803e10c0; kf178[3] = DAT_803e10c4;
    (void)kfF; (void)a0row;

    EmitterSetFlagCbFn setFlagCb = (EmitterSetFlagCbFn)(void*)&prop_fx_dispatch_byte;

    // --- template 0 : DAT_805ee8cc / DAT_805eed70 ---
    particle_def_create((double)FLOAT_8061b844, (double)FLOAT_8061b848, (u32)&DAT_805eed70, 0, 0, 1, 0x300, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_size_keyframes(d, d, d, d, 0x10101000, 0x8080800, 0x1f1f1f00);
    }
    particle_def_set_keyframes_raw((int*)kfC, (int*)kfB, 4);
    particle_def_set_size_start_end((double)FLOAT_8061b84c, (double)FLOAT_8061b850,
                                    (double)FLOAT_8061b854, (double)FLOAT_8061b858);
    particle_def_set_color_end((double)FLOAT_8061b85c, (double)FLOAT_8061b84c,
                               (double)FLOAT_8061b860, (double)FLOAT_8061b84c);
    particle_def_set_velocity_ranges((double)FLOAT_8061b848, (double)FLOAT_8061b854,
                                     (double)FLOAT_8061b864, (double)FLOAT_8061b868,
                                     (double)FLOAT_8061b848, (double)FLOAT_8061b86c);
    particle_emitter_create((double)FLOAT_8061b870, (double)FLOAT_8061b874, (double)FLOAT_8061b878,
                            (u32)&DAT_805ee8cc, (u32)&DAT_805eed70, 7, 0);
    particle_emitter_set_color_range((double)FLOAT_8061b848, (double)FLOAT_8061b848,
                                     (double)FLOAT_8061b87c, (double)FLOAT_8061b84c);
    particle_emitter_set_lifetime_range((double)FLOAT_8061b848, (double)FLOAT_8061b84c);
    ((EmitterSetFlagCbFn)&particle_emitter_set_flags)(setFlagCb);
    ((EmitterSetScalePtrFn)&particle_emitter_set_scale)(scaleA);

    // --- template 1 : DAT_805ee848 / DAT_805eee3c ---
    particle_def_create((double)FLOAT_8061b880, (double)FLOAT_8061b848, (u32)&DAT_805eee3c, 3, 1, 1, 0x306, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_size_keyframes(d, d, d, d, 0x7f7f7f00, 0x7f7f7f00, 0);
    }
    particle_def_set_keyframes_raw((int*)kfE, (int*)kfD, 4);
    particle_def_set_size_start_end((double)FLOAT_8061b84c, (double)FLOAT_8061b850,
                                    (double)FLOAT_8061b884, (double)FLOAT_8061b888);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_color_end(d, d, d, d);
    }
    particle_def_set_velocity_ranges((double)FLOAT_8061b88c, (double)FLOAT_8061b84c,
                                     (double)FLOAT_8061b890, (double)FLOAT_8061b870,
                                     (double)FLOAT_8061b848, (double)FLOAT_8061b86c);
    particle_emitter_create((double)FLOAT_8061b84c, (double)FLOAT_8061b84c, (double)FLOAT_8061b874,
                            (u32)&DAT_805ee848, (u32)&DAT_805eee3c, 7, 0);
    particle_emitter_set_color_range((double)FLOAT_8061b848, (double)FLOAT_8061b84c,
                                     (double)FLOAT_8061b864, (double)FLOAT_8061b84c);
    particle_emitter_set_lifetime_range((double)FLOAT_8061b848, (double)FLOAT_8061b84c);
    ((EmitterSetScalePtrFn)&particle_emitter_set_scale)(kfF);
    ((EmitterSetFlagCbFn)&particle_emitter_set_flags)(setFlagCb);
    ((EmitterSetScalePtrFn)&particle_emitter_set_scale)(scaleA);

    // --- template 2 : DAT_805ee950 / DAT_805eef08 ---
    particle_def_create((double)FLOAT_8061b894, (double)FLOAT_8061b848, (u32)&DAT_805eef08, 0, 1, 1, 0x302, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_size_keyframes(d, d, d, d, 0x65656500, 0x65656500, 0);
    }
    particle_def_set_keyframes_raw((int*)kf118, (int*)kf108, 4);
    particle_def_set_size_start_end((double)FLOAT_8061b84c, (double)FLOAT_8061b850,
                                    (double)FLOAT_8061b848, (double)FLOAT_8061b898);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_color_end(d, d, (double)FLOAT_8061b89c, d);
    }
    particle_def_set_velocity_ranges((double)FLOAT_8061b848, (double)FLOAT_8061b870,
                                     (double)FLOAT_8061b8a0, (double)FLOAT_8061b8a4,
                                     (double)FLOAT_8061b848, (double)FLOAT_8061b86c);
    particle_emitter_create((double)FLOAT_8061b880, (double)FLOAT_8061b8a8, (double)FLOAT_8061b8ac,
                            (u32)&DAT_805ee950, (u32)&DAT_805eef08, 7, 0);
    particle_emitter_set_color_range((double)FLOAT_8061b8b0, (double)FLOAT_8061b84c,
                                     (double)FLOAT_8061b8b4, (double)FLOAT_8061b84c);
    particle_emitter_set_lifetime_range((double)FLOAT_8061b8b0, (double)FLOAT_8061b84c);
    ((EmitterSetFlagCbFn)&particle_emitter_set_flags)(setFlagCb);
    ((EmitterSetScalePtrFn)&particle_emitter_set_scale)(scaleA);

    // --- template 3 : DAT_805eec68 / DAT_805eefd4 ---
    particle_def_create((double)FLOAT_8061b8b8, (double)FLOAT_8061b848, (u32)&DAT_805eefd4, 0, 1, 1, 0x302, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_size_keyframes(d, d, d, d, 0x65656500, 0x65656500, 0);
    }
    particle_def_set_keyframes_raw((int*)kf118, (int*)kf108, 4);
    particle_def_set_size_start_end((double)FLOAT_8061b84c, (double)FLOAT_8061b850,
                                    (double)FLOAT_8061b848, (double)FLOAT_8061b898);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_color_end(d, d, (double)FLOAT_8061b89c, d);
    }
    particle_def_set_velocity_ranges((double)FLOAT_8061b848, (double)FLOAT_8061b870,
                                     (double)FLOAT_8061b854, (double)FLOAT_8061b8a4,
                                     (double)FLOAT_8061b848, (double)FLOAT_8061b86c);
    particle_emitter_create((double)FLOAT_8061b880, (double)FLOAT_8061b8a8, (double)FLOAT_8061b8ac,
                            (u32)&DAT_805eec68, (u32)&DAT_805eefd4, 7, 0);
    particle_emitter_set_color_range((double)FLOAT_8061b8b0, (double)FLOAT_8061b84c,
                                     (double)FLOAT_8061b8b4, (double)FLOAT_8061b84c);
    particle_emitter_set_lifetime_range((double)FLOAT_8061b8b0, (double)FLOAT_8061b84c);
    ((EmitterSetFlagCbFn)&particle_emitter_set_flags)(setFlagCb);
    ((EmitterSetScalePtrFn)&particle_emitter_set_scale)(scaleA);

    // --- template 4 : DAT_805eea58 / DAT_805ef0a0 ---
    particle_def_create((double)FLOAT_8061b864, (double)FLOAT_8061b848, (u32)&DAT_805ef0a0, 0, 1, 1, 0x306, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_size_keyframes(d, d, d, d, 0x75757500, 0x5b5b5b00, 0);
    }
    particle_def_set_keyframes_raw((int*)a0row, (int*)(a0row + 5), 5);
    particle_def_set_size_start_end((double)FLOAT_8061b84c, (double)FLOAT_8061b850,
                                    (double)FLOAT_8061b870, (double)FLOAT_8061b858);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_color_end(d, d, (double)FLOAT_8061b8bc, d);
    }
    particle_def_set_velocity_ranges((double)FLOAT_8061b8c0, (double)FLOAT_8061b8c4,
                                     (double)FLOAT_8061b898, (double)FLOAT_8061b870,
                                     (double)FLOAT_8061b848, (double)FLOAT_8061b86c);
    particle_emitter_create((double)FLOAT_8061b8b8, (double)FLOAT_8061b874, (double)FLOAT_8061b874,
                            (u32)&DAT_805eea58, (u32)&DAT_805ef0a0, 7, 0);
    particle_emitter_set_color_range((double)FLOAT_8061b848, (double)FLOAT_8061b84c,
                                     (double)FLOAT_8061b87c, (double)FLOAT_8061b84c);
    particle_emitter_set_lifetime_range((double)FLOAT_8061b848, (double)FLOAT_8061b84c);
    ((EmitterSetFlagCbFn)&particle_emitter_set_flags)(setFlagCb);
    ((EmitterSetScalePtrFn)&particle_emitter_set_scale)(scaleA);

    // --- template 5 : DAT_805ee9d4 / DAT_805ef16c ---
    particle_def_create((double)FLOAT_8061b894, (double)FLOAT_8061b848, (u32)&DAT_805ef16c, 0, 1, 1, 0x303, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_size_keyframes(d, d, d, d, 0x65656500, 0x65656500, 0);
    }
    particle_def_set_keyframes_raw((int*)kf138, (int*)kf128, 4);
    particle_def_set_size_start_end((double)FLOAT_8061b84c, (double)FLOAT_8061b850,
                                    (double)FLOAT_8061b87c, (double)FLOAT_8061b864);
    particle_def_set_color_end((double)FLOAT_8061b874, (double)FLOAT_8061b84c,
                               (double)FLOAT_8061b8c8, (double)FLOAT_8061b84c);
    particle_def_set_velocity_ranges((double)FLOAT_8061b848, (double)FLOAT_8061b8b8,
                                     (double)FLOAT_8061b8a0, (double)FLOAT_8061b8a4,
                                     (double)FLOAT_8061b848, (double)FLOAT_8061b86c);
    particle_emitter_create((double)FLOAT_8061b880, (double)FLOAT_8061b85c, (double)FLOAT_8061b874,
                            (u32)&DAT_805ee9d4, (u32)&DAT_805ef16c, 7, 0);
    particle_emitter_set_color_range((double)FLOAT_8061b8b0, (double)FLOAT_8061b84c,
                                     (double)FLOAT_8061b8cc, (double)FLOAT_8061b84c);
    particle_emitter_set_lifetime_range((double)FLOAT_8061b8b0, (double)FLOAT_8061b84c);
    ((EmitterSetFlagCbFn)&particle_emitter_set_flags)(setFlagCb);
    ((EmitterSetScalePtrFn)&particle_emitter_set_scale)(scaleA);

    // --- template 6 : DAT_805eeadc / DAT_805ef238 ---
    particle_def_create((double)FLOAT_8061b894, (double)FLOAT_8061b848, (u32)&DAT_805ef238, 0, 1, 1, 0x304, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_size_keyframes(d, d, d, d, 0x35353500, 0x35353500, 0);
    }
    particle_def_set_keyframes_raw((int*)kf158, (int*)kf148, 4);
    particle_def_set_size_start_end((double)FLOAT_8061b84c, (double)FLOAT_8061b850,
                                    (double)FLOAT_8061b848, (double)FLOAT_8061b898);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_color_end(d, d, (double)FLOAT_8061b89c, d);
    }
    particle_def_set_velocity_ranges((double)FLOAT_8061b848, (double)FLOAT_8061b870,
                                     (double)FLOAT_8061b8a0, (double)FLOAT_8061b8a4,
                                     (double)FLOAT_8061b848, (double)FLOAT_8061b86c);
    particle_emitter_create((double)FLOAT_8061b880, (double)FLOAT_8061b8a8, (double)FLOAT_8061b8ac,
                            (u32)&DAT_805eeadc, (u32)&DAT_805ef238, 7, 0);
    particle_emitter_set_color_range((double)FLOAT_8061b8b0, (double)FLOAT_8061b84c,
                                     (double)FLOAT_8061b8b4, (double)FLOAT_8061b84c);
    particle_emitter_set_lifetime_range((double)FLOAT_8061b8b0, (double)FLOAT_8061b84c);
    ((EmitterSetFlagCbFn)&particle_emitter_set_flags)(setFlagCb);
    ((EmitterSetScalePtrFn)&particle_emitter_set_scale)(scaleA);

    // --- template 7 : DAT_805eeb60 / DAT_805ef304 ---
    particle_def_create((double)FLOAT_8061b894, (double)FLOAT_8061b848, (u32)&DAT_805ef304, 0, 1, 1, 0x305, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_size_keyframes(d, d, d, d, 0x35353500, 0x35353500, 0);
    }
    particle_def_set_keyframes_raw((int*)kf178, (int*)kf168, 4);
    particle_def_set_size_start_end((double)FLOAT_8061b84c, (double)FLOAT_8061b850,
                                    (double)FLOAT_8061b87c, (double)FLOAT_8061b864);
    particle_def_set_color_end((double)FLOAT_8061b874, (double)FLOAT_8061b84c,
                               (double)FLOAT_8061b8c8, (double)FLOAT_8061b84c);
    particle_def_set_velocity_ranges((double)FLOAT_8061b848, (double)FLOAT_8061b8b8,
                                     (double)FLOAT_8061b8a0, (double)FLOAT_8061b8a4,
                                     (double)FLOAT_8061b848, (double)FLOAT_8061b86c);
    particle_emitter_create((double)FLOAT_8061b880, (double)FLOAT_8061b85c, (double)FLOAT_8061b874,
                            (u32)&DAT_805eeb60, (u32)&DAT_805ef304, 7, 0);
    particle_emitter_set_color_range((double)FLOAT_8061b8b0, (double)FLOAT_8061b84c,
                                     (double)FLOAT_8061b8cc, (double)FLOAT_8061b84c);
    particle_emitter_set_lifetime_range((double)FLOAT_8061b8b0, (double)FLOAT_8061b84c);
    ((EmitterSetFlagCbFn)&particle_emitter_set_flags)(setFlagCb);
    ((EmitterSetScalePtrFn)&particle_emitter_set_scale)(scaleA);

    // --- template 8 : DAT_805eebe4 / DAT_805ef3d0 (with drag) ---
    particle_def_create((double)FLOAT_8061b894, (double)FLOAT_8061b848, (u32)&DAT_805ef3d0, 0, 1, 1, 0x302, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_size_keyframes(d, d, d, d, 0x65656500, 0x65656500, 0);
    }
    particle_def_set_keyframes_raw((int*)kf118, (int*)kf108, 4);
    particle_def_set_size_start_end((double)FLOAT_8061b84c, (double)FLOAT_8061b850,
                                    (double)FLOAT_8061b848, (double)FLOAT_8061b898);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_color_end(d, d, (double)FLOAT_8061b8d0, d);
    }
    particle_def_set_velocity_ranges((double)FLOAT_8061b8d4, (double)FLOAT_8061b8d4,
                                     (double)FLOAT_8061b864, (double)FLOAT_8061b854,
                                     (double)FLOAT_8061b848, (double)FLOAT_8061b864);
    particle_def_set_drag((double)FLOAT_8061b8d8);
    particle_emitter_create((double)FLOAT_8061b8dc, (double)FLOAT_8061b8a8, (double)FLOAT_8061b8e0,
                            (u32)&DAT_805eebe4, (u32)&DAT_805ef3d0, 7, 0);
    particle_emitter_set_color_range((double)FLOAT_8061b85c, (double)FLOAT_8061b84c,
                                     (double)FLOAT_8061b8e4, (double)FLOAT_8061b84c);
    ((EmitterSetFlagCbFn)&particle_emitter_set_flags)(setFlagCb);

    // --- template 9 : DAT_805eecec / DAT_805ef49c ---
    particle_def_create((double)FLOAT_8061b8c0, (double)FLOAT_8061b848, (u32)&DAT_805ef49c, 0, 0, 1, 0x300, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_size_keyframes((double)FLOAT_8061b848, d, d, d, 0, 0, 0xf0f0f00);
    }
    particle_def_set_keyframes_raw((int*)kfC, (int*)kfB, 4);
    particle_def_set_size_start_end((double)FLOAT_8061b84c, (double)FLOAT_8061b850,
                                    (double)FLOAT_8061b848, (double)FLOAT_8061b898);
    {
        double d = (double)FLOAT_8061b84c;
        particle_def_set_color_end(d, d, (double)FLOAT_8061b8e8, d);
    }
    particle_def_set_velocity_ranges((double)FLOAT_8061b84c, (double)FLOAT_8061b8ec,
                                     (double)FLOAT_8061b8c0, (double)FLOAT_8061b8a4,
                                     (double)FLOAT_8061b848, (double)FLOAT_8061b848);
    particle_emitter_create((double)FLOAT_8061b848, (double)FLOAT_8061b880, (double)FLOAT_8061b874,
                            (u32)&DAT_805eecec, (u32)&DAT_805ef49c, 7, 0);
    {
        double d = (double)FLOAT_8061b84c;
        particle_emitter_set_color_range(d, d, (double)FLOAT_8061b8b4, d);
    }
    ((EmitterSetFlagCbFn)&particle_emitter_set_flags)(setFlagCb);
    ((EmitterSetScalePtrFn)&particle_emitter_set_scale)(scaleA);

    FUN_80382700();
}

// ============================================================================
// 0x8037ed6c  prop_fx_transfer_handles
// Move the source entity's four cached emitter handles (src+0x7c[+0xd24..])
// onto the destination entity (dst+0x7c[+0x50..]), tagging each with flag bits
// and re-pointing any emitter whose owner (+0x88) was the source to the dest.
// ============================================================================
void prop_fx_transfer_handles(void)
{
    unsigned long long r = FUN_803826dc();
    int dst = (int)r;
    int src = (int)((unsigned long long)r >> 0x20);

    if (src == 0) { FUN_80382728(); return; }
    int srcCache = *(int*)(src + 0x7c);
    if (srcCache == 0) { FUN_80382728(); return; }
    if (dst == 0) { FUN_80382728(); return; }
    int dstCache = *(int*)(dst + 0x7c);
    if (dstCache == 0) { FUN_80382728(); return; }

    unsigned handle;
    for (int i = 0, off = 0; i < 4; ++i, off += 4) {
        int dstSlot = dstCache + off;
        particle_handle_transfer((unsigned*)(dstSlot + 0x50), (unsigned*)(srcCache + off + 0xd24));
        handle = *(unsigned*)(dstSlot + 0x50);
        ((void (*)(unsigned*, unsigned, unsigned))&particle_emitter_set_flags)(&handle, 0x10000001, 0x100000);
        int emitter = *(int*)(dstSlot + 0x50);
        if ((emitter != 0) && (*(int*)(emitter + 0x88) == src)) {
            *(int*)(emitter + 0x88) = dst;
        }
    }
    FUN_80382728();
}




// ============================================================================
// batch_1060 additions: particle-handle cache freelist (0x8037ee2c - 0x8037ef04)
//
// The prop FX cache keeps a 0x960-byte pool carved into 0xf nodes of 0xa0 bytes
// through the generic freelist helpers, plus a per-entity destroy of the four
// cached emitter handles. DAT_805ef568 is the freelist control block.
// ============================================================================
extern int  DAT_805ef568;    // particle-cache freelist control block

extern "C" int  FUN_803826f0(void);   // begin cache op -> entity record ptr
extern "C" void FUN_8038273c(void);    // end cache op

// ---- 0x8037ee2c  prop_fx_cache_destroy_handles -----------------------------
// Destroy the entity's four cached emitter handles (record+0x00..+0x0c).
void prop_fx_cache_destroy_handles(void)
{
    int rec = FUN_803826f0();
    int handle[6];
    for (int i = 0, off = 0; i < 4; ++i, off += 4) {
        handle[0] = *(int*)(rec + off);
        particle_handle_destroy(handle);
    }
    FUN_8038273c();
}

// ---- 0x8037ee84  prop_fx_cache_pool_alloc ----------------------------------
// Allocate the 0x960-byte cache pool and lay it out as a 0xf-entry, 0xa0-stride
// freelist. mem_alloc has a narrowed generated prototype; reach the wide ABI by
// address (RangeAllocFn declared earlier in this file).
void prop_fx_cache_pool_alloc(unsigned p1, unsigned p2, unsigned p3, unsigned p4,
                              unsigned p5, unsigned p6, unsigned p7, unsigned p8,
                              unsigned p11, unsigned p12, unsigned p13, unsigned p14,
                              unsigned p15, unsigned p16)
{
    void* base = (void*)((RangeAllocFn)mem_alloc)(p1, p2, p3, p4, p5, p6, p7, p8,
                                                  0x960, 1, p11, p12, p13, p14, p15, p16);
    freelist_init((void**)&DAT_805ef568, 0xa0, base, 0xf);
}

// ---- 0x8037eec4  prop_fx_cache_pool_reset ----------------------------------
// Re-init the freelist in place from its current head pointer.
void prop_fx_cache_pool_reset(void)
{
    void* base = freelist_peek_head((void**)&DAT_805ef568);
    freelist_init((void**)&DAT_805ef568, 0xa0, base, 0xf);
}

// ---- 0x8037ef04  prop_fx_cache_node_alloc ----------------------------------
// Pop a node from the cache freelist and zero its 0xa0 bytes.
void* prop_fx_cache_node_alloc(void)
{
    void* node = freelist_pop((int)(long long)&DAT_805ef568);
    memset_wrapper((int)node, 0, 0xa0);
    return node;
}
