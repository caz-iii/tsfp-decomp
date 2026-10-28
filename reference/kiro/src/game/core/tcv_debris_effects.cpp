// tcv_debris_effects.cpp
// TCV boss set-piece: wheel/node spin animation, skeleton matrix update, spin-angle
// smoothing, "all settled" gate check, debris-entity spawning with a physics impulse,
// a fuse countdown that detonates into shell-casings + explosions, the phase FSM
// handler, and small explosion spawn helpers.
//
// Callers (all in the same TCV family, defined in tcv_boss_sequence.cpp / earlier
// batches): tcv_render_matrix_update (0x8024e158), tcv_master_fsm_tick (0x8024e1e8),
// tcv_big_finale (0x8024ea90), and worm_gate_rig_update (0x801d3ecc).
//
// Address range: 0x802512f0 - 0x802520cc  (batch_727)
//
// Self-contained translation unit: all raw ROM symbols and cross-module helper
// prototypes are declared file-locally with extern "C" linkage matching the ROM's
// unmangled symbol names. Ghidra intrinsics are provided as file-local inlines.

typedef unsigned char      undefined1;
typedef unsigned int       undefined4;
typedef unsigned long long undefined8;
typedef unsigned int       uint;

static inline float  SQRT(float x)  { return __builtin_sqrtf(x); }
static inline float  ABSf(float x)  { return x < 0.0f ? -x : x; }
static inline double ABSd(double x) { return x < 0.0  ? -x : x; }
static inline unsigned long long CONCAT44(unsigned int hi, unsigned int lo) {
    return ((unsigned long long)hi << 32) | (unsigned long long)lo;
}
static inline unsigned int countLeadingZeros(unsigned int x) {
    unsigned int n = 0;
    if (x == 0) return 32u;
    while ((x & 0x80000000u) == 0u) { x <<= 1; ++n; }
    return n;
}

