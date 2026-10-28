// Address range: 0x80149d88 - 0x8014b028 | batch_417 | 2026-09-05
//
// Entity render sub-object BREAK / FRACTURE + impact-damage callbacks
// (companion to subobj_pool/build/dynamics/solver/collision/solver_dispatch,
// batches 409-416).
//
// These are the per-sub-object per-frame update callbacks registered by
// subobj_build_descriptor plus the fracture spawner that shatters a parent mesh
// into independent physics sub-objects:
//
//   * subobj_set_flag_bits / _clear_flag_bits / _set_half_extent_x - descriptor
//        flag (+0x1c0) and AABB half-extent (+0x1c4) mutators.
//   * subobj_process_impact_damage  - spawn-frame impact callback: on a fast
//        enough contact, dispatch damage to the struck entity (and the reverse
//        body when flagged) then emit an impact ambient sound.
//   * subobj_get_restitution        - resolve the descriptor material bounce /
//        restitution term used by the break tick.
//   * subobj_break_tick / _impl     - per-frame health-decay + separation-force
//        update for a breaking-off piece; destroys the piece when health <= 0
//        (firing death effects / projectile-death / break-callback).
//   * subobj_apply_ground_impulse   - spawn-frame ground-contact callback that
//        accumulates torque (+0x128) and linear impulse (+0x14c) into the
//        descriptor and rebuilds the world contact normal (+0x134).
//   * subobj_fracture_spawn / _default - iterate a break-mask bitfield and, per
//        set bit, spawn a child entity + sub-object piece, build its matrix,
//        allocate a pool slot, and apply a scatter/directed breaking impulse.
//
// Descriptor byte offsets (see subobj_solver_dispatch.cpp for the full map):
//   +0x1c0 flags   +0x1c4 half-extent X   +0x1cc material byte   +0x1e4 aux handle
//   +0x40 world inverse-inertia   +0x128 angular-impulse accum   +0x14c linear
//   +0x134 world contact normal   +0x140 contact origin   +0x1a8 inverse mass

#include "common.h"
#include "generated_decls.h"

extern "C" {
// spawn-frame push/pop scratch (pop matches push)
extern unsigned long long FUN_803826f0(void); extern void FUN_8038273c(void);
extern unsigned long long FUN_803826c8(void); extern void FUN_80382714(void);
extern unsigned int       __save_gpr_28(void); extern void __restore_gpr_28(void);

// sound-material params lookup (sound-group -> params ptr)
extern int  FUN_80252384(unsigned int soundGroup);
// break-off secondary impulse dispatch + fx (self-externed, call-site arity)
extern int  FUN_8014b058(double a, double b, double c, double d, double e, double f, double g,
                         unsigned long long h, int handle, float* pos, float* dir);
extern int  FUN_801560c8(unsigned long long a, double b, double c, double d, double e, double f,
                         double g, unsigned long long h);
extern int  FUN_8014f024(unsigned int handle, int a, int b);   // piece flag/type setup
}

// Several shared helpers are declared in generated_decls.h with simplified
// prototypes (the wide-ABI float/spill params dropped). Call them through typed
// aliases matching the real PPC call sites in this module.
typedef int  (*slot_reset_fn)(int rec);
typedef void (*damage_dispatch_fn)(double a, double b, double c, double d, double e, double f,
                                   double g, unsigned long long h, int* rec);
typedef void*(*create_instance_fn)(unsigned long long f1, double p2, double p3, double p4, double p5,
                                   double p6, double p7, unsigned long long p8, unsigned int type,
                                   int posBase, float* spawnDef);
typedef void (*build_desc_default_fn)(double f1, double p2, double p3, double p4, double p5,
                                      double p6, double p7, unsigned long long p8, unsigned int h,
                                      int entity, unsigned int flags, int lod, float extra);
#define SLOT_RESET     ((slot_reset_fn)&switch_slot_reset)
#define DAMAGE_DISPATCH ((damage_dispatch_fn)&damage_dispatch_to_entity)
#define CREATE_INSTANCE ((create_instance_fn)&entity_create_instance)
#define BUILD_DESC_DEF  ((build_desc_default_fn)&subobj_build_descriptor_default)

// pool tables + material def
extern int    DAT_8061218c;    // descriptor pool base (stride 0x200)
extern int    DAT_80612180;    // per-slot 0x20-stride auxiliary table
extern int    DAT_80612184;    // per-slot owner-entity pointer table
extern int    DAT_8048ca94;    // area-effect / ground half-height table (stride 0x30)
extern int    DAT_8048ca9c;    // area-effect / ground offset table (stride 0x30)
extern int    DAT_803ccaf8;    // default spawn-def word 0
extern int    DAT_803ccafc;    // default spawn-def word 1
extern int    DAT_803ccb00;    // default spawn-def word 2

// shared float / double constants
extern float  FLOAT_80615ea8;  // 0.0f
extern float  FLOAT_80615eb0;  // 1.0f
extern float  FLOAT_80615ebc;  // 0.5f
extern float  FLOAT_80615ec4;  // length^2 epsilon
extern float  FLOAT_80615ec8;  // 3.0f (NR)
extern float  FLOAT_80615ee8;  // impact damage scale
extern float  FLOAT_80615f00;  // ambient sound param
extern float  FLOAT_80615f14;  // impact speed threshold
extern float  FLOAT_80615f18;  // ambient sound gain scale
extern float  FLOAT_80615f1c;  // ambient sound gain bias
extern float  FLOAT_80615f28;  // 2.0f-ish scatter range
extern float  FLOAT_80615f2c;  // ground-impulse scale
extern float  FLOAT_80615f30;  // fracture base speed
extern float  FLOAT_80615f34;  // fracture delayed-event time
extern float  FLOAT_80615f38;  // rng 15-bit divisor
extern float  FLOAT_80615f3c;  // fracture speed variance
extern double DOUBLE_80615f40; // 2^52 magic-double bias

