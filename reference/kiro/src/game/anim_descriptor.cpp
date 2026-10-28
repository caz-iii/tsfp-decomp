// =============================================================================
// anim_descriptor.cpp - Animation descriptor load/registry + skeleton concat
// Address range: 0x80022134 - 0x80022d5c
// Batch: 388 | Date: 2026-09-05
//
// The animation-descriptor loading/registration layer + the recursive skeleton
// bone-matrix concatenation used by the pose pipeline (model_render.cpp batch_385,
// model_anim_eval.cpp batch_387).
//
//   * anim_load_animtags        (0x80022134) load <anim>.animtags.dat, parse event/tag table
//   * anim_release_descriptors  (0x80022354) clear descriptor-table slots for an anim list
//   * anim_load_descriptor_list (0x80022398) load every descriptor referenced by a model
//   * anim_load_descriptor      (0x80022438) MASSIVE single-descriptor loader/parser/registrar
//   * anim_set_descriptor_flags (0x800229dc) store the per-anim flag words for an id
//   * anim_get_descriptor       (0x80022a08) id -> descriptor ptr accessor
//   * skel_concat_matrices_a    (0x80022a38) recursive bone-matrix concat (mode a; FUN_80022a38)
//   * skel_concat_matrices_b    (0x80022b1c) recursive bone-matrix concat (mode b; FUN_80022b1c)
//   * skel_concat_slerp         (0x80022c14) recursive bone-matrix slerp blend (FUN_80022c14)
//   * anim_keyframe_binary_search (0x80022d5c) binary-search the keyframe pair bracketing a time
//
// Compile-clean non-matching reconstruction. The two big loaders drop the wide
// float-ABI spill params and register-guessed locals that Ghidra surfaced; the
// mismatched named helpers (mem_alloc / asset_get_size / dvd_read_file_to_buffer
// / mtx4_slerp) are invoked through typed function-pointer casts at the observed
// call-site arity. Recursive skeleton walkers resolve the packed scratch frame at
// the public entry and recurse through a static core (no per-node re-fetch).
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // pack_get_asset_ptr, leaf_31e8, game_level_supports_scoring,
                               // mtx4_slerp, mem_alloc, asset_get_size, dvd_read_file_to_buffer

// ---- raw runtime globals ---------------------------------------------------
extern int    DAT_80611824;    // animation descriptor table base (idx*4)
extern short  DAT_804d1cd0;    // anim id -> descriptor index table (short)
extern int    DAT_804d0e20;    // anim id -> event/tag table ptr (idx*4)
extern int    DAT_804d34d8;    // per-anim flag table (word 0, idx*8)
extern int    DAT_804d34dc;    // per-anim flag table (word 1, idx*8)
extern char   DAT_804d5d00;    // animtags text buffer (0x2800)
extern char*  DAT_804d5bf8;    // per-descriptor ptr list
extern int    DAT_806117fc;    // animtags buffer valid length
extern int    DAT_80611820;    // event-table bump-alloc cursor
extern int    DAT_806117e0;    // event-table arena base
extern int    DAT_806117f0;    // descriptor-list count
extern int    DAT_806117ec;    // first-descriptor ptr cache
extern int    DAT_806117c0;    // descriptor-list-build enable flag
extern void*  DAT_806117c4;    // relocation/compression arena base (0 => none)
extern int    DAT_806117c8;    // relocation arena cursor
extern int    DAT_806117d8;    // descriptor-index allocation cursor
extern int    s_Not_animtags_dat_803f23e8;   // ".Not.animtags.dat" suffix literal

// ---- numeric constants -----------------------------------------------------
extern float  FLOAT_80613930;  // 0.0
extern double DOUBLE_80613938; // uint->double magic bias
extern float  FLOAT_8060faf8;  // fixed-point scale (keyframe times)
extern float  FLOAT_80613944;  // 1.0