extern "C" {
// ---- cross-module helpers (file-local, ROM signatures per call site) ----
unsigned long long FUN_803826e8(void);   // aim-tick context enter (packed target|self)
void FUN_80382734(void);                 // aim-tick context leave
unsigned long long __save_gpr_28(void);  // reset prologue: returns packed (dir|pos) ptrs
void __restore_gpr_28(void);
double math_sqrt(double x);

void tcv_apply_node_spin(double ax, double ay, double az, int ctx, int node, uint flags);
void tcv_set_phase(int phase, int arg);

void mtx4_set_translation(double x, double y, double z);
void mtx4_set_rotation_x(double angle, int outMtx);
void mtx4_multiply(float *a, float *b, float *out);

int  trigger_slot_resolve_entity(int fourcc);
uint trigger_is_idle(int entity);
void statlvl_class_arm(int cls);
void statlvl_class_complete(double a1, undefined8 a2, undefined8 a3, undefined8 a4,
                            undefined8 a5, undefined8 a6, undefined8 a7, undefined8 a8);

int  entity_spawn_with_activation(double a1, double a2, double a3, undefined8 a4, undefined8 a5,
                                  undefined8 a6, undefined8 a7, undefined8 a8, int a9,
                                  undefined4 a10, float *a11, float *a12, int a13, int a14);
void entity_register_for_update(int entity, int slot, int callback);
void entity_destroy_with_event(void *entity);

int  subobj_pool_alloc(void);
void subobj_build_descriptor_default(undefined8 a1, double a2, double a3, undefined8 a4,
                                     undefined8 a5, undefined8 a6, undefined8 a7, undefined8 a8,
                                     undefined4 a9, int a10, int a11, int a12, double a13);
void subobj_set_mass(double mass, uint handle);
int  subobj_get_contact_origin(uint handle);
void subobj_add_force_at_point(uint handle, float *point, float *force);

uint rng_counter_tick(void);
void shell_casing_spawn_batch(double a1, double a2, double a3, double a4, undefined8 a5,
                              undefined8 a6, undefined8 a7, undefined8 a8, int a9,
                              float *pos, float *tint, uint count);
void player_ai_explosion_spawn(int type, int subtype, float *pos, float *tint,
                               int a5, int a6, int a7);

// ---- raw globals (file-local, typed per call site) ----
extern int   DAT_805cb590;   // ptr to anim state block A
extern int   DAT_805cb594;   // ptr to anim state block B
extern uint  DAT_805cb5a0;   // spin phase counter
extern float DAT_805cb5a4;   // smoothed spin rate
extern float DAT_805cb5a8;   // current spin angle (target)
extern double DAT_805cb5ac;  // rig translation Z
extern int   DAT_805cb5bc;
extern int   DAT_805cb5c4;   // phase/state selector
extern int   DAT_805cb5c8;
extern int   DAT_805cb5cc;
extern int   DAT_805cb5d0;
extern int   DAT_805cb5e8;   // active member record ptr
extern undefined4 DAT_805cb904, DAT_805cb908, DAT_805cb90c;
extern undefined4 DAT_805cb910, DAT_805cb914, DAT_805cb918;

extern int   DAT_80612610;   // entity list count
extern int   DAT_80611d78;   // entity list base
extern int   DAT_80611d74;   // active session record base
extern uint  DAT_806109c0;   // frame/tick counter
extern int   DAT_806108e4;   // wind sample count
extern float DAT_806108ec;   // spin decay coefficient
extern undefined4 DAT_80610f10; // debris default arg A
extern int   iRam80610f14;      // debris default arg B

extern float DAT_803e0484, DAT_803e0488, DAT_803e048c; // explosion tint A (r,g,b)
extern float DAT_803e0490;                             // explosion tint B r
extern undefined4 DAT_803e0494, DAT_803e0498;          // explosion tint B g,b
// staged TCV-member detonation position/tint pool (pos xyz + tint xyz per entry)
extern float DAT_803e049c, DAT_803e04a8;
extern undefined4 DAT_803e04a0, DAT_803e04a4, DAT_803e04ac, DAT_803e04b0;
extern float DAT_803e04b4, DAT_803e04c0;
extern undefined4 DAT_803e04b8, DAT_803e04bc, DAT_803e04c4, DAT_803e04c8;
extern float DAT_803e04cc, DAT_803e04d8;
extern undefined4 DAT_803e04d0, DAT_803e04d4, DAT_803e04dc, DAT_803e04e0;
extern float DAT_803e04e4, DAT_803e04f0;
extern undefined4 DAT_803e04e8, DAT_803e04ec, DAT_803e04f4, DAT_803e04f8;
extern int DAT_805cb810;   // TCV member particle-emitter definition

void particle_emitter_spawn(int a1, int *def, int a3, float *pos, float *tint);

extern unsigned char DAT_80583228; // 0x2000-byte sine/cosine LUT (float[])

// ---- float/double literal pool ----
extern float FLOAT_80618c2c, FLOAT_80618c40, FLOAT_80618c44, FLOAT_80618c54, FLOAT_80618c58;
extern float FLOAT_80618c5c, FLOAT_80618c64, FLOAT_80618c70, FLOAT_80618c74, FLOAT_80618c80;
extern float FLOAT_80618c8c, FLOAT_80618c98, FLOAT_80618ca8, FLOAT_80618cac, FLOAT_80618cb0;
extern float FLOAT_80618cc8, FLOAT_80618ce4, FLOAT_80618d24, FLOAT_80618d38, FLOAT_80618d3c;
extern float FLOAT_80618d74, FLOAT_80618d78, FLOAT_80618d7c, FLOAT_80618d80, FLOAT_80618d84;
extern float FLOAT_80618d88, FLOAT_80618d8c;
extern double DOUBLE_80618c38, DOUBLE_80618d48;

// ---- forward decls for intra-file references ----
void FUN_802517dc(undefined8 p1, undefined8 p2, double p3, undefined8 p4, undefined8 p5,
                  undefined8 p6, undefined8 p7, undefined8 p8, undefined4 p9, undefined4 p10,
                  undefined4 p11, int p12);
void FUN_802520cc(float *pos);

// ============================================================================
// 0x802512f0  tcv_apply_wheel_spins
// Applies scaled node spins across the 5 TCV members (stride 0x4c). Member index
// 4 additionally sets a rig translation and multiplies a bone matrix.
// in_f30/in_f31 (paired-single) arrive as incoming FP registers -> params.
// ============================================================================
void FUN_802512f0(double in_f30, double in_f31, double in_ps30_1, double in_ps31_1)
{
    int    ctx;
    float *pf;
    float *bone;
    int    i;
    int    off;
    double dScale;   // FLOAT_80618cac
    double dDiv;     // FLOAT_80618c5c
    float  local_18, fStack_14, local_8, fStack_4;

    local_8   = (float)in_f31;
    fStack_4  = (float)in_ps31_1;
    local_18  = (float)in_f30;
    fStack_14 = (float)in_ps30_1;
    (void)local_8; (void)fStack_4; (void)local_18; (void)fStack_14;

    ctx    = (int)FUN_803826e8();
    dScale = (double)FLOAT_80618cac;
    dDiv   = (double)FLOAT_80618c5c;
    i   = 0;
    off = 0;
    do {
        pf = (float *)(DAT_805cb590 + off);
        tcv_apply_node_spin((double)FLOAT_80618c44,
                            (double)(float)((double)(float)(dScale * (double)*pf) / dDiv),
                            (double)FLOAT_80618c44, ctx, (int)pf[9], (uint)pf[0xb]);
        tcv_apply_node_spin((double)FLOAT_80618c44,
                            (double)(float)((double)(float)(dScale * (double)*pf) / dDiv),
                            (double)FLOAT_80618c44, ctx, (int)pf[10], (uint)pf[0xb]);
        tcv_apply_node_spin((double)(float)((double)(float)(dScale * (double)pf[3]) / dDiv),
                            (double)FLOAT_80618c44, (double)FLOAT_80618c44, ctx, (int)pf[10],
                            (uint)pf[0xc]);
        if (i == 4) {
            mtx4_set_translation((double)FLOAT_80618c44, (double)FLOAT_80618c44,
                                 (double)DAT_805cb5ac);
            bone = (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + 0x180);
            mtx4_multiply((float *)0 /*afStack_78*/, bone, bone);
            tcv_apply_node_spin((double)FLOAT_80618c44,
                                (double)((FLOAT_80618cac * *pf) / FLOAT_80618c5c),
                                (double)FLOAT_80618c44, ctx, 5, (uint)pf[0xb]);
            tcv_apply_node_spin((double)((FLOAT_80618cac * pf[3]) / FLOAT_80618c5c),
                                (double)FLOAT_80618c44, (double)FLOAT_80618c44, ctx, 5,
                                (uint)pf[0xc]);
        }
        i   = i + 1;
        off = off + 0x4c;
    } while (i < 5);
    FUN_80382734();
}

// ============================================================================
// 0x80251468  tcv_update_node_matrices
// Rebuilds the shared parent transform (translate to pivot, rotate X by current
// spin angle, translate back) and multiplies it into every TCV bone matrix.
// ============================================================================
void FUN_80251468(void)
{
    int    ctx;
    int   *pRec;
    int    i;
    int    off;
    int    rec;
    float *pf;
    float  afStack_98[16];
    float  afStack_58[22];

    ctx  = (int)FUN_803826e8();
    pRec = *(int **)(*(int *)(ctx + 4) + 4);
    pf   = (float *)pRec[*pRec * -0x30 + 0xab];
    mtx4_set_translation((double)*pf, (double)pf[1], (double)pf[2]);
    mtx4_set_rotation_x((double)((FLOAT_80618cac * DAT_805cb5a8) / FLOAT_80618c5c), (int)afStack_58);
    mtx4_multiply(afStack_58, afStack_98, afStack_98);
    mtx4_set_translation(-(double)*pf, -(double)pf[1], -(double)pf[2]);
    mtx4_multiply(afStack_58, afStack_98, afStack_98);

    pf = (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + 0x3c0);
    mtx4_multiply(afStack_98, pf, pf);
    pf = (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + 0x440);
    mtx4_multiply(afStack_98, pf, pf);
    pf = (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + 0x140);
    mtx4_multiply(afStack_98, pf, pf);

    i   = 0;
    off = 0;
    do {
        rec = DAT_805cb590 + off;
        pf  = (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + *(int *)(rec + 0x24) * 0x40);
        mtx4_multiply(afStack_98, pf, pf);
        pf  = (float *)(*(int *)(*(int *)(ctx + 4) + 0x10) + *(int *)(rec + 0x28) * 0x40);
        mtx4_multiply(afStack_98, pf, pf);
        i   = i + 1;
        off = off + 0x4c;
    } while (i < 5);
    FUN_80382734();
}

// ============================================================================
// 0x802515d8  tcv_update_spin_angle
// Integrates the wheel spin rate from a sine-LUT delta and eases the current
// spin angle toward the target (DAT_805cb5a8), clamping the per-frame step.
// ============================================================================
void FUN_802515d8(void)
{
    float f;
    uint  uPrev;   // counter ^ 0x80000000  (signed->double via CONCAT44)
    uint  uNext;   // counter+1

    f = *(float *)(DAT_805cb594 + 0x2c);
    if (f < *(float *)(DAT_805cb594 + 0x14)) {
        f = *(float *)(DAT_805cb594 + 0x14);
    }
    uNext      = DAT_805cb5a0 + 1;
    uPrev      = DAT_805cb5a0 ^ 0x80000000;
    DAT_805cb5a0 = uNext;

    DAT_805cb5a4 =
        DAT_805cb5a4 * FLOAT_80618d74 +
        (f * FLOAT_80618c8c *
             (*(float *)((int)&DAT_80583228 +
                         (((int)(FLOAT_80618ca8 *
                                 ((FLOAT_80618d38 *
                                   (float)((double)CONCAT44(0x43300000, uNext ^ 0x80000000) -
                                           DOUBLE_80618c38)) /
                                  FLOAT_80618ce4) + FLOAT_80618c74) + 0x200) * 4 & 0x1ffcU)) -
              *(float *)((int)&DAT_80583228 +
                         (((int)(FLOAT_80618ca8 *
                                 ((FLOAT_80618d38 *
                                   (float)((double)CONCAT44(0x43300000, uPrev) -
                                           DOUBLE_80618c38)) /
                                  FLOAT_80618ce4) + FLOAT_80618c74) + 0x200) * 4 & 0x1ffcU)))) /
        FLOAT_80618c98;

    f = DAT_805cb5a4;
    if (FLOAT_80618c58 <= ABSf(DAT_805cb5a8 - DAT_805cb5a4)) {
        if (DAT_805cb5a4 <= DAT_805cb5a8) {
            f = DAT_805cb5a8 - FLOAT_80618c74;
        } else {
            f = DAT_805cb5a8 + FLOAT_80618c74;
        }
    }
    DAT_805cb5a8 = f;
}

// ============================================================================
// 0x80251718  tcv_check_all_settled
// Returns 1 (and arms class 5) once the CPDR trigger entity is idle and no live
// entity still has residual height above the threshold; 0 otherwise.
// ============================================================================
undefined4 FUN_80251718(void)
{
    int  entity;
    uint idle;
    int  count;
    int  off;

    entity = trigger_slot_resolve_entity(0x43504452 /* "CPDR" */);
    idle   = trigger_is_idle(entity);
    if (idle == 0) {
        return 0;
    }
    off   = 0;
    count = DAT_80612610;
    if (0 < DAT_80612610) {
        do {
            if ((FLOAT_80618c44 < *(float *)(*(int *)(DAT_80611d78 + off + 0x14) + 0x1cc)) &&
                (*(int *)(DAT_80611d78 + off + 0x1400) != 0x15)) {
                return 0;
            }
            off   = off + 0x141c;
            count = count + -1;
        } while (count != 0);
    }
    statlvl_class_arm(5);
    return 1;
}

// ============================================================================
// 0x802517b0  tcv_spawn_debris_default
// Thin wrapper: forwards to tcv_spawn_debris with the two default trailing args
// pulled from globals (DAT_80610f10 / iRam80610f14).
// ============================================================================
void FUN_802517b0(undefined8 p1, undefined8 p2, double p3, undefined8 p4, undefined8 p5,
                  undefined8 p6, undefined8 p7, undefined8 p8, undefined4 p9, undefined4 p10)
{
    FUN_802517dc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, DAT_80610f10, iRam80610f14);
}

