// =============================================================================
// spawn_pack_stream.cpp - Character-instance / spawn-pack streaming manager
// Address range: 0x80026648 - 0x80026bb0
// Batch: 394 | Date: 2026-09-05
//
// Continues the animation/actor pipeline (batches 385-393). This module holds
// the per-frame spawn-pack streaming manager that sits behind the pose-eval
// entry point (anim_pose_eval_frame, batch 393): it allocates the actor pools,
// initialises the per-slot descriptor table (DAT_804d1cd0, 0x23 short slots),
// streams the character/anim packs, scales instance timers, and tears the whole
// thing down when the level/mode changes.
//
//   * spawn_pack_commit_despawn    (0x80026648) walk actor list, fire despawn/behaviour, clear slot flags (FUN_80026648)
//   * spawn_pack_frame             (0x800267b8) guarded per-frame wrapper around the setup pass (FUN_800267b8)
//   * spawn_pack_stream_enabled    (0x80026830) is pack streaming active for this mode? (FUN_80026830)
//   * spawn_pack_setup             (0x80026884) main setup: alloc pools, init slot table, stream packs (FUN_80026884)
//   * spawn_pack_scale_timers      (0x80026a44) scale each live instance's +0x18 timer (FUN_80026a44)
//   * spawn_pack_clear_ready       (0x80026a88) clear the "packs loaded" flag (FUN_80026a88)
//   * spawn_pack_alloc_workbuf     (0x80026a94) allocate the 0xc80 streaming work buffer (FUN_80026a94)
//   * spawn_pack_teardown          (0x80026ac4) free pack list + work buffer (FUN_80026ac4)
//   * spawn_pack_open_by_name      (0x80026b34) open a pack by name, link into the live list (FUN_80026b34)
//   * spawn_pack_speed_changed     (0x80026bb0) has the instance's +0x190 speed drifted from base? (FUN_80026bb0)
//
// Compile-clean non-matching reconstruction.
// =============================================================================
#include "common.h"
#include "generated_decls.h"   // mem_alloc, mem_free_top, freelist_init, pack_open,
                               // pack_find_by_name, pack_destroy_slot, loadscreen_set_phase,
                               // cutscene_open_and_parse, stream_process_callbacks,
                               // entity_destroy_with_event, aiactor_run_behaviour_phase

// NOTE: memset_wrapper, resource_pool_clear_ptrs, anim_start_clip,
// anim_primary_clip_has_flags, anim_secondary_clip_has_flags are named
// cross-module helpers that resolve through the shared headers
// (generated_externs.h / generated_decls.h); do not re-declare them here.

// ---- raw runtime helpers (kept raw: different arity/type across TUs) --------
extern int  FUN_803826f0(void);           // frame-scope open  (returns "cutscene active")
extern void FUN_8038273c(void);           // frame-scope close
extern void FUN_801baaac(void);           // post-pool init (character side)
extern void FUN_801baa78(void);           // post-stream init (character side)
extern void FUN_801bf3ac(void);           // finalise streamed packs
extern void FUN_80364428(void);           // reset a global streaming sub-state
extern void FUN_801849e0(void);           // release pack-index resources
extern int  FUN_801c2e1c(void);           // mode gate probe (returns nonzero when enabled)

// ---- raw runtime globals ---------------------------------------------------
extern int*  DAT_80540a44;     // head of the live actor linked list
extern int*  DAT_80611c50;     // current-actor cursor (walk state)
extern int   DAT_80611824;     // instance-pointer table base (idx*4)
extern short DAT_804d1cd0;     // per-slot descriptor index table (0x23 shorts)
extern uint  DAT_804d34d8;     // per-anim flag table word 0 (idx*8)
extern int   DAT_806117d8;     // live slot count (working)
extern int   DAT_806117dc;     // live slot count (committed)
extern int   DAT_80611820;     // free-list head (working)
extern int   DAT_8061181c;     // free-list head (committed)
extern int   DAT_806117e0;     // instance pool base allocation
extern int   DAT_806117c4;     // streaming work-buffer base
extern int   DAT_806117c8;     // streaming work-buffer cursor
extern int   DAT_806117cc;     // head of the open-pack linked list
extern int   DAT_806117ec;     // "streaming in progress" flag
extern int   DAT_806117f0;     // "setup complete" flag
extern int   DAT_80611800;     // "packs already loaded" latch
extern int   DAT_80611930;     // deferred-clear request flag
extern int   DAT_80612614;     // level/mode id
extern char  DAT_805015bd;     // region/mode selector ('\n' special)
extern int   DAT_80501603;     // cutscene id (-1 == none)
extern uint  DAT_805015c0;     // global mode flags
extern int   DAT_804e652c;     // instance count (reset target)
extern int   DAT_804e6530;     // instance table storage (address taken)
extern int   DAT_804d5238;     // freelist pool storage
extern int   DAT_804d5248;     // freelist node storage

extern float FLOAT_80613930;   // base instance speed (compare value)
extern float FLOAT_80613944;   // default clip-blend value
extern float FLOAT_806139e8;   // per-frame timer scale factor