// ---- raw runtime helpers (self-extern at call-site arity) ------------------
extern "C" {
    unsigned long long FUN_803826d8(void);   // scratch: packed (?<<32 | animId)
    unsigned long long FUN_803826e4(void);   // scratch: packed (recHi<<32 | listPtr)
    unsigned long long FUN_803826e8(void);   // scratch: packed (recHi<<32 | modelBase)
    unsigned long long FUN_803826cc(void);   // scratch: packed (namePtr<<32 | animId)
    unsigned long long FUN_803826f0(void);   // scratch: packed (recHi<<32 | modelBase)
    void   FUN_80382718(void);                // scratch free (cc)
    void   FUN_80382724(void);                // scratch free (d8)
    void   FUN_80382730(void);                // scratch free (e4)
    void   FUN_80382734(void);                // scratch free (e8)
    void   FUN_8038273c(void);                // scratch free (f0)

    int    FUN_8038b5c8(int str);                       // strlen
    int    FUN_8038b378(uint* a, uint* b);              // string compare/suffix test
    void   FUN_8038b510(int* dst, int* src);            // string copy/append
    int    FUN_8038b258(char* s, void* sep);            // strrchr-ish
    int    FUN_8038b338(int buf, int magic, int len);   // magic/header compare
    uint   FUN_8038c67c(char* s);                       // atoi
    int    FUN_80184bdc(void);                          // heap top / budget
    int    FUN_80184908(int size);                      // reloc arena alloc
    int    FUN_8018493c(int a, int b, uint c, void* d); // reloc copy
    int    FUN_801849e0(void);                          // reloc arena reset
    void   FUN_80022fb4(void);                           // post-descriptor hook
    void   FUN_803a1508(float* out, float* a, float* b); // matrix multiply
}
// anim_get_animtags_path is declared in generated_externs.h as int(...); it
// actually returns the path buffer pointer. text_find_line_match (batch 387) is
// declared void but its ABI returns the match position in r3; reach both through
// typed function-pointer casts.
typedef char* (*animtags_path_fn)(void);
static const animtags_path_fn __anim_get_animtags_path = (animtags_path_fn)&anim_get_animtags_path;
typedef char* (*find_line_fn)(int hay, int needle, int hayLen);
static const find_line_fn __text_find_line_match = (find_line_fn)&text_find_line_match;

// --- typed casts for named helpers whose shared proto != call-site ABI ------
// mem_alloc real proto (size,type); the loader call site passes (size, flag, ...)
typedef int  (*mem_alloc2_fn)(int size, int type);
static const mem_alloc2_fn __mem_alloc2 = (mem_alloc2_fn)&mem_alloc;
// asset_get_size real proto (const char*); call site passes the name buffer.
typedef int  (*asset_get_size1_fn)(void* name);
static const asset_get_size1_fn __asset_get_size1 = (asset_get_size1_fn)&asset_get_size;
// dvd_read_file_to_buffer real proto (path,buf,size); call site passes (path,buf,size).
typedef int  (*dvd_read3_fn)(void* path, void* buf, int size);
static const dvd_read3_fn __dvd_read3 = (dvd_read3_fn)&dvd_read_file_to_buffer;
// mtx4_slerp: call site is (int dstMtx, int aMtx, float* bMtx) — 3-arg form.
typedef void (*mtx4_slerp3_fn)(int dst, int a, float* b);
static const mtx4_slerp3_fn __mtx4_slerp3 = (mtx4_slerp3_fn)&mtx4_slerp;

// anim_set_descriptor_flags (0x800229dc)
// Store the two per-anim flag words (word0/word1) for an animation id into the
// parallel flag tables, keyed on the id's descriptor-table index.
void anim_set_descriptor_flags(int animId, unsigned int, unsigned int word0, unsigned int word1)
{
    short idx = (&DAT_804d1cd0)[animId];
    *(unsigned int*)(&DAT_804d34dc + idx * 8) = word1;
    *(unsigned int*)(&DAT_804d34d8 + idx * 8) = word0;
}

// anim_get_descriptor (0x80022a08)
// Resolve an animation id to its descriptor pointer, or 0 if the id has no slot.
int anim_get_descriptor(int animId)
{
    if (-1 < (&DAT_804d1cd0)[animId]) {
        return *(int*)(DAT_80611824 + (&DAT_804d1cd0)[animId] * 4);
    }
    return 0;
}

// anim_release_descriptors (0x80022354)
// Clear the descriptor-table slots for each entry in an anim list (stride 0x18,
// id at +0x10), so the descriptors can be reloaded / freed.
void anim_release_descriptors(int list, int count)
{
    if (count < 1) return;
    int off = 0;
    do {
        unsigned short id = *(unsigned short*)(list + off + 0x10);
        *(int*)(DAT_80611824 + (&DAT_804d1cd0)[id] * 4) = 0;
        off += 0x18;
        count = count - 1;
    } while (count != 0);
}