// -----------------------------------------------------------------------------
// subobj_set_flag_bits (0x80149d88) - OR bits into descriptor flags +0x1c0.
// -----------------------------------------------------------------------------
void subobj_set_flag_bits(unsigned int handle, unsigned int bits)
{
    int desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    *(unsigned int*)(desc + 0x1c0) = *(unsigned int*)(desc + 0x1c0) | bits;
}

// -----------------------------------------------------------------------------
// subobj_clear_flag_bits (0x80149da4) - clear bits from descriptor flags +0x1c0.
// -----------------------------------------------------------------------------
void subobj_clear_flag_bits(unsigned int handle, unsigned int bits)
{
    int desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    *(unsigned int*)(desc + 0x1c0) = *(unsigned int*)(desc + 0x1c0) & ~bits;
}

// -----------------------------------------------------------------------------
// subobj_set_half_extent_x (0x80149dc0) - store descriptor half-extent word +0x1c4.
// -----------------------------------------------------------------------------
void subobj_set_half_extent_x(unsigned int handle, unsigned int value)
{
    *(unsigned int*)(DAT_8061218c + (handle & 0xffff) * 0x200 + 0x1c4) = value;
}

// -----------------------------------------------------------------------------
// subobj_process_impact_damage (0x80149dd4)
// Spawn-frame impact callback. When the descriptor is not sound-suppressed
// (flag 0x4000 clear) and the contact speed (aux slot *pfVar4) is below a cap,
// dispatch impact damage to the struck entity (flag 0x400 / 0x1000000 gates),
// and, if the other body opts in (flag 0x800 / peer flag 0x400), dispatch the
// reversed impulse to it. Always emits a speed-scaled ambient impact sound.
// -----------------------------------------------------------------------------
void subobj_process_impact_damage(unsigned long long selfArg, double p2, double p3,
                                  double p4, double p5, double p6, double p7,
                                  unsigned long long p8)
{
    unsigned int handle;
    unsigned int flags;
    unsigned int peerFlags;
    float* aux;
    int entity;
    int desc;
    double dmg;
    // HitRecord frame (matches switch_slot_reset layout)
    float hitOrigin;      // local_6c
    unsigned int hitPad;  // local_68
    float* hitDir;        // local_44
    unsigned int hitZero; // local_48
    float* hitOut;        // local_40
    float dirX, dirY, dirZ;  // local_78/74/70
    float amount;            // local_4c

    handle = (unsigned int)FUN_803826f0();
    desc = DAT_8061218c + (handle & 0xffff) * 0x200;
    flags = *(unsigned int*)(desc + 0x1c0);
    entity = *(int*)(DAT_80612184 + (handle & 0xffff) * 4);
    aux = (float*)(DAT_80612180 + (*(unsigned int*)(desc + 0x1e4) & 0xffff) * 0x20);

    if ((flags & 0x4000) == 0) {
        if (*aux < FLOAT_80615f14) {
            if (((flags & 0x8000000) == 0) || ((flags & 0x1000000) != 0)) {
                dmg = (double)SLOT_RESET((int)&hitOrigin);
                hitDir = aux + 1;
                hitZero = 0;
                hitOut = &dirX;
                dirX = aux[4];
                dirY = aux[5];
                dirZ = aux[6];
                if ((*(unsigned int*)(entity + 0x28) & 0x10) != 0) {
                    unsigned int f = *(unsigned int*)(desc + 0x1c0);
                    if ((f & 0x400) != 0) {
                        if (((f & 0x1000000) == 0) || ((f & 0x2000000) == 0)) {
                            dmg = (double)FLOAT_80615ee8;
                            amount = (float)(dmg * ABS((double)*aux));
                        }
                        else {
                            amount = *(float*)(entity + 0x1cc);
                        }
                        float peer = aux[7];
                        if ((peer != 0.0f) && (*(int*)((int)peer + 0x20) == 8)) {
                            hitPad = (unsigned int)peer;
                        }
                        hitOrigin = (float)entity;
                        (void)amount;
                        DAMAGE_DISPATCH(dmg, p2, p3, p4, p5, p6, p7, p8, (int*)&hitOrigin);
                    }
                }
                float peer = aux[7];
                if (((peer != 0.0f) && ((*(unsigned int*)((int)peer + 0x28) & 0x10) != 0)) &&
                    (*(int*)((int)peer + 0x20) != 8)) {
                    unsigned int ph = *(unsigned int*)((int)peer + 0x80);
                    if (((ph == 0) && ((*(unsigned int*)(desc + 0x1c0) & 0x800) != 0)) ||
                        ((ph != 0) && (peerFlags = subobj_get_flags(ph), (peerFlags & 0x400) != 0))) {
                        dirX = -aux[4];
                        dirY = -aux[5];
                        dirZ = -aux[6];
                        amount = (float)((double)FLOAT_80615ee8 * ABS((double)*aux));
                        hitOrigin = aux[7];
                        hitPad = (unsigned int)entity;
                        (void)amount;
                        DAMAGE_DISPATCH((double)FLOAT_80615ee8, p2, p3, p4, p5, p6, p7, p8,
                                        (int*)&hitOrigin);
                    }
                }
            }
        }
    }
    subobj_ambient_sound_tick((double)(FLOAT_80615f18 * (ABS(*aux) - FLOAT_80615f1c)),
                              (double)FLOAT_80615f00, entity);
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// subobj_get_restitution (0x80149fc4)
// Spawn-frame: resolve the descriptor's material params (from the sound-material
// byte +0x1cc via FUN_80252384) and its flags, returning the bounce/restitution
// term used by the break tick. (Ghidra models the return in f1.)
// -----------------------------------------------------------------------------
double subobj_get_restitution(void)
{
    int* rec;
    int entity;

    rec = (int*)__save_gpr_28();
    entity = *rec;
    FUN_80252384((unsigned int)*(unsigned char*)
                 (DAT_8061218c + (*(unsigned int*)(entity + 0x80) & 0xffff) * 0x200 + 0x1cc));
    subobj_get_flags(*(unsigned int*)(entity + 0x80));
    __restore_gpr_28();
    return (double)FLOAT_80615ea8;
}

// -----------------------------------------------------------------------------
// subobj_break_tick (0x8014a0cc) - thin wrapper -> subobj_break_tick_impl.
// -----------------------------------------------------------------------------
void subobj_break_tick(unsigned long long p1, unsigned long long p2, double p3, double p4,
                       double p5, double p6, double p7, unsigned long long p8)
{
    subobj_break_tick_impl(p1, p2, p3, p4, p5, p6, p7, p8);
}

// -----------------------------------------------------------------------------
// subobj_break_tick_impl (0x8014a0f0)
// Per-frame update for a breaking-off piece. Handles per-char-type gating (bag
// tags / special entity types), resolves the separation direction (explicit
// dir words, negated dir, or normalized center-to-center), applies a restitution
// -scaled separation force at the piece origin, and decays the piece health by
// the per-frame decrement; when health crosses zero it clears render state,
// fires death effects / projectile-death / a break callback, and destroys the
// piece entity.
// -----------------------------------------------------------------------------
void subobj_break_tick_impl(unsigned long long p1, unsigned long long p2, double p3, double p4,
                            double p5, double p6, double p7, unsigned long long p8)
{
    float lenSq;
    float rInv;
    bool haveDir;
    unsigned int* rec;
    unsigned int flags;
    float* dir;
    unsigned int* entity;
    double rest;
    double scaled;
    unsigned long long framed;
    float force[3];   // local_48/44/40
    float dirVec[3];  // local_3c/38/34
    float origin[3];  // local_30/2c/28

    framed = __save_gpr_28();
    rec = (unsigned int*)(unsigned int)(framed >> 0x20);
    haveDir = false;
    entity = (unsigned int*)*rec;

    if (((rec[6] == 0x17) || ((unsigned int*)rec[2] != entity)) &&
        (((entity[0x27] & 0x100000) == 0) ||
         ((player_ai_char_def_get_flags(rec[6]) & 0x800) != 0) || (rec[6] == 0x17))) {

        if ((FLOAT_80615ea8 < (float)entity[0x73]) && (FLOAT_80615ea8 < (float)entity[0x76])) {
            if (((entity[0x27] & 2) == 0) ||
                ((player_ai_char_def_get_flags(rec[6]) & 0x800) == 0)) {
                if ((rec[6] == 0x16) && (entity[0x5a] != 0)) {
                    entity_register_for_update((int)entity, 5, 0);
                }
            }
            else if (entity[0x5a] == 0) {
                if (entity[0x8a] != 0) {
                    sound_stop_handle(entity[0x8a]);
                }
                entity[0x8a] = 0;
                entity_register_for_update((int)entity, 5, -0x7ff85e6c);
                particle_kill_by_entity();
            }
        }

        dir = (float*)rec[10];
        if (dir == (float*)0x0) {
            origin[0] = (float)entity[0x19];
            origin[1] = (float)entity[0x1a];
            origin[2] = (float)entity[0x1b];
        }
        else {
            origin[0] = dir[0];
            origin[1] = dir[1];
            origin[2] = dir[2];
        }

        dir = (float*)rec[0xb];
        if (dir == (float*)0x0) {
            dir = (float*)rec[0xc];
            if (dir == (float*)0x0) {
                dirVec[0] = (float)entity[0x19] - origin[0];
                dirVec[1] = (float)entity[0x1a] - origin[1];
                dirVec[2] = (float)entity[0x1b] - origin[2];
                lenSq = dirVec[2] * dirVec[2] + dirVec[0] * dirVec[0] + dirVec[1] * dirVec[1];
                if (FLOAT_80615ec4 < lenSq) {
                    rInv = 1.0f / SQRT(lenSq);
                    rInv = rInv * -(FLOAT_80615ebc * lenSq * rInv * rInv - FLOAT_80615ec8);
                    scaled = (double)(FLOAT_80615eb0 *
                             rInv * -(FLOAT_80615ebc * lenSq * rInv * rInv - FLOAT_80615ec8));
                    dirVec[0] = (float)(dirVec[0] * scaled);
                    dirVec[1] = (float)(dirVec[1] * scaled);
                    dirVec[2] = (float)(dirVec[2] * scaled);
                }
            }
            else {
                dirVec[0] = dir[0];
                haveDir = true;
                dirVec[1] = dir[1];
                dirVec[2] = dir[2];
            }
        }
        else {
            haveDir = true;
            dirVec[0] = -dir[0];
            dirVec[1] = -dir[1];
            dirVec[2] = -dir[2];
        }

        rest = subobj_get_restitution();
        if (((((double)FLOAT_80615ea8 < rest) && (haveDir)) && (rec[9] != 0)) &&
            ((rec[9] != 0x17) && (rec[6] != 0x15)) && (rec[6] != 0x16)) {
            force[0] = (float)((double)dirVec[0] * rest);
            force[1] = (float)((double)dirVec[1] * rest);
            force[2] = (float)((double)dirVec[2] * rest);
            subobj_add_force_at_point(entity[0x20], origin, force);
        }

        float* hpHealth = (float*)&entity[0x73];
        float* hpMax    = (float*)&entity[0x76];
        if ((((int)framed != 0) && ((entity[10] & 0x10) != 0)) &&
            ((FLOAT_80615ea8 < *hpMax) &&
             (*hpHealth = *hpHealth - *(float*)&rec[8], *hpHealth <= FLOAT_80615ea8))) {
            *hpHealth = FLOAT_80615ea8;
            entity_set_render_type((int)entity, 0);
            entity_register_for_update((int)entity, 5, 0);
            death_effects_dispatch();
            subobj_redirty_in_aabb_wrapper();
            if ((entity[10] & 0x10000) != 0) {
                player_ai_projectile_death_dispatch();
            }
            if ((subobj_get_flags(entity[0x20]) & 0x200) != 0) {
                FUN_8014b058(rest, (double)FLOAT_80615ea8, p3, p4, p5, p6, p7, p8,
                             entity[0x20], origin, dirVec);
            }
            if (entity[8] == 0x400000) {
                FUN_801560c8(0, (double)FLOAT_80615ea8, p3, p4, p5, p6, p7, p8);
            }
            if ((void*)entity[0x56] != (void*)0) {
                ((void(*)(unsigned int*, int))entity[0x56])(entity, 0);
            }
            entity_destroy_with_event((int*)entity);
        }
    }
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// subobj_apply_ground_impulse (0x8014a4c4)
// Spawn-frame ground-contact callback. For a type-8 struck entity whose
// descriptor has the ground-contact flags (0x200000 + 0x100) set, compute a
// ground-plane contact point (from the area-effect ground tables at the strip's
// slot 0x48) and, when the incoming normal velocity is negative (penetrating),
// accumulate a scaled angular impulse (+0x128) and linear impulse (+0x14c) into
// the descriptor and rebuild the world contact normal (+0x134), then mark dirty.
// -----------------------------------------------------------------------------
void subobj_apply_ground_impulse(void)
{
    float f1, f2, f3, f4, f5, f6;
    int rec;
    int strip;
    int slotTab;
    int* frame;
    int entity4;
    unsigned int handle;
    int desc;
    double vy, vx, vz;
    double scale;
    unsigned long long framed;
    float cp[3];       // local_5c/58/54
    float localDir[10];// afStack_50
    float acc[3];      // local_68/64/60

    framed = FUN_803826f0();
    entity4 = (int)(framed >> 0x20);
    frame = (int*)(unsigned int)framed;
    rec = *frame;
    handle = *(unsigned int*)(entity4 + 0x80);
    desc = DAT_8061218c + (handle & 0xffff) * 0x200;

    if (((*(int*)(rec + 0x20) == 8) && ((*(unsigned int*)(desc + 0x1c0) & 0x200000) != 0)) &&
        ((*(unsigned int*)(desc + 0x1c0) & 0x100) != 0)) {
        strip = *(int*)(rec + 0x7c);
        slotTab = *(short*)(strip + 0x48) * 0x30;
        vy = (double)(float)frame[1];
        vz = (double)(float)frame[2];
        scale = -(double)*(float*)(&DAT_8048ca9c + slotTab);
        cp[0] = (float)(vy * scale + (double)*(float*)(rec + 0x64));
        cp[1] = (float)((double)FLOAT_80615ea8 * scale +
                        (double)(FLOAT_80615ebc * *(float*)(&DAT_8048ca94 + slotTab) +
                                 *(float*)(rec + 0x68)));
        cp[2] = (float)(vz * scale + (double)*(float*)(rec + 0x6c));
        double groundY = (double)(*(float*)(entity4 + 0x68) + *(float*)(entity4 + 0x1ac));
        subobj_world_to_local_dir(handle, cp, localDir);
        double zero = (double)FLOAT_80615ea8;
        double proj = (double)(float)((double)*(float*)(strip + 0x40) * vz +
                        (double)(float)((double)*(float*)(strip + 0x3c) * vy + zero));
        if (proj < zero) {
            cp[1] = (float)groundY;
            double imp = (double)(FLOAT_80615f28 * (float)((double)FLOAT_80615f2c * proj));
            f4 = (float)(groundY - (double)*(float*)(desc + 0x144));
            f5 = cp[0] - *(float*)(desc + 0x140);
            f1 = (float)(vy * imp);
            f2 = (float)(vz * imp);
            f6 = cp[2] - *(float*)(desc + 0x148);
            f3 = (float)(zero * imp);
            *(float*)(desc + 0x128) = *(float*)(desc + 0x128) + (f4 * f2 - f6 * f3);
            *(float*)(desc + 0x12c) = *(float*)(desc + 0x12c) + (f6 * f1 - f5 * f2);
            *(float*)(desc + 0x130) = *(float*)(desc + 0x130) + (f5 * f3 - f4 * f1);
            f4 = *(float*)(desc + 0x1a8);
            *(float*)(desc + 0x14c) = *(float*)(desc + 0x14c) + f1 * f4;
            *(float*)(desc + 0x150) = *(float*)(desc + 0x150) + f3 * f4;
            *(float*)(desc + 0x154) = *(float*)(desc + 0x154) + f2 * f4;
            acc[0] = *(float*)(desc + 0x128);
            acc[1] = *(float*)(desc + 0x12c);
            acc[2] = *(float*)(desc + 0x130);
            mtx4_transform_vector_alias((float*)(desc + 0x40), acc);
            *(float*)(desc + 0x134) = acc[0];
            *(float*)(desc + 0x138) = acc[1];
            *(float*)(desc + 0x13c) = acc[2];
            subobj_dirty_list_add(handle);
        }
    }
    FUN_8038273c();
}

// subobj_pool_alloc is declared void() in generated_decls but returns a handle
// in EAX; call through a typed alias.
typedef unsigned int (*pool_alloc_fn)(void);
#define POOL_ALLOC ((pool_alloc_fn)&subobj_pool_alloc)

// rng_counter_tick is declared void() in generated_decls but returns a 32-bit
// value in EAX; call through a typed alias.
typedef unsigned int (*rng_tick_fn)(void);
#define RNG_TICK ((rng_tick_fn)&rng_counter_tick)

// small helper: rng float in [-0.5, +0.5]-ish, matching the CONCAT44 pattern
static inline float __brk_rng_unit(void)
{
    unsigned int r = RNG_TICK();
    return ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_80615f40) / FLOAT_80615f38) - FLOAT_80615ebc;
}

