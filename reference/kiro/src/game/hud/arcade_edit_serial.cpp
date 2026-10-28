// Batch: batch_881 | Module: arcade map-editor map serialize / deserialize (save-load)
// Address range: 0x8030fb3c - 0x80311ea0
// Date: 2026-09-11
//
// The arcade map-editor's save/load core. aveditor_serialize_map walks the whole live
// editor state (config, member list DAT_80612c14, roster, props, path graphs, aux tables)
// and writes it into a scratch buffer as a stream of tagged chunks (tag word 0xc776xx +
// length, then payload). aveditor_deserialize_map does the inverse: it allocates a load
// buffer and replays each chunk through a switch on the tag, rebuilding ctx DAT_80612be4.
// The small helpers are the editor-state-block field accessors (byte+9 flag, word[0x8f]
// capacity), the empty-map flag, the height-range recompute, and two load thunks.
//
//   aveditor_set_entdesc_flag_byte9  0x8030fb3c  write editor-state block byte+9 flag.
//   aveditor_get_record_capacity     0x8030fb78  read editor-state block word[0x8f] (0 if null).
//   aveditor_set_record_capacity     0x8030fbb8  write editor-state block word[0x8f].
//   aveditor_serialize_map           0x8030fbf4  serialize whole map to tagged-chunk buffer.
//   aveditor_restore_cursor_state    0x80310f4c  chunk 0xc77671 handler: restore cursor fields
//                                                (mode 0) or the challenge/stats record (else).
//   aveditor_set_empty_map_flag      0x80310fe0  set/clear empty-map flag bit 0x800 @ctx+0x25c.
//   aveditor_recompute_height_range  0x80311018  recompute height field ctx+0x264 / +0x268.
//   aveditor_deserialize_map         0x803110d4  rebuild map from serialized buffer (tag switch).
//   aveditor_flag_dirty_if_pending   0x80311e68  OR dirty bit 1 @ctx+0x238 when pending.
//   aveditor_load_thunk              0x80311ea0  thunk to FUN_80311f98 (map-load entry).
//
// NON-MATCHING: the serializer/deserializer carry the PPC scratch-frame push/pop
// intrinsics (FUN_80382xxx) as raw externs and the {base,index} register-pair returns
// (FUN_803826xx) are read as u64 and split hi/lo verbatim. The PPC int->float magic-bias
// idiom ((double)CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE_8061a5e0) is preserved.
// The decompiler's f31/ps31 incoming-FPR spill locals are kept as-is. Big-function
// unreachable-block warnings are dead tail code the decompiler pruned. Named helpers
// resolve via generated_decls.h; every raw FUN_/DAT_/DOUBLE_ symbol has a per-file extern.

#include "common.h"
#include "hud.h"

// Named cross-module + prior-batch helpers (aveditor_tally_and_trigger,
// aveditor_write_record_pair, aveditor_relink_type21_cells, aveditor_sum_member_counts,
// aveditor_sum_member_type_counts, aveditor_place_all_segments, aveditor_min_linked_index,
// aveditor_update_member_placeable_flag, aveditor_count_active_type_cells,
// aveditor_member_list_index, aveditor_snapshot_cursor_state, avanim_layout_is_0x21,
// entdesc_flag_bit0_key/bit1_key, map_build_connectivity, memset_wrapper, leaf_31e8,
// stats_get_challenge_record, asel_reset_screen, mem_alloc, mem_free_top,
// match_register_event_handlers) resolve through generated_decls.h at C++ linkage and
// must NOT be re-declared here. Only raw FUN_/DAT_ symbols get per-file externs below.
extern "C" {

// active editor screen ctx + global member list
extern int    DAT_80612be4;
extern float* DAT_80612c14;
extern int    DAT_80612bd8;   // connectivity accumulator

// serialize/deserialize working globals
extern unsigned int DAT_805d59a0;   // current config id
extern int          DAT_805d59a4;
extern int          DAT_805d59a8;
extern unsigned int DAT_805d59ac;
extern int          DAT_805d59dc;   // "config touched" flag
extern int          DAT_805015d8;   // stats slot id
extern unsigned char DAT_805d5fa4;  // cursor snapshot record (0x10 bytes)
extern double       DOUBLE_8061a5e0; // int->float magic bias constant

// palette / color tables copied into the header chunk
extern unsigned int DAT_805d6630;
extern int          DAT_805d6340;
extern int          DAT_805d6260;
extern int          DAT_805d6180;
extern short        DAT_80477bca;   // per-type class-id table (0x62 stride)
extern unsigned int DAT_80477c80;   // per-type capability flags (0x31 stride)
extern void*        DAT_8047c154;   // per-type position-callback table (0x140 stride)

// scratch-frame push/pop intrinsics (register-pair returns carry the buffer base/index)
u64  FUN_803826c0(void);   // serialize: {buffer_base, index/flags}
u64  FUN_803826bc(void);   // deserialize: {buffer_base, index/flags}
void FUN_8038270c(void);
void FUN_80382708(void);

// editor-state block resolver (shared with batch_880)
int* FUN_80339b90(int* scratch, int key); // resolve editor-state block (key 0xc77668)

// raw FUN_ helpers used by the serializer. NOTE: several of these appear in
// generated_decls.h typed `void` (the decompiler couldn't see the return at some
// call site) but are used here for their return value; per Compile-Clean rule 6 we
// invoke those through a typed function-pointer cast at the call site rather than
// re-declaring the symbol. The ones below are NOT in generated_decls.h and get a
// per-file extern.
int  FUN_8031424c(void);                 // count live members
int  FUN_803142f4(void);                 // extra per-member record count
int  FUN_80336948(int kind);             // count aux records of a kind
void FUN_8033646c(int src, u32 count);   // unpack kind-10 records
void FUN_803366ac(int src, int len);     // unpack kind-3 records
u32  FUN_8031d710(void);                 // config aux table A base
int  FUN_8031d708(void);                 // config aux count
u32  FUN_8032067c(void);                 // current sub-mode word
u32  FUN_80336778(void);                 // aux record buffer base
void FUN_80336408(int dst, int count);   // pack kind-10 records
void FUN_803366e8(int dst);              // pack kind-3 records
void FUN_8038b510(int* dst, int* src);   // copy 0x14-byte block
int  FUN_80314280(int* chain, int start);// count linked cells
int  FUN_80343c4c(int type);             // per-type cell-count A
u32  FUN_80342e08(void);                 // next placed sub-record id
u32  FUN_80342bc8(float* member, int* pos, int outPtr); // place linked companion
unsigned char FUN_8032b070(int slot);    // read soundpad enable byte
int  FUN_80313af4(double x, double y, double z);        // create/resolve cell
int  FUN_80344710(double x, double y, double z, u32 rec, u32 kind, unsigned short* outPair);
int  FUN_8034545c(unsigned short* pair);  // validate placed pair
void FUN_80345484(int pairPtr);           // drop invalid pair
int  FUN_80341e64(void);                  // path-node handle
void FUN_80336690(int block, int idx);    // resolve path entry
int  FUN_80338b88(int idx);               // resolve path descriptor
u32  FUN_80314358(int handle);            // path length
u32  FUN_803395a4(int cell);              // cell aux short
int  FUN_80328888(void);                  // pending-placement predicate
int  FUN_8032a670(void);                  // active grenade/prop record
void FUN_8032a830(void);
void FUN_8032a84c(void);

// raw FUN_ helpers used by the deserializer
u64  FUN_80337c9c(void);                 // required load-buffer size
void FUN_80339c00(int* buf);             // validate/prep load buffer
unsigned char FUN_8031d614(u32 cfgId);   // apply config id, returns flag
u32  FUN_8031d6e8(void);                 // derived config word
void FUN_803430b8(u32 a, u32* out, u32 b);
int  FUN_80313e94(double x, double y, u32 rec, int a, float b, float c); // spawn from record
char FUN_8032582c(u32 packedColor);      // pack color byte
void FUN_80313da0(void);                 // post-spawn fixup
int  FUN_80314148(int rec, u32* pos, u32 flags); // attach linked cell
void FUN_8032b0d8(int slot, u32 val);    // write soundpad enable
void FUN_803365b8(void);
void FUN_80336548(void);
void FUN_8032c194(void);
void FUN_80311f98(void);                 // map-load entry (thunk target)

} // extern "C"

