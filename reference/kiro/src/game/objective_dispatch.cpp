// Batch: batch_998 | Module: objective slot handler dispatch + position-gather callbacks
// Address range: 0x8034eb18 - 0x8034f10c
// Date: 2026-09-12
//
// The objective-slot handler layer under the batch_997 progression driver. Each objective
// slot type has a row in the handler table DAT_80482154 (8 rows, stride 0xc): word[0] = type
// id, word[1] = secondary/update handler, word[2] = gather handler. objective_handler_lookup
// finds the row; objective_invoke_gather / objective_invoke_secondary call the two handlers.
// The gather handlers (objective_gather_*) each fill up to three parallel output arrays -- an
// entity-pointer array, a world-position triple array, and an id array -- via the shared
// objective_write_slot_triple helper, one row per resolved entity.
//
//   objective_wave_evaluate       0x8034eb18  evaluate the active wave: per tracked slot, clear
//                                             completion (objective_slot_check_clear) and count
//                                             done slots (objective_slot_evaluate), then tick.
//   objective_write_slot_triple   0x8034ebf4  write {entity ptr, position triple, id} into the
//                                             three optional output arrays at row `index`.
//   objective_gather_raycast_target 0x8034ec40 gather the slot's target: use the entity's aim
//                                             point, or a downward scene_raycast hit, plus its
//                                             muzzle offset; resolve nearest zone entity if unset.
//   objective_gather_zone_entities 0x8034edb4 gather every unfilled entity in the slot's zone
//                                             record (soundpad zone -> actor table position).
//   objective_gather_single_entity 0x8034ee6c gather the single owner entity's position.
//   objective_gather_matching_players 0x8034eefc gather up to 10 players matching the slot's
//                                             award filter (award_collect_matching_players).
//   objective_marker_resolves     0x8034f050  true if the slot's marker id (+0xc) resolves to a
//                                             live entity handle.
//   objective_handler_lookup      0x8034f088  find the handler-table row (DAT_80482154, stride
//                                             0xc, 8 rows) whose type id (word[0]) matches.
//   objective_invoke_gather       0x8034f0c8  invoke the slot type's gather handler (row word[2]).
//   objective_invoke_secondary    0x8034f10c  when the slot is not yet done, invoke its secondary
//                                             handler (row word[1]).
//
// NON-MATCHING: reproduces the decompiler's structure. Register save-pair values recovered by
// the decompiler are expressed as explicit locals seeded from the level-context accessors
// (FUN_803826dc / FUN_803826e8) (Compile-Clean rule 3). mem_alloc's ABI-widened argument tail
// in objective_gather_matching_players is dropped in favor of its canonical (size,type)
// prototype (rule 1/6). Named helpers resolve through shared headers (rule 6); raw callees and
// the handler/data tables are declared file-locally (rule 5).

#include "common.h"

