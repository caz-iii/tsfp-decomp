// level_slot_dispatch.cpp
// Level spawn-slot membership queries, spawn-slot descriptor construction,
// a HUD text word-wrap helper, and trigger/entity setup dispatch by type.
//
// Globals:
//   DAT_805c7c14 : count of spawn-group descriptors
//   DAT_805c7c18 : base of spawn-group descriptor array (stride 0x220)
//   DAT_805c7c10 : base of spawn-slot records (stride 0xb4)
//   DAT_80612614 : active level id
//   FLOAT_8061ad04/08, DOUBLE_8061ad10 : text-wrap width tuning constants

#include "common.h"
#include "text.h"
#include "entity.h"

// ---- per-file externs ----
extern int    DAT_805c7c10;
extern int    DAT_805c7c14;
extern int    DAT_805c7c18;
extern int    DAT_80612614;
extern float  FLOAT_8061ad04;
extern float  FLOAT_8061ad08;
extern double DOUBLE_8061ad10;

extern int    FUN_803826e4(void);            // acquire working text buffer
extern void   FUN_80382730(void);            // release working text buffer
extern void   memset_wrapper(int dst, int val, int len);
extern int    session_get_state_flag(void);
extern void   text_set_font_table(int table);
extern double text_get_char_width(unsigned char* s);
extern char*  FUN_8038b308(int s, char ch);  // strchr-like (forward)
extern char*  FUN_8038b2c0(int s, char ch);  // strchr-like (line scan)
extern unsigned int FUN_8038b5c8(int s);     // string length
extern void   FUN_8038b4cc(int dst, int src, unsigned int n);  // strncpy-like
extern void   FUN_8038b4a0(int dst, int src);                  // strcat-like
extern void   FUN_8038b510(int* dst, int* src);                // string copy record

extern unsigned long long FUN_803826f0(void);  // acquire slot cursor (hi=slot ptr, lo=ctx base)
extern void   FUN_8038273c(void);            // release slot cursor
extern int    entity_handle_resolve(int handle);
extern int    trigger_slot_resolve_entity(unsigned int handle);
extern void*  level_get_info_cached(int levelId);  // canonical: returns level-info ptr
extern void   FUN_8034ac28(int entity, int arg);   // type-0x18 trigger setup body
extern void   FUN_8034d50c(int entity);            // type-0x46 trigger body
extern void   FUN_8034ad00(int entity);            // type-0x15 trigger body

// Returns 1 if the given spawn-slot record is referenced by any spawn-group
// descriptor (either its type-1 member list at +0x50/+0x54 or its type-2 list
// at +0x88/+0x8c). Type <= 0 group entries always match (open group).
unsigned int spawn_slot_contains_ref(int* slotRec)
{
    int groupIdx = 0;
    int off = 0;
    do {
        if (DAT_805c7c14 <= groupIdx) {
            return 0;
        }
        int type = *slotRec;
        int grp = DAT_805c7c18 + off;
        if (type == 1) {
            int n = *(int*)(grp + 0x50);
            int mo = 0;
            while (n > 0) {
                if (slotRec == (int*)(DAT_805c7c10 + *(int*)(grp + mo + 0x54) * 0xb4)) {
                    return 1;
                }
                mo += 4;
                n--;
            }
        } else if (type < 1) {
            if (type >= 0) {
                return 1;
            }
        } else if (type < 3) {
            int n = *(int*)(grp + 0x88);
            int mo = 0;
            while (n > 0) {
                if (slotRec == (int*)(DAT_805c7c10 + *(int*)(grp + mo + 0x8c) * 0xb4)) {
                    return 1;
                }
                mo += 4;
                n--;
            }
        }
        groupIdx++;
        off += 0x220;
    } while (true);
}

// Returns flag bit 0x20 when the slot has any of the pickup-class flags
// (0x3c mask at byte +0x2c) set and is NOT already referenced by a group.
unsigned int spawn_slot_pickup_flag(int* slotRec)
{
    unsigned int flag = 0;
    if ((*(unsigned char*)((char*)slotRec + 0x2c * 4) & 0x3c) != 0 &&
        spawn_slot_contains_ref(slotRec) == 0) {
        flag = 0x20;
    }
    return flag;
}

// Build a compact spawn-slot descriptor from the current cursor slot into the
// caller's 9-word record: [0]=index (-1 if not found), [1]=field1, [2]=short
// field, [3]=pickup flags, [4]=0, [6]=5, [8]=0.
void spawn_slot_build_descriptor(unsigned int /*unused1*/, unsigned int /*unused2*/, int* out)
{
    unsigned long long acquired = FUN_803826f0();
    int* cursorSlot = (int*)(unsigned int)(acquired >> 0x20);  // high half: slot pointer
    int  ctxBase   = (int)(unsigned int)acquired;              // low half: level slot context

    out[1] = cursorSlot[1];
    out[2] = (int)*(short*)(cursorSlot + 3);
    out[0] = -1;
    unsigned int flags = spawn_slot_pickup_flag(cursorSlot);
    *(char*)(out + 3) = (char)flags;
    out[6] = 5;
    out[4] = 0;
    out[8] = 0;

    int off = 0;
    for (int i = 0; i < *(int*)(ctxBase + 300); i++) {
        if (cursorSlot == (int*)(*(int*)(ctxBase + 0x130) + off)) {
            out[0] = i;
        }
        off += 0xb4;
    }
    FUN_8038273c();
}