// Several prior-batch helpers are typed `void` in generated_decls.h (the decompiler
// couldn't see a return at their defining call site) but the serializer/deserializer
// read their PPC return register (EAX). Per Compile-Clean rule 6 we invoke them through
// a typed function-pointer cast at the call site rather than re-declaring the symbol.
static inline int call_ret_i(void (*fn)(void)) {
    return ((int(*)(void))(void*)fn)();
}
#define TALLY_AND_TRIGGER()       call_ret_i(aveditor_tally_and_trigger)
#define PLACE_ALL_SEGMENTS()      call_ret_i(aveditor_place_all_segments)
#define RELINK_TYPE21_CELLS()     call_ret_i(aveditor_relink_type21_cells)
#define MAP_BUILD_CONNECTIVITY()  call_ret_i(map_build_connectivity)
#define COUNT_ACTIVE_TYPE_CELLS() call_ret_i(aveditor_count_active_type_cells)

// ---------------------------------------------------------------------------
// 0x8030fb3c  aveditor_set_entdesc_flag_byte9
// ---------------------------------------------------------------------------
void aveditor_set_entdesc_flag_byte9(unsigned char value, int* scratch)
{
    int* block = FUN_80339b90(scratch, 0xc77668);
    *(unsigned char*)((int)block + 9) = value;
}

// ---------------------------------------------------------------------------
// 0x8030fb78  aveditor_get_record_capacity
// ---------------------------------------------------------------------------
int aveditor_get_record_capacity(int* scratch)
{
    if (*scratch == 0) {
        return 0;
    }
    int* block = FUN_80339b90(scratch, 0xc77668);
    return block[0x8f];
}

// ---------------------------------------------------------------------------
// 0x8030fbb8  aveditor_set_record_capacity
// ---------------------------------------------------------------------------
void aveditor_set_record_capacity(int value, int* scratch)
{
    int* block = FUN_80339b90(scratch, 0xc77668);
    block[0x8f] = value;
}

