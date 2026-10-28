// Camera State Machine - Core camera mode transitions and per-frame update
// Address Range: 0x8009d080 - 0x8009df90
// Date: 2026-08-11
// Batch: 207

#include "common.h"

// ============================================================================
// Camera Mode Types (stored at player_slot +0x218 via player_slot_set_entity)
// ============================================================================
// 0x00 = First-person follow (position from player data structure)
// 0x01 = Third-person orbit (behind player, angle-controlled)
// 0x02 = Entity follow (attached to entity, direction from velocity)
// 0x03 = Fixed point (static position, direction from path node)
// 0x04 = Fixed point + look-at entity
// 0x05 = Multiplayer spectator view
// 0x06 = Gameplay input handler mode
// 0x07 = Death camera (offset behind killer)
// 0x08 = Static world position (level-defined)
// 0x09 = Special scripted mode
// 0x0d-0x0f, 0x1c = Camera path (line/quad/spline interpolation)
// 0x10 = Static with no tracking
// 0x11 = Orbit (free look)
// 0x13 = End marker (path terminator)
// 0x14 = Replay/demo camera
// 0x18 = Vehicle look-behind
// 0x19 = Death/respawn flyover
// 0x1a = Death distance camera
// 0x1b = Pause menu camera
// 0x1d = Scripted transition
// 0x1e = Death look-at (lerp to target)
// 0x1f = Vehicle attached
// 0x20 = Cinematic/cutscene fixed

// ============================================================================
// Extern references
// ============================================================================
extern void player_slot_set_entity(int pSlot, int entity_id);
extern int  player_get_active_count(void);
extern float player_get_camera_lerp_factor(int pSlot);
extern void player_init_spawn_position(int pSlot, void* pos, int room);
extern void player_get_eye_position(int pSlot, float* out_pos);
extern void camera_path_get_position(int pSlot, float* out_pos, float* out_dir);
extern void camera_path_get_direction(int pSlot, float* out_pos, float* out_dir);
extern int  camera_path_segment_is_valid(int pSlot);

// Camera definition table: 0xC0-byte entries at DAT_80402580
extern char DAT_80402580[];

// Global player slot state
extern int DAT_80611d54;    // g_player_slot_current
extern int DAT_80611d30;    // g_spawn_point_count
extern int* DAT_80611d74;   // player data structure pointer

// ============================================================================
// Address: 0x8009d080 | camera_transition_stub_zero
// Returns 0. Stub used in camera transition validation logic.
// 8 xrefs - called during path camera transitions to check conditions.
// ============================================================================
int camera_transition_stub_zero(void)
{
    return 0;
}

// ============================================================================
// Address: 0x8009d088 | camera_stub_nop_a
// Empty stub. 2 xrefs.
// ============================================================================
void camera_stub_nop_a(void)
{
    return;
}

// ============================================================================
// Address: 0x8009d08c | camera_stub_nop_b
// Empty stub. 1 xref.
// ============================================================================
void camera_stub_nop_b(void)
{
    return;
}

// ============================================================================
// Address: 0x8009d090 | camera_set_mode_from_spawn
// Wrapper that calls camera_set_mode using the spawn point count as the
// camera definition source. 1 xref.
// ============================================================================
void camera_set_mode_from_spawn(int camera_def, int entity_ref)
{
    camera_set_mode(DAT_80611d30, camera_def, entity_ref);
}

