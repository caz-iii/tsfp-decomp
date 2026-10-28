// Address: 0x80351a08 | Name: objective_stamp_task_matrix | Date: 2026-09-12
// Batch: batch_1004
//
// Objective visibility-mask update and task transform helpers. Continues the
// objective marker / wave / trigger / tick subsystem. The visibility descriptor
// tables are DAT_80482244 (10 entries, 0x4c bytes = 0x13 dwords each) referenced
// by objective_triggers.cpp via objective_find_visibility_desc / _desc_at.
//
// Objective context (from register-pair stub): +0x04 -> render node, whose +0x10
// is a matrix array and +0x30 a visibility bitmask; +0x7c a task-state record;
// +0x94 a task descriptor id.

#include "common.h"

// ---- raw symbols ----
extern int   DAT_80482244;      // team visibility descriptor table (0x4c stride)
extern int   DAT_805c7b8c;      // zone-group table base (0x4c stride)
extern int   DAT_805c7b90;      // zone-group table count
extern float FLOAT_8061ad00;    // 0.0f constant
extern float FLOAT_8061ad38;    // reach/box scale constant

// ---- named cross-module helpers ----
extern int  mtx4_set_identity(int pMtx);
extern int  mtx4_copy_array(int dst, int src, int count);
extern void mtx4_copy(float *dst, float *src);
extern void mtx4_set_translation(double x, double y, double z);
extern int * objective_find_task_descriptor(unsigned id);
extern void switch_toggle_state(int pSwitch);
extern int  slot_find_player_instance(void);

// Register-pair context stubs.
extern "C" int  FUN_803826f0(void);
extern "C" void FUN_8038273c(void);
extern "C" int  __save_gpr_28(void);
extern "C" void __restore_gpr_28(void);

// ---------------------------------------------------------------------------
// Stamp the task's world matrix: set identity into a scratch matrix, copy the
// render node's matrix array, and raise the "matrix dirty" flag (bit 0x200000).
void objective_stamp_task_matrix(int pTask)
{
    unsigned char scratch[68];
    mtx4_set_identity((int)scratch);
    mtx4_copy_array(*(u32 *)(*(int *)(pTask + 4) + 0x10), (int)scratch,
                    *(int *)(*(int *)(*(int *)(pTask + 4) + 4) + 4));
    *(u32 *)(pTask + 0x28) = *(u32 *)(pTask + 0x28) | 0x200000;
}

// ---------------------------------------------------------------------------
// Update a task's render-node visibility bitmask (+0x30) from its visibility
// descriptor: always OR in the "always visible" zones (desc[3]); then, based on
// whether the task-state's +0x18 flag is set, swap between the on/off zone sets
// desc[1] and desc[2].
void objective_update_task_visibility_mask(void)
{
    int ctx = FUN_803826f0();
    int state = *(int *)(ctx + 0x7c);
    int *zones = *(int **)(*(int *)(ctx + 4) + 4);
    int *desc = objective_find_task_descriptor((unsigned)*(unsigned short *)(ctx + 0x94));
    u32 *pMask = (u32 *)(*(int *)(ctx + 4) + 0x30);

    if (-1 < desc[3]) {
        *pMask = *pMask | (u32)zones[*zones * -0x30 + desc[3] * 0x30 + 0x14];
    }
    if ((state == 0) || (*(int *)(state + 0x18) == 0)) {
        *pMask = *pMask & ~(u32)zones[*zones * -0x30 + desc[1] * 0x30 + 0x14];
        *pMask = *pMask | (u32)zones[*zones * -0x30 + desc[2] * 0x30 + 0x14];
    } else {
        *pMask = *pMask | (u32)zones[*zones * -0x30 + desc[1] * 0x30 + 0x14];
        *pMask = *pMask & ~(u32)zones[*zones * -0x30 + desc[2] * 0x30 + 0x14];
    }
    FUN_8038273c();
}

// Thin wrapper (distinct callback entry).
void objective_update_task_visibility_wrap(void)
{
    objective_update_task_visibility_mask();
}

// ---------------------------------------------------------------------------
// Switch handler: if the linked entity's pressure value (+0x1cc) exceeds 0 and
// the switch is not already latched (flag bit0 at +0x10), toggle its state.
void switch_toggle_if_pressure(int pSwitch)
{
    if ((FLOAT_8061ad00 < *(float *)(*(int *)(pSwitch + 8) + 0x1cc)) &&
        ((*(u32 *)(pSwitch + 0x10) & 1) == 0)) {
        switch_toggle_state(pSwitch);
    }
}

// ---------------------------------------------------------------------------
// Bind a record to its zone group: scan the zone-group table (DAT_805c7b8c,
// 0x4c stride) for the entry whose id matches record[0]; copy its field[0xf]
// into record[8].
void objective_bind_zone_group(int *pRecord)
{
    int off = 0;
    int remaining = DAT_805c7b90;
    if (DAT_805c7b90 < 1) {
        return;
    }
    do {
        if (*(int *)(DAT_805c7b8c + off) == *pRecord) {
            pRecord[8] = ((int *)(DAT_805c7b8c + off))[0xf];
            return;
        }
        off = off + 0x4c;
        remaining = remaining + -1;
    } while (remaining != 0);
}