// ---------------------------------------------------------------------------
// 0x8030fbf4  aveditor_serialize_map
// Serialize the entire editor map into a scratch buffer as a stream of tagged
// chunks. The buffer base + a flags word arrive in the FUN_803826c0 register pair.
// ---------------------------------------------------------------------------
void aveditor_serialize_map(void)
{
    u64 pair = FUN_803826c0();
    int* buf = (int*)(u32)(pair >> 0x20);
    int memberCount = FUN_8031424c();
    char lockCount = '\0';
    u32 savedMode = *(u32*)(DAT_80612be4 + 0x264);

    if ((pair & 4) != 0) {
        DAT_805d59dc = 1;
    }
    memset_wrapper((int)buf, 0, 0x7ffc);

    u32 flagsWord = TALLY_AND_TRIGGER();
    int off = aveditor_write_record_pair((int)buf, 0, 0xc77667, 0x12d);
    off = aveditor_write_record_pair((int)buf, off, 0xc77668, 0x3d0);
    u32* auxTableA = (u32*)FUN_8031d710();
    int auxCount = FUN_8031d708();

    u32* hdr = (u32*)((int)buf + off);
    *hdr = flagsWord;
    u32* p8f = hdr + 0x8f;
    *(short*)((int)hdr + 10) = (short)DAT_805d59a0;
    *(char*)(hdr + 3) = (char)DAT_805d59ac;
    hdr[1] = memberCount;
    *(short*)((int)hdr + 0xe) = (short)*(u32*)(DAT_80612be4 + 0x210);
    *(short*)(hdr + 4) = (short)*(u32*)(DAT_80612be4 + 0x220);
    *(char*)((int)hdr + 0x12) = (char)*(u32*)(DAT_80612be4 + 0x224);
    *(short*)(hdr + 5) = (short)DAT_805d59a4;
    hdr[0x8e] = 0;
    *(char*)((int)hdr + 0x236) = (char)*(u32*)(DAT_80612be4 + 0x228);

    // copy the 0x32-entry config array into the header
    u32* src = (u32*)(DAT_80612be4 + 0x11484);
    for (int i = 0x32; i != 0; i = i - 1) {
        u32* lo = src + 1;
        src = src + 2;
        u32 hi = *src;
        p8f[1] = *lo;
        p8f = p8f + 2;
        *p8f = hi;
    }

    // set the enabled-config bitmask from the aux table
    if (1 < auxCount) {
        int bit = 1;
        int idx = 4;
        int n = auxCount - 1;
        do {
            if (*(int*)((int)auxTableA + idx) != 0) {
                hdr[0x8e] = hdr[0x8e] | (1 << bit);
            }
            bit = bit + 1;
            idx = idx + 4;
            n = n - 1;
        } while (n != 0);
    }
    if (*(int*)(DAT_80612be4 + 0x200) != 0) { hdr[0x8e] = hdr[0x8e] | 1; }
    if (*(int*)(DAT_80612be4 + 0x214) != 0) { hdr[0x8e] = hdr[0x8e] | 0x8000; }
    if (*(int*)(DAT_80612be4 + 0x218) != 0) { hdr[0x8e] = hdr[0x8e] | 0x10000; }
    if (*(int*)(DAT_80612be4 + 0x21c) != 0) { hdr[0x8e] = hdr[0x8e] | 0x20000; }

    // 10-entry team/spawn table
    *(unsigned char*)((int)hdr + 0x16) = 10;
    int srcOff = 0;
    int dstOff = 0;
    for (int i = 0; i < (int)(unsigned int)*(unsigned char*)((int)hdr + 0x16); i = i + 1) {
        *(short*)((int)hdr + dstOff + 0x18) = *(short*)(DAT_80612be4 + srcOff + 0x11864);
        int a = srcOff + 0x1186a;
        int b = srcOff + 0x11867;
        srcOff = srcOff + 8;
        *(char*)((int)hdr + dstOff + 0x1b) = (char)*(short*)(DAT_80612be4 + a);
        *(unsigned char*)((int)hdr + dstOff + 0x1a) = *(unsigned char*)(DAT_80612be4 + b);
        dstOff = dstOff + 4;
    }

    *(short*)(hdr + 0x10) = (short)FUN_8032067c();

    // 6-entry aux short table
    *(unsigned char*)((int)hdr + 0x42) = 6;
    dstOff = 0;
    srcOff = 0;
    for (int i = 0; i < (int)(unsigned int)*(unsigned char*)((int)hdr + 0x42); i = i + 1) {
        int a = srcOff + 0x1190c;
        int b = dstOff + 0x44;
        dstOff = dstOff + 2;
        srcOff = srcOff + 4;
        *(short*)((int)hdr + b) = (short)*(u32*)(DAT_80612be4 + a);
    }

    FUN_8038b510((int*)((int)hdr + 0x62), (int*)&DAT_805d6340);
    FUN_8038b510((int*)((int)hdr + 0x76), (int*)&DAT_805d6260);
    FUN_8038b510((int*)((int)hdr + 0x156), (int*)&DAT_805d6180);

    // palette chunk
    off = aveditor_write_record_pair((int)buf, off + 0x3d0, 0xc77672, 0x100);
    leaf_31e8((int)buf + off, (u32)&DAT_805d6630, 0x100);
    off = off + 0x100;

    // aux record blocks (kinds 7/0xe/9 packed together, then 10, then 3)
    int c7 = FUN_80336948(7);
    int ce = FUN_80336948(0xe);
    int c9 = FUN_80336948(9);
    int auxTotal = ce + c7 + c9;
    if (auxTotal != 0) {
        auxTotal = auxTotal * 0x40;
        off = aveditor_write_record_pair((int)buf, off, 0xc77673, auxTotal);
        u32 recBase = FUN_80336778();
        leaf_31e8((int)buf + off, recBase, auxTotal);
        off = off + auxTotal;
    }
    {
        int c10 = FUN_80336948(10);
        if (c10 != 0) {
            unsigned int sz = ((unsigned int)(c10 * 0x14) + 0xf) & 0xfffffff0;
            unsigned char scratch10[2000];
            memset_wrapper((int)scratch10, 0, 2000);
            FUN_80336408((int)scratch10, c10);
            off = aveditor_write_record_pair((int)buf, off, 0xc77674, sz);
            leaf_31e8((int)buf + off, (u32)scratch10, sz);
            off = off + sz;
        }
    }
    {
        int c3 = FUN_80336948(3);
        if (c3 != 0) {
            c3 = c3 * 8;
            unsigned char scratch3[3000];
            memset_wrapper((int)scratch3, 0, 3000);
            FUN_803366e8((int)scratch3);
            off = aveditor_write_record_pair((int)buf, off, 0xc77675, c3);
            leaf_31e8((int)buf + off, (u32)scratch3, c3);
            off = off + c3;
        }
    }

    DAT_80612bd8 = 0;
    if (memberCount != 0) {
        int extra = FUN_803142f4();
        int type21 = RELINK_TYPE21_CELLS();
        int sumA = aveditor_sum_member_counts();
        int sumB = aveditor_sum_member_type_counts();
        off = aveditor_write_record_pair((int)buf, off, 0xc77669,
                  memberCount * 0x28 + extra * 0x18 + (sumA + sumB) * 8 + type21 * 4);

        for (float* member = DAT_80612c14; member != nullptr; member = (float*)*(int*)((int*)member + 0x10)) {
            int* rec = (int*)((int)buf + off);
            *rec = ((int)member[0xb] + 0x7fb83eb8) / 0x140;
            int type = *rec;
            *(char*)(rec + 1) = (char)(int)*member;
            *(char*)((int)rec + 5) = (char)(int)member[1];
            *(char*)((int)rec + 6) = (char)(int)member[2];
            *(char*)((int)rec + 7) = (char)(int)member[3];
            *(short*)(rec + 8) = 0;
            aveditor_update_member_placeable_flag((int)member);
            *(short*)(rec + 7) = (short)(int)member[0x11];
            *(char*)((int)rec + 0x23) = (char)('\x01' - (((u32)member[5] & 0x20000000) == 0 ? 1 : 0));
            rec[2] = PLACE_ALL_SEGMENTS();

            void** cb = (void**)(&DAT_8047c154 + type * 0x140);
            if (*cb != nullptr) {
                u32 extraFlags = ((u32(*)(float*, u32))*cb)(member, DAT_805d59a0);
                rec[2] = rec[2] | extraFlags;
            }
            rec[3] = ((int)(&DAT_805d6630)[*(unsigned char*)(member + 6)] & 0x7f7f7f00) * 2 + 0xff;
            rec[4] = ((int)(&DAT_805d6630)[*(unsigned char*)((int)member + 0x19)] & 0x7f7f7f00) * 2 + 0xff;
            *(char*)((int)rec + 0x22) = (char)(int)member[7];
            rec[5] = (int)member[10];
            rec[6] = (int)member[9];
            int linked = FUN_80314280((int*)(int)member[0xf], -1);
            *(short*)((int)rec + 0x1e) = (short)linked;
            int cellsA = FUN_80343c4c(*rec);
            int cellsB = COUNT_ACTIVE_TYPE_CELLS();
            *(short*)(rec + 9) = (short)((cellsB + cellsA) * 8 + 0x28);
            off = off + *(short*)(rec + 9);

            if (*(short*)((int)rec + 0x1e) != 0) {
                for (int* cell = (int*)(int)member[0xf]; cell != nullptr; cell = (int*)cell[5]) {
                    int cellType = *cell;
                    u32* out = (u32*)((int)buf + off);
                    *(short*)((int)out + 0xe) = (short)cellType;
                    *(char*)((int)out + 0x15) = (char)*(short*)(cell + 3);
                    *(char*)((int)out + 0x16) = (char)*(short*)((int)cell + 0xe);
                    *(char*)((int)out + 0x17) = (char)*(short*)(cell + 4);

                    u32 isLayout21 = avanim_layout_is_0x21(*cell);
                    unsigned char bit0 = 0, bit1 = 0;
                    if (isLayout21 != 0) {
                        bit0 = entdesc_flag_bit0_key(*cell, buf);
                        if (bit0 == 0) { bit1 = entdesc_flag_bit1_key(*cell, buf); }
                    }
                    if (isLayout21 == 0 || (bit0 == 0 && bit1 == 0)) {
                        *(unsigned char*)(out + 5) = 1;
                        if (((&DAT_80477c80)[cellType * 0x31] & 0x800) != 0) {
                            lockCount = (char)(lockCount + *(char*)(out + 5));
                        }
                        out[6] = FUN_80342e08();
                        if (((&DAT_80477c80)[cellType * 0x31] & 0xc0) != 0) {
                            int j = 0;
                            for (int k = 0; k < *(int*)(DAT_80612be4 + 0x81a4); k = k + 1) {
                                if ((int)*(short*)(DAT_80612be4 + j + 0x81b4) == cell[1]) {
                                    *(u32*)(DAT_80612be4 + j + 0x81b8) = out[6];
                                }
                                j = j + 0xb4;
                            }
                        }
                    } else {
                        int posBuf = cell[3];
                        short posShort = *(short*)(cell + 4);
                        (void)posShort;
                        unsigned char placed = FUN_80342bc8(member, &posBuf, (int)(out + 6));
                        *(unsigned char*)(out + 5) = placed;
                        lockCount = (char)(lockCount + *(char*)(out + 5));
                    }
                    *(short*)(out + 4) = (short)cell[2];
                    *(short*)((int)out + 0x12) = (short)cell[1];

                    int t = *cell;
                    if (t == 0x35 || t == 0x36 || t == 0x37) {
                        *out = cell[6];
                        out[1] = cell[7];
                        *(short*)(out + 2) = (short)cell[8];
                        *(short*)((int)out + 10) = (short)cell[0xb];
                        *(unsigned char*)(out + 3) = (unsigned char)((cell[9] << 4) | (cell[10] & 0xf));
                        *(unsigned char*)((int)out + 0xd) = *(unsigned char*)(cell + 0xe);
                    } else if (((u32)(t - 0x32) < 2) || (t == 0x34)) {
                        *out = cell[6];
                        out[1] = cell[7];
                        *(short*)(out + 2) = (short)cell[8];
                    } else if ((t < 10) || (0xe < t)) {
                        if (t == 5 || t == 6) {
                            *out = cell[6];
                            u32 m = aveditor_min_linked_index(*out, 3);
                            out[1] = (u32)(m != 0);
                        } else if (t == 7 || t == 8) {
                            *out = cell[6];
                            u32 m = aveditor_min_linked_index(*out, 7);
                            out[1] = (u32)(m != 0);
                            if (FUN_80328888() != 0) {
                                int prop = FUN_8032a670();
                                *(short*)(out + 2) = (short)*(u32*)(prop + 0x58);
                            }
                        } else {
                            int cls = (int)(short)(&DAT_80477bca)[cellType * 0x62];
                            if (cls == 8 || cls == 9 || ((u32)(cls - 0x13) & 0xffff) < 2 || cls == 0x15) {
                                *out = cell[6];
                                out[1] = cell[7];
                                *(short*)((int)out + 10) = (short)FUN_803395a4((int)cell);
                            } else if (cls == 0x1f) {
                                *(short*)((int)out + 10) = (short)FUN_803395a4((int)cell);
                            } else if (t == 0x43) {
                                *out = ((cell[8] & 0xf) << 4) | (cell[9] & 0xf);
                                out[1] = cell[10];
                                *(short*)(out + 2) = (short)cell[0xb];
                                *(unsigned short*)((int)out + 10) =
                                    (unsigned short)(((cell[7] << 4) & 0xf0) | (cell[6] & 0xf));
                            } else {
                                *(short*)((int)out + 10) = 0;
                                *(short*)(out + 2) = 0;
                                out[1] = 0;
                                *out = 0;
                            }
                        }
                    } else {
                        *out = cell[6];
                        out[1] = cell[7];
                        *(short*)(out + 2) = (short)cell[8];
                    }
                    off = off + (u32)*(unsigned char*)(out + 5) * 4 + 0x18;
                }
            }
            DAT_80612bd8 = DAT_80612bd8 + MAP_BUILD_CONNECTIVITY();
        }
    }

    // roster chunk (0xc7766b)
    {
        int j = 0;
        for (int i = 0; i < (int)(unsigned int)*(unsigned short*)(DAT_80612be4 + 0x306e); i = i + 1) {
            off = aveditor_write_record_pair((int)buf, off, 0xc7766b, 0x20);
            char* r = (char*)((int)buf + off);
            r[0] = (char)*(short*)(DAT_80612be4 + j + 0x3070);
            r[1] = (char)*(short*)(DAT_80612be4 + j + 0x3074);
            r[2] = (char)*(short*)(DAT_80612be4 + j + 0x3072);
            r[3] = *(char*)(DAT_80612be4 + j + 0x3076);
            r[4] = *(char*)(DAT_80612be4 + j + 0x3077);
            r[5] = *(char*)(DAT_80612be4 + j + 0x3078);
            *(short*)(r + 8) = *(short*)(DAT_80612be4 + j + 0x307a);
            r[6] = *(char*)(DAT_80612be4 + j + 0x3079);
            r[0xc] = 0; r[0xd] = 0; r[0xe] = 0; r[0xf] = 1;

            double bias = DOUBLE_8061a5e0;
            double dz = (double)(CONCAT44(0x43300000, (u32)(int)r[0]) ^ 0x80000000ull);
            double dy = (double)(CONCAT44(0x43300000, (u32)(int)r[2]) ^ 0x80000000ull);
            double dx = (double)(CONCAT44(0x43300000, (u32)(int)r[1]) ^ 0x80000000ull);
            int made = FUN_80313af4((double)(float)(dz - bias), (double)(float)(dy - bias),
                                    (double)(float)(dx - bias));
            if (made != 0) {
                unsigned char kind = (unsigned char)r[3];
                if ((char)kind < '\v') {
                    u32 mode = ((kind & 1) != 0) ? 1u : 4u;
                    char cy = r[1], cz = r[2], cx = r[0];
                    double px = (double)(CONCAT44(0x43300000, (u32)(int)cx) ^ 0x80000000ull);
                    double py = (double)CONCAT44(0x43300000, (u32)((int)cz ^ 0x80000000));
                    double pz = (double)CONCAT44(0x43300000, (u32)((int)cy ^ 0x80000000));
                    u32 rid = FUN_80313af4((double)(float)(px - DOUBLE_8061a5e0),
                                           (double)(float)(py - DOUBLE_8061a5e0),
                                           (double)(float)(pz - DOUBLE_8061a5e0));
                    FUN_80344710((double)(float)(px - DOUBLE_8061a5e0),
                                 (double)(float)(py - DOUBLE_8061a5e0),
                                 (double)(float)(pz - DOUBLE_8061a5e0),
                                 rid, mode, (unsigned short*)(r + 0x10));
                } else {
                    char cy = r[1], cz = r[2], cx = r[0];
                    double px = (double)CONCAT44(0x43300000, (u32)((int)cx ^ 0x80000000));
                    double py = (double)CONCAT44(0x43300000, (u32)((int)cz ^ 0x80000000));
                    double pz = (double)CONCAT44(0x43300000, (u32)((int)cy ^ 0x80000000));
                    u32 rid = FUN_80313af4((double)(float)(px - DOUBLE_8061a5e0),
                                           (double)(float)(py - DOUBLE_8061a5e0),
                                           (double)(float)(pz - DOUBLE_8061a5e0));
                    FUN_80344710((double)(float)(px - DOUBLE_8061a5e0),
                                 (double)(float)(py - DOUBLE_8061a5e0),
                                 (double)(float)(pz - DOUBLE_8061a5e0),
                                 rid, (int)(char)kind, (unsigned short*)(r + 0x10));
                }
                if (FUN_8034545c((unsigned short*)(r + 0x10)) == 0) {
                    FUN_80345484((int)(r + 0x10));
                }
            }
            off = off + 0x20;
            j = j + 0xc;
        }
    }

    // path graphs (0xc7766c)
    if (0 < *(int*)(DAT_80612be4 + 0x280)) {
        int j = 0;
        for (int i = 0; i < *(int*)(DAT_80612be4 + 0x280); i = i + 1) {
            int* pnode = (int*)(DAT_80612be4 + j + 0x288);
            unsigned int sz = (unsigned int)(pnode[0x16] * 4 + 0x53) & 0xfffffff0;
            pnode[0x10] = FUN_80341e64();
            off = aveditor_write_record_pair((int)buf, off, 0xc7766c, sz);
            int* out = (int*)((int)buf + off);
            FUN_8038b510(out, pnode);
            *(short*)((int)out + 0x22) = (short)i;
            int w = 0, cnt = 0;
            *(short*)(out + 9) = (short)pnode[10];
            *(short*)((int)out + 0x26) = (short)pnode[0xb];
            *(char*)(out + 10) = (char)pnode[0xc];
            *(char*)(out + 8) = (char)pnode[0xd];
            *(short*)((int)out + 0x2a) = (short)(pnode[0x12] - 1);
            out[0xe] = pnode[0xe];
            out[0xf] = pnode[0xf];
            *(short*)(out + 0x10) = (short)pnode[0x10];
            out[0xd] = pnode[0x15];
            *(short*)(out + 0xb) = (short)pnode[0x16];
            *(short*)((int)out + 0x2e) = (short)pnode[0x17];
            *(char*)(out + 0xc) = (char)pnode[0x13];
            *(char*)((int)out + 0x31) = (char)pnode[0x11];
            *(short*)((int)out + 0x42) = (short)pnode[0x18];
            for (int k = 0; k < pnode[0x16]; k = k + 1) {
                int id = FUN_80342e08();
                if (id == -1) {
                    cnt = cnt - 1;
                    *(short*)(out + 0xb) = *(short*)(out + 0xb) - 1;
                } else {
                    *(short*)(out + cnt + 0x11) = (short)id;
                    int mi = aveditor_member_list_index(*(int*)((int)pnode + w + 100));
                    *(char*)((int)out + cnt * 4 + 0x46) = (char)mi;
                    *(char*)((int)out + cnt * 4 + 0x47) = (char)*(u32*)((int)pnode + w + 0x70);
                }
                w = w + 0x10;
                cnt = cnt + 1;
            }
            off = off + sz;
            j = j + 0xe4;
        }
    }

    // soundpad table (0xc7766d)
    if (0 < *(int*)(DAT_80612be4 + 0x81a4)) {
        int j = 0;
        for (int i = 0; i < *(int*)(DAT_80612be4 + 0x81a4); i = i + 1) {
            int ctx = DAT_80612be4;
            off = aveditor_write_record_pair((int)buf, off, 0xc7766d, 0xc0);
            leaf_31e8((int)buf + off, ctx + j + 0x81a8, 0xc0);
            off = off + 0xc0;
            j = j + 0xb4;
        }
    }

    // placement table (0xc7766e)
    if (0 < *(int*)(DAT_80612be4 + 0x41e0)) {
        int j = 0;
        for (int i = 0; i < *(int*)(DAT_80612be4 + 0x41e0); i = i + 1) {
            off = aveditor_write_record_pair((int)buf, off, 0xc7766e, 0x220);
            leaf_31e8((int)buf + off, DAT_80612be4 + j + 0x41e4, 0x220);
            off = off + 0x220;
            j = j + 0x220;
        }
    }

    // mode-3 path/objective rows (0xc7766f)
    if (0 < *(int*)(DAT_80612be4 + 0xdd10)) {
        int j = 0;
        int localSlots[10];
        for (int i = 0; i < *(int*)(DAT_80612be4 + 0xdd10); i = i + 1) {
            int rowBase = DAT_80612be4 + j;
            int kind = *(int*)(rowBase + 0xdd94);
            if (kind == 3) {
                int w = 0, w2 = 0;
                for (int k = 0; k < *(int*)(j + DAT_80612be4 + 0xdd34); k = k + 1) {
                    FUN_80336690(DAT_80612be4 + j + 0xdd14, k);
                    *(u32*)((int)localSlots + w) = *(u32*)(j + DAT_80612be4 + w2 + 0xdd38);
                    *(unsigned short*)(j + DAT_80612be4 + w2 + 0xdd3c) = (unsigned short)FUN_80342e08();
                    int mi = aveditor_member_list_index(*(int*)(j + DAT_80612be4 + w2 + 0xdd38));
                    w = w + 4;
                    int base = DAT_80612be4 + w2;
                    w2 = w2 + 8;
                    *(int*)(j + base + 0xdd38) = mi;
                }
                off = aveditor_write_record_pair((int)buf, off, 0xc7766f, 0x90);
                leaf_31e8((int)buf + off, DAT_80612be4 + j + 0xdd14, 0x90);
                int r = 0, w3 = 0;
                for (int k = 0; k < *(int*)(j + DAT_80612be4 + 0xdd34); k = k + 1) {
                    int base = r + 0xdd38;
                    r = r + 8;
                    *(u32*)(j + DAT_80612be4 + base) = *(u32*)((int)localSlots + w3);
                    w3 = w3 + 4;
                }
            } else if (kind == 10) {
                int w = 0;
                for (int k = 0; k < *(int*)(j + DAT_80612be4 + 0xdd84); k = k + 1) {
                    int mi = aveditor_member_list_index(*(int*)(j + DAT_80612be4 + w + 0xdd34));
                    int base = DAT_80612be4 + w;
                    w = w + 8;
                    *(int*)(j + base + 0xdd38) = mi + 1;
                }
                off = aveditor_write_record_pair((int)buf, off, 0xc7766f, 0x90);
                leaf_31e8((int)buf + off, DAT_80612be4 + j + 0xdd14, 0x90);
            } else if (kind == 0xd) {
                int desc = FUN_80338b88(i);
                if (*(int*)(desc + 8) == 7) {
                    *(u32*)(rowBase + 0xdd34) = FUN_80314358(*(int*)(rowBase + 0xdd98));
                }
                off = aveditor_write_record_pair((int)buf, off, 0xc7766f, 0x90);
                leaf_31e8((int)buf + off, DAT_80612be4 + j + 0xdd14, 0x90);
            } else {
                off = aveditor_write_record_pair((int)buf, off, 0xc7766f, 0x90);
                leaf_31e8((int)buf + off, DAT_80612be4 + j + 0xdd14, 0x90);
            }
            off = off + 0x90;
            j = j + 0x8c;
        }
    }

    // soundpad enable bits (0xc77670)
    unsigned char padBits[8];
    memset_wrapper((int)padBits, 0, 8);
    {
        int i = 0, ctxOff = 0, bytePos = 0;
        do {
            unsigned char* pb = padBits + bytePos;
            int inner = 0, io = 0;
            do {
                *pb = FUN_8032b070(DAT_80612be4 + ctxOff + io + 0x113c4);
                inner = inner + 1;
                pb = pb + 1;
                io = io + 0x18;
            } while (inner < 4);
            i = i + 1;
            ctxOff = ctxOff + 0x60;
            bytePos = bytePos + 4;
        } while (i < 2);
    }
    off = aveditor_write_record_pair((int)buf, off, 0xc77670, 8);
    leaf_31e8((int)buf + off, (u32)padBits, 8);

    // cursor snapshot (0xc77671) + trailer (0xc7766a)
    aveditor_snapshot_cursor_state(&DAT_805d5fa4);
    off = aveditor_write_record_pair((int)buf, off + 8, 0xc77671, 0x10);
    leaf_31e8((int)buf + off, (u32)&DAT_805d5fa4, 0x10);
    off = aveditor_write_record_pair((int)buf, off + 0x10, 0xc7766a, off + 0x10);
    aveditor_set_record_capacity(off, buf);
    aveditor_set_entdesc_flag_byte9((unsigned char)lockCount, buf);

    *(u32*)(DAT_80612be4 + 0x264) = savedMode;
    FUN_8032a830();
    FUN_8032a84c();
    FUN_8038270c();
}