// -----------------------------------------------------------------------------
// subobj_fracture_spawn (0x8014a6f4)
// Iterate the break-mask (uVar10) bit by bit; for each set bit find the matching
// mesh strip (word 0x50 == 1<<bit), spawn a child entity at that strip, allocate
// a physics sub-object pool slot, build the piece world matrix (bone or object),
// build its descriptor + orthonormal basis, seed contact origin, then apply a
// breaking impulse: either a directed impulse relative to param13/param14 or a
// randomized scatter (rng_counter_tick). An optional per-piece callback (param17)
// runs last. Spawn-frame (FUN_803826c8 / FUN_80382714).
// -----------------------------------------------------------------------------
void subobj_fracture_spawn(unsigned long long p1, double p2, double p3, double p4, double p5,
                           double p6, double p7, unsigned long long p8, unsigned int p9,
                           unsigned int p10, unsigned int param11, unsigned int param12,
                           float* param13, float* param14, int param15, unsigned int param16,
                           void* param17)
{
    float len;
    float rInv;
    unsigned int* piece;
    float* mtxSrc;
    int entity4;
    unsigned int mask;
    int bit;
    unsigned int handle;
    int desc;
    int strip;
    unsigned long long framed;
    double baseSpeed;
    double var16;
    double var17;
    float dirV[3];    // local_17c/178/174 or local_164/160/15c
    float posV[3];    // local_188/184/180 or local_170/16c/168
    float objMtx[16]; // afStack_100
    float boneMtx[16];// afStack_140
    float spawnDef[3];// local_14c/148/144

    framed = FUN_803826c8();
    entity4 = (int)(framed >> 0x20);
    mask = (unsigned int)framed;
    var17 = (double)(float)(FLOAT_80615f30 /* * incoming speed (f1) */);
    baseSpeed = var17;   /* incoming energy carried in f1; kept as a scalar here */
    var16 = var17;
    spawnDef[0] = (float)(unsigned int)DAT_803ccaf8;
    spawnDef[1] = (float)(unsigned int)DAT_803ccafc;
    spawnDef[2] = (float)(unsigned int)DAT_803ccb00;
    bit = 0;

    while (mask != 0) {
        if ((mask & 1) != 0) {
            unsigned int* stripTab = *(unsigned int**)(*(int*)(entity4 + 4) + 4);
            unsigned int count = *stripTab;
            unsigned int idx = 0;
            int off = 0;
            strip = 0;
            while ((count != 0) &&
                   (strip = (int)stripTab + off + *stripTab * -0xc0,
                    *(int*)(strip + 0x50) != (1 << bit))) {
                idx = idx + 1;
                off = off + 0xc0;
                count = count - 1;
            }
            if ((strip != 0) && (idx < *stripTab)) {
                piece = (unsigned int*)CREATE_INSTANCE(
                            0, p2, p3, p4, p5, p6, p7, p8,
                            (unsigned int)*(unsigned short*)(entity4 + 0x94), entity4 + 100, spawnDef);
                *(int*)(piece[1] + 0x30) = 1 << bit;
                piece[8] = 1;
                piece[10] = piece[10] | 0x10;
                piece[0x76] = (unsigned int)FLOAT_80615ea8;
                piece[0x73] = (unsigned int)FLOAT_80615ea8;

                handle = POOL_ALLOC();
                if (handle == 0) {
                    entity_destroy_with_event((int*)piece);
                    FUN_80382714();
                    return;
                }
                if (*(unsigned int*)(entity4 + 0x80) == 0) {
                    entity_build_local_matrix(boneMtx, (void*)(long long)entity4, 0, 0);
                    if (((*(unsigned int*)(entity4 + 0x28) & 0x200000) == 0) ||
                        ((*(unsigned int*)(entity4 + 0x28) & 8) == 0)) {
                        mtx4_copy(boneMtx, objMtx);
                    }
                    else {
                        mtx4_multiply((float*)(*(int*)(*(int*)(entity4 + 4) + 0x10) +
                                       *(char*)(strip + 0x3d) * 0x40), boneMtx, objMtx);
                    }
                }
                else {
                    mtxSrc = (float*)subobj_get_object_matrix(*(unsigned int*)(entity4 + 0x80));
                    mtx4_copy(mtxSrc, objMtx);
                }

                posV[0] = *(float*)(strip + 0x84);
                posV[1] = *(float*)(strip + 0x88);
                posV[2] = *(float*)(strip + 0x8c);
                mtx4_transform_point_alias(objMtx, posV);
                posV[0] = posV[0] + *(float*)(entity4 + 0x64);
                posV[1] = posV[1] + *(float*)(entity4 + 0x68);
                posV[2] = posV[2] + *(float*)(entity4 + 0x6c);
                piece[0x20] = handle;
                BUILD_DESC_DEF(0, p2, p3, p4, p5, p6, p7, p8, handle, (int)piece,
                               param11, -1, (float)(param12 | 0x400000));
                subobj_build_orthonormal_matrix(handle, objMtx, (float*)0x0);
                subobj_set_contact_origin(handle, (unsigned int*)posV);
                FUN_8014f024(handle, 8, 0xf1);
                subobj_dirty_list_add(handle);
                desc = DAT_8061218c + (handle & 0xffff) * 0x200;
                if (param16 != 0) {
                    weapon_start_delayed_event((double)FLOAT_80615f34, (int)piece, param16);
                }

                if (param13 == (float*)0x0) {
                    if (param14 != (float*)0x0) {
                        dirV[0] = FLOAT_80615ebc * __brk_rng_unit() + param14[0];
                        dirV[1] = FLOAT_80615ebc * __brk_rng_unit() + param14[1];
                        dirV[2] = FLOAT_80615ebc * __brk_rng_unit() + param14[2];
                        len = dirV[2] * dirV[2] + dirV[0] * dirV[0] + dirV[1] * dirV[1];
                        if (FLOAT_80615ec4 < len) {
                            rInv = 1.0f / SQRT(len);
                            rInv = rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8);
                            double n = (double)(FLOAT_80615eb0 *
                                       rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8));
                            dirV[0] = (float)(dirV[0] * n);
                            dirV[1] = (float)(dirV[1] * n);
                            dirV[2] = (float)(dirV[2] * n);
                        }
                        float speed = (float)((double)FLOAT_80615f3c *
                                     (double)(float)(var16 * (double)(__brk_rng_unit() + FLOAT_80615ebc)) + var17);
                        dirV[0] = dirV[0] * speed;
                        dirV[1] = dirV[1] * speed;
                        dirV[2] = dirV[2] * speed;
                        posV[0] = (float)((double)(FLOAT_80615f28 * __brk_rng_unit() * (float)piece[0x70]) * (double)FLOAT_80615ebc) + (float)piece[0x19] + (float)piece[0x6a];
                        posV[1] = (float)((double)(FLOAT_80615f28 * __brk_rng_unit() * (float)piece[0x71]) * (double)FLOAT_80615ebc) + (float)piece[0x1a] + (float)piece[0x6b];
                        posV[2] = (float)((double)(FLOAT_80615f28 * __brk_rng_unit() * (float)piece[0x72]) * (double)FLOAT_80615ebc) + (float)piece[0x1b] + (float)piece[0x6c];
                        subobj_add_force_at_point(handle, posV, dirV);
                    }
                }
                else {
                    dirV[0] = *(float*)(desc + 0x140) - param13[0];
                    dirV[1] = *(float*)(desc + 0x144) - param13[1];
                    dirV[2] = *(float*)(desc + 0x148) - param13[2];
                    len = dirV[2] * dirV[2] + dirV[0] * dirV[0] + dirV[1] * dirV[1];
                    if (FLOAT_80615ea8 < len) {
                        rInv = 1.0f / SQRT(len);
                        rInv = rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8);
                        double n = (double)(FLOAT_80615eb0 *
                                   rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8));
                        dirV[0] = (float)(dirV[0] * n);
                        dirV[1] = (float)(dirV[1] * n);
                        dirV[2] = (float)(dirV[2] * n);
                        if (param14 != (float*)0x0) {
                            dirV[0] = FLOAT_80615ebc * __brk_rng_unit() + (dirV[0] + param14[0]);
                            dirV[1] = FLOAT_80615ebc * __brk_rng_unit() + (dirV[1] + param14[1]);
                            dirV[2] = FLOAT_80615ebc * __brk_rng_unit() + (dirV[2] + param14[2]);
                            len = dirV[2] * dirV[2] + dirV[0] * dirV[0] + dirV[1] * dirV[1];
                            if (FLOAT_80615ea8 < len) {
                                rInv = 1.0f / SQRT(len);
                                rInv = rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8);
                                double n2 = (double)(FLOAT_80615eb0 *
                                            rInv * -(FLOAT_80615ebc * len * rInv * rInv - FLOAT_80615ec8));
                                dirV[0] = (float)(dirV[0] * n2);
                                dirV[1] = (float)(dirV[1] * n2);
                                dirV[2] = (float)(dirV[2] * n2);
                            }
                        }
                        float speed = (float)((double)FLOAT_80615f3c *
                                     (double)(float)(var16 * (double)(__brk_rng_unit() + FLOAT_80615ebc)) + var17);
                        dirV[0] = dirV[0] * speed;
                        dirV[1] = dirV[1] * speed;
                        dirV[2] = dirV[2] * speed;
                        posV[0] = (float)((double)(FLOAT_80615f28 * __brk_rng_unit() * (float)piece[0x70]) * (double)FLOAT_80615ebc) + (float)piece[0x19] + (float)piece[0x6a];
                        posV[1] = (float)((double)(FLOAT_80615f28 * __brk_rng_unit() * (float)piece[0x71]) * (double)FLOAT_80615ebc) + (float)piece[0x1a] + (float)piece[0x6b];
                        posV[2] = (float)((double)(FLOAT_80615f28 * __brk_rng_unit() * (float)piece[0x72]) * (double)FLOAT_80615ebc) + (float)piece[0x1b] + (float)piece[0x6c];
                        subobj_add_force_at_point(handle, posV, dirV);
                    }
                }
                if (param17 != (void*)0) {
                    ((void(*)(unsigned int*))param17)(piece);
                }
            }
        }
        mask = mask >> 1;
        bit = bit + 1;
    }
    FUN_80382714();
}

