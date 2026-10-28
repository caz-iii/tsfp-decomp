// worm_web_net.cpp
// Worm mission (CDH1) web-net strand effect. A pool of "strand" records
// (DAT_80612988, stride 0x140, count DAT_805cad9c) attached to the WRTH entity
// (handle 0x57525448). Each strand has a state at +0x00:
//   0 = idle/anchored, 1 = growing/active projectile, 2 = dead/expired.
// The strands sweep across the scene nodes registered in the fx node list
// (DAT_805cad40, count DAT_805cad54 - populated by the slot_fx list in batch_717)
// and can capture the player, render as textured projected quads, scatter, be
// popped by explosions, and score via worm_cdh1_award_points / statlvl_class_*.
//
// This file covers the render pass, the master per-frame simulation tick, strand
// scattering, swept-capture test, explosion burst, and per-strand swept-AABB build.
//
// Strand record layout (int/float indices into the 0x140-byte record):
//   [0]      state (0/1/2)
//   [7..9]   position xyz            (byte 0x1c..0x24)
//   [0x10..0x12] velocity xyz        (byte 0x40..0x48)
//   [0x0a..0x0f] swept AABB min/max  (byte 0x28..0x3c) built by build_swept_aabb
//   [0x14]   angular speed scale
//   [0x15..0x17] spin axis
//   [0x18]   spin phase
//   [0x19]   fade / length scalar
//   [0x1a]   life phase (state 1)
//   [0x1b]   particle emitter handle
//   [0x4e]   (short) trail write cursor (0..7 ring)
//   [0x4f]   trail timestamp
//   +0x130   trail alpha ring (8 bytes)
//   [0x1c..] +0x34.. trail point ring (two endpoints x 8 slots)
//
// Address range: 0x80244a74 - 0x802472b3

// Self-contained translation unit (no shared-header include) so the module's
// call-site signatures for cross-module helpers do not fight header drift; all
// raw symbols and helper prototypes are declared file-locally with extern "C"
// linkage matching the ROM's unmangled symbols. Ghidra-style intrinsics used
// here (SQRT/ABS) are provided as file-local inlines.

static inline float SQRT(float x) { return __builtin_sqrtf(x); }
static inline float ABS(float x)  { return x < 0.0f ? -x : x; }

