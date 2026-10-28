// slot_machine_grab.cpp
// Level-prop subsystem: 3-reel combination / grab-and-spin prop - the player
// grab/engage + release path, the spin physics (swept collision, spin-throw
// measurement + personal-best stats, tip/topple torque), and the secondary FX
// pool used by the spin effect. Fourth file of the cluster (batch_714 reels,
// batch_715 lifecycle, batch_716 instance).
//
// Interaction states (instance rec +0x04): 1 idle, 3 grabbed/spinning,
//   4 upright hold, 5/6/7 tip directions. Player menu-state marker 0x1000 = grabbing.
//
// Address range: 0x80243bc8 - 0x80244a73

extern "C" {

// ---- shared cross-module helpers ----
void  slot_camera_setup_params(int entity);                 // 0x80242ee0 (batch_716)
void  slot_camera_resolve_view_node(unsigned long long a1, double a2); // 0x80243a5c (b716)
int   subobj_get_object_matrix(unsigned int body);
int   subobj_get_descriptor(unsigned int body);
int   subobj_get_contact_normal(unsigned int body);
int   subobj_get_contact_origin(unsigned int body);
int   subobj_is_idle(unsigned int body);
int   subobj_get_activation_frame(unsigned int body);
double subobj_get_inverse_mass(unsigned int body);
void  subobj_add_angular_force(unsigned int body, float *f);
void  subobj_add_linear_force(unsigned int body, float *f);
void  subobj_add_torque_from_offset(unsigned int body, float *at, float *f);
void  subobj_dirty_list_add(unsigned int body);
void  mtx4_transform_vector_alias(float *mtx, float *v);
int   entity_query_aabb(int node, float *aabb, int out, int count, int stride, unsigned int mask);
void  collision_entity_swept_cast(int a1, float *from, float *vel, int a4, int entity,
                                  int *ctx, int a7, int mask);
void  player_set_state(int player, int state);
int   camera_set_mode(int cam, int mode, int entity);
void  camera_set_mode_by_index(int cam, int mode, int entity);
void  object_model_swap_in(int model);
void  object_model_swap_out_and_clear(int model);
double math_sqrt(double x);
int   freelist_peek_head(unsigned int *pool);
void  memset_wrapper(int dst, int val, int len);
int   mem_alloc_aligned(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                        unsigned long long a4, unsigned long long a5, unsigned long long a6,
                        unsigned long long a7, unsigned long long a8, int size, int count,
                        int align, int a12, int a13, int a14, int a15, int a16);
double worm_stat_get_snapshot(int stat, int player_rec);
int   get_string_by_id(int id);
void  time_format_long(double a1, double a2, double a3, double a4, unsigned long long a5,
                       unsigned long long a6, unsigned long long a7, unsigned long long a8);
int   debug_log_formatted(double a1, double a2, double a3, double a4, unsigned long long a5,
                          unsigned long long a6, unsigned long long a7, unsigned long long a8,
                          int fmt, int a10, void *a11, int a12, void *a13, int a14, void *a15,
                          int a16);
void  hud_text_submit(int a1, int a2, unsigned int *text);

// ---- raw symbols referenced with file-local signatures ----
extern int  FUN_803826f0(void);   // fetch active slot instance entity (single)
extern int  FUN_803826dc(void);   // fetch active slot render context (packed hi:entity)
extern int  FUN_803826e8(void);   // fetch active slot work context (packed hi:entity)
extern void FUN_8038273c(void);   // instance-context release
extern void FUN_80382734(void);   // work-context release
extern void FUN_80382728(void);   // query-context release
extern void FUN_80237a70(double a1, int stat, int player_rec); // record personal-best stat

extern int  s__s___2f__s___s___2f__s___s__s_8046cddc; // HUD format string

// ---- state globals ----
extern unsigned int DAT_805cad30;  // instance freelist head
extern int  DAT_80612988;   // secondary FX pool base
extern int  DAT_805cad54;   // fx registration list cursor
extern int  DAT_805cad40;   // fx registration list base
extern float DAT_805cad58, DAT_805cad5c, DAT_805cad60, DAT_805cad64;
extern float DAT_805cad68, DAT_805cad6c, DAT_805cad78;
extern int  DAT_805cad74, DAT_805cad88, DAT_805cada4;
extern int  DAT_806109c0;   // frame-rate divisor
extern int  DAT_80611928;   // current frame counter
extern int  DAT_805015c0;   // gameplay flags byte
extern int  DAT_8060f948;   // build/region id
extern unsigned char DAT_80610f08; // fallback name string

extern float  FLOAT_80618980;  // 0.0
extern float  FLOAT_806189b4;  // clamp max
extern float  FLOAT_806189c4;  // torque falloff
extern float  FLOAT_806189cc;  // contact-offset scale
extern float  FLOAT_806189d0;  // sound floor
extern float  FLOAT_806189d8;  // sound blend base
extern float  FLOAT_806189dc;  // downward-vel damp
extern float  FLOAT_806189e8;  // speed^2 -> sound scale
extern float  FLOAT_806189ec;  // local up offset
extern float  FLOAT_806189f0;  // spin force @50fps
extern float  FLOAT_806189f4;  // spin force @60fps
extern float  FLOAT_806189f8;  // angular-velocity scale
extern float  FLOAT_806189fc;  // force epsilon
extern float  FLOAT_806108ec;  // frame delta (float)
extern float  FLOAT_80618a00;  // upright up-threshold
extern float  FLOAT_80618a04;  // hold timeout
extern float  FLOAT_80618a08;  // tip lateral threshold
extern float  FLOAT_80618a0c;  // contact-normal spin threshold
extern float  FLOAT_80618a10;  // tip torque scale (state 6)
extern float  FLOAT_80618a14;  // tip torque scale (state 7 / default)
extern float  FLOAT_80618a18;  // fx state default A
extern float  FLOAT_80618a1c;  // fx state default B
extern float  FLOAT_80618a20;  // fx state default C
extern double DOUBLE_80618990; // int->double magic bias

// forward
void slot_release_grab(int entity);
void slot_fx_pool_reset(void);

// -----------------------------------------------------------------------------
// slot_grab_engage (0x80243bc8)
// Player grabs the machine: transition idle(1)/free -> grabbed(3), bind the
// player, set the player's menu state 0x1000, set up and resolve the interaction
// camera, arm a 0x1e-frame timer, and swap the player model out.
// -----------------------------------------------------------------------------
void slot_grab_engage(unsigned long long param_1, double param_2)
{
    unsigned long long packed = (unsigned long long)FUN_803826f0();
    int entity = (int)(packed >> 0x20);
    int player = (int)packed;
    int inst = *(int *)(entity + 0x7c);

    if ((*(int *)(inst + 4) == 1) || (*(int *)(inst + 0x10) == 0)) {
        *(int *)(inst + 8) = 3;
        *(int *)(inst + 4) = 3;
        *(int *)(inst + 0x10) = player;
        player_set_state(*(int *)(player + 0x150), 0x1000);
        slot_camera_setup_params(entity);
        int cam = *(int *)(*(int *)(player + 0x150) + 8);
        unsigned long long view = 0;
        if (cam != 0) {
            view = (unsigned int)camera_set_mode(cam, *(int *)(inst + 0x28) * 0xc0 + -0x7fb93320,
                                                 entity);
        }
        slot_camera_resolve_view_node(view, param_2);
        *(int *)(*(int *)(player + 0x150) + 0x140c) = 0x1e;
        object_model_swap_out_and_clear(*(int *)(player + 0x7c));
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// slot_release_grab (0x80243c88)
// Release the grabbing player: save prev state and set idle(1), restore the
// player camera/state, swap the player model back in, clear the player binding.
// -----------------------------------------------------------------------------
void slot_release_grab(int entity)
{
    float zero = FLOAT_80618980;
    int inst = *(int *)(entity + 0x7c);
    *(int *)(inst + 8) = *(int *)(inst + 4);
    *(int *)(inst + 4) = 1;
    *(float *)(inst + 0x14) = zero;

    int player = *(int *)(inst + 0x10);
    if (player != 0) {
        int prec = *(int *)(player + 0x150);
        if ((prec != 0) && (*(int *)(prec + 8) != 0) && (*(int *)(prec + 0x114) != 0x10)) {
            camera_set_mode_by_index(*(int *)(prec + 8), 0, player);
            player_set_state(prec, 2);
            object_model_swap_in(*(int *)(*(int *)(prec + 0x14) + 0x7c));
        }
        int cam = *(int *)(prec + 8);
        if ((cam != 0) && ((*(unsigned int *)(cam + 0x21c) & 0x10000) != 0)) {
            *(unsigned int *)(cam + 0x21c) &= 0xfffeffff;
        }
    }
    *(int *)(inst + 0x10) = 0;
}

// -----------------------------------------------------------------------------
// slot_spin_sweep_collide (0x80243d4c)
// Build a swept AABB from the machine's descriptor velocity, query entities in
// the nearby scene zones, then swept-cast against lighter or idle bodies so a
// spinning machine can knock them around.
// -----------------------------------------------------------------------------
void slot_spin_sweep_collide(void)
{
    unsigned long long packed = (unsigned long long)FUN_803826dc();
    int rctx = (int)(packed >> 0x20);
    float *desc = (float *)subobj_get_descriptor(*(unsigned int *)(rctx + 0x80));
    unsigned int self = *(unsigned int *)(rctx + 0x80);
    unsigned int mask = 0x38;
    int out[33];
    float sweep[16];   // local_c8..local_90 span

    if (((unsigned int)desc[0x70] & 0x20000) != 0) {
        mask = 0x3a;
    }

    double vScale = 0.0;  // decompiler in_f1 (frame dt) - passed via prior call
    float vx = (float)((double)desc[0x53] * vScale);
    float vy = (float)((double)desc[0x54] * vScale);
    float vz = (float)((double)desc[0x55] * vScale);

    double pad = (double)(float)(vScale * (double)(FLOAT_806189e8 *
                        (desc[0x55] * desc[0x55] + desc[0x53] * desc[0x53] + desc[0x54] * desc[0x54]))
                        + (double)FLOAT_806189d8);
    if ((double)FLOAT_806189d0 < pad) {
        // clamp to floor when above ceiling handled below
    }
    double padc = pad;
    if ((double)FLOAT_806189d0 < pad) padc = (double)FLOAT_806189d0;
    if ((double)FLOAT_806189d8 <= padc) {
        if ((double)FLOAT_806189d0 < pad) pad = (double)FLOAT_806189d0;
    } else {
        pad = (double)FLOAT_806189d8;
    }

    float base[3];
    base[0] = FLOAT_80618980; base[1] = FLOAT_806189ec; base[2] = FLOAT_80618980;
    mtx4_transform_vector_alias(desc, base);

    float from[3];
    from[0] = desc[0x50] - base[0];
    from[1] = desc[0x51] - base[1];
    from[2] = desc[0x52] - base[2];

    float lo[3], hi[3];
    double fx = (double)from[0], fy = (double)from[1], fz = (double)from[2];
    double ex = (double)(float)(fx + (double)vx);
    double ey = (double)(float)(fy + (double)vy);
    double ez = (double)(float)(fz + (double)vz);
    lo[0] = (float)((fx < ex ? fx : ex) - pad);
    lo[1] = (float)((fy < ey ? fy : ey) - pad);
    lo[2] = (float)((fz < ez ? fz : ez) - pad);
    hi[0] = (float)(pad + (ex < fx ? fx : ex));
    hi[1] = (float)(pad + (ey < fy ? fy : ey));
    hi[2] = (float)(pad + (ez < fz ? fz : ez));

    // aabb laid out as lo[3] then hi[3] contiguous for the query
    float aabb[6] = { lo[0], lo[1], lo[2], hi[0], hi[1], hi[2] };

    int count = 0;
    int off = 0;
    for (int i = 0; i < *(char *)(rctx + 0x30); i++) {
        int node = *(int *)(rctx + off + 0x38);
        if ((0 < node) && (node < 0xc9)) {
            count = entity_query_aabb(node, aabb, (int)out, count, 0x14, mask);
        }
        off += 4;
    }

    off = 0;
    for (int i = 0; i < count; i++) {
        unsigned int other = *(unsigned int *)(*(int *)((int)out + off) + 0x80);
        if (((other & 0xffff) < (self & 0xffff)) ||
            ((other != 0) && (subobj_is_idle(other) != 0))) {
            collision_entity_swept_cast(*(int *)(*(int *)((int)out + off) + 4), from, &vx, 0, rctx,
                                        (int *)packed, 1, 0x24000);
        }
        off += 4;
    }
    FUN_80382728();
}

// -----------------------------------------------------------------------------
// slot_spin_measure_stats (0x80244020)
// While the machine is being spun/thrown, apply the spin force, measure the throw
// (peak height + travel distance + duration) and, on release, commit personal-best
// stats (5=distance, 6=height, 7=time, 8=total) and post the HUD readout.
// -----------------------------------------------------------------------------
void slot_spin_measure_stats(void)
{
    int entity = FUN_803826e8();
    int inst = *(int *)(entity + 0x7c);
    float *mtx = (float *)subobj_get_object_matrix(*(unsigned int *)(entity + 0x80));

    float spinForce = FLOAT_806189f4;
    if (DAT_806109c0 == 0x32) {
        spinForce = FLOAT_806189f0;
    }
    double sf = (double)spinForce;

    float force[3];
    float angForce[3];
    angForce[1] = -(FLOAT_806189f8 * *(float *)(inst + 0x18));
    double s = (double)(float)(-(double)*(float *)(inst + 0x14) * sf);
    force[0] = (float)((double)mtx[0] * s);
    force[1] = (float)((double)mtx[1] * s);
    force[2] = (float)((double)mtx[2] * s);
    if ((double)FLOAT_806189d0 < (double)force[1]) {
        force[1] = (float)((double)force[1] * (double)FLOAT_806189dc);
    }
    angForce[0] = force[0]; angForce[2] = force[2];  // packed &local_118 group

    if ((subobj_is_idle(*(unsigned int *)(entity + 0x80)) == 0) &&
        (subobj_get_activation_frame(*(unsigned int *)(entity + 0x80)) < DAT_80611928 + -5)) {
        if ((DAT_805015c0 & 0x40) != 0) {
            if (*(int *)(inst + 0x20) == 0) {
                *(int *)(inst + 0x20) = 1;
                *(int *)(inst + 0x44) = *(int *)(entity + 100);
                *(int *)(inst + 0x48) = *(int *)(entity + 0x68);
                *(int *)(inst + 0x4c) = *(int *)(entity + 0x6c);
            }
            float peakH = *(float *)(inst + 0x3c);
            float curH = *(float *)(entity + 0x68) - *(float *)(inst + 0x48);
            if (peakH < curH) peakH = curH;
            *(float *)(inst + 0x3c) = peakH;

            float dy = *(float *)(entity + 0x68) - *(float *)(inst + 0x48);
            float dx = *(float *)(entity + 100) - *(float *)(inst + 0x44);
            float dz = *(float *)(entity + 0x6c) - *(float *)(inst + 0x4c);
            double dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
            *(float *)(inst + 0x40) = (float)(dist < 0 ? -dist : dist);
            *(int *)(inst + 0x38) = DAT_80611928
                                    - subobj_get_activation_frame(*(unsigned int *)(entity + 0x80));
        }
        if ((subobj_get_activation_frame(*(unsigned int *)(entity + 0x80))
                < DAT_80611928 - DAT_806109c0) &&
            (angForce[2] = -(FLOAT_806189f8 * *(float *)(inst + 0x1c)),
             FLOAT_806189fc < (angForce[1] < 0 ? -angForce[1] : angForce[1]))) {
            subobj_add_angular_force(*(unsigned int *)(entity + 0x80), angForce);
        }
    } else {
        if (DAT_806109c0 < *(int *)(inst + 0x38)) {
            void *name;
            if (DAT_8060f948 == 4) {
                name = &DAT_80610f08;
            } else {
                name = (void *)get_string_by_id(0x177d);
            }
            time_format_long(0, 0, 0, sf, 0, 0, 0, 0);
            int s1 = get_string_by_id(0x15fd);
            int s2 = get_string_by_id(0x15ff);
            int s3 = get_string_by_id(0x15fe);
            unsigned int *text = (unsigned int *)debug_log_formatted(
                (double)*(float *)(inst + 0x3c), (double)*(float *)(inst + 0x40), 0, sf,
                0, 0, 0, 0, s__s___2f__s___s___2f__s___s__s_8046cddc,
                s3, name, s2, name, s1, (void *)0, 0);
            hud_text_submit(*(int *)(*(int *)(*(int *)(inst + 0x10) + 0x150) + 4), 2, text);
        }
        if (*(int *)(inst + 0x20) != 0) {
            int prec = *(int *)(*(int *)(inst + 0x10) + 0x150) + 4;
            int prc = *(int *)prec;
            double best = worm_stat_get_snapshot(7, prc);
            double thisTime = (double)(float)(double)(int)*(int *)(inst + 0x38);
            if (best < thisTime) {
                FUN_80237a70(thisTime, 7, prc);
                double total = worm_stat_get_snapshot(8, prc);
                FUN_80237a70((double)(float)(double)((int)total + *(int *)(inst + 0x38)), 8, prc);
            }
            best = worm_stat_get_snapshot(6, prc);
            if (best < (double)*(float *)(inst + 0x40)) {
                FUN_80237a70((double)*(float *)(inst + 0x40), 6, prc);
            }
            best = worm_stat_get_snapshot(5, prc);
            if (best < (double)*(float *)(inst + 0x3c)) {
                FUN_80237a70((double)*(float *)(inst + 0x3c), 5, prc);
            }
        }
        if (subobj_is_idle(*(unsigned int *)(entity + 0x80)) == 0) {
            subobj_get_activation_frame(*(unsigned int *)(entity + 0x80));
        }
        float zero = FLOAT_80618980;
        *(int *)(inst + 0x20) = 0;
        *(int *)(inst + 0x38) = 0;
        *(float *)(inst + 0x3c) = zero;
        *(float *)(inst + 0x40) = zero;
        if (*(int *)(inst + 4) == 3) {
            if (FLOAT_806189fc < (angForce[1] < 0 ? -angForce[1] : angForce[1])) {
                subobj_add_angular_force(*(unsigned int *)(entity + 0x80), angForce);
            }
            if (FLOAT_806189fc < force[2] * force[2] + force[0] * force[0] + force[1] * force[1]) {
                subobj_add_linear_force(*(unsigned int *)(entity + 0x80), force);
            }
        }
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// slot_apply_tip_torque (0x802444b0)
// While grabbed, drive the upright/tip state machine and apply the corresponding
// torque: upright hold (4) tips into 5/6/7 when knocked, and 5/6/7 apply a torque
// couple about the contact origin to spin/topple the machine.
// -----------------------------------------------------------------------------
void slot_apply_tip_torque(void)
{
    unsigned long long packed = (unsigned long long)FUN_803826e8();
    int entity = (int)(packed >> 0x20);
    unsigned int body = *(unsigned int *)(entity + 0x80);
    int inst = *(int *)(entity + 0x7c);
    int mtx = subobj_get_object_matrix(body);
    double invMass = subobj_get_inverse_mass(body);
    float zero = FLOAT_80618980;

    float at[3];
    float f[3];

    int state = *(int *)(inst + 4);
    if (((state == 4) || ((unsigned int)(state - 6) < 2) || (state == 5)) &&
        (FLOAT_80618a00 < *(float *)(mtx + 0x14))) {
        *(int *)(inst + 4) = *(int *)(inst + 8);
        *(float *)(inst + 0x30) = zero;
    }

    if ((FLOAT_80618a04 < *(float *)(inst + 0x14)) || (*(int *)(inst + 4) != 4) ||
        ((packed & 0x20) == 0) || (*(int *)(inst + 0x20) != 0)) {
        state = *(int *)(inst + 4);
        if ((state == 5) || (state == 6) || (state == 7)) {
            float *n = (float *)subobj_get_contact_normal(*(unsigned int *)(entity + 0x80));
            *(float *)(inst + 0x30) =
                FLOAT_806108ec *
                (FLOAT_806189b4 /
                 (float)((double)(long long)(unsigned int)(DAT_806109c0 ^ 0x80000000)
                         - DOUBLE_80618990)) + *(float *)(inst + 0x30);
            subobj_dirty_list_add(body);
            if (n[2] * n[2] + n[0] * n[0] + n[1] * n[1] < FLOAT_80618a0c) {
                float *origin = (float *)subobj_get_contact_origin(*(unsigned int *)(entity + 0x80));
                int st = *(int *)(inst + 4);
                int axisOff = (~-(unsigned int)(st == 5) & 2) * 0x10;
                int row = mtx + axisOff;
                at[0] = FLOAT_806189cc * *(float *)(mtx + axisOff) + origin[0];
                at[1] = FLOAT_806189cc * *(float *)(row + 4) + origin[1];
                at[2] = FLOAT_806189cc * *(float *)(row + 8) + origin[2];
                if (st == 6) {
                    float k = (float)((double)FLOAT_80618a10 * invMass);
                    f[0] = *(float *)(mtx + 0x10) * k;
                    f[1] = *(float *)(mtx + 0x14) * k;
                    f[2] = *(float *)(mtx + 0x18) * k;
                } else if (st == 7) {
                    float k = (float)((double)FLOAT_80618a14 * invMass);
                    f[0] = *(float *)(mtx + 0x10) * k;
                    f[1] = *(float *)(mtx + 0x14) * k;
                    f[2] = *(float *)(mtx + 0x18) * k;
                } else {
                    unsigned int sign = *(unsigned int *)(mtx + 4);
                    float k = (float)(invMass * (double)(FLOAT_80618a14 *
                              -(float)(sign & 0x80000000 | 0x3f800000)));
                    f[0] = *(float *)(mtx + 0x10) * k;
                    f[1] = *(float *)(mtx + 0x14) * k;
                    f[2] = *(float *)(mtx + 0x18) * k;
                }
                subobj_add_torque_from_offset(body, at, f);
                f[0] *= FLOAT_806189c4;
                f[1] *= FLOAT_806189c4;
                at[0] = (at[0] - origin[0]) * FLOAT_806189c4 + origin[0];
                f[2] *= FLOAT_806189c4;
                at[1] = (at[1] - origin[1]) * FLOAT_806189c4 + origin[1];
                at[2] = (at[2] - origin[2]) * FLOAT_806189c4 + origin[2];
                subobj_add_torque_from_offset(body, at, f);
            }
        }
    } else if ((*(float *)(mtx + 4) < 0 ? -*(float *)(mtx + 4) : *(float *)(mtx + 4))
               <= FLOAT_80618a08) {
        if (*(float *)(mtx + 0x24) <= FLOAT_80618980) {
            *(int *)(inst + 4) = 7;
        } else {
            *(int *)(inst + 4) = 6;
        }
    } else {
        *(int *)(inst + 4) = 5;
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// slot_release_all_players (0x8024488c)
// Release every engaged player across the instance pool (4 records, stride 0x5c).
// -----------------------------------------------------------------------------
void slot_release_all_players(void)
{
    FUN_803826f0();
    int off = 0;
    for (int i = 0; i < 4; i++) {
        int head = freelist_peek_head(&DAT_805cad30);
        if (*(int *)(head + off + 4) != 0) {
            slot_release_grab(*(int *)(head + off + 0xc));
        }
        off += 0x5c;
    }
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// slot_find_player_instance (0x802448f8)
// Find the instance record a given player (menu-state 0x1000) is engaged with.
// -----------------------------------------------------------------------------
void slot_find_player_instance(void)
{
    int player = FUN_803826e8();
    if (*(int *)(*(int *)(player + 0x150) + 0x114) == 0x1000) {
        int off = 0;
        for (int i = 0; i < 4; i++) {
            int head = freelist_peek_head(&DAT_805cad30);
            if ((*(int *)(head + off + 4) != 0) && (*(int *)(head + off + 0x10) == player)) {
                break;
            }
            off += 0x5c;
        }
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// slot_fx_pool_alloc (0x80244994)
// Allocate the secondary FX pool (0x20-byte records, 0x640 count) then reset it.
// -----------------------------------------------------------------------------
void slot_fx_pool_alloc(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                        unsigned long long a4, unsigned long long a5, unsigned long long a6,
                        unsigned long long a7, unsigned long long a8, int a12, int a13,
                        int a14, int a15, int a16)
{
    DAT_80612988 = mem_alloc_aligned(a1, a2, a3, a4, a5, a6, a7, a8, 0x20, 0x640, 1,
                                     a12, a13, a14, a15, a16);
    slot_fx_pool_reset();
}

// -----------------------------------------------------------------------------
// slot_fx_pool_reset (0x802449c8)
// Zero the FX pool and its 0x78-byte state block, seed default state values.
// -----------------------------------------------------------------------------
void slot_fx_pool_reset(void)
{
    memset_wrapper(DAT_80612988, 0, 0x640);
    memset_wrapper(-0x7fa352c0, 0, 0x78);
    DAT_805cad58 = FLOAT_80618a18;
    DAT_805cad5c = FLOAT_80618a1c;
    DAT_805cad60 = FLOAT_80618a20;
    DAT_805cad64 = FLOAT_80618a18;
    DAT_805cad68 = FLOAT_80618a18;
    DAT_805cad6c = FLOAT_80618a18;
    DAT_805cad74 = 0;
    DAT_805cad78 = FLOAT_80618a18;
    DAT_805cad88 = 0;
    DAT_805cada4 = -1;
}

// -----------------------------------------------------------------------------
// slot_fx_list_push (0x80244a54)
// Append an id to the FX registration ring (base DAT_805cad40, cursor DAT_805cad54).
// -----------------------------------------------------------------------------
void slot_fx_list_push(int id)
{
    (&DAT_805cad40)[DAT_805cad54] = id;
    DAT_805cad54 += 1;
}

} // extern "C"