// Trigger setup for entity type 0x35..0x37 (>0x34 && <0x38): resolve the
// entity handle and run the type-0x18 body.
void trigger_setup_type18(int handle, int slotDesc, int arg)
{
    if (*(int*)(slotDesc + 0x84) < 0x38 && 0x34 < *(int*)(slotDesc + 0x84)) {
        int entity = entity_handle_resolve(handle + 1);
        FUN_8034ac28(entity, arg);
    }
}

// Trigger setup for type 0x46 (run FUN_8034d50c) or type 0x35..0x37 (run body
// FUN_8034ad00).
void trigger_setup_type15(int handle, int slotDesc)
{
    int type = *(int*)(slotDesc + 0x84);
    if (type == 0x46) {
        int entity = trigger_slot_resolve_entity((unsigned int)(handle + 1));
        FUN_8034d50c(entity);
    } else if (type < 0x46 && type < 0x38 && 0x34 < type) {
        int entity = entity_handle_resolve((unsigned int)(handle + 1));
        FUN_8034ad00(entity);
    }
}

// Dispatch trigger/entity setup based on the record's opcode (+8). Resolves the
// per-level slot descriptor for the record's short index (+0xc) and routes to
// the type-0x18 or type-0x15 setup path.
void trigger_dispatch_by_type(int rec, int arg)
{
    int levelInfo = (int)level_get_info_cached(DAT_80612614);
    int shortIdx = (int)*(short*)(rec + 0xc);
    int opcode = *(int*)(rec + 8);
    int slotDesc = *(int*)(levelInfo + 0x128) + shortIdx * 0x8c;
    if (opcode == 0x18) {
        trigger_setup_type18(shortIdx, slotDesc, arg);
    } else if (opcode < 0x18 && opcode == 0x15) {
        trigger_setup_type15(shortIdx, slotDesc);
    }
}

// ---------------------------------------------------------------------------
// batch_986 additions: trigger notification + type-0x18 slot flagging.
// ---------------------------------------------------------------------------

extern unsigned char DAT_805015bd;   // game-mode selector byte
extern int* DAT_805c7b64;            // spawn-instance array base (first int = per-group count)
extern int  DAT_805c7b68;            // spawn-instance group count
extern int  DAT_805c7c0c;            // spawn-slot record count
// DAT_805c7c10 already declared above (spawn-slot records, stride 0xb4)

extern void FUN_80351bbc(int rec);   // pre-activation hook
extern void FUN_80351c04(int* rec);  // post-activation hook (non-story type-4)

// Notify the trigger system that the slot at (index = *rec - 1) activated:
// walk the level's spawn groups (count +0x134, array +0x138 stride 0x220),
// and for any active group (+0x21c) whose member list (+0x50/+0x54) contains
// a slot with matching short index, dispatch that group's trigger (+0x8c).
void trigger_notify_slot_activated(int* rec)
{
    int index = *rec;
    if (index <= 0) {
        return;
    }
    int levelInfo = (int)level_get_info_cached(DAT_80612614);
    int grpOff = 0;
    for (int g = 0; g < *(int*)(levelInfo + 0x134); g++) {
        int grp = *(int*)(levelInfo + 0x138) + grpOff;
        if (*(int*)(grp + 0x21c) != 0) {
            int n = *(int*)(grp + 0x50);
            int mo = 0;
            while (n > 0) {
                int memberSlot = *(int*)(levelInfo + 0x130) +
                                 *(int*)(grp + mo + 0x54) * 0xb4;
                if ((int)*(short*)(memberSlot + 0xc) == index - 1) {
                    trigger_dispatch_by_type(*(int*)(levelInfo + 0x130) +
                                             *(int*)(grp + 0x8c) * 0xb4, (int)rec);
                    return;
                }
                mo += 4;
                n--;
            }
        }
        grpOff += 0x220;
    }
}

// Run the pre-activation hook, notify triggers, and (for type-4 records in a
// non-story mode) run the post-activation hook.
void trigger_activate_and_notify(int* rec)
{
    FUN_80351bbc((int)rec);
    trigger_notify_slot_activated(rec);
    if (rec[3] == 4 && DAT_805015bd != 3) {
        FUN_80351c04(rec);
    }
}

// Sweep all spawn instances; for each type-2 slot referencing the instance's
// short index whose opcode (+2 word) is 0x18, OR-in the ready flags (0x130 if
// bit 8 clear, else 0x30) on the instance's flag word (+0x10).
void spawn_mark_type18_slots(void)
{
    int instOff = 0;
    for (int i = 0; i < DAT_805c7b68; i++) {
        if (0 < *DAT_805c7b64) {
            int instId = *(int*)((int)DAT_805c7b64 + instOff);
            int slotOff = 0;
            for (int j = 0; j < DAT_805c7c0c; j++) {
                int* slot = (int*)(DAT_805c7c10 + slotOff);
                if (instId - 1 == (int)*(short*)((char*)slot + 0xc) && *slot == 2) {
                    if (slot[2] == 0x18) {
                        unsigned int flags = *(unsigned int*)((int)DAT_805c7b64 + instOff + 0x10);
                        if ((flags & 8) == 0) {
                            *(unsigned int*)((int)DAT_805c7b64 + instOff + 0x10) = flags | 0x130;
                        } else {
                            *(unsigned int*)((int)DAT_805c7b64 + instOff + 0x10) = flags | 0x30;
                        }
                    }
                }
                slotOff += 0xb4;
            }
        }
        instOff += 0x8c;
    }
}