// Absolute memset targets used by the setup pass (Ghidra rendered as signed).
#define SPAWN_SLOTFLAGS_BASE  ((int)0x804d1cd0)   // -0x7fb2e330
#define SPAWN_INSTANCE_BASE   ((int)0x804e652c)   // -0x7fb19ad4
#define SPAWN_WORK_BASE       ((int)0x804d34d8)   // -0x7fb2cb28

// -----------------------------------------------------------------------------
// spawn_pack_commit_despawn (0x80026648)
// Walk the live actor list: any actor whose active clip carries the 0x4000
// (despawn) flag is either handed to its behaviour phase (class 8) or destroyed.
// Then sweep the slot table clearing any 0x4000-flagged instance, and commit the
// working slot/free-list counters.
// -----------------------------------------------------------------------------
void spawn_pack_commit_despawn(int animIdx)
{
    int* actor;
    int  slotIdx;
    int  flagFound;
    long long behaviourResult;
    short descIdx;

    actor = DAT_80540a44;
    while (actor != (int*)0) {
        DAT_80611c50 = actor;
        if (*(int*)(actor[1] + 0xcc) != 0) {
            flagFound = anim_primary_clip_has_flags((int)actor, animIdx, 0x4000, 0);
            if (flagFound == 0) {
                flagFound = anim_secondary_clip_has_flags((int)actor, animIdx, 0x4000, 0);
            }
            if (flagFound != 0) {
                if (actor[8] == 8) {
                    aiactor_run_behaviour_phase((u32)(uintptr_t)actor, 0x3eb,
                                                (int*)(actor[1] + 0xcc), 0);
                    // class-8 actors that report "not finished" get a fresh clip
                    anim_start_clip(FLOAT_80613930, FLOAT_80613944);
                } else {
                    entity_destroy_with_event(actor);
                }
            }
        }
        actor = (int*)*DAT_80611c50;
    }

    DAT_80611c50 = (int*)0;
    slotIdx = 0;
    for (int i = 0xbfe; i != 0; i--) {
        descIdx = *(short*)((int)&DAT_804d1cd0 + slotIdx);
        if (-1 < descIdx) {
            int instOff = descIdx * 4;
            if ((*(int*)(DAT_80611824 + instOff) != 0) &&
                ((*(uint*)((int)&DAT_804d34d8 + descIdx * 8) & 0x4000) != 0)) {
                *(int*)(DAT_80611824 + instOff) = 0;
                *(short*)((int)&DAT_804d1cd0 + slotIdx) = (short)0xffff;
            }
        }
        slotIdx += 2;
    }

    DAT_806117d8 = DAT_806117dc;
    DAT_80611820 = DAT_8061181c;
    (void)behaviourResult;
}

// -----------------------------------------------------------------------------
// spawn_pack_setup (0x80026884)
// Main setup pass (fwd-declared for the wrapper below).
// -----------------------------------------------------------------------------
void spawn_pack_setup(int actorList, float blendA, float blendB);

// -----------------------------------------------------------------------------
// spawn_pack_frame (0x800267b8)
// Guarded per-frame wrapper: optionally flushes stale resource pointers when the
// mode calls for it, then runs the setup pass.
// -----------------------------------------------------------------------------
void spawn_pack_frame(int actorList, float blendA, float blendB)
{
    DAT_806117ec = 0;
    if (DAT_80611930 == 0) {
        spawn_pack_setup(actorList, blendA, blendB);
    } else {
        if (((DAT_805015bd == '\n') || (DAT_80612614 == 0x66)) && (DAT_80611800 == 0)) {
            actorList = resource_pool_clear_ptrs();
        }
        spawn_pack_setup(actorList, blendA, blendB);
    }
    DAT_806117f0 = 0;
}

// -----------------------------------------------------------------------------
// spawn_pack_stream_enabled (0x80026830)
// Streaming is enabled unless the mode id is a "no-stream" cinematic mode, or the
// packs have already been latched as loaded.
// -----------------------------------------------------------------------------
int spawn_pack_stream_enabled(void)
{
    int enabled = 0;
    if (((DAT_80612614 < 0x65) || (FUN_801c2e1c() != 0)) && (DAT_80611800 == 0)) {
        enabled = 1;
    }
    return enabled;
}

// -----------------------------------------------------------------------------
// spawn_pack_alloc_workbuf (0x80026a94)
// Allocate the 0xc80-byte streaming work buffer and reset its cursor.
// -----------------------------------------------------------------------------
void spawn_pack_alloc_workbuf(void)
{
    DAT_806117c4 = mem_alloc(0xc80, 0);
    DAT_806117c8 = DAT_806117c4;
}