extern "C" {

// ---- named cross-module helpers (file-local, ROM signatures per call site) ----
int    entity_handle_resolve(int handle);
void   scene_query_nodes_in_frustum(int a1, int aabb, int out_node, int mask);
int    scene_node_get_render_data(int id);
void   scene_get_node_aabb(int node, float *min_out, float *max_out);
void   collision_set_layer_mask(int *node, int mask);
int    anim_is_clip_active(int entity, int clip);
void   anim_play_scaled(double a1, double a2);
void   mtx4_basis_from_fwd_ref(float *mtx, float *fwd, float *up);
void   mtx4_scale_axes(double sx, double sy, double sz, float *mtx);
void   mtx4_transform_vector(float *mtx, float *v);
void   mtx4_set_axis_angle(double angle, float *mtx, float *axis);
void   mtx4_set_rotation_y(double angle, float *mtx);
void   gx_set_state_c0(int v);
void   gx_set_sub_material_id(int id);
void   gx_set_blend_mode(int mode);
int    render_instance_draw_pass(double a1, double a2, int node, int pass, int a5);
void   gx_begin_projected_batch(double a1, double a2, double a3, double a4, int count, int tex);
void   gx_projtex_add_point(double x, double y, double z, double a4, double a5, double a6, float col);
void   gx_end_batch(void);
int    sprite_get_texture_id(int id);
int    aiactor_find_player_by_team(int team);
int    entity_swept_cast_room(int node, int *pos, float *vel, int a4, int *out_list, int mask, int a7);
double floor_query_with_interact(double a1, double a2, int *pos, int a4, int *a5, int a6);
void   subobj_constraint_pool_reset(void);
void   build_swept_bounds(double radius, int *out_bounds, float *from, float *to);
int    aabb_vs_entity_bounds(int bounds, float *entity_aabb);
void   sound_init_play_params(int params);
void   sound_play_request(int *params);
void   sound_update_3d_source(double a1, double a2, double a3, int *a4, int *a5, int snd, int a7);
void   scalar_step_toward_snap(double target, double rate, int *state);
int    rng_counter_tick(void);
double math_sqrt(double x);
void   particle_emitter_set_flags(int *rec, int set, int clear);
void   particle_cached_set_drag(double drag, int *rec);
void   particle_cached_set_position(int *rec, int *pos);
void   particle_cached_set_size_range(double lo, double hi, int *rec);
void   player_ai_explosion_spawn(int a1, int a2, float *pos, float *a4, int a5, int a6, int a7);
void   worm_cdh1_award_points(int *pos);
void   statlvl_class_set_state2(int state);
void   statlvl_class_complete(double a1, double a2, double a3, double a4, double a5, double a6, double a7, double a8);
int    switch_slot_reset(int ctx);
void   damage_dispatch_to_entity(int ctx, double a2, double a3, double a4, double a5, double a6, double a7, double a8, int *params);

// ---- raw symbols referenced with file-local signatures ----
// Render / tick context save+restore intrinsics (packed hi:pointer return).
extern unsigned long long FUN_803826cc(void);  // render-context enter (returns WRTH view ptr)
extern void FUN_80382718(void);                // render-context leave
extern void FUN_803826c8(void);                // sim-tick context enter
extern void FUN_80382714(void);                // sim-tick context leave
extern unsigned long long FUN_803826e8(void);  // work-context enter (packed hi:vector ptr)
extern void FUN_80382734(void);                // work-context leave
extern int  FUN_803825c0(double x);            // clamp-to-byte alpha (0..255) from float
extern void FUN_8037dc94(double a1, double a2, double a3, int *emitter, int flags,
                         float *pos, float *vel, int *a8, int a9, int a10, int a11); // particle spawn
extern unsigned long long __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern unsigned long long __save_gpr_25(void);
extern void __restore_gpr_25(void);
// FUN_8024786c (net-ownership / mission-active gate) is DEFINED in this file
// (batch_719) as webnet_mission_active below; no extern needed.

// ---- batch_719 additions (net phase control, strand spawn, phase params, bones) ----
extern int   FUN_803826f0(void);                // render/anim list lock enter; returns WRTH ctx/entity ptr
extern void  FUN_8038273c(void);                // render/anim list lock (leave)
extern void  FUN_80249364(void);                // web-net bone-matrix commit (defined later)
void   mtx4_copy(float *dst, float *src);
void   mtx4_set_identity(int mtx);
void   anim_blend_eval_flagged(int entity);

extern int   DAT_805cadb8;   // net life state / phase selector (0..10, 9=score-complete)
extern int   DAT_805cae30;   // secondary-offset enable flag
extern float DAT_805cae04;   // secondary vertical offset

extern float FLOAT_80618ab4;
extern float FLOAT_80618ab8;
extern float FLOAT_80618ac0;
extern float FLOAT_80618ac4;
extern float FLOAT_80618ac8;

extern float DAT_803e0280;   // per-frame bone offset template (x/scroll)
extern float DAT_803e0284;
extern float DAT_803e0288;

// Net spawn anchor table: 5 vec3 seed positions (stride 0xc), indexed by byte.
extern float DAT_8046ce00;

// ---- forward declarations for this module's own functions ----
void webnet_strand_pick_anchor_node(int strand, int enabled);
int  webnet_strand_advance_trail_index(int strand);
void webnet_render_strands(void);
int  webnet_node_in_active_list(int node);
void webnet_simulate_tick(void);
void webnet_scatter_strands(void);
void webnet_capture_check_swept(unsigned long long param_1, double param_2, double param_3,
                                double param_4, double param_5, double param_6,
                                double param_7, double param_8);
int  webnet_count_free_strands(void);
void webnet_explosion_burst_strands(int param_1, int param_2, float *param_3);
void webnet_strand_build_swept_aabb(int strand);
// batch_719 forward decls (definitions at end of file)
void webnet_strand_spawn(float *pos);
int  webnet_mission_active(void);   // was FUN_8024786c

// ---- state globals (web-net) ----
extern int   DAT_80612988;   // strand pool base
extern int   DAT_805cad9c;   // strand pool count
extern int   DAT_805cad54;   // fx node-list count
extern int   DAT_805cad40;   // fx node-list base (int[])
extern int   DAT_805cad74;   // net phase: 0 off, 1 growing, 2 attached, 3 retract
extern float DAT_805cad78;   // net progress timer
extern int   DAT_805cad88;   // has-explicit-target flag
extern int   DAT_805cada0;   // net behaviour flags
extern float DAT_805cad7c;   // target xyz
extern float DAT_805cad80;
extern float DAT_805cad84;
extern float DAT_805cad58;   // current attach centre xyz
extern float DAT_805cad5c;
extern float DAT_805cad60;
extern float DAT_805cad64;   // attach drift dir xyz
extern float DAT_805cad68;
extern float DAT_805cad6c;
extern float DAT_805cad8c;   // guide-point active timer
extern float DAT_805cad90;   // guide point xyz
extern float DAT_805cad94;
extern float DAT_805cad98;
extern int   DAT_805cada4;   // 3d-sound source state
extern float DAT_805cada8;   // net loudness (smoothed)
extern float DAT_805cadac;   // net centroid xyz (for sound)
extern float DAT_805cadb0;
extern float DAT_805cadb4;

extern int   DAT_80611928;   // global frame counter
extern int   DAT_806108e4;   // per-frame dt fixed-point base
extern int   DAT_80611b1c;   // node-visible flags base
extern int   DAT_80611d54;   // active camera block base
extern int   DAT_80611d78;   // player record table base
extern int   DAT_80612610;   // player record count
extern int   DAT_80612614;   // mission mode selector
extern int   DAT_80612098;   // active projectile count
extern int   DAT_80588610;   // active projectile table base
extern int   DAT_805015c0;   // global gameplay flags
extern int   DAT_805015d8;   // net-lethal flag

// packed 0x43300000 double-conversion helpers (magic-number int->double reads)
extern unsigned int DAT_806109c0;
extern unsigned int DAT_806109c4;
extern unsigned int DAT_806109c8;
extern unsigned int DAT_8060f950;
extern unsigned int DAT_8060f954;

extern float  FLOAT_806108ec;
extern float  FLOAT_80618a18;  // 0.0
extern float  FLOAT_80618a1c;  // 1.0
extern float  FLOAT_80618a24;  // 0.5-ish constant used widely
extern float  FLOAT_80618a28;
extern float  FLOAT_80618a2c;
extern float  FLOAT_80618a30;
extern float  FLOAT_80618a34;
extern float  FLOAT_80618a38;
extern float  FLOAT_80618a3c;
extern float  FLOAT_80618a40;
extern float  FLOAT_80618a44;
extern float  FLOAT_80618a48;
extern float  FLOAT_80618a60;
extern float  FLOAT_80618a64;
extern float  FLOAT_80618a68;
extern float  FLOAT_80618a6c;
extern float  FLOAT_80618a70;
extern float  FLOAT_80618a74;
extern float  FLOAT_80618a78;
extern float  FLOAT_80618a7c;
extern float  FLOAT_80618a80;
extern float  FLOAT_80618a84;
extern float  FLOAT_80618a88;
extern float  FLOAT_80618a8c;
extern float  FLOAT_80618a90;
extern float  FLOAT_80618a94;
extern float  FLOAT_80618a98;
extern float  FLOAT_80618a9c;
extern float  FLOAT_80618aa0;
extern float  FLOAT_80618aa4;
extern float  FLOAT_80618aa8;
extern float  FLOAT_80618aac;
extern float  FLOAT_80618ab0;
extern double DOUBLE_80618a50;
extern double DOUBLE_80618a58;

extern float DAT_803e0240;  // constant vectors used as templates
extern float DAT_803e0244;
extern float DAT_803e0248;
extern float DAT_803e024c;
extern float DAT_803e0250;
extern float DAT_803e0254;
extern float DAT_803e0258;
extern float DAT_803e025c;
extern float DAT_803e0260;
extern float DAT_803e0264;
extern float DAT_803e0268;
extern float DAT_803e026c;
extern float DAT_803e0270;
extern float DAT_803e0274;
extern float DAT_803e0278;

// Helper: reinterpret the GameCube int->double magic conversion
// ((0x43300000 << 32) | (u ^ 0x80000000)) - 2^52 - 2^31  ==  (double)(int)u.
// The decompiler emits this inline; we express it as (double)(int)u for a
// working non-matching translation.

// ============================================================================
// 0x80244a74  webnet_strand_pick_anchor_node
// Choose the scene node a strand renders against: if the strand's own node
// (+0x04) is in the fx node list, keep it, else fall back to the first entry.
// ============================================================================
void webnet_strand_pick_anchor_node(int strand, int enabled)
{
    bool found;
    int  idx;
    int  remaining;

    found = false;
    if (enabled != 0) {
        scene_query_nodes_in_frustum(0, strand + 0x28, strand + 4, 5);
        idx = 0;
        remaining = DAT_805cad54;
        if (0 < DAT_805cad54) {
            do {
                if (*(int *)(strand + 4) == *(int *)((int)&DAT_805cad40 + idx)) {
                    found = true;
                    break;
                }
                idx += 4;
                remaining += -1;
            } while (remaining != 0);
        }
        if (found) {
            *(int *)(strand + 0x18) = *(int *)(strand + 4);
        } else {
            *(int *)(strand + 0x18) = DAT_805cad40;
        }
    }
}

// ============================================================================
// 0x80244b20  webnet_strand_advance_trail_index
// Advance the strand's per-frame trail timestamp; returns 1 when the trail
// ring should record a new sample this frame (not already stamped this frame
// and the accumulated dt byte at +0x13b wrapped non-zero).
// ============================================================================
int webnet_strand_advance_trail_index(int strand)
{
    if (*(int *)(strand + 0x13c) != DAT_80611928) {
        *(char *)(strand + 0x13b) = (char)(*(char *)(strand + 0x13b) + (char)DAT_806108e4);
        if (*(char *)(strand + 0x13b) != '\0') {
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// 0x80244b5c  webnet_render_strands
// Render pass: if any registered node is visible, draw each live strand as an
// oriented, scaled projected-texture ribbon (head quad + trail ring), fading in
// over its length scalar. Called from session_render_scene_by_mode.
// ============================================================================
void webnet_render_strands(void)
{
    int   view;
    bool  visible;
    float fx, fy, fz;
    float ax, ay, az;
    float len2, inv, scl;
    int   idx, remaining;
    int   entity;
    int   strandIdx;
    int   strandOff;
    int  *strand;
    unsigned int alpha;
    unsigned int trailN, ring;
    double lenScale;
    double drawRet;
    int    tex;
    int   *node;
    float *vtx;
    float e0, e1, e2;
    double packHi = 0.0;

    FUN_803826cc();

    visible = false;
    idx = 0;
    remaining = DAT_805cad54;
    if (0 < DAT_805cad54) {
        do {
            if (*(char *)(DAT_80611b1c + *(int *)((int)&DAT_805cad40 + idx)) != '\0') {
                visible = true;
                break;
            }
            idx += 4;
            remaining += -1;
        } while (remaining != 0);
    }
    if (!visible) {
        FUN_80382718();
        return;
    }

    view = scene_node_get_render_data(0x17);
    collision_set_layer_mask(*(int **)(view + 4), 10);

    if (!((DAT_805cad74 != 0) && (FLOAT_80618a1c < DAT_805cad78))) {
        FUN_80382718();
        return;
    }
    entity = entity_handle_resolve(0x57525448);
    if (entity == 0) {
        FUN_80382718();
        return;
    }

    if (anim_is_clip_active(entity, 0x13e) == 0) {
        anim_play_scaled((double)FLOAT_80618a18, (double)FLOAT_80618a24);
    }

    strandOff = 0;
    for (strandIdx = 0; strandIdx < DAT_805cad9c; strandIdx++) {
        strand = (int *)(DAT_80612988 + strandOff);
        int stamp = webnet_strand_advance_trail_index((int)strand);
        if (*strand != 2) {
            // Compute a screen-facing basis from the strand velocity.
            ax = (float)((double)FLOAT_80618a24 * (double)(float)strand[0x12]
                       - (double)((double)FLOAT_80618a18 * (double)(float)strand[0x11]));
            ay = (float)((double)FLOAT_80618a18 * (double)(float)strand[0x10]
                       - (double)((double)FLOAT_80618a18 * (double)(float)strand[0x12]));
            az = (float)((double)FLOAT_80618a18 * (double)(float)strand[0x11]
                       - (double)((double)FLOAT_80618a24 * (double)(float)strand[0x10]));
            len2 = az * az + ax * ax + ay * ay;
            if (FLOAT_80618a28 < len2) {
                inv = 1.0f / SQRT(len2);
                inv = inv * -(FLOAT_80618a1c * len2 * inv * inv - FLOAT_80618a2c);
                scl = (float)((double)FLOAT_80618a24 * (double)(inv * -(FLOAT_80618a1c * len2 * inv * inv - FLOAT_80618a2c)));
                ax = ax * scl;
                ay = ay * scl;
                az = az * scl;
            }
            fx = (float)strand[0x10];
            fy = (float)strand[0x11];
            fz = (float)strand[0x12];
            len2 = fz * fz + fx * fx + fy * fy;
            if (FLOAT_80618a28 < len2) {
                inv = 1.0f / SQRT(len2);
                inv = inv * -(FLOAT_80618a1c * len2 * inv * inv - FLOAT_80618a2c);
                scl = FLOAT_80618a24 * inv * -(FLOAT_80618a1c * len2 * inv * inv - FLOAT_80618a2c);
                fx = fx * scl;
                fy = fy * scl;
                fz = fz * scl;
            }
            mtx4_basis_from_fwd_ref((float *)(entity + 0x274), &ax, &fx);

            if (*strand == 1) {
                lenScale = (double)((float)strand[0x1a] * FLOAT_80618a30);
            } else {
                lenScale = (double)FLOAT_80618a24;
            }
            double sc = (double)(float)((double)FLOAT_80618a34 * lenScale);
            mtx4_scale_axes(sc, sc, sc, (float *)(entity + 0x274));

            *(int *)(entity + 100)  = strand[7];
            *(int *)(entity + 0x68) = strand[8];
            *(int *)(entity + 0x6c) = strand[9];

            double fadeLen = (double)(float)strand[0x19];
            if ((double)FLOAT_80618a18 < fadeLen) {
                if (fadeLen < (double)FLOAT_80618a24) {
                    gx_set_state_c0((int)((double)FLOAT_80618a38 * fadeLen) & 0xff);
                    collision_set_layer_mask(*(int **)(entity + 4), 5);
                }
                gx_set_sub_material_id(1);
                *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200001;
                *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) & 0xffffefff;
                *(short *)(entity + 0x14) = 0;
                gx_set_blend_mode(4);
                drawRet = (double)render_instance_draw_pass(0.0, sc, *(int *)(entity + 4), 0, 0);
                gx_set_blend_mode(10);
                drawRet = (double)render_instance_draw_pass(drawRet, sc, *(int *)(entity + 4), 2, 0);
                gx_set_blend_mode(2);
                render_instance_draw_pass(drawRet, sc, *(int *)(entity + 4), 1, 0);
                *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) & 0xffdffffe;
                if ((float)strand[0x19] < FLOAT_80618a24) {
                    collision_set_layer_mask(*(int **)(entity + 4), 0);
                    *(unsigned int *)(entity + 0x18) = *(unsigned int *)(entity + 0x18) & 0xfffffffb;
                    *(unsigned char *)(*(int *)(entity + 4) + 0x1f0) = 0xff;
                }
            }

            // Trail sample: view-facing alpha ramp.
            float facing = -(fz * *(float *)(DAT_80611d54 + 0x33c)
                           + fx * *(float *)(DAT_80611d54 + 0x334)
                           + fy * *(float *)(DAT_80611d54 + 0x338));
            float clampF = FLOAT_80618a18;
            if (FLOAT_80618a18 < facing) {
                clampF = facing;
            }
            alpha = (unsigned int)(FLOAT_80618a38 * clampF);

            if ((alpha & 0xff) == 0) {
                if (stamp != 0) {
                    ring = (unsigned int)*(unsigned short *)((int)strand + 0x138); // strand[0x4e]
                    strand[ring * 3 + 0x1c] = (int)FLOAT_80618a18;
                    strand[ring * 3 + 0x1d] = (int)FLOAT_80618a18;
                    strand[ring * 3 + 0x1e] = (int)FLOAT_80618a18;
                    strand[ring * 3 + 0x34] = (int)FLOAT_80618a18;
                    strand[ring * 3 + 0x35] = (int)FLOAT_80618a18;
                    strand[ring * 3 + 0x36] = (int)FLOAT_80618a18;
                    *(char *)((int)strand + ring + 0x130) = (char)alpha;
                    *(short *)((int)strand + 0x138) = *(short *)((int)strand + 0x138) + 1;
                    if (7 < *(unsigned short *)((int)strand + 0x138)) {
                        *(short *)((int)strand + 0x138) = 0;
                        *(unsigned char *)((int)strand + 0x13a) = 1;
                    }
                    strand[0x4f] = DAT_80611928;
                }
            } else {
                gx_set_blend_mode(2);
                gx_set_sub_material_id(2);
                tex = sprite_get_texture_id(0x292);
                gx_begin_projected_batch(1.0, (double)(FLOAT_80618a3c * *(float *)(DAT_80611d54 + 0x268)),
                                         (double)FLOAT_80618a24, (double)FLOAT_80618a40, 4, tex);
                node = *(int **)(*(int *)(entity + 4) + 4);
                vtx = (float *)node[*node * -0x30 + 0x1b];
                e0 = vtx[0]; e1 = vtx[1]; e2 = vtx[2];
                mtx4_transform_vector((float *)(entity + 0x274), &e0);
                e0 = e0 + *(float *)(entity + 100);
                e1 = e1 + *(float *)(entity + 0x68);
                e2 = e2 + *(float *)(entity + 0x6c);
                if (stamp != 0) {
                    ring = (unsigned int)*(unsigned short *)((int)strand + 0x138);
                    strand[ring * 3 + 0x1c] = (int)e0;
                    strand[ring * 3 + 0x1d] = (int)e1;
                    strand[ring * 3 + 0x1e] = (int)e2;
                }
                float colA = (float)(alpha << 0x18 | 0x7f);
                gx_projtex_add_point((double)e0, (double)e1, (double)e2,
                                     (double)(FLOAT_80618a44 * *(float *)(DAT_80611d54 + 0x268)),
                                     (double)FLOAT_80618a18, (double)FLOAT_80618a18, colA);
                float colB = (float)(alpha << 0x18 | 0x4f4f4f);
                gx_projtex_add_point((double)e0, (double)e1, (double)e2,
                                     (double)(FLOAT_80618a48 * *(float *)(DAT_80611d54 + 0x268)),
                                     (double)FLOAT_80618a18, (double)FLOAT_80618a18, colB);
                {
                    int *node2 = *(int **)(*(int *)(entity + 4) + 4);
                    int base = node2[*node2 * -0x30 + 0x1b];
                    e0 = *(float *)(base + 0x18);
                    e1 = *(float *)(base + 0x1c);
                    e2 = *(float *)(base + 0x20);
                }
                mtx4_transform_vector((float *)(entity + 0x274), &e0);
                e0 = e0 + *(float *)(entity + 100);
                e1 = e1 + *(float *)(entity + 0x68);
                e2 = e2 + *(float *)(entity + 0x6c);
                if (stamp != 0) {
                    ring = (unsigned int)*(unsigned short *)((int)strand + 0x138);
                    strand[ring * 3 + 0x34] = (int)e0;
                    strand[ring * 3 + 0x35] = (int)e1;
                    strand[ring * 3 + 0x36] = (int)e2;
                    *(char *)((int)strand + ring + 0x130) = (char)alpha;
                    *(short *)((int)strand + 0x138) = *(short *)((int)strand + 0x138) + 1;
                    if (7 < *(unsigned short *)((int)strand + 0x138)) {
                        *(short *)((int)strand + 0x138) = 0;
                        *(unsigned char *)((int)strand + 0x13a) = 1;
                    }
                    strand[0x4f] = DAT_80611928;
                }
                gx_projtex_add_point((double)e0, (double)e1, (double)e2,
                                     (double)(FLOAT_80618a44 * *(float *)(DAT_80611d54 + 0x268)),
                                     (double)FLOAT_80618a18, (double)FLOAT_80618a18, colA);
                gx_projtex_add_point((double)e0, (double)e1, (double)e2,
                                     (double)(FLOAT_80618a48 * *(float *)(DAT_80611d54 + 0x268)),
                                     (double)FLOAT_80618a18, (double)FLOAT_80618a18, colB);
                gx_end_batch();
            }

            // Draw the accumulated trail ribbon.
            if (*(char *)((int)strand + 0x13a) == '\0') {
                trailN = (unsigned int)*(unsigned short *)((int)strand + 0x138);
                ring = 0;
            } else {
                trailN = 8;
                ring = (unsigned int)*(unsigned short *)((int)strand + 0x138);
            }
            if (stamp != 0) {
                trailN = trailN - 1;
            }
            if (0 < (int)trailN) {
                tex = sprite_get_texture_id(0x292);
                gx_begin_projected_batch(1.0, (double)(FLOAT_80618a3c * *(float *)(DAT_80611d54 + 0x268)),
                                         (double)FLOAT_80618a24, (double)FLOAT_80618a40,
                                         (int)(trailN << 1), tex);
                for (unsigned int t = 0; t < trailN; t++) {
                    double frac = (double)FLOAT_80618a34 * (double)(int)(t + 1)
                                / (double)(int)(trailN + 1);
                    unsigned int ta = (unsigned int)*(unsigned char *)((int)strand + ring + 0x130);
                    int c0 = FUN_803825c0((double)(float)(frac * (double)(int)ta));
                    gx_projtex_add_point((double)(float)strand[ring * 3 + 0x1c],
                                         (double)(float)strand[ring * 3 + 0x1d],
                                         (double)(float)strand[ring * 3 + 0x1e],
                                         (double)(float)(FLOAT_80618a44 * *(float *)(DAT_80611d54 + 0x268)),
                                         (double)FLOAT_80618a18, (double)FLOAT_80618a18,
                                         (float)(c0 << 0x18 | 0x7f));
                    int c1 = FUN_803825c0((double)(float)(frac * (double)(int)ta));
                    gx_projtex_add_point((double)(float)strand[ring * 3 + 0x34],
                                         (double)(float)strand[ring * 3 + 0x35],
                                         (double)(float)strand[ring * 3 + 0x36],
                                         (double)(float)(FLOAT_80618a44 * *(float *)(DAT_80611d54 + 0x268)),
                                         (double)FLOAT_80618a18, (double)FLOAT_80618a18,
                                         (float)(c1 << 0x18 | 0x7f));
                    ring = (ring + 1) & 7;
                }
                gx_end_batch();
            }
        }
        strandOff += 0x140;
    }
    (void)packHi;
    FUN_80382718();
}

// ============================================================================
// 0x80245588  webnet_node_in_active_list
// Returns 1 if the given scene node is in the fx node list.
// ============================================================================
int webnet_node_in_active_list(int node)
{
    int idx = 0;
    int remaining = DAT_805cad54;
    if (0 < DAT_805cad54) {
        do {
            if (node == *(int *)((int)&DAT_805cad40 + idx)) {
                return 1;
            }
            idx += 4;
            remaining += -1;
        } while (remaining != 0);
    }
    return 0;
}

// ============================================================================
// 0x802455c8  webnet_simulate_tick
// Master per-frame simulation of the web-net. Drives strand physics through
// the net phases (grow / attach / retract), tracks the guide target, integrates
// each strand, resolves swept collisions against rooms and the floor, and
// applies capture damage to nearby projectiles. Called from worm mission ticks.
// ============================================================================
void webnet_simulate_tick(void)
{
    bool  anyVisible;
    bool  hitFound;
    int   entity;
    int   idx, remaining;
    int   strandIdx, strandOff;
    int  *strand;
    int   target;
    int   pn, pcount;
    float bestDistSq;
    float dx, dy, dz, distSq;
    float radius;

    int   local_hit;
    float castDir[3];
    int   castFrom[3];
    int   castHitList;
    unsigned char castFlagA, castFlagB;
    float floorPos[3];
    float rotAxis[3];
    float mtx[16];
    int   dmgParams[16];
    float aabb[6];

    FUN_803826c8();
    entity = entity_handle_resolve(0x57525448);
    anyVisible = false;

    if (entity == 0) {
        if (DAT_805cad74 != 0) {
            idx = 0;
            DAT_805cad74 = 0;
            for (strandIdx = 0; strandIdx < DAT_805cad9c; strandIdx++) {
                int *rec = (int *)(DAT_80612988 + idx);
                *rec = 2;
                int em = rec[0x1b];
                particle_emitter_set_flags(&em, 1, 0);
                em = rec[0x1b];
                particle_cached_set_drag((double)FLOAT_80618a18, &em);
                idx += 0x140;
            }
        }
        FUN_80382714();
        return;
    }

    idx = 0;
    remaining = DAT_805cad54;
    if (0 < DAT_805cad54) {
        do {
            if (*(char *)(DAT_80611b1c + *(int *)((int)&DAT_805cad40 + idx)) != '\0') {
                anyVisible = true;
                break;
            }
            idx += 4;
            remaining += -1;
        } while (remaining != 0);
    }

    if (!anyVisible || DAT_805cad74 == 0) {
        FUN_80382714();
        return;
    }

    hitFound = true;
    target = aiactor_find_player_by_team(0x56444d4e);
    if ((double)FLOAT_80618a18 < (double)DAT_805cad8c) {
        DAT_805cad8c = -(float)((double)(FLOAT_80618a24 / (float)(double)(int)DAT_806109c0)
                              * (double)FLOAT_806108ec - (double)DAT_805cad8c);
    }
    if (target != 0) {
        DAT_805cad7c = *(float *)(target + 100);
        DAT_805cad80 = *(float *)(target + 0x68);
        DAT_805cad84 = *(float *)(target + 0x6c);
        DAT_805cad88 = 1;
    }

    if (DAT_805cad88 == 0) {
        bool captured = false;
        idx = 0;
        for (pn = 0; pn < DAT_80612610; pn++) {
            if (webnet_node_in_active_list(*(int *)(DAT_80611d78 + idx + 0xdec)) != 0) {
                captured = true;
                break;
            }
            idx += 0x141c;
        }
        if (DAT_80612614 == 1) {
            if (((target == 0) || (webnet_node_in_active_list(*(int *)(target + 0x34)) == 0)) && (!captured)) {
                target = aiactor_find_player_by_team(0x564e4d4e);
                hitFound = false;
            }
            if ((target == 0) || (webnet_node_in_active_list(*(int *)(target + 0x34)) == 0)) {
                target = *(int *)(DAT_80611d78 + 0x14);
            }
            if ((target == 0) || (webnet_node_in_active_list(*(int *)(target + 0x34)) == 0)) {
                target = aiactor_find_player_by_team(0x4a425448);
            }
            if (target == 0) goto done;
        } else {
            target = *(int *)(DAT_80611d78 + 0x14);
        }
    }

    // Integrate every live strand.
    strandOff = 0;
    for (strandIdx = 0; strandIdx < DAT_805cad9c; strandIdx++) {
        strand = (int *)(DAT_80612988 + strandOff);
        if (*strand != 2) {
            webnet_strand_build_swept_aabb((int)strand);
            webnet_strand_pick_anchor_node((int)strand, entity);
            scene_get_node_aabb(strand[6], aabb + 3, aabb);

            bestDistSq = FLOAT_80618a60;
            if (((target != 0) && (*(int *)(target + 0x150) != 0)) && (0 < DAT_80612610)) {
                int pi = 0;
                pcount = DAT_80612610;
                do {
                    int prec = DAT_80611d78 + pi;
                    float px = *(float *)(prec + 0xa8);
                    dy = *(float *)(prec + 0xac) - (float)strand[8];
                    dz = *(float *)(prec + 0xb0) - (float)strand[9];
                    distSq = dz * dz
                           + (px - (float)strand[7]) * (px - (float)strand[7])
                           + dy * dy;
                    if ((strand[6] == *(int *)(prec + 0xdec))
                        && ((double)px < (double)FLOAT_80618a64)
                        && (distSq < bestDistSq)) {
                        target = *(int *)(prec + 0x14);
                        bestDistSq = distSq;
                    }
                    pi += 0x141c;
                    pcount += -1;
                } while (pcount != 0);
            }

            float dt = FLOAT_806108ec / (float)(double)(int)DAT_806109c0;
            if (DAT_805cad74 == 1) {
                // Grow phase: accelerate outward from the WRTH.
                DAT_805cad78 = (float)((double)DAT_805cad78
                             + (double)(dt * (float)((double)(int)DAT_806108e4)
                                        / (float)((double)(int)DAT_805cad9c)));
                if (FLOAT_80618a1c < DAT_805cad78) {
                    float gscale = (*strand == 1) ? FLOAT_80618a74 : FLOAT_80618a24;
                    strand[7] = (int)((float)strand[0x10]
                                * (FLOAT_80618a6c * (float)strand[8] * FLOAT_80618a30 + FLOAT_80618a68)
                                * (float)((double)(int)DAT_806108e4) * dt * gscale
                                + (float)strand[7]);
                    strand[8] = (int)((float)strand[0x11]
                                * (FLOAT_80618a6c * (float)strand[8] * FLOAT_80618a30 + FLOAT_80618a68)
                                * (float)((double)(int)DAT_806108e4) * dt * gscale
                                + (float)strand[8]);
                    strand[9] = (int)((float)strand[0x12]
                                * (FLOAT_80618a68 + (float)strand[8] * FLOAT_80618a30)
                                * (float)((double)(int)DAT_806108e4) * dt * gscale
                                + (float)strand[9]);
                    if (FLOAT_80618a74 < (float)strand[8]) {
                        DAT_805cad74 = 2;
                    }
                    strand[0x19] = (int)((float)strand[8] * FLOAT_80618a30);
                }
            } else if (DAT_805cad74 == 3) {
                // Retract phase: pull velocity toward zero and invert lateral.
                float k = FLOAT_80618a2c * (FLOAT_80618a24 / (float)(double)(int)DAT_806109c0)
                        * (float)((double)(int)DAT_806108e4);
                strand[7] = (int)((float)strand[0x10] * k + (float)strand[7]);
                strand[8] = (int)((float)strand[0x11] * k + (float)strand[8]);
                strand[9] = (int)((float)strand[0x12] * k + (float)strand[9]);
                float ki = FLOAT_80618a1c * (FLOAT_80618a24 / (float)(double)(int)DAT_806109c0)
                         * (float)((double)(int)DAT_806108e4);
                strand[0x11] = (int)-(ki - (float)strand[0x11]);
            } else {
                // Attach phase: spring toward the moving attach centre.
                float radial = ((float)strand[9] - DAT_805cad60) * ((float)strand[9] - DAT_805cad60)
                             + ((float)strand[7] - DAT_805cad58) * ((float)strand[7] - DAT_805cad58)
                             + ((float)strand[8] - DAT_805cad5c) * ((float)strand[8] - DAT_805cad5c);
                float pull = FLOAT_80618a24;
                if (radial < FLOAT_80618a78) {
                    pull = FLOAT_80618a1c * ((FLOAT_80618a78 - radial) / FLOAT_80618a78) + FLOAT_80618a24;
                }
                float step = (float)((double)(int)DAT_806108e4) * pull
                           * (FLOAT_80618a24 / (float)(double)(int)DAT_806109c0);
                strand[7] = (int)((float)strand[0x10] * step + (float)strand[7]);
                strand[8] = (int)((float)strand[0x11] * step + (float)strand[8]);
                strand[9] = (int)((float)strand[0x12] * step + (float)strand[9]);
                strand[0x11] = (int)-(float)(FLOAT_80618a1c
                              * (FLOAT_80618a24 / (float)(double)(int)DAT_806109c0)
                              * (float)((double)(int)DAT_806108e4) - (float)strand[0x11]);
            }

            // Clamp the strand endpoints to the anchor node AABB (+radius).
            radius = FLOAT_80618a44;
            {
                int off = 0;
                int cnt = 3;
                do {
                    float hi = radius + *(float *)((int)aabb + off + 0xc);
                    if (*(float *)((int)strand + off + 0x1c) < hi) {
                        *(float *)((int)strand + off + 0x1c) = hi;
                        *(float *)((int)strand + off + 0x40) = ABS(*(float *)((int)strand + off + 0x40));
                    }
                    float lo = *(float *)((int)aabb + off) - radius;
                    if (lo < *(float *)((int)strand + off + 0x1c)) {
                        *(float *)((int)strand + off + 0x1c) = lo;
                        *(float *)((int)strand + off + 0x40) = -ABS(*(float *)((int)strand + off + 0x40));
                    }
                    off += 4;
                    cnt += -1;
                } while (cnt != 0);
            }

            if ((DAT_805cad74 == 2) || (DAT_805cad74 == 3)) {
                float sx = (float)strand[7];
                float sy = (float)strand[8];
                float sz = (float)strand[9];
                if (FLOAT_80618a18 <
                    (sz - DAT_805cad60) * (sz - DAT_805cad60)
                    + (sx - DAT_805cad58) * (sx - DAT_805cad58)
                    + (sy - DAT_805cad5c) * (sy - DAT_805cad5c)) {
                    float tx = DAT_805cad7c, ty = DAT_805cad80, tz = DAT_805cad84;
                    if (DAT_805cad88 == 0) {
                        if (*strand == 1) {
                            tx = FLOAT_80618a34 + FLOAT_80618a7c *
                                 ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c) * FLOAT_80618a84;
                            ty = FLOAT_80618a88 + FLOAT_80618a7c *
                                 ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c) * FLOAT_80618a84;
                            tz = FLOAT_80618a8c + FLOAT_80618a7c *
                                 ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c) * FLOAT_80618a84;
                        } else {
                            tx = *(float *)(target + 100);
                            ty = *(float *)(target + 0x68) + FLOAT_80618a2c;
                            tz = *(float *)(target + 0x6c);
                            if (FLOAT_80618a18 < DAT_805cad8c) {
                                float gx = DAT_805cad90 - sx;
                                float gy = DAT_805cad94 - sy;
                                float gz = DAT_805cad98 - sz;
                                double d2 = (double)(gz * gz + gx * gx + gy * gy);
                                if (d2 < (double)FLOAT_80618a70) {
                                    double dsx = (double)(sx - DAT_805cad90);
                                    double dsy = (double)(sy - DAT_805cad94);
                                    double dsz = (double)(sz - DAT_805cad98);
                                    double inv = math_sqrt(d2);
                                    inv = (double)(float)((double)FLOAT_80618a90 / inv);
                                    tx = (float)(dsx * inv) + DAT_805cad90;
                                    ty = (float)(dsy * inv) + DAT_805cad94;
                                    tz = (float)(dsz * inv) + DAT_805cad98;
                                }
                            }
                        }
                    }
                    float ddz = (float)strand[9] - tz;
                    bool isFar = FLOAT_80618a94 <=
                               ddz * ddz
                               + ((float)strand[7] - tx) * ((float)strand[7] - tx)
                               + ((float)strand[8] - ty) * ((float)strand[8] - ty);
                    float phaseStep = (float)(FLOAT_80618a24 / (float)(double)(int)DAT_806109c0) * FLOAT_806108ec;
                    float spinBase = (*strand == 1) ? FLOAT_80618a74 : FLOAT_80618a24;
                    strand[0x18] = (int)-(float)(phaseStep * spinBase - (float)strand[0x18]);

                    if (((float)strand[0x18] <= FLOAT_80618a18)
                        || (((!isFar && (FLOAT_80618a30 < (float)strand[0x18])) || (DAT_805cad74 == 3)))) {
                        float ex = tx - (float)strand[7];
                        float ey = ty - (float)strand[8];
                        float ez = tz - (float)strand[9];
                        rotAxis[0] = (float)((double)ey * (double)(float)strand[0x12]
                                           - (double)((double)ez * (double)(float)strand[0x11]));
                        rotAxis[1] = (float)((double)ez * (double)(float)strand[0x10]
                                           - (double)((double)ex * (double)(float)strand[0x12]));
                        rotAxis[2] = (float)((double)ex * (double)(float)strand[0x11]
                                           - (double)((double)ey * (double)(float)strand[0x10]));
                        float m2 = rotAxis[2] * rotAxis[2] + rotAxis[0] * rotAxis[0] + rotAxis[1] * rotAxis[1];
                        if (FLOAT_80618a28 < m2) {
                            float inv = 1.0f / SQRT(m2);
                            float k = FLOAT_80618a1c * m2;
                            inv = inv * -(float)((double)k * (double)(inv * inv) - (double)FLOAT_80618a2c);
                            float k2 = inv * -(float)((double)k * (double)(inv * inv) - (double)FLOAT_80618a2c);
                            float sc = (float)((double)FLOAT_80618a24 * (double)k2);
                            rotAxis[0] = rotAxis[0] * sc;
                            rotAxis[1] = rotAxis[1] * sc;
                            rotAxis[2] = rotAxis[2] * sc;
                        }
                        strand[0x15] = (int)rotAxis[0];
                        strand[0x16] = (int)rotAxis[1];
                        strand[0x17] = (int)rotAxis[2];
                        double ang = (double)((float)((double)(int)DAT_806108e4)
                                   * (float)strand[0x14]
                                   * (FLOAT_80618a24 / (float)(double)(int)DAT_806109c0));
                        mtx4_set_axis_angle(ang, mtx, rotAxis);
                        if (isFar) {
                            strand[0x18] = (int)(FLOAT_80618a1c
                                * ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80)
                                + FLOAT_80618a30);
                        } else {
                            strand[0x18] = (int)(FLOAT_80618a30
                                * ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80)
                                * FLOAT_80618a1c + FLOAT_80618a98);
                        }
                    } else {
                        float sc = (*strand == 1) ? FLOAT_80618a74 : FLOAT_80618a24;
                        double ang = (double)((float)((double)(int)DAT_806108e4)
                                   * (FLOAT_80618a24 / (float)(double)(int)DAT_806109c0)
                                   * (float)strand[0x14] * sc);
                        mtx4_set_axis_angle(ang, mtx, (float *)(strand + 0x15));
                    }
                    mtx4_transform_vector(mtx, (float *)(strand + 0x10));
                }
            }

            if ((DAT_805cad74 == 2) || (DAT_805cad74 == 3)) {
                double lenPrev = (double)(float)strand[0x19];
                castDir[0] = DAT_803e0240;
                castDir[1] = DAT_803e0244;
                castDir[2] = DAT_803e0248;
                local_hit = 0;
                castFlagA = 1;
                castFlagB = 0;
                subobj_constraint_pool_reset();
                strand[0x19] = (int)FLOAT_80618a24;
                int hit = entity_swept_cast_room(strand[6], strand + 7, castDir, 0, &local_hit, 0xfffffff9, 0);
                if (hit != 0) {
                    for (int it = local_hit; it != 0; it = *(int *)(it + 0x34)) {
                        float lim = (float)strand[0x19];
                        float cand = -(ABS(*(float *)(it + 0x18)) * FLOAT_80618a1c - FLOAT_80618a24);
                        if (cand < lim) lim = cand;
                        strand[0x19] = (int)lim;
                    }
                }
                castFrom[0] = strand[7];
                *(float *)(castFrom + 1) = (float)strand[8] + FLOAT_80618a24;
                castFrom[2] = strand[9];
                double floorY = floor_query_with_interact((double)FLOAT_80618a9c, (double)FLOAT_80618a7c,
                                                          castFrom, 0, (int *)0, 1);
                float df = ABS((float)(floorY - (double)(float)strand[8]));
                if (df < FLOAT_80618a24) {
                    float lim = (float)strand[0x19];
                    if (df < lim) lim = df;
                    strand[0x19] = (int)lim;
                }
                float ln = (float)strand[0x19];
                float lc = ln;
                if (FLOAT_80618a24 < ln) lc = FLOAT_80618a24;
                float lo = FLOAT_80618a18;
                if ((FLOAT_80618a18 <= lc) && (lo = ln, FLOAT_80618a24 < ln)) lo = FLOAT_80618a24;
                strand[0x19] = (int)lo;
                strand[0x19] = (int)(FLOAT_80618a44 * (float)strand[0x19]
                                    + (float)(FLOAT_80618aa0 * lenPrev));
                (void)floorPos;
            }
        }
        strandOff += 0x140;
    }

    // Post-integration: move the attach centre and apply capture damage.
    if ((DAT_805cad74 == 2) || (DAT_805cad74 == 3)) {
        if (target == 0) {
            if (DAT_805cad88 != 0) {
                DAT_805cad58 = DAT_805cad7c;
                DAT_805cad5c = DAT_805cad80;
                DAT_805cad60 = DAT_805cad84;
            }
        } else {
            if (!hitFound) {
                if (DAT_80612614 == 1) {
                    statlvl_class_set_state2(7);
                }
                strandOff = 0;
                for (strandIdx = 0; strandIdx < DAT_805cad9c; strandIdx++) {
                    int rec = DAT_80612988 + strandOff;
                    dx = *(float *)(rec + 0x1c) - *(float *)(target + 100);
                    dz = *(float *)(rec + 0x24) - *(float *)(target + 0x6c);
                    if ((double)(dx * dx + dz * dz) < (double)FLOAT_80618aa4) {
                        float nrm = DAT_803e0254 * *(float *)(rec + 0x48)
                                  + DAT_803e024c * *(float *)(rec + 0x40)
                                  + DAT_803e0250 * *(float *)(rec + 0x44);
                        if (nrm <= FLOAT_80618a18) {
                            mtx4_set_rotation_y((double)FLOAT_80618aa8, aabb + 0 /* reuse scratch */);
                            mtx4_transform_vector(aabb, (float *)(DAT_80612988 + strandOff + 0x40));
                        }
                        if (DAT_80612614 == 1) {
                            statlvl_class_complete((double)FLOAT_80618a18, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
                        }
                    }
                    strandOff += 0x140;
                }
            }
            DAT_805cad58 = *(float *)(target + 100);
            DAT_805cad5c = *(float *)(target + 0x68) + FLOAT_80618a2c;
            DAT_805cad60 = *(float *)(target + 0x6c);
        }

        float drift = FLOAT_80618a74 * (FLOAT_80618a24 / (float)(double)(int)DAT_806109c0)
                    * (float)((double)(int)DAT_806108e4);
        DAT_805cad58 = DAT_805cad64 * drift + DAT_805cad58;
        DAT_805cad5c = DAT_805cad68 * drift + DAT_805cad5c;
        DAT_805cad60 = DAT_805cad6c * drift + DAT_805cad60;

        if (hitFound) {
            // Deal capture damage to nearby active projectiles.
            int pi = 0;
            for (int pj = 0; pj < DAT_80612098; pj++) {
                int *proj = *(int **)((int)&DAT_80588610 + pi);
                if (((proj[9] & 8U) == 0) && (proj[0] != 0)) {
                    int origin = proj[0];
                    float ox = *(float *)(origin + 100);
                    float oy = *(float *)(origin + 0x68) + FLOAT_80618a2c;
                    float oz = *(float *)(origin + 0x6c);
                    int si = 0;
                    for (int sj = 0; sj < DAT_805cad9c; sj++) {
                        int *s = (int *)(DAT_80612988 + si);
                        if (*s != 2) {
                            bool inList = false;
                            int q = 0;
                            int qr = DAT_805cad54;
                            if (0 < DAT_805cad54) {
                                do {
                                    if (*(int *)(proj[0] + 0x34) == *(int *)((int)&DAT_805cad40 + q)) {
                                        inList = true;
                                        break;
                                    }
                                    q += 4;
                                    qr += -1;
                                } while (qr != 0);
                            }
                            if (inList && (*(int *)(proj[0] + 0x60) != 0x56444d4e)
                                && ((oz - (float)s[9]) * (oz - (float)s[9])
                                    + (ox - (float)s[7]) * (ox - (float)s[7])
                                    + (oy - (float)s[8]) * (oy - (float)s[8]) < FLOAT_80618a24)
                                && ((((DAT_805cada0 & 2) != 0 && (proj != (int *)0)
                                      && (proj[2] < DAT_80612610)) || ((DAT_805cada0 & 2) == 0)))) {
                                float imp[3];
                                imp[0] = FLOAT_80618a7c *
                                    ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
                                imp[1] = FLOAT_80618a7c *
                                    ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
                                imp[2] = FLOAT_80618a7c *
                                    ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
                                float m2 = imp[2] * imp[2]
                                         + imp[0] * imp[0] + imp[1] * imp[1];
                                if (FLOAT_80618a28 < m2) {
                                    float inv = 1.0f / SQRT(m2);
                                    float k = FLOAT_80618a1c * m2;
                                    inv = inv * -(float)((double)k * (double)(inv * inv) - (double)FLOAT_80618a2c);
                                    float k2 = inv * -(float)((double)k * (double)(inv * inv) - (double)FLOAT_80618a2c);
                                    float sc = (float)((double)FLOAT_80618a24 * (double)k2);
                                    imp[0] = imp[0] * sc;
                                    imp[1] = imp[1] * sc;
                                    imp[2] = imp[2] * sc;
                                }
                                int dctx = switch_slot_reset((int)dmgParams);
                                dmgParams[0] = proj[0];
                                dmgParams[6] = 2;   // local_118
                                dmgParams[7] = 2;   // local_114
                                *(float *)&dmgParams[9] = FLOAT_80618aac; // local_10c
                                *(void **)&dmgParams[11] = (void *)&ox;   // local_104
                                *(void **)&dmgParams[13] = (void *)imp;   // local_fc
                                damage_dispatch_to_entity(dctx, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, dmgParams);
                            }
                        }
                        si += 0x140;
                    }
                }
                pi += 4;
            }
        }
    }

    // Trail centroid + 3D loop-sound update.
    {
        float cx = DAT_803e0258, cy = DAT_803e025c, cz = DAT_803e0260;
        double total = (double)FLOAT_80618a18;
        int off = 0;
        for (int i = 0; i < DAT_805cad9c; i++) {
            int *s = (int *)(DAT_80612988 + off);
            int st = *s;
            if (st == 1) {
                s[0x1a] = (int)-((FLOAT_80618a24 / (float)(double)(int)DAT_806109c0) * FLOAT_806108ec
                                - (float)s[0x1a]);
                if (FLOAT_80618a7c <= (float)s[0x1a]) {
                    int em = s[0x1b];
                    double sz = (double)((float)s[0x1a] * FLOAT_80618a30);
                    particle_cached_set_position(&em, s + 7);
                    em = s[0x1b];
                    particle_cached_set_size_range(sz, sz, &em);
                    total = (double)(float)(total + sz);
                    cx = (float)((double)(float)s[7] * sz + (double)cx);
                    cy = (float)((double)(float)s[8] * sz + (double)cy);
                    cz = (float)((double)(float)s[9] * sz + (double)cz);
                } else {
                    *s = 2;
                    int em = s[0x1b];
                    particle_emitter_set_flags(&em, 1, 0);
                    em = s[0x1b];
                    particle_cached_set_drag((double)FLOAT_80618a18, &em);
                    double one = (double)FLOAT_80618a24;
                    FUN_8037dc94(one, one, one, (int *)0, 0x200, (float *)(s + 7), (float *)(s + 0x10),
                                 (int *)0, 0, 0, 0);
                }
            } else if ((st < 1) && (-1 < st)) {
                cx = cx + (float)s[7];
                total = (double)(float)(total + (double)FLOAT_80618a24);
                cy = cy + (float)s[8];
                cz = cz + (float)s[9];
            }
            off += 0x140;
        }
        if ((double)FLOAT_80618aac < total) {
            float inv = (float)((double)FLOAT_80618a24 / total);
            cx = cx * inv; cy = cy * inv; cz = cz * inv;
            DAT_805cadac = FLOAT_80618a44 * (cx - DAT_805cadac) + DAT_805cadac;
            DAT_805cadb0 = FLOAT_80618a44 * (cy - DAT_805cadb0) + DAT_805cadb0;
            DAT_805cadb4 = FLOAT_80618a44 * (cz - DAT_805cadb4) + DAT_805cadb4;
        }
        if (DAT_805cad74 != 0) {
            double loud = total;
            if ((double)FLOAT_80618a84 < total) loud = (double)FLOAT_80618a84;
            if ((double)FLOAT_80618a18 <= loud) {
                if ((double)FLOAT_80618a84 < total) total = (double)FLOAT_80618a84;
            } else {
                total = (double)FLOAT_80618a18;
            }
            scalar_step_toward_snap((double)(float)((double)FLOAT_80618a1c * total),
                                    (double)FLOAT_80618aac, (int *)&DAT_805cada8);
            sound_update_3d_source((double)DAT_805cada8, (double)FLOAT_80618a78, (double)FLOAT_80618a24,
                                   &DAT_805cada4, (int *)&DAT_805cadac, 0x14e, 0);
        }
    }

done:
    FUN_80382714();
}