// anim_load_animtags (0x80022134)
// Load the animtags side-file for a model's animation and parse its per-anim
// event/tag table into DAT_804d0e20[id]. Reads "<dir>.animtags.dat" into the
// shared text buffer (DAT_804d5d00) once, finds the line matching the anim's
// base name (text_find_line_match), reads the event count then that many
// (frame, value) pairs into a freshly bump-allocated (or mem_alloc'd) table.
void anim_load_animtags(void)
{
    unsigned long long packed = FUN_803826d8();
    int animId = (int)(unsigned int)packed;

    char* path = __anim_get_animtags_path();
    // walk back from the end of the anim base name to the last '/'
    int animPathBase = (int)(size_t)(&DAT_804d5d00);
    int nameLen = FUN_8038b5c8(animPathBase);
    char* slash = (char*)(animPathBase + nameLen);
    while (*slash != '/') { slash = slash - 1; }

    // (re)load the animtags text buffer if the suffix differs
    if (FUN_8038b378((uint*)path, (uint*)&s_Not_animtags_dat_803f23e8) != 0) {
        int len = __dvd_read3(path, &DAT_804d5d00, 0x2800);
        FUN_8038b510((int*)&s_Not_animtags_dat_803f23e8, (int*)path);
        DAT_806117fc = len;
    }

    int idx = (&DAT_804d1cd0)[animId] * 4;
    *(int*)(&DAT_804d0e20 + idx) = 0;
    int desc = *(int*)(DAT_80611824 + idx);

    char* line = __text_find_line_match((int)(size_t)&DAT_804d5d00, (int)(size_t)(slash + 1), DAT_806117fc);
    if (line == (char*)0) { FUN_80382724(); return; }

    while (*line != '\n') { line = line + 1; }
    line = line + 1;
    int count = (int)FUN_8038c67c(line);
    if (count <= 0) {
        // still record an empty table header below
    }

    int table = 0;
    if (count > 0) {
        int need = DAT_80611820 + count * 4 + 8;
        if (need < 0x1401) {
            table = DAT_806117e0 + DAT_80611820;
            DAT_80611820 = need;
        } else {
            count = 0;
        }
    }
    *(short*)(table + 6) = (short)count;
    *(short*)(table + 4) = (short)*(int*)(desc + 0x10);

    int off = 0;
    for (int i = 0; i < count; ++i) {
        while (*line != '\n') { line = line + 1; }
        line = line + 1;
        short* pair = (short*)(table + off + 8);
        pair[0] = (short)FUN_8038c67c(line);
        while (*line != ' ' && *line != '\t') { line = line + 1; }
        while (*line == ' ' || *line == '\t') { line = line + 1; }
        pair[1] = (short)FUN_8038c67c(line);
        off += 4;
    }
    *(int*)(&DAT_804d0e20 + (&DAT_804d1cd0)[animId] * 4) = table;

    FUN_80382724();
}