// ============================================================================
// Address: 0x8009d0c0 | camera_set_mode
// MASSIVE camera state transition handler. 13 xrefs.
// Processes a camera node definition pointer and configures the player slot
// for the appropriate camera behavior. Handles 20+ camera mode types via switch.
//
// Core logic:
// 1. Validates the new camera node (non-null, different from current or forced)
// 2. Stores previous/next path node pointers (+0x26c, +0x270)
// 3. Calls player_slot_set_entity with the node type as the "entity ID"
// 4. Sets transition flags based on path type combinations
// 5. Per-mode initialization:
//    - Mode 0 (follow): stores entity ref, inits spawn position from entity
//    - Mode 1 (third-person): clears entity, zeroes angles, copies entity pos
//    - Mode 2 (entity follow): stores entity, inits from entity position
//    - Mode 3 (fixed): validates path segment, inits from node position
//    - Mode 4 (fixed+lookat): same as 3 but stores entity for look-at
//    - Mode 0xd-0xf,0x1c (path): evaluates path position, stores prev pos
//    - Mode 0x10 (static): clears all refs
//    - Mode 0x11 (orbit): clears refs, calls orbit init
//    - Mode 0x18 (vehicle behind): same as mode 1 but for vehicles
//    - Mode 0x19 (death flyover): builds path from entity + offset
//    - Mode 0x1a (death distance): builds path from distance to entity
//    - Mode 0x1e (death lookat): stores entity ref
//    - Mode 0x1f (vehicle attached): inits from vehicle transform
//    - Mode 0x20 (cinematic): inits from node + vehicle direction
// ============================================================================
void camera_set_mode(int param_1, int camera_node, int entity_ref)
{
    // [MASSIVE implementation - see Ghidra decompilation at 0x8009d0c0]
    // ~750 bytes of switch-case handling for camera mode initialization.
    // Each case configures:
    //   - pSlot+0x2a8 (entity reference A)
    //   - pSlot+0x2ac (entity reference B)
    //   - pSlot+0x21c (flag bits for mode behavior)
    //   - pSlot+0x26c (current path node)
    //   - pSlot+0x270 (previous path node)
    //   - Position via player_init_spawn_position
    //   - Mode-specific angle/velocity initialization
}

// ============================================================================
// Address: 0x8009d830 | camera_set_mode_by_index
// Indexes into the camera definition table (0xC0-byte entries at DAT_80402580)
// and calls camera_set_mode with the resolved definition pointer.
// 31 xrefs - the primary entry point for camera mode changes.
// ============================================================================
void camera_set_mode_by_index(int param_1, int def_index, int entity_ref)
{
    camera_set_mode(param_1, (int)&DAT_80402580[def_index * 0xC0], entity_ref);
}

// ============================================================================
// Address: 0x8009d860 | camera_death_look_at
// Sets up a "death look-at" camera transition. Resolves the target zone/position,
// computes a per-frame velocity vector towards the target, and configures the
// lerp factor for smooth interpolation.
// 6 xrefs - called during death/respawn camera transitions.
// ============================================================================
void camera_death_look_at(float* target_pos)
{
    int pSlot;  // from coroutine context
    float eye_pos[3];
    float direction[3];
    float velocity_scale;
    int zone_id;

    // Resolve target zone from spawn system
    zone_id = FUN_80238f94();
    if (zone_id == -1) {
        // No zone - use raw target position
        direction[0] = target_pos[0];
        direction[1] = target_pos[1];
        direction[2] = target_pos[2];
    } else {
        // Resolve zone position from sound pad system
        // (zones store camera-relevant position data)
        int zone_ptr = soundpad_find_zone_by_id(zone_id);
        // ... resolves position from zone data ...
    }

    // Set camera mode to 0x1e (death look-at)
    player_slot_set_entity(DAT_80611d54, 0x1e);

    // Compute velocity = (target - current) / duration
    // pSlot+0x2c4..0x2cc = velocity vector
    // If duration > 0, scale velocity by inverse frame-duration
    // pSlot+0x2d0 = duration
    // pSlot+0x2d4 = elapsed time (starts at 0)
    // pSlot+0x2d8..0x2e0 = target direction
    // pSlot+0x2e4 = lerp start factor

    // Apply camera lerp factor for smooth transition
    float lerp = player_get_camera_lerp_factor(pSlot);
    // Store lerp and direction in player data
}