extern "C" {

// ---- named cross-module helpers --------------------------------------------
extern int  objective_slot_check_clear(int rec);           // batch_997
extern unsigned int objective_slot_evaluate(int rec);      // batch_997
extern void objective_progression_tick(unsigned int, unsigned int, int); // batch_997
extern int  entity_handle_resolve(int id);
extern int  soundpad_resolve_entity_to_zone(int entityId);
extern int  soundpad_find_nearest_entity_in_zone(double thresh, float* pos, int zone);
extern void scene_raycast(unsigned int origin, int dirPtr, float* endPtr, int mask,
                          float* outHit, float* outNormal, int* outSurface,
                          int a9, int a10, int* a11);
extern int  entdesc_field1_cur(int typeId);
extern int  award_collect_matching_players(void* filter, int outList, int a3, int desc);
extern int  mem_alloc(int size, int type);
extern void mem_free_top(int ptr);
extern int  zone_slot_record_by_entity(int entityId);
extern int  worm_class_find_first_active_index(void);

// ---- raw callees (file-local) ----------------------------------------------
extern int  FUN_803826dc(void);   // acquire wave/objective cursor
extern void FUN_80382728(void);   // release (paired with 803826dc)
extern int  FUN_803826e8(void);   // acquire slot-record cursor
extern void FUN_80382734(void);   // release (paired with 803826e8)
extern void FUN_803501b4(void);   // objective refresh after state change
extern void FUN_8034f9a0(void);   // tracker completion-notify
extern int  FUN_80352cb8(int ent);// aim-mode test (0 => use stored aim point)

// ---- data symbols ----------------------------------------------------------
extern int*   DAT_80612da0;     // wave dispatch table (entry = tracked-list header)
extern char   DAT_80501572;     // story-mode / progression flag
extern int    DAT_8061208c;     // objective descriptor block base
extern int*   DAT_806120c4;     // "force-select" flag ptr
extern int    DAT_80611b8c;     // actor record table base (stride 0x20)
extern int    DAT_80482154;     // objective handler table base (8 rows, stride 0xc)
extern void*  PTR_DAT_80477bd0; // per-type award filter pointer table base (stride 0x31 words)
extern char   DAT_80477bc8;     // filter table sentinel/base
extern float  FLOAT_8061ad00;   // alive threshold
extern float  FLOAT_8061ad24;   // raycast height bias
extern float  DAT_803e0bc0;     // downward ray direction .x
extern float  DAT_803e0bc4;     // .y
extern float  DAT_803e0bc8;     // .z

// forward decls
int* objective_handler_lookup(int typeId);

// ---------------------------------------------------------------------------
// 0x8034eb18 : evaluate the active objective wave. For each tracked slot in the
// active wave's list (dispatch entry for worm_class_find_first_active_index),
// clear its completion flag (and notify in story mode) then count the slots that
// evaluate to "done"; finally tick the progression with the done count.
// ---------------------------------------------------------------------------
void objective_wave_evaluate(void)
{
    FUN_803826dc();
    int wave = worm_class_find_first_active_index();
    int doneCount = 0;
    int header = *(int*)(DAT_80612da0 + wave * 4);
    if (header != 0) {
        int count = *(int*)(header + 4);
        int off = 0;
        for (int i = 0; i < count; i++) {
            int slot = **(int**)(wave * 4 + DAT_80612da0) + off;
            if ((objective_slot_check_clear(slot) != 0) && ('\0' < DAT_80501572)) {
                FUN_8034f9a0();
                FUN_803501b4();
            }
            if (objective_slot_evaluate(slot) != 0) {
                doneCount = doneCount + 1;
            }
            off = off + 0xc;
        }
        objective_progression_tick(wave, doneCount, count);
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x8034ebf4 : write one output row at `index` into up to three optional arrays:
//   entArr[index]      = ent        (stride 4)
//   posArr[index]      = pos triple  (stride 0xc)
//   idArr[index]       = id          (stride 4)
// ---------------------------------------------------------------------------
void objective_write_slot_triple(int entArr, unsigned int ent, int posArr, unsigned int* pos,
                                 int idArr, unsigned int id, int index)
{
    if (entArr != 0) {
        *(unsigned int*)(entArr + index * 4) = ent;
    }
    if (posArr != 0) {
        *(unsigned int*)(posArr + index * 0xc) = pos[0];
        int row = posArr + index * 0xc;
        *(unsigned int*)(row + 4) = pos[1];
        *(unsigned int*)(row + 8) = pos[2];
    }
    if (idArr != 0) {
        *(unsigned int*)(idArr + index * 4) = id;
    }
}

// ---------------------------------------------------------------------------
// 0x8034ec40 : gather the slot's target position. The slot entity arrives via
// the r28 save-pair. Uses the stored aim point when in aim mode, else a downward
// scene raycast; adds the entity's muzzle offset (+0x1a8..0x1b0); resolves the
// nearest zone entity into +0x110 when unset. Writes one output row.
// ---------------------------------------------------------------------------
void objective_gather_raycast_target(unsigned int p1, unsigned int p2,
                                     int posArr, int entArr, int index)
{
    (void)p1; (void)p2;
    int cur = FUN_803826e8();
    int ent = *(int*)cur;                 // slot entity (from r28 save-pair)
    int surface;
    float endPt[3];
    float hitZ;
    float hitY;
    unsigned int hitPad;
    float aimX, aimY, aimZ;
    float outX, outY, outZ;

    if ((DAT_806120c4 != 0) && (*DAT_806120c4 != 0)) {
        *(unsigned int*)(ent + 0x28) = *(unsigned int*)(ent + 0x28) | 0x10;
    }
    if (FUN_80352cb8(ent) == 0) {
        surface = *(int*)(ent + 0x34);
        outX = *(float*)(ent + 100);
        outY = *(float*)(ent + 0x68);
        (void)outX; (void)outY;
        *(unsigned int*)&aimZ = *(unsigned int*)(ent + 0x6c);
        aimX = *(float*)(ent + 100);
        aimY = *(float*)(ent + 0x68);
    } else {
        aimX = DAT_803e0bc0;
        *(unsigned int*)&aimY = *(unsigned int*)&DAT_803e0bc4;
        *(unsigned int*)&aimZ = *(unsigned int*)&DAT_803e0bc8;
        scene_raycast(*(unsigned int*)(ent + 0x34), ent + 100, &aimX, 1, &hitZ, endPt,
                      &surface, 0, 0, (int*)0);
        aimY = FLOAT_8061ad24 + hitY;
        aimX = hitZ;
        *(unsigned int*)&aimZ = hitPad;
    }
    outX = *(float*)(ent + 0x1a8) + *(float*)(ent + 100);
    outY = *(float*)(ent + 0x1ac) + *(float*)(ent + 0x68);
    outZ = *(float*)(ent + 0x1b0) + *(float*)(ent + 0x6c);
    (void)aimX; (void)aimY; (void)aimZ;
    if (*(int*)(ent + 0x110) < 0) {
        int nearest = soundpad_find_nearest_entity_in_zone((double)FLOAT_8061ad00, &outX, surface);
        *(int*)(ent + 0x110) = nearest;
    }
    unsigned int posv[3];
    posv[0] = *(unsigned int*)&outX;
    posv[1] = *(unsigned int*)&outY;
    posv[2] = *(unsigned int*)&outZ;
    objective_write_slot_triple(entArr, (unsigned int)ent, posArr, posv,
                                cur, *(unsigned int*)(ent + 0x110), index);
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x8034edb4 : gather every unfilled entity in the slot's zone record. The slot
// entity arrives via r28; its zone record (+4 count, +8 entity list, +0x30 fill
// flags) is walked and each unfilled member's actor position is written.
// ---------------------------------------------------------------------------
void objective_gather_zone_entities(unsigned int p1, unsigned int p2,
                                    int posArr, int entArr, int index)
{
    (void)p1; (void)p2;
    int cur = FUN_803826dc();
    int rec = zone_slot_record_by_entity(*(int*)(cur + 8));
    int off = 0;
    for (int i = 0; i < *(int*)(rec + 4); i++) {
        int zone = soundpad_resolve_entity_to_zone(*(int*)(rec + off + 8));
        int actorIdx = *(short*)(zone + 8) + -0x3e9;
        if (*(int*)(rec + off + 0x30) == 0) {
            objective_write_slot_triple(entArr, 0, posArr,
                                        (unsigned int*)(DAT_80611b8c + actorIdx * 0x20 + 0xc),
                                        cur, actorIdx, index);
            index = index + 1;
        }
        off = off + 4;
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x8034ee6c : gather the single owner entity's world position. Clears the "in
// zone" flag bit (0x10 of +0x28) and writes one row using the AI record id.
// ---------------------------------------------------------------------------
int objective_gather_single_entity(int* slot, int cur, int posArr, int entArr, int index)
{
    int ent = *slot;
    *(unsigned int*)(ent + 0x28) = *(unsigned int*)(ent + 0x28) & 0xffffffef;
    float pos[3];
    pos[0] = *(float*)(ent + 0x1a8) + *(float*)(ent + 100);
    pos[1] = *(float*)(ent + 0x1ac) + *(float*)(ent + 0x68);
    pos[2] = *(float*)(ent + 0x1b0) + *(float*)(ent + 0x6c);
    objective_write_slot_triple(entArr, (unsigned int)ent, posArr, (unsigned int*)pos,
                                cur, *(unsigned int*)(*(int*)(ent + 0x7c) + 4), index);
    return index + 1;
}

// ---------------------------------------------------------------------------
// 0x8034eefc : gather up to 10 players matching the slot's award filter. Looks
// up the filter pointer (PTR_DAT_80477bd0 row) by the slot's award type, counts
// matches, allocates a scratch list, collects them, and writes one row each.
// ---------------------------------------------------------------------------
void objective_gather_matching_players(unsigned long long a1, unsigned long long a2,
                                       unsigned long long a3, unsigned long long a4,
                                       unsigned long long a5, unsigned long long a6,
                                       unsigned long long a7, unsigned long long a8,
                                       unsigned int p9, unsigned int p10,
                                       int posArr, int entArr, int index)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    (void)p9; (void)p10;
    int cur = FUN_803826dc();
    int slot = *(int*)(cur + 4);
    int awardType = *(int*)(*(int*)(DAT_8061208c + 0x128) +
                            *(short*)(slot + 0xc) * 0x8c + 0x84);
    void* filter = (void*)(&PTR_DAT_80477bd0)[awardType * 0x31];
    int desc = entdesc_field1_cur(awardType);
    int count = award_collect_matching_players(filter, 0, 0, desc);
    if (10 < count) {
        count = 10;
    }
    if (count != 0) {
        int list = mem_alloc((count * 4 + 0xf) & 0xfffffff0, 1);
        award_collect_matching_players(filter, list, 0, desc);
        int off = 0;
        for (int i = 0; i < count; i++) {
            int ent = **(int**)(list + off);
            float pos[3];
            pos[0] = *(float*)(ent + 0x1a8) + *(float*)(ent + 100);
            pos[1] = *(float*)(ent + 0x1ac) + *(float*)(ent + 0x68);
            pos[2] = *(float*)(ent + 0x1b0) + *(float*)(ent + 0x6c);
            objective_write_slot_triple(entArr, (unsigned int)ent, posArr, (unsigned int*)pos,
                                        cur, (unsigned int)(*(int**)(list + off))[0xe], index);
            index = index + 1;
            off = off + 4;
        }
        mem_free_top(list);
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x8034f050 : true if the slot's marker id (+0xc) resolves to a live entity.
// ---------------------------------------------------------------------------
bool objective_marker_resolves(int slot)
{
    int ent = entity_handle_resolve((int)*(short*)(slot + 0xc) + 1);
    return (bool)('\x01' - (ent == 0));
}

// ---------------------------------------------------------------------------
// 0x8034f088 : find the handler-table row (DAT_80482154, 8 rows, stride 0xc)
// whose type id (word[0]) matches typeId; 0 if none.
// ---------------------------------------------------------------------------
int* objective_handler_lookup(int typeId)
{
    int off = 0;
    int remaining = 8;
    do {
        if (*(int*)((int)&DAT_80482154 + off) == typeId) {
            return (int*)((int)&DAT_80482154 + off);
        }
        off = off + 0xc;
        remaining = remaining - 1;
    } while (remaining != 0);
    return (int*)0;
}

// ---------------------------------------------------------------------------
// 0x8034f0c8 : invoke the slot type's gather handler (row word[2]) for a cell.
// (This is the "linked" predicate objective_collect_linked_cells passes.)
// ---------------------------------------------------------------------------
void objective_invoke_gather(int cell)
{
    int* row = objective_handler_lookup(*(int*)(cell + 8));
    (*(void(**)(int))&row[2])(cell);
}

// ---------------------------------------------------------------------------
// 0x8034f10c : when the slot is not yet done (objective_slot_evaluate == 0),
// invoke its secondary handler (row word[1]). Slot record arrives via r28.
// ---------------------------------------------------------------------------
void objective_invoke_secondary(unsigned int p1, unsigned int p2, unsigned int p3,
                                unsigned int p4, unsigned int p5)
{
    (void)p1; (void)p2;
    int cur = FUN_803826e8();
    if (objective_slot_evaluate(cur) == 0) {
        int* row = objective_handler_lookup(*(int*)(*(int*)(cur + 4) + 8));
        if ((void*)row[1] != 0) {
            (*(void(**)(int, int, unsigned int, unsigned int, unsigned int))&row[1])
                (cur, cur, p3, p4, p5);
        }
    }
    FUN_80382734();
}

} // extern "C"