// -----------------------------------------------------------------------------
// subobj_fracture_spawn_default (0x8014b028)
// Fracture with default terrain/impulse mode (param15=1, param16=4, no callback).
// -----------------------------------------------------------------------------
void subobj_fracture_spawn_default(unsigned long long p1, double p2, double p3, double p4, double p5,
                                   double p6, double p7, unsigned long long p8, unsigned int p9,
                                   unsigned int p10, unsigned int param11, unsigned int param12,
                                   float* param13, float* param14)
{
    subobj_fracture_spawn(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, param11, param12,
                          param13, param14, 1, 4, (void*)0);
}

// ===========================================================================
// batch_423 addition (0x8014fa98): subobj_spawn_break_piece
// ===========================================================================

extern "C" {
// spawn-frame push/pop (returns packed {accum<<32 | ctx}); pop matches.
extern unsigned long long FUN_803826dc(void);
extern void               FUN_80382728(void);
// periodic-angle reduce (radians -> reduced turn count), FP ABI.
extern double             FUN_80390324(double x);
}

// entity_spawn_by_type / subobj_apply_angular_impulse_local are declared with
// simplified prototypes in generated_decls; call through typed aliases matching
// the real wide-ABI call sites.
typedef void*(*spawn_by_type_fn)(double f1, double p2, double p3, double p4, double p5,
                                 unsigned long long p6, unsigned long long p7, unsigned long long p8,
                                 unsigned int type, float* posV, float* dirV, int a, int b);
