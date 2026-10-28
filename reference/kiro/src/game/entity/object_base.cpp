// object_base.cpp
// Game object/prop base system: object-type table dispatch, deferred render-flag
// queue, and object activate/deactivate + state predicates.
//
// The object-type table lives at DAT_80610ba0 with a 0x29c-byte stride per type;
// offset +0xc is the type flag word and +0x294 is a per-type method function
// pointer. Object instances carry a bound render entity at instance+0x54 and a
// linked owner/def block at instance+0x354.
//
// Batch: batch_532 | Subsystem: game | Date: 2026-09-06

#include "common.h"

// ---- Per-file externs for raw symbols referenced by this translation unit ----
extern int DAT_80610ba0;              // base of object-type table (0x29c stride)
extern int DAT_80611c24;              // entity pool base (0x2e8 stride)
extern int DAT_80611d74;              // active-object context block
extern int DAT_80611928;              // global frame counter
extern float FLOAT_80616b98;          // default object field-0x48 value
extern int DAT_806125c0;              // deferred render-flag queue count
extern int DAT_805c18d8;              // deferred render-flag queue base (20 x 0x10)
extern int DAT_805c18dc;              // queue entry +0x04 (render flag index)
extern int DAT_805c18e0;              // queue entry +0x08 (render flag value)
extern int DAT_805c18e4;              // queue entry +0x0c (enqueue frame)

extern void entity_set_render_flag(int entity, int index, int value);
extern void player_ai_projectile_death_dispatch(void);

// Cross-module object helpers (from the 0x801af object-context module).
extern int FUN_801afda0(int ctx, int object);   // bind object to active context
extern int FUN_801afe30(int object);            // query object context predicate

// Active-object accessor pair: FUN_803826f0 returns the current object in the
// high word of a register pair; FUN_8038273c releases/ends the accessor scope.
extern unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void);

// memset shim (Ghidra intrinsic wrapper, void* base).
extern void memset_wrapper(void *dst, int value, int size);

// ---- Object base implementation ----

// 0x801a7324: initialise an object's field-0x44 handle and reset field-0x48.
void object_set_handle(int object, int handle)
{
    *(int *)(object + 0x44) = handle;
    *(float *)(object + 0x48) = FLOAT_80616b98;
}

// 0x801a7334: dispatch a method through the object-type table. param_2 is the
// type id; the per-type method pointer lives at table + type*0x29c + 0x294.
int object_type_dispatch(int arg0, int type_id, int arg2, int arg3, int arg4)
{
    int result = 0;
    if (type_id >= 0) {
        int (*method)(int, int, int, int) =
            *(int (**)(int, int, int, int))(DAT_80610ba0 + type_id * 0x29c + 0x294);
        if (method != 0) {
            result = method(arg0, arg2, arg3, arg4);
        }
    }
    return result;
}

// 0x801a7390: reset the deferred render-flag queue (0x140 bytes = 20 x 0x10).
void object_render_queue_reset(void)
{
    memset_wrapper(&DAT_805c18d8, 0, 0x140);
    DAT_806125c0 = 0;
}

// 0x801a73c8: forward a render-flag set to the object's bound render entity.
void object_apply_render_flag(int object, int index, int value)
{
    if (*(int *)(object + 0x54) != 0) {
        entity_set_render_flag(*(int *)(object + 0x54), index, value);
    }
}

// 0x801a73f4: apply a render flag to every node of the current object's entity.
void object_apply_render_flag_all_nodes(void)
{
    unsigned long long acc = FUN_803826f0();
    int object = (int)(acc >> 0x20);
    int value = (int)acc;
    if (*(int *)(object + 0x54) != 0) {
        int node_count = **(int **)(*(int *)(*(int *)(object + 0x54) + 4) + 4);
        for (int i = 0; i < node_count; i = i + 1) {
            object_apply_render_flag(object, i, value);
        }
    }
    FUN_8038273c();
}

// 0x801a7468: enqueue a deferred render-flag change for the object's entity.
// The queue holds up to 20 entries of 0x10 bytes each.
void object_render_queue_push(int object, int index, int value)
{
    if (*(int *)(object + 0x54) == 0) {
        return;
    }
    if (DAT_806125c0 > 0x13) {
        return;
    }
    int byte_off = 0;
    int remaining = 0x14;
    do {
        if (*(int *)((int)&DAT_805c18d8 + byte_off) == 0) {
            *(int *)((int)&DAT_805c18d8 + byte_off) =
                *(int *)(*(int *)(object + 0x54) + 0x2c);
            int frame = DAT_80611928;
            *(int *)((int)&DAT_805c18dc + byte_off) = index;
            *(int *)((int)&DAT_805c18e0 + byte_off) = value;
            *(int *)((int)&DAT_805c18e4 + byte_off) = frame;
            DAT_806125c0 = DAT_806125c0 + 1;
            return;
        }
        byte_off = byte_off + 0x10;
        remaining = remaining + -1;
    } while (remaining != 0);
}

// 0x801a74e0: process the deferred render-flag queue. Entries older than two
// frames flush their render-flag change to the resolved entity and are cleared.
void object_render_queue_flush(void)
{
    if (DAT_806125c0 == 0) {
        return;
    }
    int i = 0;
    int byte_off = 0;
    do {
        unsigned int *slot = (unsigned int *)((int)&DAT_805c18d8 + byte_off);
        unsigned int entity_id = *slot;
        if (entity_id != 0) {
            int entity = DAT_80611c24 + (entity_id >> 0x16) * 0x2e8;
            if (*(unsigned int *)(entity + 0x2c) != entity_id) {
                entity = 0;
                *slot = 0;
            }
            if (*(int *)((int)&DAT_805c18e4 + byte_off) <= DAT_80611928 + -2) {
                if (entity != 0) {
                    entity_set_render_flag(entity,
                        *(int *)((int)&DAT_805c18dc + byte_off),
                        *(int *)((int)&DAT_805c18e0 + byte_off));
                }
                memset_wrapper(slot, 0, 0x10);
                DAT_806125c0 = DAT_806125c0 + -1;
            }
        }
        i = i + 1;
        byte_off = byte_off + 0x10;
    } while (i < 0x14);
}

// 0x801a75bc: set the active/inactive state of the current object. Toggles the
// bound render entity + linked def flags and emits object event type 0x16.
void object_set_active(int arg0, int arg1, int active)
{
    unsigned long long acc = FUN_803826f0();
    int object = (int)(acc >> 0x20);
    (void)arg0;
    (void)arg1;
    if (object != 0) {
        unsigned int type_flags =
            *(unsigned int *)(DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c + 0xc);
        *(int *)(object + 0x38) = active;
        *(int *)(*(int *)(object + 0x354) + 0x34) = active;
        FUN_801afda0(DAT_80611d74, object);
        if ((type_flags & 0x40) != 0) {
            if ((*(unsigned int *)(DAT_80611d74 + 0xc) & 0x40) != 0) {
                if (FUN_801afe30(object) == 0) {
                    *(unsigned int *)(DAT_80611d74 + 0xc) =
                        *(unsigned int *)(DAT_80611d74 + 0xc) & 0xffffffbf;
                    goto after_flags;
                }
            }
            if (((*(unsigned int *)(DAT_80611d74 + 0xc) & 0x40) == 0) &&
                ((*(unsigned int *)(DAT_80611d74 + 0xc) & 0x20) != 0) &&
                (FUN_801afe30(object) != 0)) {
                *(unsigned int *)(DAT_80611d74 + 0xc) =
                    *(unsigned int *)(DAT_80611d74 + 0xc) | 0x40;
            }
        }
after_flags:
        {
            int render = *(int *)(object + 0x58);
            if (render != 0) {
                int def = *(int *)(object + 0x354);
                if (active == 0) {
                    *(unsigned int *)(render + 0x28) =
                        *(unsigned int *)(render + 0x28) & 0xfffffffe;
                    int node = *(int *)(*(int *)(def + 0xe8) + 4);
                    *(unsigned int *)(node + 0x30) =
                        *(unsigned int *)(node + 0x30) & 0xfffffffd;
                    int grp = *(int *)(def + 0xe8);
                    *(unsigned int *)(grp + 0x28) =
                        *(unsigned int *)(grp + 0x28) & 0xffffff9f;
                    player_ai_projectile_death_dispatch();
                    object_type_dispatch(0x16, *(int *)(def + 0x30), 0, def, 0);
                } else {
                    *(unsigned int *)(render + 0x28) =
                        *(unsigned int *)(render + 0x28) | 1;
                    int node = *(int *)(*(int *)(def + 0xe8) + 4);
                    *(unsigned int *)(node + 0x30) =
                        *(unsigned int *)(node + 0x30) | 2;
                    int grp = *(int *)(def + 0xe8);
                    *(unsigned int *)(grp + 0x28) =
                        *(unsigned int *)(grp + 0x28) | 0x60;
                    object_type_dispatch(0x16, *(int *)(def + 0x30), 0, def, 1);
                }
            }
        }
    }
    if ((int)acc != 0) {
        *(int *)((int)acc + 0x34) = active;
    }
}

// 0x801a7768: predicate - object is blocking/solid. True if the active context
// carries flag 0x40, or the object is state 1 without type flag 0x20000.
int object_is_blocking(int ctx, int object)
{
    if ((*(unsigned int *)(ctx + 0xc) & 0x40) != 0) {
        return 1;
    }
    if ((*(int *)(object + 0x38) == 1) &&
        ((*(unsigned int *)(DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c + 0xc) & 0x20000) == 0)) {
        return 1;
    }
    return 0;
}

// 0x801a77b4: predicate - object is an active state-2 object whose type carries
// flag 0x4000.
int object_is_active_tagged(int object)
{
    int result = 0;
    if ((object != 0) && (*(int *)(object + 0x3c) == 2)) {
        if ((*(int *)(object + 0x30) != -1) &&
            ((*(unsigned int *)(DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c + 0xc) & 0x4000) != 0)) {
            result = 1;
        }
    }
    return result;
}

// ===========================================================================
// Batch: batch_533 | Object-type param tables + prop preload/spawn helpers
// ===========================================================================
//
// A second per-type table DAT_8042e238 (0xe0-byte stride) stores animation/sound
// parameter defaults per object type. Entries are 32-bit; flag word +0x08 (via
// DAT_8042e240) bit 0 selects a scaled-float interpretation. Object instances
// receive these defaults written into their sound-slot region (instance+0x61e,
// per-channel shorts) and param region (instance+0x90).

// Object-type param table (0xe0 stride): +0x00 value, +0x08 flag word.
extern int DAT_8042e238;              // per-type param value
extern int DAT_8042e240;              // per-type param flag word (+0x08 of entry)
extern unsigned int DAT_806109c0;     // scale numerator source (int-as-double)
extern double DOUBLE_80616ba0;        // 2^52 int->double normalization bias
extern float FLOAT_80616b9c;          // param scale divisor

// Channel table used by the sound-register wrapper (0x3c stride per object).
extern int DAT_8043e238;              // per-object channel id table base

// Default spawn transform block for prop spawn (6 floats).
extern int DAT_803cd138;
extern int DAT_803cd13c;
extern int DAT_803cd140;
extern int DAT_803cd144;
extern int DAT_803cd148;
extern int DAT_803cd14c;

// Entity system helpers. The prop preload/spawn helpers thread a register-pair
// "carrier" (returned in the high word) through successive calls; model it as an
// opaque void* passed and returned by value.
extern void entity_preload_resources(void *carrier, double a, double b, void *c,
                                      void *d, void *e, void *f, void *g, int def_id);
extern void *entity_spawn_with_activation(void *carrier, double a, double b, void *c,
                                          void *d, void *e, void *f, void *g,
                                          int activate, int def_id, void *xform,
                                          void *xform2, int z0, int z1);
extern void entity_destroy(void *entity);

// Sound/animation channel register (defined below in batch_539).
void object_sound_slot_register(int object, int channel_id, short *param_ptr);

// 0x801a77fc: predicate - object type carries flag bit 0x80.
unsigned int object_type_has_flag80(int object)
{
    return *(unsigned int *)(DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c + 0xc) & 0x80;
}

// 0x801a7818: resolve a per-type param value. Entry flag bit 0 selects a scaled
// interpretation: (scale_src - bias) * (value - bias) / divisor; otherwise the
// raw 32-bit value is returned.
int object_param_resolve(int type_index)
{
    int entry = type_index * 0xe0;
    int result;
    if ((*(unsigned int *)((int)&DAT_8042e240 + entry) & 1) == 0) {
        result = *(int *)((int)&DAT_8042e238 + entry);
    } else {
        double scale_src = (double)(int)DAT_806109c0 - DOUBLE_80616ba0;
        double value = (double)*(int *)((int)&DAT_8042e238 + entry) - DOUBLE_80616ba0;
        result = (int)(((float)scale_src * (float)value) / FLOAT_80616b9c);
    }
    return result;
}

// 0x801a7900: write resolved param defaults into the current object's sound-slot
// region (instance+0x61e, per-channel shorts). Uses the primary channel at +0x14
// and optional secondary channel at +0x1c of the type entry.
void object_write_sound_slots(void)
{
    unsigned long long acc = FUN_803826f0();
    int object = (int)(acc >> 0x20);
    int type_index = (int)acc;
    if ((type_index >= 0) && (type_index < 0x42)) {
        int entry = DAT_80610ba0 + type_index * 0x29c;
        int chan_a = *(int *)(entry + 0x14);
        int chan_b = *(int *)(entry + 0x1c);
        int val = object_param_resolve(chan_a);
        *(short *)(object + chan_a * 2 + 0x61e) = (short)val;
        if ((chan_b != 0) && (chan_b != chan_a)) {
            val = object_param_resolve(chan_b);
            *(short *)(object + chan_b * 2 + 0x61e) = (short)val;
        }
    }
    FUN_8038273c();
}

// 0x801a7898: apply object_write_sound_slots for both channels (2 iterations).
void object_write_sound_slots_all(void)
{
    int i = 0;
    do {
        object_write_sound_slots();
        i = i + 1;
    } while (i < 2);
}

// 0x801a798c: write the type's raw default params into an object instance's
// param region (instance+0x90), for the primary (+0x14/+0x18) and secondary
// (+0x1c/+0x20) channels.
void object_write_default_params(int object, int type_index)
{
    if (type_index < 0) {
        return;
    }
    int entry = DAT_80610ba0 + type_index * 0x29c;
    if (*(int *)(entry + 0x14) != 0) {
        *(short *)(object + *(int *)(entry + 0x14) * 2 + 0x90) =
            (short)*(int *)(entry + 0x18);
    }
    if (*(int *)(entry + 0x1c) == 0) {
        return;
    }
    *(short *)(object + *(int *)(entry + 0x1c) * 2 + 0x90) =
        (short)*(int *)(entry + 0x20);
}

// 0x801a7a44: register the type's sound-param block (+0x28) for a channel via
// the sound-register wrapper, if the channel id is in range.
void object_register_sound_channel(int object, int channel_id)
{
    if ((channel_id >= 0) && (channel_id < 0x42)) {
        object_sound_slot_register(object, channel_id,
                     (short *)(DAT_80610ba0 + channel_id * 0x29c + 0x28));
    }
}

// 0x801a79dc: register both sound channels for the current object. Channel ids
// come from the per-object channel table (DAT_8043e238, 0x3c stride).
void object_register_sound_channels_all(void)
{
    unsigned long long acc = FUN_803826f0();
    int object = (int)(acc >> 0x20);
    int i = 0;
    int byte_off = 0;
    do {
        object_register_sound_channel(object,
            *(int *)((int)&DAT_8043e238 + (int)acc * 0x3c + byte_off));
        i = i + 1;
        byte_off = byte_off + 4;
    } while (i < 2);
}

// 0x801a7a84: preload or spawn a prop entity by def id. When mode==0, preload
// resources only; otherwise spawn an activated entity at the default transform
// and immediately destroy it (forces model/resource load). The register-pair
// carrier is threaded through and returned.
void *object_prop_preload_or_spawn(void *carrier, double a, double b, void *c,
                                   void *d, void *e, void *f, void *g,
                                   int def_id, int mode)
{
    if ((def_id != -1) && (def_id < 0x9c7)) {
        if (mode == 0) {
            entity_preload_resources(carrier, a, b, c, d, e, f, g, def_id);
        } else {
            // Two consecutive default blocks: group_a (0x803cd138..140) passed
            // first, then group_b (0x803cd144..14c). Layout mirrors the stack
            // locals local_1c (group_a) and local_28 (group_b) in the original.
            int xform[6];
            xform[0] = DAT_803cd138;   // local_1c
            xform[1] = DAT_803cd13c;   // local_18
            xform[2] = DAT_803cd140;   // local_14
            xform[3] = DAT_803cd144;   // local_28
            xform[4] = DAT_803cd148;   // local_24
            xform[5] = DAT_803cd14c;   // local_20
            void *entity = entity_spawn_with_activation(carrier, a, b, c, d, e, f, g,
                                                        1, def_id, &xform[0], &xform[3], 0, 0);
            entity_destroy(entity);
        }
    }
    // The register-pair carrier passes through unchanged in both branches.
    return carrier;
}

// 0x801a7b28: preload the three explosion/effect prop defs (0x743, 0x745, 0x744).
void *object_prop_preload_explosion_set(void *carrier, double a, double b, void *c,
                                        void *d, void *e, void *f, void *g, int mode)
{
    void *cur = object_prop_preload_or_spawn(carrier, a, b, c, d, e, f, g, 0x743, mode);
    cur = object_prop_preload_or_spawn(cur, a, b, c, d, e, f, g, 0x745, mode);
    cur = object_prop_preload_or_spawn(cur, a, b, c, d, e, f, g, 0x744, mode);
    return cur;
}

// 0x801a7b74: preload the single smoke/trail prop def (0x746).
void *object_prop_preload_smoke(void *carrier, double a, double b, void *c,
                                void *d, void *e, void *f, void *g, int mode)
{
    return object_prop_preload_or_spawn(carrier, a, b, c, d, e, f, g, 0x746, mode);
}

// ===========================================================================
// Batch: batch_535 | Object system reset + per-object state / anim-frame commit
// ===========================================================================

// System-reset globals.
extern int DAT_805c1a18;              // 4-entry object handle scratch table
extern int DAT_806125d0;              // object system counter A
extern int DAT_806125cc;              // object system fade target id
extern float FLOAT_806125c8;          // object system fade value
extern float FLOAT_80616ba8;          // fade start value
extern int DAT_806125c4;              // active object count (per-object byte table)

// Cross-module helpers.
extern void *object_preload_match_weapons(void *carrier, double a, double b, void *c,
                                          void *d, void *e, void *f, void *g);
extern void FUN_8035f6f4(int a, int b);     // render subsystem reset
extern void FUN_80360f78(int a);            // effect subsystem reset
extern int  FUN_801aae80(int index);        // resolve per-object byte value
// 0x801aaa10 (object_slot_switch_permitted) is defined below in batch_541.
int object_slot_switch_permitted(int object, int mode);
// 0x801a89cc (object_anim_pick_event) and 0x801a8760 (object_anim_commit_default)
// are defined below in batch_537 / batch_536.
int object_anim_pick_event(int object);
void object_anim_commit_default(void);
extern void FUN_801b16fc(double a, int b, int def, int flag); // anim event dispatch
extern int  match_slot_field_ptr(int base, int slot);       // 0x7c-stride +0x68 accessor

// 0x801a836c: reset the object system - clear the 4-entry handle scratch table
// (DAT_805c1a18), reset the render/effect subsystems and the deferred render
// queue, and clear the fade state.
void object_system_reset(void)
{
    int off = 0;
    int n = 4;
    do {
        *(int *)((int)&DAT_805c1a18 + off) = -1;
        off = off + 4;
        n = n + -1;
    } while (n != 0);
    object_render_queue_reset();
    FUN_8035f6f4(0, 0);
    FUN_80360f78(0);
    DAT_806125d0 = 0;
    DAT_806125cc = 0;
    FLOAT_806125c8 = FLOAT_80616b98;
}

// 0x801a83d8: preload the match weapons then reset the object system.
void object_preload_and_reset(void *carrier, double a, double b, void *c,
                              void *d, void *e, void *f, void *g)
{
    object_preload_match_weapons(carrier, a, b, c, d, e, f, g);
    object_system_reset();
}

// 0x801a8400: fill an object's per-slot byte table (instance+0xcd4) with
// resolved values for each active object.
void object_fill_byte_table(int object)
{
    for (int i = 0; i < DAT_806125c4; i = i + 1) {
        int v = FUN_801aae80(i);
        *(char *)(object + i + 0xcd4) = (char)v;
    }
}

// 0x801a8458: begin a fade to the given target id.
void object_fade_begin(int target_id)
{
    DAT_806125cc = target_id;
    FLOAT_806125c8 = FLOAT_80616ba8;
}

// 0x801a8468: set the "pending swap" flag (bit 0x10) on an object (+0x878).
void object_set_pending_swap(int object)
{
    *(unsigned int *)(object + 0x878) = *(unsigned int *)(object + 0x878) | 0x10;
}

// 0x801a8478: process a pending object swap. If the swap flag (0x10) is set,
// clear it, hide the old render entity (+0x898) and show the active-context one
// (DAT_80611d74+0x1d8). When requested, run the post-process for context modes
// 0x27-0x2a and flush.
void object_process_pending_swap(int object, int do_flush)
{
    int ctx = DAT_80611d74;
    int ctx_slot = DAT_80611d74 + 0x184;
    if ((*(unsigned int *)(object + 0x878) & 0x10) != 0) {
        *(unsigned int *)(object + 0x878) = *(unsigned int *)(object + 0x878) & 0xffffffef;
        int render = *(int *)(object + 0x898);
        if (render != 0) {
            *(unsigned int *)(render + 0x28) = *(unsigned int *)(render + 0x28) & 0xfffffffe;
        }
        int active_render = *(int *)(ctx + 0x1d8);
        if (active_render != 0) {
            *(unsigned int *)(active_render + 0x28) = *(unsigned int *)(active_render + 0x28) | 1;
        }
    }
    if (do_flush != 0) {
        int mode = *(int *)(ctx + 0x26c);
        if ((mode == 0x27) || ((unsigned int)(mode - 0x28) < 2) || (mode == 0x2a)) {
            object_anim_pick_event(ctx_slot);
            object_anim_commit_default();
        }
        object_anim_commit_default();
    }
}

