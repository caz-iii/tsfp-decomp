// Batch: batch_997 | Module: objective wave/slot progression
// Address range: 0x8034e558 - 0x8034ea6c
// Date: 2026-09-12
//
// The per-frame objective progression subsystem, wired up by the objective setup pass
// (FUN_803507a4). It sits on top of the batch_996 spawn-table helpers: each objective
// "wave" owns a dispatch-table entry (DAT_80612da0[index-1]) holding a tracked-entity
// list; the level's objective cells resolve to entity-pool records whose per-type handler
// (record+0x10) is a function pointer. Progression walks the active wave, runs its slots'
// handlers, and advances to the next wave when the current one is cleared.
//
//   objective_build_tracked_list   0x8034e558  fill a wave's dispatch entry with resolved
//                                              entity handles + objective-cell indices from
//                                              the objective's descriptor list.
//   objective_next_active_wave     0x8034e62c  next wave index (from param+1) whose state
//                                              (+0x14, stride 0x60) == 3, else param+1.
//   objective_slot_is_populated    0x8034e680  a wave slot is populated when its dispatch
//                                              entry has a non-zero count (+4), or FUN_80350230.
//   objective_advance_wave_if_clear 0x8034e6e4 scan remaining waves; if none populated,
//                                              advance to objective_next_active_wave.
//   objective_compact_waves        0x8034e780  shift the 0xc-word tracked block of each
//                                              non-state-3 wave down from its successor.
//   objective_collect_linked_cells 0x8034e83c collect the objective's linked cell records
//                                              (passing FUN_8034f0c8) into an output list.
//   objective_slot_check_clear     0x8034e91c  if a type-1 slot's flag (+0x20) is set, clear
//                                              it and report 1 (state changed).
//   objective_slot_evaluate        0x8034e980  evaluate a slot: state 1 => done; else invoke
//                                              its handler (+0x10) and test for completion.
//   objective_run_active_handlers  0x8034e9e0  for the active objective wave, run every linked
//                                              type-2 slot's handler (+0x10) when non-null.
//   objective_progression_tick     0x8034ea6c  per-frame tick: if the current wave is ready,
//                                              run its handlers and (unless it's the last wave
//                                              and the story flag is set) advance/compact.
//
// NON-MATCHING: reproduces the decompiler's structure. The r28/r25 save-pair values the
// decompiler recovered are expressed as explicit locals seeded from the level-context
// accessors (FUN_803826d0 / FUN_803826f0) rather than synthetic register pairs
// (Compile-Clean rule 3). Named helpers from batch_996 (objective_find_spawn_record,
// objective_cell_index, spawn_table_get, entity_pool_find_type1/2, objective_cell_is_active)
// resolve through the shared headers (rule 6); raw callees are declared file-locally (rule 5).

#include "common.h"