// ---------------------------------------------------------------------------
// 0x80310f4c  aveditor_restore_cursor_state
// Chunk 0xc77671 handler. mode 0 restores the 5 cursor fields; else copies the
// challenge/stats record (3 u64 pairs) from the stats block.
// ---------------------------------------------------------------------------
void aveditor_restore_cursor_state(unsigned char* src, unsigned int* dst, int mode)
{
    if (mode == 0) {
        dst[0] = (unsigned int)src[0];
        dst[1] = *(unsigned int*)(src + 4);
        dst[2] = *(unsigned int*)(src + 8);
        dst[3] = (unsigned int)src[0xc];
        dst[4] = (unsigned int)src[0xd];
    } else {
        unsigned char* rec = (unsigned char*)stats_get_challenge_record(0, DAT_805015d8);
        unsigned int* d = dst - 1;
        unsigned int* s = (unsigned int*)(rec - 4);
        int n = 3;
        do {
            unsigned int* lo = s + 1;
            s = s + 2;
            unsigned int hi = *s;
            d[1] = *lo;
            d = d + 2;
            *d = hi;
            n = n - 1;
        } while (n != 0);
    }
}

// ---------------------------------------------------------------------------
// 0x80310fe0  aveditor_set_empty_map_flag
// ---------------------------------------------------------------------------
void aveditor_set_empty_map_flag(int a, int b)
{
    if (a == 0 && b == 0) {
        *(u32*)(DAT_80612be4 + 0x25c) = *(u32*)(DAT_80612be4 + 0x25c) | 0x800;
        return;
    }
    *(u32*)(DAT_80612be4 + 0x25c) = *(u32*)(DAT_80612be4 + 0x25c) & 0xfffff7ff;
}