// ============================================================================
// 0x80246cc8  webnet_scatter_strands
// Initialise every strand with a randomised spread position/velocity around a
// base point and enter the retract phase (net phase 3).
// ============================================================================
void webnet_scatter_strands(void)
{
    float baseX = DAT_803e0264;
    int   off = 0;

    __save_gpr_28();
    for (int i = 0; i < DAT_805cad9c; i++) {
        float rx = FLOAT_80618a7c * ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
        float ry = FLOAT_80618a7c * ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
        float rz = FLOAT_80618a7c * ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
        *(float *)(DAT_80612988 + off + 0x1c) = (float)((double)DAT_803e0264 + (double)(rx * FLOAT_80618a1c));
        *(float *)(DAT_80612988 + off + 0x20) = (float)((double)DAT_803e0268 + (double)(ry * FLOAT_80618a1c));
        *(float *)(DAT_80612988 + off + 0x24) = (float)((double)DAT_803e026c + (double)(rz * FLOAT_80618a1c));
        *(float *)(DAT_80612988 + off + 0x40) = FLOAT_80618ab0;
        *(float *)(DAT_80612988 + off + 0x44) = FLOAT_80618a18;
        *(float *)(DAT_80612988 + off + 0x48) = FLOAT_80618a18;
        float sp = FLOAT_80618a30 * ((float)(double)(int)rng_counter_tick() / FLOAT_80618a80) + FLOAT_80618a24;
        *(float *)(DAT_80612988 + off + 0x50) = sp;
        off += 0x140;
    }
    DAT_805cad58 = baseX;
    DAT_805cad5c = DAT_803e0268;
    DAT_805cad60 = DAT_803e026c;
    DAT_805cad74 = 3;
    __restore_gpr_28();
}