// 0x801a853c: predicate - object (+0x844) is active-tagged and has a render
// entity (+0x898) with the pending-swap flag (0x10) set.
int object_swap_is_pending(int object)
{
    int result = 0;
    if (object_is_active_tagged(object + 0x844) != 0) {
        if ((*(int *)(object + 0x898) != 0) && ((*(unsigned int *)(object + 0x878) & 0x10) != 0)) {
            result = 1;
        }
    }
    return result;
}

// 0x801a85a0: predicate - the object's linked type has a valid channel id
// (DAT_8043e238, 0xf-word stride) and the anim predicate passes.
int object_has_active_channel(int object)
{
    int result = 0;
    int obj = *(int *)(*(int *)(object + 0x14) + 0x7c);
    if (((&DAT_8043e238)[*(int *)(obj + 0x98) * 0xf] != -1) &&
        (FUN_801aaa10(obj, 0) != 0)) {
        result = 1;
    }
    return result;
}

// 0x801a8608: thin wrapper around object_has_active_channel (returns its result;
// batch_537 driver reads the return value).
int object_has_active_channel_stub(int object)
{
    return object_has_active_channel(object);
}

// 0x801a8628: commit an object animation frame for the current object. Dispatches
// the pre-frame event (0x12), stores the new frame index at +0xe8, updates the
// state flags at +0x34 (set bit 2, clear bit 3, set bit 4 on frame change),
// stores the interpolation values at +0xf0/+0xf4, dispatches the post-frame
// event (0x11), and for frame 9 fires an anim event via FUN_801b16fc.
void object_commit_anim_frame(float interp)
{
    unsigned long long acc = FUN_803826f0();
    int object = (int)(acc >> 0x20);
    int frame = (int)acc;
    if (*(int *)(object + 0x44) != 0) {
        int prev_frame = *(int *)(object + 0xe8);
        if (frame >= 0) {
            int type_id = (object == 0)
                ? *(int *)(*(int *)(object + 0x354) + 0x30)
                : *(int *)(object + 0x30);
            object_type_dispatch(0x12, type_id, object, *(int *)(object + 0x354), 0);
        }
        *(int *)(object + 0xe8) = frame;
        *(unsigned int *)(object + 0x34) = *(unsigned int *)(object + 0x34) | 2;
        *(unsigned int *)(object + 0x34) = *(unsigned int *)(object + 0x34) & 0xfffffff7;
        if (prev_frame != *(int *)(object + 0xe8)) {
            *(unsigned int *)(object + 0x34) = *(unsigned int *)(object + 0x34) | 4;
        }
        *(float *)(object + 0xf4) = interp;
        *(float *)(object + 0xf0) = FLOAT_80616b98;
        if (frame >= 0) {
            int type_id = (object == 0)
                ? *(int *)(*(int *)(object + 0x354) + 0x30)
                : *(int *)(object + 0x30);
            object_type_dispatch(0x11, type_id, object, *(int *)(object + 0x354),
                                 (unsigned int)(frame != prev_frame));
        }
        if (frame == 9) {
            int slot = match_slot_field_ptr(DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c,
                                            *(int *)(object + 0x5c));
            FUN_801b16fc((double)*(float *)(slot + 0x50), *(int *)(slot + 0x4c),
                         *(int *)(object + 0x354), 1);
        }
    }
    FUN_8038273c();
}

// ===========================================================================
// Batch: batch_536 | Object animation-slot resolver + per-object anim-state
// accessors. Date: 2026-09-07
//
// These small helpers operate on an object instance's animation-state fields:
//   +0xe8  current frame index
//   +0xf0  interpolation value (float)
//   +0xf8  pending target frame
//   +0x34  state flag word (bit2/bit3/bit4/bit8 used by the anim commit path)
//   +0x3c/+0x40  channel-state fields
// The slot resolver walks the per-type sound/anim slot table (type entry +0x190
// base, +0x194 count, 0xc-byte {typeId, subType} records) and remaps object
// event ids 0x27-0x2c based on the active object state and channel.
// ===========================================================================

// 0x801a8760: default per-frame anim-commit wrapper (forwards to the float
// variant with the default interpolation). object_commit_anim_frame takes the
// interpolation as a float; the register-pair form here passes FLOAT_80616b98.
void object_anim_commit_default(void)
{
    object_commit_anim_frame(FLOAT_80616b98);
}

// 0x801a8784: store the interpolation value at +0xf0, set state flag bit 8
// (+0x34), and clear the pending target (+0xf8) if the current frame (+0xe8)
// has reached it.
void object_anim_set_interp(float interp, int object)
{
    *(float *)(object + 0xf0) = interp;
    *(unsigned int *)(object + 0x34) = *(unsigned int *)(object + 0x34) | 8;
    if (*(int *)(object + 0xe8) == *(int *)(object + 0xf8)) {
        *(int *)(object + 0xf8) = 0;
    }
}

// 0x801a87b0: mark the anim frame dirty - set state flag bit 4 (+0x34) and clear
// the pending target (+0xf8) if the current frame has reached it.
void object_anim_mark_frame_dirty(int object)
{
    *(unsigned int *)(object + 0x34) = *(unsigned int *)(object + 0x34) | 4;
    if (*(int *)(object + 0xe8) == *(int *)(object + 0xf8)) {
        *(int *)(object + 0xf8) = 0;
    }
}

// 0x801a87d8: return the object's current animation frame index (+0xe8).
int object_anim_get_frame(int object)
{
    return *(int *)(object + 0xe8);
}

// 0x801a87e0: classify the object's channel state from fields +0x3c/+0x40.
// Returns 1 when +0x3c is 0 or 2, else 2 when +0x40 is set, else 0.
int object_anim_channel_state(int object)
{
    if ((*(int *)(object + 0x3c) != 0) && (*(int *)(object + 0x3c) != 2)) {
        return (*(int *)(object + 0x40) != 0) ? 2 : 0;
    }
    return 1;
}

// 0x801a8814: search a type's anim-slot table for a record matching (type_id,
// sub_type). The table base is type_entry+0x190 with +0x194 as the count and a
// 0xc-byte stride; each record is {short typeId, short subType, ...}. Exact
// matches return immediately; otherwise a subType 2<->0 fallback record is
// remembered and returned.
short *object_anim_slot_find(int type_entry, int type_id, int sub_type)
{
    int count = *(int *)(type_entry + 0x194);
    short *fallback = (short *)0;
    int byte_off = 0;
    while (count > 0) {
        short *rec = (short *)(*(int *)(type_entry + 0x190) + byte_off);
        if (type_id == (int)rec[0]) {
            short rec_sub = rec[1];
            if (sub_type == (int)rec_sub) {
                return rec;
            }
            if ((rec_sub == 2) && (sub_type == 0)) {
                fallback = rec;
            } else if ((rec_sub == 0) && (sub_type == 2)) {
                fallback = rec;
            }
        }
        byte_off = byte_off + 0xc;
        count = count + -1;
    }
    return fallback;
}

// 0x801a8890: resolve the anim slot for the current object. Picks the object's
// type-table entry (DAT_80610ba0, 0x29c stride) via type index +0x30; when the
// object is active-tagged and the event id is 0x27-0x29 or 0x2a, it instead uses
// the active-context object's type entry (DAT_80611d74+0x514). When the active
// context has a channel, event ids 0x27/0x29 remap to 0x2b and 0x28/0x2a to
// 0x2c. Finally looks up the slot with the object's channel-state as sub_type
// and returns the matching slot record (or 0). The batch_537 driver reads the
// returned slot's {typeId, subType, interp} fields.
short *object_anim_slot_resolve(void)
{
    unsigned long long acc = FUN_803826f0();
    int object = (int)(acc >> 0x20);
    int event_id = (int)acc;
    short *slot = (short *)0;
    if (*(int *)(object + 0x30) != -1) {
        int type_entry = DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c;
        if ((object_is_active_tagged(object) != 0) &&
            (((unsigned int)(event_id - 0x27) < 3) || (event_id == 0x2a))) {
            type_entry = DAT_80610ba0 + *(int *)(DAT_80611d74 + 0x514) * 0x29c;
        }
        if (object_has_active_channel(DAT_80611d74) != 0) {
            if ((event_id == 0x27) || (event_id == 0x29)) {
                event_id = 0x2b;
            } else if ((event_id == 0x28) || (event_id == 0x2a)) {
                event_id = 0x2c;
            }
        }
        int state = object_anim_channel_state(object);
        slot = object_anim_slot_find(type_entry, event_id, (unsigned int)(unsigned char)state);
    }
    FUN_8038273c();
    return slot;
}

// 0x801a896c: predicate - the object's current frame (+0xe8) has reached the
// given target frame. Implemented via CLZ>>5 of the difference (1 when equal).
unsigned int object_anim_frame_reached(int object, int target_frame)
{
    return (unsigned int)countLeadingZeros(target_frame - *(int *)(object + 0xe8)) >> 5;
}

// 0x801a8980: predicate - the object's current frame (+0xe8) equals the target
// frame AND the pending target (+0xf8) does not (frame just committed).
int object_anim_frame_active_no_pending(int object, int target_frame)
{
    if ((*(int *)(object + 0xe8) == target_frame) &&
        (*(int *)(object + 0xf8) != target_frame)) {
        return 1;
    }
    return 0;
}

// 0x801a89a8: predicate - the object's pending target frame (+0xf8) has reached
// the given frame. CLZ>>5 of the difference (1 when equal).
unsigned int object_anim_pending_reached(int object, int target_frame)
{
    return (unsigned int)countLeadingZeros(target_frame - *(int *)(object + 0xf8)) >> 5;
}

// ===========================================================================
// Batch: batch_537 | Object per-frame animation-frame driver + model swap
// helpers. Date: 2026-09-07
//
// The core here is object_anim_frame_driver (0x801a8ca4), the per-frame object
// animation state machine. It reads the current frame id (+0xe8), classifies it
// through a big switch into "sound", "event" and "special" categories, resolves
// the anim slot (object_anim_slot_resolve), copies clip state onto the active
// context's anim slot when needed, starts/blends the clip, sets clip speed from
// the interp value, dispatches object event 0x13, and fires follow-up events.
//
// Active-context anim-slot layout used by the driver:
//   DAT_80611d74 + 0xd1c   -> pointer to the active-context anim-object
//   DAT_80611d74 + 0xd20   active-context clip block base (for blend init)
//   DAT_80611d74 + 0xd3c   active-context clip speed field
//   DAT_80611d74 + 0xd94   active-context blend-weight field
// Object anim-slot: instance+0x54 -> anim-object; its +4 is the primary clip.
// ===========================================================================

// batch_537 raw-symbol externs.
extern int   DAT_80611d78;            // object instance pool base (0x141c stride)
extern int   DAT_80612610;            // object instance pool count
extern int   DAT_80611824;            // anim descriptor table base (4-byte ptrs)
extern int   DAT_8060fae0;            // global "active context valid" gate

// Active-object accessor variant: FUN_803826d0 returns the current object as a
// plain pointer; FUN_8038271c closes that accessor scope.
extern int  FUN_803826d0(void);
extern void FUN_8038271c(void);

// Object-context predicate (0x801af module): true when the object owns the
// active context anim slot.
extern int  FUN_801afb40(int object);

// Follow-up event predicate used by the driver's post-dispatch path.
extern int  FUN_801ab608(void);

// Animation clip helpers (named in Ghidra, not yet decompiled to src, so
// declared here with signatures matching the driver's call sites). The clip
// APIs take the clip time/phase in FP registers; model the leading FP arg as a
// double and the target block as an int base pointer.
extern int    anim_is_clip_active(int clip_block, int clip_id);
extern int    anim_active_slot_userflags(int clip);
extern double anim_primary_clip_phase(int clip);
extern void   anim_start_clip(double phase, double time);
extern void   anim_clip_init_blend(double phase, double time, int arg, int block,
                                    int *speed_field, int *weight_field);
extern void   anim_slot_set_speed(double speed, int speed_field);

// 0x801a89bc: predicate - the object has a pending target frame (+0xf8 != 0).
bool object_anim_has_pending(int object)
{
    return *(int *)(object + 0xf8) != 0;
}

// 0x801a89cc: pick the follow-up event id for an object. Returns 0x1b when the
// object is active (+0x38 set) and the anim slot resolves, otherwise 0xa.
int object_anim_pick_event(int object)
{
    if ((*(int *)(object + 0x38) == 0) || (object_anim_slot_resolve() == 0)) {
        return 0xa;
    }
    return 0x1b;
}

// 0x801a8a10: start the current object's animation clip and initialise the blend
// onto the active context's clip block. For a non-active object the object's own
// clip is started; when the active context has no channel and the object is in
// channel-state 1 the clip is started and blended onto the context clip block
// (DAT_80611d74+0xd20); with an active channel the blend targets the active
// context anim-object's clip block instead.
void object_anim_start_clip(double phase, int arg)
{
    int object = FUN_803826d0();
    if ((object_is_active_tagged(object) == 0) && (*(int *)(object + 0x54) != 0)) {
        anim_start_clip(phase, phase);
    }
    if (object_has_active_channel_stub(DAT_80611d74) == 0) {
        if (*(int *)(object + 0x3c) == 1) {
            anim_start_clip(phase, phase);
            anim_clip_init_blend(phase, phase, arg, DAT_80611d74 + 0xd20,
                                 (int *)(DAT_80611d74 + 0xd3c),
                                 (int *)(DAT_80611d74 + 0xd94));
        }
    } else if (*(int *)(object + 0x3c) == 1) {
        int clip = *(int *)(*(int *)(DAT_80611d74 + 0xd1c) + 4);
        anim_clip_init_blend(phase, phase, arg, clip + 0x184,
                             (int *)(clip + 0xcc), (int *)(clip + 0x124));
    } else {
        anim_clip_init_blend(phase, phase, arg, DAT_80611d74 + 0xd20,
                             (int *)(DAT_80611d74 + 0xd3c),
                             (int *)(DAT_80611d74 + 0xd94));
    }
    FUN_8038273c();
}

// 0x801a8b5c: query whether the object's animation clip is active. For an
// active-tagged object the active context anim-object (DAT_80611d74+0xd1c) is
// queried with the passed clip id; otherwise the object's own clip is queried.
int object_anim_clip_is_active(int object, int clip_a, int clip_id)
{
    unsigned long long acc = FUN_803826f0();
    int cur = (int)(acc >> 0x20);
    int result = 0;
    (void)clip_a;
    if (*(int *)(cur + 0x54) != 0) {
        if (object_is_active_tagged(cur) == 0) {
            result = anim_is_clip_active(*(int *)(cur + 0x54), (int)acc);
        } else {
            result = anim_is_clip_active(*(int *)(DAT_80611d74 + 0xd1c), clip_id);
        }
    }
    (void)object;
    FUN_8038273c();
    return result;
}

// 0x801a8bd4: return the user flags of the object's active clip. Uses the active
// context anim-object for an active-tagged object, else the object's own clip.
int object_anim_slot_userflags(int object)
{
    if (*(int *)(object + 0x54) == 0) {
        return 0;
    }
    if (object_is_active_tagged(object) == 0) {
        return anim_active_slot_userflags(*(int *)(*(int *)(object + 0x54) + 4));
    }
    return anim_active_slot_userflags(*(int *)(*(int *)(DAT_80611d74 + 0xd1c) + 4));
}

// 0x801a8c3c: return the phase of the object's primary animation clip (default
// FLOAT_80616b98 when the object has no anim-object). Active-tagged objects read
// the active-context anim-object's clip.
double object_anim_slot_phase(int object)
{
    if (*(int *)(object + 0x54) == 0) {
        return (double)FLOAT_80616b98;
    }
    if (object_is_active_tagged(object) == 0) {
        return anim_primary_clip_phase(*(int *)(*(int *)(object + 0x54) + 4));
    }
    return anim_primary_clip_phase(*(int *)(*(int *)(DAT_80611d74 + 0xd1c) + 4));
}

// 0x801a8ca4: per-frame object animation-frame driver. Reads the current frame
// id (+0xe8), classifies it via a switch into flags (bVar1=event, bVar2=sound,
// iVar7=special id, uVar8/uVar9 pending), resolves the anim slot, copies clip
// state onto the active-context slot for channel-state-1 objects, starts/blends
// the clip and sets its speed from the interp field (+0xf0), then dispatches
// object event 0x13 and fires follow-up events for flagged frames.
void object_anim_frame_driver(void)
{
    int object = FUN_803826d0();
    int def = *(int *)(object + 0x354);
    unsigned int frame_dirty = *(unsigned int *)(object + 0x34) >> 1 & 1;
    bool is_event = false;
    bool is_sound = false;
    bool is_special = false;
    unsigned int pending_flag = 0;
    unsigned int swap_pending = 0;
    int special_id = 0;

    if (*(int *)(object + 0x54) == 0) {
        FUN_8038271c();
        return;
    }
    *(int *)(object + 0xf8) = 0;

    int active = object_is_active_tagged(object);
    if ((active == 0) && ((FUN_801afb40(object) == 0) || (DAT_8060fae0 == 0))) {
        if (*(int *)(object + 0x3c) == 1) {
            *(unsigned int *)(*(int *)(DAT_80611d74 + 0xd1c) + 0x28) =
                *(unsigned int *)(*(int *)(DAT_80611d74 + 0xd1c) + 0x28) & 0xfffffffe;
        }
        FUN_8038271c();
        return;
    }

    // Active/owner path.
    int frame = *(int *)(object + 0xe8);
    if (((frame == 0x28) || (frame == 0x2a)) &&
        ((object_has_active_channel(DAT_80611d74) == 0) ||
         ((*(unsigned int *)(object + 0x878) & 0x10) != 0))) {
        if (*(int *)(object + 0x3c) == 0) {
            FUN_8038271c();
            return;
        }
    } else if (*(int *)(object + 0x3c) == 2) {
        FUN_8038271c();
        return;
    }

    // For channel-state-1 owner objects, mirror clip state onto the active
    // context anim-object (DAT_80611d74+0xd1c).
    if ((*(int *)(DAT_80611d74 + 0xd1c) != 0) && (FUN_801afb40(object) != 0) &&
        (*(int *)(object + 0x54) != 0) && (*(int *)(object + 0x3c) == 1)) {
        int ctx_clip = *(int *)(DAT_80611d74 + 0xd1c);
        int src = *(int *)(object + 0x54);
        *(int *)(ctx_clip + 0x64) = *(int *)(src + 0x64);
        *(int *)(ctx_clip + 0x68) = *(int *)(src + 0x68);
        *(int *)(ctx_clip + 0x6c) = *(int *)(src + 0x6c);
        *(unsigned int *)(ctx_clip + 0x28) = *(unsigned int *)(ctx_clip + 0x28) | 1;
        *(int *)(ctx_clip + 0xc0) = *(int *)(src + 0xc0);
        *(int *)(ctx_clip + 0x70) = *(int *)(src + 0x70);
        *(int *)(ctx_clip + 0xc8) = *(int *)(src + 0xc8);
        *(int *)(ctx_clip + 0x34) = *(int *)(src + 0x34);
        *(char *)(ctx_clip + 0x32) = 2;
        *(char *)(ctx_clip + 0x31) = 1;
    }

    switch (frame) {
    case 1:  is_sound = true; pending_flag = frame_dirty; break;
    case 3:  if (*(int *)(object + 0x30) != 0x13) { is_sound = true; } break;
    case 4:  special_id = 5; break;
    case 6:  is_sound = true; break;
    case 7:  is_sound = true; pending_flag = frame_dirty; break;
    case 9:  pending_flag = frame_dirty; break;
    case 0xb: case 0xc: case 0xe: is_event = true; break;
    case 0x17: case 0x18: case 0x19: case 0x1a: is_event = true; break;
    case 0x1f: case 0x20: case 0x21: case 0x24: is_event = true; break;
    case 0x28:
    case 0x2a:
        is_event = true;
        swap_pending = (unsigned int)countLeadingZeros(1 - *(int *)(object + 0x3c)) >> 5;
        break;
    case 0x2e: special_id = 0x13; is_special = true; break;
    default: break;
    }

    if ((*(unsigned int *)(object + 0x34) & 4) != 0) {
        pending_flag = 1;
    }

    short *slot = object_anim_slot_resolve();
    if (slot == 0) {
        object_anim_commit_default();
        slot = object_anim_slot_resolve();
    }

    if ((object_anim_clip_is_active(object, (int)slot[2], (int)slot[3]) != 0) &&
        (object_anim_slot_userflags(object) != 0)) {
        *(int *)(object + 0xf8) = *(int *)(object + 0xe8);
        if (swap_pending != 0) {
            object_process_pending_swap(DAT_80611d74, 1);
        }
    }

    if ((object_anim_clip_is_active(object, (int)slot[2], (int)slot[3]) == 0) ||
        (pending_flag != 0) || ((*(unsigned int *)(object + 0x34) & 8) != 0)) {
        if ((*(int *)(object + 0xf8) == *(int *)(object + 0xe8)) &&
            ((pending_flag != 0) || ((*(unsigned int *)(object + 0x34) & 8) != 0))) {
            *(int *)(object + 0xf8) = 0;
        }
        object_anim_start_clip((double)*(float *)(object + 0xf4), (int)slot[3]);
        if ((*(unsigned int *)(object + 0x34) & 8) != 0) {
            int clip = *(int *)(*(int *)(object + 0x54) + 4);
            double speed = (double)(*(float *)(object + 0xf0) *
                *(float *)(*(int *)(DAT_80611824 + *(int *)(clip + 0xd0) * 4) + 0x18));
            anim_slot_set_speed(speed, clip + 0xcc);
            if ((object_has_active_channel_stub(DAT_80611d74) == 0) ||
                (*(int *)(object + 0x3c) == 1)) {
                anim_slot_set_speed(speed, *(int *)(*(int *)(DAT_80611d74 + 0xd1c) + 4) + 0xcc);
            } else {
                anim_slot_set_speed(speed, DAT_80611d74 + 0xd3c);
            }
        }
    }

    *(unsigned int *)(object + 0x34) = *(unsigned int *)(object + 0x34) & 0xfffffff1;
    *(int *)(object + 0xec) = *(int *)(*(int *)(*(int *)(object + 0x54) + 4) + 0xd4);
    *(float *)(object + 0xf0) = (float)object_anim_slot_phase(object);

    int type_id = (object == 0) ? *(int *)(def + 0x30) : *(int *)(object + 0x30);
    object_type_dispatch(0x13, type_id, object, def, 0);

    if (((is_event) || (special_id != 0) || (is_sound)) &&
        ((pending_flag == 0) && (*(int *)(object + 0xf8) != 0))) {
        if ((is_sound) && (object_anim_slot_resolve() != 0) && (FUN_801ab608() != 0)) {
            object_anim_commit_default();
        } else if (special_id == 0) {
            object_anim_pick_event(object);
            object_anim_commit_default();
        } else {
            object_anim_commit_default();
        }
        if ((is_special) && (object_type_has_flag80(object) != 0)) {
            *(unsigned int *)(*(int *)(object + 0x54) + 0x28) =
                *(unsigned int *)(*(int *)(object + 0x54) + 0x28) | 1;
        }
    }
    FUN_8038271c();
}