// ============================================================================
// 0x802517dc  tcv_spawn_debris
// Spawns a debris entity and, if it has an attached sub-object, computes a
// randomized launch impulse (biased away from the player, wind-limited) and
// applies it before registering the entity for update.
// __save_gpr_28 returns a packed (dir ptr | pos ptr) pair.
// ============================================================================
void FUN_802517dc(undefined8 p1, undefined8 p2, double p3, undefined8 p4, undefined8 p5,
                  undefined8 p6, undefined8 p7, undefined8 p8, undefined4 p9, undefined4 p10,
                  undefined4 p11, int p12)
{
    float  fVar1, fVar2;
    int    ent;
    int    subHandle;
    float *pDir;
    float *pPos;
    double extraout_f1 = (double)FLOAT_80618c44; // incoming FP arg (magnitude)
    double dVar7, dVar8;
    unsigned long long packed;
    double in_f30 = 0.0, in_f31 = 0.0, in_ps30_1 = 0.0, in_ps31_1 = 0.0;
    float  local_88, local_84, local_80, local_7c, local_78, local_74, local_70, local_6c, local_68;
    float  local_18, fStack_14, local_8, fStack_4;
    uint   uRng;

    local_8   = (float)in_f31;
    fStack_4  = (float)in_ps31_1;
    local_18  = (float)in_f30;
    fStack_14 = (float)in_ps30_1;
    (void)local_8; (void)fStack_4; (void)local_18; (void)fStack_14; (void)p1; (void)p2;

    packed = __save_gpr_28();
    pDir = (float *)(unsigned int)(packed >> 0x20);
    pPos = (float *)(unsigned int)packed;

    dVar7    = (double)FLOAT_80618c44;
    local_70 = FLOAT_80618c44;
    local_6c = (float)((double)(float)((double)FLOAT_80618c5c * extraout_f1) / (double)FLOAT_80618cac);
    local_68 = FLOAT_80618c44;
    (void)local_6c; (void)local_68;

    ent = entity_spawn_with_activation(
              (double)(float)((double)FLOAT_80618c5c * extraout_f1), dVar7, p3, p4, p5, p6, p7, p8,
              1, p11, pDir, &local_70, 0, 0);

    fVar2 = FLOAT_80618c2c;
    *(uint *)(ent + 0x28) = *(uint *)(ent + 0x28) | 0x1062;
    *(uint *)(ent + 0x18) = *(uint *)(ent + 0x18) | 2;
    fVar1 = *(float *)(ent + 0x1c4);
    if (fVar1 < fVar2) {
        fVar1 = fVar2;
    }
    *(float *)(ent + 0x68) = *(float *)(ent + 0x68) + fVar1;

    subHandle = subobj_pool_alloc();
    *(int *)(ent + 0x80) = subHandle;
    if (subHandle == 0) {
        uRng = rng_counter_tick();
        *(float *)(ent + 0x1a4) =
            FLOAT_80618d24 *
                ((float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) / FLOAT_80618d3c) +
            FLOAT_80618d24;
        entity_register_for_update(ent, 3, -0x7fdae1c4);
        __restore_gpr_28();
        return;
    }

    subobj_build_descriptor_default((double)FLOAT_80618c44, dVar7, p3, p4, p5, p6, p7, p8,
                                    *(undefined4 *)(ent + 0x80), ent, 1, p12, (double)2.950753e-39f);
    subobj_set_mass((double)FLOAT_80618d78, *(uint *)(ent + 0x80));

    uRng    = DAT_806109c0 ^ 0x80000000;
    local_80 = FLOAT_80618d78 * (float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618c38);
    local_88 = (-(*pDir - *(float *)(DAT_80611d74 + 0xb4)) / FLOAT_80618c54) * local_80;
    local_84 = (FLOAT_80618d7c + -(pDir[1] - *(float *)(DAT_80611d74 + 0xb8)) / FLOAT_80618c54) *
               local_80;
    local_80 = (-(pDir[2] - *(float *)(DAT_80611d74 + 0xbc)) / FLOAT_80618c54) * local_80;

    uRng  = rng_counter_tick();
    dVar7 = (double)(FLOAT_80618c40 *
                     ((float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) / FLOAT_80618d3c -
                      FLOAT_80618c74));
    rng_counter_tick();
    uRng  = rng_counter_tick();
    local_88 = local_88 + (float)(dVar7 * (double)FLOAT_80618d80);
    local_84 = local_84 + FLOAT_80618c44;
    local_80 = local_80 +
               (float)((double)(FLOAT_80618c40 *
                                ((float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) /
                                 FLOAT_80618d3c - FLOAT_80618c74)) * (double)FLOAT_80618d80);

    dVar7 = (double)(local_80 * local_80 + local_88 * local_88 + local_84 * local_84);
    if (((double)FLOAT_80618d84 < dVar7) &&
        (dVar7 = math_sqrt(dVar7), (float)((double)local_84 / dVar7) < FLOAT_80618cc8)) {
        uRng = rng_counter_tick();
        local_88 = FLOAT_80618c40 *
                   ((float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) / FLOAT_80618d3c -
                    FLOAT_80618c74);
        uRng = rng_counter_tick();
        local_84 = FLOAT_80618c40 *
                   ((float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) / FLOAT_80618d3c -
                    FLOAT_80618c74);
        uRng = rng_counter_tick();
        local_88 = local_88 * FLOAT_80618d88 + *pPos;
        local_84 = (FLOAT_80618c74 * ABSf(local_84) + FLOAT_80618c74) * FLOAT_80618d88 + pPos[1];
        local_80 = FLOAT_80618c40 *
                   ((float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) / FLOAT_80618d3c -
                    FLOAT_80618c74) * FLOAT_80618d88 + pPos[2];
    }

    if (0 < DAT_806108e4) {
        dVar7 = math_sqrt((double)(local_80 * local_80 + local_88 * local_88 + local_84 * local_84));
        uRng  = DAT_806108e4 ^ 0x80000000;
        dVar8 = (double)(FLOAT_80618d8c /
                         (float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618c38));
        if (dVar8 < dVar7) {
            fVar1 = local_80 * local_80 + local_88 * local_88 + local_84 * local_84;
            if (FLOAT_80618cb0 < fVar1) {
                fVar2 = 1.0f / SQRT(fVar1);
                fVar2 = fVar2 * -(FLOAT_80618c74 * fVar1 * fVar2 * fVar2 - FLOAT_80618c8c);
                fVar1 = FLOAT_80618c2c * fVar2 *
                        -(FLOAT_80618c74 * fVar1 * fVar2 * fVar2 - FLOAT_80618c8c);
                local_88 = local_88 * fVar1;
                local_84 = local_84 * fVar1;
                local_80 = local_80 * fVar1;
            }
            local_88 = (float)((double)local_88 * dVar8);
            local_84 = (float)((double)local_84 * dVar8);
            local_80 = (float)((double)local_80 * dVar8);
        }
    }

    uRng  = rng_counter_tick();
    fVar1 = (float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) / FLOAT_80618d3c -
            FLOAT_80618c74;
    uRng  = rng_counter_tick();
    local_78 = FLOAT_80618c44;
    local_7c = FLOAT_80618c74 *
               (FLOAT_80618c64 *
                    ((float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) / FLOAT_80618d3c) +
                FLOAT_80618c64) *
               (float)(((uint)fVar1 & 0x80000000) | 0x3f800000);
    uRng  = rng_counter_tick();
    fVar1 = (float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) / FLOAT_80618d3c -
            FLOAT_80618c74;
    uRng  = rng_counter_tick();
    local_74 = FLOAT_80618c74 *
               (FLOAT_80618c64 *
                    ((float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) / FLOAT_80618d3c) +
                FLOAT_80618c64) *
               (float)(((uint)fVar1 & 0x80000000) | 0x3f800000);

    pPos = (float *)subobj_get_contact_origin(*(uint *)(ent + 0x80));
    local_7c = local_7c + *pPos;
    local_78 = local_78 + pPos[1];
    local_74 = local_74 + pPos[2];
    subobj_add_force_at_point(*(uint *)(ent + 0x80), &local_7c, &local_88);
    *(float *)(ent + 0x1cc) = FLOAT_80618c44;

    uRng = rng_counter_tick();
    *(float *)(ent + 0x1a4) =
        FLOAT_80618c70 *
            ((float)((double)CONCAT44(0x43300000, uRng) - DOUBLE_80618d48) / FLOAT_80618d3c) +
        FLOAT_80618c80;
    entity_register_for_update(ent, 3, -0x7fdae1c4);
    __restore_gpr_28();
}