// ============================================================================
// 0x80246f44  webnet_capture_check_swept
// Given a swept segment (from the WRTH work-context), start the net effect on
// any strand whose bounds intersect the segment; when all strands are engaged,
// report mission completion.
// ============================================================================
void webnet_capture_check_swept(unsigned long long param_1, double param_2, double param_3,
                                double param_4, double param_5, double param_6,
                                double param_7, double param_8)
{
    float *from;
    int    engaged = 0;
    int    off;
    int    hit;
    float  dir[3];
    int    play[8];
    int    poolRec;
    int    poolX, poolY, poolZ;
    unsigned char poolFlag;
    int    bounds[23];

    unsigned long long uv = FUN_803826e8();
    from = (float *)(unsigned int)(uv >> 0x20);
    dir[0] = DAT_803e0270;
    *(int *)&dir[1] = (int)DAT_803e0274;
    *(int *)&dir[2] = (int)DAT_803e0278;

    if (DAT_805cad74 != 0) {
        DAT_805cad8c = FLOAT_80618a24;
        DAT_805cad90 = from[0];
        DAT_805cad94 = from[1];
        DAT_805cad98 = from[2];
        build_swept_bounds((double)FLOAT_80618a1c, bounds, from, (float *)(unsigned int)uv);
        off = 0;
        for (int i = 0; i < DAT_805cad9c; i++) {
            int *strand = (int *)(DAT_80612988 + off);
            if ((*strand != 2) && (*strand != 1)
                && (aabb_vs_entity_bounds((int)bounds, (float *)(strand + 10)) != 0)) {
                sound_init_play_params((int)play);
                poolFlag = 1;
                play[0] = 0x181;
                poolX = strand[7];
                poolY = strand[8];
                poolZ = strand[9];
                play[5] = poolX; play[6] = poolY; play[7] = poolZ;
                *(unsigned char *)((int)play + 0x21) = poolFlag;
                sound_play_request(play);
                double one = (double)FLOAT_80618a24;
                FUN_8037dc94(one, (double)FLOAT_80618a7c, one, strand + 0x1b, 0x100,
                             (float *)(strand + 7), dir, (int *)0, 0, 0, 0);
                *strand = 1;
            }
            if ((*strand == 1) || (*strand == 2)) {
                engaged++;
            }
            off += 0x140;
        }
        if (engaged == DAT_805cad9c) {
            statlvl_class_complete((double)FLOAT_80618a18, param_2, param_3, param_4,
                                   param_5, param_6, param_7, param_8);
        }
    }
    (void)poolRec;
    FUN_80382734();
}