// 0x801a91d4: swap out an object's model - move the pending model handle (+0x94)
// into the saved slot (+0x9c) and clear +0x94/+0x98. Skipped for pooled objects
// while the system swap gate (DAT_806125d0) is active.
void object_model_swap_out(int object)
{
    if ((object != 0) && (*(int *)(object + 8) < DAT_80612610) && (DAT_806125d0 != 0)) {
        return;
    }
    if (*(int *)(object + 0x94) == 0) {
        return;
    }
    *(int *)(object + 0x9c) = *(int *)(object + 0x94);
    *(int *)(object + 0x94) = 0;
    *(int *)(object + 0x98) = 0;
}

// 0x801a9218: clear an object's pending model handles (+0x94/+0x98) and reset the
// three sub-object handle slots (+0x1c8/+0x528/+0x888) of the pooled instance to
// 3 via object_set_handle (handles at +0x184/+0x4e4/+0x844).
void object_model_clear_handles(int object)
{
    *(int *)(object + 0x94) = 0;
    *(int *)(object + 0x98) = 0;
    if ((object != 0) && (*(int *)(object + 8) < DAT_80612610)) {
        int inst = DAT_80611d78 + *(int *)(object + 8) * 0x141c;
        if (*(int *)(inst + 0x1c8) != 3) {
            object_set_handle(inst + 0x184, 3);
        }
        if (*(int *)(inst + 0x528) != 3) {
            object_set_handle(inst + 0x4e4, 3);
        }
        if (*(int *)(inst + 0x888) != 3) {
            object_set_handle(inst + 0x844, 3);
        }
    }
}

// 0x801a92b4: swap out an object's model then clear its handles.
void object_model_swap_out_and_clear(int object)
{
    object_model_swap_out(object);
    object_model_clear_handles(object);
}

// ===========================================================================
// Batch: batch_538 | Object weapon/type-slot selection + switch notification.
// Date: 2026-09-07
//
// Objects carry a "weapon/type slot" selection: instance+0x94 is the active slot
// index and instance+0x9c is the saved slot (for swap-out/in). Slots index two
// parallel per-slot tables (0x3c-byte stride, 0x46 == 70 entries):
//   DAT_8043e264[slot*0xf]  -> slot group id
//   DAT_8043e22c[slot*0xf]  -> slot flag word (bit2=grouped, bit4=persist,
//                              bit7=group base marker)
//   DAT_8043e238 / DAT_8043e23c[slot*0xf] -> primary/secondary param type ids
// The pooled instance (DAT_80611d78 + poolIndex*0x141c) keeps a per-group active
// byte table at +0xcd4 and its own type flag word at +0xc.
// ===========================================================================

// batch_538 per-slot tables (0x3c stride) and globals.
extern int   DAT_8043e264;            // per-slot group id
extern int   DAT_8043e22c;            // per-slot flag word
extern int   DAT_8043e23c;            // per-slot secondary param type id
extern int   DAT_8043f294;            // per-type name string-id table (0x54 stride via *0x15 words)
extern int   DAT_8043f2e4;            // per-type pickup callback table (0x54 stride)
extern char  DAT_805015bd;            // pickup-message mode byte
extern unsigned int DAT_805015c0;     // sound/notify suppress flags
extern int   DAT_80610b88;            // format string " %s"
extern int   s__s__s_80610b8c;        // format string "%s %s"
extern float FLOAT_80616bac;          // slot re-arm playback rate
extern float FLOAT_80611718;          // slot-switch sound distance/param

// Cross-module helpers (weapon/hud/sound/anim). Named helpers resolve through
// generated_decls; the raw FUN_ helpers are self-externed here.
extern int  FUN_801a9fa4(int object, unsigned int slot, int mode);  // slot occupancy test
extern int  FUN_801ab1e8(void);                                     // resolve default slot
extern int  FUN_801aa628(int *object, int type_id, unsigned int slot); // slot-switch predicate
extern int  FUN_801c1714(void);                                     // hud/context mode query
extern int  FUN_801b651c(int type_id);                              // resolve switch sound id
extern void FUN_80237e0c(int event, int pool_index, int type_id);   // object event 4 dispatch
extern int  FUN_803826e4(void);                                     // pair-frame accessor (returns object hi/lo)
extern void FUN_80382730(void);                                     // pair-frame scope close

// Named helpers (declared here to pin the exact call-site signatures used).
extern void anim_set_playback_rate(double rate, int *clip, int mode);
extern int  game_is_standard_arcade(void);
extern int  get_string_by_id(int string_id);
extern unsigned int *debug_log_formatted(unsigned long long f1, unsigned long long a2,
    unsigned long long a3, unsigned long long a4, unsigned long long a5,
    unsigned long long a6, unsigned long long a7, unsigned long long a8,
    void *fmt, int s0, unsigned int s1, int r6, int r7, int r8, int r9, int r10);
extern void hud_text_submit(int owner, int type, unsigned int *text);
extern void hud_weapon_select_begin_switch(int hud);
extern void sound_get_emitter_position(int *object, void *out_pos);
extern void sound_init_play_params(int params);
extern void sound_play_request(int *params);

// 0x801a95b8: set the object's active weapon/type slot. When the slot is grouped
// (flag bit2) but the object fails the primary anim predicate yet passes the
// secondary, walk back to the group-base slot (marked by flag bit7). Stores the
// resolved slot at +0x94 and records it in the pooled instance's per-group byte
// table (+0xcd4, indexed by the slot group id).
void object_set_weapon_slot(int object, int slot)
{
    if (((&DAT_8043e22c)[slot * 0xf] & 4) != 0) {
        if (FUN_801aaa10(object, 0) == 0) {
            if (FUN_801aaa10(object, 1) != 0) {
                slot = slot - 1;
                for (int off = slot * 0x3c;
                     (*(unsigned int *)((int)&DAT_8043e22c + off) & 0x80) == 0;
                     off = off - 0x3c) {
                    slot = slot - 1;
                }
            }
        }
    }
    *(int *)(object + 0x94) = slot;
    if ((object != 0) && (*(int *)(object + 8) < DAT_80612610)) {
        *(char *)(DAT_80611d78 + *(int *)(object + 8) * 0x141c + 0xcd4 +
                  (&DAT_8043e264)[slot * 0xf]) = (char)slot;
    }
}

// 0x801a92e8: activate a slot and mark the pooled instance's type flag 0x4000.
void object_activate_slot_tagged(int object, int slot)
{
    int inst = DAT_80611d78 + *(int *)(object + 8) * 0x141c;
    object_set_weapon_slot(object, slot);
    *(unsigned int *)(inst + 0xc) = *(unsigned int *)(inst + 0xc) | 0x4000;
}

// 0x801a932c: swap the saved slot back in - restore +0x9c into +0x94 and clear
// +0x9c. Skipped for pooled objects while the swap gate (DAT_806125d0) is active.
void object_model_swap_in(int object)
{
    if ((object != 0) && (*(int *)(object + 8) < DAT_80612610) && (DAT_806125d0 != 0)) {
        return;
    }
    if (*(int *)(object + 0x9c) == 0) {
        return;
    }
    *(int *)(object + 0x94) = *(int *)(object + 0x9c);
    *(int *)(object + 0x9c) = 0;
}

// 0x801a936c: enable the object slot-swap gate (blocks swap-in/out for pooled
// objects).
void object_swap_gate_enable(void)
{
    DAT_806125d0 = 1;
}

// 0x801a9378: disable the object slot-swap gate.
void object_swap_gate_disable(void)
{
    DAT_806125d0 = 0;
}

// 0x801a9694: resolve the group-base slot for a slot. Ungrouped slots (flag bit2
// clear) return unchanged; grouped slots walk back to the nearest slot marked as
// group-base (flag bit7).
int object_resolve_group_base_slot(int slot)
{
    if (((&DAT_8043e22c)[slot * 0xf] & 4) == 0) {
        return slot;
    }
    int base = slot - 1;
    for (int off = base * 0x3c;
         (*(unsigned int *)((int)&DAT_8043e22c + off) & 0x80) == 0;
         off = off - 0x3c) {
        base = base - 1;
    }
    return base;
}

// 0x801a96d0: if the object's current slot (+0x94) is grouped and fails the
// primary anim predicate but passes the secondary, re-select the group-base slot
// via object_set_weapon_slot.
void object_reselect_slot(int object)
{
    int slot = *(int *)(object + 0x94);
    if ((((&DAT_8043e22c)[slot * 0xf] & 4) != 0) && (FUN_801aaa10(object, 0) == 0)) {
        if (FUN_801aaa10(object, 1) != 0) {
            slot = slot - 1;
            for (int off = slot * 0x3c;
                 (*(unsigned int *)((int)&DAT_8043e22c + off) & 0x80) == 0;
                 off = off - 0x3c) {
                slot = slot - 1;
            }
        }
    }
    if (slot != *(int *)(object + 0x94)) {
        object_set_weapon_slot(object, slot);
    }
}

// 0x801a9384: apply a slot selection across the sub-object group. For every slot
// sharing the selected slot's group id (DAT_8043e264), set the per-slot active
// byte (+0x54c) when the flags permit. Then, for the pooled instance, update the
// per-group active byte table (+0xcd4) and trigger the HUD weapon-switch UI when
// this is a fresh selection; finally write the type's default params into the
// object param regions (+0xa4 primary, +0x1d4 secondary).
//
// The register-pair accessor (__save_gpr_25 form) delivers the object pointer in
// the high word and the selected slot index in the low word.
void object_slot_select_apply(int object, unsigned int slot)
{
    char prev_active = *(char *)(object + slot + 0x54c);
    int group_id = (&DAT_8043e264)[slot * 0xf];
    int slot_idx = 0;
    int off = 0;
    int remaining = 0x46;
    do {
        if (group_id == *(int *)((int)&DAT_8043e264 + off)) {
            if (((*(unsigned int *)((int)&DAT_8043e22c + off) & 4) == 0) ||
                (((&DAT_8043e22c)[slot * 0xf] & 4) != 0)) {
                *(char *)(object + slot_idx + 0x54c) = 1;
            }
        }
        slot_idx = slot_idx + 1;
        off = off + 0x3c;
        remaining = remaining - 1;
    } while (remaining != 0);

    if (object != 0) {
        if (*(int *)(object + 8) < DAT_80612610) {
            int inst = DAT_80611d78 + *(int *)(object + 8) * 0x141c;
            int grp = inst + group_id;
            unsigned int cur = (unsigned int)*(unsigned char *)(grp + 0xcd4);
            if ((cur != slot) && (FUN_801a9fa4(object, cur, 1) == 0)) {
                *(char *)(grp + 0xcd4) = (char)slot;
            }
            if ((prev_active == '\0') && (((&DAT_8043e22c)[slot * 0xf] & 0x10) == 0)) {
                hud_weapon_select_begin_switch(inst);
            }
        }
    }
    if ((((&DAT_8043e22c)[slot * 0xf] & 0x10) != 0) && (*(int *)(object + 0xa0) == 0)) {
        *(unsigned int *)(object + 0xa0) = slot;
    }
    if (prev_active == '\0') {
        object_write_default_params(object + 0xa4, (&DAT_8043e238)[slot * 0xf]);
        object_write_default_params(object + 0x1d4, (&DAT_8043e23c)[slot * 0xf]);
    }
}

// 0x801a94e0: deselect the current object's slot. Clears the per-slot active byte
// (+0x54c); if the active/saved slot equals this one, resolve a replacement via
// FUN_801ab1e8 (active -> set through object_set_weapon_slot; saved -> store into
// +0x9c). Refreshes the pooled per-group byte table entry.
void object_slot_deselect(void)
{
    unsigned long long acc = FUN_803826f0();
    int object = (int)(acc >> 0x20);
    unsigned int slot = (unsigned int)acc;
    *(char *)(object + slot + 0x54c) = 0;
    if (*(unsigned int *)(object + 0x94) == slot) {
        *(int *)(object + 0x94) = 0;
        int repl = FUN_801ab1e8();
        object_set_weapon_slot(object, repl);
    }
    if ((*(unsigned int *)(object + 0x9c) == slot) && (slot != 0)) {
        *(int *)(object + 0x9c) = 0;
        *(int *)(object + 0x9c) = FUN_801ab1e8();
    }
    if ((object != 0) && (*(int *)(object + 8) < DAT_80612610)) {
        int grp_off = *(int *)(object + 8) * 0x141c + 0xcd4 + (&DAT_8043e264)[slot * 0xf];
        if (*(unsigned char *)(DAT_80611d78 + grp_off) == slot) {
            int v = FUN_801aae80((&DAT_8043e264)[slot * 0xf]);
            *(char *)(DAT_80611d78 + grp_off) = (char)v;
        }
    }
    FUN_8038273c();
}

// 0x801a9780: switch the object's weapon/type slot and fire the associated HUD
// pickup text, switch sound and pickup callback. Applies the slot selection
// (object_slot_select_apply), re-arms the clip playback rate for switch-eligible
// slots (FUN_801aa628 predicate / standard-arcade type 0x22), then for a fresh
// selection with a bound entity emits the "picked up %s" HUD text (via
// get_string_by_id + debug_log_formatted + hud_text_submit), runs the per-type
// pickup callback (DAT_8043f2e4) and plays the switch sound (sound_play_request).
// Finally dispatches object event 4.
void object_slot_switch_notify(unsigned long long a1, unsigned long long a2,
    unsigned long long a3, unsigned long long a4, unsigned long long a5,
    unsigned long long a6, unsigned long long a7, unsigned long long a8)
{
    (void)a1;
    int local_pos[3];
    unsigned char params[0x60];   // sound play-params scratch (sound_init_play_params fills it)

    unsigned long long pair = FUN_803826e4();
    int *object = (int *)(pair >> 0x20);
    int type_id = (int)pair;
    unsigned int slot = (unsigned int)*(unsigned char *)((int)object + type_id + 0x54c);

    object_slot_select_apply((int)object, (unsigned int)type_id);
    object[10] = object[10] | 8;

    if ((FUN_801aa628(object, type_id, slot) != 0) ||
        ((type_id == 0x22) && (game_is_standard_arcade() != 0))) {
        anim_set_playback_rate((double)FLOAT_80616bac, *(int **)(*object + 4), 2);
        object_set_weapon_slot((int)object, type_id);
    }

    if ((object != (int *)0) && (object[2] < DAT_80612610)) {
        int inst = DAT_80611d78 + object[2] * 0x141c;
        if ((-1 < *(int *)(inst + 4)) &&
            (((slot == 0) || (type_id == 0x2f)) && (4 < DAT_80611928))) {
            unsigned int *text;
            int mode = FUN_801c1714();
            if ((mode == 4) ||
                ((FUN_801c1714() == 3) && (0 < *(int *)(inst + 4)))) {
                int name = get_string_by_id((&DAT_8043f294)[type_id * 0x15]);
                text = debug_log_formatted(a1, a2, a3, a4, a5, a6, a7, a8,
                    &DAT_80610b88, name, slot, 0, 0, 0, 0, 0);
            } else {
                int name = get_string_by_id((&DAT_8043f294)[type_id * 0x15]);
                int prefix = get_string_by_id(0x18a1);
                text = debug_log_formatted(a1, a2, a3, a4, a5, a6, a7, a8,
                    &s__s__s_80610b8c, prefix, name, 0, 0, 0, 0, 0);
            }
            if ((DAT_805015bd == '\n') && (slot == 0) &&
                (*(void (**)(int))(&DAT_8043f2e4 + type_id * 0x54) != (void (*)(int))0)) {
                (*(void (**)(int))(&DAT_8043f2e4 + type_id * 0x54))(*(int *)(inst + 4));
            }
            hud_text_submit(*(int *)(inst + 4), 2, text);
        }
        if (((DAT_805015c0 & 0x20000000) == 0) && ((DAT_805015c0 & 0x40000000) == 0)) {
            sound_get_emitter_position(object, &local_pos[0]);
            int sound_id = FUN_801b651c(type_id);
            sound_init_play_params((int)&params[0]);
            // Stack layout from the original: local_5c[0]=sound id, local_44=distance
            // param (float), local_40/3c/38 = emitter position xyz, local_34=flag(1).
            *(int *)&params[0x00] = sound_id;                   // local_5c[0]
            *(float *)&params[0x18] = FLOAT_80611718;           // local_44
            *(int *)&params[0x1c] = local_pos[0];               // local_40
            *(int *)&params[0x20] = local_pos[1];               // local_3c
            *(int *)&params[0x24] = local_pos[2];               // local_38
            params[0x28] = 1;                                   // local_34
            sound_play_request((int *)&params[0]);
        }
    }
    FUN_80237e0c(4, object[2], type_id);
    FUN_80382730();
}

// ===========================================================================
// Batch: batch_539 | Object sound-slot / ammo-count management. Date: 2026-09-07
//
// Objects keep two banks of per-channel 16-bit counters:
//   instance+0x61e  primary sound-param / ammo counters (per channel index)
//   instance+0xa4 / +0x1d4 + (chan*2 + 0x38)  secondary param blocks
// A per-type entry (DAT_80610ba0 + type*0x29c) names the primary channel (+0x14)
// and optional secondary channel (+0x1c), plus a scale multiplier (+0x18). The
// object's slot type index is at +0x98 (indexing DAT_8043e238/e23c param tables).
// These helpers add/clamp/consume those counters and dispatch the "consumed"
// object event (type 2).
// ===========================================================================

// batch_539 raw-symbol externs.
extern unsigned int DAT_806125fc;     // consume-suppress mask A
extern unsigned int DAT_80612600;     // consume-suppress mask B
extern int   DAT_80612614;            // level/mode id
extern int   DAT_805cc614;            // sub-mode id
extern int   DAT_80611790;            // consume-disable flag A
extern int   DAT_80611794;            // consume-disable flag B (gate)

extern int  FUN_803826d8(void);       // pair-frame accessor (object hi / channel lo)
extern void FUN_80382724(void);       // pair-frame scope close
extern void FUN_80382730(void);       // (declared batch_538)
extern unsigned int FUN_801c0894(void);// input/context bit query

// object_sound_slot_sum_secondary is defined below; forward declare for the
// accumulate helper that uses it.
int object_sound_slot_sum_secondary(int object, int channel);

// 0x801a9a8c: add a delta to a primary sound-param counter (+0x61e) for a
// channel, clamping so the counter plus the secondary-block sum does not exceed
// the type's resolved maximum (object_param_resolve). Object + channel arrive via
// the register-pair accessor in the original; passed explicitly here.
void object_sound_slot_accumulate(int object, int channel, short delta)
{
    int secondary_sum = object_sound_slot_sum_secondary(object, channel);
    int slot = object + channel * 2;
    int max_val = object_param_resolve(channel);
    *(short *)(slot + 0x61e) = (short)(*(short *)(slot + 0x61e) + delta);
    if (max_val < *(short *)(slot + 0x61e) + secondary_sum) {
        *(short *)(slot + 0x61e) = (short)(max_val - secondary_sum);
    }
}

// 0x801a9a14: register/add the type's sound-param values into the current
// object's slots (FUN_803826f0 accessor gives object + slot type index). Adds the
// primary channel value (param_ptr[0]) at type entry +0x14 and the secondary
// (param_ptr[1]) at +0x1c when present and distinct.
void object_sound_slot_register(int object_unused, int channel_unused, short *param_ptr)
{
    (void)object_unused;
    (void)channel_unused;
    unsigned long long acc = FUN_803826f0();
    int object = (int)(acc >> 0x20);
    int entry = DAT_80610ba0 + (int)acc * 0x29c;
    if (*(int *)(entry + 0x14) != 0) {
        object_sound_slot_accumulate(object, *(int *)(entry + 0x14), *param_ptr);
    }
    int sec = *(int *)(entry + 0x1c);
    if ((sec != 0) && (sec != *(int *)(entry + 0x14))) {
        object_sound_slot_accumulate(object, sec, param_ptr[1]);
    }
    FUN_8038273c();
}

// 0x801a99e0: add a secondary sound-param value using the object's secondary
// type-table entry (DAT_8043e23c).
void object_sound_slot_add_secondary(int object, int slot_index, short *param_ptr)
{
    object_sound_slot_register(object, (&DAT_8043e23c)[slot_index * 0xf], param_ptr);
}

// 0x801a9b0c: add a scaled sound-param value. Resolves the object's secondary
// type (DAT_8043e23c); when valid, multiplies the delta by the type's scale
// (+0x18, min 1) and accumulates into the type's primary channel (+0x14).
int object_sound_slot_add_scaled(int object, int slot_index, short delta)
{
    if ((&DAT_8043e23c)[slot_index * 0xf] == -1) {
        return 0;
    }
    int entry = DAT_80610ba0 + (&DAT_8043e23c)[slot_index * 0xf] * 0x29c;
    int scale = *(int *)(entry + 0x18);
    if (scale < 1) {
        scale = 1;
    }
    object_sound_slot_accumulate(object, *(int *)(entry + 0x14), (short)(delta * (short)scale));
    return 0;
}

// 0x801a9dd8: sum the secondary-block counter values (+0x38) that match a given
// channel across both param blocks (+0xa4 primary, +0x1d4 secondary). The object's
// slot type index (+0x98) selects a pair of param-type ids from DAT_8043e238.
int object_sound_slot_sum_secondary(int object, int channel)
{
    int sum = 0;
    int block_index = 0;
    int table_off = 0;
    int remaining = 2;
    do {
        int block = (block_index == 0) ? (object + 0xa4) : (object + 0x1d4);
        int type_id = *(int *)((int)&DAT_8043e238 +
                               *(int *)(object + 0x98) * 0x3c + table_off);
        if ((type_id != -1) && (block != 0)) {
            int entry = DAT_80610ba0 + type_id * 0x29c;
            int primary = *(int *)(entry + 0x14);
            if (primary == channel) {
                sum = sum + *(short *)(block + primary * 2 + 0x38);
            } else {
                int sec = *(int *)(entry + 0x1c);
                if ((sec != 0) && (sec == channel)) {
                    sum = sum + *(short *)(block + sec * 2 + 0x38);
                }
            }
        }
        block_index = block_index + 1;
        table_off = table_off + 4;
        remaining = remaining - 1;
    } while (remaining != 0);
    return sum;
}