// ============================================================================
// 0x80251e3c  tcv_fuse_tick_explode
// Per-frame fuse countdown on a debris entity. When the fuse (offset 0x69)
// reaches zero it spawns a shell-casing burst plus two explosions and destroys
// the entity.
// ============================================================================
void FUN_80251e3c(undefined4 *ent)
{
    float  fZero;
    uint   uRng;
    double dDecay;
    double dInvCount;
    double dScale;
    undefined8 in_f5 = 0, in_f6 = 0, in_f7 = 0, in_f8 = 0;
    float  local_28, local_1c, local_18, local_14;
    undefined4 local_24, local_20;
    uint   uStack_c;

    fZero  = FLOAT_80618c44;
    dScale = (double)FLOAT_80618c2c;
    uStack_c = DAT_806109c0 ^ 0x80000000;
    dDecay = (double)DAT_806108ec;
    dInvCount = (double)(float)(dScale / (double)(float)((double)CONCAT44(0x43300000, uStack_c) -
                                                         DOUBLE_80618c38));

    ent[0x69] = (undefined4)(int)(long long)-(float)(dDecay * dInvCount -
                                                     (double)(float)(int)ent[0x69]);
    if ((float)(int)ent[0x69] <= fZero) {
        local_28 = DAT_803e0484;
        local_24 = *(undefined4 *)&DAT_803e0488;
        local_20 = *(undefined4 *)&DAT_803e048c;
        local_1c = (float)(int)ent[0x19] + (float)(int)ent[0x6a];
        local_18 = (float)(int)ent[0x1a] + (float)(int)ent[0x6b];
        local_14 = (float)(int)ent[0x1b] + (float)(int)ent[0x6c];
        (void)local_24; (void)local_20;
        uRng = rng_counter_tick();
        shell_casing_spawn_batch((double)(float)(int)ent[0x86], dInvCount, dScale, dDecay,
                                 in_f5, in_f6, in_f7, in_f8, 0, &local_1c, &local_28,
                                 uRng % 6 + 8);
        player_ai_explosion_spawn(0x12, 0x17, &local_1c, &local_28, 0, 0, 1);
        local_18 = local_18 - FLOAT_80618c2c;
        player_ai_explosion_spawn(0x16, 0x17, &local_1c, &local_28, 0, 0, 1);
        entity_destroy_with_event(ent);
    }
}

