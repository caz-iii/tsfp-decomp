// Address: 0x80358ac4 | Name: melee_spark_pick_texture      | Date: 2026-09-12
// Address: 0x80358b58 | Name: melee_target_is_hittable      | Date: 2026-09-12
// Address: 0x80358bf0 | Name: melee_target_is_arc_target    | Date: 2026-09-12
// Address: 0x80358f54 | Name: melee_arc_mark_camera_slot    | Date: 2026-09-12
// Address: 0x80358fc4 | Name: melee_arc_spawn_emitter       | Date: 2026-09-12
// Address: 0x8035904c | Name: melee_arc_spawn_trail         | Date: 2026-09-12
// Address: 0x803590f8 | Name: melee_spark_spawn_impact      | Date: 2026-09-12
//
// Melee / electro-weapon hit effect. When a melee weapon strikes a valid target
// the impact dispatcher (player_ai_impact_dispatch @ 0x800b6b48) runs an arc
// (electric) particle burst attached to the target and a random spark impact
// from the hit ray. The arc emitter templates are the shared particle-def
// records set up by weather_electric_effect_init (DAT_805e3c84 / 805e3dd4 /
// 805e3f24 / 805e4074).
//   melee_target_is_hittable  gate: target holds a melee weapon, not shielded,
//                             and weapon id is not 0x1c / 0x35.
//   melee_target_is_arc_target adds a coop-node check on top of the above.
//   melee_arc_spawn_*         spawn the arc emitter / trail and tag the camera
//                             slot so the local player's own view is excluded.

#include "common.h"

// ---- particle-def / emitter templates (shared with weather electric fx) ----
extern int DAT_805e3c84;   // arc emitter template 0
extern int DAT_805e3dd4;   // arc emitter template 1
extern int DAT_805e3f24;   // arc emitter template 2 (copy of template 1, add blend)
extern int DAT_805e4074;   // arc trail emitter template

// ---- globals ---------------------------------------------------------------
extern int DAT_80612610;   // active local-player / split count

// ---- helpers ---------------------------------------------------------------
extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);
extern int  rng_counter_tick(void);
extern int  sprite_get_texture_id(int ref);
extern int  player_ai_is_melee_weapon(int weaponId);
extern int  player_get_camera_slot_index(int ctx);
extern unsigned int tracker_actor_coop_node_active(int actor);
extern void particle_emitter_set_scale(int mask);
extern void particle_emitter_spawn(int a, void* tmpl, int b, float* pos, float* vel);
extern void particle_emitter_spawn_attached(int* handle, void* tmpl, int a, int b);
extern void particle_handle_clear(int* handle);
extern void particle_handle_release(int* handle);
extern void particle_emitter_set_flags2(int* handle, int a, int b);
extern void particle_cached_set_position(int* handle, unsigned int* pos);
extern void particle_cached_set_velocity(int* handle, unsigned int* vel);
extern void particle_spawn_impact_from_ray(int texId, int a, int color, unsigned short flags,
                                           int b, unsigned int c, int d, float* ray);

// forward decls (used before their definition in this file)
int  melee_target_is_hittable(int target);
void melee_arc_mark_camera_slot(int emitter);
int  melee_spark_pick_texture(void);

// ============================================================================
// melee_spark_pick_texture  (0x80358ac4)
// Pick one of five spark impact textures at random (sprite refs 0x3bc..0x3c1).
// Returns the resolved texture id from sprite_get_texture_id.
// ============================================================================
int melee_spark_pick_texture(void)
{
    unsigned int r = (unsigned int)rng_counter_tick() % 5;
    if (r == 2)  return sprite_get_texture_id(0x3bd);
    if (r < 2) {
        if (r == 0) return sprite_get_texture_id(0x3c0);
        return sprite_get_texture_id(0x3bc);
    }
    if (r != 4 && r < 4) return sprite_get_texture_id(0x3bf);
    return sprite_get_texture_id(0x3c1);
}

