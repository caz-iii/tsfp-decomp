// slot_machine_instance.cpp
// Level-prop subsystem: 3-reel combination / slot-machine prop - per-instance
// construction, culling, the interactive grab/spin handler, the settle/charge
// physics path, the interaction camera, and instance teardown.
// Third file of the slot-machine cluster (batch_714 reels, batch_715 lifecycle).
//
// Instance record (0x5c bytes, pooled at DAT_80612980; entity->+0x7c points to it):
//   [0]  variant index (0/1/2)          [1]  interaction state (1=idle..7)
//   [2]  saved/return state             [3]  owning entity
//   [4]  grabbing player entity (0=none)
//   [5]  reel spin velocity accumulator [6]  reel nudge accumulator
//   [9]  flags (bit0 lock, bit1 teardown)
//   [0xc] fade/blend timer              [0xd] present-flash timer
//   [0x14] model/resource id            [0x15] sound-blend scalar  [0x16] active flag
// Global phase DAT_80612614: 0xb teardown, 0x44 alt-build, 0x64/0x65/100 gameplay modes.
//
// Address range: 0x80242bb8 - 0x80243bc7

extern "C" {

// ---- shared cross-module helpers ----
void  slot_instance_tick(void);                 // 0x80242984 (batch_715)
int   slot_variant_from_model_id(int model_id); // 0x802428a0 (batch_715)
void  freelist_reset(unsigned int *pool);
void  freelist_init(unsigned int *pool, int stride, void *base, int count);
int  *freelist_pop(int pool);
void  freelist_push(int pool, void *node);
void  memset_wrapper(int dst, int val, int len);
int   subobj_get_object_matrix(unsigned int body);
int   subobj_get_contact_origin(unsigned int body);
int   subobj_get_contact_normal(unsigned int body);
int   subobj_get_linear_impulse(unsigned int body);
int   subobj_get_entity(unsigned int body);
int   subobj_is_idle(unsigned int body);
int   subobj_set_flag_bits(unsigned int body, unsigned int bits);
int   subobj_clear_flag_bits(unsigned int body, unsigned int bits);
int   subobj_pool_alloc(void);
void  subobj_build_descriptor(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                              unsigned long long a4, unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8, int body, int entity,
                              float *offset, int a11, int a12, float a13);
void  mtx4_transform_point(float *mtx, float *pt);
int   scene_test_frustum_vs_world(float *aabb);
int   scene_point_in_node_aabb(int node, float *pt);
int   scene_ray_traverse_portals(double a1, double a2, float *from, unsigned int mask,
                                 float *to, int a6);
void  entity_register_for_update(int entity, int slot, int cb);
int   entity_assign_room(int entity);
unsigned int input_get_buttons_held(int pad);
double input_get_analog_value(int pad, int axis);
void  player_set_state(int player, int state);
void  camera_set_mode_by_index(int cam, int mode, int entity);
void  object_model_swap_in(int model);
void  sound_init_play_params(int params);
int   sound_play_request(void *params);
void  sound_update_3d_source(double a1, double a2, double a3, int handle_slot, int pos,
                             int sound_id, int loop);
void  scalar_step_toward_snap(double target, double rate, float *value);
double math_sqrt(double x);
void  hud_hint_show_objective_grab(unsigned long long a1, double a2, double a3,
                                   unsigned long long a4, unsigned long long a5,
                                   unsigned long long a6, unsigned long long a7,
                                   unsigned long long a8);
void  gx_set_blend_mode(int mode);
void  gx_set_color_update_mode(void);
void  gx_set_sub_material_id(int id);
int   gx_set_state_c0(int a1);
void  render_instance_draw_pass(double a1, double a2, int a3, int a4, int a5);
int   __save_gpr_28(void);
void  __restore_gpr_28(void);

// ---- raw symbols referenced with file-local signatures ----
extern int  FUN_803826f0(void);                    // fetch active slot instance entity
extern int  FUN_803826d8(void);                    // fetch active slot render context (packed)
extern void FUN_8038273c(void);                    // instance-context release
extern void FUN_80382724(void);                    // render-context release
void slot_release_grab(int entity);                // 0x80243c88 (slot_machine_grab.cpp)
void slot_apply_tip_torque(void);                  // 0x802444b0 (slot_machine_grab.cpp)
void slot_spin_measure_stats(void);                // 0x80244020 (slot_machine_grab.cpp)
extern int  FUN_802ef5a8(int a1);                  // player-busy predicate
extern void FUN_80237ad8(double a1, int a2, int a3); // scheduled charge event
extern int  FUN_803825c0(double a1);               // render state builder
extern int  DAT_80611d74;                          // player list base (deref *DAT_80611d74)

// ---- state globals ----
extern int  DAT_80612614;   // global prop-subsystem phase
extern int  DAT_80612980;   // slot instance pool base
extern int  DAT_80612984;   // slot instance pool count/cursor
extern unsigned int DAT_805cad30;  // slot instance freelist head
extern int  DAT_80611aa8;   // world zone AABB array base
extern int  DAT_80611b14;   // world zone count
extern int  DAT_806108e4;   // grab-enabled / interaction-allowed flag
extern int  DAT_806108ec;   // frame delta (fixed)
extern int  DAT_806109c0;   // frame-rate divisor (int, magic-biased)
extern int  DAT_80611934;   // gameplay sub-mode
extern int  DAT_805015bd;   // level id byte
extern int  DAT_80611d54;   // scene node table base
extern int  DAT_80611d58;   // scene visibility set count
extern int  DAT_80611d5c;   // scene visibility bitmap base

extern int   DAT_8046cce8;  // camera param block base (stride 0xc0, per-variant)
extern int   DAT_8046cda0;  // per-variant spin-max table (stride 0x14)
extern int   DAT_8046cda4;  // per-variant spin-min table
extern int   DAT_8046cda8;  // per-variant spin-rate table
extern int   DAT_8046cdac;  // per-variant spin-damp table
extern int   DAT_8046cdb0;  // per-variant nudge-scale table

extern float  FLOAT_80618980;  // 0.0
extern float  FLOAT_8061899c;  // local anchor offset x
extern float  FLOAT_806189a0;  // local anchor offset y
extern float  FLOAT_806189a4;  // camera height bias
extern float  FLOAT_806189a8;  // camera target bias
extern float  FLOAT_806189ac;  // contact up-threshold
extern float  FLOAT_806189b0;  // contact speed-threshold / present duration
extern float  FLOAT_806189b4;  // clamp max
extern float  FLOAT_806189b8;  // stick deadzone (lock axis)
extern float  FLOAT_806189bc;  // stick deadzone (nudge axis)
extern float  FLOAT_806189c0;  // stick range scale
extern float  FLOAT_806189c4;  // opposing-spin damp
extern float  FLOAT_806189c8;  // grab SFX volume
extern float  FLOAT_806189cc;  // spin-speed->sound scale
extern float  FLOAT_806189d0;  // sound floor
extern float  FLOAT_806189d4;  // sound range
extern float  FLOAT_806189d8;  // sound blend rate
extern float  FLOAT_806189dc;  // sound param scale A
extern float  FLOAT_806189e0;  // sound param B
extern float  FLOAT_806189e4;  // present-flash rate
extern float  FLOAT_80618998;  // teardown height gate
extern float  FLOAT_806108ec;  // frame delta (float alias)
extern double DOUBLE_80618990; // int->double magic bias

extern float  DAT_803e020c;    // variant-0 descriptor offset (x/y/z triplet)
extern float  DAT_803e0210;
extern float  DAT_803e0214;
extern float  DAT_803e0218;    // variant-1 descriptor offset
extern float  DAT_803e021c;
extern float  DAT_803e0220;

// -----------------------------------------------------------------------------
// slot_pool_reset (0x80242da8)
// Zero the instance record and rebuild the instance freelist.
// -----------------------------------------------------------------------------
void slot_pool_reset(void)
{
    DAT_80612984 = 0;
    memset_wrapper(DAT_80612980, 0, 0x170);
    freelist_reset(&DAT_805cad30);
    freelist_init(&DAT_805cad30, 0x5c, (void *)DAT_80612980, 4);
}

// -----------------------------------------------------------------------------
// slot_instance_get_anchor (0x80242e00)
// World-space anchor point: fixed local offset transformed by the object matrix
// plus the entity world position.
// -----------------------------------------------------------------------------
void slot_instance_get_anchor(int entity, float *out)
{
    float *mtx = (float *)subobj_get_object_matrix(*(unsigned int *)(entity + 0x80));
    out[0] = FLOAT_8061899c;
    out[1] = FLOAT_806189a0;
    out[2] = FLOAT_80618980;
    mtx4_transform_point(mtx, out);
    out[0] += *(float *)(entity + 100);
    out[1] += *(float *)(entity + 0x68);
    out[2] += *(float *)(entity + 0x6c);
}

// -----------------------------------------------------------------------------
// slot_instance_get_facing (0x80242e8c)
// The instance facing axis: negated object-matrix X row.
// -----------------------------------------------------------------------------
void slot_instance_get_facing(int entity, float *out)
{
    float *mtx = (float *)subobj_get_object_matrix(*(unsigned int *)(entity + 0x80));
    out[0] = -mtx[0];
    out[1] = -mtx[1];
    out[2] = -mtx[2];
}

// -----------------------------------------------------------------------------
// slot_camera_setup_params (0x80242ee0)
// Seed the per-variant interaction-camera parameter block (DAT_8046cce8, stride
// 0xc0, indexed by the instance's variant slot) with eye and target positions
// derived from the entity world position plus bias constants.
// -----------------------------------------------------------------------------
void slot_camera_setup_params(int entity)
{
    float biasT = FLOAT_806189a8;
    float biasH = FLOAT_806189a4;
    float zero  = FLOAT_80618980;
    int inst = *(int *)(entity + 0x7c);
    int slot = *(int *)(inst + 0x28) * 0xc0;

    *(int *)(&DAT_8046cce8 + slot) = *(int *)(entity + 100);
    *(int *)(&DAT_8046cce8 + 4 + slot) = *(int *)(entity + 0x68);
    *(int *)(&DAT_8046cce8 + 8 + slot) = *(int *)(entity + 0x6c);
    *(int *)(&DAT_8046cce8 + 0x30 + slot) = *(int *)(entity + 100);
    *(int *)(&DAT_8046cce8 + 0x34 + slot) = *(int *)(entity + 0x68);
    *(int *)(&DAT_8046cce8 + 0x38 + slot) = *(int *)(entity + 0x6c);

    *(float *)(&DAT_8046cce8 + 4 + slot) += biasH;
    *(float *)(&DAT_8046cce8 + 0x30 + slot) += biasT;
    *(float *)(&DAT_8046cce8 + 0x34 + slot) = zero;
    *(float *)(&DAT_8046cce8 + 0x38 + slot) = zero;
}

// -----------------------------------------------------------------------------
// slot_instance_cull_tick (0x80242bb8)
// Visibility/AABB gate around slot_instance_tick. Builds the instance world AABB
// (position +/- half-extents), then depending on the global phase either tests it
// against the world zone list (phase 100), or runs a frustum test (phase 0xb with
// a height gate), and calls slot_instance_tick only when the instance should update.
// -----------------------------------------------------------------------------
void slot_instance_cull_tick(int entity)
{
    bool run;
    float cx, cy, cz, hx, hy, hz;

    cx = *(float *)(entity + 100)  + *(float *)(entity + 0x1a8);
    cy = *(float *)(entity + 0x68) + *(float *)(entity + 0x1ac);
    cz = *(float *)(entity + 0x6c) + *(float *)(entity + 0x1b0);
    hx = cx + *(float *)(entity + 0x1c0); cx = cx - *(float *)(entity + 0x1c0);
    hy = cy + *(float *)(entity + 0x1c4); cy = cy - *(float *)(entity + 0x1c4);
    hz = cz + *(float *)(entity + 0x1c8); cz = cz - *(float *)(entity + 0x1c8);

    if (DAT_80612614 == 100) {
        int idx = 0xb4;
        run = true;
        int n = DAT_80611b14;
        if (0 < DAT_80611b14) {
            do {
                int z = DAT_80611aa8 + idx;
                if (*(float *)(z + 0x10) < hx) {
                    if (cx < *(float *)(z + 0x1c)) {
                        if (*(float *)(z + 0x18) < hz) {
                            if (cz < *(float *)(z + 0x24)) {
                                run = false;
                                break;
                            }
                        }
                    }
                }
                idx += 0xb4;
                n--;
            } while (n != 0);
        }
        int off = 0;
        int cnt = (int)*(char *)(entity + 0x30);
        if (0 < cnt) {
            do {
                int z = DAT_80611aa8 + *(int *)(entity + off + 0x38) * 0xb4;
                if ((((*(float *)(z + 0x1c) < cx) || (hx < *(float *)(z + 0x10))) &&
                     ((*(float *)(z + 0x20) < cy) || (hy < *(float *)(z + 0x14)))) &&
                    ((*(float *)(z + 0x24) < cz) || (hz < *(float *)(z + 0x18)))) {
                    run = true;
                    break;
                }
                off += 4;
                cnt--;
            } while (cnt != 0);
        }
        if (run) {
            slot_instance_tick();
        }
    } else if ((DAT_80612614 != 0xb) ||
               ((scene_test_frustum_vs_world(&cx) != 0) &&
                (FLOAT_80618998 <= *(float *)(entity + 0x68)))) {
        if ((scene_test_frustum_vs_world(&cx) == 0) && (DAT_80612614 != 0x65)) {
            slot_instance_tick();
        }
    } else {
        slot_instance_tick();
    }
}

// -----------------------------------------------------------------------------
// slot_settle_charge_tick (0x80242fb4)
// Landing/settle handler: when the object has come to rest (contact normal near
// vertical and slow) advance the per-variant spin-charge accumulator in interaction
// states 4-7, else transition into ready state 4 and fire a scheduled charge event.
// -----------------------------------------------------------------------------
void slot_settle_charge_tick(void)
{
    unsigned int body = FUN_803826f0();
    int mtx = subobj_get_object_matrix(body);
    int entity = subobj_get_entity(body);
    int *inst = *(int **)(entity + 0x7c);
    float *n = (float *)subobj_get_contact_normal(body);

    if ((*(float *)(mtx + 0x14) <= FLOAT_806189ac) &&
        (n[2] * n[2] + n[0] * n[0] + n[1] * n[1] <= FLOAT_806189b0)) {
        int state = inst[1];
        if ((state == 4) || ((unsigned int)(state - 6) < 2) || (state == 5)) {
            float v = (float)inst[5];
            float rate = *(float *)(&DAT_8046cda8 + *inst * 0x14);
            if ((v <= FLOAT_80618980) || (v < rate)) {
                if ((FLOAT_80618980 <= v) || (rate < v)) {
                    inst[5] = (int)FLOAT_80618980;
                } else {
                    inst[5] = (int)((float)inst[5] + rate);
                }
            } else {
                inst[5] = (int)(v - rate);
            }
        } else {
            inst[1] = 4;
            FUN_80237ad8((double)FLOAT_806189b4, 9, *(int *)DAT_80611d74);
            inst[0xc] = (int)(FLOAT_806108ec *
                              (FLOAT_806189b4 /
                               (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000)
                                       - DOUBLE_80618990)) + (float)inst[0xc]);
        }
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// slot_grab_interaction_handler (0x80243154)
// Main per-frame interactive handler for a grabbed slot machine: shows the grab
// hint, reads the player's analog stick to spin/nudge the reels (per-variant
// clamp tables), plays grab SFX, drives the reel-set update, renders the present
// flash pass, and updates the two 3D sound sources. Releases on exit input.
// -----------------------------------------------------------------------------
void slot_grab_interaction_handler(void)
{
    float fVar1, fVar2;
    int entity;
    float *imp;
    unsigned int held;
    int iVar6;
    int *inst;
    double dVar10, dVar12, dVar13, dVar15, dVar14, dVar11;
    int params[6];
    float local_90;
    int local_8c, local_88, local_84;
    unsigned char local_80;

    entity = __save_gpr_28();
    dVar15 = (double)FLOAT_80618980;
    unsigned int body = *(unsigned int *)(entity + 0x80);
    dVar10 = dVar15;
    imp = (float *)subobj_get_linear_impulse(body);
    if (DAT_806108e4 == 0) { __restore_gpr_28(); return; }

    inst = *(int **)(entity + 0x7c);
    if (FLOAT_806189b0 < (float)inst[0xc]) {
        slot_instance_tick();
        inst[0xc] = (int)FLOAT_80618980;
    }
    slot_instance_cull_tick(entity);

    dVar12 = (double)(imp[0] * imp[0]);
    dVar13 = (double)(imp[2] * imp[2]);
    if ((float)(dVar13 + (double)(float)(dVar12 + (double)(imp[1] * imp[1]))) <= FLOAT_806189b4) {
        subobj_clear_flag_bits(body, 8);
    } else {
        subobj_set_flag_bits(body, 8);
    }

    if ((2 < inst[1]) && (inst[4] != 0)) {
        if (FUN_802ef5a8(*(int *)(*(int *)(inst[4] + 0x150) + 4)) != 0) {
            __restore_gpr_28();
            return;
        }
        hud_hint_show_objective_grab(0, dVar12, dVar13, 0, 0, 0, 0, 0);
        held = input_get_buttons_held(**(int **)(inst[4] + 0x150));
        if (*(int *)(*(int *)(inst[4] + 0x150) + 0x114) == 0x10) {
            slot_release_grab(entity);
            __restore_gpr_28();
            return;
        }
        slot_settle_charge_tick();
        if (3 < inst[1]) {
            slot_apply_tip_torque();
        }
        if (((inst[9] & 1U) == 0) &&
            ((input_get_buttons_held(**(int **)(inst[4] + 0x150)) & 0x40) != 0)) {
            player_set_state(*(int *)(inst[4] + 0x150), 2);
            slot_release_grab(entity);
        }

        if (inst[1] == 3) {
            dVar10 = input_get_analog_value(**(int **)(inst[4] + 0x150), 1);
            dVar12 = input_get_analog_value(**(int **)(inst[4] + 0x150), 6);
            dVar13 = input_get_analog_value(**(int **)(inst[4] + 0x150), 5);
            float rate = *(float *)(&DAT_8046cda8 + *inst * 0x14);
            if (dVar12 < 0 ? -dVar12 < (double)FLOAT_806189b8 : dVar12 < (double)FLOAT_806189b8) {
                double ad = (dVar13 < 0 ? -dVar13 : dVar13);
                if ((double)FLOAT_806189b8 <= ad) {
                    dVar11 = (double)(float)inst[5];
                    dVar14 = (double)(float)(dVar13 * (double)rate);
                    if (dVar11 <= (double)FLOAT_80618980) {
                        if ((dVar11 < (double)FLOAT_80618980) ||
                            (subobj_is_idle(*(unsigned int *)(entity + 0x80)) != 0)) {
                            inst[5] = (int)(float)((double)(float)inst[5] - dVar14);
                        }
                    } else {
                        inst[5] = (int)-(float)(dVar14 * (double)FLOAT_806189bc - dVar11);
                    }
                    float mn = *(float *)(&DAT_8046cda4 + *inst * 0x14);
                    if ((float)inst[5] < -mn) {
                        inst[5] = (int)-mn;
                    }
                }
            } else {
                inst[5] = (int)((float)inst[5] + (float)(dVar12 * (double)rate));
                float mx = *(float *)(&DAT_8046cda0 + *inst * 0x14);
                if (mx < (float)inst[5]) {
                    inst[5] = (int)mx;
                }
            }

            double ad10 = (dVar10 < 0 ? -dVar10 : dVar10);
            if (ad10 <= (double)FLOAT_806189bc) {
                inst[6] = (int)FLOAT_80618980;
            } else {
                fVar1 = *(float *)(&DAT_8046cdb0 + *inst * 0x14) *
                        ((float)(ad10 - (double)FLOAT_806189bc) / FLOAT_806189c0);
                if ((double)FLOAT_80618980 <= dVar10) {
                    inst[6] = (int)fVar1;
                } else {
                    inst[6] = (int)-fVar1;
                }
                dVar15 = (double)((float)(ad10 - (double)FLOAT_806189bc) / FLOAT_806189c0);
                double ad12 = (dVar12 < 0 ? -dVar12 : dVar12);
                double ad13 = (dVar13 < 0 ? -dVar13 : dVar13);
                if ((ad12 < (double)FLOAT_806189b8) && ((double)FLOAT_806189b8 <= ad13) &&
                    ((float)inst[5] < FLOAT_80618980)) {
                    inst[6] = (int)(FLOAT_806189c4 * (float)inst[6]);
                }
            }

            if ((held & 0x80) != 0) {
                sound_init_play_params((int)params);
                local_80 = 1;
                params[0] = 0x3b2;
                local_90 = FLOAT_806189c8;
                local_8c = *(int *)(entity + 100);
                local_88 = *(int *)(entity + 0x68);
                local_84 = *(int *)(entity + 0x6c);
                (void)local_90; (void)local_8c; (void)local_88; (void)local_84; (void)local_80;
                sound_play_request(params);
            }

            fVar1 = (float)inst[5];
            fVar2 = *(float *)(&DAT_8046cdac + *inst * 0x14);
            if (fVar1 <= fVar2) {
                if (-fVar2 <= fVar1) {
                    inst[5] = (int)FLOAT_80618980;
                } else {
                    inst[5] = (int)(fVar1 + fVar2);
                }
            } else {
                inst[5] = (int)(fVar1 - fVar2);
            }

            dVar10 = math_sqrt((double)(*(float *)(entity + 0x100) * *(float *)(entity + 0x100) +
                                        *(float *)(entity + 0xf8) * *(float *)(entity + 0xf8) +
                                        *(float *)(entity + 0xfc) * *(float *)(entity + 0xfc)));
            double frameScale = (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000)
                                        - DOUBLE_80618990);
            double instScale  = (float)((double)(long long)(unsigned int)(inst[0x16] ^ 0x80000000)
                                        - DOUBLE_80618990);
            dVar10 = (double)((float)(frameScale * dVar10 / FLOAT_806189cc) * (float)instScale);
            dVar12 = dVar10;
            if ((double)FLOAT_806189b4 < dVar10) dVar12 = (double)FLOAT_806189b4;
            if ((double)FLOAT_806189d0 <= dVar12) {
                if ((double)FLOAT_806189b4 < dVar10) dVar10 = (double)FLOAT_806189b4;
            } else {
                dVar10 = (double)FLOAT_806189d0;
            }
            dVar13 = (double)((float)(dVar10 - (double)FLOAT_806189d0) / FLOAT_806189d4);
            dVar12 = dVar15;
            if (dVar13 < dVar15) dVar12 = dVar13;
            if ((double)FLOAT_80618980 <= dVar12) {
                if (dVar13 < dVar15) dVar15 = dVar13;
            } else {
                dVar15 = (double)FLOAT_80618980;
            }
        }

        if (((inst[1] == 3) || (inst[1] == 4)) && (DAT_80611934 != 5)) {
            slot_spin_measure_stats();
        }
    }

    scalar_step_toward_snap(dVar10, (double)FLOAT_806189d8, (float *)(inst + 0x15));
    fVar1 = FLOAT_806189dc;
    inst[0x16] = DAT_806108e4;
    sound_update_3d_source((double)(fVar1 * (float)inst[0x15]), (double)FLOAT_806189e0,
                           (double)(FLOAT_806189c0 * (float)inst[0x15] + FLOAT_806189a4),
                           entity + 0x22c, entity + 100, 0xd73, 1);

    if (inst[1] == 2) {
        dVar13 = (double)(float)((double)FLOAT_806189e4 *
                 (double)(float)((double)(float)inst[0xd] / (double)FLOAT_806189b0));
        if ((double)FLOAT_806189b0 <= (double)(float)inst[0xd]) {
            inst[1] = inst[2];
        }
        gx_set_blend_mode(4);
        gx_set_color_update_mode();
        render_instance_draw_pass((double)FLOAT_806189b0, (double)FLOAT_806189e4,
                                  *(int *)(entity + 4), 0, 0);
        iVar6 = FUN_803825c0(dVar13);
        int st = gx_set_state_c0(iVar6);
        gx_set_sub_material_id(0xf);
        gx_set_color_update_mode();
        render_instance_draw_pass((double)st, (double)FLOAT_806189e4,
                                  *(int *)(entity + 4), 0, 0);
        float cap = FLOAT_806189b0;
        inst[0xd] = (int)(FLOAT_806108ec *
                          (FLOAT_806189b4 /
                           (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000)
                                   - DOUBLE_80618990)) + (float)inst[0xd]);
        if (cap < (float)inst[0xd]) {
            inst[0xd] = (int)cap;
        }
    }

    sound_update_3d_source((double)(float)((double)FLOAT_806189dc * dVar15),
                           (double)FLOAT_806189e0, (double)FLOAT_806189b4,
                           entity + 0x228, entity + 100, 0x5ff, 0);
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// slot_construct_instance (0x80243810)
// Spawn/construct handler: pop an instance record from the pool, resolve the
// variant from the model id, wire it to the entity, register for updates, assign
// a room, allocate the physics sub-object and build its per-variant descriptor.
// -----------------------------------------------------------------------------
void slot_construct_instance(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                             unsigned long long a4, unsigned long long a5, unsigned long long a6,
                             unsigned long long a7, unsigned long long a8)
{
    int entity = FUN_803826f0();
    int *inst = freelist_pop(-0x7fa352d0);
    int variant = slot_variant_from_model_id((unsigned int)*(unsigned short *)(entity + 0x94));
    if (inst == (int *)0) { FUN_8038273c(); return; }

    inst[0] = variant;
    inst[1] = 1;
    inst[2] = 1;
    inst[3] = entity;
    inst[4] = 0;
    inst[0x15] = (int)FLOAT_80618980;
    inst[0x16] = 0;
    *(int **)(inst[3] + 0x7c) = inst;
    *(unsigned int *)(inst[3] + 0x28) |= 4;
    entity_register_for_update(inst[3], 0, -0x7fdbc438);
    entity_assign_room(entity);

    if (*(int *)(entity + 0x80) == 0) {
        *(int *)(entity + 0x80) = subobj_pool_alloc();
    }

    if (variant == 1) {
        float off[3];
        off[0] = DAT_803e0218; *(int *)&off[1] = *(int *)&DAT_803e021c; *(int *)&off[2] = *(int *)&DAT_803e0220;
        if (*(int *)(entity + 0x80) != 0) {
            subobj_build_descriptor(0, a2, a3, a4, a5, a6, a7, a8,
                                    *(int *)(entity + 0x80), entity, off, 0x16, 0x1a, 1.2199921e-19f);
        }
        inst[0x14] = 0xdb2;
    } else if ((variant >= 0) && (variant < 3)) {
        float off[3];
        off[0] = DAT_803e020c; *(int *)&off[1] = *(int *)&DAT_803e0210; *(int *)&off[2] = *(int *)&DAT_803e0214;
        int body = *(int *)(entity + 0x80);
        if (body != 0) {
            if (DAT_80612614 == 0x44) {
                subobj_build_descriptor(0, a2, a3, a4, a5, a6, a7, a8, body, entity,
                                        off, 0x17, -1, 1.2199921e-19f);
            } else {
                subobj_build_descriptor(0, a2, a3, a4, a5, a6, a7, a8, body, entity,
                                        off, 0x16, -1, 1.2199921e-19f);
            }
        }
        inst[0x14] = 0xdda;
    }

    if ((*(unsigned int *)(entity + 0x80) != 0) && (DAT_805015bd == '\n') &&
        (DAT_80612614 == 0xb)) {
        subobj_set_flag_bits(*(unsigned int *)(entity + 0x80), 0x400000);
    }
    if (DAT_80612614 == 0xb) {
        inst[9] |= 2;
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// slot_destroy_instance (0x80243a18)
// Destroy handler: release the grabbing player, clear the instance state and push
// the record back to the pool freelist.
// -----------------------------------------------------------------------------
void slot_destroy_instance(int entity)
{
    slot_release_grab(entity);
    int *inst = *(int **)(entity + 0x7c);
    inst[1] = 0;
    freelist_push(-0x7fa352d0, inst);
}

// -----------------------------------------------------------------------------
// slot_camera_resolve_view_node (0x80243a5c)
// Resolve the interaction camera's clip/view node: pick the nearest visible scene
// node whose AABB contains the machine's contact origin, then portal-traverse a ray
// toward the camera target and store the resulting node on the player camera.
// -----------------------------------------------------------------------------
void slot_camera_resolve_view_node(unsigned long long a1, double a2)
{
    unsigned long long packed = (unsigned long long)FUN_803826d8();
    int rctx = (int)(packed >> 0x20);
    int lo = (int)packed;
    float *origin = (float *)subobj_get_contact_origin(*(unsigned int *)(rctx + 0x80));
    unsigned int node = *(unsigned int *)(rctx + 0x34);
    float local_38, local_34, local_30;

    if (*(int *)(lo + 8) != 0) {
        if (1 < *(char *)(rctx + 0x30)) {
            int best = 999;
            int off = 0;
            for (int i = 0; i < *(char *)(rctx + 0x30); i++) {
                int nd = *(int *)(rctx + off + 0x38);
                int order = *(int *)(DAT_80611d54 + nd * 0xc + 0x3bc);
                if ((order < best) && (-1 < order)) {
                    int vis = *(int *)(*(int *)(lo + 8) + 0x214);
                    char flag;
                    if (vis < DAT_80611d58) {
                        flag = *(char *)(DAT_80611d5c + vis * 0xffc + nd + 0xd2c);
                    } else {
                        flag = 0;
                    }
                    if ((flag != 0) && (scene_point_in_node_aabb(nd, origin) != 0)) {
                        node = *(unsigned int *)(rctx + off + 0x38);
                        best = order;
                    }
                }
                off += 4;
            }
        }
        int cam = *(int *)(lo + 8);
        local_38 = *(float *)(cam + 0x27c) - origin[0];
        local_34 = *(float *)(cam + 0x280) - origin[1];
        local_30 = (float)((double)*(float *)(cam + 0x284) - (double)origin[2]);
        int hit = scene_ray_traverse_portals((double)*(float *)(cam + 0x284), a2, origin, node,
                                             &local_38, 0);
        if (hit != 0) {
            *(int *)(*(int *)(lo + 8) + 0x2a0) = hit;
        }
    }
    FUN_80382724();
}

} // extern "C"