// ============================================================================
// 0x80251f8c  tcv_phase_handler
// Phase FSM notification handler. p10 selects the phase transition:
//   0 -> reset all state; 1 -> mark member; 2 -> capture member snapshot;
//   3 -> completion. p9 is the member/context pointer.
// ============================================================================
void FUN_80251f8c(undefined8 p1, undefined8 p2, undefined8 p3, undefined8 p4, undefined8 p5,
                  undefined8 p6, undefined8 p7, undefined8 p8, int p9, int p10)
{
    if (p10 == 2) {
        if ((DAT_805cb5c4 == 10) && (DAT_805cb5e8 != 0)) {
            *(undefined1 *)(p9 + 10) = 1;
            DAT_805cb904 = *(undefined4 *)(DAT_805cb5e8 + 100);
            DAT_805cb908 = *(undefined4 *)(DAT_805cb5e8 + 0x68);
            DAT_805cb90c = *(undefined4 *)(DAT_805cb5e8 + 0x6c);
            DAT_805cb910 = *(undefined4 *)(DAT_805cb5e8 + 0xc0);
            DAT_805cb914 = *(undefined4 *)(DAT_805cb5e8 + 0x70);
            DAT_805cb918 = *(undefined4 *)(DAT_805cb5e8 + 200);
        }
    } else if (p10 < 2) {
        if (p10 == 0) {
            DAT_805cb5bc = 0;
            DAT_805cb5c4 = 0;
            DAT_805cb5cc = 0;
            DAT_805cb5d0 = 0;
            DAT_805cb5c8 = 0;
        } else if ((-1 < p10) && (*(char *)(p9 + 10) != '\0')) {
            tcv_set_phase(10, 0);
            *(undefined4 *)(DAT_80611d74 + 0xe8c) = 0;
        }
    } else if ((p10 < 4) && (DAT_805cb5c4 == 10)) {
        statlvl_class_complete((double)FLOAT_80618c44, p2, p3, p4, p5, p6, p7, p8);
    }
}