// ============================================================================
// 0x802470d4  webnet_count_free_strands
// Returns the number of strands not yet engaged (state != 1 and != 2), or 0 if
// the net isn't currently owned (FUN_8024786c gate).
// ============================================================================
int webnet_count_free_strands(void)
{
    int count = 0;
    if (webnet_mission_active() == 0) {
        return 0;
    }
    int off = 0;
    int remaining = DAT_805cad9c;
    if (0 < DAT_805cad9c) {
        do {
            if ((*(int *)(DAT_80612988 + off) != 1) && (*(int *)(DAT_80612988 + off) != 2)) {
                count++;
            }
            off += 0x140;
            remaining += -1;
        } while (remaining != 0);
    }
    return count;
}

// ============================================================================
// 0x80247154  webnet_explosion_burst_strands
// When a lethal explosion overlaps live strands, either burst them harmlessly
// or award CDH1 points and spawn a burst FX, then expire the strand.
// ============================================================================
void webnet_explosion_burst_strands(int param_1, int param_2, float *param_3)
{
    int    bounds[22];

    unsigned long long uv = __save_gpr_25();
    int   ctx = (int)(uv >> 0x20);

    if ((DAT_805015c0 & 0x40) != 0) {
        build_swept_bounds((double)FLOAT_80618a9c, bounds, (float *)(unsigned int)uv, param_3);
        int off = 0;
        for (int i = 0; i < DAT_805cad9c; i++) {
            int *strand = (int *)(DAT_80612988 + off);
            if (*strand != 2) {
                if (aabb_vs_entity_bounds((int)bounds, (float *)(strand + 10)) != 0) {
                    if (((DAT_805015c0 & 0x40) == 0) || (DAT_805015d8 != 0)) {
                        player_ai_explosion_spawn(0x1b, 0x17, (float *)(strand + 7), (float *)0, 0, 0, 1);
                        *strand = 2;
                    } else if ((ctx != 0) && (*(int *)(ctx + 8) < DAT_80612610)) {
                        worm_cdh1_award_points(strand + 7);
                        player_ai_explosion_spawn(0x1b, 0x17, (float *)(strand + 7), (float *)0, 0, 0, 1);
                        *strand = 2;
                    }
                }
            }
            off += 0x140;
        }
    }
    (void)param_1;
    (void)param_2;
    __restore_gpr_25();
}

