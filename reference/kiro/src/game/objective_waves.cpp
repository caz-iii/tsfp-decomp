// Address: 0x80350230 | Name: objective_cell_has_both_flags | Date: 2026-09-12
// Batch: batch_1001
//
// Objective "wave" builder (mode 3). Continues the objective marker / spawn-bag
// subsystem (objective_markers.cpp, spawn_bag.cpp).
//
// The map's objective cell table lives at DAT_805c7b04 (base) / DAT_805c7b08
// (count), 0x20-byte stride. Each cell carries:
//   +0x04  cell id (u32)
//   +0x08  flag bits (u32)  - 0x80 = "team A" role, 0x100 = "team B" role
//   +0x1c  team/zone byte
//
// Per-context objective record (ctx, base 0x8034xxxx-relative offsets):
//   +0xe4  wave record array base   (0x60-byte stride)
//   +0xe8  wave count
// A wave record holds two short id lists (offsets +0x20 and +0x40).

#include "common.h"

// ---- raw symbols ----
extern char DAT_805015bd;        // game-mode byte (3 = this mode)
extern char DAT_805015c8;        // secondary mode/flag counter
extern char DAT_80501572;        // "resolve markers each wave" flag
extern int  DAT_805c7b04;        // objective cell table base
extern int  DAT_805c7b08;        // objective cell table count
extern int  DAT_80501570;        // string/context arg passed through
extern int  DAT_80612da0;        // per-wave tracked-cell list array base
extern int  DAT_80611274;        // debug format string

// ---- named cross-module helpers ----
extern int  objective_max_score(void);
extern int  objective_collect_linked_cells(int ctx, int wave, int outBuf);
extern void objective_build_tracked_list(int src, int dst, int count, int wave);
extern void objective_compact_waves(void);
extern void objective_resolve_all_markers(void);
extern int  worm_class_find_first_active_index(void);
extern int  get_string_by_id(int id);
extern void aveditor_type_record_ptr(int pRecord, int kind);
extern int  memset_wrapper(int dst, int val, int len);
extern void mem_free_top(int p);

// mem_alloc / debug_log_formatted / FUN_80387fb8 are variadic PPC ABI shims;
// declare with the exact arity used at the call sites.
extern "C" int mem_alloc(unsigned long long f1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int size, int tag,
                         int a11, int a12, int a13, int a14, int a15, int a16);
extern "C" unsigned long long FUN_80387fb8(unsigned long long f1, unsigned long long a2,
                         unsigned long long a3, unsigned long long a4, unsigned long long a5,
                         unsigned long long a6, unsigned long long a7, unsigned long long a8,
                         int buf, const char *fmt, int val, int a11, int a12, int a13, int a14,
                         int a15);
extern "C" void debug_log_formatted(unsigned long long f1, unsigned long long a2,
                         unsigned long long a3, unsigned long long a4, unsigned long long a5,
                         unsigned long long a6, unsigned long long a7, unsigned long long a8,
                         void *fmt, void *buf, int val, int a11, int a12, int a13, int a14, int a15);

// Register-pair objective-context stubs.
extern "C" unsigned long long FUN_803826f0(void);  // fetch (ctx, code) pair
extern "C" void               FUN_8038273c(void);
extern "C" int                FUN_803826d8(void);   // current objective ctx
extern "C" void               FUN_80382724(void);
extern "C" int                __save_gpr_28(void);
extern "C" void               __restore_gpr_28(void);

// ---------------------------------------------------------------------------
// True (returns -1) when the cell table contains, for the given team, at least
// one cell with the 0x80 role flag AND at least one with the 0x100 role flag.
// Returns -2 otherwise (the original computes -1 - !both).
char objective_cell_has_both_flags(uint team)
{
    bool sawA = false;
    bool sawB = false;
    int off = 0;
    int remaining = DAT_805c7b08;
    if (0 < DAT_805c7b08) {
        do {
            int cell = DAT_805c7b04 + off;
            if ((*(byte *)(cell + 0x1c) == team) && ((*(uint *)(cell + 8) & 0x80) != 0)) {
                sawA = true;
            }
            if ((*(byte *)(cell + 0x1c) == team) && ((*(uint *)(cell + 8) & 0x100) != 0)) {
                sawB = true;
            }
            off += 0x20;
            remaining += -1;
        } while (remaining != 0);
    }
    bool both = (sawA && sawB);
    return (char)(-1 - !both);
}