// 0x801a9e90: predicate - the object's slot is active (+0x54c) and its secondary
// type resolves to a channel with remaining capacity (primary counter +0x61e plus
// secondary-block sum > 0).
int object_sound_slot_has_capacity(int object, int slot_index)
{
    if (*(char *)(object + slot_index + 0x54c) != '\0') {
        int channel = *(int *)(DAT_80610ba0 + (&DAT_8043e23c)[slot_index * 0xf] * 0x29c + 0x14);
        if (channel != 0) {
            int sum = object_sound_slot_sum_secondary(object, channel);
            if (0 < *(short *)(object + channel * 2 + 0x61e) + sum) {
                return 1;
            }
        }
    }
    return 0;
}

// 0x801a9b78: consume ammo/sound-param from a counter. Increments the usage
// counter (+0x10c). For mode >= 2 the secondary block (+0x38) is decremented,
// otherwise the primary counter (+0x61e); both floor at 0.
void object_ammo_consume(int object, int block, int channel, short amount, int mode)
{
    *(int *)(block + 0x10c) = *(int *)(block + 0x10c) + 1;
    if ((mode != 0) && (mode != 1)) {
        int slot = block + channel * 2;
        *(short *)(slot + 0x38) = (short)(*(short *)(slot + 0x38) - amount);
        if (*(short *)(slot + 0x38) < 0) {
            *(short *)(slot + 0x38) = 0;
        }
        return;
    }
    int slot = object + channel * 2;
    *(short *)(slot + 0x61e) = (short)(*(short *)(slot + 0x61e) - amount);
    if (*(short *)(slot + 0x61e) < 0) {
        *(short *)(slot + 0x61e) = 0;
    }
}

// 0x801a9dac: consume ammo, gated by the consume-disable flag DAT_80611794.
void object_ammo_consume_gated(int object, int block, int channel, short amount, int mode)
{
    if (DAT_80611794 == 0) {
        object_ammo_consume(object, block, channel, amount, mode);
    }
}

// 0x801a9bec: predicate - the object can consume ammo. True when the object has a
// handle (+0x44) and either the active context carries type flag 0x4000, or the
// pickup-message mode (DAT_805015bd == '\n') is set with match-slot flag 0x40.
int object_can_consume_ammo(int object)
{
    if (*(int *)(object + 0x44) == 0) {
        return 0;
    }
    unsigned int *slot = (unsigned int *)match_slot_field_ptr(
        DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c, 0);
    if ((*(unsigned int *)(DAT_80611d74 + 0xc) & 0x4000) == 0) {
        if ((DAT_805015bd != '\n') || ((*slot & 0x40) == 0)) {
            return 0;
        }
    }
    return 1;
}

// 0x801a9c68: consume-ammo dispatch (FUN_803826d8 accessor: object hi / channel
// lo). Gated by input/context suppress masks (FUN_801c0894 bits, level id
// DAT_80612614, sub-mode DAT_805cc614) and the DAT_805015c0/DAT_80611790 suppress
// flags. When permitted, decrements via object_ammo_consume; then if the primary
// counter reached 0, dispatches object event 2 (empty).
// Accessor pair: object (hi word) + the object-with-type "carrier" (lo word,
// iVar4). def/channel_arg/amount/mode are the caller's parameters (param_3..6).
void object_ammo_consume_dispatch(int arg0, int arg1, int def, int channel_arg,
                                  short amount, int mode)
{
    unsigned long long acc = FUN_803826d8();
    int *object = (int *)(acc >> 0x20);
    int carrier = (int)acc;
    (void)arg0;
    (void)arg1;
    int base = DAT_80610ba0 + *(int *)(carrier + 0x30) * 0x29c;
    unsigned int *slot = (unsigned int *)match_slot_field_ptr(base, *(int *)(def + 0x5c));
    int inst = *(int *)(*object + 0x150);
    unsigned int input_bits = FUN_801c0894();
    bool skip = false;
    if ((DAT_806125fc & DAT_80612600 & input_bits & 0x10) != 0) {
        if ((DAT_80612614 == 0x65) || (DAT_805cc614 == 5)) {
            skip = true;
        }
    }
    if (!skip) {
        if (((DAT_805015c0 & 0x100) == 0) && ((*(unsigned int *)(inst + 0xc) & 0x4000) == 0) &&
            (DAT_80611790 == 0)) {
            if ((DAT_805015bd != '\n') || ((*slot & 0x40) == 0)) {
                // object_ammo_consume(object, block=carrier, channel=channel_arg,
                //                     amount, mode)
                object_ammo_consume((int)object, carrier, channel_arg, amount, mode);
            }
        }
    }
    if (*(short *)((int)object + *(int *)(base + 0x14) * 2 + 0x61e) == 0) {
        int type_id = (def == 0) ? *(int *)(carrier + 0x30) : *(int *)(def + 0x30);
        object_type_dispatch(2, type_id, def, carrier, 0);
    }
    FUN_80382724();
}

// ===========================================================================
// Batch: batch_540 | Subsystem: game (object-base weapon-slot auto-switch)
// Date: 2026-09-07 | Addresses: 0x801a9f24 - 0x801aa9f0
//
// Weapon/type-slot AUTO-SWITCH decision layer. When a slot is picked up,
// dropped, or emptied, these routines decide which of the up-to-0x46 parallel
// per-slot type tables (0x3c stride) the object should activate next, honouring
// weapon-group membership (DAT_8043e264 group id + DAT_8043e22c flags), pickup
// priority (DAT_8043e230 MP / DAT_8043e234 arcade), and per-channel sound-slot
// capacity. The type entry table is DAT_80610ba0 (0x29c stride); the object's
// active-slot byte table is instance+0x54c and its per-channel sound counters
// live at instance+0x61e (primary channel +0x14, secondary +0x1c).
// ---------------------------------------------------------------------------

// ---- batch_540 per-file externs ----
extern int DAT_8043e264;              // per-slot weapon-group id (0x3c stride, +0x38 word)
extern int DAT_8043e234;              // per-slot arcade pickup priority
extern int DAT_8043e230;              // per-slot multiplayer pickup priority
extern float FLOAT_80616ba8;          // anim blend-weight full value (1.0)
// Default sound-slot parameter seed block (2x4 ints copied into the local
// accumulator by object_slot_apply_params).
extern int DAT_8061c188;
extern int DAT_8061c18c;
extern int DAT_8061c190;
extern int DAT_8061c194;
extern int DAT_8061c198;
extern int DAT_8061c19c;
extern int DAT_8061c1a0;
extern int DAT_8061c1a4;

// Register-pair spawn frames (object in high word, type index in low word).
extern unsigned long long __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern unsigned long long __save_gpr_28(void);
extern void __restore_gpr_28(void);

// Projectile-count stub reused as a slot-availability probe (batch_262).
extern int player_ai_projectile_iterate_all_stub(void);
// Object active-channel predicate (0x801ab790).
extern int FUN_801ab790(void);

// Forward declarations for within-batch call graph.
int  object_slot_secondary_has_capacity(int object, int type);
int  object_slot_primary_has_capacity(int object, int type);
int  object_slot_apply_params(int arg_object, int arg_type, int mode);

// 0x801a9f24: predicate - is there an ACTIVE grouped slot that shares this
// type's weapon group? Walks the object's active-slot byte table (+0x54c) and,
// for each live entry, compares the slot's group id (DAT_8043e264[i]) against
// this type's group id and requires the "grouped" flag (bit 2 of DAT_8043e22c).
int object_group_has_active_member(int object, int type)
{
    int i = 0;
    for (;;) {
        if (0x45 < i) {
            return 0;
        }
        int byte_off = 0;
        int remaining = 0x46;
        int slot = 0;
        do {
            i = slot;
            if (*(char *)(object + i + 0x54c) != '\0') {
                if (((&DAT_8043e264)[type * 0xf] == *(int *)((int)&DAT_8043e264 + byte_off)) &&
                    ((*(unsigned int *)((int)&DAT_8043e22c + byte_off) & 4) != 0)) {
                    return 1;
                }
            }
            byte_off = byte_off + 0x3c;
            remaining = remaining + -1;
            slot = i + 1;
        } while (remaining != 0);
        i = i + 2;
    }
}

// 0x801a9fa4: apply a type's default sound-slot parameters into the object and
// (re)activate the slot when there is capacity. Frame pair (object hi / type
// lo). Skips grouped/persist slots and slots blocked by an already-active group
// member; then seeds the primary (+0xa4) and secondary (+0x1d4) sound-slot
// blocks from the type's DAT_8043e238 channel table plus the DAT_8061c188 seed
// block, tallies the object's per-channel sound counters at +0x61e, checks
// capacity (mode 0 -> primary, else -> secondary), and if the two seed flags are
// still set OR capacity remains, reselects the slot via FUN_801aaa10.
int object_slot_apply_params(int arg_object, int arg_type, int mode)
{
    // object/type arrive in the r3:r4 register pair (read via __save_gpr_25),
    // matching the explicit (object, type) the group-cycle callers pass; mode
    // (r5/param_3) selects the primary-vs-secondary capacity check. Returns 1
    // when the slot was (re)selected via object_slot_switch_permitted.
    (void)arg_object; (void)arg_type;
    unsigned long long frame = __save_gpr_25();
    int object = (int)(frame >> 0x20);
    int type = (int)frame;
    int result = 0;

    if (type != 0) {
        if (type == 1) {
            if ((*(int *)(object + 8) < DAT_80612610) && ((DAT_805015c0 & 0x2000) == 0)) {
                countLeadingZeros((unsigned int)*(unsigned char *)(object + 0x584));
            }
        } else if ((DAT_80612610 <= *(int *)(object + 8)) ||
                   (((&DAT_8043e22c)[type * 0xf] & 2) == 0)) {
            unsigned int flags = (&DAT_8043e22c)[type * 0xf];
            if (((flags & 0x10) == 0) &&
                (((DAT_80612610 <= *(int *)(object + 8) || ((flags & 0x100) == 0)) ||
                  (object_group_has_active_member(object, type) == 0))) &&
                (*(char *)(object + type + 0x54c) != '\0')) {

                int seed[18];
                seed[6] = DAT_8061c188;  seed[7] = DAT_8061c18c;
                seed[4] = DAT_8061c190;  seed[5] = DAT_8061c194;
                seed[2] = DAT_8061c198;  seed[3] = DAT_8061c19c;
                seed[0] = DAT_8061c1a0;  seed[1] = DAT_8061c1a4;
                seed[8] = 1;
                seed[9] = 1;

                // Resolve primary/secondary channel type ids from the object's
                // slot-type table (instance+0x98 -> DAT_8043e238) and cache the
                // matching default param values (+0x38 stride within +0xa4/+0x1d4).
                int chan = 0;
                int off = 0;
                int passes = 2;
                do {
                    int type_id = *(int *)((int)&DAT_8043e238 +
                                           *(int *)(object + 0x98) * 0x3c + off);
                    if (type_id != -1) {
                        int block = (chan == 0) ? (object + 0xa4) : (object + 0x1d4);
                        if (block != 0) {
                            int entry = DAT_80610ba0 + type_id * 0x29c;
                            int prim = *(int *)(entry + 0x14);
                            int sec = *(int *)(entry + 0x1c);
                            *(int *)((int)seed + off + 0x10) = prim;
                            *(int *)((int)seed + off + 0x18) =
                                (int)*(short *)(block + prim * 2 + 0x38);
                            if ((sec != 0) && (sec != prim)) {
                                short sv = *(short *)(block + sec * 2 + 0x38);
                                *(int *)((int)seed + off) = sec;
                                *(int *)((int)seed + off + 8) = (int)sv;
                            }
                        }
                    }
                    chan = chan + 1;
                    off = off + 4;
                    passes = passes + -1;
                } while (passes != 0);

                // Accumulate the object's per-channel sound-slot counters
                // (instance+0x61e) plus any seed offsets that match this slot's
                // primary/secondary channel ids.
                int i = 0;
                int rem = 2;
                do {
                    int type_id = *(int *)((int)(&DAT_8043e22c + type * 0xf) + i + 0xc);
                    if (type_id != -1) {
                        int entry = DAT_80610ba0 + type_id * 0x29c;
                        int prim = *(int *)(entry + 0x14);
                        int sec = *(int *)(entry + 0x1c);
                        *(int *)((int)seed + i + 0x20) =
                            (int)*(short *)(object + prim * 2 + 0x61e);
                        if ((sec != 0) && (sec != prim)) {
                            *(int *)((int)seed + i + 0x20) =
                                *(int *)((int)seed + i + 0x20) +
                                (int)*(short *)(object + sec * 2 + 0x61e);
                        }
                        if (prim == seed[4]) {
                            *(int *)((int)seed + i + 0x20) =
                                *(int *)((int)seed + i + 0x20) + seed[6];
                        }
                        if (prim == seed[5]) {
                            *(int *)((int)seed + i + 0x20) =
                                *(int *)((int)seed + i + 0x20) + seed[7];
                        }
                        if (sec != 0) {
                            if (sec == seed[0]) {
                                *(int *)((int)seed + i + 0x20) =
                                    *(int *)((int)seed + i + 0x20) + seed[2];
                            }
                            if (sec == seed[1]) {
                                *(int *)((int)seed + i + 0x20) =
                                    *(int *)((int)seed + i + 0x20) + seed[3];
                            }
                        }
                    }
                    i = i + 4;
                    rem = rem + -1;
                } while (rem != 0);

                unsigned int capacity;
                if (mode == 0) {
                    capacity = (unsigned int)object_slot_primary_has_capacity(object, type);
                } else {
                    capacity = (unsigned int)object_slot_secondary_has_capacity(object, type);
                }
                if (((seed[8] != 0) && (seed[9] != 0)) || (capacity != 0)) {
                    result = object_slot_switch_permitted(object, 0);
                }
            }
        }
    }
    __restore_gpr_25();
    return result;
}

// 0x801aa3c0: deselect-and-reselect driver. Frame pair (object hi / active-type
// lo). First iterates every FREE slot (byte +0x54c == 0) whose primary/secondary
// channel (DAT_8043e23c) is grouped/persist (flag 0x210) and bound to the active
// type, dispatching object_slot_switch_notify for each. Then, if not suppressed
// (param_11 == 0), the object model (+0x1398) is present and its group mode
// (+0x68) is 0 or 2, auto-picks the first eligible LIVE slot sharing the active
// type's channels with zero counters and >= priority, and activates it via
// object_set_weapon_slot.
void object_slot_deselect_and_reselect(unsigned long long a1, unsigned long long a2,
                                       unsigned long long a3, unsigned long long a4,
                                       unsigned long long a5, unsigned long long a6,
                                       unsigned long long a7, unsigned long long a8,
                                       unsigned int a9, unsigned int a10, int suppress)
{
    unsigned long long frame = __save_gpr_25();
    int *object = (int *)(frame >> 0x20);
    int type = (int)frame;
    (void)a9; (void)a10;

    int model = *(int *)(*object + 0x150);
    if (model != 0) {
        int slot = 1;
        int off = 0x3c;
        do {
            if (*(char *)((int)object + slot + 0x54c) == '\0') {
                if ((*(int *)((int)&DAT_8043e23c + off) != -1) &&
                    ((*(unsigned int *)((int)&DAT_8043e22c + off) & 0x210) != 0)) {
                    int entry = DAT_80610ba0 + *(int *)((int)&DAT_8043e23c + off) * 0x29c;
                    int chan = *(int *)(entry + 0x14);
                    if ((chan == type) ||
                        ((chan = *(int *)(entry + 0x1c), chan != 0 && (chan == type)))) {
                        object_slot_switch_notify(a1, a2, a3, a4, a5, a6, a7, a8);
                    }
                }
            }
            slot = slot + 1;
            off = off + 0x3c;
        } while (slot < 0x46);

        if (((suppress == 0) && (*(int *)(model + 0x1398) != 0)) &&
            (-1 < *(int *)(model + 4))) {
            int mode = *(int *)(*(int *)(model + 0x1398) + 0x68);
            if (mode == 0 || mode == 2) {
                int slot2 = 1;
                int off2 = 0x3c;
                do {
                    if (*(char *)((int)object + slot2 + 0x54c) != '\0') {
                        bool bound = false;
                        short prim_ct = 0;
                        short sec_ct = 0;
                        if (*(int *)((int)&DAT_8043e238 + off2) != -1) {
                            int entry = DAT_80610ba0 + *(int *)((int)&DAT_8043e238 + off2) * 0x29c;
                            int chan = *(int *)(entry + 0x14);
                            if ((chan == type) ||
                                ((chan = *(int *)(entry + 0x1c), chan != 0 && (chan == type)))) {
                                bound = true;
                                prim_ct = *(short *)((int)object + type * 2 + 0xdc);
                            }
                        }
                        if (*(int *)((int)&DAT_8043e23c + off2) != -1) {
                            int entry = DAT_80610ba0 + *(int *)((int)&DAT_8043e23c + off2) * 0x29c;
                            int chan = *(int *)(entry + 0x14);
                            if ((chan == type) ||
                                ((chan = *(int *)(entry + 0x1c), chan != 0 && (chan == type)))) {
                                bound = true;
                                sec_ct = *(short *)((int)object + type * 2 + 0x20c);
                            }
                        }
                        if ((bound && (prim_ct == 0) && (sec_ct == 0)) &&
                            (*(int *)((int)&DAT_8043e234 + off2) <=
                             (int)(&DAT_8043e234)[object[0x25] * 0xf])) {
                            object_set_weapon_slot((int)object, slot2);
                        }
                    }
                    slot2 = slot2 + 1;
                    off2 = off2 + 0x3c;
                } while (slot2 < 0x46);
            }
        }
    }
    __restore_gpr_25();
}

// 0x801aa5e4: predicate - do two slots share the SAME secondary channel while
// the first slot has no primary channel? Used to detect ammo-only companion
// slots that should follow the primary weapon.
int object_slots_share_secondary(int slot_a, int slot_b)
{
    if (((&DAT_8043e23c)[slot_a * 0xf] == (&DAT_8043e23c)[slot_b * 0xf]) &&
        ((&DAT_8043e238)[slot_a * 0xf] == -1)) {
        return 1;
    }
    return 0;
}

// 0x801aa628: auto-select the best in-group slot after a change. Frame pair
// (object hi / candidate-slot lo). Resolves the object's weapon-group mode from
// the model (+0x1398 -> +0x68), and unless the candidate merely shares a
// secondary channel with the active slot, applies the candidate via
// object_slot_apply_params when its pickup priority beats the active slot's
// (arcade DAT_8043e234 in level 0x65, else MP DAT_8043e230). Mode gates:
//   0        -> always eligible
//   2        -> priority compare
//   4        -> priority compare only when not suppressed (suppress==0)
//   default  -> apply when not suppressed
void object_slot_auto_select(unsigned int candidate, unsigned int arg1, int suppress)
{
    unsigned long long frame = __save_gpr_28();
    int *object = (int *)(frame >> 0x20);
    int slot = (int)frame;
    (void)candidate; (void)arg1;

    int mode = 2;
    if ((*(int *)(*object + 0x150) != 0) &&
        (*(int *)(*(int *)(*object + 0x150) + 0x1398) != 0)) {
        mode = *(int *)(*(int *)(*(int *)(*object + 0x150) + 0x1398) + 0x68);
    }

    if (((DAT_80612610 <= object[2]) ||
         (((slot != 0x38 && (slot != 0x39)) || (object[0x25] != 1)))) &&
        (object_slots_share_secondary(slot, object[0x25]) == 0)) {
        if (mode == 2) {
            if (object[2] < DAT_80612610) {
                if ((int)(&DAT_8043e234)[slot * 0xf] <=
                    (int)(&DAT_8043e234)[object[0x25] * 0xf]) {
                    object_slot_apply_params((int)object, slot, 0);
                }
            } else if (*(int *)(&DAT_8043e230 + slot * 0x3c) <=
                       *(int *)(&DAT_8043e230 + object[0x25] * 0x3c)) {
                object_slot_apply_params((int)object, slot, 0);
            }
        } else if (mode < 2) {
            if (mode == 0) {
                object_slot_apply_params((int)object, slot, 0);
            }
        } else if (mode == 4) {
            if (suppress == 0) {
                if (object[2] < DAT_80612610) {
                    if ((int)(&DAT_8043e234)[slot * 0xf] <=
                        (int)(&DAT_8043e234)[object[0x25] * 0xf]) {
                        object_slot_apply_params((int)object, slot, 0);
                    }
                } else if (*(int *)(&DAT_8043e230 + slot * 0x3c) <=
                           *(int *)(&DAT_8043e230 + object[0x25] * 0x3c)) {
                    object_slot_apply_params((int)object, slot, 0);
                }
            }
        } else if ((mode < 4) && (suppress == 0)) {
            object_slot_apply_params((int)object, slot, 0);
        }
    }
    __restore_gpr_28();
}

// 0x801aa864: secondary-channel capacity predicate. Slot types 0x2b/0x2c/0x2e/
// 0x2f are always "full" (no capacity); type 0x2d defers to the projectile
// count stub; all others report capacity.
int object_slot_secondary_has_capacity(int object, int type)
{
    (void)object;
    char has;
    if (((unsigned int)(type - 0x2e) < 2) || (type == 0x2c) || (type == 0x2b)) {
        has = '\0';
    } else if (type == 0x2d) {
        int count = player_ai_projectile_iterate_all_stub();
        has = (char)('\x01' - (count == 0));
    } else {
        has = '\x01';
    }
    return (int)has;
}

// 0x801aa8c0: primary-channel capacity predicate. Type 0x2d defers to the
// projectile count stub; otherwise the slot has capacity when the type entry's
// primary (+0x14) and secondary (+0x1c) channel ids are both zero (unassigned).
int object_slot_primary_has_capacity(int object, int type)
{
    (void)object;
    if (type == 0x2d) {
        if (player_ai_projectile_iterate_all_stub() != 0) {
            return 1;
        }
    } else {
        int entry = DAT_80610ba0 + (&DAT_8043e23c)[type * 0xf] * 0x29c;
        if ((*(int *)(entry + 0x14) == 0) && (*(int *)(entry + 0x1c) == 0)) {
            return 1;
        }
    }
    return 0;
}

// 0x801aa93c: combined slot-activate gate - true when the object's active
// channel probe (FUN_801ab790) reports empty AND the primary channel has
// capacity. Both operands are folded through the CLZ>>5 is-zero idiom.
int object_slot_can_activate(int object, int type)
{
    unsigned int active = countLeadingZeros(FUN_801ab790());
    unsigned int cap = countLeadingZeros(object_slot_primary_has_capacity(object, type));
    if ((active >> 5 != 0) && (cap >> 5 != 0)) {
        return 1;
    }
    return 0;
}

