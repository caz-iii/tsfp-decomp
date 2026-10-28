// AX - GameCube audio synthesizer library init and voice-list allocator.
//
// AX runs the DSP mixing task; each voice is described by a Voice Parameter
// Block (VPB). The VPBs are kept in two per-priority linked-list arrays:
//   DAT_805f3820[prio] = list head, DAT_805f38a0[prio] = list tail.
// A separate free stack (DAT_80613308, chained through voice[2]) holds the
// stopped voices awaiting recycling. This file is the init/teardown path plus
// the voice acquire/free/priority-relink primitives.
//
// Address: 0x803abfb0..0x803ac5c4 | Module: audio/ax.cpp | Date: 2026-09-15

#include "common.h"

// ---- AX voice block ------------------------------------------------------
// [0] next within priority list (also free-list link when idle)
// [1] prev within priority list
// [2] stopped-voice stack link
// [3] priority (index into the head/tail arrays)
// [4] drop/stop callback
// [5] callback user data
// byte at +0x146: "1" => needs the running flag word[8] set
struct AxVoice {
    struct AxVoice* next;   // [0]
    struct AxVoice* prev;   // [1]
    struct AxVoice* stack;  // [2]
    int  priority;          // [3]
    void* drop_cb;          // [4]
    int  user;              // [5]
    int  pad6;
    int  pad7;
    int  running;           // [8]  (word index 8)
};

// ---- SDK version registration -------------------------------------------
extern int os_register_version(u32, u32, u32, u32, u32, u32, u32, u32,
                               void*, u32, u32, u32, u32, u32, u32, u32);
extern void* DAT_80611488;         // "<< Dolphin SDK - AX\trelease build: ..."

extern void os_disable_interrupts(void);
extern int  os_get_interrupts_enabled(void);

// ---- AX subsystem bring-up / teardown helpers ----------------------------
extern void ax_voice_lists_reset(void);         // FUN_803ac0e0
extern void ax_voice_lists_reset_b(void);        // FUN_803ac194 (quit path)
extern int  ax_voice_touch(int voice);           // FUN_803ae814
extern void FUN_803ae858(void);
extern void FUN_803add78(void);
extern void ax_studio_reset(void);               // FUN_803ac5c4
extern AxVoice* ax_voice_stack_pop(void);        // FUN_803ac278 (stopped-voice stack pop)
extern void ax_voice_unlink(AxVoice* v);         // FUN_803ac294 (unlink from priority list)
void        ax_voice_freelist_push(AxVoice* v);  // defined below
extern int  FUN_803ad140(void);
extern int  FUN_803ad538(int, u32, u32, u32, u32, u32, u32, u32, u32);
extern void FUN_803aea5c(void);
extern void FUN_803adda4(void);
extern void FUN_803ac6a8(void);
extern void FUN_803ad164(void);
extern void FUN_803ad8d8(void);

// ---- AX voice-list state -------------------------------------------------
extern AxVoice* DAT_805f3820;      // priority-list head array base
extern AxVoice* DAT_805f38a0;      // priority-list tail array base
extern AxVoice* DAT_805f3824;      // (&DAT_805f3820)[1] as free-stack head
extern AxVoice* DAT_805f38a4;      // (&DAT_805f38a0)[1]
extern AxVoice* DAT_80613308;      // stopped-voice stack head
extern int      DAT_80613310;      // studio state (ax_studio_reset)
extern int      DAT_80613314;
extern int      DAT_80613318;
extern int      DAT_8061331c;
extern int      DAT_80613330;
extern int      DAT_80613334;
extern int      DAT_80613338;
extern int      DAT_8061333c;
extern int      DAT_80613340;
extern void*    DAT_805f3920;      // studio pool a base
extern void*    DAT_805f4fa0;      // studio pool b base

// ax_init_impl: AX synthesizer init. Registers version, brings up voice
// lists, AXOUT/mixer, AUX/SRC, studio, allocates the callback context and
// installs the AX frame callback.
int ax_init_impl(u32 p1, u32 p2, u32 p3, u32 p4, u32 p5, u32 p6, u32 p7,
                 u32 p8, u32 p9, u32 p10, u32 p11, u32 p12, u32 p13, u32 p14,
                 u32 p15, u32 p16) {
    int ctx;
    os_register_version(p1, p2, p3, p4, p5, p6, p7, p8, DAT_80611488,
                        p10, p11, p12, p13, p14, p15, p16);
    ax_voice_lists_reset();
    FUN_803ae858();
    FUN_803add78();
    ax_studio_reset();
    ctx = FUN_803ad140();
    return FUN_803ad538(ctx, p2, p3, p4, p5, p6, p7, p8, p9);
}