typedef void (*apply_ang_imp_fn)(unsigned int handle, float* imp);
// mtx4_to_euler/subobj_set_contact_origin/_set_linear_impulse are declared with
// simplified prototypes (fewer args / uint* vecs); call via typed aliases.
typedef void (*mtx_to_euler4_fn)(int mtx, float* ex, float* ey, float* ez);
typedef void (*set_vec_fn)(unsigned int handle, float* vec);
#define SPAWN_BY_TYPE  ((spawn_by_type_fn)&entity_spawn_by_type)
#define APPLY_ANG_IMP  ((apply_ang_imp_fn)&subobj_apply_angular_impulse_local)
#define MTX_TO_EULER4  ((mtx_to_euler4_fn)&mtx4_to_euler)
#define SET_CONTACT_ORIGIN ((set_vec_fn)&subobj_set_contact_origin)
#define SET_LINEAR_IMPULSE ((set_vec_fn)&subobj_set_linear_impulse)

extern float FLOAT_80615ecc;   // euler->angle scale
extern float FLOAT_80615ec0;   // pitch reference
extern float FLOAT_80615ed0;   // 180.0f (deg span)
extern float FLOAT_80615f94;   // wrap bias
extern float FLOAT_80615f98;   // 360.0f (turn span)
extern int   DAT_806108e4;     // impulse-enable gate
extern int   DAT_806109c0;     // frame counter / rng
extern float FLOAT_806108ec;   // frame-rate divisor
extern double DOUBLE_80615ed8; // dequant magic bias