// 0x801aa9a8: anim blend-weight for the object's channel state (+0x44). States
// 3/4/5 use the full weight (1.0); state 2 uses (full - interp +0xf0); state 6
// returns the raw interp value; other states use the default field-0x48 value.
double object_anim_slot_blend_weight(int object)
{
    int state = *(int *)(object + 0x44);
    float weight;
    if ((state == 3) || (state == 4) || (weight = FLOAT_80616ba8, state == 5)) {
        weight = FLOAT_80616b98;
    }
    if (state == 2) {
        weight = FLOAT_80616ba8 - *(float *)(object + 0xf0);
    }
    if (state != 6) {
        return (double)weight;
    }
    return (double)*(float *)(object + 0xf0);
}

// 0x801aa9f0: alignment stub - forwards to the projectile-count probe and
// discards the result.
void object_slot_stub(void)
{
    player_ai_projectile_iterate_all_stub();
}

// ===========================================================================
// Batch: batch_541 | Subsystem: game (object-base weapon-slot group cycling)
// Date: 2026-09-07 | Addresses: 0x801aaa10 - 0x801aad8c
//
// Weapon-group CYCLING + switch-permission layer sitting on top of the
// batch_540 auto-switch decision code. The per-slot type tables are indexed in
// display order by DAT_8044098c (0x46 shorts, slot-order array) and grouped by
// DAT_8043e264 (group id, built by object_build_group_index_table). Cycling
// next/previous weapon within a group walks that order array, matching group id
// and applying each candidate via object_slot_apply_params.
// ---------------------------------------------------------------------------
// 0x801aa9f0: alignment stub - forwards to the projectile-count probe. The
// probe's slot count remains in the result register (r3) on return, so callers
// that need the slot/projectile state read it (object_weapon_dispatch_93ec),
// while object-base callers discard it.
int object_slot_stub(void)
{
    return player_ai_projectile_iterate_all_stub();
}
    return player_ai_projectile_iterate_all_stub();
}/ object-with-type carrier (lo word); FUN_80382724 closes the scope.
extern unsigned long long FUN_803826d8(void);
extern void FUN_80382724(void);

// 0x801aaa10: is a slot switch permitted for this object right now? Objects that
// carry a grab/hold link (+0x80 != 0) block the switch when mode is 0 or the
// global suppress-mask has the switch-inhibit bit (0x1000) set; otherwise the
// switch is allowed. This is the gate object_slot_apply_params /
// object_slot_deselect_and_reselect consult before committing a slot change.
int object_slot_switch_permitted(int object, int mode)
{
    if (*(int *)(object + 0x80) != 0) {
        if ((mode == 0) || ((DAT_805015c0 & 0x1000) != 0)) {
            return 0;
        }
    }
    return 1;
}

// 0x801aaa48: switch-permitted probe for the current object (FUN_803826f0
// accessor: object hi / mode lo).
int object_slot_switch_permitted_current(void)
{
    unsigned long long acc = FUN_803826f0();
    int result = object_slot_switch_permitted((int)(acc >> 0x20), (int)acc);
    FUN_8038273c();
    return result;
}

// 0x801aaabc: true when the current object permits a switch on two consecutive
// probes (the decompiler shows the accessor queried twice; both must pass).
int object_slot_switch_permitted_twice(void)
{
    int result = 0;
    if ((object_slot_switch_permitted_current() != 0) &&
        (object_slot_switch_permitted_current() != 0)) {
        result = 1;
    }
    return result;
}

// 0x801aab1c: is this object being carried by a GRABBING local player? Resolves
// the pooled player instance (DAT_80611d78 + player-index*0x141c) for objects
// with a valid player index (+8 < DAT_80612610) and no active-slot override
// (+0x94 == 0), then requires a live grabbed body (+0x538) whose grab-state
// (+0x590) is set and whose linked body def carries flag 0x8000.
int object_carried_by_grabbing_player(int object)
{
    if ((((object != 0) && (*(int *)(object + 8) < DAT_80612610)) &&
         (*(int *)(object + 0x94) == 0))) {
        int inst = DAT_80611d78 + *(int *)(object + 8) * 0x141c;
        int body = *(int *)(inst + 0x538);
        if (body != 0) {
            if ((*(int *)(inst + 0x590) != 0) &&
                ((*(unsigned int *)(*(int *)(body + 4) + 0x30) & 0x8000) != 0)) {
                return 1;
            }
        }
    }
    return 0;
}

// 0x801aab84: does the object's active slot type carry a non-unit scale? Looks
// up the slot type (instance+0x98 -> DAT_8043e23c) and its type entry
// (DAT_80610ba0); returns 0 (no scale) when both scale fields +0x34 and +0x38
// equal 1.0, else 1.
int object_slot_has_scale(int object)
{
    if (((object != 0) &&
         (0 < (&DAT_8043e23c)[*(int *)(object + 0x98) * 0xf])) &&
        ((&DAT_8043e23c)[*(int *)(object + 0x98) * 0xf] < 0x46)) {
        int entry = DAT_80610ba0 + (&DAT_8043e23c)[*(int *)(object + 0x98) * 0xf] * 0x29c;
        if ((FLOAT_80616ba8 == *(float *)(entry + 0x34)) &&
            (FLOAT_80616ba8 == *(float *)(entry + 0x38))) {
            return 0;
        }
    }
    return 1;
}

// 0x801aabec: (re)build the per-slot weapon-group index table. Walks all 0x46
// slots; a slot begins a new group (group counter DAT_806125c4++) unless it is
// flagged grouped (bit 2) or persist (bit 3) of DAT_8043e22c - those inherit the
// running group id. Each slot's group id is written to DAT_8043e264.
void object_build_group_index_table(void)
{
    DAT_806125c4 = -1;
    int off = 0;
    int remaining = 0x46;
    do {
        if (((*(unsigned int *)((int)&DAT_8043e22c + off) & 8) == 0) &&
            ((*(unsigned int *)((int)&DAT_8043e22c + off) & 4) == 0)) {
            DAT_806125c4 = DAT_806125c4 + 1;
        }
        *(int *)((int)&DAT_8043e264 + off) = DAT_806125c4;
        off = off + 0x3c;
        remaining = remaining + -1;
    } while (remaining != 0);
}

// 0x801aac38: is the object's player index at the local-player boundary? A
// branchless (index == DAT_80612610) test (XOR + sign-bit extract) used to
// clamp cycling wraparound to the local player's slot range.
unsigned int object_slot_index_is_boundary(int object)
{
    unsigned int diff = DAT_80612610 ^ *(unsigned int *)(object + 8);
    return (unsigned int)(((int)diff >> 1) - (diff & DAT_80612610)) >> 0x1f;
}

// 0x801aac58: find a slot type's position in the display-order array
// (DAT_8044098c, 0x46 entries). Returns 0 when not found (also the first slot).
int object_slot_order_index(int type)
{
    int index = 0;
    int off = 0;
    int remaining = 0x46;
    do {
        if (type == *(int *)((int)&DAT_8044098c + off)) {
            return index;
        }
        index = index + 1;
        off = off + 4;
        remaining = remaining + -1;
    } while (remaining != 0);
    return 0;
}

// 0x801aac98: cycle FORWARD to the next weapon in the active slot's group.
// Accessor pair (object hi / active-slot lo). Starting from the active slot's
// order position, advances through DAT_8044098c (wrapping at 0x46 to the
// boundary index when param_3 permits), and for the first entry in the same
// group whose object_slot_apply_params reports a successful reselect, stops.
void object_slot_cycle_next_in_group(unsigned int arg0, unsigned int arg1, int wrap)
    unsigned int start = (unsigned int)object_slot_order_index(active);
    unsigned int boundary = object_slot_index_is_boundary(object);
    int slot = 0;
    if (active < (int)boundary) {
        FUN_80382724();
        return slot;
    }
    unsigned int i = start;
    do {
        i = i + 1;
        i = i + 1;
        if (0x45 < (int)i) {
            if (wrap == 0) {
                break;
            }
            i = object_slot_index_is_boundary(object);
        }
        slot = (&DAT_8044098c)[i];
        if (((&DAT_8043e264)[slot * 0xf] == (&DAT_8043e264)[active * 0xf]) &&
            (object_slot_apply_params(object, slot, 1) != 0)) {
            break;
        }
        if (start == i) {
            break;
        }
    } while (true);
    FUN_80382724();
    return slot;
}       }
        if (start == i) {
            break;
        }
    } while (true);
    FUN_80382724();
}

// 0x801aad8c: cycle BACKWARD to the previous weapon in the active slot's group.
    int start = object_slot_order_index(active);
    unsigned int boundary = object_slot_index_is_boundary(object);
    int slot = 0;
    if (active < (int)boundary) {
        FUN_80382724();
        return slot;
    }
    int i = start;
    do {
        i = i - 1;
        boundary = object_slot_index_is_boundary(object);
        if (i < (int)boundary) {
            if (wrap == 0) {
                break;
            }
            i = 0x45;
        }
        slot = (&DAT_8044098c)[i];
        if (((&DAT_8043e264)[slot * 0xf] == (&DAT_8043e264)[active * 0xf]) &&
            (object_slot_apply_params(object, slot, 1) != 0)) {
            break;
        }
        if (start == i) {
            break;
        }
    } while (true);
    FUN_80382724();
    return slot;
}       if (((&DAT_8043e264)[(&DAT_8044098c)[i] * 0xf] == (&DAT_8043e264)[active * 0xf]) &&
            (object_slot_apply_params(object, (&DAT_8044098c)[i], 1) != 0)) {
            break;
        }
        if (start == i) {
            break;
        }
    } while (true);
    FUN_80382724();
}

// ===========================================================================
// Batch: batch_541 | Subsystem: game (object-base weapon-slot group cycling)
// Date: 2026-09-07 | Addresses: 0x801aaa10 - 0x801aad8c
//
// Weapon-group CYCLING + switch-permission layer sitting on top of the
// batch_540 auto-switch decision code. The per-slot type tables are indexed in
// display order by DAT_8044098c (0x46 shorts, slot-order array) and grouped by
// DAT_8043e264 (group id, built by object_build_group_index_table).
// ---------------------------------------------------------------------------

// ---- batch_541 per-file externs ----
extern int DAT_8044098c;              // slot display-order array (0x46 shorts, 4-byte stride)
extern unsigned long long FUN_803826d8(void);  // object/carrier accessor
extern void FUN_80382724(void);

// 0x801aaa10: is a slot switch permitted for this object right now? Objects that
// carry a grab/hold link (+0x80 != 0) block the switch when mode is 0 or the
// global suppress-mask has the switch-inhibit bit (0x1000) set.
int object_slot_switch_permitted(int object, int mode)
{
    if (*(int *)(object + 0x80) != 0) {
        if ((mode == 0) || ((DAT_805015c0 & 0x1000) != 0)) {
            return 0;
        }
    }
    return 1;
}

// 0x801aaa48: switch-permitted probe for the current object (FUN_803826f0
// accessor: object hi / mode lo).
int object_slot_switch_permitted_current(void)
{
    unsigned long long acc = FUN_803826f0();
    int result = object_slot_switch_permitted((int)(acc >> 0x20), (int)acc);
    FUN_8038273c();
    return result;
}

// 0x801aaabc: true when the current object permits a switch on two consecutive
// probes (the decompiler shows the accessor queried twice; both must pass).
int object_slot_switch_permitted_twice(void)
{
    int result = 0;
    if ((object_slot_switch_permitted_current() != 0) &&
        (object_slot_switch_permitted_current() != 0)) {
        result = 1;
    }
    return result;
}

// 0x801aab1c: is this object being carried by a GRABBING local player? Resolves
// the pooled player instance (DAT_80611d78 + player-index*0x141c) for objects
// with a valid player index (+8 < DAT_80612610) and no active-slot override
// (+0x94 == 0), then requires a live grabbed body (+0x538) whose grab-state
// (+0x590) is set and whose linked body def carries flag 0x8000.
int object_carried_by_grabbing_player(int object)
{
    if ((((object != 0) && (*(int *)(object + 8) < DAT_80612610)) &&
         (*(int *)(object + 0x94) == 0))) {
        int inst = DAT_80611d78 + *(int *)(object + 8) * 0x141c;
        int body = *(int *)(inst + 0x538);
        if (body != 0) {
            if ((*(int *)(inst + 0x590) != 0) &&
                ((*(unsigned int *)(*(int *)(body + 4) + 0x30) & 0x8000) != 0)) {
                return 1;
            }
        }
    }
    return 0;
}

// 0x801aab84: does the object's active slot type carry a non-unit scale? Looks
// up the slot type (instance+0x98 -> DAT_8043e23c) and its type entry
// (DAT_80610ba0); returns 0 (no scale) when both scale fields +0x34 and +0x38
// equal 1.0, else 1.
int object_slot_has_scale(int object)
{
    if (((object != 0) &&
         (0 < (&DAT_8043e23c)[*(int *)(object + 0x98) * 0xf])) &&
        ((&DAT_8043e23c)[*(int *)(object + 0x98) * 0xf] < 0x46)) {
        int entry = DAT_80610ba0 + (&DAT_8043e23c)[*(int *)(object + 0x98) * 0xf] * 0x29c;
        if ((FLOAT_80616ba8 == *(float *)(entry + 0x34)) &&
            (FLOAT_80616ba8 == *(float *)(entry + 0x38))) {
            return 0;
        }
    }
    return 1;
}

// 0x801aabec: (re)build the per-slot weapon-group index table. Walks all 0x46
// slots; a slot begins a new group (group counter DAT_806125c4++) unless it is
// flagged grouped (bit 2) or persist (bit 3) of DAT_8043e22c - those inherit the
// running group id. Each slot's group id is written to DAT_8043e264.
void object_build_group_index_table(void)
{
    DAT_806125c4 = -1;
    int off = 0;
    int remaining = 0x46;
    do {
        if (((*(unsigned int *)((int)&DAT_8043e22c + off) & 8) == 0) &&
            ((*(unsigned int *)((int)&DAT_8043e22c + off) & 4) == 0)) {
            DAT_806125c4 = DAT_806125c4 + 1;
        }
        *(int *)((int)&DAT_8043e264 + off) = DAT_806125c4;
        off = off + 0x3c;
        remaining = remaining + -1;
    } while (remaining != 0);
}

// 0x801aac38: is the object's player index at the local-player boundary? A
// branchless (index == DAT_80612610) test (XOR + sign-bit extract) used to
// clamp cycling wraparound to the local player's slot range.
unsigned int object_slot_index_is_boundary(int object)
{
    unsigned int diff = DAT_80612610 ^ *(unsigned int *)(object + 8);
    return (unsigned int)(((int)diff >> 1) - (diff & DAT_80612610)) >> 0x1f;
}

// 0x801aac58: find a slot type's position in the display-order array
// (DAT_8044098c, 0x46 entries). Returns 0 when not found (also the first slot).
int object_slot_order_index(int type)
{
    int index = 0;
    int off = 0;
    int remaining = 0x46;
    do {
        if (type == *(int *)((int)&DAT_8044098c + off)) {
            return index;
        }
        index = index + 1;
        off = off + 4;
        remaining = remaining + -1;
    } while (remaining != 0);
    return 0;
}

// 0x801aac98: cycle FORWARD to the next weapon in the active slot's group.
// Accessor pair (object hi / active-slot lo). Starting from the active slot's
// order position, advances through DAT_8044098c (wrapping at 0x46 to the
// boundary index when wrap permits), and stops at the first same-group entry
// whose object_slot_apply_params reports a successful reselect.
void object_slot_cycle_next_in_group(unsigned int arg0, unsigned int arg1, int wrap)
{
    unsigned long long acc = FUN_803826d8();
    int object = (int)(acc >> 0x20);
    int active = (int)acc;
    (void)arg0; (void)arg1;

    unsigned int start = (unsigned int)object_slot_order_index(active);
    unsigned int boundary = object_slot_index_is_boundary(object);
    if (active < (int)boundary) {
        FUN_80382724();
        return;
    }
    unsigned int i = start;
    do {
        i = i + 1;
        if (0x45 < (int)i) {
            if (wrap == 0) {
                break;
            }
            i = object_slot_index_is_boundary(object);
        }
        if (((&DAT_8043e264)[(&DAT_8044098c)[i] * 0xf] == (&DAT_8043e264)[active * 0xf]) &&
            (object_slot_apply_params(object, (&DAT_8044098c)[i], 1) != 0)) {
            break;
        }
        if (start == i) {
            break;
        }
    } while (true);
    FUN_80382724();
}

// 0x801aad8c: cycle BACKWARD to the previous weapon in the active slot's group.
// Mirror of object_slot_cycle_next_in_group, decrementing through DAT_8044098c
// (wrapping at the low end to 0x45 when wrap permits).
void object_slot_cycle_prev_in_group(unsigned int arg0, unsigned int arg1, int wrap)
{
    unsigned long long acc = FUN_803826d8();
    int object = (int)(acc >> 0x20);
    int active = (int)acc;
    (void)arg0; (void)arg1;

        (object_slot_primary_has_capacity((int)object, object[0x25]) == 0)) {
    unsigned int boundary = object_slot_index_is_boundary(object);
    if (active < (int)boundary) {
        FUN_80382724();
        return;
    }
    int i = start;
    do {
        i = i - 1;
        boundary = object_slot_index_is_boundary(object);
        if (i < (int)boundary) {
            if (wrap == 0) {
                break;
            }
            i = 0x45;
        }
        if (((&DAT_8043e264)[(&DAT_8044098c)[i] * 0xf] == (&DAT_8043e264)[active * 0xf]) &&
            (object_slot_apply_params(object, (&DAT_8044098c)[i], 1) != 0)) {
            break;
        }
        if (start == i) {
            break;
        }
    } while (true);
    FUN_80382724();
}

// ===========================================================================
// Batch: batch_544 | Object weapon-attachment geometry: muzzle/ejection point
// resolution, bone-attach transforms, view/projection, shell-casing spawn.
// Date: 2026-09-07 | Addresses: 0x801ac024 - 0x801aca04
//
// These routines resolve world/screen positions for weapon attachment points
// (muzzle, shell ejection) from the object's render-entity bone palette. The
// render entity lives at object+0x54; its bone matrix table is reached via
// *(entity+4)+0x10 (0x40-stride 4x4 matrices) and the vertex/attach records via
// *(*(entity+4)+4) (a packed node table: [0]=count, negative-strided 0x30 node
// records whose +0x1b field points at 0x18-stride attach-point vectors). An
// attach id packs (nodeIndex<<16 | vertexIndex&0xffff). Euler transforms use the
// object's pos (+0x64/+0x68/+0x6c) and rotation (+0xc0/+0x70/+0xc8) scaled by
// FLOAT_80616bdc/FLOAT_80616be0. The active-context (grabbed/carried) object
// uses a separate anim-object pointer at DAT_80611d74+0xd1c and view matrices at
// DAT_80611d54+0x20c/+0x210.
// ---------------------------------------------------------------------------

// ---- batch_544 per-file externs (raw symbols) ----
extern int   DAT_80611d54;               // camera/view state block
extern int   DAT_80611748;               // viewport params block (fov near/far)
extern unsigned int DAT_806109c8;        // view-proj int-as-double source
extern unsigned int DAT_806109c4;        // view-proj int-as-double source
extern int   DAT_8060f954;               // view-proj int-as-double divisor
extern int   DAT_8060f950;               // view-proj int-as-double divisor
extern float FLOAT_80616bd8;             // muzzle Y offset
extern float FLOAT_80616bdc;             // rotation scale numerator
extern float FLOAT_80616be0;             // rotation scale divisor
extern float FLOAT_80616be4;             // fov scale factor
extern float FLOAT_80616be8;             // fov scale divisor
extern float FLOAT_806104a0;             // perspective near
extern float FLOAT_806104a4;             // perspective far
extern float FLOAT_80616bec;             // active-ctx bone default X
extern float FLOAT_80616bf0;             // active-ctx bone default Y
extern float FLOAT_80616bf4;             // active-ctx bone default Z
extern double DOUBLE_80616ba0;           // int->double bias (2^52)
extern double DOUBLE_80616bd0;           // int->double bias
extern int   DAT_803cd150;               // ejection default point X
extern int   DAT_803cd154;               // ejection default point Y
extern int   DAT_803cd158;               // ejection default point Z

// Object accessor pair variant (FUN_803826e8 object hi / attach-id lo;
// FUN_80382734 releases). Distinct from the 803826f0/8038273c pair above.
extern unsigned long long FUN_803826e8(void);
extern void FUN_80382734(void);

// shell_casing_spawn resolved via generated_decls.h (6-arg form):
//   void shell_casing_spawn(int playerIdx, int weaponType, int weaponEntity,
//                           int attackerEntity, int weaponData, int* playerSlot);

// 0x801ac024: spawn a shell casing for the current object's weapon. Guarded by
// the render entity (+0x54), the object-context predicate FUN_801afb40, and the
// active-context view flag (DAT_80611d54+0x21c bit 2). Resolves the two ejection
// attach points (+0x1ec primary, +0x1f0 secondary of the object def) into world
// vectors (object+0x10 / object+0x20) via object_resolve_attach_world_point,
// then issues shell_casing_spawn with the player slot / weapon / attacker refs.
void object_spawn_shell_casing(unsigned int arg0, int entity, int def)
{
    unsigned long long acc = FUN_803826e8();
    int object = (int)(acc >> 0x20);
    (void)arg0;
    if ((*(int *)(def + 0x54) != -1) && (FUN_801afb40(object) != 0) &&
        (*(int *)(object + 0x54) != 0)) {
        if ((-1 < *(int *)(DAT_80611d74 + 4)) &&
            ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 4) != 0) &&
            (*(int *)(entity + 0x1ec) != -1) && (*(int *)(entity + 0x1f0) != -1)) {
            int *player_slot = *(int **)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
            if (FUN_801afb40(object) != 0) {
                object_resolve_attach_world_point(object, *(int *)(entity + 0x1ec),
                                                  (float *)(object + 0x10));
                object_resolve_attach_world_point(object, *(int *)(entity + 0x1f0),
                                                  (float *)(object + 0x20));
            }
            shell_casing_spawn(*(int *)DAT_80611d74, *(int *)(def + 0x54), object,
                               (int)acc, entity, player_slot);
        }
    }
    FUN_80382734();
}