// AXInit: public wrapper; init with mode arg 0.
void AXInit(u32 p1, u32 p2, u32 p3, u32 p4, u32 p5, u32 p6, u32 p7, u32 p8,
            u32 p10, u32 p11, u32 p12, u32 p13, u32 p14, u32 p15, u32 p16) {
    ax_init_impl(p1, p2, p3, p4, p5, p6, p7, p8, 0, p10, p11, p12, p13, p14,
                 p15, p16);
}

// AXQuit: tear down the AX subsystems in reverse of ax_init_impl.
void AXQuit(void) {
    ax_voice_lists_reset_b();
    FUN_803aea5c();
    FUN_803adda4();
    FUN_803ac6a8();
    FUN_803ad164();
    FUN_803ad8d8();
}

// ax_voice_by_index: return the VPB pointer stored at slot 'index'.
AxVoice* ax_voice_by_index(int index) {
    return (&DAT_805f3820)[index];
}

// ax_free_stopped_voices: drain the stopped-voice stack, firing each voice's
// drop callback, unlinking it from its priority list, and freeing it.
void ax_free_stopped_voices(void) {
    AxVoice* v;
    v = ax_voice_stack_pop();
    while (v != 0) {
        if (v->priority != 0) {
            if (v->drop_cb != 0) {
                ((void (*)(AxVoice*))v->drop_cb)(v);
            }
            ax_voice_unlink(v);
            ax_voice_freelist_push(v);
        }
        v = ax_voice_stack_pop();
    }
}

// ax_voice_lists_reset: clear the priority-list head/tail arrays and the
// stopped-voice stack. Two 0x40-word blocks are zeroed.
void ax_voice_lists_reset(void) {
    u32* tail;
    u32* head;
    int  i;
    DAT_80613308 = 0;
    i = 2;
    tail = (u32*)&DAT_805f38a0;
    head = (u32*)&DAT_805f3820;
    do {
        for (int k = 0; k < 0x10; ++k) {
            tail[k] = 0;
            head[k] = 0;
        }
        tail += 0x10;
        head += 0x10;
        i = i - 1;
    } while (i != 0);
}

// ax_voice_lists_reset_b: identical reset used on the quit path.
void ax_voice_lists_reset_b(void) {
    u32* tail;
    u32* head;
    int  i;
    DAT_80613308 = 0;
    i = 2;
    tail = (u32*)&DAT_805f38a0;
    head = (u32*)&DAT_805f3820;
    do {
        for (int k = 0; k < 0x10; ++k) {
            tail[k] = 0;
            head[k] = 0;
        }
        tail += 0x10;
        head += 0x10;
        i = i - 1;
    } while (i != 0);
}

// ax_voice_freelist_push: return a voice to the free list and clear priority.
void ax_voice_freelist_push(AxVoice* v) {
    v->next = DAT_805f3820;
    DAT_805f3820 = v;
    v->priority = 0;
}

// ax_studio_reset: reset the AX studio/mixer bookkeeping and zero the two
// studio pools (0x805f3920 and 0x805f4fa0, 0x1e x 0x40-word blocks each).
void ax_studio_reset(void) {
    u32* a;
    u32* b;
    int  i;
    DAT_80613310 = 0;
    DAT_80613314 = 0;
    i = 0x1e;
    DAT_80613318 = 0;
    a = (u32*)&DAT_805f3920;
    b = (u32*)&DAT_805f4fa0;
    DAT_8061331c = 0;
    DAT_80613330 = 0;
    DAT_80613334 = 1;
    DAT_80613338 = 0;
    DAT_8061333c = 1;
    DAT_80613340 = 2;
    do {
        for (int k = 0; k < 0x10; ++k) {
            a[k] = 0;
            b[k] = 0;
        }
        a += 0x10;
        b += 0x10;
        i = i - 1;
    } while (i != 0);
}

// ============================================================================
// batch_1133: AX voice priority-list primitives (stack push/pop, unlink,
// acquire/free/set-priority). Address: 0x803ac268..0x803ac51c.
// ============================================================================

extern int ax_voice_touch(int voice);            // FUN_803ae814 (declared above)

// ax_voice_stack_push: push a voice onto the stopped-voice stack (chained
// through voice[2] / byte offset +8).
void ax_voice_stack_push(int voice) {
    *(int*)(voice + 8) = (int)DAT_80613308;
    DAT_80613308 = (AxVoice*)voice;
}

// ax_voice_stack_pop: pop the head off the stopped-voice stack. The prior
// head value is what callers consume (read before this advances it).
AxVoice* ax_voice_stack_pop(void) {
    AxVoice* head = DAT_80613308;
    if (DAT_80613308 == 0) {
        return head;
    }
    DAT_80613308 = *(AxVoice**)((int)DAT_80613308 + 8);
    return head;
}