// ============================================================================
// Address: 0x8009da9c | camera_movement_tick
// Per-frame camera position integration with speed capping and portal traversal.
// 4 xrefs - called by path camera modes (types 3, 4, 0xd-0x1c).
//
// Algorithm:
// 1. Compute delta = target_position - current_position
// 2. Compute velocity from frame-rate-independent derivative
// 3. Cap movement speed based on flags (0x1800 = path mode uses higher cap)
// 4. If capped, scale delta to max speed
// 5. Compute velocity for this frame (delta * fps_factor)
// 6. Store velocity history at +0x2e8..0x2f0 and +0x31c..0x324
// 7. Call scene_ray_traverse_portals to update room assignment
// 8. Integrate position: pos += delta
// 9. Store target as previous position (+0x304..0x30c)
// ============================================================================
void camera_movement_tick(void)
{
    int pSlot;  // from coroutine context
    float* target_pos;
    float delta[3];
    float speed_cap;
    float distance;

    // Speed cap based on camera transition flags
    speed_cap = 400.0f;  // FLOAT_806146c4 - default
    if ((*(u32*)(pSlot + 0x21c) & 0x1800) != 0) {
        speed_cap = 800.0f;  // FLOAT_806146c8 - path mode higher cap
    }

    // Compute frame-rate-independent max distance
    float max_distance = FLOAT_806108ec * (speed_cap / fps_to_time_factor);

    // delta = target - current
    delta[0] = target_pos[0] - *(float*)(pSlot + 0x27c);
    delta[1] = target_pos[1] - *(float*)(pSlot + 0x280);
    delta[2] = target_pos[2] - *(float*)(pSlot + 0x284);

    distance = sqrt(delta[0]*delta[0] + delta[1]*delta[1] + delta[2]*delta[2]);

    // Cap movement to max speed
    if (max_distance < distance) {
        float scale = max_distance / distance;
        delta[0] *= scale;
        delta[1] *= scale;
        delta[2] *= scale;
    }

    // Store velocity (per-second) at +0x2e8
    *(float*)(pSlot + 0x2e8) = (delta[0] * fps_to_time_factor) / FLOAT_806108ec;
    *(float*)(pSlot + 0x2ec) = (delta[1] * fps_to_time_factor) / FLOAT_806108ec;
    *(float*)(pSlot + 0x2f0) = (delta[2] * fps_to_time_factor) / FLOAT_806108ec;

    // Portal traversal for room tracking
    *(u32*)(pSlot + 0x2a4) = *(u32*)(pSlot + 0x2a0);
    int new_room = scene_ray_traverse_portals(pSlot + 0x27c, *(u32*)(pSlot + 0x2a4), delta, 0);
    *(int*)(pSlot + 0x2a0) = new_room;

    // Integrate position
    *(float*)(pSlot + 0x27c) += delta[0];
    *(float*)(pSlot + 0x280) += delta[1];
    *(float*)(pSlot + 0x284) += delta[2];

    // Store target as history
    *(float*)(pSlot + 0x304) = target_pos[0];
    *(float*)(pSlot + 0x308) = target_pos[1];
    *(float*)(pSlot + 0x30c) = target_pos[2];
}

// ============================================================================
// Address: 0x8009dd64 | camera_direction_slerp
// Spherical interpolation of camera direction vector towards a target direction.
// Converts to yaw/pitch, computes angular delta with wrapping, interpolates
// by a per-frame factor, then converts back to Cartesian unit vector.
// 1 xref - called from camera_per_frame_update for path cameras.
//
// Parameters:
//   pSlot   - player slot
//   target  - target direction (vec3)
//   steps   - interpolation frame count (higher = slower)
// ============================================================================
void camera_direction_slerp(int pSlot, float* target, u32 steps)
{
    if (DAT_806108e4 == 0) return;  // skip if paused

    if ((int)steps < 1) {
        // Instant snap
        *(float*)(DAT_80611d54 + 0x334) = target[0];
        *(float*)(DAT_80611d54 + 0x338) = target[1];
        *(float*)(DAT_80611d54 + 0x33c) = target[2];
    } else {
        // Spherical interpolation:
        // 1. Convert current direction to (yaw, pitch)
        // 2. Convert target direction to (yaw, pitch)
        // 3. Compute angular delta with [-PI, PI] wrapping
        // 4. Interpolate: angle += (delta / steps) * fps_factor
        // 5. Convert back: x = cos(pitch)*sin(yaw), y = sin(pitch), z = cos(pitch)*cos(yaw)
        float blend = 1.0f / (float)steps;

        // Current -> spherical
        float cur_dist_xz = sqrt(cur_x*cur_x + cur_z*cur_z);
        float cur_yaw = atan2(cur_x, cur_z);
        float cur_pitch = atan2(cur_y, cur_dist_xz);

        // Target -> spherical
        float tgt_dist_xz = sqrt(target[0]*target[0] + target[2]*target[2]);
        float tgt_yaw = atan2(target[0], target[2]);
        float tgt_pitch = atan2(target[1], tgt_dist_xz);

        // Wrap delta to [-PI, PI]
        float d_yaw = tgt_yaw - cur_yaw;
        while (d_yaw <= -PI) d_yaw += TWO_PI;
        while (d_yaw > PI) d_yaw -= TWO_PI;

        float d_pitch = tgt_pitch - cur_pitch;
        while (d_pitch <= -PI) d_pitch += TWO_PI;
        while (d_pitch > PI) d_pitch -= TWO_PI;

        // Blend
        float new_yaw = cur_yaw + blend * d_yaw;
        float new_pitch = cur_pitch + blend * d_pitch;

        // Back to Cartesian
        *(float*)(DAT_80611d54 + 0x334) = cos(new_pitch) * sin(new_yaw);
        *(float*)(DAT_80611d54 + 0x338) = sin(new_pitch);
        *(float*)(DAT_80611d54 + 0x33c) = cos(new_pitch) * cos(new_yaw);
    }
}