// ============================================================================
// 0x802520b0  tcv_is_phase_ten
// Returns 1 when the phase selector equals 10, else 0 (branchless via clz).
// ============================================================================
uint FUN_802520b0(void)
{
    return countLeadingZeros((uint)(10 - DAT_805cb5c4)) >> 5;
}

// ============================================================================
// 0x802520cc  tcv_spawn_explosion_at
// Spawns a single type-0x1c explosion at the supplied position with tint B.
// ============================================================================
void FUN_802520cc(float *pos)
{
    float      local_18;
    undefined4 local_14, local_10;

    local_18 = DAT_803e0490;
    local_14 = *(undefined4 *)&DAT_803e0494;
    local_10 = *(undefined4 *)&DAT_803e0498;
    (void)local_14; (void)local_10;
    player_ai_explosion_spawn(0x1c, 0, pos, &local_18, 0, 0, 1);
}

// ============================================================================
// 0x8025212c  tcv_burst_explosion_a
// Fixed-position type-0x1c explosion using tint pair (DAT_803e049c.. / 04a8..).
// One of the staged TCV member detonations fired by tcv_render_callback.
// ============================================================================
void FUN_8025212c(void)
{
    float      local_1c, local_28;
    undefined4 local_18, local_14, local_24, local_20;

    local_1c = DAT_803e049c;
    local_18 = *(undefined4 *)&DAT_803e04a0;
    local_14 = *(undefined4 *)&DAT_803e04a4;
    local_28 = DAT_803e04a8;
    local_24 = *(undefined4 *)&DAT_803e04ac;
    local_20 = *(undefined4 *)&DAT_803e04b0;
    (void)local_18; (void)local_14; (void)local_24; (void)local_20;
    player_ai_explosion_spawn(0x1c, 0, &local_1c, &local_28, 0, 0, 1);
}