// ax_voice_unlink: remove a voice from its priority list, patching the
// head/tail arrays and neighbour links for each of the four cases.
void ax_voice_unlink(AxVoice* v) {
    int  neighbour;
    int* tail;
    int* head;

    head = (int*)&DAT_805f3820 + v->priority;
    tail = (int*)&DAT_805f38a0 + v->priority;
    if (*head == *tail) {                       // sole element
        *tail = 0;
        *head = 0;
        return;
    }
    if ((int*)v == (int*)*head) {               // at head
        *head = ((int*)v)[0];
        *(int*)(*head + 4) = 0;
        return;
    }
    if ((int*)v == (int*)*tail) {               // at tail
        *tail = ((int*)v)[1];
        *(int*)*tail = 0;
        return;
    }
    tail = (int*)((int*)v)[1];                   // middle
    neighbour = ((int*)v)[0];
    *tail = neighbour;
    *(int**)(neighbour + 4) = tail;
}

// AXFreeVoice: release a voice; unlink, set running flag if the VPB requests
// it, touch the DSP task, and return the block to the free list.
void AXFreeVoice(AxVoice* v) {
    os_disable_interrupts();
    ax_voice_unlink(v);
    if (*(short*)((int)v + 0x146) == 1) {
        v->running = 1;
    }
    ax_voice_touch((int)v);
    v->next = DAT_805f3820;
    DAT_805f3820 = v;
    v->priority = 0;
    os_get_interrupts_enabled();
}

// AXAcquireVoice: allocate a voice at 'prio'. Prefer the free list; else
// steal the lowest-priority voice below 'prio'. Link at the priority tail.
AxVoice* AXAcquireVoice(u32 prio, int drop_cb, int user) {
    // The head array (DAT_805f3820) and tail array (DAT_805f38a0) are parallel
    // word arrays whose cells hold voice-block addresses. head_slot/bucket_tail
    // walk those cells; victim is the chosen voice block address.
    int  i;
    int  scan;             // a head-cell value (voice address)
    int  victim;           // chosen voice block address (0 = none)
    int  neighbour;
    int* head_slot;        // &DAT_805f3820[k]
    int* bucket_tail;      // &DAT_805f38a0[k]
    int* tail;

    head_slot = (int*)&DAT_805f3820;
    os_disable_interrupts();
    victim = (int)DAT_805f3820;
    if (DAT_805f3820 == 0) {
        i = (int)prio - 1;
        bucket_tail = (int*)&DAT_805f38a4;
        if (1 < prio) {
            do {
                head_slot = head_slot + 1;
                scan = *head_slot;
                victim = 0;
                if (scan != 0) {
                    int t = *bucket_tail;
                    if (scan == t) {
                        *bucket_tail = 0;
                        *head_slot = 0;
                        victim = scan;
                    } else if (t != 0) {
                        *bucket_tail = ((int*)t)[1];
                        *(int*)*bucket_tail = 0;
                        victim = t;
                    }
                }
                if (victim != 0) {
                    if (*(short*)(victim + 0x146) == 1) {
                        ((int*)victim)[8] = 1;
                    }
                    if (((int*)victim)[4] != 0) {
                        ((void (*)(int))((int*)victim)[4])(victim);
                    }
                    break;
                }
                bucket_tail = bucket_tail + 1;
                i = i - 1;
            } while (i != 0);
        }
    } else {
        DAT_805f3820 = *(AxVoice**)DAT_805f3820;
    }
    if (victim != 0) {
        tail = (int*)&DAT_805f3820 + prio;      // &DAT_805f3820[prio]
        ((int*)victim)[0] = *tail;
        ((int*)victim)[1] = 0;
        neighbour = ((int*)victim)[0];
        if (neighbour == 0) {
            (&DAT_805f38a0)[prio] = (AxVoice*)victim;
            *tail = victim;
        } else {
            *(int*)(neighbour + 4) = victim;
            *tail = victim;
        }
        ((int*)victim)[3] = (int)prio;
        ((int*)victim)[4] = drop_cb;
        ((int*)victim)[5] = user;
        ax_voice_touch(victim);
    }
    os_get_interrupts_enabled();
    return (AxVoice*)victim;
}

// AXSetVoicePriority: relink a voice into a new priority bucket's tail.
void AXSetVoicePriority(AxVoice* v, int prio) {
    int* tail;
    os_disable_interrupts();
    ax_voice_unlink(v);
    tail = (int*)&DAT_805f3820 + prio;
    ((int*)v)[0] = *tail;
    ((int*)v)[1] = 0;
    if (((int*)v)[0] == 0) {
        (&DAT_805f38a0)[prio] = v;
        *tail = (int)v;
    } else {
        *(int**)(*tail + 4) = (int*)v;
        *tail = (int)v;
    }
    v->priority = prio;
    os_get_interrupts_enabled();
}