// ============================================================================
// Address: 0x8009df90 | camera_per_frame_update
// MASSIVE per-frame camera update function. 7 xrefs.
// This is the main camera tick called every frame for the active player slot.
//
// Responsibilities:
// 1. Double-buffer management: swaps matrix pointers (+0x200..0x210)
// 2. FOV computation (calls FUN_8009fd34 for perspective setup)
// 3. Per-mode position/direction update via large switch:
//    - Mode 0: copy from player data structure
//    - Mode 1: third-person orbit update (FUN_800a18d4)
//    - Mode 2: entity follow with velocity-based direction
//    - Mode 3: fixed point with player-controlled look (analog input)
//    - Mode 4: fixed point with look-at entity
//    - Mode 0xd-0x1c: path camera with portal traversal + collision
//    - Mode 0x11: orbit (FUN_800a0ec0)
//    - Mode 0x14: replay with spline interpolation
//    - Mode 0x18/0x20: vehicle modes (FUN_800a28b4)
//    - Mode 0x19/0x1a: death cameras (FUN_800a35b0)
//    - Mode 0x1b: pause camera with spline path playback
//    - Mode 0x1e: death look-at lerp
//    - Mode 0x1f: vehicle attached with transform extraction
// 4. Camera shake/roll application (fields +0x34c, +0x350)
// 5. Direction normalization and up-vector orthonormalization
// 6. View matrix construction (FUN_8012f680 -> builds look-at matrix)
// 7. Matrix double-buffer copy and interpolation
// 8. Motion blur factor computation (+0x3b4)
// 9. Final flag updates (bit 0x420 = matrix valid + room valid)
// ============================================================================
void camera_per_frame_update(void)
{
    // [MASSIVE implementation - ~2000 bytes of camera mode switch + matrix math]
    // See Ghidra decompilation at 0x8009df90 for full pseudocode.
    //
    // Key player slot fields used:
    //   +0x200: double-buffer index (0 or 1)
    //   +0x204..0x210: matrix buffer pointers (view, inv, proj, combined)
    //   +0x218: current camera mode type
    //   +0x21c: camera flags
    //   +0x26c: current path node pointer
    //   +0x270: previous path node pointer
    //   +0x27c..0x284: camera world position (vec3)
    //   +0x288..0x290: previous frame position (vec3)
    //   +0x294..0x29c: angular velocity (yaw/pitch/roll)
    //   +0x2a0: current room index
    //   +0x2a4: previous room index
    //   +0x2a8: entity reference A (varies by mode)
    //   +0x2ac: entity reference B (varies by mode)
    //   +0x2b0..0x2b8: saved position (for path cameras)
    //   +0x2c4..0x2cc: movement velocity
    //   +0x2d0..0x2e4: lerp parameters
    //   +0x304..0x30c: previous target position
    //   +0x310..0x318: target entity position
    //   +0x31c..0x324: velocity history
    //   +0x328..0x330: look-at point (pos + direction)
    //   +0x334..0x33c: camera direction (normalized forward vec3)
    //   +0x340..0x348: previous direction
    //   +0x34c..0x354: camera shake offsets (yaw, pitch, roll)
    //   +0x358..0x360: post-shake direction
    //   +0x364..0x36c: camera up vector
    //   +0x370..0x378: camera right vector
    //   +0x384: FOV (field of view)
    //   +0x388: FOV interpolation target
    //   +0x3b4: motion blur factor
    //   +0xfd0: pause camera animation handle
    //   +0xfd4: pause camera time accumulator
    //   +0xfe4..0xfe8: angular state (yaw, pitch)
}