// ---------------------------------------------------------------------------
// Find the visibility descriptor whose first word matches `id` in the 10-entry
// table at DAT_80482244 (0x4c stride). Returns null if none.
int * objective_find_visibility_desc(int id)
{
    int off = 0;
    int i = 10;
    do {
        if (id == *(int *)((int)&DAT_80482244 + off)) {
            return (int *)((int)&DAT_80482244 + off);
        }
        off = off + 0x4c;
        i = i + -1;
    } while (i != 0);
    return (int *)0x0;
}

// Direct index into the visibility descriptor table (0x13 dwords per entry).
u32 * objective_visibility_desc_at(int index)
{
    return (u32 *)(&DAT_80482244) + index * 0x13;
}

// ---------------------------------------------------------------------------
// Axis-aligned proximity test between two entities. Returns 1 when both exist
// and the target is within the box: X and Z within the target's radius (+0x4c
// on its owner record) and Y within the caller-supplied vertical span.
u32 entity_within_box(double ySpan, int pA, int pB)
{
    if ((pA == 0) || (pB == 0)) {
        return 0;
    }
    float radius = *(float *)(*(int *)(pB + 0x7c) + 0x4c);
    if (radius < ABS(*(float *)(pA + 100) - *(float *)(pB + 100))) {
        return 0;
    }
    if (ySpan < ABS((double)((*(float *)(pA + 0x68) + *(float *)(pA + 0x1ac)) -
                             (*(float *)(pB + 0x68) + *(float *)(pB + 0x1b8))))) {
        return 0;
    }
    if (radius < ABS(*(float *)(pA + 0x6c) - *(float *)(pB + 0x6c))) {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Reach test: if the caller entity is a mounted/attached object (type 0x1000 at
// +0x150 +0x114) substitute the driving player instance, then run the box test
// with the standard reach span.
void entity_within_reach(int pA, int pB)
{
    if ((pA != 0) && (*(int *)(pA + 0x150) != 0) &&
        (*(int *)(*(int *)(pA + 0x150) + 0x114) == 0x1000)) {
        pA = slot_find_player_instance();
    }
    entity_within_box((double)FLOAT_8061ad38, pA, pB);
}

// ---------------------------------------------------------------------------
// Build the mirrored/split bone matrices for a task (register-pair ctx). Copies
// the source matrix out, resets identity, applies four translation variants,
// and (when flag bit0 at +0x44 is set) fans the two base matrices out into the
// eight bone slots listed at +0x14.. +0x40.
void objective_build_mirror_matrices(int pSrc, int pDstMtx)
{
    unsigned long long pair = __save_gpr_28();
    int ctx = (int)(unsigned)(pair >> 0x20);
    int rec = (int)pair;

    mtx4_copy_array(*(u32 *)(*(int *)(ctx + 4) + 0x10), pDstMtx,
                    *(int *)(*(int *)(*(int *)(ctx + 4) + 4) + 4));
    mtx4_set_identity(*(int *)(*(int *)(ctx + 4) + 0x10));

    float s = *(float *)(pSrc + 0x2c) * *(float *)(pSrc + 0x24);
    mtx4_set_translation((double)(s), (double)FLOAT_8061ad00, (double)FLOAT_8061ad00);
    mtx4_set_translation((double)(-s), (double)FLOAT_8061ad00, (double)FLOAT_8061ad00);
    mtx4_set_translation((double)(FLOAT_8061ad38 * s), (double)FLOAT_8061ad00, (double)FLOAT_8061ad00);
    mtx4_set_translation((double)(FLOAT_8061ad38 * -s), (double)FLOAT_8061ad00, (double)FLOAT_8061ad00);

    if ((*(u32 *)(rec + 0x44) & 1) != 0) {
        int base = *(int *)(*(int *)(ctx + 4) + 0x10);
        float *m0 = (float *)(base + *(int *)(rec + 0x14) * 0x40);
        float *m1 = (float *)(base + *(int *)(rec + 0x1c) * 0x40);
        mtx4_copy(m0, (float *)(base + *(int *)(rec + 0x28) * 0x40));
        mtx4_copy(m0, (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + *(int *)(rec + 0x2c) * 0x40));
        mtx4_copy(m0, (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + *(int *)(rec + 0x30) * 0x40));
        mtx4_copy(m0, (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + *(int *)(rec + 0x24) * 0x40));
        mtx4_copy(m1, (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + *(int *)(rec + 0x38) * 0x40));
        mtx4_copy(m1, (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + *(int *)(rec + 0x3c) * 0x40));
        mtx4_copy(m1, (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + *(int *)(rec + 0x40) * 0x40));
        mtx4_copy(m1, (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + *(int *)(rec + 0x34) * 0x40));
    }
    __restore_gpr_28();
}