// 0x801ac11c: build the object's muzzle/attach transform matrix. Frame (object
// via __save_gpr_28). When the object has no bound anim-object (+0xe8==0) it
// copies the active-context view fields (DAT_80611d74+0x14). Otherwise it builds
// a translate+Z-rotate matrix from the anim-object pos/rot, multiplies by the
// bone-palette matrix when visible, and applies the per-attach vertex offset
// (type entry +0x23c attach id). Result stored to the object's +0x00 vec3.
void object_build_muzzle_matrix(void)
{
    unsigned long long acc = __save_gpr_28();
    float y_off = FLOAT_80616bd8;
    int *object = (int *)(acc >> 0x20);
    int anim = object[0x3a];
    int entry = DAT_80610ba0 + object[0xc] * 0x29c;
    float local_out[3];
    float mtx_a[16];
    float mtx_b[22];
    if (anim == 0) {
        int view = *(int *)(DAT_80611d74 + 0x14);
        object[0] = *(int *)(view + 100);
        ((float *)object)[1] = y_off + *(float *)(view + 0x68);
        object[2] = *(int *)(view + 0x6c);
        __restore_gpr_28();
        return;
    }
    mtx4_set_translate_rotz((double)*(float *)(anim + 100), (double)*(float *)(anim + 0x68),
                            (double)*(float *)(anim + 0x6c),
                            (double)((FLOAT_80616bdc * *(float *)(anim + 0x70)) / FLOAT_80616be0),
                            mtx_b);
    bool used_bone = false;
    if (((*(unsigned int *)(anim + 0x28) & 0x100) == 0) &&
        ((*(unsigned int *)(anim + 0x10) & 0xffff) != 0)) {
        float *bone = *(float **)(*(int *)(anim + 4) + 0x10);
        if ((bone != 0) && (*(int *)(*(int *)(*(int *)(anim + 4) + 4) + 4) >= 1)) {
            mtx4_multiply(bone, mtx_b, mtx_a);
            used_bone = true;
        }
    }
    if (!used_bone) {
        int view = *(int *)(DAT_80611d74 + 0x14);
        mtx4_set_translate_rotz((double)*(float *)(view + 100),
                                (double)(FLOAT_80616bd8 + *(float *)(view + 0x68)),
                                (double)*(float *)(view + 0x6c),
                                (double)((FLOAT_80616bdc * *(float *)(view + 0x70)) / FLOAT_80616be0),
                                mtx_a);
    }
    unsigned int attach = *(unsigned int *)(entry + (int)acc * 4 + 0x23c);
    if (attach != 0xffffffff) {
        int *nodes = *(int **)(*(int *)(anim + 4) + 4);
        float *v = (float *)(nodes[*nodes * -0x30 + ((int)attach >> 0x10) * 0x30 + 0x1b] +
                             (attach & 0xffff) * 0x18);
        mtx4_set_translation(mtx_b, *v, v[1], v[2]);
        mtx4_multiply(mtx_b, mtx_a, mtx_a);
    }
    object[0] = *(int *)&local_out[0];
    object[1] = *(int *)&local_out[1];
    object[2] = *(int *)&local_out[2];
    __restore_gpr_28();
}

// 0x801ac2c0: build the object weapon-view perspective matrix from the viewport
// params (DAT_80611748 fov +0xc/+0x20, DAT_806109c8/c4 int-as-double sources)
// and the active fov scale (DAT_80611d54+0x384). Output into param_1 (4x4).
void object_build_view_proj_matrix(float *out)
{
    float fov_x = (float)((double)CONCAT44(0x43300000, *(unsigned int *)(DAT_80611748 + 0xc) ^ 0x80000000)
                          - DOUBLE_80616ba0);
    float ratio_a = (float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_80616ba0)
                    / (float)((double)CONCAT44(0x43300000, DAT_8060f954) - DOUBLE_80616bd0);
    float ratio_b = (float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80616ba0)
                    / (float)((double)CONCAT44(0x43300000, DAT_8060f950) - DOUBLE_80616bd0);
    float fov_y = (float)((double)CONCAT44(0x43300000, *(unsigned int *)(DAT_80611748 + 0x20) ^ 0x80000000)
                          - DOUBLE_80616ba0);
    double aspect = (double)((fov_x * (ratio_a / ratio_b)) / fov_y);
    double fov = (double)((FLOAT_80616bdc * FLOAT_80616be4 *
                          (*(float *)(DAT_80611d54 + 900) / FLOAT_80616be8)) / FLOAT_80616be0);
    mtx4_set_perspective(aspect, fov, (double)FLOAT_806104a0, (double)FLOAT_806104a4, out);
}

// 0x801ac3bc: transform a bone-attach point into screen space. Frame (object via
// __save_gpr_28); attach point in the acc lo vec3, bone node index in param_3.
// Builds the object euler transform, concatenates the bone-palette matrix and
// the view-proj matrix, then applies the inverse camera matrix
// (DAT_80611d54+0x20c). Result written to param_4 (vec3).
void object_bone_point_to_screen(unsigned int arg0, unsigned int arg1, int bone_index, float *out)
{
    unsigned long long acc = __save_gpr_28();
    int object = (int)(acc >> 0x20);
    float *pt = (float *)acc;
    (void)arg0; (void)arg1;
    if ((FUN_801afb40(object) != 0) && ((object = *(int *)(object + 0x54)) != 0)) {
        out[0] = pt[0];
        out[1] = pt[1];
        out[2] = pt[2];
        float proj[16];
        float inv[16];
        float world[16];
        float euler[22];
        object_build_view_proj_matrix(proj);
        mtx4_invert(inv, *(float **)(DAT_80611d54 + 0x20c));
        mtx4_set_transform_euler((double)*(float *)(object + 100), (double)*(float *)(object + 0x68),
                                 (double)*(float *)(object + 0x6c),
                                 (double)((FLOAT_80616bdc * *(float *)(object + 0xc0)) / FLOAT_80616be0),
                                 (double)((FLOAT_80616bdc * *(float *)(object + 0x70)) / FLOAT_80616be0),
                                 (double)((FLOAT_80616bdc * *(float *)(object + 200)) / FLOAT_80616be0),
                                 euler);
        mtx4_multiply((float *)(*(int *)(*(int *)(object + 4) + 0x10) + bone_index * 0x40), euler, world);
        mtx4_multiply(world, proj, euler);
        mtx4_transform_point_alias(euler, out);
        mtx4_transform_point_alias(inv, out);
    }
    __restore_gpr_28();
}

// 0x801ac4c4: resolve a bone-attach point for the active-context (carried)
// object (DAT_80611d74+0xd1c). Builds the euler transform (with a +FLOAT_80616be0
// Y-rot bias), multiplies by bone-palette matrix +0x4c0, transforms the default
// point (FLOAT_80616bec/bf0/bf4) and writes to param_2 (vec3).
void object_active_ctx_bone_point(unsigned int arg0, float *out)
{
    int anim = *(int *)(DAT_80611d74 + 0xd1c);
    (void)arg0;
    float pt[3];
    float world[16];
    float euler[16];
    pt[0] = FLOAT_80616bec;
    pt[1] = FLOAT_80616bf0;
    pt[2] = FLOAT_80616bf4;
    mtx4_set_transform_euler((double)*(float *)(anim + 100), (double)*(float *)(anim + 0x68),
                             (double)*(float *)(anim + 0x6c),
                             (double)((FLOAT_80616bdc * *(float *)(anim + 0xc0)) / FLOAT_80616be0),
                             (double)((FLOAT_80616bdc * (FLOAT_80616be0 + *(float *)(anim + 0x70))) / FLOAT_80616be0),
                             (double)((FLOAT_80616bdc * *(float *)(anim + 200)) / FLOAT_80616be0),
                             euler);
    mtx4_multiply((float *)(*(int *)(*(int *)(anim + 4) + 0x10) + 0x4c0), euler, world);
    mtx4_transform_point_alias(world, pt);
    out[0] = pt[0];
    out[1] = pt[1];
    out[2] = pt[2];
}

// 0x801ac594: resolve a weapon attach point to a world position for the current
// object (FUN_803826e8 accessor: object hi / attach id lo). Inactive objects
// resolve the bone-local attach point (object_bone_point_to_screen); active-
// context objects use object_active_ctx_bone_point then apply view-proj and the
// inverse camera matrix. Result written to param_3 (vec3).
void object_resolve_attach_world_point(unsigned int arg0, unsigned int arg1, float *out)
{
    unsigned long long acc = FUN_803826e8();
    int object = (int)(acc >> 0x20);
    unsigned int attach = (unsigned int)acc;
    int entity = *(int *)(object + 0x54);
    int node_index = 0;
    (void)arg0; (void)arg1;
    if (object_is_active_tagged(object) == 0) {
        if (entity != 0) {
            float pt[3];
            pt[0] = FLOAT_80616b98;
            pt[1] = FLOAT_80616b98;
            pt[2] = FLOAT_80616b98;
            if (attach != 0xffffffff) {
                node_index = (int)attach >> 0x10;
                int *nodes = *(int **)(*(int *)(entity + 4) + 4);
                float *v = (float *)(nodes[*nodes * -0x30 + node_index * 0x30 + 0x1b] +
                                    (attach & 0xffff) * 0x18);
                pt[0] = v[0];
                pt[1] = v[1];
                pt[2] = v[2];
            }
            object_bone_point_to_screen(object, (unsigned int)pt, node_index, out);
        }
    } else {
        float world_pt[3];
        float inv[16];
        float proj[24];
        object_active_ctx_bone_point(object, world_pt);
        out[0] = world_pt[0];
        out[1] = world_pt[1];
        out[2] = world_pt[2];
        object_build_view_proj_matrix(proj);
        mtx4_transform_point_alias(proj, out);
        mtx4_invert(inv, *(float **)(DAT_80611d54 + 0x20c));
        mtx4_transform_point_alias(inv, out);
    }
    FUN_80382734();
}

// 0x801ac6b8: resolve a bone-local attach point (no projection). Frame (object
// via __save_gpr_28); attach id in acc lo. Reads the packed attach vector, then
// applies the object euler transform + bone-palette matrix. Result in param_3.
void object_bone_local_point(unsigned int arg0, unsigned int arg1, float *out)
{
    unsigned long long acc = __save_gpr_28();
    int object = (int)(acc >> 0x20);
    unsigned int attach = (unsigned int)acc;
    float def = FLOAT_80616b98;
    (void)arg0; (void)arg1;
    if ((FUN_801afb40(object) != 0) && ((object = *(int *)(object + 0x54)) != 0)) {
        int node_index = 0;
        out[0] = FLOAT_80616b98;
        out[1] = def;
        out[2] = def;
        if (attach != 0xffffffff) {
            node_index = (int)attach >> 0x10;
            int *nodes = *(int **)(*(int *)(object + 4) + 4);
            int voff = (attach & 0xffff) * 0x18;
            out[0] = *(float *)(nodes[*nodes * -0x30 + node_index * 0x30 + 0x1b] + voff);
            nodes = *(int **)(*(int *)(object + 4) + 4);
            out[1] = *(float *)(nodes[*nodes * -0x30 + node_index * 0x30 + 0x1b] + voff + 4);
            nodes = *(int **)(*(int *)(object + 4) + 4);
            out[2] = *(float *)(nodes[*nodes * -0x30 + node_index * 0x30 + 0x1b] + voff + 8);
        }
        float euler[22];
        float world[16];
        mtx4_set_transform_euler((double)*(float *)(object + 100), (double)*(float *)(object + 0x68),
                                 (double)*(float *)(object + 0x6c),
                                 (double)((FLOAT_80616bdc * *(float *)(object + 0xc0)) / FLOAT_80616be0),
                                 (double)((FLOAT_80616bdc * *(float *)(object + 0x70)) / FLOAT_80616be0),
                                 (double)((FLOAT_80616bdc * *(float *)(object + 200)) / FLOAT_80616be0),
                                 euler);
        mtx4_multiply((float *)(*(int *)(*(int *)(object + 4) + 0x10) + node_index * 0x40), euler, world);
        mtx4_transform_point_alias(world, out);
    }
    __restore_gpr_28();
}

// 0x801ac814: get a weapon ejection/attach point by index for the current object
// (FUN_803826f0 accessor). For inactive objects with a bound anim-object
// (+0x54), resolves the packed attach id (type entry +0x214 index) into a world
// vector, applies the euler transform + the active view matrix (DAT_80611d54+
// 0x210). Active-context objects fall back to the def attach id (+0x1e4, or the
// primary +0x1e4 when index 1 is unset) via object_resolve_attach_world_point.
void object_get_ejection_point(unsigned int arg0, unsigned int arg1, int index)
{
    unsigned long long acc = FUN_803826f0();
    float *object = (float *)acc;
    (void)arg0; (void)arg1;
    float out[3];
    float world[16];
    float euler[23];
    out[0] = *(float *)&DAT_803cd150;
    out[1] = *(float *)&DAT_803cd154;
    out[2] = *(float *)&DAT_803cd158;
    int entry = DAT_80610ba0 + (int)((int *)object)[0xc] * 0x29c;
    if ((FUN_801afb40((int)object) == 0) && (object_is_active_tagged((int)object) == 0)) {
        int anim = ((int *)object)[0x15];
        if (anim != 0) {
            unsigned int attach = *(unsigned int *)(entry + index * 4 + 0x214);
            if (attach != 0xffffffff) {
                int *nodes = *(int **)(*(int *)(anim + 4) + 4);
                float *v = (float *)(nodes[*nodes * -0x30 + ((int)attach >> 0x10) * 0x30 + 0x1b] +
                                    (attach & 0xffff) * 0x18);
                out[0] = v[0];
                out[1] = v[1];
                out[2] = v[2];
            }
            mtx4_set_transform_euler((double)*(float *)(anim + 100), (double)*(float *)(anim + 0x68),
                                     (double)*(float *)(anim + 0x6c),
                                     (double)((FLOAT_80616bdc * *(float *)(anim + 0xc0)) / FLOAT_80616be0),
                                     (double)((FLOAT_80616bdc * *(float *)(anim + 0x70)) / FLOAT_80616be0),
                                     (double)((FLOAT_80616bdc * *(float *)(anim + 200)) / FLOAT_80616be0),
                                     euler);
            mtx4_multiply(euler, *(float **)(DAT_80611d54 + 0x210), world);
            mtx4_transform_point4_alias(world, out, object);
        }
    } else {
        int attach = *(int *)(entry + index * 4 + 0x1e4);
        if ((attach == -1) && (index == 1)) {
            attach = *(int *)(entry + 0x1e4);
        }
        object_resolve_attach_world_point((unsigned int)object, attach, object);
    }
    FUN_8038273c();
}

// 0x801ac994: predicate - the object's current anim frame is in the fire set
// (10, 11, 0xc, or 0x1b).
int object_anim_frame_is_fire(int object)
{
    int frame = object_anim_get_frame(object);
    bool in_set = true;
    if ((1 < (unsigned int)(frame - 10)) && (frame != 0xc)) {
        in_set = false;
    }
    if ((!in_set) && (frame != 0x1b)) {
        return 0;
    }
    return 1;
}

// 0x801aca04: commit the object anim frame, gated by the resolved anim slot and
// whether the target frames 3/5 have been reached. When there is no slot or
// neither frame reached, pick the event + commit default; otherwise commit the
// current frame. Sets the frame-committed flag (+0xb8).
void object_anim_frame_commit_gated(int object)
{
    short *slot = object_anim_slot_resolve();
    if ((slot == 0) ||
        ((object_anim_frame_reached(object, 3) == 0) &&
         (object_anim_frame_reached(object, 5) == 0))) {
        object_anim_pick_event(object);
        object_anim_commit_default();
    } else {
        object_commit_anim_frame(FLOAT_80616b98);
    }
    *(int *)(object + 0xb8) = 1;
}

// ===========================================================================
// Batch: batch_545 | Object weapon FIRE PIPELINE: fire state machine, projectile
// spawn + fire effects, fire-request entry, melee/thrown fire, reload helper.
// Date: 2026-09-07 | Addresses: 0x801acaac - 0x801af364
//
// This is the core object weapon-firing path. object_fire_request (0x801adf98)
// is the entry: it gates on gameplay-active (DAT_806108e4), resolves the slot
// (match_slot_field_ptr), dispatches object event 8, and enters the fire state
// machine object_fire_state_machine (0x801acbf8) which walks the reload/consume
// logic and, when a round is emitted, calls the projectile-spawn/effects driver
// object_fire_spawn_effects (0x801ad25c). The thrown/melee path
// object_melee_thrown_fire (0x801ae0fc) is the parallel driver for those weapon
// classes. Anim-frame commit helpers (object_anim_frame_advance /
// object_anim_frame_commit_muzzle) drive the fire animation.
//
// Per-slot working ammo counters live at carrier+chan*2+0x38, reserve at
// player-slot(+0x61e). The active-object context is DAT_80611d74; the player
// slot record is *(DAT_80611d74+0x14)+0x7c. Rotation scale FLOAT_80616bdc/be0,
// rng via rng_counter_tick + DOUBLE_80616bd0/FLOAT_80616bc4 normalization.
// NOTE: the two large drivers are non-matching structural translations; the
// heavy paired-single register spill locals in the Ghidra output are collapsed
// to concrete float math here.
// ---------------------------------------------------------------------------

// ---- batch_545 per-file externs (raw symbols) ----
extern int   DAT_805015bf;               // controller/mode byte gate
extern int   DAT_80431200;               // per-weapon scale table (8-byte stride)
extern int   DAT_8048ca94;               // female-lean offset table (0x30 stride)
extern int   DAT_804308b8;               // per-weapon mode table (0x24 stride)
extern int   DAT_80611e2c;               // default particle-id fallback
extern int   DAT_803cd15c;               // trail default point A (x/y/z at +0/+4/+8)
extern int   DAT_803cd160;
extern int   DAT_803cd164;
extern int   DAT_803cd168;               // melee default dir A
extern int   DAT_803cd16c;
extern int   DAT_803cd170;
extern int   DAT_803cd174;               // melee axis B
extern int   DAT_803cd178;
extern int   DAT_803cd17c;
extern int   DAT_80612610;               // min visible node count gate
extern float FLOAT_80616bf8;             // slot cooldown threshold
extern float FLOAT_80616bfc;             // rng bias term
extern float FLOAT_80616c00;             // rng/clamp constant
extern float FLOAT_80616c04;             // rng scale
extern float FLOAT_80616c08;             // spread scale
extern float FLOAT_80616c0c;             // jitter angle base
extern float FLOAT_80616c10;             // pitch offset
extern float FLOAT_80616c14;             // min sq-length for normalize
extern float FLOAT_80616c18;             // reload handle timer
extern float FLOAT_80616c1c;             // melee distance divisor
extern float FLOAT_80616c20;             // melee rng bias
extern float FLOAT_80616c24;             // female melee rng scale
extern float FLOAT_80616c28;             // female lean factor
extern float FLOAT_80616c2c;             // aim dot threshold
extern float FLOAT_80616c30;             // proximity sq threshold
extern float FLOAT_80616c34;             // aim assist gate A
extern float FLOAT_80616c38;             // aim assist gate B
extern float FLOAT_80616c3c;             // aim assist gate C
extern float FLOAT_80616c40;             // female raycast scale
extern float FLOAT_80616c44;             // reload dist base
extern float FLOAT_80616c48;             // reload dist divisor
extern float FLOAT_80616bac;             // jitter spread
extern float FLOAT_80616bc8;             // rng 0.5 bias / normalize
extern float FLOAT_80616bd8;             // newton-raphson const 1.5
extern float FLOAT_80616ba8;             // 1.0
extern float FLOAT_80616bc4;             // rng normalize divisor
extern double DOUBLE_80616bd0;           // rng int->double bias
// (DAT_80610ba0, DAT_80611d74, DAT_80611d54, DAT_806108e4, DAT_805015c0,
//  DAT_805015bd, DAT_80612610, DAT_806109c0, DAT_8043e238/e23c, FLOAT_80616b98,
//  FLOAT_80611718, FLOAT_806108ec, DOUBLE_80616ba0 already declared above.)

// Object accessor variants (object hi / carrier|attach lo register pair).
extern unsigned long long FUN_803826d0(void);   // fire accessor (melee/thrown)
extern void FUN_8038271c(void);
// FUN_803826d4 / FUN_803826dc / FUN_80382720 / FUN_80382728 / FUN_80382734
// already declared above.

// Cross-module raw-symbol helpers used only by this fire path.
extern void FUN_801b1528(int object, float *out_pos, int mode);  // resolve muzzle world pos
extern void FUN_801b1390(void);                                  // active-ctx swap finalize
extern void FUN_801b4aa0(unsigned long long frame, double a, double b, double c, double d,
                         double e, double f, double g, int *player_slot); // beam/continuous fire
extern void FUN_801af44c(double a, double b, int player_ent, float *carrier, int object,
                         int attach_id, unsigned int *p0, unsigned int *p1, float *p2,
                         unsigned int *p3, unsigned int *p4, unsigned int *p5, int one,
                         int *player_slot, int index);           // spawn projectile
extern void FUN_801af828(double a, double b, int player_ent, float *pos, int zero, int chan,
                         unsigned int *dir, int two, int *player_slot, int zero2); // melee hit dispatch
extern void FUN_801b0a98(void);                                  // dry-fire click
extern int  FUN_801b6e68(int object);                            // fire suppressed predicate
extern int  FUN_80237ad8(double a, int b, int player_ent);       // player anim helper A
extern int  FUN_80237b50(double a, int b, int player_ent);       // player anim helper B
extern int  FUN_802c3464(int a, int char_def);                   // team/target check

// 0x801acaac: advance the object anim frame (muzzle variant). Resolves the anim
// slot; with no slot, picks the event + commits default; otherwise commits the
extern void shell_casing_spawn(int playerIdx, int weaponType, int weaponEntity,
                               int attackerEntity, int weaponData, int *playerSlot);

// Forward decls (batch_544 intra-block ordering).
void object_resolve_attach_world_point(unsigned int arg0, unsigned int arg1, float *out);
void object_build_view_proj_matrix(float *out);
void object_bone_point_to_screen(unsigned int arg0, unsigned int arg1, int bone_index, float *out);
void object_active_ctx_bone_point(unsigned int arg0, float *out);

// 0x801ac024: spawn a shell casing for the current object's weapon.
    int entry = DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c;
    short *slot = object_anim_slot_resolve();