// ===========================================================================
// 0x8014fa98  subobj_spawn_break_piece
// Spawn a break-off physics piece when a constraint breaks. Resolves the parent
// render matrix (skinned bone if the parent is animated, else base transform
// +0x274), extracts its euler orientation and world origin, converts the parent
// object angles into wrapped spawn angles (euler*scale, FUN_80390324 reduce),
// spawns a child entity (entity_spawn_by_type), allocates a sub-object pool slot
// and builds its descriptor. If a source bone index (>=0) is given, the contact
// origin is offset to that bone vertex; otherwise the parent origin is used.
// Finally, when impulses are enabled and the parent has a velocity history, it
// derives a linear + angular launch impulse from the parent's per-frame motion.
// ===========================================================================
void subobj_spawn_break_piece(double p1, double addY, double addZ, double impulseScale,
                              unsigned long long p5, unsigned long long p6, unsigned long long p7,
                              unsigned long long p8, unsigned int arg9, unsigned int arg10,
                              unsigned int arg11, unsigned int boneRef, unsigned int spawnFlags,
                              float extra)
{
    (void)arg9; (void)arg10;
    unsigned long long frame = FUN_803826dc();
    int* ctx = (int*)((unsigned long long)frame >> 0x20);
    unsigned int slot = (unsigned int)frame;

    // Resolve the parent's world matrix (skinned bone or base transform).
    int mtx;
    if ((*(int*)(ctx[1] + 4) < 1) ||
        ((*(unsigned int*)(*ctx + 0x28) & 0x200000) == 0) ||
        ((*(unsigned int*)(*ctx + 0x28) & 8) == 0)) {
        mtx = *ctx + 0x274;
    } else {
        mtx = ctx[4] + *(int*)(*(int*)(ctx[0x21]) + (int)slot * 4) * 0x40;
    }

    float eulerX, eulerY, eulerZ;
    MTX_TO_EULER4(mtx, &eulerX, &eulerY, &eulerZ);

    // Parent world origin -> local space of the parent entity.
    float origin[4];
    origin[0] = *(float*)(mtx + 0x30);
    origin[1] = *(float*)(mtx + 0x34);
    origin[2] = *(float*)(mtx + 0x38);
    float localMtx[16];
    entity_build_local_matrix((void*)localMtx, (void*)(long)*ctx, 1, 0);
    mtx4_transform_point_alias((float*)localMtx, origin);

    // Convert parent object angles into wrapped spawn angles.
    float spawnAng[4];
    double aZ = (double)(float)(p1 + (double)((FLOAT_80615ecc * (eulerZ - FLOAT_80615ed0)) /
                                              FLOAT_80615ed0 + *(float*)(*ctx + 0xc0)));
    double rZ = FUN_80390324((double)((float)(aZ - (double)FLOAT_80615f94) / FLOAT_80615f98));
    spawnAng[2] = -(float)((double)FLOAT_80615f98 * (double)(float)rZ - aZ);

    double aY = (double)(float)(addY + (double)((FLOAT_80615ecc * (eulerY - FLOAT_80615ec0)) /
                                                FLOAT_80615ed0 + *(float*)(*ctx + 0x70)));
    double rY = FUN_80390324((double)(float)((double)(float)(aY - (double)FLOAT_80615f94) /
                                             (double)FLOAT_80615f98));
    spawnAng[1] = -(float)((double)FLOAT_80615f98 * (double)(float)rY - aY);

    double aX = (double)(float)(addZ + (double)(float)(
                    (double)((float)(FLOAT_80615ecc * -eulerX) / FLOAT_80615ed0) +
                    (double)*(float*)(*ctx + 200)));
    double rX = FUN_80390324((double)(float)((double)(float)(aX - (double)FLOAT_80615f94) /
                                             (double)FLOAT_80615f98));
    spawnAng[0] = -(float)((double)FLOAT_80615f98 * (double)(float)rX - aX);

    void* piece = SPAWN_BY_TYPE((double)(float)rX, aY, aZ, rZ, (double)FLOAT_80615f98,
                                p6, p7, p8, arg11, origin, spawnAng, 0, 0);

    unsigned int handle = (unsigned int)POOL_ALLOC();
    if (handle == 0) {
        entity_destroy_with_event((int*)piece);
        FUN_80382728();
        return;
    }

    BUILD_DESC_DEF((double)FLOAT_80615ea8, aY, aZ, rZ, (double)FLOAT_80615f98, p6, p7, p8,
                   handle, (int)piece, spawnFlags, -1, extra);

    float* objMtx = (float*)subobj_get_object_matrix(handle);

    // Contact origin: parent origin, or offset to a source bone vertex.
    float contact[4];
    if ((int)boneRef < 0) {
        contact[0] = origin[0];
        contact[1] = origin[1];
        contact[2] = origin[2];
    } else {
        int** pieceData = (int**)((int*)piece)[1];
        float* boneVtx = (float*)((*(int**)((int)pieceData + 4))
            [**(int**)((int)pieceData + 4) * -0x30 + ((int)boneRef >> 0x10) * 0x30 + 0x1b]
            + (boneRef & 0xffff) * 0x18);
        contact[0] = -boneVtx[0];
        contact[1] = -boneVtx[1];
        contact[2] = -boneVtx[2];
        mtx4_transform_vector_alias(objMtx, contact);
        float* co = (float*)subobj_get_contact_origin(handle);
        contact[0] = contact[0] + co[0];
        contact[1] = contact[1] + *(float*)(subobj_get_contact_origin(handle) + 4);
        contact[2] = contact[2] + *(float*)(subobj_get_contact_origin(handle) + 8);
        SET_CONTACT_ORIGIN(handle, contact);
    }

    subobj_apply_world_bounds(handle);
    subobj_dirty_list_add(handle);

    // Launch impulse from the parent's per-frame motion (velocity history).
    if ((0 < DAT_806108e4) && (ctx[0x24] != 0) && ((double)FLOAT_80615ea8 != impulseScale)) {
        int histBase = ctx[0x24] + (int)slot * 0xc;
        double dt = (double)((float)((double)(unsigned int)(DAT_806109c0) - DOUBLE_80615ed8) /
                             FLOAT_806108ec);
        float lin[4];
        float s = (float)(impulseScale * dt);
        lin[0] = (*(float*)(mtx + 0x30) - *(float*)(histBase)) * s;
        lin[1] = (*(float*)(mtx + 0x34) - *(float*)(histBase + 4)) * s;
        lin[2] = (*(float*)(mtx + 0x38) - *(float*)(histBase + 8)) * s;
        mtx4_transform_vector_alias((float*)localMtx, lin);
        SET_LINEAR_IMPULSE(handle, lin);

        float ang[4];
        ang[2] = (contact[2] - origin[2]) * lin[0] - (contact[0] - origin[0]) * lin[2];
        ang[1] = (contact[0] - origin[0]) * lin[1] - (contact[1] - origin[1]) * lin[0];
        ang[0] = (contact[1] - origin[1]) * lin[2] - (contact[2] - origin[2]) * lin[1];
        APPLY_ANG_IMP(handle, ang);
    }
    FUN_80382728();
}