// anim_load_descriptor (0x80022438)
// Load, decompress-relocate, parse and register a single animation descriptor.
// Fetches the pack asset by name; if a relocation arena (DAT_806117c4) is active
// and the asset fits the budget, copies it there (leaf_31e8). Validates the
// header magic, appends to the descriptor-build list, assigns a table slot,
// computes the clip length from the last keyframe time, and walks the channel
// stream table (switch over channel type 0/2/4/8/9/0xa/0xb/0xc/0xd) to lay out
// the per-channel data pointers. Finishes by loading the animtags table
// (anim_load_animtags). animId comes from the packed scratch frame.
void anim_load_descriptor(unsigned char* nameBuf, int listIndex)
{
    unsigned long long packed = FUN_803826cc();
    int* namePtr = (int*)(unsigned int)(packed >> 0x20);
    int animId = (int)(unsigned int)packed;
    bool ownedInline = true;

    unsigned char work[128];
    FUN_8038b510((int*)work, namePtr);
    int sep = FUN_8038b258((char*)work, &s_Not_animtags_dat_803f23e8);
    if (sep != 0) {
        FUN_8038b510((int*)sep, (int*)&s_Not_animtags_dat_803f23e8);
    }
    int asset = (int)(size_t)pack_get_asset_ptr((const char*)work, (u32*)0);
    int resolved = asset;
    unsigned int assetSize = 0;

    if ((DAT_806117c4 != (void*)0) &&
        ((assetSize = (unsigned int)__asset_get_size1(work)) != 0xffffffff) &&
        ((unsigned int)(FUN_80184bdc() - 0x19000) < assetSize)) {
        ownedInline = false;
        resolved = __mem_alloc2((int)assetSize, 1);
        leaf_31e8((u32)resolved, (u32)asset, (s32)assetSize);
    }

    int hdr = FUN_8038b338(resolved, -0x7f9f04ec, 4);
    int descData = resolved;
    if ((hdr == 0) || (FUN_8038b338(resolved, -0x7f9f04e4, 4) == 0)) {
        if (DAT_806117c0 != 0) {
            if (DAT_806117ec == 0) { DAT_806117ec = resolved; }
            bool room = DAT_806117f0 < 0x3c;
            (&DAT_804d5bf8)[DAT_806117f0] = (char*)(size_t)resolved;
            if (room) { DAT_806117f0 = DAT_806117f0 + 1; }
        }
        if (*(int*)(resolved + 4) == 0) { FUN_80382718(); return; }
        descData = resolved + *(int*)(resolved + 4);
    }

    int slot = (int)(short)(&DAT_804d1cd0)[animId];
    if (slot == -1) {
        (&DAT_804d1cd0)[animId] = (short)DAT_806117d8;
        slot = DAT_806117d8;
    }
    int idx4 = slot * 4;
    *(int*)(&DAT_804d34dc + slot * 8) = 0;
    *(int*)(&DAT_804d34d8 + slot * 8) = 0;
    *(unsigned int*)(DAT_80611824 + idx4) = (unsigned int)descData;

    if ((*(unsigned int*)(descData + 8) & 4) == 0) {
        int relocDelta = 0;
        if ((DAT_806117c4 != (void*)0) && ownedInline) {
            int placed = FUN_80184908((int)assetSize);
            relocDelta = placed - resolved;
        }
        // clip length from last keyframe time
        unsigned int rawLen = *(unsigned int*)(*(int*)(DAT_80611824 + idx4) + 0xc) ^ 0x80000000;
        double t = (double)(float)((double)CONCAT44(0x43300000, rawLen) - DOUBLE_80613938);
        *(float*)(*(int*)(DAT_80611824 + idx4) + 0x18) =
            (float)((double)(float)(t - (double)FLOAT_80613944) / (double)FLOAT_8060faf8);

        *(unsigned int*)(descData + 0x1c) = descData + 0x38 + relocDelta;
        unsigned int flagCompact = *(unsigned int*)(descData + 8) & 1;
        unsigned int half = *(unsigned int*)(descData + 8) >> 4 & 1;
        int p = descData + 0x38 + *(int*)(descData + 0x10) * 4;
        *(int*)(descData + 0x28) = p;
        p = p + *(int*)(descData + 0x24) * 0x20;
        if ((*(unsigned int*)(descData + 8) & 8) == 0) {
            *(int*)(descData + 0x2c) = 0;
        } else {
            *(int*)(descData + 0x2c) = p + relocDelta;
            p = p + *(int*)(descData + 0x10) * 4;
        }
        if (((*(int*)(descData + 0x10) == 1) && (p = p + 8, DAT_806117c0 != 0)) &&
            (0 < DAT_806117f0) && (DAT_806117f0 < 0x3c)) {
            DAT_806117f0 = DAT_806117f0 - 1;
        }

        int stride = 4 - (int)half;   // words per key component
        int strideHalf = stride * 2;
        int rec16 = 0;
        int nChan = *(int*)(descData + 0x24);
        for (int c = 0; c < nChan; ++c) {
            int chan = *(int*)(descData + 0x28) + rec16;
            *(int*)(chan + 0x14) = p + relocDelta;
            int add = 0;
            switch (*(int*)(chan + 4)) {
            case 0:
                add = (flagCompact != 0) ? (strideHalf + 0xc) : ((7 - (int)half) * 4);
                break;
            case 2:
                add = (flagCompact == 0) ? ((stride * *(int*)(chan + 0xc) + 3) * 4)
                                         : (stride * *(int*)(chan + 0xc) * 2 + 0xc);
                break;
            case 4:
                add = (flagCompact == 0) ? (stride * 4 + *(int*)(chan + 0xc) * 0xc)
                                         : (strideHalf + *(int*)(chan + 0xc) * 0xc);
                break;
            case 8:
            case 9:
                add = (flagCompact == 0)
                        ? (*(int*)(chan + 0xc) * 0xc + stride * *(int*)(chan + 0xc) * 4)
                        : (*(int*)(chan + 0xc) * 0xc + stride * *(int*)(chan + 0xc) * 2);
                break;
            case 10:
                add = *(int*)(chan + 0xc) * 0x20;
                break;
            case 0xb:
                add = *(int*)(chan + 0xc) * 10;
                break;
            case 0xc:
                add = (*(int*)(chan + 0xc) * 2 + 3) * 2;
                break;
            case 0xd:
                add = (*(int*)(chan + 0xc) * 3 + 2) * 2;
                break;
            default:
                break;
            }
            p = p + add;
            rec16 = rec16 + 0x20;
            *(int*)(chan + 0x10) = *(int*)(descData + 0x1c);
        }

        if ((DAT_806117c4 != (void*)0) && ownedInline) {
            *(unsigned int*)(descData + 8) = *(unsigned int*)(descData + 8) | 4;
            unsigned int cacheSlot = slot * 0x38 + 0x804d8500;
            *(int*)(descData + 0x28) = *(int*)(descData + 0x28) + relocDelta;
            leaf_31e8(cacheSlot, (u32)descData, 0x38);
            *(unsigned int*)(DAT_80611824 + idx4) = cacheSlot;
            *(int*)(descData + 0x34) = animId;
            FUN_8018493c(0, resolved, assetSize, (void*)(size_t)DAT_806117c8);
            DAT_806117c8 = DAT_806117c8 + 8;
            if ((int)((size_t)DAT_806117c4 + 0x318) <= DAT_806117c8) {
                FUN_801849e0();
                DAT_806117c8 = (int)(size_t)DAT_806117c4;
            }
        }
        anim_load_animtags();
        *(unsigned int*)(descData + 8) = *(unsigned int*)(descData + 8) | 4;
        if (slot == DAT_806117d8) { DAT_806117d8 = DAT_806117d8 + 1; }
    } else {
        if ((DAT_806117c4 != (void*)0) && ownedInline) {
            unsigned int cacheSlot = slot * 0x38 + 0x804d8500;
            leaf_31e8(cacheSlot,
                      (u32)((&DAT_804d1cd0)[*(int*)(descData + 0x34)] * 0x38 + 0x804d8500), 0x38);
            *(unsigned int*)(DAT_80611824 + idx4) = cacheSlot;
        }
        if (game_level_supports_scoring() != 0) {
            anim_load_animtags();
        }
    }
    (void)namePtr; (void)nameBuf; (void)listIndex;
    FUN_80382718();
}