// ---------------------------------------------------------------------------
// 0x80311018  aveditor_recompute_height_range
// ---------------------------------------------------------------------------
void aveditor_recompute_height_range(void)
{
    double bias = DOUBLE_8061a5e0;
    int member = DAT_80612c14 != nullptr ? (int)DAT_80612c14 : 0;
    if (member == 0) {
        *(u32*)(DAT_80612be4 + 0x264) = 0;
    } else {
        *(u32*)(DAT_80612be4 + 0x264) = 3;
        for (; member != 0; member = *(int*)(member + 0x40)) {
            double cur = (double)(CONCAT44(0x43300000, *(u32*)(DAT_80612be4 + 0x264)) ^ 0x80000000ull);
            if (*(float*)(member + 8) < (float)(cur - bias)) {
                *(int*)(DAT_80612be4 + 0x264) = (int)*(float*)(member + 8);
            }
        }
        if (*(int*)(DAT_80612be4 + 0x264) < -2) {
            *(u32*)(DAT_80612be4 + 0x264) = 0xfffffffe;
        }
    }
    int derived = -2;
    if (-2 < *(int*)(DAT_80612be4 + 0x264)) {
        derived = *(int*)(DAT_80612be4 + 0x264) - 1;
    }
    *(int*)(DAT_80612be4 + 0x268) = derived;
}