// ============================================================================
// 0x80247290  webnet_strand_build_swept_aabb
// Build a strand's swept AABB (+0x28..+0x3c) from its position (+0x1c..+0x24)
// and the WRTH entity's sweep half-extents (+0x1a8..+0x1b0) and margin
// (+0x1c0..+0x1c8).
// ============================================================================
void webnet_strand_build_swept_aabb(int strand)
{
    int e = entity_handle_resolve(0x57525448);
    *(float *)(strand + 0x28) = (*(float *)(strand + 0x1c) + *(float *)(e + 0x1a8)) - *(float *)(e + 0x1c0);
    *(float *)(strand + 0x34) = *(float *)(e + 0x1c0) + *(float *)(strand + 0x1c) + *(float *)(e + 0x1a8);
    *(float *)(strand + 0x2c) = (*(float *)(strand + 0x20) + *(float *)(e + 0x1ac)) - *(float *)(e + 0x1c4);
    *(float *)(strand + 0x38) = *(float *)(e + 0x1c4) + *(float *)(strand + 0x20) + *(float *)(e + 0x1ac);
    *(float *)(strand + 0x30) = (*(float *)(strand + 0x24) + *(float *)(e + 0x1b0)) - *(float *)(e + 0x1c8);
    *(float *)(strand + 0x3c) = *(float *)(e + 0x1c8) + *(float *)(strand + 0x24) + *(float *)(e + 0x1b0);
}