// ---------------------------------------------------------------------------
// Copy `count` cell ids (32-bit source array) into a wave record's short list:
// role 1 -> list at +0x20, role 0 -> list at +0x40.
void objective_copy_cells_to_slot(int pWave, int role, int pSrc, int count)
{
    int dstOff = 0;
    int srcOff = 0;
    if (0 < count) {
        do {
            if (role == 1) {
                *(short *)(pWave + dstOff + 0x20) = (short)*(u32 *)(pSrc + srcOff);
            } else if ((role < 1) && (-1 < role)) {
                *(short *)(pWave + dstOff + 0x40) = (short)*(u32 *)(pSrc + srcOff);
            }
            dstOff += 2;
            srcOff += 4;
            count += -1;
        } while (count != 0);
    }
}

// ---------------------------------------------------------------------------
// Gather up to `max` cell ids matching (team, role flag) into outBuf; returns
// how many were written. role 1 uses flag 0x80, role 0 uses flag 0x100.
int objective_collect_flagged_cells(uint team, int role, int outBuf, int max)
{
    int written = 0;
    int outOff = 0;
    int idx = 0;
    int off = 0;
    do {
        if (DAT_805c7b08 <= idx) {
            return written;
        }
        int cell = DAT_805c7b04 + off;
        uint cellTeam = (uint)*(byte *)(cell + 0x1c);
        if (cellTeam != 0) {
            if (max <= written) {
                return written;
            }
            if (role == 1) {
                if ((cellTeam == team) && ((*(uint *)(cell + 8) & 0x80) != 0)) {
                    written = written + 1;
                    *(u32 *)(outBuf + outOff) = *(u32 *)(cell + 4);
                    outOff = outOff + 4;
                }
            } else if ((role < 1) && (-1 < role) && (cellTeam == team) &&
                       ((*(uint *)(cell + 8) & 0x100) != 0)) {
                written = written + 1;
                *(u32 *)(outBuf + outOff) = *(u32 *)(cell + 4);
                outOff = outOff + 4;
            }
        }
        idx = idx + 1;
        off = off + 0x20;
    } while (true);
}

