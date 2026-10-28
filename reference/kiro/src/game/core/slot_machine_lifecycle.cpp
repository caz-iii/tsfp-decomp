// slot_machine_lifecycle.cpp
// Level-prop subsystem: 3-reel combination / slot-machine prop - lifecycle,
// audio, player-interaction and per-instance physics/placement tick.
// Companion to slot_machine_prop.cpp (batch_714, the reel state machine).
//
// The interacting player is held in DAT_805cad20 (entity), DAT_805cad28 is the
// "player engaged" flag, DAT_805cad0c the machine state (see slot_machine_prop).
// Reel-lock flags live in DAT_805cacdc / DAT_805cacec / DAT_805cacfc (bit1).
//
// Address range: 0x802425c0 - 0x80242bb7

extern "C" {

// ---- shared cross-module helpers (resolve via generated headers) ----
void  sound_init_play_params(int params);
int   sound_play_request(void *params);
void  sound_stop_handle(void);
unsigned int input_get_buttons_held(int pad);
void  input_clear_buttons(int pad, unsigned int mask);
void  input_reset_pad(int pad);
void  player_exit_menu_state(void);
void  event_set_handler(int class_id, int slot, void *handler);
void *mem_alloc(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                unsigned long long a4, unsigned long long a5, unsigned long long a6,
                unsigned long long a7, unsigned long long a8, int size, int align,
                unsigned int a11, unsigned int a12, unsigned int a13, unsigned int a14,
                unsigned int a15, unsigned int a16);
void  freelist_init(unsigned int *pool, int stride, void *base, int count);
int   soundpad_find_nearest_entity(double radius, float *pos);
int  *soundpad_resolve_zone_node(int idx);
void  mtx4_set_rotation_y(double angle, float *mtx);
void  subobj_build_orthonormal_matrix(int body, float *mtx, float *up);
void  subobj_apply_world_bounds(int body);
int   subobj_get_contact_origin(int body);
void  subobj_set_contact_origin(int body, float *origin);
int   subobj_get_descriptor(int body);
void  subobj_set_linear_impulse(int body, float *v);
void  subobj_set_angular_impulse(int body, float *v);
void  subobj_integrate(double dt, float *desc);
void  entity_assign_room(int entity);
int   camera_set_mode(int cam, int mode, int entity);
void  entity_destroy_with_event(void *entity);

// ---- raw symbols referenced with file-local signatures ----
extern int  FUN_803826f0(void);         // fetch active slot instance entity
void slot_pool_reset(void);              // 0x80242da8 (slot_machine_instance.cpp)
extern int  FUN_80242ee0(int entity);    // resolve bound-player view target
extern void FUN_80243a5c(int view, double a2); // apply view target
extern void FUN_8038273c(void);          // instance-context release

// event handler targets (defined elsewhere in the slot module family)
extern void FUN_80243154(void);          // spawn handler
extern void FUN_80243810(void);          // tick handler
extern void FUN_80243a18(void);          // destroy handler

// ---- state globals ----
extern int  DAT_805cad0c;   // machine state
extern int  DAT_805cad08;   // per-state frame counter
extern int  DAT_805cad14;   // attract display-flag cursor A
extern int  DAT_805cad18;   // attract display-flag cursor B
extern int  DAT_805cad20;   // interacting player entity (0 = none)
extern int  DAT_805cad28;   // player-engaged flag
extern int  DAT_805cacdc;   // reel[0].flags (bit1 = locked)
extern int  DAT_805cacec;   // reel[1].flags
extern int  DAT_805cacfc;   // reel[2].flags
extern int  DAT_80610f00;   // spin-loop SFX handle (-1 = none)
extern int  DAT_80612614;   // global prop-subsystem phase (0xb = teardown)

extern int          DAT_8046cb24;  // attract flag table A (0 terminator)
extern int          DAT_8046cbd4;  // attract flag table B (-1 terminator)
extern int         *DAT_80612980;  // slot instance pool base
extern unsigned int DAT_805cad30;  // slot instance freelist head

extern float  FLOAT_80618980;  // zero / default vector component
extern float  FLOAT_80618984;  // zone height bias
extern float  FLOAT_80618988;  // yaw scale numerator
extern float  FLOAT_8061898c;  // yaw scale denominator
extern double DOUBLE_80618990; // int->double magic bias
extern float  DAT_803e0200;    // default anchor.x
extern float  DAT_803e0204;    // default anchor.y
extern float  DAT_803e0208;    // default anchor.z

// -----------------------------------------------------------------------------
// slot_spin_loop_sfx_start (0x802425c0)
// Start the looping spin SFX 0x39d if not already playing (handle DAT_80610f00).
// -----------------------------------------------------------------------------
void slot_spin_loop_sfx_start(void)
{
    int params[14];
    if ((int)DAT_80610f00 < 0) {
        sound_init_play_params((int)params);
        params[0] = 0x39d;
        DAT_80610f00 = sound_play_request(params);
    }
}

// -----------------------------------------------------------------------------
// slot_spin_loop_sfx_stop (0x80242604)
// Stop the looping spin SFX and clear the handle.
// -----------------------------------------------------------------------------
void slot_spin_loop_sfx_stop(void)
{
    if (-1 < DAT_80610f00) {
        sound_stop_handle();
        DAT_80610f00 = -1;
    }
}

// -----------------------------------------------------------------------------
// slot_attract_animate (0x80242638)
// State-0 idle "attract" animation: every 0x1e frames advance two cursors that
// walk the decorative display-flag tables (A wraps on 0, B wraps on -1) and OR
// them into the render flags of the state context.
// -----------------------------------------------------------------------------
void slot_attract_animate(int ctx)
{
    DAT_805cad08 += 1;
    if (DAT_805cad08 < 0x1e) {
        return;
    }
    DAT_805cad14 += 1;
    DAT_805cad08 = 0;
    if ((&DAT_8046cb24)[DAT_805cad14] == 0) {
        DAT_805cad14 = 0;
    }
    DAT_805cad18 += 1;
    if ((&DAT_8046cbd4)[DAT_805cad18] == -1) {
        DAT_805cad18 = 0;
    }
    *(unsigned int *)(*(int *)(ctx + 4) + 0x30) =
        (unsigned int)((&DAT_8046cb24)[DAT_805cad14] | (&DAT_8046cbd4)[DAT_805cad18] | 0x40000);
}

// forward decl
void slot_release_player(void);

// -----------------------------------------------------------------------------
// slot_handle_player_input (0x802426f4)
// Per-frame input for the engaged player. In the no-win hold state (4) the three
// action buttons (0x10/0x40/0x20) toggle the per-reel lock flag (bit1) with a
// click SFX 0x3a1. The exit button (0x400) releases the player. Only runs while
// a player is engaged and standing in the machine's activation zone (menu-state
// marker 0x100 at +0x150[0x45]).
// -----------------------------------------------------------------------------
void slot_handle_player_input(void)
{
    int p1[13];
    int p2[12];
    int p3[12];

    if ((DAT_805cad20 != 0) &&
        ((*(int **)(DAT_805cad20 + 0x150))[0x45] == 0x100) &&
        (DAT_805cad28 != 0)) {
        unsigned int held = input_get_buttons_held(**(int **)(DAT_805cad20 + 0x150));
        input_clear_buttons(**(int **)(DAT_805cad20 + 0x150), 0x470);

        if (DAT_805cad0c == 4) {
            if ((held & 0x10) != 0) {
                DAT_805cacdc ^= 2;
                sound_init_play_params((int)p1);
                p1[0] = 0x3a1;
                sound_play_request(p1);
            }
            if ((held & 0x40) != 0) {
                DAT_805cacec ^= 2;
                sound_init_play_params((int)p2);
                p2[0] = 0x3a1;
                sound_play_request(p2);
            }
            if ((held & 0x20) != 0) {
                DAT_805cacfc ^= 2;
                sound_init_play_params((int)p3);
                p3[0] = 0x3a1;
                sound_play_request(p3);
            }
        }
        if ((held & 0x400) != 0) {
            slot_release_player();
        }
    }
}

// -----------------------------------------------------------------------------
// slot_release_player (0x8024281c)
// Detach the interacting player: reset its pad, pop its menu state, clear the
// engaged flag, and if the machine was merely "presenting" (state 1) drop back
// to idle (state 0).
// -----------------------------------------------------------------------------
void slot_release_player(void)
{
    if (DAT_805cad20 != 0) {
        input_reset_pad(**(int **)(DAT_805cad20 + 0x150));
        player_exit_menu_state();
        DAT_805cad20 = 0;
        DAT_805cad28 = 0;
        if (DAT_805cad0c == 1) {
            DAT_805cad0c = 0;
        }
    }
}

// -----------------------------------------------------------------------------
// slot_get_active_player (0x80242890)
// Return the entity currently interacting with the machine (0 if none).
// -----------------------------------------------------------------------------
int slot_get_active_player(void)
{
    return DAT_805cad20;
}

// -----------------------------------------------------------------------------
// slot_variant_from_model_id (0x802428a0)
// Map a slot-machine model/resource id to its variant index:
//   0x385 -> 0, 0x8c4 -> 1, 0x88f -> 2, anything else -> 0.
// -----------------------------------------------------------------------------
int slot_variant_from_model_id(int model_id)
{
    if (model_id == 0x88f) {
        return 2;
    }
    if (model_id < 0x88f) {
        if (model_id == 0x385) {
            return 0;
        }
    } else if (model_id == 0x8c4) {
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// slot_register_event_handlers (0x802428e4)
// Register the slot-machine prop class (0x10000000) spawn/tick/destroy handlers.
// -----------------------------------------------------------------------------
void slot_register_event_handlers(void)
{
    event_set_handler(0x10000000, 3, (void *)FUN_80243154);   // spawn
    event_set_handler(0x10000000, 0, (void *)FUN_80243810);   // tick
    event_set_handler(0x10000000, 1, (void *)FUN_80243a18);   // destroy
}

// -----------------------------------------------------------------------------
// slot_pool_init (0x8024293c)
// Allocate the slot-machine instance pool (0x170-byte records) and build a
// freelist of stride 0x5c over it, then run the post-init hook.
// -----------------------------------------------------------------------------
void slot_pool_init(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                    unsigned long long a4, unsigned long long a5, unsigned long long a6,
                    unsigned long long a7, unsigned long long a8, unsigned int a11,
                    unsigned int a12, unsigned int a13, unsigned int a14,
                    unsigned int a15, unsigned int a16)
{
    DAT_80612980 = (int *)mem_alloc(a1, a2, a3, a4, a5, a6, a7, a8, 0x170, 1,
                                    a11, a12, a13, a14, a15, a16);
    freelist_init(&DAT_805cad30, 0x5c, DAT_80612980, 4);
    slot_pool_reset();
}

// -----------------------------------------------------------------------------
// slot_instance_tick (0x80242984)
// Per-instance placement / physics tick for a slot-machine entity: settle it
// onto the nearest sound-pad zone, orient by the zone yaw, resolve the physics
// contact origin, zero out impulses, integrate one step, and - when a player is
// bound - drive the interaction camera. Self-destructs when the prop subsystem
// is tearing down (DAT_80612614 == 0xb) and the entity is flagged dead (bit1).
// -----------------------------------------------------------------------------
void slot_instance_tick(void)
{
    int *entity;
    int inst;
    float *contact;
    int *zone;
    int view;
    int cam;
    double dz;
    float local_78, local_74, local_70;
    float afStack_60[16];

    entity = (int *)FUN_803826f0();
    inst = entity[0x1f];
    zone = (int *)0;

    float anchorY = DAT_803e0204;

    if ((*(unsigned int *)(inst + 0x24) & 2) == 0) {
        int idx = soundpad_find_nearest_entity((double)FLOAT_80618980,
                                               (float *)(entity + 0x19));
        if (-1 < idx) {
            zone = soundpad_resolve_zone_node(idx);
            anchorY = FLOAT_80618984 + (float)zone[6];
        }
    }

    dz = DOUBLE_80618990;
    if ((*(unsigned int *)(inst + 0x24) & 2) == 0) {
        if (*(int *)(inst + 0x20) != 0) {
            *(int *)(inst + 0x38) = 0;
            *(float *)(inst + 0x40) = (float)(4503601774854144.0 - dz);
            *(float *)(inst + 0x3c) = (float)(4503601774854144.0 - dz);
            *(int *)(inst + 0x20) = 0;
        }

        mtx4_set_rotation_y((double)((FLOAT_80618988 * anchorY) / FLOAT_8061898c), afStack_60);
        subobj_build_orthonormal_matrix(entity[0x20], afStack_60, (float *)0);
        subobj_apply_world_bounds(entity[0x20]);

        contact = (float *)subobj_get_contact_origin(entity[0x20]);
        dz = (double)((float)zone[4] - (float)entity[0x1a]);
        local_78 = ((float)zone[3] - (float)entity[0x19]) + contact[0];
        local_74 = (float)(dz + (double)contact[1]);
        local_70 = ((float)zone[5] - (float)entity[0x1b]) + contact[2];
        subobj_set_contact_origin(entity[0x20], &local_78);
        subobj_apply_world_bounds(entity[0x20]);
        entity_assign_room((int)entity);

        local_78 = FLOAT_80618980;
        entity[0xd] = (int)*(short *)((int)zone + 10);
        local_74 = local_78;
        local_70 = local_78;
        subobj_set_linear_impulse(entity[0x20], &local_78);
        subobj_set_angular_impulse(entity[0x20], &local_78);
        contact = (float *)subobj_get_descriptor(entity[0x20]);
        subobj_integrate((double)FLOAT_80618980, contact);

        if ((*(int *)(inst + 0x10) != 0) &&
            (cam = *(int *)(*(int *)(inst + 0x10) + 0x150), cam != 0)) {
            view = FUN_80242ee0((int)entity);
            cam = *(int *)(cam + 8);
            if (cam != 0) {
                view = camera_set_mode(cam, *(int *)(inst + 0x28) * 0xc0 + -0x7fb93320,
                                       (int)entity);
            }
            FUN_80243a5c(view, dz);
        }
    }

    if ((DAT_80612614 == 0xb) && ((*(unsigned int *)(inst + 0x24) & 2) != 0)) {
        entity_destroy_with_event(entity);
    }
    FUN_8038273c();
}

} // extern "C"