// ============================================================================
// melee_target_is_hittable  (0x80358b58)
// True when the target is a valid melee-strike victim: it is either the active
// player slot's actor (or single-player), holds a melee weapon, has no
// stun/immune flag (0x40000), and its weapon id is not 0x1c and not 0x35 (and
// its held-item's flag word lacks 0x40000).
// ============================================================================
int melee_target_is_hittable(int target)
{
    int result = 0;
    if ((target == 0) || (DAT_80612610 <= *(int*)(target + 8)) || (1 < DAT_80612610)) {
        if (player_ai_is_melee_weapon((int)*(short*)(target + 0x4a)) != 0 &&
            ((*(unsigned int*)(target + 0xc6c) & 0x40000) == 0) &&
            (*(short*)(target + 0x48) != 0x1c)) {
            if ((*(short*)(target + 0x48) != 0x35) &&
                ((*(unsigned int*)(*(int*)(target + 0xbf0) + 0x58) & 0x40000) == 0)) {
                result = 1;
            }
        }
    }
    return result;
}

// ============================================================================
// melee_target_is_arc_target  (0x80358bf0)
// Hittable AND not currently owned by an active coop tracker node.
// ============================================================================
int melee_target_is_arc_target(int target)
{
    int result = 0;
    if (melee_target_is_hittable(target) != 0 &&
        tracker_actor_coop_node_active(target) == 0) {
        result = 1;
    }
    return result;
}

// ============================================================================
// melee_arc_mark_camera_slot  (0x80358f54)
// If the emitter belongs to an object bound to a local player camera, mask that
// camera slot out of the emitter's per-view visibility so the wielding player
// does not see their own arc effect.
// ============================================================================
void melee_arc_mark_camera_slot(int emitter)
{
    int slot;
    int* owner;
    if ((emitter != 0) && (*(int*)(emitter + 0x20) == 8) &&
        (owner = *(int**)(emitter + 0x7c), owner != 0) &&
        (owner[2] < DAT_80612610) &&
        (slot = player_get_camera_slot_index(*(int*)(*owner + 0x150)), -1 < slot)) {
        particle_emitter_set_scale(~(1 << slot));
    }
}

// ============================================================================
// melee_arc_spawn_emitter  (0x80358fc4)
// Spawn one of the three arc emitter templates (selected by templateIdx 0..2)
// at pos/vel, then mask out the owning camera slot.
// ============================================================================
void melee_arc_spawn_emitter(int templateIdx, int target, unsigned int a3, float* pos, float* vel)
{
    void* tmpl = 0;
    if (templateIdx == 1) {
        tmpl = &DAT_805e3dd4;
    } else if (templateIdx < 1) {
        if (-1 < templateIdx) tmpl = &DAT_805e3c84;
    } else if (templateIdx < 3) {
        tmpl = &DAT_805e3f24;
    }
    if (tmpl != 0) {
        particle_emitter_spawn(a3, tmpl, 0, pos, vel);
        melee_arc_mark_camera_slot(target);
    }
}

// ============================================================================
// melee_arc_spawn_trail  (0x8035904c)
// Spawn the attached arc-trail emitter (DAT_805e4074), mask the owning camera
// slot, then push the requested position and velocity into the cached emitter.
// ============================================================================
void melee_arc_spawn_trail(unsigned int a1, unsigned int a2, unsigned int* pos, unsigned int* vel)
{
    unsigned long long saved = __save_gpr_28();
    int target = (int)(saved >> 0x20);
    int emitParam = (int)saved;

    int handle[7];
    particle_handle_clear(handle);
    particle_emitter_spawn_attached(handle, &DAT_805e4074, target, emitParam);
    melee_arc_mark_camera_slot(target);

    int h;
    h = handle[0];
    particle_emitter_set_flags2(&h, 2, 0);
    h = handle[0];
    particle_cached_set_position(&h, pos);
    h = handle[0];
    particle_cached_set_velocity(&h, vel);
    particle_handle_release(handle);
    __restore_gpr_28();
}

// ============================================================================
// melee_spark_spawn_impact  (0x803590f8)
// Spawn a random spark impact effect from the hit ray: pick a random texture,
// tick the rng, and hand it to the ray-impact particle spawner with the fixed
// electric-spark colour 0x7f323232.
// ============================================================================
void melee_spark_spawn_impact(unsigned int a1, unsigned int a2, unsigned int surfaceId,
                              unsigned short flags)
{
    unsigned long long saved = __save_gpr_28();
    rng_counter_tick();
    int tex = melee_spark_pick_texture();
    particle_spawn_impact_from_ray(tex, 1, 0x7f323232, flags, 1, surfaceId,
                                   (int)(saved >> 0x20), (float*)(int)saved);
    __restore_gpr_28();
}