extern void mtx4_set_translation(float *mtx, float x, float y, float z);
extern void mtx4_set_identity(int mtx);
        object_anim_pick_event(object);
        object_anim_commit_default();
    } else {
        object_commit_anim_frame(FLOAT_80616b98);
        if ((*(unsigned int *)(entry + 0xc) & 0x200) != 0) {
            object_fire_spawn_effects();
        }
    }
    *(int *)(object + 0xb8) = 1;
}

// 0x801acb44: gate the muzzle-frame advance. For type 6 with flag 0x200 and a
// positive cooldown (+0x108 of the def) it advances immediately; otherwise it
// advances only when the anim has reached frame 3, 4, or 5.
void object_anim_frame_advance(int object)
{
    int type = *(int *)(object + 0x30);
    if (type == -1) {
        return;
    }
    if ((type != 6) || ((*(unsigned int *)(DAT_80610ba0 + type * 0x29c + 0xc) & 0x200) == 0) ||
        (*(float *)(*(int *)(object + 0x354) + 0x108) <= FLOAT_80616b98)) {
        if ((object_anim_frame_reached(object, 3) != 0) ||
            (object_anim_frame_reached(object, 4) != 0) ||
            (object_anim_frame_reached(object, 5) != 0)) {
            object_anim_frame_commit_muzzle(object);
        }
    } else {
        object_anim_frame_commit_muzzle(object);
    }
}

// 0x801adf58: release the object's fluid-grid cell (+0xc8) back to the pool.
void object_release_fluid_cell(int object)
{
    if (*(int *)(object + 0xc8) != -1) {
        fluid_grid_cell_mark(*(int *)(object + 0xc8));
        *(int *)(object + 0xc8) = -1;
    }
}

// 0x801af334: get the active-context sub-object's slot field (+0x68) for the
// primary (+0x184) or secondary (+0x4e4) context slot. Returns 0 when the slot
// has no bound render entity (+0x54).
int object_ctx_slot_field(int ctx, int secondary)
{
    int slot = (secondary == 0) ? (ctx + 0x184) : (ctx + 0x4e4);
    if (*(int *)(slot + 0x54) != 0) {
        return *(int *)(slot + 0x68);
    }
    return 0;
}

// 0x801af364: resolve the active weapon's fire sound/effect id for the current
// player. Picks the primary (+0x184) or secondary (+0x4e4) context slot based on
// whether the active channel has capacity, then reads the slot's match field +4.
int object_active_fire_field(int player)
{
    int obj = *(int *)(*(int *)(player + 0x14) + 0x7c);
    bool active = false;
    if (((&DAT_8043e238)[*(int *)(obj + 0x98) * 0xf] != -1) && (*(int *)(obj + 0x80) == 0)) {
        active = true;
    }
    int has_cap = 0;
    if (active && (*(int *)(obj + 0x194) < *(int *)(obj + 0x2c4))) {
        has_cap = 1;
    }
    unsigned int is_primary = countLeadingZeros((unsigned int)has_cap) >> 5;
    int block;
    int ctx_slot;
    if (is_primary == 0) {
        ctx_slot = player + 0x184;
        block = obj + 0xa4;
    } else {
        ctx_slot = player + 0x4e4;
        block = obj + 0x1d4;
    }
    int type = *(int *)(block + 0x30);
    if ((type < 0) || (0x41 < type)) {
        return -1;
    }
    int field = match_slot_field_ptr(DAT_80610ba0 + type * 0x29c, *(int *)(ctx_slot + 0x5c));
    return *(int *)(field + 4);
}

// Forward decls for the fire-path drivers (defined below in this batch).
void object_fire_spawn_effects(void);
void object_fire_state_machine(int carrier, int object, int arg2);

// 0x801adf98: fire-request entry point. Frame (object hi / carrier lo via
// __save_gpr_25). Gated by gameplay-active (DAT_806108e4). Clears the trigger-
// held bit (+0x34 bit 5), closes the weapon-select HUD, and (for non-0x100 slot
// flags) type-7 objects gate through object_slot_param_gate. Dispatches object
// event 8; on success, either enters the fire state machine (slot flag 0x10
// clear) or plays a dry-fire click. param_3 = channel, param_4 = mode,
// param_5 = slot-3c value.
void object_fire_request(unsigned int arg0, unsigned int arg1, int channel, int mode, int slot3c)
{
    unsigned long long acc = __save_gpr_25();
    int object = (int)(acc >> 0x20);
    int carrier = (int)acc;
    (void)arg0; (void)arg1;
    unsigned int *slot = (unsigned int *)match_slot_field_ptr(
        DAT_80610ba0 + *(int *)(object + 0x30) * 0x29c, channel);
    int player_slot = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    if (0 < DAT_806108e4) {
        *(unsigned int *)(carrier + 0x34) = *(unsigned int *)(carrier + 0x34) & 0xffffffdf;
        hud_weapon_select_begin_close();
        if ((*slot & 0x100) == 0) {
            bool gated_ok = true;
            if (*(int *)(carrier + 0x44) == 7) {
                if (object_slot_param_gate(player_slot, object, channel) == 0) {
                    gated_ok = false;
                } else {
                    object_set_handle(carrier, 1);
                }
            }
            if (gated_ok) {
                int type_id = (carrier == 0) ? *(int *)(object + 0x30) : *(int *)(carrier + 0x30);
                int ev_arg = (unsigned int)(channel != 0) | (-(unsigned int)(mode != 0) & 2);
                int r = object_type_dispatch(8, type_id, carrier, object, ev_arg);
                if (r == 0) {
                    if ((*slot & 0x10) == 0) {
                        *(int *)(carrier + 0x5c) = channel;
                        *(int *)(carrier + 0x60) = mode;
                        *(int *)(carrier + 0x3c) = slot3c;
                        *(unsigned int *)(object + 0xf8) = slot[0xb];
                        object_fire_state_machine(carrier, object, 0);
                    } else if (((DAT_805015c0 & 0x4000) == 0) && (mode != 1)) {
                        FUN_801b0a98();
                    }
                }
            }
        }
    }
    __restore_gpr_25();
}

// 0x801acbf8: object weapon FIRE STATE MACHINE. Frame (object hi / carrier lo
// via FUN_803826d4). Runs while the object's fire-cooldown (+0xb0) is clear.
// Decrements the burst counter (+0xf8), selects primary/secondary channel caps
// (entry +0x14/+0x18 vs +0x1c/+0x20), and branches: fully-automatic reload path,
// out-of-ammo dry-fire click (sound 0xd3c), or the normal fire path which
// dispatches object event 10, consumes ammo (object_ammo_consume_dispatch),
// commits the fire animation, handles the pending model swap for the active
// context, and refreshes handles. Non-matching structural translation.
void object_fire_state_machine(int arg_carrier, int arg_object, int arg2)
{
    (void)arg_carrier; (void)arg_object;
    unsigned long long acc = FUN_803826d4();
    int object = (int)(acc >> 0x20);
    int carrier = (int)acc;
    int *player_slot = *(int **)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    int base = DAT_80610ba0 + *(int *)(carrier + 0x30) * 0x29c;
    int field = match_slot_field_ptr(base, *(int *)(object + 0x5c));
    int chan = *(int *)(base + 0x14);
    bool round_changed = false;
    int cap = *(int *)(base + 0x18);

    if (*(int *)(object + 0xb0) != 0) {
        FUN_80382720();
        return;
    }

    int burst = *(int *)(carrier + 0xf8);
    if (0 < burst) {
        int mark = *(int *)(field + 0x2c);
        *(int *)(carrier + 0xf8) = burst - 1;
        round_changed = (mark != burst);
    }
    if ((*(int *)(object + 0x5c) == 1) && (*(int *)(base + 0x1c) != 0)) {
        cap = *(int *)(base + 0x20);
        chan = *(int *)(base + 0x1c);
    }
    *(int *)(object + 0xa8) = 0;
    *(float *)(DAT_80611d74 + 0xe48) = FLOAT_80616b98;
    *(unsigned int *)(object + 0xb0) = countLeadingZeros((unsigned int)(1 - cap)) >> 5;

    int need = (*(int *)(object + 0x60) == 1) ? *(int *)(field + 0x28) : *(int *)(field + 0x24);

    if ((cap == 0) && (*(short *)((int)player_slot + chan * 2 + 0x61e) < need) &&
        (*(int *)(object + 0x60) == 1) && (object_is_active_tagged(object) == 0)) {
        // Auto-reload path.
        *(int *)(carrier + 0xf8) = 0;
        *(int *)(object + 0xa8) = -1;
        object_anim_frame_commit_gated(object);
        object_ammo_consume_frame((unsigned int)player_slot, carrier, object, 1, *(int *)(object + 0x3c));
    } else if (((cap == 0) || (cap == 1)) || (need <= *(short *)(carrier + chan * 2 + 0x38))) {
        if (((cap == 0) || (cap == 1)) && (*(short *)((int)player_slot + chan * 2 + 0x61e) < need)) {
            // Out of reserve: try switch or dry-fire click.
            *(int *)(carrier + 0xf8) = 0;
            *(int *)(object + 0xb0) = 0;
            int touched = object_slot_field_touch_either();
            if ((touched == 0) &&
                ((*(int *)(object + 0x60) != 1) || (object_slot_field_touch_either() == 0)) &&
                (object_slot_primary_has_capacity((int)player_slot, player_slot[0x26]) == 0) &&
                (object_is_active_tagged(object) == 0)) {
                object_switch_to_next_available(player_slot);
            } else if (5 < *(int *)(carrier + 0xf0)) {
                if ((*(unsigned int *)(base + 0xc) & 0x4000) == 0) {
                    float pos[3];
                    FUN_801b1528(carrier, pos, 1);
                    unsigned char params[0x60];
                    sound_init_play_params((int)params);
                    params[0x44] = 1;
                    *(int *)params = 0xd3c;
                    *(float *)(params + 0x14) = FLOAT_80611718;
                    *(float *)(params + 0x18) = pos[0];
                    *(float *)(params + 0x1c) = pos[1];
                    *(float *)(params + 0x20) = pos[2];
                    sound_play_request((int *)params);
                }
                *(int *)(carrier + 0xf0) = 0;
                if ((*(int *)(object + 0x5c) == 1) || ((*(unsigned int *)(base + 0xc) & 0x400) == 0)) {
                    *(int *)(carrier + 0xf4) = 0;
                }
            }
        } else {
            // Normal fire: dispatch event 10, consume, commit anim, handle swap.
            int type_id = (object == 0) ? *(int *)(carrier + 0x30) : *(int *)(object + 0x30);
            unsigned int r = object_type_dispatch(10, type_id, object, carrier, arg2);
            if ((r & 2) == 0) {
                object_ammo_consume_dispatch((unsigned int)player_slot, carrier, object,
                                             chan, (short)need, cap);
                *(int *)(carrier + 0xf0) = 0;
                if ((*(int *)(object + 0x5c) == 1) || ((*(unsigned int *)(base + 0xc) & 0x400) == 0)) {
                    *(int *)(carrier + 0xf4) = 0;
                }
                *(int *)(object + 0xac) = 0;
                if (object_swap_is_pending(DAT_80611d74) != 0) {
                    object_anim_pick_event(DAT_80611d74 + 0x844);
                    object_anim_commit_default();
                }
                object_process_pending_swap(DAT_80611d74, 1);
                int ctx = DAT_80611d74;
                if (object_is_active_tagged(object) == 0) {
                    if (object_type_has_flag80(object) != 0) {
                        object_set_handle(object, 10);
                    }
                    if ((FUN_801afb40(object) != 0) && (!round_changed)) {
                        bool pending4 = false;
                        if ((object_anim_slot_resolve() != 0) &&
                            (object_anim_pending_reached(object, 4) == 0)) {
                            pending4 = true;
                        }
                        if (((*(unsigned int *)(base + 0xc) & 0x80) == 0) ||
                            (object_anim_slot_resolve() == 0)) {
                            if ((*(int *)(object + 0x5c) == 1) && (object_anim_slot_resolve() != 0)) {
                                object_anim_commit_default();
                            } else if (((*(int *)(object + 0x60) != 1) ||
                                        (object_anim_slot_resolve() == 0)) ||
                                       (pending4 && (object_anim_frame_reached(object, 5) == 0))) {
                                if ((*(int *)(object + 0x60) == 1) &&
                                    (object_anim_slot_resolve() != 0)) {
                                    object_anim_commit_default();
                                } else if (object_anim_slot_resolve() != 0) {
                                    object_anim_commit_default();
                                    object_anim_mark_frame_dirty(object);
                                }
                            } else {
                                object_anim_commit_default();
                            }
                        } else {
                            object_anim_commit_default();
                        }
                    }
                    if (*(int *)(object + 0x30) == 0x15) {
                        object_set_handle(object, 0xb);
                    }
                    if ((*(float *)(field + 0xc) < FLOAT_80616bf8) &&
                        ((*(unsigned int *)(base + 0xc) & 0x280) == 0)) {
                        object_fire_spawn_effects();
                    }
                } else {
                    // Active-context (carried weapon) path: mark pending swap.
                    int ctx_slot = DAT_80611d74 + 0x184;
                    int ctx_sec = DAT_80611d74 + 0x4e4;
                    object_set_pending_swap(DAT_80611d74);
                    object_set_handle(object, 9);
                    if (*(int *)(ctx + 0x1c8) != 0) {
                        object_set_handle(ctx_slot, 9);
                    }
                    if ((*(int *)(ctx + 0x538) != 0) && (FUN_801afb40(ctx_sec) != 0) &&
                        (object_anim_frame_reached(ctx_sec, 0x2e) != 0) &&
                        (object_sound_slot_refresh() != 0)) {
                        *(unsigned int *)(*(int *)(ctx + 0x538) + 0x28) =
                            *(unsigned int *)(*(int *)(ctx + 0x538) + 0x28) | 1;
                    }
                    object_set_handle(ctx_sec, 9);
                    FUN_801b1390();
                }
            }
        }
    } else {
        // Enough loaded, but cap gate: commit + consume-frame reload.
        *(int *)(carrier + 0xf8) = 0;
        *(int *)(object + 0xa8) = -1;
        object_anim_frame_commit_gated(object);
        if (*(int *)(object + 0x44) == 0xb) {
            object_set_handle(object, 1);
        }
        object_ammo_consume_frame((unsigned int)player_slot, carrier, object, 1, *(int *)(object + 0x3c));
    }
    FUN_80382720();
}

// 0x801ad25c: object weapon PROJECTILE-SPAWN + FIRE-EFFECTS driver. Frame
// (object hi / carrier lo via FUN_803826d4). For each pellet (slot +8 count),
// computes a jittered fire direction from the muzzle basis (player_slot+0x37..),
// spawns the projectile (FUN_801af44c), and emits the muzzle/trail particles
// (player_ai_spawn_trail_particle, particle_cached_*). Ejects a shell casing
// when the per-fire counter (+0x44 of carrier) expires and plays the fire sound
// (object_play_fire_sound). Finishes by dispatching object events 9 and 7.
// Non-matching structural translation: the paired-single register spill locals
// from the raw decompile are collapsed to concrete float computations, and the
// register-pair scratch (dVar/in_fN) is represented by plain float temporaries.
void object_fire_spawn_effects(void)
{
    unsigned long long acc = FUN_803826d4();
    float *object = (float *)(acc >> 0x20);
    float *carrier = (float *)acc;
    int obji = (int)(acc >> 0x20);
    int base = DAT_80610ba0 + (int)((int *)carrier)[0xc] * 0x29c;
    unsigned int *slot = (unsigned int *)match_slot_field_ptr(base, (int)object[0x17]);
    unsigned int char_id = slot[1];
    int *player_slot = *(int **)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    unsigned int pellet_count = slot[2];

    // Special weapon (type 8.40779e-45 == raw 6) resets the per-shot counter.
    if (((int *)carrier)[0xc] == 6) {
        int prev = (int)object[0x30];
        object[0x30] = 0.0f;
        pellet_count = (unsigned int)(prev + 1);
    }

    // View-visible gate: player index valid + view flag DAT_80611d54+0x21c bit2.
    bool visible;
    if (*(int *)(DAT_80611d74 + 4) < 0) {
        visible = false;
    } else if ((DAT_80611d54 == 0) || ((*(unsigned int *)(DAT_80611d54 + 0x21c) & 4) == 0)) {
        visible = false;
    } else {
        visible = true;
    }

    if ((FUN_801afb40(obji) == 0) || (FUN_801b6e68(obji) != 0)) {
        // Reset fire-effect state fields.
        object[0x1a] = 2.0f;             /* raw 1.4013e-45 -> flag 1 */
        object[0x1d] = FLOAT_80616b98;
        object[0x1e] = FLOAT_80616b98;
        object[0x1f] = FLOAT_80616b98;
        object[0x1b] = FLOAT_80616b98;

        if (FUN_801afb40(obji) == 0) {
            object[0x20] = FLOAT_80616b98;
            if (((int *)carrier)[0xc] == 10) {
                unsigned int rnd = rng_counter_tick();
                float n = (float)((double)(0x4330000000000000ULL | (unsigned long long)rnd) - DOUBLE_80616bd0);
                object[0x20] = object[0x20] * (FLOAT_80616c00 * (n / FLOAT_80616bc4) + FLOAT_80616bfc);
            }
        } else {
            object[0x20] = FLOAT_80616b98;
        }

        object[0x28] = FLOAT_80616ba8;
        {
            unsigned int rnd = rng_counter_tick();
            float n = (float)((double)(0x4330000000000000ULL | (unsigned long long)rnd) - DOUBLE_80616bd0);
            float m = n / FLOAT_80616bc4;
            if (object[0x27] < (FLOAT_80616c04 * m)) {
                object[0x21] = FLOAT_80616b98;
                object[0x22] = FLOAT_80616b98;
                object[0x23] = FLOAT_80616b98;
                object[0x24] = FLOAT_80616b98;
                object[0x25] = FLOAT_80616b98;
                object[0x26] = FLOAT_80616b98;
                object[0x27] = FLOAT_80616ba8;
            }
        }
        object[0x31] = *(float *)((int)&DAT_80431200 + slot[0x19] * 8);

        object_build_muzzle_matrix();
        if (visible) {
            object_get_ejection_point((unsigned int)DAT_80611d74, (unsigned int)object, (int)object[0x17]);
        }

        if (((int *)carrier)[0xc] == 0x1b) {
            // Beam/continuous fire class.
            FUN_801b4aa0(acc, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, player_slot);
            object[0x2b] = 2.0f;   /* raw 1.4013e-45 */
        } else {
            if ((char_id != 0xffffffff) &&
                ((player_ai_char_def_get_flags(char_id) & 0x80000) == 0)) {
                FUN_80237ad8((double)FLOAT_80616ba8, 3, *(int *)DAT_80611d74);
                FUN_80237b50((double)FLOAT_80616ba8, 8, *(int *)DAT_80611d74);
                FUN_80237e0c(0, *(int *)DAT_80611d74, player_slot[0x26]);
            }

            for (int p = 0; p < (int)pellet_count; p = p + 1) {
                float origin[3], origin_b[3];
                origin[0] = carrier[0]; origin[1] = carrier[1]; origin[2] = carrier[2];
                origin_b[0] = carrier[0]; origin_b[1] = carrier[1]; origin_b[2] = carrier[2];
                if (visible) {
                    origin[0] = object[0]; origin[1] = object[1]; origin[2] = object[2];
                    origin_b[0] = object[0]; origin_b[1] = object[1]; origin_b[2] = object[2];
                    if ((*slot & 0x20) != 0) {
                        origin_b[0] = *(float *)(DAT_80611d74 + 0xb4);
                        origin_b[1] = *(float *)(DAT_80611d74 + 0xb8);
                        origin_b[2] = *(float *)(DAT_80611d74 + 0xbc);
                    }
                }
                if ((*slot & 0x1000) != 0) {
                    for (int k = 0; k < 3; k = k + 1) {
                        unsigned int rnd = rng_counter_tick();
                        float n = (float)((double)(0x4330000000000000ULL | (unsigned long long)rnd) - DOUBLE_80616bd0);
                        origin[k] = FLOAT_80616c08 * (n / FLOAT_80616bc4 - FLOAT_80616bc8) + origin[k];
                    }
                }

                float dir[4];
                player_ai_projectile_compute_jittered_dir((double)FLOAT_80616c0c,
                    (double)FLOAT_80616bac, carrier + 0x45, dir);
                float dx = dir[0] * (float)slot[8];
                float dy = dir[1] * (float)slot[8];
                if ((((*(unsigned int *)(DAT_80611d74 + 0xc) & 0x20) == 0) &&
                     (*(int *)(*(int *)(DAT_80611d74 + 0x1398) + 0x74) != 2)) ||
                    ((*slot & 2) != 0)) {
                    if ((*(unsigned int *)(DAT_80611d74 + 0xc) & 0x20) != 0) {
                        dx = dx * FLOAT_80616bc8;
                        dy = dy * FLOAT_80616bc8;
                    }
                } else {
                    dx = dx * FLOAT_80616c08;
                    dy = dy * FLOAT_80616c08;
                }
                if (((*slot & 0x800) != 0) && (p == 0)) {
                    dx = FLOAT_80616b98;
                    dy = FLOAT_80616b98;
                }
                if (((int *)carrier)[0xc] == 0x22) {
                    dy = dy + FLOAT_80616c10;
                }
                if ((obji != 0) && (object_is_active_tagged(obji) != 0)) {
                    dy = FLOAT_80616c04 * carrier[0x42] + dy;
                }

                // Build world-space fire direction from the muzzle basis
                // (player_slot right/up/fwd at +0x37/+0x3a rows).
                float rx = *(float *)((int)player_slot + 0x37 * 4) * dx;
                float ry = *(float *)((int)player_slot + 0x38 * 4) * dx;
                float rz = *(float *)((int)player_slot + 0x39 * 4) * dx;
                float ux = *(float *)((int)player_slot + 0x3a * 4) * dy;
                float uy = *(float *)((int)player_slot + 0x3b * 4) * dy;
                float uz = *(float *)((int)player_slot + 0x3c * 4) * dy;

                float d0[3], d1[3];
                d0[0] = *(float *)((int)player_slot + 0x2df * 4) + rx + ux;
                d0[1] = *(float *)((int)player_slot + 0x2e0 * 4) + ry + uy;
                d0[2] = *(float *)((int)player_slot + 0x2e1 * 4) + rz + uz;
                d1[0] = d0[0]; d1[1] = d0[1]; d1[2] = d0[2];

                if (char_id != 0xffffffff) {
                    *(int *)(DAT_80611d74 + 0xdf8) = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x34);
                    float *maybe_obj = visible ? object : (float *)0;
                    FUN_801af44c(0.0, 0.0, *(int *)(DAT_80611d74 + 0x14), carrier, (int)maybe_obj,
                                 (int)object[0x17], (unsigned int *)origin_b, (unsigned int *)origin,
                                 &carrier[0], (unsigned int *)d0, (unsigned int *)d1,
                                 (unsigned int *)d1, 1, player_slot, p);
                    double alpha = pickup_fade_get_alpha((int)(player_slot + 0x31a));
                    if (((double)FLOAT_80616ba8 == alpha) || (((int *)carrier)[0xc] == 10)) {
                        if (visible && (object[0x15] != 0.0f)) {
                            float ta[3], tb[3];
                            ta[0] = *(float *)&DAT_803cd15c;
                            ta[1] = *(float *)&DAT_803cd160;
                            ta[2] = *(float *)&DAT_803cd164;
                            mtx4_transform_point4_alias(*(float **)(DAT_80611d54 + 0x208), origin, ta);
                            tb[0] = d0[0]; tb[1] = d0[1]; tb[2] = d0[2];
                            mtx4_transform_vector(*(float **)(DAT_80611d54 + 0x208), tb);
                            if ((slot[0x1a] != 0xffffffff) && ((*slot & 0x8000) == 0)) {
                                int vpmask = (DAT_80611d54 == 0) ? 0 : (1 << *(int *)(DAT_80611d54 + 0x214));
                                player_ai_spawn_trail_particle(1, slot[0x1a], vpmask, ta, tb,
                                                               *(int *)(DAT_80611d74 + 4) + 0xca);
                                if (1 < FUN_801c1714()) {
                                    unsigned int inv = (DAT_80611d54 == 0) ? 0xffffffff
                                                        : ~(1u << *(int *)(DAT_80611d54 + 0x214));
                                    player_ai_spawn_trail_particle(0, slot[0x1a], inv, origin_b, d1, -1);
                                }
                            }
                            if (slot[0x1c] != 0) {
                                for (int q = 0; ; q = q + 4) {
                                    int pid = *(int *)(slot[0x1c] + q);
                                    if (pid < 0) break;
                                    int *pp;
                                    if (pid < *(int *)((int)object[0x15] + 0x1e8)) {
                                        pp = (int *)(*(int *)((int)object[0x15] + 0x1fc) + pid * 4);
                                    } else {
                                        pp = (int *)&DAT_80611e2c;
                                    }
                                    int emitter = *pp;
                                    if ((*slot & 0x200) == 0) {
                                        particle_emitter_set_single_color_key(emitter, *(int *)(DAT_80611d74 + 4) + 0xca);
                                        int e2 = emitter;
                                        particle_emitter_set_flags(&e2, 0x10000, 0);
                                    }
                                    int vpmask = (DAT_80611d54 == 0) ? 0 : (1 << *(int *)(DAT_80611d54 + 0x214));
                                    int e3 = emitter;
                                    particle_cached_set_scale(&e3, vpmask);
                                    int e4 = emitter;
                                    particle_cached_restart(&e4);
                                }
                            }
                        } else if (slot[0x1a] != 0xffffffff) {
                            player_ai_spawn_trail_particle(0, slot[0x1a], 0xffffffff, carrier, &carrier[0x3d], -1);
                        }
                        if (slot[0x1d] != 0) {
                            for (int q = 0; ; q = q + 4) {
                                int pid = *(int *)(slot[0x1d] + q);
                                if (pid < 0) break;
                                int *pp;
                                if (pid < *(int *)((int)carrier[0x3a] + 0x1e8)) {
                                    pp = (int *)(*(int *)((int)carrier[0x3a] + 0x1fc) + pid * 4);
                                } else {
                                    pp = (int *)&DAT_80611e2c;
                                }
                                int emitter = *pp;
                                unsigned int inv = (DAT_80611d54 == 0) ? 0xffffffff
                                                    : ~(1u << *(int *)(DAT_80611d54 + 0x214));
                                int e1 = emitter;
                                particle_cached_set_scale(&e1, inv);
                                int e2 = emitter;
                                particle_cached_restart(&e2);
                            }
                        }
                    }
                }
            }

            // Shell-casing eject when the per-fire counter (+0x44) expires.
            float scale_src = (float)((double)(0x4330000000000000ULL |
                              (unsigned long long)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80616ba0);
            float ejf = (float)(int)(scale_src * *(float *)(base + 0x298));
            carrier[0x44] = ejf;
            if (((int)carrier[0x44] < 1) && (slot[0x15] != 0xffffffff)) {
                object_spawn_shell_casing((unsigned int)object, (int)carrier, base);
                carrier[0x44] = 0.0f;
            }

            float firepos[3];
            FUN_801b1528((int)carrier, firepos, 1);
            object_play_fire_sound((unsigned int)carrier, (unsigned int)object[0x17], firepos, (int)object[0x18]);
            object[0x2b] = 2.0f;   /* raw 1.4013e-45 */

            if ((FUN_801afb40(obji) == 0) && (((int *)carrier)[0xc] == 0x22)) {
                object_set_handle(obji, 5);
                object[0x12] = FLOAT_80616c18;
            }
            if (-1 < *(int *)(DAT_80611d74 + 4)) {
                int type_id = (obji == 0) ? (int)carrier[0xc] : (int)object[0xc];
                object_type_dispatch(9, type_id, obji, (int)carrier, 0);
            }
            int type_id = (obji == 0) ? (int)carrier[0xc] : (int)object[0xc];
            object_type_dispatch(7, type_id, obji, (int)carrier, 0);
        }
    }
    FUN_80382720();
}

