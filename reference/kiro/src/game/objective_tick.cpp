// Address: 0x80351330 | Name: objective_update_water_volume_sound | Date: 2026-09-12
// Batch: batch_1003
//
// Objective task-progression tick. Continues the objective marker / wave /
// trigger subsystem (objective_markers/waves/triggers.cpp). objective_update_frame
// (0x803519a0) is registered as a per-level update callback.
//
// The task list lives in the objective context DAT_80612db0:
//   [0]      task-array base pointer
//   +0x7c    task states array (4-byte entries; state at +4)
//   [0x1f]   task count            (word)
// Each task record is 0x25e bytes; it holds a count of sub-entities (short at
// +0), a short id list from +0x02, per-entity progress codes at +0xca (stride
// 2), a mode word at +0x66/+0x33*2, a done-flags byte at +0x25c/+0x12e.

#include "common.h"

// ---- raw symbols ----
extern int   DAT_80612610;         // water volume count
extern int   DAT_80611d78;         // water volume table base (0x141c stride)
extern int   DAT_80612db0;         // objective task context (int* array)
extern int   DAT_806108e4;         // per-frame timer delta
extern int   DAT_804821cc;         // task descriptor table base (0x18 stride, 5 entries)
extern int   DAT_80612da8;         // sound source handle slot
extern int   DAT_80612dac;         // active objective sound id (-1 = none)
extern float FLOAT_80612da4;       // smoothed sound pitch state
extern float FLOAT_8061ad00;       // pitch numerator base
extern float FLOAT_8061ad24;       // pitch step
extern float FLOAT_8061ad2c;       // pitch snap target
extern float FLOAT_8061ad30;       // sound base x
extern float FLOAT_8061ad34;       // sound base y

// ---- named cross-module helpers ----
extern int  water_test_position_in_volume(int pVolume, int count, int pList, int pRec);
extern double math_sqrt(double x);
extern void scalar_step_toward_snap(double cur, double target, float *pState);
extern void sound_update_3d_source(double x, double y, double z, int *pHandle,
                                   int a5, int soundId, int a7);
extern int  entity_pool_find_type1(int index);
extern int  entity_pool_find_type2(int index);
extern void objective_tick_triggers(void);

// Register-pair context stubs.
extern "C" int  FUN_803826c0(void);   // task completion predicate callback ptr
extern "C" void FUN_8038270c(void);
extern "C" unsigned long long FUN_803826e8(void);
extern "C" void FUN_80382734(void);
extern "C" unsigned long long FUN_803826d8(void);
extern "C" void FUN_80382724(void);

// ---------------------------------------------------------------------------
// Update the ambient "objective in water" sound: fraction of water volumes the
// listener is inside drives a pitch that is smoothed toward a snap target and
// applied to the active 3D sound source.
void objective_update_water_volume_sound(void)
{
    if (DAT_80612dac != -1) {
        double num = (double)FLOAT_8061ad00;
        double step = (double)FLOAT_8061ad24;
        int off = 0;
        double inside = num;
        for (int i = 0; i < DAT_80612610; i = i + 1) {
            int vol = DAT_80611d78 + off;
            if (-1 < *(int *)(vol + 4)) {
                int hit = water_test_position_in_volume(
                    vol + 0xb4, (int)*(char *)(*(int *)(vol + 0x14) + 0x30),
                    *(int *)(vol + 0x14) + 0x38, vol);
                if (hit != 0) {
                    inside = (double)(float)(inside + step);
                }
                num = (double)(float)(num + step);
            }
            off = off + 0x141c;
        }
        double ratio = math_sqrt((double)(float)(inside / num));
        scalar_step_toward_snap(ratio, (double)FLOAT_8061ad2c, &FLOAT_80612da4);
        sound_update_3d_source(
            (double)(FLOAT_8061ad30 + FLOAT_80612da4), (double)FLOAT_8061ad34,
            (double)FLOAT_8061ad24, &DAT_80612da8, 0, DAT_80612dac, 0);
    }
}

// ---------------------------------------------------------------------------
// Update callback variant: sound only.
void objective_tick_sound_only(void)
{
    objective_update_water_volume_sound();
}

// Update callback variant: sound then trigger tick.
void objective_tick_sound_and_triggers(void)
{
    objective_update_water_volume_sound();
    objective_tick_triggers();
}