// -----------------------------------------------------------------------------
// spawn_pack_teardown (0x80026ac4)
// Free every open pack in the linked list and the streaming work buffer.
// -----------------------------------------------------------------------------
void spawn_pack_teardown(void)
{
    int* node = (int*)DAT_806117cc;
    while (node != (int*)0) {
        int next = *node;
        mem_free_top((int)node);
        pack_destroy_slot(node[1]);
        node = (int*)next;
    }
    DAT_806117cc = 0;
    FUN_801849e0();
    mem_free_top(DAT_806117c4);
    DAT_806117c4 = 0;
}

// -----------------------------------------------------------------------------
// spawn_pack_setup (0x80026884)
// Allocate the instance pool + slot table, stream the character/anim packs, run
// the character-side init hooks, and commit the working counters. Guarded so the
// heavy allocation only happens the first time (or after an explicit reset flag).
// -----------------------------------------------------------------------------
void spawn_pack_setup(int actorList, float blendA, float blendB)
{
    int  cutsceneActive;
    int  slotIdx;
    bool didStream;

    (void)actorList;
    (void)blendA;
    (void)blendB;

    cutsceneActive = FUN_803826f0();
    if (DAT_80612614 == 0x67) {
        FUN_8038273c();
        return;
    }

    slotIdx = 0;
    DAT_804e652c = 0;
    DAT_80611824 = (int)&DAT_804e6530;

    if (((DAT_805015c0 & 0x20000000) != 0) || (spawn_pack_stream_enabled() != 0)) {
        DAT_806117d8 = 0;
        memset_wrapper(SPAWN_SLOTFLAGS_BASE, 0xff, 0x1806);
        memset_wrapper(SPAWN_INSTANCE_BASE, 0, 0xeb4);
        memset_wrapper(SPAWN_WORK_BASE, 0, 0x1d60);
        DAT_806117e0 = mem_alloc(0x1400, 1);
        DAT_80611820 = 0;
        DAT_8061181c = 0;
        slotIdx = 0;
        for (int i = 0, n = 0x23; n != 0; n--, i++) {
            *(short*)((int)&DAT_804d1cd0 + slotIdx) = (short)i;
            slotIdx += 2;
        }
        DAT_806117d8 = 0x23;
    }

    didStream = (spawn_pack_stream_enabled() != 0);
    if (didStream) {
        loadscreen_set_phase(3);
        spawn_pack_alloc_workbuf();
        FUN_801baaac();
    }

    if (cutsceneActive != 0) {
        if (DAT_80501603 != -1) {
            cutscene_open_and_parse();
        }
        stream_process_callbacks();
    }

    if ((DAT_80611800 == 0) && (DAT_80612614 != 0x6a)) {
        if (!didStream) {
            spawn_pack_alloc_workbuf();
        }
        didStream = true;
        FUN_801baa78();
    }

    cutsceneActive = spawn_pack_stream_enabled();
    if (cutsceneActive != 0) {
        FUN_801bf3ac();
        FUN_80364428();
        freelist_init((void**)&DAT_804d5238, 0xf8, (void*)&DAT_804d5248, 10);
        DAT_80611800 = 1;
    }

    if (didStream) {
        spawn_pack_teardown();
        DAT_806117dc = DAT_806117d8;
        DAT_8061181c = DAT_80611820;
    }

    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// spawn_pack_scale_timers (0x80026a44)
// Multiply the +0x18 timer of every live instance by the per-frame scale factor.
// -----------------------------------------------------------------------------
void spawn_pack_scale_timers(void)
{
    float scale = FLOAT_806139e8;
    int   off = 0;
    for (int i = 0; i < DAT_806117d8; i++) {
        int inst = *(int*)(DAT_80611824 + off);
        if (inst != 0) {
            *(float*)(inst + 0x18) = *(float*)(inst + 0x18) * scale;
        }
        off += 4;
    }
}

// -----------------------------------------------------------------------------
// spawn_pack_clear_ready (0x80026a88)
// Clear the "packs already loaded" latch so the next setup re-streams.
// -----------------------------------------------------------------------------
void spawn_pack_clear_ready(void)
{
    DAT_80611800 = 0;
}

// -----------------------------------------------------------------------------
// spawn_pack_open_by_name (0x80026b34)
// Open a pack by name (if not already open) and link its slot handle into the
// live pack list. Returns 1 on success, 0 on failure.
// -----------------------------------------------------------------------------
uint spawn_pack_open_by_name(const char* name)
{
    uint  slot;
    int*  node;

    slot = (uint)pack_find_by_name(name);
    if (slot == 0xffffffff) {
        pack_open(name, 1, 0);
        slot = (uint)pack_find_by_name(name);
        if (-1 < (int)slot) {
            node = (int*)mem_alloc(8, 0);
            *node = DAT_806117cc;
            DAT_806117cc = (int)node;
            node[1] = (int)slot;
        }
    }
    return (slot >> 0x1f) ^ 1;
}

// -----------------------------------------------------------------------------
// spawn_pack_speed_changed (0x80026bb0)
// True when the instance's playback-speed field (+0x190 == +400) has drifted
// away from the base value.
// -----------------------------------------------------------------------------
bool spawn_pack_speed_changed(int inst)
{
    return FLOAT_80613930 != *(float*)(inst + 400);
}