// ============================================================================
// batch_719: net-phase control, strand spawning, phase-parameter accessors, and
// the per-frame bone matrix update. Address range 0x80247354 - 0x80247a7c.
// ============================================================================

// ============================================================================
// 0x80247354  webnet_strand_spawn
// Recycle a dead strand (state 2) or, if none and the pool isn't full (<= 5),
// grow the pool. Randomise the strand's spin phase and velocity, normalise and
// scale the velocity, seed position from the caller's vec3, then build the swept
// AABB and pick its render anchor node against the WRTH entity.
// ============================================================================
void webnet_strand_spawn(float *pos)
{
    int   slot;
    int   idx;
    int   count;
    int   base;
    float vx, vy, vz, lenSq, inv, scl;

    slot = -1;
    idx = 0;
    count = DAT_805cad9c;
    if (0 < DAT_805cad9c) {
        int i = 0;
        do {
            if (*(int *)(DAT_80612988 + idx) == 2) {
                slot = i;
            }
            i++;
            idx += 0x140;
            count += -1;
        } while (count != 0);
    }
    if (slot == -1) {
        if (5 < DAT_805cad9c) {
            return;
        }
        slot = DAT_805cad9c;
        DAT_805cad9c = DAT_805cad9c + 1;
    }

    base = DAT_80612988 + slot * 0x140;

    // Spin phase / axis randomisation (three rng draws, scaled into [-a,+a]).
    *(float *)(base + 0x1c) = FLOAT_80618a7c * ((float)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
    *(float *)(base + 0x20) = FLOAT_80618a7c * ((float)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
    *(float *)(base + 0x24) = FLOAT_80618a7c * ((float)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
    // The original immediately overwrites +0x20 with 0.0 and then seeds
    // position from the caller vec3 into +0x1c..+0x24.
    *(float *)(base + 0x20) = FLOAT_80618a18;
    *(int *)(base + 0x1c) = *(int *)&pos[0];
    *(int *)(base + 0x20) = *(int *)&pos[1];
    *(int *)(base + 0x24) = *(int *)&pos[2];

    // Velocity randomisation.
    *(float *)(base + 0x40) = FLOAT_80618a7c * ((float)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
    *(float *)(base + 0x44) = FLOAT_80618a7c * ((float)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
    *(float *)(base + 0x48) = FLOAT_80618a7c * ((float)rng_counter_tick() / FLOAT_80618a80 - FLOAT_80618a1c);
    *(float *)(base + 0x40) = *(float *)(base + 0x40) * FLOAT_80618a1c;
    *(float *)(base + 0x44) = *(float *)(base + 0x44) * FLOAT_80618a1c;
    *(float *)(base + 0x48) = *(float *)(base + 0x48) * FLOAT_80618a1c;
    *(float *)(base + 0x48) = FLOAT_80618ab4 + ABS(*(float *)(base + 0x44));

    vx = *(float *)(base + 0x40);
    vy = *(float *)(base + 0x44);
    vz = *(float *)(base + 0x48);
    lenSq = vz * vz + vx * vx + vy * vy;
    if (FLOAT_80618a28 < lenSq) {
        inv = 1.0f / SQRT(lenSq);
        inv = inv * -(FLOAT_80618a1c * lenSq * inv * inv - FLOAT_80618a2c);
        scl = FLOAT_80618a24 * inv * -(FLOAT_80618a1c * lenSq * inv * inv - FLOAT_80618a2c);
        *(float *)(base + 0x40) = vx * scl;
        *(float *)(base + 0x44) = *(float *)(base + 0x44) * scl;
        *(float *)(base + 0x48) = *(float *)(base + 0x48) * scl;
    }
    *(float *)(base + 0x40) = *(float *)(base + 0x40) * FLOAT_80618a84;
    *(float *)(base + 0x44) = *(float *)(base + 0x44) * FLOAT_80618a84;
    *(float *)(base + 0x48) = *(float *)(base + 0x48) * FLOAT_80618a84;

    // Angular speed + life scalars.
    *(float *)(base + 0x4c) = FLOAT_80618ab8 * ((float)rng_counter_tick() / FLOAT_80618a80);
    *(float *)(base + 0x50) = FLOAT_80618a74 * ((float)rng_counter_tick() / FLOAT_80618a80) + FLOAT_80618a30;
    *(float *)(base + 0x60) = FLOAT_80618a18;
    *(float *)(base + 0x64) = FLOAT_80618a24;
    *(float *)(base + 0x68) = FLOAT_80618a74;

    *(int *)base = 0;
    webnet_strand_build_swept_aabb(base);
    {
        int e = entity_handle_resolve(0x57525448);
        webnet_strand_pick_anchor_node(base, e);
    }
}

// ============================================================================
// 0x802477b8  webnet_strand_spawn_xyz
// Thin wrapper: pack three scalar coordinates into a local vec3 and spawn.
// ============================================================================
void webnet_strand_spawn_xyz(double x, double y, double z)
{
    float pos[3];
    pos[0] = (float)x;
    pos[1] = (float)y;
    pos[2] = (float)z;
    webnet_strand_spawn(pos);
}

// ============================================================================
// 0x802477e8  webnet_net_begin
// Start a new web-net: seed 5 strands from the anchor table DAT_8046ce00
// (5 vec3 entries, stride 0xc) and set the net phase to 1 (growing). Bracketed
// by the render/anim list lock so pool growth is safe against the frame thread.
// ============================================================================
void webnet_net_begin(void)
{
    int i;
    int off;

    (void)FUN_803826f0();
    i = 0;
    off = 0;
    do {
        webnet_strand_spawn((float *)((int)&DAT_8046ce00 + off));
        i = i + 1;
        off = off + 0xc;
    } while (i < 5);
    DAT_805cad74 = 1;
    FUN_8038273c();
}

// ============================================================================
// 0x8024784c  webnet_net_arm
// Arm the net (phase 1) only if it is currently idle (phase 0).
// ============================================================================
void webnet_net_arm(void)
{
    if (DAT_805cad74 != 0) {
        return;
    }
    DAT_805cad74 = 1;
}

// ============================================================================
// 0x8024786c  webnet_mission_active
// Net-ownership / mission-active gate: returns 1 when the mission-mode selector
// is either the CDH1 web-net level (0x3c) or the intro mode (1), else 0.
// Referenced by webnet_count_free_strands and elsewhere as FUN_8024786c.
// ============================================================================
int webnet_mission_active(void)
{
    if ((DAT_80612614 != 0x3c) && ((0x3b < DAT_80612614) || (DAT_80612614 != 1))) {
        return 0;
    }
    return 1;
}

// ============================================================================
// 0x80247898  webnet_set_flags
// Store the net behaviour-flags word (DAT_805cada0).
// ============================================================================
void webnet_set_flags(int flags)
{
    DAT_805cada0 = flags;
}

// ============================================================================
// 0x802478a8  webnet_phase_status
// Map the net life-state selector (DAT_805cadb8, 0..10) to a coarse status
// code. Every defined state currently yields 2 ("active"); the tail arithmetic
// preserves the ROM's fall-through mapping for any out-of-range value.
// ============================================================================
int webnet_phase_status(void)
{
    unsigned int code = 0;
    switch (DAT_805cadb8) {
    case 0: case 1: case 2: case 3: case 4: case 5:
    case 6: case 7: case 8: case 9: case 10:
        code = 2;
        break;
    default:
        code = 0;
        break;
    }
    if (code == 2) {
        return 2;
    }
    if (code < 2) {
        if (code == 0) {
            return -1;
        }
        return 1;
    }
    if (code == 4) {
        return 4;
    }
    if (3 < code) {
        return 0;
    }
    return 3;
}

// ============================================================================
// 0x8024797c  webnet_phase_param_a
// Return the first per-phase tuning scalar. All defined phases share
// FLOAT_80618ac0; out-of-range falls back to FLOAT_80618ac4.
// ============================================================================
double webnet_phase_param_a(void)
{
    switch (DAT_805cadb8) {
    case 0: case 1: case 2: case 3: case 4: case 5:
    case 6: case 7: case 8: case 9: case 10:
        return (double)FLOAT_80618ac0;
    default:
        return (double)FLOAT_80618ac4;
    }
}

// ============================================================================
// 0x802479fc  webnet_phase_param_b
// Return the second per-phase tuning scalar. Early phases (0..3) use
// FLOAT_80618ac4; phases 4..10 use FLOAT_80618ac8; out-of-range -> ac4.
// ============================================================================
double webnet_phase_param_b(void)
{
    switch (DAT_805cadb8) {
    case 0: case 1: case 2: case 3:
        return (double)FLOAT_80618ac4;
    case 4: case 5: case 6: case 7:
    case 8: case 9: case 10:
        return (double)FLOAT_80618ac8;
    default:
        return (double)FLOAT_80618ac4;
    }
}

// ============================================================================
// 0x80247a7c  webnet_update_bones
// Per-frame skeleton update for the WRTH web-net entity. In the score-complete
// phase (DAT_805cadb8 == 9) it evaluates the blended animation, propagates the
// root bone matrix down the chain, and applies the per-frame world offset
// (DAT_803e0280/84/88, plus the secondary vertical offset when enabled). In any
// other phase it resets every bone matrix to identity. Finally commits the
// matrices (FUN_80249364) and flags the render transform dirty (bit 0x200000).
// FUN_803826f0 returns the WRTH context/entity pointer on enter.
// ============================================================================
void webnet_update_bones(double a1, double a2, double a3, double a4,
                         double a5, double a6, double a7, double a8)
{
    int    entity;
    float  offX, offY, offZ;
    int    boneOff;
    int    boneIdx;
    float *root;

    entity = FUN_803826f0();
    offZ = DAT_803e0288;
    offY = DAT_803e0284;
    offX = DAT_803e0280;

    if (DAT_805cadb8 == 9) {
        statlvl_class_complete((double)FLOAT_80618ac4, a2, a3, a4, a5, a6, a7, a8);
        if (DAT_805cae30 != 0) {
            offX = offX + DAT_805cae04;
        }
        anim_blend_eval_flagged(entity);
        boneOff = 0x40;
        root = *(float **)(*(int *)(entity + 4) + 0x10);
        for (boneIdx = 1; boneIdx < *(int *)(*(int *)(*(int *)(entity + 4) + 4) + 4); boneIdx++) {
            mtx4_copy(root, (float *)((int)root + boneOff));
            boneOff = boneOff + 0x40;
        }
        *(float *)(entity + 0x64) = *(float *)(entity + 0x64) + offX;
        *(float *)(entity + 0x68) = *(float *)(entity + 0x68) + offY;
        *(float *)(entity + 0x6c) = *(float *)(entity + 0x6c) + offZ;
    } else {
        boneOff = 0;
        for (boneIdx = 0; boneIdx < *(int *)(*(int *)(*(int *)(entity + 4) + 4) + 4); boneIdx++) {
            mtx4_set_identity(*(int *)(*(int *)(entity + 4) + 0x10) + boneOff);
            boneOff = boneOff + 0x40;
        }
    }
    FUN_80249364();
    *(unsigned int *)(entity + 0x28) = *(unsigned int *)(entity + 0x28) | 0x200000;
    FUN_8038273c();
}

} // extern "C"