// ---------------------------------------------------------------------------
// Advance timed tasks: for each entry in the timed-task list (context +0x9c
// count, +0xa0 base, 0x10 stride) with flag bit 2 set, add the frame delta to
// its accumulator at +0x04.
void objective_advance_timed_tasks(void)
{
    int i = 0;
    int off = 0;
    while (i < *(int *)(DAT_80612db0 + 0x9c)) {
        int entry = *(int *)(DAT_80612db0 + 0xa0) + off;
        if ((*(uint *)(entry + 8) & 2) != 0) {
            *(int *)(entry + 4) = *(int *)(entry + 4) + DAT_806108e4;
        }
        i = i + 1;
        off = off + 0x10;
    }
}

// ---------------------------------------------------------------------------
// Evaluate task completion. For each task (0x25e stride) not already at state 1,
// count how many sub-entities satisfy their progress predicate. Progress code 5
// entries query the entity vtable (+0x10) and update code to 2 (done) or 4
// (partial). When enough entities complete per the task mode (+0x66), mark the
// task record done (+0x12e bit0) and set its state to 1 (all) or 3 (partial).
void objective_evaluate_task_completion(void)
{
    int (*pred)() = (int (*)())FUN_803826c0();
    int stateOff = 0;
    int taskOff = 0;
    int *ctx = (int *)DAT_80612db0;
    for (int t = 0; t < ((int *)DAT_80612db0)[0x1f]; t = t + 1) {
        ctx = (int *)DAT_80612db0;
        bool markDone = false;
        short *task = (short *)(*(int *)DAT_80612db0 + taskOff);
        int completed = 0;
        int fullCount = 0;
        if (*(int *)((int)DAT_80612db0 + stateOff + 4) != 1) {
            int entOff = 0;
            int n = 0;
            for (int e = 0; (n = (int)*task, e < n); e = e + 1) {
                int prog = 0;
                int ent = entity_pool_find_type1((int)*(short *)((int)task + entOff + 2));
                if ((pred == nullptr) || ((*pred)() != 0)) {
                    if (*(short *)((int)task + entOff + 0xca) == 5) {
                        int (**vtbl)(int) = *(int (***)(int))(ent + 0x10);
                        if ((vtbl != nullptr) && ((*(byte *)(ent + 0xc) & 0x40) != 0) &&
                            ((*vtbl)(ent) == 0)) {
                            *(byte *)(ent + 0xc) = *(byte *)(ent + 0xc) & 0xbf;
                        }
                        if ((vtbl != nullptr) && ((*(byte *)(ent + 0xc) & 0x40) == 0)) {
                            prog = (*vtbl)(ent);
                            short mode = *(short *)((int)task + entOff + 0x66);
                            if ((mode == 1) || (mode == 3)) {
                                prog = (prog == 0) ? 1 : 0;
                            }
                        }
                        if (prog != 0) {
                            completed = completed + 1;
                            if (prog == 2) {
                                if (*(short *)((int)task + entOff + 0xca) == 5) {
                                    *(unsigned short *)((int)task + entOff + 0xca) = 2;
                                }
                                fullCount = fullCount + 1;
                            } else if (*(short *)((int)task + entOff + 0xca) == 5) {
                                *(unsigned short *)((int)task + entOff + 0xca) = 4;
                            }
                        }
                    } else {
                        completed = completed + 1;
                    }
                }
                entOff = entOff + 2;
            }

            short taskMode = task[0x33];
            int remaining = (int)*task;
            if (((taskMode == 1) || (taskMode == 0)) && (completed == remaining)) {
                int cnt = 0;
                int o = 0;
                if (0 < remaining) {
                    do {
                        short c = *(short *)((int)task + o + 0xca);
                        if ((c == 2) || (c == 4)) cnt = cnt + 1;
                        o = o + 2;
                        remaining = remaining + -1;
                    } while (remaining != 0);
                }
                markDone = cnt != 0;
            } else if (((taskMode == 3) || (taskMode == 2)) && (completed != 0)) {
                int cnt = 0;
                int o = 0;
                if (0 < remaining) {
                    do {
                        short c = *(short *)((int)task + o + 0xca);
                        if ((c == 2) || (c == 4)) cnt = cnt + 1;
                        o = o + 2;
                        remaining = remaining + -1;
                    } while (remaining != 0);
                }
                markDone = cnt != 0;
            }

            if (markDone) {
                int o = 0;
                for (int e = 0; e < *task; e = e + 1) {
                    int co = o + 0xca;
                    if (*(short *)((int)task + co) == 2) {
                        *(unsigned short *)((int)task + co) = 1;
                    } else if (*(short *)((int)task + co) == 4) {
                        *(unsigned short *)((int)task + co) = 3;
                    }
                    o = o + 2;
                }
                int newState = 3;
                if (fullCount == *task) newState = 1;
                *(u32 *)((int)ctx + stateOff + 4) = newState;
                *(byte *)(task + 0x12e) = *(byte *)(task + 0x12e) | 1;
            }
        }
        stateOff = stateOff + 4;
        taskOff = taskOff + 0x25e;
    }
    FUN_8038270c();
}