// anim_load_descriptor_list (0x80022398)
// Iterate a model's animation list (count entries, stride 0x18) and load each
// descriptor: build the name (FUN_80387fb8), load it (anim_load_descriptor), run
// the post hook, then stamp the per-anim flag words (anim_set_descriptor_flags).
void anim_load_descriptor_list(unsigned char* nameBuf, int, int count)
{
    unsigned long long packed = FUN_803826e4();
    int listHi = (int)(unsigned int)(packed >> 0x20);
    int list = (int)(unsigned int)packed;

    unsigned char name[152];
    int off = 0;
    for (int i = 0; i < count; ++i) {
        int* ent = (int*)(list + off);
        unsigned int animId = *(unsigned int*)ent;
        // build the descriptor name into 'name' (FUN_80387fb8 == string builder)
        // then load + register it
        anim_load_descriptor(name, listHi);
        FUN_80022fb4();
        anim_set_descriptor_flags((int)*(unsigned short*)((char*)ent + 0x10),
                                  0, ((unsigned int*)ent)[2], ((unsigned int*)ent)[3]);
        off += 0x18;
    }
    (void)nameBuf;
    FUN_80382730();
}

// anim_keyframe_binary_search (0x80022d5c)
// Binary-search a channel's keyframe timeline for the pair bracketing time t.
// Writes the lo/hi keyframe indices to *outLo/*outHi. Degenerate cases: t<=0 or
// single key -> {0,0}; t past the end -> {last,last}.
void anim_keyframe_binary_search(double t, int channel, unsigned int* outLo, unsigned int* outHi)
{
    int nKeys = *(int*)(channel + 0x10);
    if ((t <= (double)FLOAT_80613930) || (nKeys == 1)) {
        *outHi = 0;
        *outLo = 0;
    } else if (t < (double)*(float*)(channel + 0x18)) {
        unsigned int hi = (unsigned int)(nKeys - 1);
        unsigned int lo = 0;
        unsigned int mid = ((int)hi >> 1) + (unsigned int)(((int)hi < 0) && ((hi & 1) != 0));
        while (true) {
            unsigned int* key = (unsigned int*)(*(int*)(channel + 0x1c) + mid * 4);
            double t0 = (double)((float)((double)CONCAT44(0x43300000, key[0] ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8);
            double t1 = (double)((float)((double)CONCAT44(0x43300000, key[1] ^ 0x80000000) - DOUBLE_80613938) / FLOAT_8060faf8);
            if (t == t0) { *outHi = mid; *outLo = mid; return; }
            if (t == t1) { *outHi = mid + 1; *outLo = mid + 1; return; }
            if ((t0 < t) && (t <= t1)) { *outLo = mid; *outHi = mid + 1; return; }
            if (t0 <= t) {
                if (t1 < t) {
                    unsigned int span = hi - mid;
                    lo = mid;
                    mid = mid + ((int)span >> 1) + (unsigned int)(((int)span < 0) && ((span & 1) != 0));
                }
            } else {
                unsigned int span = mid - lo;
                hi = mid;
                mid = lo + ((int)span >> 1) + (unsigned int)(((int)span < 0) && ((span & 1) != 0));
            }
        }
    } else {
        *outHi = (unsigned int)(nKeys - 1);
        *outLo = (unsigned int)(nKeys - 1);
    }
}

// ---------------------------------------------------------------------------
// Recursive skeleton bone-matrix concatenation.
//
// Each bone record (stride 0x30 ints / 0xc0 bytes off the model's skeleton block
// piVar7) has: a matrix index at +0x3d, a mode byte at +0xf (0/2/3), a "sibling"
// index at +0x3f, and a "child" index at +0x10. The walkers compose each bone's
// local matrix (model[0x10] + matIdx*0x40) with its parent's, recursing to
// siblings and children. Mode a concatenates when mode==0 with a valid parent;
// mode b when mode==2. The parent index threaded to children depends on the mode.
// ---------------------------------------------------------------------------
static void skel_concat_a_rec(int recHi, int model, int boneIdx, int parentIdx)
{
    int* skel = *(int**)(recHi + 4);
    int baseBone = *skel;
    int recOff = boneIdx * 0xc0 + baseBone * -0xc0;
    int childParent = parentIdx;

    if ((*(char*)(skel + baseBone * -0x30 + boneIdx * 0x30 + 0xf) == '\0') &&
        (childParent = boneIdx, -1 < parentIdx)) {
        float* dst = (float*)(model + *(char*)((int)skel + recOff + 0x3d) * 0x40);
        float* par = (float*)(model + *(char*)((int)skel + parentIdx * 0xc0 + baseBone * -0xc0 + 0x3d) * 0x40);
        FUN_803a1508(dst, par, dst);
    }
    char sibling = *(char*)((int)skel + recOff + 0x3f);
    if ((-1 < sibling) && (*(char*)(skel + baseBone * -0x30 + boneIdx * 0x30 + 0xf) != '\x03')) {
        skel_concat_a_rec(recHi, model, (int)sibling, childParent);
    }
    char child = *(char*)(skel + baseBone * -0x30 + boneIdx * 0x30 + 0x10);
    if (-1 < child) {
        skel_concat_a_rec(recHi, model, (int)child, parentIdx);
    }
}

// skel_concat_matrices_a (0x80022a38) — public entry (FUN_80022a38)
extern "C" void FUN_80022a38(int* model, int mtxBase, int boneIdx, int parentIdx)
{
    unsigned long long packed = FUN_803826e8();
    int recHi = (int)(unsigned int)(packed >> 0x20);
    int modelBase = (int)(unsigned int)packed;
    skel_concat_a_rec(recHi, modelBase, boneIdx, parentIdx);
    (void)model; (void)mtxBase;
    FUN_80382734();
}

static void skel_concat_b_rec(int recHi, int model, int boneIdx, int parentIdx)
{
    int* skel = *(int**)(recHi + 4);
    int baseBone = *skel;
    int recOff = boneIdx * 0xc0 + baseBone * -0xc0;

    if ((*(char*)(skel + baseBone * -0x30 + boneIdx * 0x30 + 0xf) == '\x02') && (-1 < parentIdx)) {
        float* dst = (float*)(model + *(char*)((int)skel + recOff + 0x3d) * 0x40);
        float* par = (float*)(model + *(char*)((int)skel + parentIdx * 0xc0 + baseBone * -0xc0 + 0x3d) * 0x40);
        FUN_803a1508(dst, par, dst);
    }
    int childParent = parentIdx;
    if (*(char*)(skel + baseBone * -0x30 + boneIdx * 0x30 + 0xf) == '\0') {
        childParent = boneIdx;
    }
    char sibling = *(char*)((int)skel + recOff + 0x3f);
    if ((-1 < sibling) && (*(char*)(skel + baseBone * -0x30 + boneIdx * 0x30 + 0xf) != '\x03')) {
        skel_concat_b_rec(recHi, model, (int)sibling, childParent);
    }
    char child = *(char*)(skel + baseBone * -0x30 + boneIdx * 0x30 + 0x10);
    if (-1 < child) {
        skel_concat_b_rec(recHi, model, (int)child, parentIdx);
    }
}

// skel_concat_matrices_b (0x80022b1c) — public entry (FUN_80022b1c)
extern "C" void FUN_80022b1c(int* model, int mtxBase, int boneIdx, int parentIdx)
{
    unsigned long long packed = FUN_803826e4();
    int recHi = (int)(unsigned int)(packed >> 0x20);
    int modelBase = (int)(unsigned int)packed;
    skel_concat_b_rec(recHi, modelBase, boneIdx, parentIdx);
    (void)model; (void)mtxBase;
    FUN_80382730();
}

static void skel_concat_slerp_rec(int recHi, int boneIdx, int mode)
{
    int* skel = *(int**)(recHi + 4);
    int baseBone = *skel;
    int recOff = boneIdx * 0xc0 + baseBone * -0xc0;

    if (*(char*)(skel + baseBone * -0x30 + boneIdx * 0x30 + 0xf) == '\x02') {
        int parOff = *(char*)((int)skel + recOff + 0x3f) * 0xc0 + baseBone * -0xc0;
        int dst = *(int*)(*(int*)(recHi + 0x84) + 0x78) +
                  *(int*)(*(int*)(*(int*)(recHi + 0x84) + 4) + boneIdx * 4) * 0x40;
        int a = *(int*)(recHi + 0x10) + *(char*)((int)skel + parOff + 0x3d) * 0x40;
        float* b = (float*)(*(int*)(recHi + 0x10) + *(char*)((int)skel + recOff + 0x3d) * 0x40);
        __mtx4_slerp3(dst, a, b);   // mode arg (param_3) selects blend weight source; both paths identical here
    }
    char sibling = *(char*)((int)skel + recOff + 0x3f);
    if (-1 < sibling) {
        if (*(char*)(skel + baseBone * -0x30 + boneIdx * 0x30 + 0xf) != '\x03') {
            skel_concat_slerp_rec(recHi, (int)sibling, mode);
        }
    }
    char child = *(char*)(skel + baseBone * -0x30 + boneIdx * 0x30 + 0x10);
    if (-1 < child) {
        skel_concat_slerp_rec(recHi, (int)child, mode);
    }
}

// skel_concat_slerp (0x80022c14) — public entry (FUN_80022c14)
extern "C" void FUN_80022c14(int* model, int boneIdx, int mode)
{
    unsigned long long packed = FUN_803826f0();
    int recHi = (int)(unsigned int)(packed >> 0x20);
    int startBone = (int)(unsigned int)packed;
    skel_concat_slerp_rec(recHi, startBone, mode);
    (void)model; (void)boneIdx;
    FUN_8038273c();
}