extern "C" {

// ---- named cross-module helpers (batch_996 + entity pool) ------------------
extern int  objective_find_spawn_record(int cellId);
extern int  objective_cell_index(int cell);
extern int  spawn_table_get(int index);
extern char objective_cell_is_active(int cell);
extern int  entity_pool_find_type1(int id);
extern int  entity_pool_find_type2(int id);
extern int  entity_handle_resolve(int id);

// ---- raw callees (file-local, faithful to call-site) -----------------------
extern int  FUN_803826d0(void);   // acquire level context (objective descriptor cursor)
extern void FUN_8038271c(void);   // release (paired with 803826d0)
extern int  FUN_803826f0(void);   // acquire wave/objective index cursor
extern void FUN_8038273c(void);   // release (paired with 803826f0)
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern char FUN_80350230(unsigned int index);   // secondary slot-populated test
extern int  FUN_8034f0c8(int cellRec);           // objective-cell "linked" predicate
extern void FUN_803501b4(void);                  // post-advance objective refresh

// ---- data symbols ----------------------------------------------------------
extern int*  DAT_80612da0;   // wave dispatch table (entry = tracked-list header ptr)
extern int   DAT_805c7bc4;   // wave state table base (stride 0x60, +0x14 = state)
extern int   DAT_805c7bc8;   // wave count
extern int   DAT_8061208c;   // objective descriptor block base
extern int   DAT_806128ac;   // objective slot state array base (stride 0x4c)
extern char  DAT_80501572;   // story-mode / progression-enabled flag

// forward decls for cluster members referenced before definition
int  objective_next_active_wave(int index);
char objective_slot_is_populated(unsigned int index);
void objective_advance_wave_if_clear(void);
void objective_run_active_handlers(void);

// ---------------------------------------------------------------------------
// 0x8034e558 : build a wave's tracked-entity list.
//   param_3 = descriptor count, param_4 = wave index (1-based into DAT_80612da0).
// For each objective descriptor with a valid marker id (+0xc >= 0), resolve its
// entity handle, store {handle, descriptor ptr, objective-cell index} into the
// wave's list (stride 0xc) and append the handle to a scratch list; finally
// record the count at header+4.
// ---------------------------------------------------------------------------
void objective_build_tracked_list(unsigned int p1, unsigned int p2, int count, int waveIndex)
{
    (void)p1; (void)p2;
    int ctx = FUN_803826d0();
    int* header = *(int**)(DAT_80612da0 + waveIndex * 4 + -4);
    int scratchOff = 0;
    int rec = objective_find_spawn_record(waveIndex);
    int listOff = 0;
    int descOff = 0;
    header[2] = *(int*)(rec + 0x4c);
    for (int i = 0; i < count; i++) {
        int marker = (int)*(short*)(*(int*)(ctx + descOff) + 0xc);
        if (-1 < marker) {
            int handle = entity_handle_resolve(marker + 1);
            int* slot = (int*)(*header + listOff);
            slot[0] = handle;
            slot[1] = *(int*)(ctx + descOff);
            slot[2] = objective_cell_index(*(int*)(ctx + descOff));
            *(int*)(ctx + scratchOff) = handle;   // scratch handle list (via ctx cursor)
            scratchOff = scratchOff + 4;
        }
        listOff = listOff + 0xc;
        descOff = descOff + 4;
    }
    *(int*)(*(int*)(DAT_80612da0 + waveIndex * 4 + -4) + 4) = count;
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// 0x8034e62c : next wave index (starting at index+1) whose state (+0x14, stride
// 0x60) == 3; returns index+1 if none.
// ---------------------------------------------------------------------------
int objective_next_active_wave(int index)
{
    int start = index + 1;
    int off = start * 0x60;
    int remaining = DAT_805c7bc8 - start;
    int cursor = start;
    if (DAT_805c7bc8 <= start) {
        return start;
    }
    do {
        if (*(int*)(DAT_805c7bc4 + off + 0x14) == 3) {
            return cursor;
        }
        cursor = cursor + 1;
        off = off + 0x60;
        remaining = remaining - 1;
    } while (remaining != 0);
    return start;
}

// ---------------------------------------------------------------------------
// 0x8034e680 : a wave slot is "populated" when its dispatch entry exists and has
// a non-zero count (+4), or FUN_80350230 reports populated. Returns -1 populated
// / -2 empty (the `-1 - !bVar1` idiom preserved).
// ---------------------------------------------------------------------------
char objective_slot_is_populated(unsigned int index)
{
    bool populated = false;
    int entry = *(int*)(DAT_80612da0 + (index - 1) * 4);
    if (((entry != 0) && (*(int*)(entry + 4) != 0)) || (FUN_80350230(index) != '\0')) {
        populated = true;
    }
    return (char)(-1 - !populated);
}

// ---------------------------------------------------------------------------
// 0x8034e6e4 : if no remaining wave (after the current one) is populated, advance
// to the next active wave. The current index arrives via the r25 save-pair.
// ---------------------------------------------------------------------------
void objective_advance_wave_if_clear(void)
{
    int index = __save_gpr_25();
    bool anyPopulated = false;
    int i = 0;
    while (true) {
        if (DAT_805c7bc8 <= i) {
            break;
        }
        int wave = index + 1 + i;
        if ((wave < DAT_805c7bc8) && (objective_slot_is_populated(wave + 1) != '\0')) {
            anyPopulated = true;
            break;
        }
        i = i + 1;
    }
    if (!anyPopulated) {
        objective_next_active_wave(index);
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x8034e780 : compact the wave table. For each non-state-3 wave, mark its slot
// state byte (+0xb of the 0x4c-stride array) = 5 and copy the 0xc-word tracked
// block down from the next active wave (objective_next_active_wave).
// ---------------------------------------------------------------------------
void objective_compact_waves(void)
{
    FUN_803826f0();
    int off = 0;
    for (int i = 0; i < *(int*)(DAT_8061208c + 0xe8) + -1; i++) {
        int wave = *(int*)(DAT_8061208c + 0xe4) + off;
        if (*(int*)(wave + 0x14) != 3) {
            *(unsigned char*)(DAT_806128ac + *(int*)(wave + 4) * 0x4c + 0xb) = 5;
            int src = objective_next_active_wave(i);
            int* dst = (int*)(wave + -4);
            int* srcPtr = (int*)(*(int*)(DAT_8061208c + 0xe4) + src * 0x60 + -4);
            int n = 0xc;
            do {
                int* a = srcPtr + 1;
                srcPtr = srcPtr + 2;
                int v = *srcPtr;
                dst[1] = *a;
                dst = dst + 2;
                *dst = v;
                n = n - 1;
            } while (n != 0);
        }
        off = off + 0x60;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8034e83c : collect the objective's linked cell records (those for which
// FUN_8034f0c8 is true) into the output list. Walks the level member records
// (ctx+0x138, stride 0x220) whose objective-cell id (+0xac) matches the current
// objective (ctx high word), then their sub-cell id list (+0x54, count +0x50).
// ---------------------------------------------------------------------------
void objective_collect_linked_cells(unsigned int p1, unsigned int p2, int out)
{
    (void)p1; (void)p2;
    int ctx = FUN_803826d0();
    int objId = ctx;                 // objective id derived from the context cursor
    int outOff = 0;
    int memberOff = 0;
    for (int m = 0; m < *(int*)(ctx + 0x134); m++) {
        int* member = (int*)(*(int*)(ctx + 0x138) + memberOff);
        if (*(int*)(*(int*)(ctx + 0x130) + *member * 0xb4 + 0xac) == objId) {
            int subOff = 0;
            for (int c = 0; c < member[0x14]; c++) {
                int cellRec = *(int*)(ctx + 0x130) +
                              *(int*)((int)member + subOff + 0x54) * 0xb4;
                if (FUN_8034f0c8(cellRec) != 0) {
                    if (out != 0) {
                        *(int*)(out + outOff) = cellRec;
                    }
                    outOff = outOff + 4;
                }
                subOff = subOff + 4;
            }
        }
        memberOff = memberOff + 0x220;
    }
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// 0x8034e91c : if a type-1 pool slot's flag (+0x20) is set, clear it and report
// 1 (state changed); else 0.
// ---------------------------------------------------------------------------
unsigned int objective_slot_check_clear(int rec)
{
    int slot = entity_pool_find_type1(*(int*)(rec + 8));
    unsigned int result;
    if ((*(int*)(slot + 0x18) == 1) && (*(int*)(slot + 0x20) == 0)) {
        result = 0;
    } else if (*(int*)(slot + 0x20) == 0) {
        result = 0;
    } else {
        *(int*)(slot + 0x20) = 0;
        result = 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8034e980 : evaluate a slot. If its state (+0x18) is already 1 (done) return
// 1; otherwise invoke its handler (+0x10) and return whether it is now complete.
// ---------------------------------------------------------------------------
unsigned int objective_slot_evaluate(int rec)
{
    int slot = entity_pool_find_type1(*(int*)(rec + 8));
    unsigned int result;
    if (*(int*)(slot + 0x18) == 1) {
        result = 1;
    } else {
        (*(void(**)(void))(slot + 0x10))();
        result = countLeadingZeros(1 - *(int*)(slot + 0x18)) >> 5;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8034e9e0 : for the active objective wave, run every linked type-2 slot's
// handler (+0x10) when non-null.
// ---------------------------------------------------------------------------
void objective_run_active_handlers(void)
{
    int index = FUN_803826f0();
    spawn_table_get(index);
    int rec = objective_find_spawn_record(index + 1);
    if (objective_cell_is_active(rec) != '\0') {
        int off = 0;
        for (int i = 0; i < *(int*)(rec + 0x88); i++) {
            int slot = entity_pool_find_type2(*(int*)(rec + off + 0x8c));
            if (*(void(**)(void))(slot + 0x10) != 0) {
                (*(void(**)(void))(slot + 0x10))();
            }
            off = off + 4;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8034ea6c : per-frame objective progression tick. The current wave index
// arrives via the r28 save-pair. If the wave is "ready" (its dispatch entry has
// no pending count, or the index is non-zero), run its handlers; then unless this
// is the last wave with the story flag set, advance and refresh.
// ---------------------------------------------------------------------------
void objective_progression_tick(unsigned int p1, unsigned int p2, int deadline)
{
    (void)p1; (void)p2;
    int index = __save_gpr_28();
    unsigned int ready;
    if (*(int*)(*(int*)(DAT_80612da0 + index * 4) + 8) == 0) {
        ready = countLeadingZeros(deadline - index) >> 5;
    } else {
        ready = (unsigned int)(index != 0);
    }
    if (ready != 0) {
        objective_run_active_handlers();
        if ((index + 1 != DAT_805c7bc8) && ('\0' < DAT_80501572)) {
            objective_advance_wave_if_clear();
            FUN_803501b4();
        }
    }
    __restore_gpr_28();
}

} // extern "C"