// ---------------------------------------------------------------------------
// Notify newly-completed tasks: for each task whose done-flag byte (+0x25c bit0)
// is set, clear it and invoke each listed sub-entity's notify vtable (+0x10).
void objective_notify_completed_tasks(void)
{
    FUN_803826e8();
    int i = 0;
    int taskOff = 0;
    while (i < ((int *)DAT_80612db0)[0x1f]) {
        int task = *(int *)DAT_80612db0 + taskOff;
        if ((*(byte *)(task + 0x25c) & 1) != 0) {
            *(byte *)(task + 0x25c) = *(byte *)(task + 0x25c) & 0xfe;
            int off = 0;
            for (int e = 0; e < *(short *)(task + 0x12e); e = e + 1) {
                int ent = entity_pool_find_type2((int)*(short *)(task + off + 0x130));
                int (**vtbl)() = *(int (***)())(ent + 0x10);
                if (vtbl != nullptr) {
                    (*vtbl)();
                }
                off = off + 2;
            }
        }
        i = i + 1;
        taskOff = taskOff + 0x25e;
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// Finalize completed tasks: for tasks in state 3, walk sub-entities; those
// whose entity state (+4) is 0 with progress code 3 advance to 5; those in
// state 1..2 clear entity flag bit1, advance the code to 5 and set task state 5.
void objective_finalize_completed_tasks(void)
{
    FUN_803826d8();
    int stateOff = 0;
    int taskOff = 0;
    int *ctx;
    for (int t = 0; ctx = (int *)DAT_80612db0, t < ((int *)DAT_80612db0)[0x1f]; t = t + 1) {
        short *task = (short *)(*(int *)DAT_80612db0 + taskOff);
        if (*(int *)((int)DAT_80612db0 + stateOff + 4) == 3) {
            int off = 0;
            for (int e = 0; e < *task; e = e + 1) {
                int ent = entity_pool_find_type1((int)*(short *)((int)task + off + 2));
                int st = *(int *)(ent + 4);
                if (st != 1) {
                    if (st < 1) {
                        if ((-1 < st) && (*(short *)((int)task + off + 0xca) == 3)) {
                            *(unsigned short *)((int)task + off + 0xca) = 5;
                        }
                    } else if ((st < 3) && (*(short *)((int)task + off + 0xca) == 3)) {
                        *(byte *)(ent + 0xc) = *(byte *)(ent + 0xc) & 0xfd;
                        *(unsigned short *)((int)task + off + 0xca) = 5;
                        *(u32 *)((int)ctx + stateOff + 4) = 5;
                    }
                }
                off = off + 2;
            }
        }
        stateOff = stateOff + 4;
        taskOff = taskOff + 0x25e;
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// Full task update chain for one frame.
void objective_tick_all_tasks(void)
{
    objective_advance_timed_tasks();
    objective_evaluate_task_completion();
    objective_notify_completed_tasks();
    objective_finalize_completed_tasks();
    objective_tick_triggers();
}

// ---------------------------------------------------------------------------
// Per-level update callback: ambient sound then the full task chain.
void objective_update_frame(void)
{
    objective_update_water_volume_sound();
    objective_tick_all_tasks();
}

// ---------------------------------------------------------------------------
// Find the task descriptor whose first word matches `key` in the 5-entry table
// at DAT_804821cc (0x18 stride). Returns null if none.
int * objective_find_task_descriptor(int key)
{
    int off = 0;
    int i = 5;
    do {
        if (key == *(int *)((int)&DAT_804821cc + off)) {
            return (int *)((int)&DAT_804821cc + off);
        }
        off = off + 0x18;
        i = i + -1;
    } while (i != 0);
    return (int *)0x0;
}
