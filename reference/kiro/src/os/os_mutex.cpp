// os_mutex.cpp - Dolphin OS mutex ownership cleanup + consistency checks
//
// Address: 0x8039d7fc | Name: os_unlock_all_mutex     | Date: 2026-09-14
// Address: 0x8039d86c | Name: os_mutex_queue_is_valid | Date: 2026-09-14
// Address: 0x8039d96c | Name: os_thread_is_in_queue   | Date: 2026-09-14
// Address: 0x8039d9a4 | Name: os_mutex_owner_is_valid | Date: 2026-09-14
//
// OSMutex bookkeeping over the OSThread structure: each thread keeps a list of
// mutexes it currently owns (head @+0x2f4, tail @+0x2f8) and a link into the
// blocked-on chain (@+0x2f0). A mutex object carries owner @+0, link @+0x10,
// prev/next owned-list links @+0x10/+0x14, and a small waiter tree
// (@+0x2c8 count-marker, @+0x2d0 priority, @+0x2e0/+0x2e4 tree links).
// os_unlock_all_mutex releases every mutex a dying thread still holds;
// os_mutex_queue_is_valid / os_thread_is_in_queue / os_mutex_owner_is_valid are
// the __OSCheck* debug consistency validators.
//
// NON-MATCHING: the raw OSThread/OSMutex field offsets are transcribed verbatim
// from the decompiler; C4311/C4312 pointer-width warnings only.

#include "common.h"

extern "C" {
// Thread queue-node release (returns the freed node to its pool / re-links it).
extern void FUN_803a0204(int *node);
}

// Release every mutex the given thread still owns (walk the owned list @+0x2f4,
// unlink each and clear its owner/links, then free the queue node).
void os_unlock_all_mutex(int thread)
{
    int *mutex;
    int next;

    while (mutex = *(int **)(thread + 0x2f4), mutex != (int *)0x0) {
        next = mutex[4];
        if (next == 0) {
            *(undefined4 *)(thread + 0x2f8) = 0;
        } else {
            *(undefined4 *)(next + 0x14) = 0;
        }
        *(int *)(thread + 0x2f4) = next;
        mutex[3] = 0;
        mutex[2] = 0;
        FUN_803a0204(mutex);
    }
}

// __OSCheckMutexes-style waiter-tree validator: verify the mutex's waiter tree
// links are consistent, every node carries the count-marker 4 (@+0x2c8), and the
// priorities are non-decreasing walking up the tree. Returns 1 if valid.
undefined4 os_mutex_queue_is_valid(int *mutex)
{
    int *pPri;
    bool ordered;
    int node;
    int parent;
    int lastPri;

    node = *mutex;
    if ((node != 0) && (*(int *)(node + 0x2e4) != 0)) {
        return 0;
    }
    lastPri = 0;
    if ((mutex[1] != 0) && (*(int *)(mutex[1] + 0x2e0) != 0)) {
        return 0;
    }
    do {
        if (node == 0) {
            if (mutex[2] == 0) {
                if (mutex[3] != 0) {
                    return 0;
                }
            } else if (mutex[3] < 1) {
                return 0;
            }
            return 1;
        }
        parent = *(int *)(node + 0x2e0);
        if ((parent != 0) && (node != *(int *)(parent + 0x2e4))) {
            return 0;
        }
        if ((*(int *)(node + 0x2e4) != 0) && (node != *(int *)(*(int *)(node + 0x2e4) + 0x2e0))) {
            return 0;
        }
        if (*(short *)(node + 0x2c8) != 4) {
            return 0;
        }
        pPri = (int *)(node + 0x2d0);
        ordered = lastPri <= *pPri;
        node = parent;
        lastPri = *pPri;
    } while (ordered);
    return 0;
}

// Does the blocked-on wait chain starting at thread loop back to itself?
// Follows the blocked-on link (@+0x2f0) then the mutex owner (@+8). Returns 1 on
// a cycle (deadlock).
undefined4 os_thread_is_in_queue(int thread)
{
    int node;

    for (node = *(int *)(thread + 0x2f0);
         (node != 0 && (node = *(int *)(node + 8), node != 0));
         node = *(int *)(node + 0x2f0)) {
        if (node == thread) {
            return 1;
        }
    }
    return 0;
}

// Validate every mutex owned by the given thread: each must list this thread as
// owner (@+8) and pass os_mutex_queue_is_valid. Returns 1 if all valid.
undefined4 os_mutex_owner_is_valid(int thread)
{
    int ok;
    int *mutex;

    mutex = *(int **)(thread + 0x2f4);
    while (true) {
        if (mutex == (int *)0x0) {
            return 1;
        }
        if (mutex[2] != thread)
            break;
        ok = os_mutex_queue_is_valid(mutex);
        if (ok == 0) {
            return 0;
        }
        mutex = (int *)mutex[4];
    }
    return 0;
}