// ---------------------------------------------------------------------------
// Fill one wave record's list for a given role: collect matching cells into a
// scratch buffer then copy them (as shorts) into the record.
void objective_fill_wave_cell_lists(int pWave, int team, int role)
{
    unsigned char scratch[88];
    unsigned long long pair = FUN_803826f0();
    memset_wrapper((int)scratch, 0, 0x40);
    int n = objective_collect_flagged_cells((uint)pair, role, (int)scratch, 0x10);
    objective_copy_cells_to_slot((int)(unsigned)(pair >> 0x20), role, (int)scratch, n);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Fill both role lists (1 and 0) of the wave record for `waveIndex`.
void objective_fill_wave_both_teams(int ctx, int waveIndex)
{
    int pWave = *(int *)(ctx + 0xe4) + waveIndex * 0x60;
    objective_fill_wave_cell_lists(pWave, waveIndex + 1, 1);
    objective_fill_wave_cell_lists(pWave, waveIndex + 1, 0);
}

// ---------------------------------------------------------------------------
// Find the editor/type record for the wave that owns zone `zone`. Walks the
// editor object list (base +0x138, 0x220 stride, count +0x134) and, on a match
// with an active (+0x04 == 1) entry, returns the typed record pointer.
int objective_find_wave_record_by_zone(int pEditor, int zone)
{
    int count = *(int *)(pEditor + 0x134);
    int off = 0;
    if (0 < count) {
        do {
            int *pObj = (int *)(*(int *)(pEditor + 0x138) + off);
            if ((*(int *)(*(int *)(pEditor + 0x130) + *pObj * 0xb4 + 0xac) == zone) &&
                (*(char *)(pObj + 1) == '\x01')) {
                return *(int *)(pEditor + 0x128) +
                       *(short *)(*(int *)(pEditor + 0x130) + pObj[0x23] * 0xb4 + 0xc) * 0x8c;
            }
            off = off + 0x220;
            count = count + -1;
        } while (count != 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Format and log an objective wave debug string (or type its editor record).
// The (ctx, code) pair selects the label: code 2 uses string 0xb81, code 0/1
// uses 0xb80; a higher range logs 0xb82/0xb83 by DAT_805015c8.
void objective_format_wave_debug(unsigned long long a2, unsigned long long a3,
                                 unsigned long long a4, unsigned long long a5,
                                 unsigned long long a6, unsigned long long a7,
                                 unsigned long long a8)
{
    int in_r6 = 0, in_r7 = 0, in_r8 = 0, in_r9 = 0, in_r10 = 0;
    unsigned char scratch[152];

    long long pair = FUN_803826f0();
    int code = (int)(unsigned)((unsigned long long)pair >> 0x20);
    int index = (int)pair;
    unsigned long long f1 = 0;

    if (index == -1) {
        index = worm_class_find_first_active_index();
    }
    int pRec = objective_find_wave_record_by_zone(-0x7fa38520, index + 1);

    if (code == 2) {
        const char *label = (const char *)get_string_by_id(0xb81);
        index = index + 1;
        f1 = FUN_80387fb8(f1, a2, a3, a4, a5, a6, a7, a8, (int)scratch, label, index,
                          in_r6, in_r7, in_r8, in_r9, in_r10);
        if (pRec == 0) {
            debug_log_formatted(f1, a2, a3, a4, a5, a6, a7, a8, &DAT_80611274, scratch, index,
                                in_r6, in_r7, in_r8, in_r9, in_r10);
        } else {
            aveditor_type_record_ptr(pRec, 2);
        }
    } else if (pair < 0x200000000LL) {
        const char *label = (const char *)get_string_by_id(0xb80);
        index = index + 1;
        f1 = FUN_80387fb8(f1, a2, a3, a4, a5, a6, a7, a8, (int)scratch, label, index,
                          in_r6, in_r7, in_r8, in_r9, in_r10);
        if ((code == 0) || (-1 < pair)) {
            if (pRec == 0) {
                debug_log_formatted(f1, a2, a3, a4, a5, a6, a7, a8, &DAT_80611274, scratch, index,
                                    in_r6, in_r7, in_r8, in_r9, in_r10);
            } else {
                aveditor_type_record_ptr(pRec, code);
            }
        }
    } else if (pair < 0x400000000LL) {
        if (DAT_805015c8 < 1) {
            get_string_by_id(0xb83);
        } else {
            get_string_by_id(0xb82);
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Allocate the per-context wave tables: the wave record array (0x60 each) and
// the tracked-cell list array (DAT_80612da0). For each wave with linked cells,
// allocate a list header plus its cell array.
void objective_alloc_wave_tables(unsigned long long f1, unsigned long long a2,
                                 unsigned long long a3, unsigned long long a4,
                                 unsigned long long a5, unsigned long long a6,
                                 unsigned long long a7, unsigned long long a8,
                                 int a11, int a12)
{
    int ctx = __save_gpr_28();
    (void)f1;

    *(int *)(ctx + 0xe8) = objective_max_score();
    *(int *)(ctx + 0xe4) = mem_alloc(f1, a2, a3, a4, a5, a6, a7, a8,
                                     *(int *)(ctx + 0xe8) * 0x60, 1, a11, a12, 0, 0, 0, 0);
    DAT_80612da0 = mem_alloc(f1, a2, a3, a4, a5, a6, a7, a8,
                             (int)((*(unsigned *)(ctx + 0xe8) * 4 + 0xf) & 0xfffffff0), 1,
                             a11, a12, 0, 0, 0, 0);
    int wave = 0;
    int listOff = 0;
    while (wave < *(int *)(ctx + 0xe8)) {
        int n = objective_collect_linked_cells(ctx, wave + 1, 0);
        if (n != 0) {
            int hdr = mem_alloc(f1, a2, a3, a4, a5, a6, a7, a8, 0x10, 1, 0, a12, 0, 0, 0, 0);
            *(int *)(DAT_80612da0 + listOff) = hdr;
            int cells = mem_alloc(f1, a2, a3, a4, a5, a6, a7, a8,
                                  (int)((n * 0xc + 0xf) & 0xfffffff0), 1, n * 0xc, a12, 0, 0, 0, 0);
            **(int **)(DAT_80612da0 + listOff) = cells;
        }
        wave = wave + 1;
        listOff = listOff + 4;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Top-level mode-3 wave build: allocate tables, then for each wave that has the
// required team-flag pair populate its record, tracked cell list and both role
// lists, and optionally resolve markers.
void objective_build_waves_mode3(unsigned long long a2, unsigned long long a3,
                                 unsigned long long a4, unsigned long long a5,
                                 unsigned long long a6, unsigned long long a7,
                                 unsigned long long a8, int a11, int a12)
{
    int ctx = FUN_803826d8();
    unsigned long long f1 = 0;
    if (DAT_805015bd == '\x03') {
        objective_alloc_wave_tables(f1, a2, a3, a4, a5, a6, a7, a8, (int)&DAT_80501570, a11);
        int recOff = 0;
        for (int wave = 0; wave < *(int *)(ctx + 0xe8); wave = wave + 1) {
            uint w1 = wave + 1;
            char both = objective_cell_has_both_flags(w1);
            if (both != '\0') {
                int pRec = *(int *)(ctx + 0xe4) + recOff;
                int n = objective_collect_linked_cells(ctx, w1, 0);
                if (n != 0) {
                    int sz = (int)((n * 4 + 0xf) & 0xfffffff0);
                    int buf1 = mem_alloc(f1, a2, a3, a4, a5, a6, a7, a8, sz, 0, 0, a12, 0, 0, 0, 0);
                    int buf2 = mem_alloc(f1, a2, a3, a4, a5, a6, a7, a8, sz, 0, 0, a12, 0, 0, 0, 0);
                    objective_collect_linked_cells(ctx, w1, buf1);
                    objective_build_tracked_list(buf1, buf2, n, w1);
                    mem_free_top(buf2);
                    mem_free_top(buf1);
                }
                *(int *)(pRec + 4) = wave;
                *(u32 *)(pRec + 8) = 0;
                *(u32 *)(pRec + 0xc) = 1;
                *(u32 *)(pRec + 0x10) = 2;
                *(u32 *)(pRec + 0x14) = 3;
                objective_fill_wave_both_teams(ctx, wave);
                if ('\0' < DAT_80501572) {
                    objective_resolve_all_markers();
                }
            }
            recOff = recOff + 0x60;
        }
        objective_compact_waves();
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// True if the entity's world position (base +0x64 plus offset +0x1a8..+0x1b0)
// lies inside the axis-aligned bounds described by param_2[0..5] (min xyz then
// max xyz interleaved as min.x,min.y,min.z,max.x,max.y,max.z).
u32 objective_entity_in_bounds(int pEntity, float *bounds)
{
    float x = *(float *)(pEntity + 100) + *(float *)(pEntity + 0x1a8);
    float y = *(float *)(pEntity + 0x68) + *(float *)(pEntity + 0x1ac);
    float z = *(float *)(pEntity + 0x6c) + *(float *)(pEntity + 0x1b0);
    if ((bounds[0] <= x) && (x <= bounds[3]) &&
        (bounds[1] <= y) && (y <= bounds[4]) &&
        (bounds[2] <= z) && (z <= bounds[5])) {
        return 1;
    }
    return 0;
}