// ============================================================================
// 0x802521b0  tcv_burst_emitter_b
// Spawns a particle emitter (&DAT_805cb810) plus a type-0x13 explosion at a
// fixed position (DAT_803e04b4.. / 04c0..).
// ============================================================================
void FUN_802521b0(void)
{
    float      local_c, local_18;
    undefined4 local_8, local_4, local_14, local_10;

    local_c  = DAT_803e04b4;
    local_8  = *(undefined4 *)&DAT_803e04b8;
    local_4  = *(undefined4 *)&DAT_803e04bc;
    local_18 = DAT_803e04c0;
    local_14 = *(undefined4 *)&DAT_803e04c4;
    local_10 = *(undefined4 *)&DAT_803e04c8;
    (void)local_8; (void)local_4; (void)local_14; (void)local_10;
    particle_emitter_spawn(0, &DAT_805cb810, 0, &local_c, &local_18);
    player_ai_explosion_spawn(0x13, 0, &local_c, &local_18, 0, 0, 1);
}

// ============================================================================
// 0x8025224c  tcv_burst_emitter_c
// As tcv_burst_emitter_b, at position DAT_803e04cc.. / 04d8...
// ============================================================================
void FUN_8025224c(void)
{
    float      local_c, local_18;
    undefined4 local_8, local_4, local_14, local_10;

    local_c  = DAT_803e04cc;
    local_8  = *(undefined4 *)&DAT_803e04d0;
    local_4  = *(undefined4 *)&DAT_803e04d4;
    local_18 = DAT_803e04d8;
    local_14 = *(undefined4 *)&DAT_803e04dc;
    local_10 = *(undefined4 *)&DAT_803e04e0;
    (void)local_8; (void)local_4; (void)local_14; (void)local_10;
    particle_emitter_spawn(0, &DAT_805cb810, 0, &local_c, &local_18);
    player_ai_explosion_spawn(0x13, 0, &local_c, &local_18, 0, 0, 1);
}

// ============================================================================
// 0x802522e8  tcv_burst_emitter_d
// As tcv_burst_emitter_b, at position DAT_803e04e4.. / 04f0...
// ============================================================================
void FUN_802522e8(void)
{
    float      local_c, local_18;
    undefined4 local_8, local_4, local_14, local_10;

    local_c  = DAT_803e04e4;
    local_8  = *(undefined4 *)&DAT_803e04e8;
    local_4  = *(undefined4 *)&DAT_803e04ec;
    local_18 = DAT_803e04f0;
    local_14 = *(undefined4 *)&DAT_803e04f4;
    local_10 = *(undefined4 *)&DAT_803e04f8;
    (void)local_8; (void)local_4; (void)local_14; (void)local_10;
    particle_emitter_spawn(0, &DAT_805cb810, 0, &local_c, &local_18);
    player_ai_explosion_spawn(0x13, 0, &local_c, &local_18, 0, 0, 1);
}

} // extern "C"
