// player_hud_state.cpp - Per-frame player HUD "objective / context" state classifier.
//
// player_compute_hud_objective_state() maps the player's current interaction
// state (+0x114 state bits, held/attached sub-object, game mode) onto a HUD
// objective-icon id (+0x1390) plus an animation blend weight (+0x1394) and a
// prompt/effect descriptor (+0xebc..+0xec4). It is called every frame from
// hud_per_frame_tick (0x80109548) and player_main_tick (0x800a9138).
//
// Address: 0x8025ad20

#include "common.h"

extern "C" {
extern int DAT_80612614;   // active game-mode id
extern int DAT_80611d74;   // player-index scratch (int base)
extern int DAT_80610ba0;   // anim/state descriptor table (0x29c stride)

extern const float FLOAT_80618e58;
extern const float FLOAT_80618e64;
extern const float FLOAT_80618ebc;
extern const float FLOAT_80618f88;
extern const float FLOAT_80618f8c;

extern int __save_gpr_28(void);        // returns player-control work context
extern void __restore_gpr_28(void);

extern int  FUN_8036fe90(int rec);     // resolve grab/attach handle for record
extern int* subobj_grab_attach_point(int* object);
extern int  object_slot_switch_permitted(int object, int which);
extern int  object_is_active_tagged(int tagRec);
extern int  object_is_blocking(int ctx, int slotRec);
extern double object_anim_slot_blend_weight(int slotRec);
}

// ---------------------------------------------------------------------------
// 0x8025ad20
void player_compute_hud_objective_state(void)
{
    float weight;
    float f1;
    float f2;
    float f3;
    int ctx;
    int stateBits;
    int iconId;
    int held;
    int slotRec;
    int* object;
    double blend;

    ctx = __save_gpr_28();
    weight = FLOAT_80618e64;
    stateBits = *(int*)(ctx + 0x114);
    object = *(int**)(*(int*)(ctx + 0x14) + 0x7c);
    if (*(int*)(ctx + 4) < 0) goto LAB_done;

    *(unsigned int*)(ctx + 0x1390) = 1;
    f1 = FLOAT_80618e58;
    *(float*)(ctx + 0x1394) = weight;
    *(float*)(ctx + 0xec4) = f1;
    *(unsigned int*)(ctx + 0xebc) = 0;
    *(unsigned int*)(ctx + 0xec0) = 0;

    if (((*(int*)(ctx + 8) != 0) && (*(int*)(*(int*)(ctx + 8) + 0x218) == 0x1b)) ||
        (stateBits == 0x2000)) goto LAB_done;

    if (stateBits < 0x2000) {
        if (stateBits == 0x100) {
LAB_from_field:
            *(unsigned int*)(ctx + 0x1390) = *(unsigned int*)(ctx + 0x140c);
            goto LAB_done;
        }
        if (stateBits < 0x100) {
            if (stateBits == 0x80) {
                if (DAT_80612614 == 4) {
                    *(unsigned int*)(ctx + 0x1390) = 0x21;
                } else {
                    iconId = FUN_8036fe90(DAT_80611d74);
                    if (iconId == 0) {
                        *(unsigned int*)(ctx + 0x1390) = 10;
                    }
                }
                goto LAB_done;
            }
            if ((stateBits < 0x80) && (stateBits == 0x40)) {
LAB_held_object:
                if ((object[0x305] == 0) ||
                    (held = *(int*)(object[0x305] + 0x7c), held == 0)) {
                    object = (int*)subobj_grab_attach_point(object);
                    if (((object == (int*)0) || (*object == 0)) ||
                        (held = *(int*)(*object + 0x6c), held == 0)) {
                        *(unsigned int*)(ctx + 0x1390) = 0;
                    } else {
                        *(unsigned int*)(ctx + 0x1390) =
                            *(unsigned int*)(DAT_80610ba0 + *(int*)(held + 0x30) * 0x29c + 0x54);
                    }
                } else {
                    *(unsigned int*)(ctx + 0x1390) =
                        *(unsigned int*)(DAT_80610ba0 +
                            *(int*)(*(int*)(held + 0x6c) + 0x30) * 0x29c + 0x54);
                }
                goto LAB_done;
            }
        } else {
            if (stateBits == 0x1000) goto LAB_from_field;
            if ((stateBits < 0x1000) && (stateBits == 0x400)) goto LAB_held_object;
        }
    } else {
        if (stateBits == 0x20000) {
            *(unsigned int*)(ctx + 0x1390) = 0x25;
            goto LAB_done;
        }
        if (stateBits < 0x20000) {
            if (stateBits == 0x10000) goto LAB_done;
            if ((stateBits < 0x10000) && (stateBits == 0x8000)) goto LAB_from_field;
        } else {
            if (stateBits == 0x100000) goto LAB_from_field;
            if ((stateBits < 0x100000) && (stateBits == 0x80000)) {
                *(unsigned int*)(ctx + 0x1390) = 0x26;
                goto LAB_done;
            }
        }
    }

    iconId = object_slot_switch_permitted((int)object, 0);
    if ((iconId == 0) && (iconId = object_slot_switch_permitted((int)object, 1), iconId == 0)) {
        *(unsigned int*)(ctx + 0x1390) = 0;
    } else {
        if ((object[0x81] != -1) ||
            ((object[0x35] != -1) &&
             (iconId = object_is_active_tagged(ctx + 0x184), iconId == 0))) {
            int animIdx = object[0x81];
            slotRec = ctx + 0x4e4;
            if ((animIdx == -1) && (iconId = object_is_active_tagged(ctx + 0x184), iconId == 0)) {
                slotRec = ctx + 0x184;
                animIdx = object[0x35];
            }
            *(unsigned int*)(ctx + 0x1390) = *(unsigned int*)(DAT_80610ba0 + animIdx * 0x29c + 0x54);
            iconId = object_is_blocking(ctx, slotRec);
            if ((iconId != 0) &&
                (animIdx = *(int*)(DAT_80610ba0 + animIdx * 0x29c + 0x58), animIdx != -1)) {
                *(int*)(ctx + 0x1390) = animIdx;
            }
            blend = object_anim_slot_blend_weight(slotRec);
            *(float*)(ctx + 0x1394) = (float)blend;
        }
        if ((*(unsigned int*)(ctx + 0xc) & 0x40) != 0) {
            *(float*)(ctx + 0x1394) = FLOAT_80618e64;
        }
        f3 = FLOAT_80618f8c;
        f1 = FLOAT_80618f88;
        f2 = FLOAT_80618ebc;
        iconId = *(int*)(ctx + 0x1390);
        if ((iconId == 0x1f) && (DAT_80612614 == 0xb)) {
            *(unsigned int*)(ctx + 0xec0) = 1;
            *(unsigned int*)(ctx + 0xebc) = *(unsigned int*)(ctx + 0xebc) | 1;
            *(float*)(ctx + 0xec4) = f2;
        } else if ((iconId == 0xd) || (iconId == 0x1f)) {
            *(unsigned int*)(ctx + 0xec0) = 1;
            *(float*)(ctx + 0xec4) = f1;
        } else if ((iconId == 9) || (iconId == 0x20)) {
            *(unsigned int*)(ctx + 0xec0) = 1;
            *(float*)(ctx + 0xec4) = f3;
        }
        f2 = FLOAT_80618f88;
        if (object[0x25] == 0xc) {
            *(unsigned int*)(ctx + 0xec0) = 1;
            *(float*)(ctx + 0xec4) = f2;
        }
    }
LAB_done:
    __restore_gpr_28();
}