// 0x801ae0fc: object MELEE / THROWN-WEAPON fire driver. Frame (player entity hi
// via FUN_803826d0). Handles the melee-swing arc: computes the swing target from
// the player aim basis, applies homing/lock-on toward DAT_80611d74+0x388 when
// set, and for each hit in the swing (slot +8 count) either dispatches melee
// damage (damage_resolve_and_dispatch, for close targets) or spawns the thrown
// projectile / hit effect (FUN_801af828). Plays the fire sound and emits the
// trail particles. param_5 = channel, param_6 = mode. Non-matching structural
// translation; paired-single spill locals collapsed to concrete float math.
void object_melee_thrown_fire(unsigned int arg0, double arg1, unsigned int arg2, unsigned int arg3,
                              int channel, int mode)
{
    (void)arg0; (void)arg1; (void)arg2; (void)arg3;
    unsigned long long acc = FUN_803826d0();
    int player = (int)(acc >> 0x20);
    int carrier_lo = (int)acc;
    int *player_slot = *(int **)(player + 0x7c);
    float *fx = (carrier_lo == 0) ? (float *)(player_slot + 0x29) : (float *)(player_slot + 0x75);
    float def_a[3];
    def_a[0] = *(float *)&DAT_803cd168;
    def_a[1] = *(float *)&DAT_803cd16c;
    def_a[2] = *(float *)&DAT_803cd170;

    if ((player_slot[1] != 0) && (*(int *)(player + 0x34) != 0) && (0 < DAT_806108e4) &&
        (fx[0xc] != -1.0f /* raw != -NAN sentinel */)) {
        float tgt[3];
        tgt[0] = *(float *)((int)player_slot + 0x2e2 * 4);
        tgt[1] = *(float *)((int)player_slot + 0x2e3 * 4);
        tgt[2] = *(float *)((int)player_slot + 0x2e4 * 4);

        // Lock-on adjust (player flag 0x1000): steer swing toward the anchor.
        if ((player_slot[0xb] & 0x1000) != 0) {
            int anchor = player_slot[0];
            float dxx = *(float *)(anchor + 100) - tgt[0];
            float dyy = *(float *)(anchor + 0x68) - tgt[1];
            float dzz = *(float *)(anchor + 0x6c) - tgt[2];
            float axis[3];
            axis[0] = *(float *)&DAT_803cd174;
            axis[1] = *(float *)&DAT_803cd178;
            axis[2] = *(float *)&DAT_803cd17c;
            float dist = (dzz * dzz + dxx * dxx + dyy * dyy) / FLOAT_80616c1c;
            float clamped = dist;
            if (FLOAT_80616ba8 < clamped) clamped = FLOAT_80616ba8;
            float used;
            if (FLOAT_80616c00 <= clamped) {
                used = (FLOAT_80616ba8 < dist) ? FLOAT_80616ba8 : dist;
            } else {
                used = FLOAT_80616c00;
            }
            unsigned int rnd = rng_counter_tick();
            float n = (float)((double)(0x4330000000000000ULL | (unsigned long long)rnd) - DOUBLE_80616bd0);
            tgt[1] = tgt[1] + used * (FLOAT_80616bfc * (n / FLOAT_80616bc4) + FLOAT_80616c20);
            // (cross-product homing terms folded into the target; exact matrix
            // omitted in this non-matching form.)
        }

        int weapon = player_slot[1];
        int wnode = *(int *)(weapon + 0x7c);
        bool melee_targets =
            (*(int *)(weapon + 0x20) == 8) && (player_slot != 0) &&
            (player_slot[0] != 0) && (DAT_80612610 <= player_slot[2]) &&
            (((*(unsigned int *)(player_slot[0] + 0x28) & 0x100) != 0) ||
             ((*(unsigned int *)(player_slot[0] + 0x10) & 0xffff) == 0)) &&
            (wnode != 0) && (DAT_80612610 <= *(int *)(wnode + 8)) &&
            (((*(unsigned int *)(weapon + 0x28) & 0x100) != 0) ||
             ((*(unsigned int *)(weapon + 0x10) & 0xffff) == 0));

        if (melee_targets) {
            int slotf = match_slot_field_ptr(DAT_80610ba0 + (int)fx[0xc] * 0x29c, (int)arg0);
            if (DAT_805015bd == '\n') {
                fx[0x3c] = 0.0f;
                fx[0x3d] = 0.0f;
            } else {
                int rec[16];
                switch_slot_reset((int)rec);
                rec[0] = player_slot[1];
                int cdef = *(int *)(slotf + 4);
                rec[2] = player;
                rec[1] = player;
                if (((*(int *)(slotf + 0x60) == -1) || (*(int *)(slotf + 0x60) == 0x15)) &&
                    ((player_ai_char_def_get_flags(cdef) & 0x802) != 0)) {
                    rec[5] = 0;
                } else {
                    rec[5] = *(int *)(slotf + 0x60);
                }
                for (int i = 0; i < *(int *)(slotf + 8); i = i + 1) {
                    double dmg = (double)FUN_80237b50((double)FLOAT_80616ba8, 8, player_slot[2]);
                    if (FUN_802c3464(player, *(int *)(slotf + 4)) != 0) {
                        damage_resolve_and_dispatch(dmg, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, rec);
                        if (player_ai_char_def_is_female(*(int *)(slotf + 4)) != 0) {
                            unsigned int rnd = rng_counter_tick();
                            float n = (float)((double)(0x4330000000000000ULL | (unsigned long long)rnd) - DOUBLE_80616bd0);
                            int extra = (int)(FLOAT_80616c24 * (n / FLOAT_80616bc4) + FLOAT_80616bc8);
                            if (extra != 0) continue;
                        }
                        FUN_80237b50((double)FLOAT_80616ba8, 9, player_slot[2]);
                    }
                }
                fx[0x3c] = 0.0f;
                fx[0x3d] = 0.0f;
            }
        } else {
            // Thrown / ranged-from-melee path: build the throw direction, apply
            // per-mode ammo consume, spawn the projectile + trail.
            int entry = DAT_80610ba0 + (int)fx[0xc] * 0x29c;
            int slotf = match_slot_field_ptr(entry, channel);
            fx[0x3c] = 0.0f;
            fx[0x3d] = 0.0f;
            float scale_src = (float)((double)(0x4330000000000000ULL |
                              (unsigned long long)(DAT_806109c0 ^ 0x80000000)) - DOUBLE_80616ba0);
            fx[0x41] = FLOAT_806108ec * (FLOAT_80616ba8 / scale_src) + fx[0x41];

            int chan = *(int *)(entry + 0x14);
            int cap = *(int *)(entry + 0x18);
            short need = (mode == 1) ? (short)*(int *)(slotf + 0x28) : (short)*(int *)(slotf + 0x24);
            if (((channel == 1) && (*(int *)(entry + 0x1c) != 0)) &&
                (*(int *)(entry + 0x1c) != *(int *)(entry + 0x14))) {
                cap = *(int *)(entry + 0x20);
                chan = *(int *)(entry + 0x1c);
            }
            if ((DAT_805015bd != '\n') || ((cap != 0) && (cap != 1))) {
                object_ammo_consume_gated((int)player_slot, (int)fx, chan, need, cap);
            }

            float dir[3];
            dir[0] = *(float *)((int)player_slot + 0x2df * 4);
            dir[1] = *(float *)((int)player_slot + 0x2e0 * 4);
            dir[2] = *(float *)((int)player_slot + 0x2e1 * 4);
            FUN_801af828(0.0, 0.0, player, fx, 0, channel, (unsigned int *)dir, 2, player_slot, 0);

            float firepos[3];
            FUN_801b1528((int)fx, firepos, 1);
            object_play_fire_sound((unsigned int)fx, channel, firepos, mode);
            FUN_80237b50((double)(float)((double)(0x4330000000000000ULL |
                         (unsigned long long)(*(unsigned int *)(slotf + 8) ^ 0x80000000)) - DOUBLE_80616ba0),
                         8, player_slot[2]);
            double alpha = pickup_fade_get_alpha((int)(player_slot + 0x31a));
            if (((double)FLOAT_80616ba8 == alpha) || (fx[0xc] == 10.0f /* raw 1.4013e-44 */)) {
                if (*(int *)(slotf + 0x68) != -1) {
                    player_ai_spawn_trail_particle(0, *(int *)(slotf + 0x68), 0xffffffff, fx, dir, -1);
                }
                if (*(int *)(slotf + 0x74) != 0) {
                    for (int q = 0; ; q = q + 4) {
                        int pid = *(int *)(*(int *)(slotf + 0x74) + q);
                        if (pid < 0) break;
                        int *pp;
                        if (pid < *(int *)((int)fx[0x3a] + 0x1e8)) {
                            pp = (int *)(*(int *)((int)fx[0x3a] + 0x1fc) + pid * 4);
                        } else {
                            pp = (int *)&DAT_80611e2c;
                        }
                        int emitter = *pp;
                        particle_cached_restart(&emitter);
                    }
                }
            }
        }
    }
    FUN_8038271c();
}

// 0x801af0a8: transfer one reload round from the reserve, then apply the reload
// push/animation. Frame (object hi / out-vec lo via FUN_803826dc). Scans the
// active weapon slots (per-object byte table +0x54c, channel table DAT_8043e23c
// 0x3c stride) for a slot whose char-def flag 0x400 is set and has reserve; when
// found (and not scripted mode DAT_805015bd=='\n'), decrements the reserve
// counter (+0x61e, floored at 0). Then builds the reload transform and applies
// the reload push via FUN_801af828. param_3 = target entity, param_4 = bone sel.
void object_reload_round_transfer(unsigned int arg0, unsigned int arg1, int target, int bone_sel)
{
    (void)arg0; (void)arg1;
    unsigned long long acc = FUN_803826dc();
    int object = (int)(acc >> 0x20);
    float *out = (float *)acc;
    int *player_slot = *(int **)(object + 0x7c);
    int base = 0;
    int chosen = -1;

    if (DAT_805015bd == '\n') {
        chosen = 0x16;
    } else {
        int byte_off = 0;
        int chan_off = 0;
        do {
            if ((*(char *)((int)player_slot + byte_off + 0x54c) != '\0') &&
                (*(int *)((int)&DAT_8043e23c + chan_off) != -1)) {
                base = DAT_80610ba0 + *(int *)((int)&DAT_8043e23c + chan_off) * 0x29c;
                int field = match_slot_field_ptr(base, 0);
                if (((player_ai_char_def_get_flags(*(int *)(field + 4)) & 0x400) != 0) &&
                    (0 < *(short *)((int)player_slot + *(int *)(base + 0x14) * 2 + 0x61e))) {
                    chosen = *(int *)((int)&DAT_8043e23c + chan_off);
                    break;
                }
            }
            byte_off = byte_off + 1;
            chan_off = chan_off + 0x3c;
        } while (byte_off < 0x45);
        if ((chosen == -1) || (base == 0)) {
            FUN_80382728();
            return;
        }
        short *reserve = (short *)((int)player_slot + *(int *)(base + 0x14) * 2 + 0x61e);
        *reserve = *reserve - 1;
        if (*reserve < 0) {
            *reserve = 0;
        }
    }

    if (((*(unsigned int *)(object + 0x28) & 0x100) == 0) &&
        ((*(unsigned int *)(object + 0x10) & 0xffff) != 0)) {
        float rot[16];
        float world[12];
        float pt[3];
        mtx4_set_translate_rotz((double)*(float *)(object + 100), (double)*(float *)(object + 0x68),
                                (double)*(float *)(object + 0x6c),
                                (double)((FLOAT_80616bdc * *(float *)(object + 0x70)) / FLOAT_80616be0),
                                rot);
        int bone_field = (bone_sel == 1) ? 0x30 : 0x2c;
        mtx4_multiply((float *)(*(int *)(*(int *)(object + 4) + 0x10) +
                      *(int *)(*(int *)(*(int *)(object + 4) + 0x84) + bone_field) * 0x40), rot, world);
        // world[0..2] holds the reload world point; steer toward the target.
        pt[0] = world[0]; pt[1] = world[1]; pt[2] = world[2];
        float dx = *(float *)(target + 100) - world[0];
        float dy = *(float *)(target + 0x68) - world[1];
        float dz = *(float *)(target + 0x6c) - world[2];
        double len = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
        double scale = (double)(FLOAT_80616c00 + (float)((len - (double)FLOAT_80616c44) / (double)FLOAT_80616c48));
        out[0] = (float)((double)out[0] * scale);
        out[1] = (float)((double)out[1] * scale);
        out[2] = (float)((double)out[2] * scale);
        FUN_801af828(scale, (double)FLOAT_80616c48, object, pt, 0, 0, (unsigned int *)out, 2, player_slot, 0);
    }
    FUN_80382728();
}
int object_melee_frame_check(void)
{
    int object = (int)FUN_803826f0();
    int hit = 0;
    if ((*(int *)(object + 0x54) != 0) &&
        (object_anim_frame_reached(object, 0x15) == 0)) {
        hit = (int)object_anim_frame_reached(object, 0x16);
    }
    FUN_8038273c();
    return hit;
}
// ===========================================================================
// Batch: batch_553 - object matrix-palette reset + type-0x11 threshold gate.
// Two entity helpers immediately following object_spinup_tick.
// Address range: 0x801b297c - 0x801b2a2f
// ===========================================================================

// 0x801b297c: object_reset_matrix_palette
// Draw-time helper (operates on the render spill frame from FUN_803826f0). Marks
// the current render object dirty (flag bit 0x200000 at spill+0x28) and resets
// every matrix in the object's matrix palette to identity. The palette base is
// *(*(spill+4)+0x10) and the entry count is *(*(*(spill+4)+4)+4); each matrix is
// a 0x40-byte (4x4 float) slot. Bracketed by the FUN_803826f0/FUN_8038273c spill
// push/pop pair.
void object_reset_matrix_palette(void)
{
    int spill = FUN_803826f0();
    *(unsigned int *)(spill + 0x28) = *(unsigned int *)(spill + 0x28) | 0x200000;

    int paletteBase = *(int *)(*(int *)(spill + 4) + 0x10);
    int count       = *(int *)(*(int *)(*(int *)(spill + 4) + 4) + 4);
    int off = 0;
    for (int i = 0; i < count; ++i) {
        mtx4_set_identity(paletteBase + off);
        off += 0x40;
    }
    FUN_8038273c();
}

// 0x801b29ec: object_type11_threshold_gate
// Predicate for object type 0x11 (charge/hold weapon variant): returns 1 when the
// object is type 0x11 (field +0x204), its type-descriptor flags (*(*(obj+0x2bc)+4)
// +0x30) have bit 2 set, and the object's charge/level field (+0x544, float) has
// reached the threshold FLOAT_80616b98; otherwise 0. Caller: FUN_801b2b74.
unsigned int object_type11_threshold_gate(int object)
{
    if (*(int *)(object + 0x204) == 0x11 &&
        (*(unsigned int *)(*(int *)(*(int *)(object + 0x2bc) + 4) + 0x30) & 2) != 0) {
        if (FLOAT_80616b98 <= *(float *)(object + 0x544)) {
            return 1;
        }
    }
    return 0;
}

// ===========================================================================
// Batch: batch_553 - object matrix-palette reset + type-0x11 threshold gate.
// Two entity helpers immediately following object_spinup_tick.
// Address range: 0x801b297c - 0x801b2a2f
// ===========================================================================

// 0x801b297c: object_reset_matrix_palette
// Draw-time helper (operates on the render spill frame from FUN_803826f0). Marks
// the current render object dirty (flag bit 0x200000 at spill+0x28) and resets
// every matrix in the object's matrix palette to identity. The palette base is
// *(*(spill+4)+0x10) and the entry count is *(*(*(spill+4)+4)+4); each matrix is
// a 0x40-byte (4x4 float) slot. Bracketed by the FUN_803826f0/FUN_8038273c spill
// push/pop pair.
void object_reset_matrix_palette(void)
{
    int spill = FUN_803826f0();
    *(unsigned int *)(spill + 0x28) = *(unsigned int *)(spill + 0x28) | 0x200000;

    int paletteBase = *(int *)(*(int *)(spill + 4) + 0x10);
    int count       = *(int *)(*(int *)(*(int *)(spill + 4) + 4) + 4);
    int off = 0;
    for (int i = 0; i < count; ++i) {
        mtx4_set_identity(paletteBase + off);
        off += 0x40;
    }
    FUN_8038273c();
}

// 0x801b29ec: object_type11_threshold_gate
// Predicate for object type 0x11 (charge/hold weapon variant): returns 1 when the
// object is type 0x11 (field +0x204), its type-descriptor flags (*(*(obj+0x2bc)+4)
// +0x30) have bit 2 set, and the object's charge/level field (+0x544, float) has
// reached the threshold FLOAT_80616b98; otherwise 0. Caller: FUN_801b2b74.
unsigned int object_type11_threshold_gate(int object)
{
    if (*(int *)(object + 0x204) == 0x11 &&
        (*(unsigned int *)(*(int *)(*(int *)(object + 0x2bc) + 4) + 0x30) & 2) != 0) {
        if (FLOAT_80616b98 <= *(float *)(object + 0x544)) {
            return 1;
        }
    }
    return 0;
}