// ---------------------------------------------------------------------------
// 0x803110d4  aveditor_deserialize_map
// Rebuild the editor map from a serialized buffer, dispatching on the 0xc776xx
// chunk tag. Inverse of aveditor_serialize_map.
// ---------------------------------------------------------------------------
void aveditor_deserialize_map(void)
{
    u64 pair = FUN_803826bc();
    int* buf = (int*)(u32)(pair >> 0x20);
    u32 restoreMode = (u32)(pair >> 1) & 1;
    u32 savedHeight = *(u32*)(DAT_80612be4 + 0x264);
    u32 savedSubmode = *(u32*)(DAT_80612be4 + 0x254);
    int memberCount = -1;
    int ctx = DAT_80612be4;

    FUN_80339c00(buf);
    aveditor_set_empty_map_flag((u32)pair & 1, restoreMode);
    *(short*)(DAT_80612be4 + 0x306a) = 0;
    *(short*)(DAT_80612be4 + 0x3068) = 0;
    *(short*)(DAT_80612be4 + 0x306c) = 0;
    *(short*)(DAT_80612be4 + 0x306e) = 0;
    asel_reset_screen(0);

    if (((u32)pair & 1) == 0) {
        *(u32*)(DAT_80612be4 + 0x254) = 1;
    } else {
        *(u32*)(DAT_80612be4 + 0x264) = savedHeight;
        *(u32*)(DAT_80612be4 + 0x254) = savedSubmode;
        int derived = -2;
        if (-2 < *(int*)(DAT_80612be4 + 0x264)) {
            derived = *(int*)(DAT_80612be4 + 0x264) - 1;
        }
        *(int*)(DAT_80612be4 + 0x268) = derived;
    }

    u32 needSize = (u32)FUN_80337c9c();
    if ((*buf == 0xc77667) && (buf[1] == 0x12d)) {
        buf = buf + 2;
        (void)ctx;
        int loadBuf = mem_alloc(needSize, 0x1000);

        while (true) {
            u32* payload = (u32*)(buf + 2);
            if (*buf == 0xc7766a) break;
            switch (*buf) {
            case 0xc77668: {
                DAT_805d59ac = (u32)*(unsigned char*)(buf + 5);
                u32 cfg = (u32)*(unsigned short*)((int)buf + 0x12);
                if (DAT_805d59a0 != cfg) {
                    DAT_805d59a0 = cfg;
                    DAT_805d59ac = (u32)FUN_8031d614(cfg);
                    *(u32*)(DAT_80612be4 + 0x238) = *(u32*)(DAT_80612be4 + 0x238) | 0x12;
                }
                *(int*)(DAT_80612be4 + 0x210) = (int)*(short*)((int)buf + 0x16);
                *(int*)(DAT_80612be4 + 0x220) = (int)*(short*)(buf + 6);
                *(u32*)(DAT_80612be4 + 0x224) = (u32)*(unsigned char*)((int)buf + 0x1a);
                *(u32*)(DAT_80612be4 + 0x200) = 0;
                *(u32*)(DAT_80612be4 + 0x214) = 0;
                DAT_805d59a8 = (int)*(short*)(buf + 0x12);
                DAT_805d59a4 = (int)*(short*)(buf + 7);
                *(u32*)(DAT_80612be4 + 0x204) = DAT_805d59a0;
                *(u32*)(DAT_80612be4 + 0x208) = FUN_8031d6e8();
                if (buf[0x90] != 0) {
                    int auxCount = FUN_8031d708();
                    u32* auxTable = (u32*)FUN_8031d710();
                    if (1 < auxCount) {
                        int bit = 1, idx = 4, n = auxCount - 1;
                        do {
                            if ((buf[0x90] & (1 << bit)) != 0) {
                                *(u32*)((int)auxTable + idx) = 1;
                            }
                            bit = bit + 1;
                            idx = idx + 4;
                            n = n - 1;
                        } while (n != 0);
                    }
                    if ((buf[0x90] & 1) != 0) { *(u32*)(DAT_80612be4 + 0x200) = 1; }
                    if ((buf[0x90] & 0x8000) != 0) { *(u32*)(DAT_80612be4 + 0x214) = 1; }
                    *(u32*)(DAT_80612be4 + 0x218) = (u32)buf[0x90] >> 0x10 & 1;
                    *(u32*)(DAT_80612be4 + 0x21c) = (u32)buf[0x90] >> 0x11 & 1;
                }
                int d = 0, s2 = 0;
                *(u32*)(DAT_80612be4 + 0x11860) = (u32)*(unsigned char*)((int)buf + 0x1e);
                for (int n = 10; n != 0; n = n - 1) {
                    *(short*)(DAT_80612be4 + d + 0x11864) = *(short*)((int)payload + s2 + 0x18);
                    *(short*)(DAT_80612be4 + d + 0x1186a) = (short)*(char*)((int)payload + s2 + 0x1b);
                    *(unsigned char*)(DAT_80612be4 + d + 0x11867) = *(unsigned char*)((int)payload + s2 + 0x1a);
                    d = d + 8;
                    s2 = s2 + 4;
                }
                int d2 = 0, s3 = 0;
                *(u32*)(DAT_80612be4 + 0x11908) = (u32)*(unsigned char*)((int)buf + 0x4a);
                for (int k = 0; k < *(int*)(DAT_80612be4 + 0x11908); k = k + 1) {
                    int a = s3 + 0x44;
                    s3 = s3 + 2;
                    *(int*)(DAT_80612be4 + d2 + 0x1190c) = (int)*(short*)((int)payload + a);
                    d2 = d2 + 4;
                }
                *(unsigned char*)((int)buf + 0x7d) = 0;
                *(unsigned char*)((int)buf + 0x15d) = 0;
                *(unsigned char*)((int)buf + 0x23d) = 0;
                FUN_8038b510((int*)&DAT_805d6340, (int*)((int)buf + 0x6a));
                FUN_8038b510((int*)&DAT_805d6260, (int*)((int)buf + 0x7e));
                FUN_8038b510((int*)&DAT_805d6180, (int*)((int)buf + 0x15e));
                memberCount = buf[3];
                *(int*)(DAT_80612be4 + 0x228) = (int)*(char*)((int)buf + 0x23e);
                int* cfgSrc = buf + 0x91;
                int* cfgDst = (int*)(DAT_80612be4 + 0x11484);
                for (int n = 0x32; n != 0; n = n - 1) {
                    int* lo = cfgSrc + 1;
                    cfgSrc = cfgSrc + 2;
                    int hi = *cfgSrc;
                    cfgDst[1] = *lo;
                    cfgDst = cfgDst + 2;
                    *cfgDst = hi;
                }
                break;
            }
            case 0xc77669: {
                int outOff = 0;
                u32* p = payload;
                double bias = DOUBLE_8061a5e0;
                for (int i = 0; i < memberCount; i = i + 1) {
                    double dx = (double)(CONCAT44(0x43300000, (u32)((int)*(char*)(p + 1) ^ 0x80000000)));
                    double dy = (double)(CONCAT44(0x43300000, (u32)((int)*(char*)((int)p + 5) ^ 0x80000000)));
                    int spawned = FUN_80313e94((double)(float)(dx - bias), (double)(float)(dy - bias),
                                               *p, (int)*(char*)((int)p + 6),
                                               (float)(int)*(char*)((int)p + 7), 0.0f);
                    *(char*)(spawned + 0x18) = FUN_8032582c((p[3] >> 1 & 0x7f7f7f00) + 0x80);
                    *(u32*)(spawned + 0x1c) = (u32)*(unsigned char*)((int)p + 0x22);
                    *(u32*)(spawned + 0x24) = p[6];
                    *(u32*)(spawned + 0x28) = p[5];
                    *(char*)(spawned + 0x19) = FUN_8032582c((p[4] >> 1 & 0x7f7f7f00) + 0x80);
                    *(u32*)(spawned + 0x44) = (u32)*(unsigned short*)(p + 7);
                    FUN_80313da0();
                    *(int*)(loadBuf + outOff) = spawned;

                    int cellsA = FUN_80343c4c(*p);
                    int cellsB = COUNT_ACTIVE_TYPE_CELLS();
                    u32* cellPtr = p + (cellsB + cellsA) * 2 + 10;
                    if (*(short*)((int)p + 0x1e) != 0) {
                        for (int c = 0; c < (int)(u32)*(unsigned short*)((int)p + 0x1e); c = c + 1) {
                            u32 fields[8];
                            int type = (int)*(short*)((int)cellPtr + 0xe);
                            fields[1] = (u32)*(unsigned short*)(cellPtr + 4);            // = old local_1424
                            u32 packed = (u32)CONCAT12(*(unsigned char*)((int)cellPtr + 0x15),
                                                       (unsigned short)*(unsigned char*)((int)cellPtr + 0x16));
                            int posB = (int)*(short*)((int)cellPtr + 0x12);
                            unsigned short posC = (unsigned short)*(unsigned char*)((int)cellPtr + 0x17);
                            u32 f0 = 0, f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0, f6 = 0;
                            unsigned char fb = 0;
                            if (((u32)(type - 0x35) < 2) || (type == 0x37)) {
                                f0 = *cellPtr; f1 = cellPtr[1];
                                f2 = (u32)*(short*)(cellPtr + 2);
                                f6 = (u32)*(unsigned short*)((int)cellPtr + 10);
                                fb = *(unsigned char*)((int)cellPtr + 0xd);
                                f3 = (u32)(*(unsigned char*)(cellPtr + 3) >> 4);
                                f4 = *(unsigned char*)(cellPtr + 3) & 0xf;
                            } else if (((u32)(type - 0x32) < 2) || (type == 0x34)) {
                                f0 = *cellPtr; f1 = cellPtr[1];
                                f2 = (u32)*(short*)(cellPtr + 2);
                            } else if ((type < 10) || (0xe < type)) {
                                if (type == 5 || type == 6) {
                                    f0 = *cellPtr;
                                } else if (type == 7 || type == 8) {
                                    f0 = *cellPtr;
                                } else {
                                    int cls = (int)(short)(&DAT_80477bca)[type * 0x62];
                                    if (cls == 8 || cls == 9 || ((u32)(cls - 0x13) & 0xffff) < 2 || cls == 0x15) {
                                        f0 = *cellPtr; f1 = cellPtr[1];
                                    } else if (type == 0x43) {
                                        f2 = *cellPtr >> 4 & 0xf;
                                        f3 = *cellPtr & 0xf;
                                        f0 = *(unsigned short*)((int)cellPtr + 10) & 0xf;
                                        f1 = *(unsigned short*)((int)cellPtr + 10) >> 4 & 0xf;
                                        f4 = cellPtr[1];
                                        f5 = (u32)*(short*)(cellPtr + 2);
                                    }
                                }
                            } else {
                                f0 = *cellPtr; f1 = cellPtr[1];
                                f2 = (u32)*(short*)(cellPtr + 2);
                            }
                            (void)type; (void)fields; (void)posB; (void)posC; (void)packed;
                            (void)f0; (void)f1; (void)f2; (void)f3; (void)f4; (void)f5; (void)f6; (void)fb;
                            u32 posPair = packed;
                            u32 posMeta = (u32)posB;
                            FUN_80314148(spawned, &posPair, posMeta);
                            cellPtr = cellPtr + *(unsigned char*)(cellPtr + 5) + 6;
                        }
                    }
                    outOff = outOff + 4;
                    p = cellPtr;
                }
                break;
            }
            case 0xc7766b: {
                int slot = (u32)*(unsigned short*)(DAT_80612be4 + 0x306e) * 0xc;
                *(short*)(DAT_80612be4 + slot + 0x3070) = (short)*(char*)payload;
                *(short*)(DAT_80612be4 + slot + 0x3072) = (short)*(char*)((int)buf + 10);
                *(short*)(DAT_80612be4 + slot + 0x3074) = (short)*(char*)((int)buf + 9);
                *(unsigned char*)(DAT_80612be4 + slot + 0x3076) = *(unsigned char*)((int)buf + 0xb);
                *(unsigned char*)(DAT_80612be4 + slot + 0x3077) = *(unsigned char*)(buf + 3);
                *(short*)(DAT_80612be4 + slot + 0x307a) = *(short*)(buf + 4);
                *(unsigned char*)(DAT_80612be4 + slot + 0x3078) = *(unsigned char*)((int)buf + 0xd);
                *(unsigned char*)(DAT_80612be4 + slot + 0x3079) = *(unsigned char*)((int)buf + 0xe);
                unsigned char team = *(unsigned char*)(DAT_80612be4 + slot + 0x3077);
                if (team == 1)      { *(short*)(DAT_80612be4 + 0x306a) += 1; }
                else if (team == 0) { *(short*)(DAT_80612be4 + 0x3068) += 1; }
                else if (team < 3)  { *(short*)(DAT_80612be4 + 0x306c) += 1; }
                *(short*)(DAT_80612be4 + 0x306e) += 1;
                break;
            }
            case 0xc7766c: {
                int w = 0;
                u32* out = (u32*)(DAT_80612be4 + *(int*)(DAT_80612be4 + 0x280) * 0xe4 + 0x2ec);
                for (int k = 0; k < (int)(u32)*(unsigned short*)(buf + 0xd); k = k + 1) {
                    *out = *(u32*)(loadBuf + (u32)*(unsigned char*)((int)payload + w + 0x46) * 4);
                    FUN_803430b8(*out, out + 1, (u32)*(unsigned short*)((int)payload + w + 0x44));
                    out[3] = (u32)*(unsigned char*)((int)payload + w + 0x47);
                    w = w + 4;
                    out = out + 4;
                }
                int rowBase = DAT_80612be4 + *(int*)(DAT_80612be4 + 0x280) * 0xe4;
                *(u32*)(rowBase + 0x2e0) = (u32)*(unsigned short*)(buf + 0xd);
                *(u32*)(rowBase + 0x2e4) = (u32)*(unsigned short*)((int)buf + 0x36);
                FUN_8038b510((int*)(rowBase + 0x288), (int*)payload);
                *(int*)(rowBase + 0x2ac) = (int)*(short*)((int)buf + 0x2a);
                *(int*)(rowBase + 0x2c0) = buf[0x10];
                *(int*)(rowBase + 0x2c4) = buf[0x11];
                *(u32*)(rowBase + 0x2c8) = (u32)*(unsigned short*)(buf + 0x12);
                *(u32*)(rowBase + 0x2b0) = (u32)*(unsigned short*)(buf + 0xb);
                *(int*)(rowBase + 0x2b4) = (int)*(short*)((int)buf + 0x2e);
                *(u32*)(rowBase + 0x2b8) = (u32)*(unsigned char*)(buf + 0xc);
                *(int*)(rowBase + 700) = (int)*(char*)(buf + 10);
                *(int*)(rowBase + 0x2d0) = *(short*)((int)buf + 0x32) + 1;
                *(int*)(rowBase + 0x2dc) = buf[0xf];
                *(u32*)(rowBase + 0x2d4) = (u32)*(unsigned char*)(buf + 0xe);
                *(u32*)(rowBase + 0x2cc) = (u32)*(unsigned char*)((int)buf + 0x39);
                *(int*)(DAT_80612be4 + 0x280) = *(int*)(DAT_80612be4 + 0x280) + 1;
                break;
            }
            case 0xc7766d:
                leaf_31e8(DAT_80612be4 + *(int*)(DAT_80612be4 + 0x81a4) * 0xb4 + 0x81a8, (u32)payload, 0xb4);
                *(int*)(DAT_80612be4 + 0x81a4) = *(int*)(DAT_80612be4 + 0x81a4) + 1;
                break;
            case 0xc7766e: {
                leaf_31e8(DAT_80612be4 + *(int*)(DAT_80612be4 + 0x41e0) * 0x220 + 0x41e4, (u32)payload, 0x220);
                *(int*)(DAT_80612be4 + 0x41e0) = *(int*)(DAT_80612be4 + 0x41e0) + 1;
                u32 z1 = countLeadingZeros(1 - buf[0x89]);
                *(u32*)(DAT_80612be4 + 0x41d8) = *(int*)(DAT_80612be4 + 0x41d8) + (z1 >> 5);
                u32 z2 = countLeadingZeros(1 - (u32)*(unsigned char*)(buf + 3));
                *(char*)(DAT_80612be4 + 0x41dc) = (char)(*(char*)(DAT_80612be4 + 0x41dc) + (char)(z2 >> 5));
                break;
            }
            case 0xc7766f: {
                leaf_31e8(DAT_80612be4 + *(int*)(DAT_80612be4 + 0xdd10) * 0x8c + 0xdd14, (u32)payload, 0x8c);
                int row = DAT_80612be4 + *(int*)(DAT_80612be4 + 0xdd10) * 0x8c;
                if (*(int*)(row + 0xdd94) == 3) {
                    int w = 0;
                    for (int k = 0; ; k = k + 1) {
                        int r = DAT_80612be4 + *(int*)(DAT_80612be4 + 0xdd10) * 0x8c;
                        if (k >= *(int*)(r + 0xdd34)) break;
                        int base = w + 0xdd38;
                        w = w + 8;
                        *(u32*)(r + base) = *(u32*)(loadBuf + *(int*)(r + base) * 4);
                    }
                } else if (*(int*)(row + 0xdd94) == 10) {
                    int w = 0;
                    for (int k = 0; k < *(int*)(row + 0xdd84); k = k + 1) {
                        int e = row + 0xdd14 + w;
                        w = w + 8;
                        *(u32*)(e + 0x20) = *(u32*)(loadBuf + *(int*)(e + 0x24) * 4 - 4);
                    }
                }
                *(int*)(DAT_80612be4 + 0xdd10) = *(int*)(DAT_80612be4 + 0xdd10) + 1;
                break;
            }
            case 0xc77670: {
                int i = 0, ctxOff = 0, bytePos = 0;
                do {
                    unsigned char* pb = (unsigned char*)((int)payload + bytePos);
                    int inner = 0, io = 0;
                    do {
                        FUN_8032b0d8(DAT_80612be4 + ctxOff + io + 0x113c4, (u32)*pb);
                        inner = inner + 1;
                        io = io + 0x18;
                        pb = pb + 1;
                    } while (inner < 4);
                    i = i + 1;
                    bytePos = bytePos + 4;
                    ctxOff = ctxOff + 0x60;
                } while (i < 2);
                break;
            }
            case 0xc77671:
                aveditor_restore_cursor_state((unsigned char*)payload, (u32*)(DAT_80612be4 + 0x1183c), restoreMode);
                break;
            case 0xc77672:
                leaf_31e8((u32)&DAT_805d6630, (u32)payload, 0x100);
                break;
            case 0xc77673: {
                int len = buf[1];
                u32 dst = FUN_80336778();
                leaf_31e8(dst, (u32)payload, len);
                dst = FUN_80336778();
                memset_wrapper((int)(dst + len), 0, 0x4b00 - len);
                break;
            }
            case 0xc77674: {
                unsigned char scratch10[2000];
                memset_wrapper((int)scratch10, 0, 2000);
                leaf_31e8((u32)scratch10, (u32)payload, buf[1]);
                FUN_8033646c((int)scratch10, (u32)buf[1] / 0x14);
                break;
            }
            case 0xc77675: {
                unsigned char scratch3[3000];
                memset_wrapper((int)scratch3, 0, 3000);
                leaf_31e8((u32)scratch3, (u32)payload, buf[1]);
                FUN_803366ac((int)scratch3, buf[1]);
                break;
            }
            default:
                break;
            }
            buf = (int*)((int)payload + buf[1]);
        }

        if (((u32)pair & 1) == 0) {
            aveditor_recompute_height_range();
        }
        mem_free_top(loadBuf);
        aveditor_flag_dirty_if_pending();
        FUN_803365b8();
        FUN_80336548();
        FUN_8032c194();
    } else {
        asel_reset_screen(0);
    }
    FUN_80382708();
}

// ---------------------------------------------------------------------------
// 0x80311e68  aveditor_flag_dirty_if_pending
// ---------------------------------------------------------------------------
void aveditor_flag_dirty_if_pending(void)
{
    if (FUN_80328888() != 0) {
        *(u32*)(DAT_80612be4 + 0x238) = *(u32*)(DAT_80612be4 + 0x238) | 1;
    }
}

// ---------------------------------------------------------------------------
// 0x80311ea0  aveditor_load_thunk
// ---------------------------------------------------------------------------
void aveditor_load_thunk(void)
{
    FUN_80311f98();
}
