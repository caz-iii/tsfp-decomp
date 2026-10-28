// Address range: 0x80150378 - 0x80151ec8 | batch_424 | 2026-09-06
//
// Player GRAB / CARRY interaction cluster (subsystem:game). This is the set of
// per-frame ticks and helpers that let a player character reach out, grab, and
// carry a physics sub-object (a "carryable" prop / body). The carried link is
// stored on the grabbing entity's render-data at slot [0x302] (byte +0xc08),
// and each carryable maintains a per-slot grab table at rec+0x70 with a 0x34
// (52-byte) stride, where rec = *(entity + 0x7c).
//
// Per-slot grab record (rec + 0x70 + i*0x34), fields used here:
//   +0x00 grabber entity handle      +0x04 secondary anim clip handle
//   +0x08 tertiary anim clip         +0x0c grab-attach vertex ref
//   +0x2c bind flags (bit 0xc==0 => free)   +0x30 grab state (0/2/4 dispatch)
//   +0xa0 carried-slot value (queried by subobj_carried_slot_value)
// Sub-object record (via *(entity+0x7c)):
//   +0x54 carry mode (1=seeking,2/3=carry,4=throw)   +0x58 state flags
//   +0x140 grab-slot count            +0x164 attach-point count
//   +0x528 push force   +0x52c contact speed   +0x53c..+0x544 impulse vec
//   +0x548/+0x550 rotor angle state   +0x558 floor height   +0x5b0 held-anim
//
// The two big ticks (subobj_grab_detect_tick, subobj_carry_physics_tick) are
// frame-registered (spawn-frame entry via FUN_803826c4 / FUN_803826cc); they
// have no direct callers. The decompiler spilled a large bank of callee-saved
// FP registers (in_f24..in_f31 / in_psNN_1) to stack shadow locals on entry;
// those are pure ABI artifacts (unused save/restore) and are dropped here.

#include "common.h"
#include "generated_decls.h"

// --- spawn-frame scratch helpers (register-pair returns) -------------------
extern "C" {
extern int                FUN_803826c4(void);          // grab_detect frame in
extern void               FUN_80382710(void);          // grab_detect frame out
extern int                FUN_803826cc(void);          // carry_phys frame in
extern void               FUN_80382718(void);          // carry_phys frame out
extern unsigned long long FUN_803826e4(void);          // anim_dispatch frame in
extern void               FUN_80382730(void);          // anim_dispatch frame out
extern int                FUN_803826f0(void);           // anim_poll frame in
extern void               FUN_8038273c(void);          // anim_poll frame out
extern int                __save_gpr_28(void);
extern void               __restore_gpr_28(void);
}

// --- raw helpers referenced by call site (self-externed, typed to call) ----
// NOTE: FUN_801523b0 (subobj_grab_collision_filter) and FUN_80151f54
// (subobj_grab_slot_lookup) are now DEFINED in this file by batch_425, so they
// are declared with their real prototypes below rather than self-externed.
extern "C" {
// NOTE: FUN_80159964 (subobj_grab_release_solve_pose) and FUN_801593fc
// (subobj_grab_release_commit) are now DEFINED in this file by batch_430;
// declared with their real prototypes in the forward-declaration block below.
extern double FUN_80390324(double x);                  // fmod/wrap
// NOTE: FUN_8015899c (subobj_grab_commit_attach), FUN_801586d4
// (subobj_grab_aim_overlap), FUN_80158740 (subobj_grab_try_teleport),
// FUN_80158e30 (subobj_grab_reach_test) and FUN_80159064
// (subobj_grab_pick_release_zone) are now DEFINED in this file by batch_429;
// declared with real prototypes in the forward-declaration block below.
// NOTE: FUN_80158128 (subobj_rotor_rev_settle) and FUN_801582dc
// (subobj_rotor_slap_detach) are now DEFINED in this file by batch_428;
// declared with real prototypes in the forward-declaration block below.
// batch_425 helpers (weapon_get_def_by_index resolves through generated_decls
// as void*; cast to char* at use sites for byte-offset field access):
extern void  dir_to_angles(float* dir, float* pitch, float* yaw);
extern void  FUN_8023fec0(int handle, float* outPos);  // entity world position
extern int   FUN_8023ddfc(int a, int b, int c, int d, int e, int* f);
// NOTE: FUN_80153344 (subobj_grab_attach_release), FUN_801540e4
// (subobj_carry_anim_event_scan) and FUN_801544f8 (subobj_carry_pose_commit)
// are now DEFINED in this file by batch_426; declared with real prototypes
// in the forward-declaration block below.

// --- batch_426 spawn-frame scratch helpers (register-pair returns) ---------
extern unsigned long long FUN_803826d4(void);   // carry_object_tick frame in
extern void               FUN_80382720(void);    // carry_object_tick frame out
extern unsigned long long FUN_803826e8(void);    // weapon_pose frame in
extern void               FUN_80382734(void);    // weapon_pose frame out
extern int                FUN_803826d8(void);    // rotor_pose frame in
extern void               FUN_80382724(void);    // rotor_pose frame out
extern unsigned long long FUN_803826dc(void);    // pose_commit frame in
extern void               FUN_80382728(void);    // pose_commit frame out

// --- batch_426 raw helpers referenced by call site (typed to call) ---------
// FUN_80158e30 (subobj_grab_reach_test) / FUN_80159064
// (subobj_grab_pick_release_zone) now DEFINED by batch_429 (fwd-declared below).
extern void   FUN_8023ff0c(int weaponEnt, void* def, float* out);
extern void   FUN_802405e8(int weaponEnt, float* out);          // weapon origin
// NOTE: FUN_80156420 (subobj_carry_ground_effect), FUN_80156914
// (subobj_rotor_integrate), FUN_80156cb8 (subobj_deform_relax) and
// FUN_80156a6c (subobj_deform_bone_impulse) are now DEFINED in this file by
// batch_427; declared with real prototypes in the forward-declaration block.

// --- batch_427 raw helpers referenced by call site (typed to call) ---------
// FUN_803826f0 / FUN_8038273c (slot-death frame in/out) declared by batch_424.
// subobj_break_tick, entity_create_instance, character_on_death_handler,
// subobj_build_descriptor_default and rng_counter_tick resolve through
// generated_decls; simplified prototypes are routed via typed aliases below.
extern int                FUN_802168dc(void* a, double b, double c, void* d, void* e,
                                       void* f, void* g, void* h, int body, unsigned int self);

// --- batch_428 spawn-frame scratch helpers (register-pair returns) ---------
// FUN_803826e8/FUN_80382734 (weapon_pose frame) declared by batch_426.
// FUN_803826f0/FUN_8038273c declared by batch_424. FUN_803826c4/FUN_80382710
// (grab-detect frame) declared by batch_424. FUN_803826dc/FUN_80382728 by 426.

// --- batch_428 raw helpers referenced by call site (typed to call) ---------
extern int    FUN_801c1714(void);                  // player local/3P origin test
extern void   FUN_80237b50(double a, int b, int c); // breakable/damage dispatch

// --- batch_429 raw helpers referenced by call site (typed to call) ---------
// NOTE: FUN_80159fb8 (subobj_carry_channel_ptr) is now DEFINED in this file by
// batch_430; declared with its real prototype in the forward-declaration block.
extern void   FUN_801a92b4(int rec);               // carried-body mount finalize
}
// stick-torque world-axis constants (data, not code):
extern float  DAT_803ccb08;    // stick torque axis x
extern float  DAT_803ccb0c;    // stick torque axis y
extern float  DAT_803ccb10;    // stick torque axis z

// player_get_camera_lerp_factor returns a double per-frame lerp divisor; the
// generated_decls prototype may be simplified, so route through a typed alias.
typedef double (*camlerp_fn)(int player);
#define CAM_LERP_FACTOR ((camlerp_fn)&player_get_camera_lerp_factor)

// --- typed aliases for helpers whose generated_decls prototype is simplified
// mtx4_transform_vector applies a matrix to an xyz stored at a float*.
typedef void (*mtxvec_fn)(float* mtx, float* v);
#define MTX4_XFORM_VEC ((mtxvec_fn)&mtx4_transform_vector)

// point_in_quad_edge_mask(px,py, quad 4 corners as 8 doubles) -> edge byte.
typedef unsigned char (*ptquad_fn)(double px, double py, double x0, double y0,
                                   double x1, double y1, double x2, double y2);
#define PT_IN_QUAD ((ptquad_fn)&point_in_quad_edge_mask)

// scene_raycast: real decl is
//   void scene_raycast(int node,float* origin,float* dir,int mode,float* hitPos,
//                      float* hitNorm,int* hitNode,uint mask,uint p9,int* p10)
// The call site treats the (nonzero) hit result as a return; route through a
// typed alias that yields int so the "hit == 0" test still works.
typedef int (*raycast_fn)(int node, int* origin, float* dir, int mode, float* hitPos,
                          float* hitNorm, int* hitNode, unsigned int mask,
                          unsigned int p9, int* p10);
#define SCENE_RAYCAST ((raycast_fn)&scene_raycast)

// mtx4_set_translation real decl: void(float* mtx,float x,float y,float z).
// The call site passes an angle triplet as the translation of a scratch matrix;
// alias to accept doubles at the PPC ABI (they widen to float in the callee).
typedef void (*setxlate_fn)(double x, double y, double z);
#define MTX4_SET_XLATE ((setxlate_fn)&mtx4_set_translation)

// anim_active_slot_userflags resolves through generated_externs (int stub).

// floor_query_with_interact real decl returns void; the call site consumes a
// float floor height in FP0 (PPC ABI). Alias to a float-returning form matching
// the two leading FP args seen at the call site.
typedef float (*floorq_fn)(double a, double b, int pos, int interact, int* out, int skip);
#define FLOOR_QUERY ((floorq_fn)&floor_query_with_interact)

// mem_advance_secondary is declared void in the shared decls, but the call site
// consumes its return as a scratch base pointer; alias to an int-returning form.
typedef int (*memadv_fn)(int size);
#define MEM_ADVANCE_SECONDARY ((memadv_fn)&mem_advance_secondary)

// subobj_engine_sound_tick: the real void(void) decl lives in generated_decls;
// a stale variadic stub in generated_externs.h causes an ambiguity that is
// resolved once gen_externs drops the stub (the definition is on disk in
// subobj_engine_sound.cpp). Plain call below relies on that regen.

// --- module float / data constants (only the symbols this TU references) ---
extern float  FLOAT_806108ec;  // frame delta coefficient
extern float  FLOAT_80615fa8;  // 1.0f
extern float  FLOAT_80615fac;  // 0.0f
extern float  FLOAT_80615fb8;  // approach / wrap-rate factor
extern float  FLOAT_80615fcc;  // 0.5f
extern float  FLOAT_80615fd0;  // rotor scale
extern float  FLOAT_80615fe0;  // large init-max
extern float  FLOAT_80615fe4;  // reciprocal-refine coefficient
extern float  FLOAT_80615fe8;  // reciprocal-refine constant
extern float  FLOAT_80615fec;  // grab range gate
extern float  FLOAT_80615ff0;  // squared radius gate
extern float  FLOAT_80615ff4;  // small epsilon
extern float  FLOAT_80615ff8;  // dot gate
extern float  FLOAT_80616004;  // inverse-mass coefficient
extern float  FLOAT_80616008;  // scale a
extern float  FLOAT_8061600c;  // up-tolerance
extern float  FLOAT_80616010;  // floor sentinel
extern float  FLOAT_80616014;  // aim delta gate
extern float  FLOAT_80616018;  // anim scale b
extern float  FLOAT_8061601c;  // roll tolerance
extern float  FLOAT_80616020;  // torque scale a
extern float  FLOAT_80616024;  // torque scale b
extern float  FLOAT_80616028;  // ik wrap span
extern float  FLOAT_8061602c;  // ik gain x
extern float  FLOAT_80616030;  // ik gain y
extern float  FLOAT_80616034;  // grab-anim bias
extern double DOUBLE_80615fd8; // dequant magic bias

extern unsigned int DAT_806109c0;    // frame counter / rng
extern int          DAT_806108e4;    // sim step (0 => paused)
extern int          DAT_80612610;    // active player count
extern int          DAT_80611d78;    // player record table base
extern int          DAT_80611d90;    // player entity table base
extern int          DAT_80611c24;    // entity handle table base
extern int          DAT_80611824;    // grab-def table base
extern int          DAT_8061c180;    // rotor accum seed hi
extern int          DAT_8061c184;    // rotor accum seed hi2
extern int          DAT_80615ffc;    // rotor accum seed lo
extern int          DAT_80616000;    // rotor accum seed lo2
extern float        DAT_8048ca94;    // per-face half-extent table base
// batch_425 additions:
extern float  FLOAT_80616038;  // weapon-aim IK: local aim scale
extern float  FLOAT_8061603c;  // weapon-aim IK: hit-dist gate (squared)
extern float  FLOAT_80616040;  // weapon-aim IK: normalize scale
extern float  FLOAT_80616044;  // weapon-aim IK: angle wrap lower
extern float  FLOAT_80616048;  // weapon-aim IK: angle wrap upper
extern float  FLOAT_8061604c;  // weapon-aim IK: yaw-clamp gate
extern float  FLOAT_80616050;  // weapon-aim IK: turn-rate gain
extern int    DAT_805893e0;    // collision-filter save stack: entity ptrs
extern int    DAT_80589408;    // collision-filter save stack: saved flag bits
extern int    DAT_806121c0;    // collision-filter save-stack depth counter
extern void*  DAT_806121e4;    // nearest-grab scan: intrusive list head
extern int    DAT_80611d54;    // local (view) player entity

// --- intra-file forward declarations (cluster calls itself) ----------------
void         subobj_grab_detect_tick(void);
void         subobj_grab_anim_dispatch(unsigned int param_1, int param_2, int param_3);
unsigned int subobj_find_active_grab(int param_1, int* param_2);
// subobj_flag_is_non_singleton resolves through generated_decls (unsigned int).
// batch_425 additions (defined below; forward-declared for earlier callers):
int          subobj_grab_slot_lookup(int* param_1);
void         subobj_grab_collision_filter(int param_1, int param_2);
int          subobj_has_live_grab(int param_1);
// batch_425: subobj_grab_attach_point / subobj_carried_weapon_aim resolve
// through generated_decls.

// --- batch_426 forward declarations (carry pose/attach cluster) ------------
int   subobj_grab_attach_release(int entity, void* rec, int a, int b, int c); // 0x80153344
void  subobj_carry_apply_stick_torque(int entity, float* stick);              // 0x801536b8
void  subobj_carry_rotor_drive(int entity, float* aim);                       // 0x801537f8
void  subobj_carry_flying_aim(void);                                          // 0x80153a64
void  subobj_carry_attach_point(int entity, unsigned int vref, float* out);   // 0x80153f80
int   subobj_carry_anim_event_scan(void);                                     // 0x801540e4
void  subobj_carry_weapon_pose(void);                                         // 0x8015410c
void  subobj_carry_pose_commit(void);                                         // 0x801544f8
void  subobj_carry_rotor_pose(void);                                          // 0x80154f48

// --- batch_426 module float / data constants ------------------------------
extern float  FLOAT_80615fb0;  // flying-aim: near-vertical gate
extern float  FLOAT_80616054;  // stick dead-zone / min gate
extern float  FLOAT_80616058;  // stick torque linear coefficient
extern float  FLOAT_8061605c;  // rotor spin-response clamp
extern float  FLOAT_80616060;  // rotor smoothing gain
extern float  FLOAT_80616064;  // angle-wrap offset (half period)
extern float  FLOAT_80616068;  // angle-wrap period (2*pi)
extern float  FLOAT_8061606c;  // flying-aim: mid pitch gate
extern float  FLOAT_80616070;  // rad<->deg scale
extern float  FLOAT_80616074;  // carry roll angle A
extern float  FLOAT_80616078;  // carry roll angle B
extern float  FLOAT_8061607c;  // rotor tail-Y offset
extern int    DAT_80611d58;    // per-entity bone-dirty bit count
extern short  DAT_804d1cd0;    // anim-def index table base (halfwords)
extern int    DAT_806117a8;    // split-screen / no-orbit gate

// --- batch_426 typed fn-ptr aliases (simplified generated_decls prototypes) -
// anim_event_scan_active real decl is (int,u16); the carry wrapper calls it with
// register-passed args the decompiler couldn't see. Route through a 0-arg alias.
typedef int (*animscan_fn)(void);
#define ANIM_EVENT_SCAN ((animscan_fn)&anim_event_scan_active)

// mtx4_to_euler canonical decl is (float*,float*); the call site passes a matrix
// plus three separate float* outs (x,y,z). Alias to that 4-arg form.
typedef void (*toeuler4_fn)(void* mtx, void* ox, void* oy, void* oz);
#define MTX4_TO_EULER4 ((toeuler4_fn)&mtx4_to_euler)

// scene_ray_traverse_portals canonical decl is void(void); the call site passes
// (startZ, dt, ray-origin, room, dir, flags) and consumes the resolved room id.
typedef int (*raytrav_fn)(double startZ, double dt, int origin, int room,
                          float* dir, int flags);
#define RAY_TRAVERSE_PORTALS ((raytrav_fn)&scene_ray_traverse_portals)

// mtx4_set_translation canonical decl is (float*,f,f,f); the rotor-pose call
// site passes only a translation triple (operates on the frame scratch matrix).
typedef void (*setxlate3_fn)(double x, double y, double z);
#define MTX4_SET_XLATE3 ((setxlate3_fn)&mtx4_set_translation)

// floor_height_from_position canonical decl is (float*,float); the attach call
// site passes two FP scalars (position packed in FP regs) and consumes an int room.
typedef int (*floorh_fn)(double a, double b);
#define FLOOR_HEIGHT ((floorh_fn)&floor_height_from_position)

// --- batch_427 forward declarations (carry deform / rotor / break cluster) --
void  subobj_break_apply_separation(void* p1, double p2, double p3, void* p4, void* p5,
                                    void* p6, void* p7, void* p8, int* dat);   // 0x801557e0
void  subobj_carry_shatter_burst(void);                                        // 0x801559b8
void  subobj_carry_dispatch_slot_deaths(unsigned long long p1, double p2, double p3,
                                        double p4, unsigned long long p5,
                                        unsigned long long p6, unsigned long long p7,
                                        unsigned long long p8);                // 0x801560c8
void  subobj_deform_apply_impulse(int entity, int* dat);                       // 0x80156180
void  subobj_deform_ground_react(double depth, int entity, int rec);           // 0x80156398
void  subobj_carry_ground_effect(void);                                        // 0x80156420
void  subobj_rotor_integrate(int entity);                                      // 0x80156914
void  subobj_deform_bone_impulse(int entity, int body);                        // 0x80156a6c
int   subobj_deform_accumulate(int entity, float* worldPt, float* impulse);    // 0x80156b1c
void  subobj_deform_relax(void);                                               // 0x80156cb8

// --- batch_427 typed fn-ptr aliases (simplified generated_decls prototypes) -
// rng_counter_tick real decl is void(void); it returns a 32-bit LCG value.
typedef unsigned int (*rng_fn)(void);
#define RNG_TICK ((rng_fn)&rng_counter_tick)

// character_on_death_handler real decl is void(void); the slot-death dispatcher
// invokes it with the PPC wide-arg ABI and threads a register-pair result.
typedef unsigned long long (*ondeath_fn)(unsigned long long p1, double p2, double p3,
                                         double p4, unsigned long long p5,
                                         unsigned long long p6, unsigned long long p7,
                                         unsigned long long p8);
#define ON_DEATH_HANDLER ((ondeath_fn)&character_on_death_handler)

// entity_create_instance canonical decl is (int,float*)->void*; the shatter
// spawner calls it with the wide-arg spawn ABI (type + position/orient + owner).
typedef void* (*create_inst_fn)(int type, float* pos, float* orient);
#define CREATE_INSTANCE ((create_inst_fn)&entity_create_instance)

// subobj_build_descriptor_default canonical decl is (uint,int,float); the shatter
// spawner calls the full (handle, descriptor, lodByte, offset, pad) form.
typedef void (*build_desc_fn)(unsigned int handle, void* desc, int lodByte, int off, float pad);
#define BUILD_DESC_DEFAULT ((build_desc_fn)&subobj_build_descriptor_default)

// player_ai_explosion_spawn canonical decl uses int first arg; the shatter path
// passes a float scale/size in FP0. Alias to accept a float first param.
typedef void (*explspawn_fn)(float size, int kind, float* pos, float* normal,
                             int entity, int room, int play_sound);
#define EXPLOSION_SPAWN ((explspawn_fn)&player_ai_explosion_spawn)

// subobj_pool_alloc canonical decl is void(void); it returns the allocated
// sub-object handle. Route through a uint-returning alias.
typedef unsigned int (*poolalloc_fn)(void);
#define POOL_ALLOC ((poolalloc_fn)&subobj_pool_alloc)

// FUN_803826f0 is declared int(void) elsewhere; the slot-death dispatcher needs
// its full register-pair (high word = entity). Route through a ull-returning alias.
typedef unsigned long long (*frame64_fn)(void);
#define FRAME_803826f0 ((frame64_fn)&FUN_803826f0)

// subobj_break_tick generated_decls prototype (ull,ull,5xdouble,ull) is the
// imprecise auto-generated form from the batch_417 wrapper. The break path calls
// it as (accBlock, then the launch direction/vectors, owner). Route through a
// typed alias so the call site's real argument shapes pass cleanly.
typedef void (*breaktick_fn)(int accBlock, double dx, double dz, double dy,
                             double nx, double ny, double nz, void* owner);
#define BREAK_TICK ((breaktick_fn)&subobj_break_tick)

// --- batch_427 module float / data constants ------------------------------
extern float  FLOAT_80615fa4;  // delayed-event time
extern float  FLOAT_80616080;  // shatter explosion color base a
extern float  FLOAT_80616084;  // shatter explosion color base b (as int)
extern float  FLOAT_80616088;  // rng range divisor
extern float  FLOAT_8061608c;  // scatter magnitude bias
extern float  FLOAT_80616090;  // scatter magnitude gain a
extern float  FLOAT_80616094;  // scatter magnitude gain b
extern float  FLOAT_806160a0;  // ground-react force scale
extern float  DAT_806160a4;    // rotor-select init A (weight sentinel)
extern float  DAT_806160a8;    // rotor-select init B (weight sentinel)
extern float  FLOAT_806160b4;  // ground align coefficient
extern float  FLOAT_806160b8;  // ground-effect base height
extern float  FLOAT_806160bc;  // ground-effect speed clamp
extern float  FLOAT_806160c0;  // ground-effect max ray dist
extern float  FLOAT_806160c4;  // ground-effect min ray dist
extern float  FLOAT_806160c8;  // ground-effect force scale
extern float  FLOAT_806160cc;  // ground-effect dist falloff
extern float  FLOAT_806160d0;  // ground-effect speed falloff
extern float  FLOAT_806160d4;  // bone-impulse vertical offset
extern float  FLOAT_806160d8;  // deform corner radius (squared)
extern double DOUBLE_80616098; // rng dequant magic bias
extern int    DAT_803ccb14;    // shatter color word 3
extern int    DAT_803ccb18;    // shatter color word 4
extern int    DAT_803ccb1c;    // shatter color word 5
extern float  DAT_803ccb20;    // shatter debris palette 0
extern int    DAT_803ccb24;    // shatter debris palette 1
extern int    DAT_803ccb28;    // shatter debris palette 2
extern float  DAT_803ccb2c;    // ground align axis x
extern float  DAT_803ccb30;    // ground align axis y
extern float  DAT_803ccb34;    // ground align axis z
extern int    DAT_806160ac;    // rotor-select init C
extern int    DAT_806160b0;    // rotor-select init D
extern int    DAT_80611928;    // frame counter (rotor spin-down)
extern int    DAT_80612614;    // active game mode
extern float  DAT_8048ca9c;    // per-material half-extent table (bone-impulse offset)

// --- batch_428 forward declarations (rotor-blade collision / drag) ---------
void  subobj_rotor_drag_integrate(double dt, int entity);                      // 0x80156f58
void  subobj_rotor_collision_broadphase(void);                                 // 0x801570d0
void  subobj_rotor_contact_dispatch(void* p1, double depth, int p3, int p4, float* pt); // 0x8015745c
void  subobj_rotor_solve_contact(int p1, int p2, float* rotor, float* impact);  // 0x80157674
void  subobj_rotor_strike_fx(void);                                            // 0x80157d18
void  subobj_rotor_strike_spark(int entity, int contact);                      // 0x80157f08
void  subobj_rotor_rev_settle(void);                                           // 0x80158128
void  subobj_rotor_slap_detach(void);                                          // 0x801582dc
unsigned int subobj_slot_occupied(int entity, int slotIndex);                  // 0x8015846c
void  subobj_grab_nearest_slot_scan(unsigned int p1, unsigned int p2, float* out); // 0x801584bc

// --- batch_428 typed fn-ptr aliases (simplified generated_decls prototypes) -
// particle_spawn_trail: canonical decl differs from the strike-fx call shape;
// route through a typed alias matching the (flag,type,mask,rec,dir) call.
typedef void (*trailspawn_fn)(int a, int type, unsigned int mask, void* rec, float* dir);
#define PARTICLE_SPAWN_TRAIL ((trailspawn_fn)&particle_spawn_trail)

// FUN_803826c4 is declared int(void) elsewhere; the rotor broadphase needs its
// full register-pair (high word = entity). Route through a ull-returning alias.
typedef unsigned long long (*frame64c4_fn)(void);
#define FRAME_803826c4 ((frame64c4_fn)&FUN_803826c4)

// The blade broadphase narrow-phase helpers have imprecise auto-generated
// prototypes (they were themselves declared via aliases in subobj_collision).
// Route each through a typed alias matching the actual call shape here.
typedef int  (*queryaabb_fn)(int room, float* aabb, int outArray, int startIdx, int maxCount, unsigned int typeMask);
#define QUERY_AABB_R ((queryaabb_fn)&entity_query_aabb)
typedef void (*genpoint_fn)(void);
#define GEN_CONSTRAINTS_POINT ((genpoint_fn)&subobj_collision_gen_constraints_point)
typedef void (*raysub_fn)(int other, int rec, unsigned short bladeIdx, int entity, int* frame);
#define RAYCAST_SUB_OBJECTS ((raysub_fn)&entity_raycast_sub_objects)
typedef void (*bvhmulti_fn)(int cfg, int rec, unsigned int bladeMask, int entity, int* frame, int swept, int flags);
#define BVH_MULTI_HIT ((bvhmulti_fn)&collision_bvh_multi_hit_query)

// --- batch_428 module float / data constants ------------------------------
extern float  FLOAT_80615fb4;  // motion-history velocity scale
extern float  FLOAT_80616100;  // rotor rev wake threshold (squared)
extern float  FLOAT_80616104;  // rotor slap impulse gate (squared)
extern float  FLOAT_806160dc;  // impact soft/hard split
extern float  FLOAT_806160e0;  // rotor drag speed hi gate
extern float  FLOAT_806160e4;  // rotor slap up-axis gate
extern float  FLOAT_806160e8;  // contact solver bias scale
extern float  FLOAT_806160ec;  // blade angle -> sin-table index scale
extern float  FLOAT_806160f0;  // contact impulse scale
extern float  FLOAT_806160f4;  // contact tangent gate (squared)
extern float  FLOAT_806160f8;  // drag ramp coefficient
extern float  FLOAT_806160fc;  // strike-fx sound gate
extern float  DAT_80583228;    // sine table base (0x800 entries)

// --- batch_429 forward declarations (grab-attach / auto-grab / zone-reach) --
unsigned int subobj_grab_aim_overlap(int entity, int body);                    // 0x801586d4
void  subobj_grab_try_teleport(void* p1, double p2, int entity, int body, int p5); // 0x80158740
void  subobj_grab_slot_set_attach(int entity, int contact, int slotIndex);     // 0x8015889c
int   subobj_grab_try_auto_attach(void* p1, double p2, int entity, int body);   // 0x801588fc
void  subobj_grab_commit_attach(void* p1, double p2, int entity, int body, int slotIndex); // 0x8015899c
void  subobj_grab_clear_link(int* body);                                       // 0x80158844
void  subobj_carry_resolve_room(unsigned long long p1, double p2);              // 0x80158c88
int   subobj_grab_reach_test(int entity, int slotRec, float* pos);             // 0x80158e30
int   subobj_grab_pick_release_zone(void);                                     // 0x80159064
int   subobj_grab_find_reachable_zone(int zone, int depth, int maxDepth);      // 0x801591e8

// --- batch_430 forward declarations (grab release + carry engage/gear) ------
void  subobj_grab_release_commit(int entity, int body, int p3, int p5);        // 0x801593fc
void  subobj_grab_release_solve_pose(int entity, void* slot, float* outPos, float* outAim); // 0x80159964
void  subobj_grab_slot_world_point(int self, int slotIndex, float* outPos);    // 0x80159bc8
void  subobj_grab_scan_release_zone(void);                                     // 0x80159c58
int   subobj_carry_held_anim_ptr(int entity);                                  // 0x80159fac
int   subobj_carry_channel_ptr(int entity);                                    // 0x80159fb8
void  subobj_carry_channel_set_param(double value, int entity);                // 0x80159fc4
unsigned int subobj_carry_flags(int entity);                                   // 0x80159ff4
void  subobj_carry_engage_update(int entity, int contact);                     // 0x8015a000
void  subobj_carry_gear_update(int entity);                                    // 0x8015a1f4

// --- batch_431 forward declarations (carry AI/nav state machine + steering) -
int*  subobj_carry_hud_gear_prompt(unsigned long long p1, unsigned long long p2, unsigned long long p3, unsigned long long p4, unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8, int entity, int* hud, unsigned int p11, unsigned int p12, unsigned int p13, unsigned int p14, unsigned int p15, unsigned int p16); // 0x8015a420
void  subobj_carry_ai_state_tick(void);                                        // 0x8015a4f4
unsigned int subobj_carry_dock_progress(int entity, int* state);               // 0x8015ab44
int   subobj_carry_nav_block_ptr(int entity);                                  // 0x8015ac78
unsigned int subobj_carry_nav_targets_valid(int entity);                       // 0x8015ac84
void  subobj_carry_nav_init(int entity, unsigned int p2, unsigned int p3, unsigned int p4); // 0x8015acb8
void  subobj_carry_nav_resolve_zones(void);                                    // 0x8015acf4
void  subobj_carry_zone_distance(void);                                        // 0x8015ad98
int   subobj_carry_nav_segment_project(void);                                  // 0x8015aee8 (returns via r3)
void  subobj_carry_steer_to_point(unsigned int p1, unsigned int p2, float* outDir); // 0x8015afe4

// --- batch_432 forward declarations (steer solvers + subsystem init/pool) ---
void  subobj_carry_steer_fallback(void);                                       // 0x8015b11c
void  subobj_carry_steer_ground(unsigned int p1, unsigned int p2, float* out);  // 0x8015b248
void  subobj_carry_steer_airborne(unsigned int p1, unsigned int p2, int out);   // 0x8015bdd0
void  subobj_carry_weapon_ai_aim(unsigned int p1, unsigned int p2, int outCtrl); // 0x8015c07c (r3 consumed via alias)
void  subobj_register_event_handlers(void);                                    // 0x8015ce58
int   subobj_pool_alloc_all(unsigned long long p1, unsigned long long p2, unsigned long long p3, unsigned long long p4, unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8, unsigned int p9, unsigned int p10, unsigned int p11, unsigned int p12, unsigned int p13, unsigned int p14, unsigned int p15, unsigned int p16); // 0x8015cec4
void  subobj_pool_init(void);                                                  // 0x8015cf24
void  subobj_pool_pop_free(void);                                              // 0x8015d080
void  subobj_pool_free_record(int* rec);                                       // 0x8015d0cc
void  subobj_release_sound_banks(unsigned long long p1, unsigned long long p2, unsigned long long p3, unsigned long long p4, unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8, int mode); // 0x8015d14c
// subobj_carry_weapon_ai_aim is declared void but the ai-state-tick caller
// consumes its r3 as an int state; route those call sites through an int alias.
typedef int (*weaponai_fn)(unsigned int p1, unsigned int p2, int outCtrl);
#define WEAPON_AI_AIM ((weaponai_fn)&subobj_carry_weapon_ai_aim)

// --- batch_433 forward declarations (spawn/snapshot/restore + render dispatch)
void  subobj_preload_carry_resources(unsigned long long p1, double p2, double p3, unsigned long long p4, unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8, int def); // 0x8015d1dc
void  subobj_apply_transform_snapshot(unsigned int p1, unsigned int p2, float* euler); // 0x8015d254
void  subobj_carry_destroy(void);                                              // 0x8015d3f8
void  subobj_snapshot_capture(void);                                           // 0x8015d518
void  subobj_snapshot_store(int entity);                                       // 0x8015d5b8
void  subobj_snapshot_restore(void);                                           // 0x8015d624
void  subobj_snapshot_all(void);                                               // 0x8015d6b0
void  subobj_snapshot_rollback(void);                                          // 0x8015d710
void  subobj_carry_spawn(unsigned long long p1, unsigned long long p2, unsigned long long p3, unsigned long long p4, unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8); // 0x8015d744
void  subobj_deform_init_springs(void);                                        // 0x8015d9e8
// subobj_pool_pop_free / subobj_pool_alloc / subobj_get_descriptor are declared
// void but subobj_carry_spawn consumes their r3; route through int aliases.
typedef int (*popfree_fn)(void);
#define POOL_POP_FREE ((popfree_fn)&subobj_pool_pop_free)
typedef unsigned int (*poolalloc_fn)(void);
#define POOL_ALLOC ((poolalloc_fn)&subobj_pool_alloc)
// mtx4_to_euler real decl is 2-arg; the snapshot call uses 3 out-angles.
typedef void (*toeuler3_fn)(int mtx, float* a, float* b, float* c);
#define MTX4_TO_EULER3 ((toeuler3_fn)&mtx4_to_euler)
// subobj_build_descriptor_default: the spawn call uses the full 13-arg PPC-ABI
// form (distinct from the batch_427 5-arg BUILD_DESC_DEFAULT alias above).
typedef void (*builddesc16_fn)(unsigned long long p1, unsigned long long p2, unsigned long long p3, unsigned long long p4, unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8, unsigned int handle, int entity, unsigned int flags, int lod, float pad);
#define BUILD_DESC_DEFAULT16 ((builddesc16_fn)&subobj_build_descriptor_default)

// --- batch_429 typed fn-ptr aliases (simplified generated_decls prototypes) -
// subobj_grab_nearest_slot_scan (batch_428) is declared void but returns the
// found slot index (or -1) in r3; route through an int-returning alias.
typedef int (*nearscan_fn)(unsigned int entity, unsigned int body, float* out);
#define NEAREST_SLOT_SCAN ((nearscan_fn)&subobj_grab_nearest_slot_scan)
// nav_movement_dispatch canonical decl is 9-arg with arg4 int; the reach probe
// call shape is (height, radius, navState, target, out, 0, 0, 0, 0). Route via alias.
typedef unsigned int (*navdisp_fn)(double height, double radius, int navState,
                                   float* target, float* out, float* p6, int* p7, int p8, int p9);
#define NAV_MOVE_DISPATCH ((navdisp_fn)&nav_movement_dispatch)
// floor_room_query out-entity arg is void**; alias to accept an int* null here.
typedef int (*floorroom_fn)(int floorPos, float* pos, int* outEnt, int* outInteract, int skip);
#define FLOOR_ROOM_QUERY ((floorroom_fn)&floor_room_query)
// soundpad_query_adjacent_zones_simple arg2 is the output buffer in the call
// shape (Ghidra typed it 'int unused'); alias to accept the int* buffer.
typedef int (*adjzones_fn)(int zoneNode, int* out, int maxResults, unsigned int excl, int inclInternal);
#define ADJ_ZONES_SIMPLE ((adjzones_fn)&soundpad_query_adjacent_zones_simple)

// --- batch_429 module float / data constants ------------------------------
extern int    DAT_80611c14;    // sound-zone node count
extern int    DAT_80611b8c;    // sound-zone node table base
extern int    DAT_80611d5c;    // per-player room-visibility table base
// --- batch_430 raw helpers / constants (only symbols this TU adds) ---------
extern "C" {
extern void   FUN_801a932c(int rec);               // clear carried-body attach state
}
extern float  FLOAT_80616108;  // gear-select speed scale
extern float  FLOAT_8061610c;  // gear-select speed divisor
// --- batch_431 raw helpers / constants (carry AI/nav state machine) --------
// text_get_font_variant resolves through generated_externs (int stub); the
// call site casts its return to the font pointer.
// NOTE: FUN_8015c07c (subobj_carry_weapon_ai_aim), FUN_8015b11c
// (subobj_carry_steer_fallback), FUN_8015b248 (subobj_carry_steer_ground) and
// FUN_8015bdd0 (subobj_carry_steer_airborne) are now DEFINED in this file by
// batch_432; declared with real prototypes in the forward-declaration block.
extern "C" {
extern int    FUN_8038b510(int* hud, int* str);             // HUD queue text
extern int    FUN_80387fb8(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                           unsigned long long p4, unsigned long long p5, unsigned long long p6,
                           unsigned long long p7, unsigned long long p8, int hud, void* fmt,
                           unsigned int val, void* font, unsigned int p13, unsigned int p14,
                           unsigned int p15, unsigned int p16); // HUD formatted text
}
extern int    DAT_806109a8;    // HUD gear-count format string ref
extern float  FLOAT_80616110;  // dock ramp up-rate
extern float  FLOAT_80616114;  // dock ramp boost-rate
// subobj_carry_nav_segment_project (0x8015aee8) is declared void(void) but the
// caller consumes its r3 result as an int; route through an int-returning alias.
typedef int (*navseg_fn)(void);
#define NAV_SEGMENT_PROJECT ((navseg_fn)&subobj_carry_nav_segment_project)
// --- batch_432 raw helpers / constants (steer solvers + subsystem init) ----
extern "C" {
extern unsigned long long FUN_803826d0(void);      // weapon-ai frame in
extern void   FUN_8038271c(void);                  // weapon-ai frame out
extern unsigned long long FUN_803826d4(void);      // steer-ground frame in
extern void   FUN_80382720(void);                  // steer-ground frame out
extern int    FUN_801a72f8(int defBlock, int p2);  // weapon aim-tuning def
extern int    FUN_802a5840(int actor);             // AI difficulty/aim-assist test
// NOTE: FUN_8015d3f8 (subobj_carry_destroy) is now DEFINED in this file by
// batch_433; declared with its real prototype in the forward-declaration block.
// NOTE: FUN_8015f9c8 (subobj_carry_def_record) and FUN_8015dc90
// (subobj_deform_compute_rest) are now DEFINED in this file by batch_434;
// declared with real prototypes in the batch_434 forward-declaration block.
}
// --- batch_433 data globals / labels (spawn link slots + snapshot buffers) --
extern int    DAT_806121c8;    // snapshot double-buffer count backup
extern int    DAT_80157f04;    // link-slot 2 handler (data label)
extern int    LAB_801580e0;    // link-slot 3 handler (code label)
extern int    LAB_80158104;    // link-slot 4 handler (code label)
extern float  FLOAT_80616180;  // deform spring inverse-mass coefficient
// weapon-ai float tuning constants
extern float  FLOAT_80616134; extern float FLOAT_80616138; extern float FLOAT_8061613c;
extern float  FLOAT_80616140; extern float FLOAT_80616144; extern float FLOAT_80616148;
// steer-ground float constants
extern float  FLOAT_8061611c; extern float FLOAT_80616120; extern float FLOAT_80616124;
// steer-airborne float constants
extern float  FLOAT_80616128; extern float FLOAT_8061612c; extern float FLOAT_80616130;
// downwash particle-def constants (subobj_pool_init)
extern float  FLOAT_80616150; extern float FLOAT_80616154; extern float FLOAT_80616158;
extern float  FLOAT_8061615c; extern float FLOAT_80616160; extern float FLOAT_80616164;
extern float  FLOAT_80616168; extern float FLOAT_8061616c; extern float FLOAT_80616170;
extern float  FLOAT_80616174; extern float FLOAT_80616178; extern float FLOAT_8061617c;
// pool + misc data globals
extern int    DAT_806121cc; extern int DAT_806121d0; extern int DAT_806121d4;
extern int    DAT_806121d8; extern int DAT_806121dc; extern int DAT_806121e0;
extern int    DAT_806121e8; extern int DAT_80610ba0;
extern int    DAT_80589430; extern int DAT_805894fc;
// particle-def / mem-alloc / swept-query / preload have simplified generated
// decls; route the PPC-ABI call shapes through typed aliases.
typedef void (*pdef_create_fn)(double a, double b, void* ref, int p4, int p5, int p6, int type, int p8);
#define PARTICLE_DEF_CREATE ((pdef_create_fn)&particle_def_create)
typedef void (*pdef_sizekf_fn)(double a, double b, double c, double d, unsigned int f0, unsigned int f1, unsigned int f2);
#define PARTICLE_DEF_SIZE_KF ((pdef_sizekf_fn)&particle_def_set_size_keyframes)
typedef void (*pdef_vel_fn)(double a, double b, double c, double d, double e, double f);
#define PARTICLE_DEF_VEL ((pdef_vel_fn)&particle_def_set_velocity_ranges)
typedef void (*pdef_size_fn)(double a, double b, double c, double d);
#define PARTICLE_DEF_SIZE_SE ((pdef_size_fn)&particle_def_set_size_start_end)
#define PARTICLE_DEF_COLOR_END ((pdef_size_fn)&particle_def_set_color_end)
typedef void (*pemit_create_fn)(double a, double b, double c, void* r1, void* r2, int p6, int p7);
#define PARTICLE_EMITTER_CREATE ((pemit_create_fn)&particle_emitter_create)
#define PARTICLE_EMITTER_COLOR ((pdef_size_fn)&particle_emitter_set_color_range)
typedef int (*memalloc_fn)(unsigned long long p1, unsigned long long p2, unsigned long long p3, unsigned long long p4, unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8, int size, int type, unsigned int p11, unsigned int p12, unsigned int p13, unsigned int p14, unsigned int p15, unsigned int p16);
#define MEM_ALLOC16 ((memalloc_fn)&mem_alloc)
// scene_swept_aabb_query returns hit result (r3) consumed as int; int alias.
typedef int (*sweptq_fn)(int node, float* pos, float* dir, int p4, int* results, unsigned int mask, unsigned int p7);
#define SWEPT_AABB_QUERY ((sweptq_fn)&scene_swept_aabb_query)
// sound_clear_bank_ref returns r3 chained into entity_preload_resources; alias.
typedef int (*sndclear_fn)(int soundId);
#define SOUND_CLEAR_BANK ((sndclear_fn)&sound_clear_bank_ref)
typedef int (*preload_fn)(int chain, unsigned long long p2, unsigned long long p3, unsigned long long p4, unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8, int typeId);
#define PRELOAD_RES ((preload_fn)&entity_preload_resources)
// event_set_handler handler arg is void*; the registered handlers are our fns.

// ===========================================================================
// batch_434 : carried-object DEFORM/BREAKABLE-FRAME build layer
// ---------------------------------------------------------------------------
// The set of per-model-type "build the deformable body" callbacks (frame-
// registered spawn callbacks, no direct xrefs) that lay out a carried craft's
// deform nodes and rotor sub-objects. Each variant reads the model's mesh
// bone/vertex table (via *(entity+4)+4 -> skinned strip block, rec at
// piVar[*piVar*-0x30 + 0x1b]) and:
//   * derives the local AABB span of the body from opposing mesh verts,
//   * fills N deform-node blocks (rec +0x184 / +0x1e8 / +0x24c / +0x2b4 / ...
//     stride 0x64) with the node "kind" tag, world anchor +0x19c, and the
//     rest-length (math_sqrt of the anchor->bone-attach delta) at +0x198,
//   * seeds the rotor spin/rev channels (subobj_deform_rest_init, 0x100 stride
//     rec+0x174) and the rotor-downwash particle emitters (rec +0x1dc/+0x240,
//     def DAT_805894fc), and
//   * optionally spawns detachable rotor/prop attachment sub-objects
//     (subobj_deform_spawn_attach) for the live-rotor states (+0x30/+0x34).
// The variants differ only in bone-offset layout and node/rotor counts (one
// per craft class in the def table). subobj_carry_def_record (0x8015f9c8) is
// the def-table accessor; subobj_deform_rest_offset (0x8015dc90) precomputes
// each rotor's rest offset once per frame.
// ---------------------------------------------------------------------------

// --- batch_434 forward declarations ----------------------------------------
void  subobj_deform_rest_init(int entity, int rotorIdx, int defIdx);           // 0x8015daa4
void  subobj_deform_spawn_attach(unsigned long long p1, unsigned long long p2, double p3, unsigned long long p4, unsigned long long p5, unsigned long long p6, unsigned long long p7, unsigned long long p8, int entity, int slot, int attachType); // 0x8015daf8
void  subobj_deform_set_def_slot(int entity, int value, int slot);             // 0x8015dc7c
void  subobj_deform_rest_offset(void);                                         // 0x8015dc90
void  subobj_deform_build_type0(void);                                         // 0x8015dd9c
void  subobj_deform_build_type1(void);                                         // 0x8015e434
void  subobj_deform_build_type2(void);                                         // 0x8015ea54
void  subobj_deform_build_type3(void);                                         // 0x8015f0a8
void  subobj_deform_build_type4(void);                                         // 0x8015f538
int   subobj_carry_def_record(int defIndex);                                   // 0x8015f9c8

// --- batch_434 spawn-frame scratch helpers (register-pair returns) ----------
// FUN_803826e4/FUN_80382730 (declared by batch_424) and FUN_803826d0/
// FUN_8038271c (batch_432) are reused. New for this batch:
extern "C" {
extern unsigned long long FUN_803826c8(void);      // deform-build frame in (type1)
extern void               FUN_80382714(void);      // deform-build frame out (type1)
extern int                __save_gpr_25(void);      // frame prologue (type3/type4)
extern void               __restore_gpr_25(void);
}

// --- batch_434 data globals / constants -------------------------------------
extern int    DAT_8040e570;    // per-rotor rest-length def table (0x1c stride)
extern int    DAT_8040e644;    // per-attach spin-rate table (0x8c stride, +0x644)
// deform build float constants (rotor rest rotation + node seeds)
extern float  FLOAT_80616150; extern float FLOAT_80616154; extern float FLOAT_80616158;
extern float  FLOAT_80616184; extern float FLOAT_80616188; extern float FLOAT_8061618c;
extern float  FLOAT_80616190; extern float FLOAT_80616194; extern float FLOAT_80616198;
extern float  FLOAT_8061619c; extern float FLOAT_806161a0; extern float FLOAT_806161a4;
extern float  FLOAT_806161a8;
// subobj_deform_spawn_attach uses subobj_get_object_matrix / mtx4_transform_vector
// / mtx4_to_euler via generated_decls. The 3-out-angle mtx4_to_euler call reuses
// MTX4_TO_EULER3 (declared by batch_433). item_spawn_entity has a simplified
// generated decl (double,int*,int); the real PPC-ABI call passes the spawn euler
// / origin scalars plus the item-def pointer and world position, so route it
// through a typed alias matching the call site.
typedef int (*itemspawn_fn)(double a, double b, double c, unsigned long long p4,
                            unsigned long long p5, unsigned long long p6,
                            unsigned long long p7, unsigned long long p8,
                            unsigned int* pItemDef, float* pos);
#define ITEM_SPAWN_ENTITY ((itemspawn_fn)&item_spawn_entity)
// particle_emitter_spawn's generated decl types arg1/arg2 as u32; the deform
// builders pass a computed rec-relative emitter block (rec+0x1dc/+0x240) and a
// def pointer (&DAT_805894fc). Route through a typed alias taking those shapes.
typedef void (*pemit_spawn_fn)(int emitterBlock, int* pDef, unsigned int flags,
                               float* position, float* direction);
#define PARTICLE_EMITTER_SPAWN ((pemit_spawn_fn)&particle_emitter_spawn)

// ===========================================================================
// (batch_424) subobj_grab_detect_tick
// ---------------------------------------------------------------------------
// MASSIVE per-frame grab-detection scan. For the owning player it walks every
// active player record, tests whether a carryable's reach volume overlaps the
// player, projects candidate grab points against the aim quad, ray-tests line
// of sight, and on a held grab button either destroys/attaches the target or
// charges the on-screen grab prompt.
// ===========================================================================
void subobj_grab_detect_tick(void)
{
    int entity = FUN_803826c4();
    int rec = *(int*)(entity + 0x7c);

    if ((*(int*)(rec + 0x54) == 0) && ((*(unsigned int*)(rec + 0x58) & 0x10) == 0)) {
        float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(entity + 0x80));
        int recStride = 0;
        for (unsigned int pi = 0; pi < DAT_80612610; pi = pi + 1) {
            int* pr = (int*)(DAT_80611d78 + recStride);
            int body = pr[5];
            int bodyRec = *(int*)(body + 0x7c);
            if (subobj_flag_is_non_singleton(pr[0x45]) != 0) {
                float ry = *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
                float by = *(float*)(body + 0x68);
                float ryLo = ry - *(float*)(entity + 0x1c4);
                float ryHi = *(float*)(entity + 0x1c4) + ry;
                float byTop = by + *(float*)(&DAT_8048ca94 +
                                    *(short*)(bodyRec + 0x48) * 0x30);
                if ((((ryLo < by) && (by < ryHi)) || ((ryLo < byTop) && (byTop < ryHi))) ||
                    (((by < ryLo) && (ryHi < byTop)) || ((ryLo < by) && (byTop < ryHi)))) {
                    float* aim = (float*)subobj_get_world_aim_point(*(unsigned int*)(entity + 0x80));
                    double best = (double)FLOAT_80615fe0;
                    double zeroGate = (double)FLOAT_80615fac;
                    double nx = 0.0, ny = 0.0;   // best-edge normal (in_f30/f29)
                    int off = 0x18;
                    int loop = 4;
                    int idxA = 3;
                    int idxB = 0;
                    do {
                        int idxSave = idxA;
                        float ay1 = ((float*)((int)aim + off))[1];
                        float by1 = (aim + idxB * 2)[1];
                        float bx1 = aim[idxB * 2];
                        float ax1 = *(float*)((int)aim + off);
                        double ey = (double)(*(float*)(body + 0x6c) - by1);
                        double ex = (double)(bx1 - ax1);
                        double dy = -(double)(by1 - ay1);
                        double dx = (double)(*(float*)(body + 100) - bx1);
                        if (zeroGate < (double)(float)(dx * dy + (double)(float)(ey * ex))) {
                            float len = (float)(dy * dy + (double)(float)(ex * ex));
                            float t = FLOAT_80615fe4 * len;
                            float inv = 1.0f / SQRT(len);
                            inv = inv * -(t * inv * inv - FLOAT_80615fe8);
                            double scl = (double)(FLOAT_80615fa8 * inv * -(t * inv * inv - FLOAT_80615fe8));
                            double sx = (double)(float)(ex * scl);
                            double sy = (double)(float)(dy * scl);
                            double proj = (double)(float)(dx * sy + (double)(float)(ey * sx));
                            if (((zeroGate <= proj) && (proj < best)) &&
                                (proj < (double)FLOAT_80615fec)) {
                                double cy = (double)(ay1 - by1);
                                double cx = (double)(ax1 - bx1);
                                double along = (double)(float)(dx * cx + (double)(float)(ey * cy));
                                double rx = dx, ry2 = ey;
                                if ((zeroGate <= along) &&
                                    (rx = zeroGate, ry2 = zeroGate,
                                     (double)(float)(cx * cx + (double)(float)(cy * cy)) < along)) {
                                    rx = (double)(float)(dx - cx);
                                    ry2 = (double)(float)(ey - cy);
                                }
                                if ((float)(rx * rx + (double)(float)(ry2 * ry2)) < FLOAT_80615ff0) {
                                    best = proj;
                                    ny = sy;
                                    nx = sx;
                                }
                            }
                        }
                        off = off + -8;
                        loop = loop + -1;
                        idxA = idxSave + -1;
                        idxB = idxSave;
                    } while (loop != 0);

                    if (best < (double)FLOAT_80615fe0) {
                        unsigned int* chosen = (unsigned int*)0;
                        unsigned int chosenIdx = 0xffffffff;
                        unsigned char baseMask = PT_IN_QUAD(
                            (double)*(float*)(body + 100), (double)*(float*)(body + 0x6c),
                            (double)aim[0], (double)aim[1], (double)aim[2], (double)aim[3],
                            (double)aim[4], (double)aim[5]);
                        int slotOff = 0;
                        unsigned int slotIdx;
                        unsigned int* slot = chosen;
                        unsigned int outIdx = chosenIdx;
                        for (unsigned int si = 0;
                             outIdx = chosenIdx, slot = chosen, si < *(unsigned int*)(rec + 0x140);
                             si = si + 1) {
                            slot = (unsigned int*)(rec + slotOff + 0x70);
                            float gx = (float)slot[8];
                            unsigned int gy = slot[9];
                            float gz = (float)slot[10];
                            float gp[3];
                            gp[0] = gx; *(unsigned int*)&gp[1] = gy; gp[2] = gz;
                            MTX4_XFORM_VEC(objMtx, gp);
                            double wx = (double)(gp[0] + *(float*)(entity + 100));
                            double wz = (double)(gp[2] + *(float*)(entity + 0x6c));
                            unsigned char m = PT_IN_QUAD(wx, wz,
                                (double)aim[0], (double)aim[1], (double)aim[2], (double)aim[3],
                                (double)aim[4], (double)aim[5]);
                            outIdx = si;
                            if (m == 0) {
                                float ddx = (float)(wx - (double)*(float*)(body + 100));
                                float ddz = (float)(wz - (double)*(float*)(body + 0x6c));
                                if (ddx * ddx + ddz * ddz < FLOAT_80615ff0) {
                                    nx = (double)(float)((double)*(float*)(body + 100) - wx);
                                    ny = (double)(float)((double)*(float*)(body + 0x6c) - wz);
                                    float ln = (float)(ny * ny +
                                        (double)(float)(nx * nx + (double)FLOAT_80615fac));
                                    if (FLOAT_80615ff4 < ln) {
                                        float iv = 1.0f / SQRT(ln);
                                        iv = iv * -(FLOAT_80615fe4 * ln * iv * iv - FLOAT_80615fe8);
                                        double sc = (double)(FLOAT_80615fa8 *
                                            iv * -(FLOAT_80615fe4 * ln * iv * iv - FLOAT_80615fe8));
                                        nx = (double)(float)(nx * sc);
                                        ny = (double)(float)(ny * sc);
                                    }
                                    break;
                                }
                            } else {
                                if ((m & baseMask) != 0) break;
                                if ((chosen == (unsigned int*)0) || (slot[0xc] == 0)) {
                                    unsigned int h = *slot;
                                    int he;
                                    if (h == 0) {
                                        he = 0;
                                    } else {
                                        he = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                                        if (*(unsigned int*)(he + 0x2c) != h) {
                                            he = 0;
                                            *slot = 0;
                                        }
                                    }
                                    if ((he == 0) || (*(int*)(he + 0x20) == 0x1000)) {
                                        chosenIdx = si;
                                        chosen = slot;
                                    }
                                }
                            }
                            slotOff = slotOff + 0x34;
                        }

                        if (slot != (unsigned int*)0) {
                            double tx = (double)(float)pr[0x30];
                            double tz = (double)(float)pr[0x32];
                            float tl = (float)(tz * tz +
                                (double)(float)(tx * tx + (double)FLOAT_80615fac));
                            if (FLOAT_80615ff4 < tl) {
                                float iv = 1.0f / SQRT(tl);
                                iv = iv * -(FLOAT_80615fe4 * tl * iv * iv - FLOAT_80615fe8);
                                double sc = (double)(FLOAT_80615fa8 *
                                    iv * -(FLOAT_80615fe4 * tl * iv * iv - FLOAT_80615fe8));
                                tx = (double)(float)(tx * sc);
                                tz = (double)(float)(tz * sc);
                            }
                            if ((float)(tx * nx + (double)(float)(tz * ny)) < FLOAT_80615ff8) {
                                int* mesh = *(int**)(*(int*)(entity + 4) + 4);
                                float* vtx = (float*)(mesh[*mesh * -0x30 +
                                    ((int)slot[1] >> 0x10) * 0x30 + 0x1b] +
                                    (slot[1] & 0xffff) * 0x18);
                                float rp[3];
                                rp[0] = vtx[0]; rp[1] = vtx[1]; rp[2] = vtx[2];
                                MTX4_XFORM_VEC(objMtx, rp);
                                double wy = (double)(rp[1] + *(float*)(entity + 0x68));
                                rp[0] = (rp[0] + *(float*)(entity + 100)) - (float)pr[0x2d];
                                rp[1] = (float)(wy - (double)(float)pr[0x2e]);
                                rp[2] = (rp[2] + *(float*)(entity + 0x6c)) - (float)pr[0x2f];
                                subobj_grab_collision_filter(entity, 0);
                                int hit = SCENE_RAYCAST(pr[0x37b], pr + 0x2d, rp, 0,
                                    (float*)0, (float*)0, (int*)0, 0x2111078, 0, (int*)0);
                                subobj_grab_collision_filter(entity, 1);
                                if (hit == 0) {
                                    unsigned int th = *slot;
                                    int* target;
                                    if (th == 0) {
                                        target = (int*)0;
                                    } else {
                                        target = (int*)(DAT_80611c24 + (th >> 0x16) * 0x2e8);
                                        if ((unsigned int)target[0xb] != th) {
                                            target = (int*)0;
                                            *slot = 0;
                                        }
                                    }
                                    unsigned int held;
                                    if (((target == (int*)0) || (target[0x54] == 0)) &&
                                        (held = input_get_buttons_held(pr[0]),
                                         (pr[8] & held) != 0)) {
                                        if ((target != (int*)0) && (target[8] == 0x1000)) {
                                            entity_destroy_with_event(target);
                                            target = (int*)0;
                                        }
                                        if (target == (int*)0) {
                                            subobj_grab_commit_attach((void*)target, wy, entity, pr[5], outIdx);
                                        } else {
                                            subobj_grab_attach_release(entity, target, 0, (int)slot[3], 1);
                                        }
                                        input_clear_buttons(pi, pr[8]);
                                    } else {
                                        unsigned int st = slot[0xc];
                                        int prompt = -1;
                                        if ((int)st < 3) {
                                            if (st == 0) prompt = 0;
                                            else if (-1 < (int)st) prompt = 1;
                                        } else if ((int)st < 5) {
                                            prompt = 2;
                                        }
                                        if ((prompt != -1) && (-1 < pr[1])) {
                                            hud_prompt_bar_charge_slot(pr[1], pr[8], (float)prompt);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            recStride = recStride + 0x141c;
        }
    }
    FUN_80382710();
}

// ===========================================================================
// 0x80150af0  subobj_carry_physics_tick
// ---------------------------------------------------------------------------
// MASSIVE carried-object physics per-frame update. Drives the held/carried
// body: applies carry lift force, queries the floor, updates the carry state
// machine (+0x54: 1=seeking, 2/3=carry, 4=throw), dispatches per-slot grab
// animations, integrates a two-channel rotor angle, and finishes by ticking the
// engine/rotor 3D sound. Ends with subobj_engine_sound_tick().
// ===========================================================================
void subobj_carry_physics_tick(void)
{
    int entity = FUN_803826cc();
    unsigned int handle = *(unsigned int*)(entity + 0x80);
    float* impulse = (float*)subobj_get_linear_impulse(handle);
    float* objMtx = (float*)subobj_get_object_matrix(handle);
    double invMass = subobj_get_inverse_mass(handle);
    int rec = *(int*)(entity + 0x7c);
    double frameInv = (double)(FLOAT_806108ec *
        (FLOAT_80615fa8 /
         (float)((double)(unsigned int)(DAT_806109c0) - DOUBLE_80615fd8)));

    if (DAT_806108e4 != 0) {
        scalar_step_toward_snap((double)FLOAT_80615fac, (double)FLOAT_80615fb8,
                                (float*)(rec + 0x96c));
        if (subobj_is_idle(handle) == 0) {
            *(unsigned int*)(entity + 0x18) = *(unsigned int*)(entity + 0x18) | 0x40;
            *(unsigned int*)(rec + 0x58) = *(unsigned int*)(rec + 0x58) & 0xfff9ffff;
            if (*(void**)(rec + 0x68) != (void*)0) {
                ((void (*)(int))(*(void**)(rec + 0x68)))(entity);
            }
            float lift = (float)((double)FLOAT_80616004 * invMass) *
                *(float*)(*(int*)(rec + 0x50) + 0x98);
            if (((*(unsigned int*)(rec + 0x58) & 0x20) != 0) &&
                ((*(unsigned int*)(rec + 0x58) & 0x80000) != 0)) {
                lift = lift * FLOAT_80616008;
            }
            float force[3];
            force[0] = FLOAT_80615fac; force[1] = lift; force[2] = FLOAT_80615fac;
            subobj_add_linear_force(*(unsigned int*)(entity + 0x80), force);
            subobj_carry_ground_effect();
            subobj_rotor_rev_settle();
            subobj_rotor_integrate(entity);

            double ux, uy, uz;
            if ((*(unsigned int*)(rec + 0x58) & 0x100000) == 0) {
                ux = (double)objMtx[8]; uy = (double)objMtx[9]; uz = (double)objMtx[10];
            } else {
                ux = -(double)objMtx[8]; uy = -(double)objMtx[9]; uz = -(double)objMtx[10];
            }
            int mode = *(int*)(rec + 0x54);
            if ((((mode == 1) || (mode - 2U < 2)) || (mode == 4)) &&
                (FLOAT_8061600c < objMtx[5])) {
                *(int*)(rec + 0x54) = 0;
            }
            double floorH = (double)FLOOR_QUERY(
                (double)FLOAT_80615fa8, (double)FLOAT_80615fa8,
                entity + 100, entity + 0x21c, (int*)0, 0);
            if ((double)FLOAT_80616010 != floorH) {
                *(float*)(rec + 0x558) = (float)floorH;
            }
            if ((*(unsigned int*)(rec + 0x58) & 1) == 0) {
                float d = (float)(uz * (double)impulse[2] +
                    (double)(float)(ux * (double)impulse[0] +
                        (double)(float)(uy * (double)impulse[1])));
                if (d < FLOAT_80615fac) d = FLOAT_80615fac;
                *(float*)(rec + 0x52c) = d;
            } else {
                *(float*)(rec + 0x52c) =
                    ABS((float)(uz * (double)impulse[2] +
                        (double)(float)(ux * (double)impulse[0] +
                            (double)(float)(uy * (double)impulse[1]))));
            }
        } else {
            *(float*)(rec + 0x52c) = FLOAT_80615fac;
            *(float*)(rec + 0x944) = FLOAT_80615fac;
            *(int*)(rec + 0x940) = 0;
        }
        *(float*)(rec + 0x534) = FLOAT_80615fac;
        *(float*)(rec + 0x530) = FLOAT_80615fac;
        subobj_deform_relax();
        subobj_grab_detect_tick();

        int held = 0;
        int found = subobj_find_active_grab(entity, &held);
        if (((found != 0) && (held != 0)) && (*(int*)(held + 0x150) != 0)) {
            *(unsigned int*)(rec + 0x5b0) =
                *(unsigned int*)(*(int*)(held + 0x7c) + 0x678);
        }

        int mode = *(int*)(rec + 0x54);
        if (mode == 1) {
            if ((*(unsigned int*)(rec + 0x58) & 0x10) == 0) {
                int stride = 0;
                for (int pi = 0; pi < DAT_80612610; pi = pi + 1) {
                    int pent = *(int*)(DAT_80611d90 + stride);
                    int* pr = *(int**)(pent + 0x150);
                    unsigned int hb = input_get_buttons_held(*pr);
                    if (((pr[8] & hb) != 0) && (pr[0x45] == 2)) {
                        float* co = (float*)subobj_get_contact_origin(*(unsigned int*)(entity + 0x80));
                        double dx = (double)((*(float*)(pent + 100) + *(float*)(pent + 0x1a8)) - co[0]);
                        double dy = (double)((*(float*)(pent + 0x68) + *(float*)(pent + 0x1ac)) - co[1]);
                        double dz = (double)((*(float*)(pent + 0x6c) + *(float*)(pent + 0x1b0)) - co[2]);
                        if ((ABS(dx) < (double)(FLOAT_80615fa8 + *(float*)(entity + 0x1c0))) &&
                            ((ABS(dy) < (double)(FLOAT_80615fa8 + *(float*)(entity + 0x1c4)) &&
                             (ABS(dz) < (double)(FLOAT_80615fa8 + *(float*)(entity + 0x1c8)))))) {
                            anim_play_scaled((double)FLOAT_80616008, (double)FLOAT_80616018);
                            subobj_dirty_list_add(handle);
                            input_clear_buttons(pi, pr[8]);
                            if (ABS(objMtx[9]) <= FLOAT_8061601c) {
                                if ((float)((double)objMtx[2] * dz +
                                    (double)(float)((double)objMtx[0] * dx +
                                        (double)(float)((double)objMtx[1] * dy))) <= FLOAT_80615fac) {
                                    *(int*)(rec + 0x54) = 3;
                                } else {
                                    *(int*)(rec + 0x54) = 2;
                                }
                            } else {
                                *(int*)(rec + 0x54) = 4;
                            }
                            break;
                        }
                    }
                    stride = stride + 0xea0;
                }
            }
        } else if (mode < 1) {
            if (-1 < mode) {
                bool anyHeld = false;
                int slotOff = 0;
                for (int i = 0; i < *(int*)(rec + 0x140); i = i + 1) {
                    unsigned int* slot = (unsigned int*)(rec + slotOff + 0x70);
                    unsigned int h = *slot;
                    int he;
                    if (h == 0) {
                        he = 0;
                    } else {
                        he = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                        if (*(unsigned int*)(he + 0x2c) != h) {
                            he = 0;
                            *slot = 0;
                        }
                    }
                    if (he != 0) {
                        int link = *(int*)(he + 0x150);
                        if (anyHeld || (link != 0)) anyHeld = true;
                        else anyHeld = false;
                        int gunEnt = (*(int*)(he + 0x20) == 8) ? *(int*)(he + 0x7c) : 0;
                        if ((link == 0) || ((*(unsigned int*)(link + 0x114) & 0x410) != 0)) {
                            subobj_deform_bone_impulse(entity, he);
                            int clipA = (slot[2] == 0) ? 0 : anim_is_clip_active(he, slot[2]);
                            if (clipA == 0) {
                                int clipB = (slot[3] == 0) ? 0 : anim_is_clip_active(he, slot[3]);
                                if (clipB == 0) {
                                    if ((-1 < (int)slot[0xb]) &&
                                        (((int)slot[0xb] < *(int*)(rec + 0x164) &&
                                          (((slot[0xc] == 4 || (slot[0xc] == 3)) && (link != 0)))) &&
                                         (((gunEnt = *(int*)(link + 8), gunEnt != 0 &&
                                            (*(int**)(gunEnt + 0x26c) != (int*)0)) &&
                                           (**(int**)(gunEnt + 0x26c) == 0))))) {
                                        *(unsigned int*)(gunEnt + 0x21c) =
                                            *(unsigned int*)(gunEnt + 0x21c) | 0x10000;
                                    }
                                    if (((link != 0) && (gunEnt = *(int*)(link + 8), gunEnt != 0)) &&
                                        ((FLOAT_80615fa8 <= *(float*)(gunEnt + 0xfc8) &&
                                          ((slot[0xc] != 4 && (slot[0xc] != 3)))))) {
                                        if (((double)FLOAT_80615fac == (double)*(float*)(gunEnt + 0xfe0)) ||
                                            ((double)FLOAT_80616014 <=
                                             ABS((double)(*(float*)(he + 0x70) - *(float*)(entity + 0x70))))) {
                                            camera_set_mode_by_index(gunEnt, 4, *(int*)(link + 0x14));
                                            *(float*)(gunEnt + 0xfc8) = FLOAT_80615fac;
                                        } else {
                                            *(float*)(gunEnt + 0xfe8) =
                                                (float)((double)*(float*)(gunEnt + 0xfe0) -
                                                        (double)*(float*)(gunEnt + 0xfe8));
                                            camera_set_orbit_angles(0.0, 0.0, 0.0, 0.0, gunEnt,
                                                *(int*)(*(int*)(rec + 0x50) + 0xac));
                                        }
                                    }
                                    if ((gunEnt == 0) || ((*(unsigned int*)(gunEnt + 0x24) & 0x108) != 0)) {
                                        if (((*(int*)(he + 0x20) == 8) &&
                                             ((link == 0 || (*(int*)(link + 0x114) != 0x10)))) &&
                                            ((gunEnt == 0 || ((*(unsigned int*)(gunEnt + 0x24) & 8) == 0)))) {
                                            subobj_grab_anim_dispatch(handle, he, (int)slot);
                                        }
                                    } else {
                                        subobj_grab_attach_release(entity, (void*)he, 0, 0, 1);
                                    }
                                } else {
                                    int uf = anim_active_slot_userflags(*(int*)(he + 4));
                                    if ((uf != 0) || (uf = subobj_carry_anim_event_scan(), uf != 0)) {
                                        subobj_carry_pose_commit();
                                        model_render_with_override_b();
                                        subobj_grab_release_solve_pose(entity, slot, (float*)(he + 100), (float*)(he + 0x70));
                                        subobj_grab_release_commit(entity, he, 0, 1);
                                        if (gunEnt != 0) {
                                            int near = soundpad_find_nearest_entity(
                                                (double)FLOAT_80615fac, (float*)(he + 100));
                                            *(int*)(gunEnt + 0x678) = near;
                                        }
                                        if (subobj_grab_aim_overlap(entity, he) != 0) {
                                            // p2 is unused ABI scratch in try_teleport
                                            subobj_grab_try_teleport((void*)0, (double)FLOAT_80615fac, entity, he, 0);
                                        }
                                    }
                                }
                            } else {
                                int uf = anim_active_slot_userflags(*(int*)(he + 4));
                                if (uf != 0) {
                                    subobj_grab_anim_dispatch(handle, he, (int)slot);
                                    model_render_with_override_b();
                                    subobj_carry_pose_commit();
                                    if ((*(unsigned int*)(rec + 0x58) & 2) != 0) {
                                        *(unsigned int*)(he + 0x28) =
                                            *(unsigned int*)(he + 0x28) & 0xfffffffe;
                                    }
                                }
                            }
                        } else {
                            subobj_grab_attach_release(entity, (void*)he, 0, 0, 0);
                        }
                    }
                    slotOff = slotOff + 0x34;
                }
                if (anyHeld) {
                    *(int*)(rec + 0x55c) = *(int*)(rec + 0x55c) + DAT_806108e4;
                } else {
                    *(int*)(rec + 0x55c) = 0;
                }
                subobj_rotor_slap_detach();
            }
        } else if (mode < 5) {
            float* cn = (float*)subobj_get_contact_normal(*(unsigned int*)(entity + 0x80));
            *(unsigned int*)(rec + 0x534) = *(unsigned int*)(*(int*)(rec + 0x50) + 8);
            subobj_dirty_list_add(handle);
            if (cn[2] * cn[2] + cn[0] * cn[0] + cn[1] * cn[1] < FLOAT_80615ff0) {
                float* co = (float*)subobj_get_contact_origin(*(unsigned int*)(entity + 0x80));
                int m = *(int*)(rec + 0x54);
                unsigned int axis = -(unsigned int)(m == 4) & 2;
                float pt[3];
                pt[0] = FLOAT_80615fcc * objMtx[axis * 4] + co[0];
                float mag;
                pt[1] = FLOAT_80615fcc * objMtx[axis * 4 + 1] + co[1];
                pt[2] = FLOAT_80615fcc * objMtx[axis * 4 + 2] + co[2];
                if (m == 2) {
                    mag = (float)((double)FLOAT_80616020 * invMass);
                } else if (m == 3) {
                    mag = (float)((double)FLOAT_80615fe4 * invMass);
                } else {
                    mag = (float)(invMass * (double)(FLOAT_80615fe4 *
                        -(float)((unsigned int)objMtx[9] & 0x80000000 | 0x3f800000)));
                }
                float tq[3];
                tq[0] = objMtx[4] * mag;
                tq[1] = objMtx[5] * mag;
                tq[2] = objMtx[6] * mag;
                subobj_add_torque_from_offset(handle, pt, tq);
                tq[0] = tq[0] * FLOAT_80616024;
                tq[1] = tq[1] * FLOAT_80616024;
                pt[0] = (pt[0] - co[0]) * FLOAT_80616024 + co[0];
                tq[2] = tq[2] * FLOAT_80616024;
                pt[1] = (pt[1] - co[1]) * FLOAT_80616024 + co[1];
                pt[2] = (pt[2] - co[2]) * FLOAT_80616024 + co[2];
                subobj_add_torque_from_offset(handle, pt, tq);
            }
        }
    }

    *(float*)(rec + 0x53c) = impulse[0];
    *(float*)(rec + 0x540) = impulse[1];
    *(float*)(rec + 0x544) = impulse[2];

    // Two-channel rotor angle integrator (only for sub-object subtype 0xb).
    if (*(short*)(entity + 0x94) == 0xb) {
        int count = *(int*)(*(int*)(rec + 0x50) + 0x9c);
        // Rotor accumulator seeds are stored as raw DATA words; read their
        // float bit-patterns. (local_e4[0..1] = lo seeds, [2..3] = hi seeds.)
        float chan[4];
        chan[0] = *(float*)&DAT_80615ffc;
        chan[1] = *(float*)&DAT_80616000;
        chan[2] = *(float*)&DAT_8061c180;
        chan[3] = *(float*)&DAT_8061c184;
        int aoff = 0;
        if (0 < count) {
            do {
                int a = rec + aoff + 0x174;
                unsigned int lo = (unsigned int)(*(float*)(a + 0x28) <= FLOAT_80615fac);
                aoff = aoff + 100;
                chan[lo + 2] = chan[lo + 2] + *(float*)(a + 0x58);
                count = count - 1;
            } while (count != 0);
        }
        int scratch = MEM_ADVANCE_SECONDARY(0x80);
        double dt = (double)FLOAT_80615fa8;
        double wrapRate = (double)FLOAT_80615fb8;
        int ch = 0;
        int coff = 0;
        int soff = 0;
        do {
            int a = rec + coff;
            int half = *(int*)(*(int*)(rec + 0x50) + 0x9c) >> 1;
            float target = (float)((double)FLOAT_80615fd0 *
                (double)((float)(dt / (float)((double)(unsigned int)half - DOUBLE_80615fd8)) *
                         chan[ch + 2])) - *(float*)(a + 0x550);
            double aTarget = ABS((double)target);
            double step = wrapRate;
            if (aTarget < wrapRate) step = aTarget;
            *(float*)(a + 0x550) =
                (float)((double)(float)((unsigned int)target & 0x80000000 | 0x3f800000) * step +
                        (double)*(float*)(a + 0x550));
            double acc = (double)(float)((double)*(float*)(a + 0x550) * frameInv +
                                         (double)*(float*)(a + 0x548));
            double wrapped = FUN_80390324((double)(float)((double)(float)(acc - (double)FLOAT_80615fac) / dt));
            *(float*)(a + 0x548) = -(float)(dt * (double)(float)wrapped - acc);
            MTX4_SET_XLATE((double)*(float*)(a + 0x548), 0.0, 0.0);
            collision_set_face_entry(*(int*)(entity + 4),
                *(int*)((int)chan + coff), scratch + soff);
            ch = ch + 1;
            coff = coff + 4;
            soff = soff + 0x40;
        } while (ch < 2);
    }
    subobj_engine_sound_tick();
    FUN_80382718();
}

// ===========================================================================
// 0x80151708  subobj_grab_ik_solve
// ---------------------------------------------------------------------------
// Per-slot IK angle-delta solve. For each occupied grab slot with an attached
// body, computes wrapped pitch/yaw deltas between the body's current and target
// facing, then either drives the attach spring (link present) or the free-swing
// angular velocity (unlinked prop).
// ===========================================================================
void subobj_grab_ik_solve(void)
{
    int entity = __save_gpr_28();
    int rec = *(int*)(entity + 0x7c);
    int slotOff = 0;
    for (int i = 0; i < *(int*)(rec + 0x140); i = i + 1) {
        float gainY = FLOAT_80616030;
        int* slot = (int*)(rec + slotOff + 0x70);
        int ap = slot[0xb];
        if (((-1 < ap) && (ap < *(int*)(rec + 0x164))) &&
            ((*(int**)(rec + ap * 0x10 + 0x144))[0x36] != 0)) {
            int* attach = *(int**)(rec + ap * 0x10 + 0x144);
            int body = attach[0];
            float cur = *(float*)(body + 0x104);
            float base = *(float*)(body + 0xc0);
            float dHi = base - (cur + FLOAT_80616028);
            float dMid = ABS(base - cur);
            float dLo = base - (cur - FLOAT_80616028);
            float aHi = ABS(dHi);
            if (aHi <= dMid) {
                if (aHi < ABS(dLo)) dLo = dHi;
            } else if (dMid < ABS(dLo)) {
                dLo = base - cur;
            }
            float pitchDelta = dLo;

            cur = *(float*)(body + 0x108);
            base = *(float*)(body + 0x70);
            dHi = base - (cur + FLOAT_80616028);
            aHi = ABS(base - cur);
            dLo = base - (cur - FLOAT_80616028);
            float aHi2 = ABS(dHi);
            if (aHi2 <= aHi) {
                if (aHi2 < ABS(dLo)) dLo = dHi;
            } else if (aHi < ABS(dLo)) {
                dLo = base - cur;
            }
            float yawDelta = dLo;

            int link = attach[0x36];
            if (*(int*)(link + 0x150) == 0) {
                if ((*(int*)(link + 0x20) == 8) && (*(int*)(*(int*)(link + 0x7c) + 4) == 0)) {
                    attach[0x13] = (int)-(FLOAT_8061602c * pitchDelta - (float)attach[0x13]);
                    attach[0x14] = (int)-(gainY * yawDelta - (float)attach[0x14]);
                }
            } else {
                attach[7] = (int)((float)attach[7] - pitchDelta);
                attach[9] = (int)((float)attach[9] - yawDelta);
                float lo = (float)attach[0xd];
                float hi = (float)attach[0xe];
                if (lo != hi) {
                    float v = (float)attach[7];
                    float c = v;
                    if (hi < v) c = hi;
                    if ((lo <= c) && (lo = v, hi < v)) lo = hi;
                    attach[7] = (int)lo;
                }
                lo = (float)attach[0xf];
                hi = (float)attach[0x10];
                if (lo != hi) {
                    float v = (float)attach[9];
                    float c = v;
                    if (hi < v) c = hi;
                    if ((lo <= c) && (lo = v, hi < v)) lo = hi;
                    attach[9] = (int)lo;
                }
            }
        }
        if (*slot != 0) {
            subobj_carry_pose_commit();
        }
        slotOff = slotOff + 0x34;
    }
    __restore_gpr_28();
}

// ===========================================================================
// 0x8015196c  subobj_grab_anim_dispatch
// ---------------------------------------------------------------------------
// Grab-state animation dispatch for one slot. Branches on the slot's grab state
// (*(rec+0x30): 0=idle/attach, 2/other=hold, 4=throw) and plays the appropriate
// clip on the body, applying a pitch bias to the held body's turn rate when a
// secondary clip is running.
// ===========================================================================
void subobj_grab_anim_dispatch(unsigned int param_1, int param_2, int param_3)
{
    (void)param_1; (void)param_2;
    unsigned long long fr = FUN_803826e4();
    unsigned int handle = (unsigned int)((unsigned long long)fr >> 0x20);
    int slot = (int)fr;
    int body = subobj_get_entity(handle);
    int bodyDef = *(int*)(slot + 4);
    int state = *(int*)(param_3 + 0x30);
    int rec = *(int*)(body + 0x7c);
    int grabDef = *(int*)(DAT_80611824 + *(int*)(bodyDef + 0xd0) * 4);

    if (state == 2) goto done;
    if (state < 2) {
        if (state == 0) {
            if ((*(int*)(param_3 + 0x14) == 0) ||
                (anim_is_clip_active(slot, *(int*)(param_3 + 0x14)) == 0)) {
                int a = anim_is_clip_active(slot, *(int*)(param_3 + 0x10));
                if (a == 0) {
                    if (anim_active_slot_userflags(*(int*)(slot + 4)) != 0) {
                        anim_play_scaled((double)FLOAT_80615fac, (double)FLOAT_80615fa8);
                    }
                } else {
                    float f = *(float*)(rec + 0x538);
                    if (f <= FLOAT_80615fac) {
                        *(float*)(bodyDef + 0xd4) = FLOAT_80616034 * f + FLOAT_80615fe4;
                    } else {
                        *(float*)(bodyDef + 0xd4) = FLOAT_80616008 * f;
                    }
                    *(float*)(bodyDef + 0xd4) =
                        *(float*)(bodyDef + 0xd4) * *(float*)(grabDef + 0x18);
                }
            } else {
                if (anim_active_slot_userflags(*(int*)(slot + 4)) != 0) {
                    anim_play_scaled((double)FLOAT_80615fac, (double)FLOAT_80615fa8);
                    model_render_with_override_b();
                    subobj_get_entity(handle);
                    subobj_carry_pose_commit();
                }
            }
            goto done;
        }
        if (state < 0) goto done;
    } else {
        if (state == 4) {
            if ((anim_is_clip_active(slot, *(int*)(param_3 + 0x10)) == 0) &&
                ((anim_active_slot_userflags(*(int*)(slot + 4)) != 0) ||
                 (*(int*)(param_3 + 8) == 0))) {
                anim_play_scaled((double)FLOAT_80615fac, (double)FLOAT_80615fa8);
            }
            goto done;
        }
        if (3 < state) goto done;
    }
    if ((anim_is_clip_active(slot, *(int*)(param_3 + 0x10)) == 0) &&
        ((anim_active_slot_userflags(*(int*)(slot + 4)) != 0) ||
         (*(int*)(param_3 + 8) == 0))) {
        anim_play_scaled((double)FLOAT_80615fac, (double)FLOAT_80615fa8);
    }
done:
    FUN_80382730();
}

// ===========================================================================
// 0x80151b5c  subobj_carry_toggle_anim
// ---------------------------------------------------------------------------
// Chooses and plays the carry pick-up / put-down animation based on the
// carryable's vertical hint (+0x1cc). Returns 1 when the "raise" branch played,
// otherwise 0.
// ===========================================================================
unsigned int subobj_carry_toggle_anim(int param_1)
{
    int def = subobj_grab_slot_lookup(*(int**)(param_1 + 0x7c));
    double zero = (double)FLOAT_80615fac;
    if (zero < (double)*(float*)(param_1 + 0x1cc)) {
        if (*(int*)(def + 0x1c) != 0) {
            anim_play_scaled(zero, (double)FLOAT_80615fa8);
            return 0;
        }
    } else if (*(int*)(def + 0x18) != 0) {
        anim_play_scaled(zero, (double)FLOAT_80615fa8);
        return 1;
    }
    return 0;
}

// ===========================================================================
// 0x80151be4  subobj_carried_slot_flags
// ---------------------------------------------------------------------------
// For a grabber entity that is currently carrying (state flag 0x400 at +0x114),
// finds this entity's slot in the carried body's grab table and returns the
// slot's "held" boolean derived from countLeadingZeros(rec+0xa0)>>5 (1 when the
// slot value is zero). Returns 0 when not carried.
// ===========================================================================
unsigned int subobj_carried_slot_flags(int param_1)
{
    int* self = *(int**)(*(int*)(param_1 + 0x14) + 0x7c);
    if ((self != (int*)0) && (*(int*)(param_1 + 0x114) == 0x400)) {
        unsigned int link = (unsigned int)self[0x302];
        int he;
        if (link == 0) {
            he = 0;
        } else {
            he = DAT_80611c24 + (link >> 0x16) * 0x2e8;
            if (*(unsigned int*)(he + 0x2c) != link) {
                he = 0;
                self[0x302] = 0;
            }
        }
        if (he != 0) {
            int rec = *(int*)(he + 0x7c);
            int off = 0;
            int n = *(int*)(rec + 0x140);
            if (0 < n) {
                do {
                    if (*(int*)(rec + off + 0x70) == *(int*)(*self + 0x2c)) {
                        unsigned int clz = countLeadingZeros(*(unsigned int*)(rec + off + 0xa0));
                        return clz >> 5;
                    }
                    off = off + 0x34;
                    n = n + -1;
                } while (n != 0);
            }
        }
    }
    return 0;
}

// ===========================================================================
// 0x80151c94  subobj_is_carried_by
// ---------------------------------------------------------------------------
// Returns 1 if the grabber entity (param_1) currently occupies a slot in its
// linked carried body's grab table, otherwise 0.
// ===========================================================================
unsigned int subobj_is_carried_by(int param_1)
{
    int* self = *(int**)(*(int*)(param_1 + 0x14) + 0x7c);
    if (self != (int*)0) {
        unsigned int link = (unsigned int)self[0x302];
        int he;
        if (link == 0) {
            he = 0;
        } else {
            he = DAT_80611c24 + (link >> 0x16) * 0x2e8;
            if (*(unsigned int*)(he + 0x2c) != link) {
                he = 0;
                self[0x302] = 0;
            }
        }
        if (he != 0) {
            int off = 0;
            int n = *(int*)(*(int*)(he + 0x7c) + 0x140);
            if (0 < n) {
                do {
                    if (*(int*)(*(int*)(he + 0x7c) + off + 0x70) == *(int*)(*self + 0x2c)) {
                        return 1;
                    }
                    off = off + 0x34;
                    n = n + -1;
                } while (n != 0);
            }
        }
    }
    return 0;
}

// ===========================================================================
// 0x80151d2c  subobj_find_active_grab
// ---------------------------------------------------------------------------
// Scans the sub-object's grab table for the first occupied slot (handle != 0)
// whose "released" flag (+0xa0) is clear and whose handle still resolves to a
// live entity. Writes the resolved entity pointer to *param_2 (0 if none) and
// returns 1 on hit, 0 otherwise.
// ===========================================================================
unsigned int subobj_find_active_grab(int param_1, int* param_2)
{
    int rec = *(int*)(param_1 + 0x7c);
    int idx = 0;
    int off = 0;
    do {
        if (*(int*)(rec + 0x140) <= idx) {
            if (param_2 != (int*)0) *param_2 = 0;
            return 0;
        }
        int e = rec + off;
        unsigned int h = *(unsigned int*)(e + 0x70);
        if ((h != 0) && (*(int*)(e + 0xa0) == 0)) {
            int he;
            if (h == 0) {
                he = 0;
            } else {
                he = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                if (*(unsigned int*)(he + 0x2c) != h) {
                    he = 0;
                    *(int*)(e + 0x70) = 0;
                }
            }
            if (he != 0) {
                if (param_2 != (int*)0) *param_2 = he;
                return 1;
            }
        }
        idx = idx + 1;
        off = off + 0x34;
    } while (true);
}

// ===========================================================================
// 0x80151dd4  subobj_carried_anim_poll
// ---------------------------------------------------------------------------
// For the player's carried body, finds the player's grab slot and polls its
// two anim clips (+0x08 / +0x0c) for activity (side effect only inside the anim
// system). Frame-registered helper invoked from the player aim-input handler.
// ===========================================================================
void subobj_carried_anim_poll(void)
{
    int* self = (int*)FUN_803826f0();
    unsigned int link = (unsigned int)self[0x302];
    int he;
    if (link == 0) {
        he = 0;
    } else {
        he = DAT_80611c24 + (link >> 0x16) * 0x2e8;
        if (*(unsigned int*)(he + 0x2c) != link) {
            he = 0;
            self[0x302] = 0;
        }
    }
    if (he != 0) {
        int off = 0;
        int n = *(int*)(*(int*)(he + 0x7c) + 0x140);
        if (0 < n) {
            do {
                int* slot = (int*)(*(int*)(he + 0x7c) + off + 0x70);
                if (*slot == *(int*)(*self + 0x2c)) {
                    if (((slot[2] == 0) || (anim_is_clip_active(*self, slot[2]) == 0)) &&
                        (slot[3] != 0)) {
                        anim_is_clip_active(*self, slot[3]);
                    }
                    break;
                }
                off = off + 0x34;
                n = n + -1;
            } while (n != 0);
        }
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x80151ec8  subobj_carried_slot_value
// ---------------------------------------------------------------------------
// Returns the carried-slot value (+0xa0) for the grabber's slot in its linked
// body's grab table, or 0xffffffff when not carried / no matching slot.
// ===========================================================================
unsigned int subobj_carried_slot_value(int* param_1)
{
    unsigned int link = (unsigned int)param_1[0x302];
    int he;
    if (link == 0) {
        he = 0;
    } else {
        he = DAT_80611c24 + (link >> 0x16) * 0x2e8;
        if (*(unsigned int*)(he + 0x2c) != link) {
            he = 0;
            param_1[0x302] = 0;
        }
    }
    if (he != 0) {
        int rec = *(int*)(he + 0x7c);
        int off = 0;
        int n = *(int*)(rec + 0x140);
        if (0 < n) {
            do {
                if (*(int*)(rec + off + 0x70) == *(int*)(*param_1 + 0x2c)) {
                    return *(unsigned int*)(rec + off + 0xa0);
                }
                off = off + 0x34;
                n = n + -1;
            } while (n != 0);
        }
    }
    return 0xffffffff;
}

// ===========================================================================
// batch_425 | 0x80151f54 - 0x801525c0 | 2026-09-06
// Grab/carry follow-on: grab-table accessors, the collision-flag raycast
// filter (subobj_grab_collision_filter, referenced above by
// subobj_grab_detect_tick), and the carried-weapon aim IK.
// ===========================================================================

// ===========================================================================
// 0x80151f54  subobj_grab_slot_lookup
// ---------------------------------------------------------------------------
// Given a grabber entity's render-data (param_1), resolves its carried body via
// link slot [0x302] and returns the pointer to this entity's grab-slot record
// (rec+0x70 + i*0x34) whose handle matches *(entity+0x2c), or 0 if not carried.
// ===========================================================================
int subobj_grab_slot_lookup(int* param_1)
{
    unsigned int link = (unsigned int)param_1[0x302];
    int he;
    if (link == 0) {
        he = 0;
    } else {
        he = DAT_80611c24 + (link >> 0x16) * 0x2e8;
        if (*(unsigned int*)(he + 0x2c) != link) {
            he = 0;
            param_1[0x302] = 0;
        }
    }
    if (he != 0) {
        int rec = *(int*)(he + 0x7c);
        int off = 0;
        int n = *(int*)(rec + 0x140);
        if (0 < n) {
            do {
                int slot = rec + off;
                if (*(int*)(slot + 0x70) == *(int*)(*param_1 + 0x2c)) {
                    return slot + 0x70;
                }
                off = off + 0x34;
                n = n + -1;
            } while (n != 0);
        }
    }
    return 0;
}

// ===========================================================================
// 0x80151fdc  subobj_grab_attach_point
// ---------------------------------------------------------------------------
// Resolves the attach-point record (rec+0x144 + i*0x10) for the grabber's slot
// via subobj_grab_slot_lookup, using the slot's attach-point index (+0x2c).
// Returns 0 when not carried or the index is out of range.
// ===========================================================================
int subobj_grab_attach_point(int* param_1)
{
    unsigned int link = (unsigned int)param_1[0x302];
    int he;
    if (link == 0) {
        he = 0;
    } else {
        he = DAT_80611c24 + (link >> 0x16) * 0x2e8;
        if (*(unsigned int*)(he + 0x2c) != link) {
            he = 0;
            param_1[0x302] = 0;
        }
    }
    if (he != 0) {
        int rec = *(int*)(he + 0x7c);
        int slot = subobj_grab_slot_lookup(param_1);
        if ((slot != 0) && (rec != 0)) {
            int idx = *(int*)(slot + 0x2c);
            if ((-1 < idx) && (idx < *(int*)(rec + 0x164))) {
                return rec + idx * 0x10 + 0x144;
            }
        }
    }
    return 0;
}

// ===========================================================================
// 0x80152084  subobj_grab_slot_by_index
// ---------------------------------------------------------------------------
// Returns the grab-slot record pointer for a given index (no bounds check).
// ===========================================================================
int subobj_grab_slot_by_index(int param_1, int param_2)
{
    return *(int*)(param_1 + 0x7c) + param_2 * 0x34 + 0x70;
}

// ===========================================================================
// 0x80152098  subobj_grab_slot_ring_advance
// ---------------------------------------------------------------------------
// Clamps *param_2 to [0, count-1], returns that slot's record pointer, and
// advances *param_2 to the next slot modulo count (ring iterator).
// ===========================================================================
int subobj_grab_slot_ring_advance(int param_1, unsigned int* param_2)
{
    int rec = *(int*)(param_1 + 0x7c);
    unsigned int idx = *param_2;
    unsigned int hi = *(int*)(rec + 0x140) - 1;
    // clamp to >= 0 (arithmetic-shift trick) then to <= count-1
    unsigned int clamped = idx & ~((int)idx >> 0x1f);
    if ((int)clamped <= (int)hi) {
        hi = clamped;
    }
    *param_2 = hi;
    unsigned int cur = *param_2;
    int nxt = cur + 1;
    *param_2 = nxt - (nxt / *(int*)(rec + 0x140)) * *(int*)(rec + 0x140);
    return rec + cur * 0x34 + 0x70;
}

// ===========================================================================
// 0x801520f4  subobj_count_live_grabs
// ---------------------------------------------------------------------------
// Counts grab slots whose handle resolves to a live entity (stale handles are
// cleared in place).
// ===========================================================================
int subobj_count_live_grabs(int param_1)
{
    int rec = *(int*)(param_1 + 0x7c);
    int off = 0;
    int count = 0;
    for (int i = 0; i < *(int*)(rec + 0x140); i = i + 1) {
        unsigned int h = *(unsigned int*)(rec + off + 0x70);
        int he;
        if (h == 0) {
            he = 0;
        } else {
            he = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int*)(he + 0x2c) != h) {
                he = 0;
                *(int*)(rec + off + 0x70) = 0;
            }
        }
        if (he != 0) count = count + 1;
        off = off + 0x34;
    }
    return count;
}

// ===========================================================================
// 0x80152168  subobj_carry_speed_response
// ---------------------------------------------------------------------------
// Maps a requested speed (param_1) to a signed, magnitude-limited response
// against the sub-object's current contact speed (+0x52c). When the "free"
// flag (+0x58 bit 0) is set, returns just the sign of param_1. Uses the
// fast integer-bit-hack reciprocal seed followed by a Newton refine.
// ===========================================================================
double subobj_carry_speed_response(double param_1, int param_2)
{
    int rec = *(int*)(param_2 + 0x7c);
    float result;
    if ((*(unsigned int*)(rec + 0x58) & 1) == 0) {
        float diff = ABS((float)(param_1 - (double)*(float*)(rec + 0x52c)));
        float seed = (float)(((int)diff >> 1) + 0x1fc00000);
        seed = (seed * seed + diff) / (FLOAT_80615fec * seed);
        float clamped = seed;
        if (FLOAT_80615fa8 < seed) clamped = FLOAT_80615fa8;
        float mag = FLOAT_80615fac;
        if ((FLOAT_80615fac <= clamped) && (mag = seed, FLOAT_80615fa8 < seed)) {
            mag = FLOAT_80615fa8;
        }
        float sign = (float)((unsigned int)(float)(param_1 - (double)*(float*)(rec + 0x52c))
                             & 0x80000000 | 0x3f800000);
        result = mag * sign;
    } else {
        result = (float)((unsigned int)(float)param_1 & 0x80000000 | 0x3f800000);
    }
    return (double)result;
}

// ===========================================================================
// 0x80152308  subobj_has_live_grab
// ---------------------------------------------------------------------------
// Returns 1 if the sub-object has at least one live grab slot AND is not in a
// seeking/carrying/throwing carry mode (+0x54 not 1 and not 2..4) and the
// "no-grab" flag (+0x58 bit 0x10) is clear; else 0.
// ===========================================================================
int subobj_has_live_grab(int param_1)
{
    int rec = *(int*)(param_1 + 0x7c);
    if (*(int*)(rec + 0x54) != 1) {
        if ((2 < (unsigned int)(*(int*)(rec + 0x54) - 2)) &&
            ((*(unsigned int*)(rec + 0x58) & 0x10) == 0)) {
            int off = 0;
            int i = 0;
            while (true) {
                if (*(int*)(rec + 0x140) <= i) {
                    return 0;
                }
                unsigned int h = *(unsigned int*)(rec + off + 0x70);
                int he;
                if (h == 0) {
                    he = 0;
                } else {
                    he = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                    if (*(unsigned int*)(he + 0x2c) != h) {
                        *(int*)(rec + off + 0x70) = 0;
                        he = 0;
                    }
                }
                if (he == 0) break;
                i = i + 1;
                off = off + 0x34;
            }
            return 1;
        }
    }
    return 0;
}

// ===========================================================================
// 0x80152234  subobj_carry_nearest_grab_scan
// ---------------------------------------------------------------------------
// Frame-registered scan (entry FUN_803826f0 / exit FUN_8038273c) that walks the
// intrusive list of grab-capable sub-objects (DAT_806121e4, linked via +0x00)
// and tracks the nearest live grab candidate's squared distance to the query
// point. (The winning distance is consumed by the frame wrapper.)
// ===========================================================================
void subobj_carry_nearest_grab_scan(void)
{
    float* q = (float*)FUN_803826f0();
    // Initial "best" distance comes in via the frame wrapper's FP register; the
    // decompiler modelled it as extraout_f1 squared. Seed with a large value.
    double best = (double)FLOAT_80615fe0;
    for (int* node = (int*)DAT_806121e4; node != (int*)0; node = (int*)*node) {
        if (subobj_has_live_grab(node[0x13]) != 0) {
            int body = node[0x13];
            float dx = (*(float*)(body + 100) + *(float*)(body + 0x1a8)) - q[0];
            float dy = (*(float*)(body + 0x68) + *(float*)(body + 0x1ac)) - q[1];
            float dz = (*(float*)(body + 0x6c) + *(float*)(body + 0x1b0)) - q[2];
            double d2 = (double)(dz * dz + dx * dx + dy * dy);
            if (d2 < best) best = d2;
        }
    }
    FUN_8038273c();
}

// ===========================================================================
// 0x801523b0  subobj_grab_collision_filter
// ---------------------------------------------------------------------------
// Temporarily clears collision-response flag bits (0x60) on the grabber, its
// carried grab targets, and their attach bodies so a line-of-sight raycast does
// not self-collide, then restores them. param_2==0 pushes+clears; param_2!=0
// pops+restores. The saved state is a small stack: DAT_805893e0[] entity ptrs,
// DAT_80589408[] saved flag bits, depth DAT_806121c0.
// ===========================================================================
void subobj_grab_collision_filter(int param_1, int param_2)
{
    if (param_2 != 0) {
        while (DAT_806121c0 != 0) {
            DAT_806121c0 = DAT_806121c0 - 1;
            int ent  = *(int*)(&DAT_805893e0 + DAT_806121c0 * 4);
            unsigned int saved = *(unsigned int*)(&DAT_80589408 + DAT_806121c0 * 4);
            *(unsigned int*)(ent + 0x28) = *(unsigned int*)(ent + 0x28) | saved;
            *(int*)(&DAT_805893e0 + DAT_806121c0 * 4) = 0;
        }
        return;
    }
    if (*(int*)(param_1 + 0x20) != 0x400000) {
        return;
    }
    int rec = *(int*)(param_1 + 0x7c);
    int si = DAT_806121c0 * 4;
    *(int*)(&DAT_805893e0 + si) = param_1;
    *(unsigned int*)(&DAT_80589408 + si) = *(unsigned int*)(param_1 + 0x28) & 0x60;
    *(unsigned int*)(param_1 + 0x28) =
        *(unsigned int*)(param_1 + 0x28) & ~*(unsigned int*)(&DAT_80589408 + si);
    DAT_806121c0 = DAT_806121c0 + 1;

    int off = 0;
    for (int i = 0; i < *(int*)(rec + 0x140); i = i + 1) {
        unsigned int* slot = (unsigned int*)(rec + off + 0x70);
        unsigned int h = *slot;
        int he;
        if (h == 0) {
            he = 0;
        } else {
            he = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int*)(he + 0x2c) != h) {
                he = 0;
                *slot = 0;
            }
        }
        if (he != 0) {
            int s = DAT_806121c0 * 4;
            *(int*)(&DAT_805893e0 + s) = he;
            *(unsigned int*)(&DAT_80589408 + s) = *(unsigned int*)(he + 0x28) & 0x60;
            *(unsigned int*)(he + 0x28) =
                *(unsigned int*)(he + 0x28) & ~*(unsigned int*)(&DAT_80589408 + s);
            DAT_806121c0 = DAT_806121c0 + 1;
        }
        unsigned int ap = slot[0xb];
        if (((-1 < (int)ap) && ((int)ap < *(int*)(rec + 0x164)))) {
            int* attach = *(int**)(rec + ap * 0x10 + 0x144);
            if ((attach != (int*)0) && (*attach != 0)) {
                int body = *attach;
                int s = DAT_806121c0 * 4;
                *(int*)(&DAT_805893e0 + s) = body;
                *(unsigned int*)(&DAT_80589408 + s) = *(unsigned int*)(body + 0x28) & 0x60;
                *(unsigned int*)(body + 0x28) =
                    *(unsigned int*)(body + 0x28) & ~*(unsigned int*)(&DAT_80589408 + s);
                DAT_806121c0 = DAT_806121c0 + 1;
            }
        }
        off = off + 0x34;
    }
}

// ===========================================================================
// 0x801525c0  subobj_carried_weapon_aim
// ---------------------------------------------------------------------------
// Carried-weapon aim IK (frame-registered, entry FUN_803826e4 / exit
// FUN_80382730). For a carried body that is a weapon (attach-point + weapon def
// via weapon_get_def_by_index), drives the weapon's aim pitch/yaw (attach
// fields +0x1c/+0x24) toward the carrier's look direction. Free-look mode
// (param_3-derived stick input) turns at a gain; a linked target instead
// ray-tests toward the target position (scene_raycast), converts the resolved
// direction to angles (dir_to_angles), wraps them (FUN_80390324) and steps the
// aim toward them with per-frame rate. Also re-classifies stale weapon states.
// ===========================================================================
void subobj_carried_weapon_aim(unsigned int param_1, int param_2, float* param_3)
{
    (void)param_1; (void)param_2;
    unsigned long long fr = FUN_803826e4();
    int frameEnt = (int)((unsigned long long)fr >> 0x20);
    int idx = *(int*)((int)fr + 0x2c);
    int rec = *(int*)(frameEnt + 0x7c);

    if ((-1 < idx) && (idx < *(int*)(rec + 0x164))) {
        int* attach = *(int**)(rec + idx * 0x10 + 0x144);
        char* wdef = (char*)weapon_get_def_by_index(attach[3]);
        double zero = (double)FLOAT_80615fac;
        if ((zero == (double)*(float*)(wdef + 0xc)) ||
            (((double)(float)attach[0x65] == (double)*(float*)(wdef + 0xc)) || (attach[4] != 8))) {
            if ((attach[4] == 0xd) || (attach[4] == 0xe)) {
                attach[7] = (int)FLOAT_80615fac;
                attach[9] = (int)FLOAT_80615fac;
            } else {
                float linkH = *(float*)&attach[7];
                int link;
                if (linkH == 0.0f) {
                    link = 0;
                } else {
                    link = DAT_80611c24 + ((unsigned int)linkH >> 0x16) * 0x2e8;
                    if (*(float*)(link + 0x2c) != linkH) {
                        link = 0;
                        *(float*)&attach[7] = 0.0f;
                    }
                }
                double frameInv = (double)(FLOAT_806108ec *
                    (FLOAT_80615fa8 /
                     (float)((double)(unsigned int)(DAT_806109c0) - DOUBLE_80615fd8)));
                double lerp = CAM_LERP_FACTOR(DAT_80611d54);
                float rate = (float)((double)*(float*)(DAT_80611d54 + 900) / lerp);
                *(unsigned int*)(*attach + 0x18) = *(unsigned int*)(*attach + 0x18) | 2;
                float gainYaw = FLOAT_80616050;

                if (*(int*)((int)fr + 0x30) == 0) {
                    int gunEnt;
                    if ((*(int*)(link + 0x150) == 0) ||
                        (gunEnt = *(int*)(*(int*)(link + 0x150) + 8), gunEnt == 0)) {
                        attach[9] = (int)(FLOAT_80616050 * rate *
                            (float)(frameInv * (double)(-param_3[2] * (float)attach[0x17])) +
                            (float)attach[9]);
                        attach[7] = (int)FLOAT_80615fac;
                    } else if ((*(unsigned int*)(rec + 0x58) & 1) == 0) {
                        attach[7] = (int)FLOAT_80615fac;
                        attach[9] = (int)FLOAT_80615fac;
                    } else {
                        // Ray-test toward the linked target and aim along the hit.
                        float dir[3];
                        dir[0] = FLOAT_80616038 * *(float*)(gunEnt + 0x334);
                        dir[1] = FLOAT_80616038 * *(float*)(gunEnt + 0x338);
                        dir[2] = FLOAT_80616038 * *(float*)(gunEnt + 0x33c);
                        float hit[3];
                        hit[0] = (float)((double)*(float*)(gunEnt + 0x27c) + (double)dir[0]);
                        hit[1] = (float)((double)*(float*)(gunEnt + 0x280) + (double)dir[1]);
                        hit[2] = (float)((double)*(float*)(gunEnt + 0x284) + (double)dir[2]);
                        subobj_grab_collision_filter(frameEnt, 0);
                        SCENE_RAYCAST(*(int*)(gunEnt + 0x2a0), (int*)(gunEnt + 0x27c), dir, 0,
                                      hit, (float*)0, (int*)0, 0x2110070, 1, (int*)0);
                        subobj_grab_collision_filter(frameEnt, 1);
                        float muzzle[3];
                        FUN_8023fec0(*attach, muzzle);
                        if (FLOAT_80615fac <
                            (hit[2] - muzzle[2]) * *(float*)(gunEnt + 0x33c) +
                            (hit[0] - muzzle[0]) * *(float*)(gunEnt + 0x334) +
                            (hit[1] - muzzle[1]) * *(float*)(gunEnt + 0x338)) {
                            dir[0] = hit[0] - *(float*)(gunEnt + 0x27c);
                            dir[1] = hit[1] - *(float*)(gunEnt + 0x280);
                            dir[2] = hit[2] - *(float*)(gunEnt + 0x284);
                            float len = dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1];
                            if (len < FLOAT_8061603c) {
                                float iv = 1.0f / SQRT(len);
                                iv = iv * -(FLOAT_80615fe4 * len * iv * iv - FLOAT_80615fe8);
                                float sc = FLOAT_80616040 * iv *
                                    -(FLOAT_80615fe4 * len * iv * iv - FLOAT_80615fe8);
                                dir[0] = dir[0] * sc;
                                dir[1] = dir[1] * sc;
                                dir[2] = dir[2] * sc;
                            }
                        }
                        dir[0] = (dir[0] + *(float*)(gunEnt + 0x27c)) - muzzle[0];
                        dir[1] = (dir[1] + *(float*)(gunEnt + 0x280)) - muzzle[1];
                        dir[2] = (dir[2] + *(float*)(gunEnt + 0x284)) - muzzle[2];
                        float pitch, yaw;
                        dir_to_angles(dir, &pitch, &yaw);

                        float p = -pitch - *(float*)(*attach + 0xc0);
                        while (p <= FLOAT_80616044) p = p + FLOAT_80616028;
                        while (FLOAT_80616048 < p)  p = p - FLOAT_80616028;
                        attach[0x73] = (int)p;

                        float y = yaw - *(float*)(*attach + 0x70);
                        while (y <= FLOAT_80616044) y = y + FLOAT_80616028;
                        while (FLOAT_80616048 < y)  y = y - FLOAT_80616028;
                        float yawClamped = y;
                        float lo = (float)attach[0x10];
                        float hi = (float)attach[0xf];
                        if (FLOAT_8061604c <= ABS(lo - hi)) {
                            float c = y;
                            if (lo < y) c = lo;
                            yawClamped = hi;
                            if ((hi <= c) && (yawClamped = y, lo < y)) yawClamped = lo;
                        }

                        float cy = (float)attach[9];
                        float dHi = yawClamped - (cy + FLOAT_80616028);
                        float dMid = ABS(yawClamped - cy);
                        float aHi = ABS(dHi);
                        float dLo = yawClamped - (cy - FLOAT_80616028);
                        if (aHi <= dMid) {
                            if (aHi < ABS(dLo)) dLo = dHi;
                        } else if (dMid < ABS(dLo)) {
                            dLo = yawClamped - cy;
                        }
                        double stepped = (double)(dLo *
                            (float)(frameInv * (double)(float)attach[0x17]) + cy);
                        double wrapped = FUN_80390324(
                            (double)((float)(stepped - (double)FLOAT_80616044) / FLOAT_80616028));
                        attach[9] = (int)-(float)((double)FLOAT_80616028 *
                            (double)(float)wrapped - stepped);
                        attach[7] = (int)FLOAT_80615fac;
                    }
                } else {
                    attach[9] = (int)(FLOAT_80616050 * rate *
                        (float)(frameInv * (double)(-param_3[0] * (float)attach[0x17])) +
                        (float)attach[9]);
                    attach[7] = (int)(gainYaw * rate *
                        (float)(frameInv * (double)(param_3[1] * (float)attach[0x16])) +
                        (float)attach[7]);
                }

                // Clamp pitch (+7) and yaw (+9) into their configured limits.
                float lo = (float)attach[0xd];
                float hi = (float)attach[0xe];
                if (lo != hi) {
                    float v = (float)attach[7];
                    float c = v;
                    if (hi < v) c = hi;
                    if ((lo <= c) && (lo = v, hi < v)) lo = hi;
                    attach[7] = (int)lo;
                }
                lo = (float)attach[0xf];
                hi = (float)attach[0x10];
                if (lo != hi) {
                    float v = (float)attach[9];
                    float c = v;
                    if (hi < v) c = hi;
                    if ((lo <= c) && (lo = v, hi < v)) lo = hi;
                    attach[9] = (int)lo;
                }

                if (((unsigned int)param_3[6] & 0x80) != 0) {
                    int mode = (*(int*)(link + 0x150) != 0) ? 4 : 3;
                    FUN_8023ddfc(*attach, attach[0x1b], 0, 0, mode, *(int**)(link + 0x7c));
                }
            }
        } else {
            // Weapon def mismatch: demote stale grabbed weapons (state 8 -> 0xd).
            int off = 0;
            for (int i = 0; i < *(int*)(rec + 0x164); i = i + 1) {
                int ap = *(int*)(rec + off + 0x144);
                char* def = (char*)weapon_get_def_by_index(*(int*)(ap + 0xc));
                if (((zero != (double)*(float*)(def + 0xc)) &&
                     ((double)*(float*)(ap + 0x194) != (double)*(float*)(def + 0xc))) &&
                    (*(int*)(ap + 0x10) == 8)) {
                    *(int*)(ap + 0x10) = 0xd;
                }
                off = off + 0x10;
            }
        }
    }
    FUN_80382730();
}

// ===========================================================================
// batch_426 : carry pose / attach / rotor cluster (0x80152cb0 - 0x80154f48)
// ---------------------------------------------------------------------------
// The follow-on to the grab/carry engine (batch_424/425). These are the
// per-frame drivers and pose helpers that run once a body is grabbed and
// carried: the top-level carry tick, the attach/release dispatcher, the
// rotor/engine channel driver, the flying-aim variant, and the bone-pose
// commit routines that write the carried object's world matrices, attach
// points and rotor/blade sub-matrices back onto the entity.
//
// Register-pair frame helpers (FUN_803826xx / FUN_803826yy) push a per-call
// scratch frame; the decompiler returns them as a 64-bit pair where the low
// word is a float*/int scratch base and the high word is the acting entity.
// The decompiler also spills callee-saved FP registers (in_fNN / in_psNN_1)
// to shadow stack slots on entry; those are pure ABI save/restore artifacts
// and are dropped here.
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x80152cb0  subobj_carry_object_tick
// ---------------------------------------------------------------------------
// Top-level per-frame carried-object drive (callers: FUN_8015a4f4,
// player_aim_input_handler). Resolves the carried body via the grab-slot link
// [0x302], then per grab-slot runs the carry state machine keyed on the slot
// grab-state (+0xc index 0xb / carry mode +0xc index 0xc):
//   mode 0  -> free carry: aim the carried weapon, emit pick-up sound, drive
//              the rotor channels (subobj_carry_rotor_drive) or the flying-aim
//              variant, and apply stick torque when not idle;
//   mode 1/2-> transfer/throw setup: hand the grab off to a free slot, spawn
//              the appropriate pick-up/put-down anim, show control hints;
//   mode 3/4-> pure aim.
// Slot rec fields: +0x70+i*0x34 grab record; word[2]/[3] anim clip handles,
// word[5] transition clip, word[0xb] attach-index, word[0xc] carry mode.
// ---------------------------------------------------------------------------
void subobj_carry_object_tick(void)
{
    unsigned long long fr = FUN_803826d4();
    int   entity = (int)(fr >> 0x20);
    float* rd    = (float*)(unsigned int)fr;   // grabber render-data slot

    bool suppress = false;
    if (DAT_806108e4 != 0) {
        float bodyH = rd[7];
        int body;
        if (bodyH == 0.0f) {
            body = 0;
        } else {
            body = DAT_80611c24 + ((unsigned int)bodyH >> 0x16) * 0x2e8;
            if (*(float*)(body + 0x2c) != bodyH) { body = 0; rd[7] = 0.0f; }
        }
        if (body != 0) {
            *(unsigned int*)(entity + 0x18) |= 0x40;
            subobj_get_object_matrix(*(unsigned int*)(entity + 0x80));
            int carriedRec = *(int*)(body + 0x7c);
            int grabRec    = *(int*)(entity + 0x7c);
            if ((*(unsigned int*)(carriedRec + 0x24) & 8) == 0) {
                // find the grab slot on this grabber matching the carried body
                int   n    = *(int*)(grabRec + 0x140);
                int   off  = 0;
                unsigned int* slot = 0;
                if (0 < n) {
                    do {
                        if (*(float*)(grabRec + off + 0x70) == rd[7]) {
                            slot = (unsigned int*)(grabRec + off + 0x70);
                            break;
                        }
                        off = off + 0x34;
                        n = n - 1;
                    } while (n != 0);
                }
                if (slot == 0) {
                    subobj_grab_attach_release(entity, (void*)body, 0, 0, 0);
                } else {
                    if ((((unsigned int)rd[6] & 0x40) != 0) &&
                        ((*(unsigned int*)(grabRec + 0x58) & 0x10) == 0)) {
                        suppress = true;
                    }
                    bool clip2 = false;
                    if ((slot[2] != 0) && (anim_is_clip_active(body, slot[2]) != 0)) clip2 = true;
                    bool clip3 = false;
                    if ((slot[3] != 0) && (anim_is_clip_active(body, slot[3]) != 0)) clip3 = true;

                    if (clip2 || clip3) {
                        // an attach/detach anim clip is playing: ease the two
                        // rotor angle accumulators toward the target rev value
                        if ((-1 < (int)slot[0xb]) && !suppress) {
                            int rotor = *(int*)(grabRec + slot[0xb] * 0x10 + 0x144);
                            if (rotor != 0) {
                                // rotor angle 1 (+0x24)
                                float a  = *(float*)(rotor + 0x24);
                                float d1 = FLOAT_80615fac - (a + FLOAT_80616028);
                                float d0 = ABS(FLOAT_80615fac - a);
                                float d2 = FLOAT_80615fac - (a - FLOAT_80616028);
                                float da = ABS(d1);
                                if (da <= d0) { if (da < ABS(d2)) d2 = d1; }
                                else if (d0 < ABS(d2)) d2 = FLOAT_80615fac - a;
                                float dt = (FLOAT_80615fa8 /
                                    (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) +
                                            (double)0x100000000 * 0 - DOUBLE_80615fd8)) *
                                    FLOAT_806108ec * *(float*)(rotor + 0x5c) *
                                    (float)(((unsigned int)d2 & 0x80000000) | 0x3f800000);
                                if (ABS(FLOAT_80615fec * dt) < ABS(*(float*)(rotor + 0x24)))
                                    *(float*)(rotor + 0x24) = *(float*)(rotor + 0x24) + dt;
                                else
                                    *(float*)(rotor + 0x24) = FLOAT_80615fac;
                                // rotor angle 0 (+0x1c)
                                a  = *(float*)(rotor + 0x1c);
                                d1 = FLOAT_80615fac - (a + FLOAT_80616028);
                                d0 = ABS(FLOAT_80615fac - a);
                                d2 = FLOAT_80615fac - (a - FLOAT_80616028);
                                da = ABS(d1);
                                if (da <= d0) { if (da < ABS(d2)) d2 = d1; }
                                else if (d0 < ABS(d2)) d2 = FLOAT_80615fac - a;
                                dt = (FLOAT_80615fa8 /
                                    (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) +
                                            (double)0x100000000 * 0 - DOUBLE_80615fd8)) *
                                    FLOAT_806108ec * *(float*)(rotor + 0x58) *
                                    (float)(((unsigned int)d2 & 0x80000000) | 0x3f800000);
                                if (ABS(FLOAT_80615fec * dt) < ABS(*(float*)(rotor + 0x1c)))
                                    *(float*)(rotor + 0x1c) = *(float*)(rotor + 0x1c) + dt;
                                else
                                    *(float*)(rotor + 0x1c) = FLOAT_80615fac;
                            }
                        }
                    } else {
                        unsigned int mode = slot[0xc];
                        if ((int)mode < 3) {
                            if (mode == 0) {
                                if (!suppress &&
                                    ((slot[5] == 0) ||
                                     (anim_is_clip_active(body, slot[5]) == 0))) {
                                    subobj_carried_weapon_aim(entity, (int)slot, rd);
                                    if (((unsigned int)rd[6] & 1)   != 0) rd[5] = FLOAT_80615fa8;
                                    if (((unsigned int)rd[6] & 2)   != 0) rd[4] = FLOAT_80615fa8;
                                    if (((unsigned int)rd[6] & 0x100) != 0) {
                                        int cam = *(int*)(*(int*)(grabRec + 0x50) + 0xc0);
                                        int req[16];
                                        sound_init_play_params((int)req);
                                        *(char*)((char*)req + 0x24) = 1;      // flag
                                        req[0]  = cam;
                                        // position from grabber entity
                                        req[9]  = *(int*)(entity + 100);
                                        req[10] = *(int*)(entity + 0x68);
                                        req[11] = *(int*)(entity + 0x6c);
                                        sound_play_request(req);
                                    }
                                    void (**cb)(int, float*) =
                                        (void (**)(int, float*))(grabRec + 100);
                                    if (*cb != 0) (*cb)(entity, rd);
                                    if ((*(unsigned int*)(grabRec + 0x58) & 1) == 0)
                                        subobj_carry_rotor_drive(entity, rd);
                                    else
                                        subobj_carry_flying_aim();
                                    if ((subobj_is_idle(*(unsigned int*)(entity + 0x80)) == 0) &&
                                        ((*(unsigned int*)(grabRec + 0x58) & 0x80000) != 0))
                                        subobj_carry_apply_stick_torque(entity, rd);
                                }
                            } else if ((-1 < (int)mode) && !suppress) {
                                subobj_carried_weapon_aim(entity, (int)slot, rd);
                                if ((*(int*)(body + 0x150) != 0) ||
                                    (((unsigned int)rd[6] & 0x200) != 0)) {
                                    // transfer the grab to a free slot
                                    int n2 = *(int*)(grabRec + 0x140);
                                    unsigned int* freeSlot = 0;
                                    char idx = 0, freeIdx = 0;
                                    int off2 = 0;
                                    if (0 < n2) {
                                        do {
                                            freeIdx = idx;
                                            if (*(int*)(grabRec + off2 + 0xa0) == 0) {
                                                freeSlot = (unsigned int*)(grabRec + off2 + 0x70);
                                                break;
                                            }
                                            freeIdx = freeIdx + 1;
                                            off2 = off2 + 0x34;
                                            n2 = n2 - 1;
                                            idx = freeIdx;
                                        } while (n2 != 0);
                                    }
                                    if ((freeSlot != 0) && (freeSlot[5] != 0) &&
                                        (anim_is_clip_active(body, freeSlot[5]) == 0)) {
                                        unsigned int h = *freeSlot;
                                        int hb;
                                        if (h == 0) { hb = 0; }
                                        else {
                                            hb = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                                            if (*(unsigned int*)(hb + 0x2c) != h) {
                                                hb = 0; *freeSlot = 0;
                                            }
                                        }
                                        if (hb == 0) {
                                            if (*(int*)(body + 0x150) != 0) {
                                                if ((*(unsigned int*)(grabRec + 0x58) & 1) == 0)
                                                    hud_hint_show_vehicle_controls();
                                                else
                                                    hud_hint_show_vehicle_fire();
                                            }
                                            *freeSlot = *slot;
                                            *slot = 0;
                                            *(char*)(carriedRec + 0xc0c) = freeIdx;
                                            anim_play_scaled((double)FLOAT_80615fac,
                                                             (double)FLOAT_80615fa8);
                                            model_render_with_override_b();
                                            subobj_carry_pose_commit();
                                        } else if ((*(int*)(body + 0x150) != 0) &&
                                                   (*(int*)(hb + 0x150) == 0) &&
                                                   (anim_is_clip_active(hb, freeSlot[3]) == 0)) {
                                            subobj_grab_attach_release(entity, (void*)hb, 0,
                                                                       freeSlot[3], 1);
                                        }
                                    }
                                }
                            }
                        } else if (((int)mode < 5) && !suppress) {
                            subobj_carried_weapon_aim(entity, (int)slot, rd);
                        }
                    }
                    if (suppress || clip3)
                        subobj_grab_attach_release(entity, (void*)body, 0, slot[3], 1);
                }
            }
        }
    }
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// 0x80153344  subobj_grab_attach_release  (12 xrefs)
// ---------------------------------------------------------------------------
// Attach or release a grabbed body to/from a grab slot. Resolves the carried
// object via the render slot [0x1f] and the carry-mode word [0x54]. When the
// grabbed body is valid (type 8) it computes the world attach position from
// the grab-attach vertex ref plus the local anchor offset, tests the reach
// (subobj_carry_reach_test / FUN_80158e30); on failure it plays the attach
// anim and optionally re-parents the third-person camera; on success it
// teleports the carried actor to a free zone and starts the carry. When the
// held object is a scripted type (0x1000) it destroys it with an event.
// ---------------------------------------------------------------------------
int subobj_grab_attach_release(int param_1, void* param_2, int param_3, int param_4, int param_5)
{
    (void)param_1; (void)param_2;
    unsigned long long fr = FUN_803826d4();
    int entity = (int)(fr >> 0x20);
    unsigned int* rd = (unsigned int*)(unsigned int)fr;

    int  grabRec = *(int*)(entity + 0x7c);
    int* held    = (int*)rd[0x1f];
    int  carryMode = rd[0x54];
    float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(entity + 0x80));
    *(unsigned int*)(entity + 0x18) |= 0x40;

    if ((rd[8] == 8) && (held != 0)) {
        int slotRec = subobj_grab_slot_lookup(held);
        if (slotRec == 0) {
            held[0x302] = 0;
            *(unsigned char*)(held + 0x303) = 0xff;
            held[9] = held[9] & 0xfffffeff;
        } else if (((carryMode == 0) ||
                    ((*(unsigned int*)(carryMode + 0x114) & 0x10) == 0)) &&
                   ((held == 0) || ((held[9] & 8U) == 0))) {
            // world attach position = objMtx * (grab vertex + local anchor)
            int* mesh = *(int**)(*(int*)(entity + 4) + 4);
            float* vtx = (float*)(mesh[*mesh * -0x30 +
                                  ((int)*(unsigned int*)(slotRec + 4) >> 0x10) * 0x30 + 0x1b] +
                                  (*(unsigned int*)(slotRec + 4) & 0xffff) * 0x18);
            float pos[3];
            pos[0] = vtx[0]; pos[1] = vtx[1]; pos[2] = vtx[2];
            mtx4_transform_vector_alias(objMtx, pos);
            double vx = (double)pos[0];
            double vz = (double)pos[2];
            pos[0] = *(float*)(slotRec + 0x20);
            pos[1] = *(float*)(slotRec + 0x24);
            pos[2] = *(float*)(slotRec + 0x28);
            mtx4_transform_vector_alias(objMtx, pos);
            pos[0] = (float)((double)pos[0] + vx) + *(float*)(entity + 100);
            pos[2] = (float)((double)pos[2] + vz) + *(float*)(entity + 0x6c);
            pos[1] = FLOAT_80615fe4 *
                     *(float*)(&DAT_8048ca94 + *(short*)(held + 0x12) * 0x30) +
                     *(float*)(grabRec + 0x558);

            int reached = subobj_grab_reach_test(entity, slotRec, pos);
            if ((reached == 0) && (param_4 != 0)) {
                if (anim_is_clip_active((int)rd, param_4) == 0) {
                    void (**cb)(int, int*) = (void (**)(int, int*))(grabRec + 0x500);
                    if (*cb != 0) (*cb)(*(int*)(grabRec + 0x4c), held);
                    anim_play_scaled((double)FLOAT_80615fac, (double)FLOAT_80615fa8);
                    model_render_with_override_b();
                    subobj_carry_pose_commit();
                    int noSplit = DAT_806117a8;
                    if ((param_5 != 0) && (rd[0x54] != 0)) {
                        int cam = *(int*)(rd[0x54] + 8);
                        if ((cam != 0) &&
                            (*(int*)(slotRec + 0x30) != 4) &&
                            (*(int*)(slotRec + 0x30) != 3)) {
                            double z = (double)FLOAT_80615fac;
                            camera_set_orbit_angles(z, z, z, z, cam,
                                *(int*)(*(int*)(grabRec + 0x50) + 0xac));
                            if (noSplit == 0)
                                camera_set_mode_by_index(*(int*)(rd[0x54] + 8), 6, (int)rd);
                        }
                    }
                }
            } else {
                int zone = subobj_grab_pick_release_zone();
                if (-1 < zone) player_ai_teleport_wrapper((int)held, zone, 0);
                if ((param_5 != 0) && (rd[0x54] != 0)) {
                    int cam = *(int*)(rd[0x54] + 8);
                    if ((cam != 0) &&
                        (*(int*)(slotRec + 0x30) != 4) &&
                        (*(int*)(slotRec + 0x30) != 3)) {
                        camera_set_mode_by_index(cam, 0, (int)rd);
                    }
                }
                subobj_grab_release_commit(entity, (int)held, param_3, param_5);
            }
        }
    } else if (rd[8] == 0x1000) {
        entity_destroy_with_event((int*)rd);
    }
    FUN_80382720();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x801536b8  subobj_carry_apply_stick_torque
// ---------------------------------------------------------------------------
// Convert the carry aim-stick input (aim[0]=yaw, aim[1]=pitch) into an angular
// force on the carried body, scaled by an inverse-mass approximation (a fast
// bit-hack estimate of 1/mass^(1/2) derived from subobj_get_inverse_mass). The
// yaw component spins about the fixed world axis (DAT_803ccb08/0c/10); the
// pitch component spins about the object matrix forward axis.
// ---------------------------------------------------------------------------
void subobj_carry_apply_stick_torque(int entity, float* aim)
{
    unsigned int handle = *(unsigned int*)(entity + 0x80);
    double invM = subobj_get_inverse_mass(handle);
    float torque[3] = { FLOAT_80615fac, FLOAT_80615fac, FLOAT_80615fac };
    // fast approx: reinterpret 1/mass and halve the exponent (rsqrt-ish seed)
    double s = (double)(float)(((int)(float)invM >> 1) + 0x1fc00000);

    if (FLOAT_80616054 < ABS(aim[0])) {
        float k = aim[0] * (float)((double)FLOAT_80616058 * s);
        torque[0] = DAT_803ccb08 * k + FLOAT_80615fac;
        torque[1] = DAT_803ccb0c * k + FLOAT_80615fac;
        torque[2] = DAT_803ccb10 * k + FLOAT_80615fac;
    }
    if (FLOAT_80616054 < ABS(aim[1])) {
        float* objMtx = (float*)subobj_get_object_matrix(handle);
        float k = aim[1] * (float)((double)FLOAT_80615fcc * s);
        torque[0] = objMtx[0] * k + torque[0];
        torque[1] = objMtx[1] * k + torque[1];
        torque[2] = objMtx[2] * k + torque[2];
    }
    subobj_add_angular_force(handle, torque);
}

// ---------------------------------------------------------------------------
// 0x801537f8  subobj_carry_rotor_drive
// ---------------------------------------------------------------------------
// Drive the carried object's two rotor/engine channels from the aim-stick.
// aim[4]/aim[5] are the two throttle inputs; when either exceeds the gate the
// per-channel drive/secondary callbacks (+0x504/+0x508/+0x50c) are invoked and
// the two rev accumulators (+0x530 drive, +0x534 secondary) are set. A smoothed
// spin accumulator (+0x538) then drives every rotor sub-object angle (+0x1c)
// and rev target (+0x5c) that opts in (flag bits at sub +8).
// ---------------------------------------------------------------------------
void subobj_carry_rotor_drive(int entity, float* aim)
{
    int rec = *(int*)(entity + 0x7c);
    if ((FLOAT_80616014 < aim[5]) || (FLOAT_80616014 < aim[4])) {
        int mode = *(int*)(rec + 0x60);
        if (mode == 2) {
            if (FLOAT_80616014 < aim[4]) {
                subobj_dirty_list_add(*(unsigned int*)(rec + 0x48));
                void (**cb)(int, float*) = (void (**)(int, float*))(rec + 0x50c);
                if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), aim + 4);
                int idx = 1;
                if (1 < *(int*)(rec + 0x5c)) idx = *(int*)(rec + 0x5c);
                *(float*)(rec + 0x530) =
                    *(float*)(*(int*)(rec + 0x50) + idx * 4 + 0x50) *
                    aim[4] * *(float*)(*(int*)(rec + 0x50) + 4);
            }
            if (FLOAT_80616014 < aim[5]) {
                *(float*)(rec + 0x534) = aim[5] * *(float*)(*(int*)(rec + 0x50) + 8);
                void (**cb)(int, int) = (void (**)(int, int))(rec + 0x508);
                if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), 0);
            }
        } else if ((mode < 2) && (0 < mode)) {
            if (FLOAT_80616014 < aim[5]) {
                subobj_dirty_list_add(*(unsigned int*)(rec + 0x48));
                void (**cb)(int, float*) = (void (**)(int, float*))(rec + 0x504);
                if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), aim + 5);
                int idx = 1;
                if (1 < *(int*)(rec + 0x5c)) idx = *(int*)(rec + 0x5c);
                *(float*)(rec + 0x530) =
                    *(float*)(*(int*)(rec + 0x50) + idx * 4 + 0x50) *
                    aim[5] * *(float*)(*(int*)(rec + 0x50) + 4);
            }
            if (FLOAT_80616014 < aim[4]) {
                *(float*)(rec + 0x534) = aim[4] * *(float*)(*(int*)(rec + 0x50) + 8);
                void (**cb)(int, int) = (void (**)(int, int))(rec + 0x508);
                if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), 0);
            }
        }
    }
    float ratio = *(float*)(rec + 0x52c) / *(float*)(*(int*)(rec + 0x50) + 0x34);
    float clamped = FLOAT_8061605c;
    if (ratio < FLOAT_8061605c) clamped = ratio;
    *(float*)(rec + 0x538) =
        FLOAT_80616060 * FLOAT_806108ec *
        (-aim[0] * (FLOAT_80615fa8 - clamped) - *(float*)(rec + 0x538)) +
        *(float*)(rec + 0x538);
    int off = 0;
    for (int i = 0; i < *(int*)(*(int*)(rec + 0x50) + 0x9c); i = i + 1) {
        int sub = rec + off + 0x174;
        if ((*(unsigned int*)(sub + 8) & 2) != 0)
            *(float*)(sub + 0x1c) =
                *(float*)(rec + 0x538) * *(float*)(*(int*)(rec + 0x50) + 0x10);
        if ((*(unsigned int*)(sub + 8) & 1) != 0)
            *(int*)(sub + 0x5c) = *(int*)(rec + 0x530);
        off = off + 100;
    }
}

// ---------------------------------------------------------------------------
// 0x80153a64  subobj_carry_flying_aim
// ---------------------------------------------------------------------------
// The flying/vehicle carry aim variant (used when carry flag +0x58 & 1). Reads
// the carrier's aim-stick, wraps it against the object's yaw, computes a pitch
// via atan2/asin against the object forward axis, then splits the resulting
// pitch magnitude into the two rotor channels (angle A / angle B) so the craft
// banks to steer. Finishes by driving every rotor sub-object rev target the
// same way subobj_carry_rotor_drive does, and firing the drive callback +0x504.
// ---------------------------------------------------------------------------
void subobj_carry_flying_aim(void)
{
    unsigned long long fr = __save_gpr_28();
    int    entity = (int)(fr >> 0x20);
    float* rd     = (float*)(unsigned int)fr;

    float bodyH = rd[7];
    int body;
    if (bodyH == 0.0f) { body = 0; }
    else {
        body = DAT_80611c24 + ((unsigned int)bodyH >> 0x16) * 0x2e8;
        if (*(float*)(body + 0x2c) != bodyH) { body = 0; rd[7] = 0.0f; }
    }
    int rec = *(int*)(entity + 0x7c);
    if (body != 0) {
        int carried = *(int*)(body + 0x150);
        if (carried != 0) {
            float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(entity + 0x80));
            double fwdX = (double)objMtx[8];
            double fwdZ = (double)objMtx[10];
            float stk[4];   // stk[0]=x, stk[1]=up, stk[2]=z ...
            float sx, sz;
            if (*(int*)(carried + 8) == 0) {
                sx = *(float*)(carried + 0xc0);
                sz = *(float*)(carried + 200);
            } else {
                *(int*)(*(int*)(carried + 8) + 0xfdc) = *(int*)(carried + 0x104);
                sx = *(float*)(*(int*)(carried + 8) + 0x334);
                sz = *(float*)(*(int*)(carried + 8) + 0x33c);
            }
            float sy = FLOAT_80615fac;
            float mag = sz * sz + sx * sx + FLOAT_80615fac * FLOAT_80615fac;
            if (FLOAT_80615ff4 < mag) {
                float r = 1.0f / SQRT(mag);
                r = r * -(FLOAT_80615fe4 * mag * r * r - FLOAT_80615fe8);
                r = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * mag * r * r - FLOAT_80615fe8);
                sx = sx * r; sy = FLOAT_80615fac * r; sz = sz * r;
            }
            float fmag = (float)(fwdZ * fwdZ + (double)(float)(fwdX * fwdX + (double)FLOAT_80615fac));
            if (FLOAT_80615ff4 < fmag) {
                float r = 1.0f / SQRT(fmag);
                r = r * -(FLOAT_80615fe4 * fmag * r * r - FLOAT_80615fe8);
                double rr = (double)(FLOAT_80615fa8 * r *
                              -(FLOAT_80615fe4 * fmag * r * r - FLOAT_80615fe8));
                fwdX = (double)(float)(fwdX * rr);
                fwdZ = (double)(float)(fwdZ * rr);
            }
            double yaw = (double)(float)math_atan2(-(double)rd[0], (double)rd[1]);
            double wrapY = FUN_80390324((double)((float)(yaw - (double)FLOAT_80616064) /
                                                 FLOAT_80616068));
            mtx4_set_rotation_y(
                -(double)(float)((double)FLOAT_80616068 * (double)(float)wrapY - yaw), stk);
            mtx4_transform_vector_alias(stk, stk);   // rotate stick into aim frame

            double dz = (double)sz;
            double dx = (double)sx;
            double dot = (double)(float)(dx * fwdX + (double)(float)(dz * fwdZ));
            double clampd = dot;
            if ((double)FLOAT_80615fa8 < dot) clampd = (double)FLOAT_80615fa8;
            if ((double)FLOAT_80616024 <= clampd) {
                if ((double)FLOAT_80615fa8 < dot) dot = (double)FLOAT_80615fa8;
            } else {
                dot = (double)FLOAT_80616024;
            }
            float m2 = objMtx[2];
            float m0 = objMtx[0];
            double pitch = (double)math_asin(dot);
            double signed_pitch = (double)(float)(pitch * (double)(float)(
                ((unsigned int)(float)(dx * (double)m0 + (double)(float)(dz * (double)m2)) &
                 0x80000000) | 0x3f800000));
            double wrapP = FUN_80390324((double)((float)(signed_pitch - (double)FLOAT_80616064) /
                                                 FLOAT_80616068));
            double wrappedP = -(double)(float)((double)FLOAT_80616068 * (double)(float)wrapP -
                                               signed_pitch);
            double absP = ABS(wrappedP);
            int chA, chB;
            if ((double)FLOAT_80615fac <= wrappedP) { chA = 1; chB = 3; }
            else                                    { chB = 1; chA = 3; }

            double planar = math_sqrt((double)(rd[1] * rd[1] + rd[0] * rd[0]));
            float zeroF = FLOAT_80615fac;
            double clampPl = planar;
            if ((double)FLOAT_80615fa8 < planar) clampPl = (double)FLOAT_80615fa8;
            if ((double)FLOAT_80615fac <= clampPl) {
                if ((double)FLOAT_80615fa8 < planar) planar = (double)FLOAT_80615fa8;
            } else {
                planar = (double)FLOAT_80615fac;
            }
            if ((double)FLOAT_8061606c <= absP) {
                if ((double)FLOAT_80615fb0 <= absP) {
                    double c = (double)FLOAT_8061600c;
                    rd[chA] = (float)(c * planar);
                    rd[chB] = (float)(c * -planar);
                } else {
                    rd[chA] = (float)planar;
                    rd[chB] = zeroF;
                }
            } else {
                rd[chA] = (float)planar;
                rd[chB] = (float)planar;
            }
        }
        // drive rotor rev targets from the two channel magnitudes
        float gate  = FLOAT_8061604c;
        float zeroF = FLOAT_80615fac;
        float chan[4];
        chan[2] = ABS(rd[1]);
        chan[3] = ABS(rd[3]);
        chan[0] = rd[1];
        chan[1] = rd[3];
        int off = 0;
        *(float*)(rec + 0x530) = FLOAT_80615fac;
        int cfg;
        for (int i = 0; (cfg = *(int*)(rec + 0x50)), i < *(int*)(cfg + 0x9c); i = i + 1) {
            int sub = rec + off + 0x174;
            unsigned int which = (unsigned int)(*(float*)(sub + 0x28) <= zeroF);
            if (chan[which + 2] <= gate) {
                if (gate < chan[3 - which]) {
                    float rv = FLOAT_80615fec * *(float*)(cfg + 8);
                    if (rv < *(float*)(sub + 0x60)) rv = *(float*)(sub + 0x60);
                    *(float*)(sub + 0x60) = rv;
                }
            } else {
                int idx = 1;
                if (1 < *(int*)(rec + 0x5c)) idx = *(int*)(rec + 0x5c);
                *(float*)(sub + 0x5c) =
                    *(float*)(cfg + idx * 4 + 0x50) * *(float*)(cfg + 4) * chan[which];
            }
            float rv = ABS(*(float*)(sub + 0x5c));
            if (rv < *(float*)(rec + 0x530)) rv = *(float*)(rec + 0x530);
            *(float*)(rec + 0x530) = rv;
            off = off + 100;
        }
        if ((FLOAT_8061604c < chan[2]) || (FLOAT_8061604c < chan[3])) {
            subobj_dirty_list_add(*(unsigned int*)(rec + 0x48));
            float best = chan[3];
            if (chan[3] < chan[2]) best = chan[2];
            void (**cb)(int, float*) = (void (**)(int, float*))(rec + 0x504);
            if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), &best);
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80153f80  subobj_carry_attach_point  (3 xrefs)
// ---------------------------------------------------------------------------
// Compute a carried object's world attach point into out[0..2]. Resolves the
// attach vertex (rec+0x4f8 packed vertex ref) plus a rotor-height bias
// (sum of the 4 rotor accumulators +0x498/+0x4b0/+0x4c8/+0x4e0), transforms it
// through the object matrix, then adds the local anchor delta transformed by
// the entity local matrix (rec+8).  'vref' is a packed vertex reference.
// ---------------------------------------------------------------------------
void subobj_carry_attach_point(int entity, unsigned int vref, float* out)
{
    (void)entity;
    unsigned long long fr = __save_gpr_28();
    int self = (int)(fr >> 0x20);
    float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    int rec = *(int*)(self + 0x7c);
    int* mesh = *(int**)(*(int*)(self + 4) + 4);
    float* base = (float*)(mesh[*mesh * -0x30 +
                           ((int)*(unsigned int*)(rec + 0x4f8) >> 0x10) * 0x30 + 0x1b] +
                           (*(unsigned int*)(rec + 0x4f8) & 0xffff) * 0x18);
    float* tip = (float*)(mesh[*mesh * -0x30 + ((int)vref >> 0x10) * 0x30 + 0x1b] +
                          (vref & 0xffff) * 0x18);
    float p[3];
    p[0] = base[0];
    p[2] = base[2];
    p[1] = FLOAT_80616008 *
           (*(float*)(rec + 0x4e0) + *(float*)(rec + 0x4c8) +
            *(float*)(rec + 0x498) + *(float*)(rec + 0x4b0)) + base[1];
    mtx4_transform_point_alias(objMtx, p);
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    p[0] = tip[0] - base[0];
    p[1] = tip[1] - base[1];
    p[2] = tip[2] - base[2];
    mtx4_transform_point_alias((float*)(rec + 8), p);
    out[0] = out[0] + p[0];
    out[1] = out[1] + p[1];
    out[2] = out[2] + p[2];
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x801540e4  subobj_carry_anim_event_scan
// ---------------------------------------------------------------------------
// Thin wrapper over anim_event_scan_active (poll for a fired carry anim event).
// ---------------------------------------------------------------------------
int subobj_carry_anim_event_scan(void)
{
    return ANIM_EVENT_SCAN();
}

// ---------------------------------------------------------------------------
// 0x8015410c  subobj_carry_weapon_pose
// ---------------------------------------------------------------------------
// Compute the carried weapon's world aim pose and its look-at basis. For a
// dropped-weapon slot (state 4) it defers to weapon_compute_fire_origin. For a
// held weapon it builds a rotation from the per-slot euler triple (words 7/9/b),
// transforms the weapon-def muzzle offset, and writes the aim origin +0xb4 and
// aim point +0xc0 onto the weapon's linked entity. If the linked entity has a
// look-at matrix (+0x334..+0x36c) it rebuilds an orthonormal aim basis with two
// Gram-Schmidt/normalize passes.
// ---------------------------------------------------------------------------
void subobj_carry_weapon_pose(void)
{
    unsigned long long fr = FUN_803826e8();
    int   frameEnt = (int)(fr >> 0x20);
    int   self     = (int)(unsigned int)fr;
    int*  rec      = *(int**)((int)self + 0x7c);
    int   linkEnt  = *(int*)((int)self + 0x150);
    unsigned int* attach = (unsigned int*)subobj_grab_attach_point((int*)rec);
    int   slotRec = subobj_grab_slot_lookup((int*)rec);
    unsigned int* slot = (unsigned int*)*attach;
    int   frameSelf = *(int*)((int)frameEnt + 0x7c);
    char* def = (char*)weapon_get_def_by_index(slot[3]);

    if (*(int*)(slotRec + 0x30) == 4) {
        FUN_8023ff0c(*(int*)*attach, def + 0x40, (float*)(linkEnt + 0xb4));
        weapon_compute_fire_origin(*(int*)*attach, 0, 0, (float*)0, (float*)(linkEnt + 0xc0));
    } else {
        float muzzle[3];
        float aimPt[3];
        float mtx[16];
        muzzle[0] = *(float*)(def + 0x40);
        muzzle[1] = *(float*)(def + 0x44);
        muzzle[2] = *(float*)(def + 0x48);
        mtx4_set_rotation_zyx(
            (double)((FLOAT_80616070 * (float)slot[7]) / FLOAT_80616048),
            (double)((FLOAT_80616070 * (float)slot[9]) / FLOAT_80616048),
            (double)((FLOAT_80616070 * (float)slot[0xb]) / FLOAT_80616048), mtx);
        mtx4_transform_vector_alias(mtx, muzzle);
        aimPt[0] = mtx[12]; aimPt[1] = mtx[13]; aimPt[2] = mtx[14];
        entity_build_local_matrix(mtx, (void*)*(int*)slot, 0, 0);
        mtx4_transform_vector_alias(mtx, muzzle);
        mtx4_transform_vector(mtx, aimPt);
        float base[3];
        FUN_802405e8(*(int*)slot, base);
        *(float*)(linkEnt + 0xb4) = muzzle[0] + base[0];
        *(float*)(linkEnt + 0xb8) = muzzle[1] + base[1];
        *(float*)(linkEnt + 0xbc) = muzzle[2] + base[2];
        *(float*)(linkEnt + 0xc0) = aimPt[0];
        *(float*)(linkEnt + 0xc4) = aimPt[1];
        *(float*)(linkEnt + 200)  = aimPt[2];
    }

    int le = *(int*)(linkEnt + 8);
    if ((le != 0) && (*(int**)(le + 0x26c) != 0) && (**(int**)(le + 0x26c) == 0)) {
        // build orthonormal aim basis into +0x334 (fwd) / +0x364 (up) / +0x370 (right)
        *(int*)(le + 0x334) = *(int*)(linkEnt + 0xc0);
        *(int*)(*(int*)(linkEnt + 8) + 0x338) = *(int*)(linkEnt + 0xc4);
        *(int*)(*(int*)(linkEnt + 8) + 0x33c) = *(int*)(linkEnt + 200);
        *(int*)(*(int*)(linkEnt + 8) + 0x364) = *(int*)(frameSelf + 0x18);
        *(int*)(*(int*)(linkEnt + 8) + 0x368) = *(int*)(frameSelf + 0x1c);
        *(int*)(*(int*)(linkEnt + 8) + 0x36c) = *(int*)(frameSelf + 0x20);
        int b = *(int*)(linkEnt + 8);
        *(float*)(b + 0x370) = *(float*)(b + 0x368) * *(float*)(b + 0x33c) -
                               *(float*)(b + 0x36c) * *(float*)(b + 0x338);
        b = *(int*)(linkEnt + 8);
        *(float*)(b + 0x374) = *(float*)(b + 0x36c) * *(float*)(b + 0x334) -
                               *(float*)(b + 0x364) * *(float*)(b + 0x33c);
        b = *(int*)(linkEnt + 8);
        *(float*)(b + 0x378) = *(float*)(b + 0x364) * *(float*)(b + 0x338) -
                               *(float*)(b + 0x368) * *(float*)(b + 0x334);
        b = *(int*)(linkEnt + 8);
        float x = *(float*)(b + 0x370);
        float mag = *(float*)(b + 0x378) * *(float*)(b + 0x378) +
                    x * x + *(float*)(b + 0x374) * *(float*)(b + 0x374);
        if (FLOAT_80615ff4 < mag) {
            float r = 1.0f / SQRT(mag);
            r = r * -(FLOAT_80615fe4 * mag * r * r - FLOAT_80615fe8);
            r = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * mag * r * r - FLOAT_80615fe8);
            *(float*)(b + 0x370) = x * r;
            *(float*)(*(int*)(linkEnt + 8) + 0x374) *= r;
            *(float*)(*(int*)(linkEnt + 8) + 0x378) *= r;
        }
        b = *(int*)(linkEnt + 8);
        *(float*)(b + 0x364) = *(float*)(b + 0x338) * *(float*)(b + 0x378) -
                               *(float*)(b + 0x33c) * *(float*)(b + 0x374);
        b = *(int*)(linkEnt + 8);
        *(float*)(b + 0x368) = *(float*)(b + 0x33c) * *(float*)(b + 0x370) -
                               *(float*)(b + 0x334) * *(float*)(b + 0x378);
        b = *(int*)(linkEnt + 8);
        *(float*)(b + 0x36c) = *(float*)(b + 0x334) * *(float*)(b + 0x374) -
                               *(float*)(b + 0x338) * *(float*)(b + 0x370);
        b = *(int*)(linkEnt + 8);
        float x2 = *(float*)(b + 0x364);
        float mag2 = *(float*)(b + 0x36c) * *(float*)(b + 0x36c) +
                     x2 * x2 + *(float*)(b + 0x368) * *(float*)(b + 0x368);
        if (FLOAT_80615ff4 < mag2) {
            float r = 1.0f / SQRT(mag2);
            r = r * -(FLOAT_80615fe4 * mag2 * r * r - FLOAT_80615fe8);
            float rr = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * mag2 * r * r - FLOAT_80615fe8);
            *(float*)(b + 0x364) = x2 * rr;
            *(float*)(*(int*)(linkEnt + 8) + 0x368) *= rr;
            *(float*)(*(int*)(linkEnt + 8) + 0x36c) *= rr;
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x801544f8  subobj_carry_pose_commit  (7 xrefs)
// ---------------------------------------------------------------------------
// Per-frame commit of the carried object's pose. Resolves the carried body and
// its carry mode, computes the world attach point (subobj_carry_attach_point),
// then depending on the active anim clips either:
//   - samples the held/transition keyframe and rotates the object matrix into
//     the carrier's grip (default carry), or
//   - for mode-4 (dropped) reads the weapon-def orientation, or
//   - for a put-down clip builds a roll matrix and samples the end keyframe.
// Writes position +100/+0x68/+0x6c and euler +0xc0/+0x70/+200 onto the carried
// body, then propagates the aim origin/basis to the weapon link entity and the
// carrier's held-weapon slot, plus a portal room re-resolve via a short ray.
// ---------------------------------------------------------------------------
void subobj_carry_pose_commit(void)
{
    unsigned long long fr = FUN_803826dc();
    int   frameEnt = (int)(fr >> 0x20);
    unsigned int* slot = (unsigned int*)(unsigned int)fr;
    unsigned int bodyH = *slot;
    int   heldSelf = 0;
    int   rec = *(int*)(frameEnt + 0x7c);
    int   body;
    if (bodyH == 0) { body = 0; }
    else {
        body = DAT_80611c24 + (bodyH >> 0x16) * 0x2e8;
        if (*(unsigned int*)(body + 0x2c) != bodyH) { body = 0; *slot = 0; }
    }
    if (body != 0) {
        int link = *(int*)(body + 0x150);
        if (*(int*)(body + 0x20) == 8) heldSelf = *(int*)(body + 0x7c);
        unsigned int vref = slot[1];
        int* mesh = *(int**)(*(int*)(frameEnt + 4) + 4);
        int vOff  = (vref & 0xffff) * 0x18;
        int vBase = mesh[*mesh * -0x30 + ((int)vref >> 0x10) * 0x30 + 0x1b];
        float attach[3];
        subobj_carry_attach_point(frameEnt, vref, attach);
        float local[8];  // local[0..2] carry offset, local[3..5] reused
        float mtx[16];
        float rot[28];

        if (((slot[2] == 0) || (anim_is_clip_active(body, slot[2]) == 0)) &&
            ((slot[3] == 0) || (anim_is_clip_active(body, slot[3]) == 0))) {
            if ((slot[0xc] == 4) && (anim_is_clip_active(body, slot[4]) != 0)) {
                int* wslot = *(int**)(rec + slot[0xb] * 0x10 + 0x144);
                char* def = (char*)weapon_get_def_by_index(wslot[3]);
                mtx4_set_rotation_y((double)((FLOAT_80616070 * (float)wslot[9]) / FLOAT_80616048),
                                    rot);
                mtx4_multiply(rot, (float*)(rec + 8), mtx);
                MTX4_TO_EULER4(mtx, (void*)(body + 0xc0), (void*)(body + 0x70), (void*)(body + 200));
                unsigned int muzz = *(unsigned int*)(def + 0x30);
                if ((int)muzz < 0) {
                    *(float*)(body + 100)  = *(float*)(frameEnt + 100)  + attach[0];
                    *(float*)(body + 0x68) = *(float*)(frameEnt + 0x68) + attach[1];
                    *(float*)(body + 0x6c) = *(float*)(frameEnt + 0x6c) + attach[2];
                } else {
                    int* wmesh = *(int**)(*(int*)(*wslot + 4) + 4);
                    int wb = *wmesh;
                    int mo = (muzz & 0xffff) * 0x18;
                    int mi = (int)muzz >> 0x10;
                    float m[3];
                    m[0] = *(float*)(wmesh[wb * -0x30 + mi * 0x30 + 0x1b] + mo);
                    m[1] = *(float*)(wmesh[wb * -0x30 + mi * 0x30 + 0x1b] + mo + 4);
                    m[2] = *(float*)(wmesh[wb * -0x30 + mi * 0x30 + 0x1b] + mo + 8);
                    mtx4_transform_vector_alias(mtx, m);
                    *(float*)(body + 100)  = m[0] + *(float*)(*wslot + 100);
                    *(float*)(body + 0x68) = m[1] + *(float*)(*wslot + 0x68);
                    *(float*)(body + 0x6c) = m[2] + *(float*)(*wslot + 0x6c);
                }
            } else {
                float kf[4];
                if ((slot[5] == 0) || (anim_is_clip_active(body, slot[5]) == 0)) {
                    anim_sample_keyframe(
                        *(int*)(DAT_80611824 + *(int*)(*(int*)(body + 4) + 0xd0) * 4), 0, 0, kf);
                } else {
                    int def = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[slot[5]] * 4);
                    anim_sample_keyframe(def, 0, *(int*)(def + 0x10) - 1, kf);
                }
                float p[3];
                p[0] = -kf[0]; p[1] = -kf[1]; p[2] = -kf[2];
                mtx4_copy((float*)(rec + 8), mtx);
                mtx4_transform_vector_alias((float*)(rec + 8), p);
                p[0] = p[0] + attach[0];
                p[1] = p[1] + attach[1];
                p[2] = p[2] + attach[2];
                *(float*)(body + 100)  = *(float*)(frameEnt + 100)  + p[0];
                *(float*)(body + 0x68) = *(float*)(frameEnt + 0x68) + p[1];
                *(float*)(body + 0x6c) = *(float*)(frameEnt + 0x6c) + p[2];
                MTX4_TO_EULER4((float*)(rec + 8), (void*)(body + 0xc0), (void*)(body + 0x70), (void*)(body + 200));
            }
        } else {
            float kf[4];
            float roll;
            if (anim_is_clip_active(body, slot[2]) == 0) {
                anim_sample_keyframe(
                    *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[slot[3]] * 4), 0, 0, kf);
                roll = (*(float*)(vBase + vOff) < FLOAT_80615fac) ? FLOAT_80616074 : FLOAT_80616078;
                mtx4_set_rotation_y((double)roll, rot);
            } else {
                int def = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[slot[2]] * 4);
                anim_sample_keyframe(def, 0, *(int*)(def + 0x10) - 1, kf);
                roll = (FLOAT_80615fac < *(float*)(vBase + vOff)) ? FLOAT_80616074 : FLOAT_80616078;
                mtx4_set_rotation_y((double)roll, rot);
            }
            float p[3];
            p[0] = -kf[0]; p[1] = -kf[1]; p[2] = -kf[2];
            mtx4_multiply(rot, (float*)(rec + 8), mtx);
            mtx4_transform_vector_alias(mtx, p);
            MTX4_TO_EULER4(mtx, (void*)(body + 0xc0), (void*)(body + 0x70), (void*)(body + 200));
            p[0] = p[0] + attach[0];
            p[1] = p[1] + attach[1];
            p[2] = p[2] + attach[2];
            *(float*)(body + 100)  = *(float*)(frameEnt + 100)  + p[0];
            *(float*)(body + 0x68) = *(float*)(frameEnt + 0x68) + p[1];
            *(float*)(body + 0x6c) = *(float*)(frameEnt + 0x6c) + p[2];
        }

        if (heldSelf != 0) {
            *(int*)(heldSelf + 0xc00) = *(int*)(frameEnt + 0x21c);
            *(int*)(heldSelf + 0xbfc) = *(int*)(rec + 0x558);
        }
        // deg->rad the resulting euler
        double d = (double)FLOAT_80616048;
        float inv = FLOAT_80616070;
        *(float*)(body + 0xc0) = (float)(d * (double)*(float*)(body + 0xc0)) / FLOAT_80616070;
        float ey = (float)(d * (double)*(float*)(body + 0x70)) / inv;
        *(float*)(body + 0xc4) = ey;
        *(float*)(body + 0x70) = ey;
        *(float*)(body + 200)  = (float)(d * (double)*(float*)(body + 200)) / inv;

        if (link != 0) {
            if ((slot[0xc] == 3) || (slot[0xc] == 4)) {
                subobj_carry_weapon_pose();
            } else {
                int cfg = *(int*)(*(int*)(body + 4) + 0x10) +
                          *(int*)(*(int*)(*(int*)(body + 4) + 0x84) + 0x54) * 0x40;
                float p[3];
                p[0] = *(float*)(cfg + 0x30);
                p[1] = *(float*)(cfg + 0x34);
                p[2] = *(float*)(cfg + 0x38);
                mtx4_transform_vector_alias(mtx, p);
                *(float*)(link + 0xb4) = p[0] + *(float*)(body + 100);
                *(float*)(link + 0xb8) = p[1] + *(float*)(body + 0x68);
                *(float*)(link + 0xbc) = p[2] + *(float*)(body + 0x6c);
                int le = *(int*)(link + 8);
                if ((le != 0) && (*(int**)(le + 0x26c) != 0) && (**(int**)(le + 0x26c) == 0)) {
                    *(unsigned int*)(le + 0x21c) |= 0x10000;
                    *(int*)(le + 0x334) = *(int*)(link + 0xc0);
                    *(int*)(*(int*)(link + 8) + 0x338) = *(int*)(link + 0xc4);
                    *(int*)(*(int*)(link + 8) + 0x33c) = *(int*)(link + 200);
                    *(int*)(*(int*)(link + 8) + 0x364) = *(int*)(rec + 0x18);
                    *(int*)(*(int*)(link + 8) + 0x368) = *(int*)(rec + 0x1c);
                    *(int*)(*(int*)(link + 8) + 0x36c) = *(int*)(rec + 0x20);
                    int b = *(int*)(link + 8);
                    *(float*)(b + 0x370) = *(float*)(b + 0x368) * *(float*)(b + 0x33c) -
                                           *(float*)(b + 0x36c) * *(float*)(b + 0x338);
                    b = *(int*)(link + 8);
                    *(float*)(b + 0x374) = *(float*)(b + 0x36c) * *(float*)(b + 0x334) -
                                           *(float*)(b + 0x364) * *(float*)(b + 0x33c);
                    b = *(int*)(link + 8);
                    *(float*)(b + 0x378) = *(float*)(b + 0x364) * *(float*)(b + 0x338) -
                                           *(float*)(b + 0x368) * *(float*)(b + 0x334);
                    b = *(int*)(link + 8);
                    float x = *(float*)(b + 0x370);
                    float mag = *(float*)(b + 0x378) * *(float*)(b + 0x378) +
                                x * x + *(float*)(b + 0x374) * *(float*)(b + 0x374);
                    if (FLOAT_80615ff4 < mag) {
                        float r = 1.0f / SQRT(mag);
                        r = r * -(FLOAT_80615fe4 * mag * r * r - FLOAT_80615fe8);
                        r = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * mag * r * r - FLOAT_80615fe8);
                        *(float*)(b + 0x370) = x * r;
                        *(float*)(*(int*)(link + 8) + 0x374) *= r;
                        *(float*)(*(int*)(link + 8) + 0x378) *= r;
                    }
                    b = *(int*)(link + 8);
                    *(float*)(b + 0x364) = *(float*)(b + 0x338) * *(float*)(b + 0x378) -
                                           *(float*)(b + 0x33c) * *(float*)(b + 0x374);
                    b = *(int*)(link + 8);
                    *(float*)(b + 0x368) = *(float*)(b + 0x33c) * *(float*)(b + 0x370) -
                                           *(float*)(b + 0x334) * *(float*)(b + 0x378);
                    b = *(int*)(link + 8);
                    *(float*)(b + 0x36c) = *(float*)(b + 0x334) * *(float*)(b + 0x374) -
                                           *(float*)(b + 0x338) * *(float*)(b + 0x370);
                    b = *(int*)(link + 8);
                    float x2 = *(float*)(b + 0x364);
                    float mag2 = *(float*)(b + 0x36c) * *(float*)(b + 0x36c) +
                                 x2 * x2 + *(float*)(b + 0x368) * *(float*)(b + 0x368);
                    if (FLOAT_80615ff4 < mag2) {
                        float r = 1.0f / SQRT(mag2);
                        r = r * -(FLOAT_80615fe4 * mag2 * r * r - FLOAT_80615fe8);
                        float rr = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * mag2 * r * r - FLOAT_80615fe8);
                        *(float*)(b + 0x364) = x2 * rr;
                        *(float*)(*(int*)(link + 8) + 0x368) *= rr;
                        *(float*)(*(int*)(link + 8) + 0x36c) *= rr;
                    }
                }
            }
            // secondary attach point (+0xa8) + portal room re-resolve
            int cfgBase = *(int*)(*(int*)(body + 4) + 0x84);
            int mesh10  = *(int*)(*(int*)(body + 4) + 0x10);
            int c0 = mesh10 + *(int*)(cfgBase + 0xc) * 0x40;
            float p[3];
            p[0] = *(float*)(c0 + 0x30);
            p[1] = *(float*)(mesh10 + *(int*)(cfgBase + 0x4c) * 0x40 + 0x34);
            p[2] = *(float*)(c0 + 0x38);
            mtx4_transform_vector_alias(mtx, p);
            *(float*)(link + 0xa8) = p[0] + *(float*)(body + 100);
            *(float*)(link + 0xac) = p[1] + *(float*)(body + 0x68);
            *(float*)(link + 0xb0) = p[2] + *(float*)(body + 0x6c);
            float ray[3];
            ray[0] = *(float*)(link + 0xb4) - *(float*)(link + 0x158);
            ray[1] = *(float*)(link + 0xb8) - *(float*)(link + 0x15c);
            ray[2] = (float)((double)*(float*)(link + 0xbc) - (double)*(float*)(link + 0x160));
            int room = RAY_TRAVERSE_PORTALS(
                (double)*(float*)(link + 0xbc), 0.0, link + 0x158,
                *(int*)(link + 0xdec), ray, 0);
            if (room != 0) *(int*)(link + 0xdec) = room;
        }
        if (heldSelf != 0) {
            int cfgBase = *(int*)(*(int*)(body + 4) + 0x84);
            int mesh10  = *(int*)(*(int*)(body + 4) + 0x10);
            int c0 = mesh10 + *(int*)(cfgBase + 0x18) * 0x40;
            float p[3];
            p[0] = *(float*)(c0 + 0x30);
            float rollY = (link != 0) ? FLOAT_80615fd0 : FLOAT_8061607c;
            p[1] = *(float*)(c0 + 0x34) - rollY;
            p[2] = *(float*)(c0 + 0x38);
            mtx4_transform_vector_alias(mtx, p);
            *(float*)(heldSelf + 0xbb4) = p[0] + *(float*)(body + 100);
            *(float*)(heldSelf + 3000)  = p[1] + *(float*)(body + 0x68);
            *(float*)(heldSelf + 0xbbc) = p[2] + *(float*)(body + 0x6c);
            *(int*)(heldSelf + 0xbc0) = *(int*)(heldSelf + 3000);
        }
        if (*(char*)(frameEnt + 0x30) != '\0') {
            leaf_31e8(body + 0x38, frameEnt + 0x38, (int)*(char*)(frameEnt + 0x30) << 2);
            *(unsigned char*)(body + 0x30) = *(unsigned char*)(frameEnt + 0x30);
        }
        if (link == 0)
            *(int*)(body + 0x34) = *(int*)(frameEnt + 0x34);
        else
            *(int*)(body + 0x34) = *(int*)(link + 0xdec);
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x80154f48  subobj_carry_rotor_pose
// ---------------------------------------------------------------------------
// Per-frame bone-matrix pose for a carried craft with articulated rotors and
// attachment particles. For each animated bone in the config:
//   - the drive bone (rec+0x4f4) is oriented from the object matrix, optionally
//     re-aimed along the smoothed velocity direction when flag +0x58 & 4;
//   - each rotor sub-object bone builds a spin+bank euler matrix and, if it has
//     a live particle handle (+0x178), syncs the emitter position/velocity to
//     the bone tip;
//   - remaining bones copy the object matrix straight through.
// Then every attach-point (rec+0x144, count +0x164) is posed via
// subobj_carry_attach_point and given the craft's euler + world position, its
// floor room resolved, and a forced bone update.
// ---------------------------------------------------------------------------
void subobj_carry_rotor_pose(void)
{
    int frameEnt = FUN_803826d8();
    if (*(int*)(*(int*)(frameEnt + 4) + 0x10) != 0) {
        float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(frameEnt + 0x80));
        int rec = *(int*)(frameEnt + 0x7c);
        // clear the 3 collision group bits owned by rotor bones
        int bitOff = 0;
        for (int k = 0; k < 3; ++k) {
            *(unsigned int*)(*(int*)(frameEnt + 4) + 0x30) &=
                ~*(unsigned int*)(rec + bitOff + 0x168);
            bitOff = bitOff + 4;
        }
        int rot[28];               // scratch matrix (as int words for copy)
        int boneOff = 0;
        int boneIdx = 0;
        int* palette = *(int**)(*(int*)(frameEnt + 4) + 4);
        int driveBone = *(int*)(rec + 0x4f4);
        while (boneIdx < palette[1]) {
            palette = *(int**)(*(int*)(frameEnt + 4) + 4);
            int mtxDst = *(int*)(*(int*)(frameEnt + 4) + 0x10) + boneOff;
            if (boneIdx == driveBone) {
                if ((*(unsigned int*)(rec + 0x58) & 4) == 0) {
                    mtx4_copy(objMtx, (float*)mtxDst);
                } else {
                    // re-orient the drive bone along the smoothed motion vector
                    float fwd[3], up[3], side[3];
                    fwd[0] = *(float*)(rec + 0x4e0) - *(float*)(rec + 0x4b0);
                    float biasY = FLOAT_80616008 *
                        (*(float*)(rec + 0x4e0) + *(float*)(rec + 0x4c8) +
                         *(float*)(rec + 0x498) + *(float*)(rec + 0x4b0));
                    unsigned int dref = *(unsigned int*)(rec + 0x4f8);
                    float* dv = (float*)(palette[*palette * -0x30 +
                                         ((int)dref >> 0x10) * 0x30 + 0x1b] +
                                         (dref & 0xffff) * 0x18);
                    fwd[1] = *(float*)(rec + 0x4dc) - *(float*)(rec + 0x4ac);
                    fwd[2] = *(float*)(rec + 0x4e4) - *(float*)(rec + 0x4b4);
                    float m = fwd[2] * fwd[2] + fwd[1] * fwd[1] + fwd[0] * fwd[0];
                    if (FLOAT_80615ff4 < m) {
                        float r = 1.0f / SQRT(m);
                        r = r * -(FLOAT_80615fe4 * m * r * r - FLOAT_80615fe8);
                        m = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * m * r * r - FLOAT_80615fe8);
                        fwd[1] *= m; fwd[0] *= m; fwd[2] *= m;
                    }
                    up[0] = *(float*)(rec + 0x494) - *(float*)(rec + 0x4c4);
                    up[1] = *(float*)(rec + 0x498) - *(float*)(rec + 0x4c8);
                    up[2] = *(float*)(rec + 0x49c) - *(float*)(rec + 0x4cc);
                    m = up[2] * up[2] + up[0] * up[0] + up[1] * up[1];
                    if (FLOAT_80615ff4 < m) {
                        float r = 1.0f / SQRT(m);
                        r = r * -(FLOAT_80615fe4 * m * r * r - FLOAT_80615fe8);
                        m = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * m * r * r - FLOAT_80615fe8);
                        up[0] *= m; up[1] *= m; up[2] *= m;
                    }
                    side[0] = up[0] * fwd[1] - up[1] * fwd[0];   // reuse: cross terms
                    float sy = up[2] * fwd[0] - up[0] * fwd[2];
                    float sz = up[1] * fwd[2] - up[2] * fwd[1];
                    (void)sy; (void)sz;
                    MTX4_SET_XLATE3((double)dv[0], (double)(dv[1] + biasY), (double)dv[2]);
                    mtx4_multiply((float*)rot, objMtx, (float*)mtxDst);
                    mtx4_from_basis((float*)rot, fwd, &side[0], up);
                    mtx4_multiply((float*)rot, (float*)mtxDst, (float*)mtxDst);
                    MTX4_SET_XLATE3(-(double)dv[0], -(double)dv[1], -(double)dv[2]);
                    mtx4_multiply((float*)rot, (float*)mtxDst, (float*)mtxDst);
                }
                mtx4_copy((float*)mtxDst, (float*)(rec + 8));
                *(float*)(rec + 0x38) = FLOAT_80615fac;
                *(float*)(rec + 0x3c) = FLOAT_80615fac;
                *(float*)(rec + 0x40) = FLOAT_80615fac;
            } else {
                int subOff = 0;
                bool posed = false;
                int n = *(int*)(*(int*)(rec + 0x50) + 0x9c);
                if (0 < n) {
                    do {
                        int sub = rec + subOff;
                        if (boneIdx == *(int*)(sub + 0x184)) {
                            float* piv = *(float**)(sub + 0x188);
                            if ((*(unsigned int*)(sub + 0x17c) & 2) == 0) {
                                mtx4_set_transform_euler(
                                    (double)piv[0], (double)piv[1], (double)piv[2],
                                    (double)*(float*)(sub + 0x18c),
                                    (double)FLOAT_80615fac, (double)FLOAT_80615fac, (float*)rot);
                            } else {
                                mtx4_set_transform_euler(
                                    (double)piv[0], (double)piv[1], (double)piv[2],
                                    (double)*(float*)(sub + 0x18c),
                                    (double)*(float*)(sub + 400),
                                    (double)FLOAT_80615fac, (float*)rot);
                            }
                            mtx4_multiply((float*)rot, objMtx, (float*)mtxDst);
                            piv = *(float**)(sub + 0x188);
                            MTX4_SET_XLATE3(-(double)piv[0], -(double)piv[1], -(double)piv[2]);
                            mtx4_multiply((float*)rot, (float*)mtxDst, (float*)mtxDst);
                            float tip[3];
                            tip[0] = *(float*)(sub + 0x19c);
                            tip[1] = *(float*)(sub + 0x1a0);
                            tip[2] = *(float*)(sub + 0x1a4);
                            posed = true;
                            mtx4_transform_vector_alias(objMtx, tip);
                            *(float*)(sub + 0x1a8) = tip[0];
                            *(float*)(sub + 0x1ac) = tip[1];
                            *(float*)(sub + 0x1b0) = tip[2];
                            *(float*)(sub + 0x1b4) = *(float*)(sub + 0x1a8) + *(float*)(frameEnt + 100);
                            *(float*)(sub + 0x1b8) = *(float*)(sub + 0x1ac) + *(float*)(frameEnt + 0x68);
                            *(float*)(sub + 0x1bc) = *(float*)(sub + 0x1b0) + *(float*)(frameEnt + 0x6c);
                            int h = *(int*)(sub + 0x178);
                            if (particle_handle_is_valid(&h)) {
                                float back = -*(float*)(sub + 0x198);
                                float emit[3];
                                emit[0] = objMtx[4] * back + *(float*)(sub + 0x1b4);
                                emit[1] = objMtx[5] * back + *(float*)(sub + 0x1b8);
                                emit[2] = objMtx[6] * back + *(float*)(sub + 0x1bc);
                                float vel[3];
                                vel[0] = objMtx[4] - objMtx[8];
                                vel[1] = objMtx[5] - objMtx[9];
                                vel[2] = objMtx[6] - objMtx[10];
                                float m = vel[2] * vel[2] + vel[0] * vel[0] + vel[1] * vel[1];
                                if (FLOAT_80615ff4 < m) {
                                    float r = 1.0f / SQRT(m);
                                    r = r * -(FLOAT_80615fe4 * m * r * r - FLOAT_80615fe8);
                                    m = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * m * r * r - FLOAT_80615fe8);
                                    vel[0] *= m; vel[1] *= m; vel[2] *= m;
                                }
                                int h2 = *(int*)(sub + 0x178);
                                particle_cached_set_position((ParticleEmitter**)&h2, (Vec3*)emit);
                                int h3 = *(int*)(sub + 0x178);
                                particle_cached_set_velocity((ParticleEmitter**)&h3, (Vec3*)vel);
                            }
                            break;
                        }
                        subOff = subOff + 100;
                        n = n - 1;
                    } while (n != 0);
                }
                if (!posed) mtx4_copy(objMtx, (float*)mtxDst);
            }
            boneIdx = boneIdx + 1;
            boneOff = boneOff + 0x40;
        }
        // pose each attach point
        if (*(int*)(rec + 0x164) != 0) {
            float ex, ey, ez;
            MTX4_TO_EULER4((float*)(rec + 8), &ex, &ey, &ez);
            ex = (FLOAT_80616048 * ex) / FLOAT_80616070;
            ey = (FLOAT_80616048 * ey) / FLOAT_80616070;
            ez = (FLOAT_80616048 * ez) / FLOAT_80616070;
            int apOff = 0;
            for (int i = 0; i < *(int*)(rec + 0x164); i = i + 1) {
                int* ap = (int*)(rec + apOff + 0x144);
                int* body = (int*)*ap;
                if ((body != 0) && (*body != 0)) {
                    int bodyRec = *body;
                    if (ap[3] != -1) {
                        mtx4_copy((float*)(rec + 8),
                            (float*)(*(int*)(*(int*)(frameEnt + 4) + 0x10) + ap[3] * 0x40));
                        int dst = *(int*)(*(int*)(frameEnt + 4) + 0x10) + ap[3] * 0x40;
                        *(float*)(dst + 0x30) = FLOAT_80615fac;
                        *(float*)(dst + 0x34) = FLOAT_80615fac;
                        *(float*)(dst + 0x38) = FLOAT_80615fac;
                    }
                    float aptip[3];
                    subobj_carry_attach_point(frameEnt, (unsigned int)*(unsigned short*)(ap + 1),
                                              aptip);
                    aptip[0] = aptip[0] + *(float*)(frameEnt + 100);
                    aptip[1] = aptip[1] + *(float*)(frameEnt + 0x68);
                    aptip[2] = aptip[2] + *(float*)(frameEnt + 0x6c);
                    *(float*)(bodyRec + 0xc0) = ex;
                    *(float*)(bodyRec + 0x70) = ey;
                    *(float*)(bodyRec + 200)  = ez;
                    *(unsigned char*)(bodyRec + 0x32) = 2;
                    *(unsigned int*)(bodyRec + 0x18) |= 0x40;
                    if (subobj_is_idle(*(unsigned int*)(frameEnt + 0x80)) == 0)
                        *(unsigned int*)(bodyRec + 0x18) |= 2;
                    *(float*)(bodyRec + 100)  = aptip[0];
                    *(float*)(bodyRec + 0x68) = aptip[1];
                    *(float*)(bodyRec + 0x6c) = aptip[2];
                    float fireOrigin[3];
                    weapon_compute_fire_origin(bodyRec, body[0x1c], 0, fireOrigin, (float*)0);
                    int room = FLOOR_HEIGHT((double)FLOAT_80615fb8,
                                            (double)FLOAT_80615fb8);
                    body[0x2c] = room;
                    if ((body[0x2c] < 1) || (200 < body[0x2c]))
                        body[0x2c] = *(int*)(bodyRec + 0x34);
                    if (((*(int*)(*(int*)(bodyRec + 4) + 0x10) == 0) ||
                         ((*(unsigned int*)(bodyRec + 0x28) & 0x200000) == 0)) ||
                        ((*(unsigned int*)(bodyRec + 0x10) & 0x80000000) == 0)) {
                        *(unsigned int*)(bodyRec + 0x10) |=
                            (1u << DAT_80611d58) - 1u & 0xffff;
                        entity_force_bone_update(bodyRec);
                    }
                    entity_update_bounds(bodyRec);
                }
                apOff = apOff + 0x10;
            }
        }
        *(unsigned int*)(frameEnt + 0x28) |= 0x200000;
    }
    FUN_80382724();
}

// ===========================================================================
// batch_427 : carried-object DEFORM / ROTOR / BREAK physics (0x801557e0 - 0x80156cb8)
// ---------------------------------------------------------------------------
// The physics-side helpers driven by subobj_carry_physics_tick for a carried
// deformable/rotor craft (helicopter-style prop). A carried body keeps four
// "corner" deform accumulators at rec+0x494 (0x18 stride) and per-rotor
// sub-object records at rec+0x174 (100-byte stride, angle +0x1c / rev +0x5c).
// This set contains: the deform-impulse distributor and its callers (bone /
// ground / break impulses), the per-frame rotor spin integrator, the ground
// downwash effect, the deform-relax spring, plus the frame-registered shatter
// burst and grabbed-slot death dispatch.
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x801557e0  subobj_break_apply_separation
// ---------------------------------------------------------------------------
// Break-off separation handler for a grabbed body (dat = per-slot break record).
// In game mode 5 it first runs the coop notify FUN_802168dc, then computes a
// separation direction from the break record's stored endpoints (dat[0xb] /
// dat[0xc] override, else current body position), normalizes it (fast rsqrt),
// scales by the configured break force (FLOAT_80616040 * dat[8] * dat[0xd]),
// distributes it into the deform corners (subobj_deform_accumulate), runs
// subobj_break_tick, and fires the owner break callback (+0x524).
// ---------------------------------------------------------------------------
void subobj_break_apply_separation(void* p1, double p2, double p3, void* p4, void* p5,
                                   void* p6, void* p7, void* p8, int* dat)
{
    int rec = *(int*)(*dat + 0x7c);
    if (DAT_80612614 == 5) {
        FUN_802168dc(p1, p2, p3, p4, p5, p6, p7, p8, *dat, (unsigned int)dat);
    }
    float base[3];
    float* endA = (float*)dat[10];
    if (endA == 0) {
        int b = *dat;
        base[0] = *(float*)(b + 100);
        base[1] = *(float*)(b + 0x68);
        base[2] = *(float*)(b + 0x6c);
    } else {
        base[0] = endA[0]; base[1] = endA[1]; base[2] = endA[2];
    }
    float dir[3];
    float* endB = (float*)dat[0xb];
    if (endB == 0) {
        float* endC = (float*)dat[0xc];
        if (endC == 0) {
            int b = *dat;
            dir[0] = *(float*)(b + 100)  - base[0];
            dir[1] = *(float*)(b + 0x68) - base[1];
            dir[2] = *(float*)(b + 0x6c) - base[2];
        } else {
            dir[0] = endC[0]; dir[1] = endC[1]; dir[2] = endC[2];
        }
    } else {
        dir[0] = -endB[0]; dir[1] = -endB[1]; dir[2] = -endB[2];
    }
    double dx = (double)dir[0], dy = (double)dir[1], dz = (double)dir[2];
    double r = (double)(FLOAT_80615fa8 /
        (float)((((int)((float)(dz * dz) + (float)(dx * dx) + (float)(dy * dy)) >> 1) + 0x1fc00000)));
    double nx = (double)(float)(dx * r);
    double ny = (double)(float)(dy * r);
    double nz = (double)(float)(dz * r);
    double scale = (double)(FLOAT_80616040 * (float)dat[8] * (float)dat[0xd]);
    dir[0] = (float)(nx * scale);
    dir[1] = (float)(ny * scale);
    dir[2] = (float)(nz * scale);
    int acc = subobj_deform_accumulate(*dat, base, dir);
    BREAK_TICK(acc, (double)dir[0], nz, ny, nx, dy, dz, p8);
    void (**cb)(int, int*) = (void (**)(int, int*))(rec + 0x524);
    if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), dat);
}

// ---------------------------------------------------------------------------
// 0x801559b8  subobj_carry_shatter_burst  (frame-registered)
// ---------------------------------------------------------------------------
// Destruction burst for a carried craft (game mode 5, flag +0x58 & 1). Spawns
// 7 physics-body debris fragments: each is a fresh entity_create_instance from
// a random debris palette (DAT_803ccb20/24/28) positioned at the contact origin
// with a random offset (rng_counter_tick), allocated a sub-object descriptor
// (subobj_build_descriptor_default + subobj_pool_alloc), given an orthonormal
// matrix + collision group, marked dirty, and launched with a random impulse
// (subobj_add_force_at_point) toward the descriptor's world point. Then 3
// explosion FX (player_ai_explosion_spawn kind 0x33) at random points plus a
// final explosion at the entity center. Sets flag +0x28|0x10 (destroyed).
// ---------------------------------------------------------------------------
void subobj_carry_shatter_burst(void)
{
    int entity = FUN_803826d4();
    int rec = *(int*)(entity + 0x7c);
    float* origin = (float*)subobj_get_contact_origin(*(unsigned int*)(entity + 0x80));
    float color[4];
    color[3] = *(float*)&DAT_803ccb14;
    // (DAT_803ccb18/1c are trailing color words used by the FX path)

    if ((DAT_80612614 == 5) && ((*(unsigned int*)(rec + 0x58) & 1) != 0)) {
        float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(entity + 0x80));
        float palette[3];
        palette[0] = DAT_803ccb20;
        palette[1] = *(float*)&DAT_803ccb24;
        palette[2] = *(float*)&DAT_803ccb28;
        for (int i = 0; i < 7; ++i) {
            float spawnPos[3];
            float orient[3] = { FLOAT_80615fac, FLOAT_80615fac, FLOAT_80615fac };
            // random offset around the contact origin, scaled by entity extents
            float ox = (float)((double)FLOAT_80615fec * (double)(float)(
                ((float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088) - FLOAT_80615fe4));
            float oy = (float)((double)FLOAT_80615fec * (double)(float)(
                ((float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088) - FLOAT_80615fe4));
            float oz = (float)((double)FLOAT_80615fec * (double)(float)(
                ((float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088) - FLOAT_80615fe4));
            spawnPos[0] = ox * *(float*)(entity + 0x1c0) + origin[0];
            spawnPos[1] = oy * *(float*)(entity + 0x1c4) + origin[1];
            spawnPos[2] = oz * *(float*)(entity + 0x1c8) + origin[2];
            unsigned int p = RNG_TICK() % 3;
            int* frag = (int*)CREATE_INSTANCE((int)palette[p], spawnPos, orient);
            unsigned int h = POOL_ALLOC();
            if (h == 0) { entity_destroy_with_event(frag); break; }
            *(unsigned int*)(entity + 0x28) |= 0x10;
            *(float*)(entity + 0x1d8) = FLOAT_80615fac;
            *(float*)(entity + 0x1cc) = FLOAT_80615fac;
            frag[0x20] = (int)h;
            BUILD_DESC_DEFAULT(h, frag, *(unsigned int*)(*(int*)(rec + 0x50) + 0xa0) & 0xff, -1, 0.0f);
            subobj_build_orthonormal_matrix(h, objMtx, (float*)0);
            subobj_set_collision_groups(h, 8, 0xf1);
            subobj_dirty_list_add(h);
            int desc = subobj_get_descriptor(h);
            weapon_start_delayed_event((double)FLOAT_80615fa4, (int)frag, 8);
            // launch impulse: direction from origin to descriptor world point
            float d[3];
            d[0] = *(float*)(desc + 0x140) - origin[0];
            d[1] = *(float*)(desc + 0x144) - origin[1];
            d[2] = *(float*)(desc + 0x148) - origin[2];
            float mag = d[2] * d[2] + d[0] * d[0] + d[1] * d[1];
            if (FLOAT_80616054 <= ABS((double)mag)) {
                float seed = (float)(((int)mag >> 1) + 0x1fc00000);
                float inv = FLOAT_80615fa8 / ((seed * seed + mag) / (FLOAT_80615fec * seed));
                d[0] = d[0] * inv; d[1] = d[1] * inv; d[2] = d[2] * inv;
            } else {
                d[0] = FLOAT_80615fec * ((float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088 - FLOAT_80615fe4);
                d[1] = FLOAT_80615fec * ((float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088 - FLOAT_80615fe4);
                d[2] = FLOAT_80615fec * ((float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088 - FLOAT_80615fe4);
            }
            float launchMag = (float)((double)FLOAT_80616090 * (double)(float)(
                (double)FLOAT_80616094 * (double)(float)(((float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088))) + (double)FLOAT_8061608c);
            d[0] *= launchMag; d[1] *= launchMag; d[2] *= launchMag;
            float pt[3];
            pt[0] = (float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088 * (float)frag[0x70] + (float)frag[0x19] + (float)frag[0x6a];
            pt[1] = (float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088 * (float)frag[0x71] + (float)frag[0x1a] + (float)frag[0x6b];
            pt[2] = (float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088 * (float)frag[0x72] + (float)frag[0x1b] + (float)frag[0x6c];
            subobj_add_force_at_point(h, pt, d);
        }
        // three scattered explosions
        float efx[3];
        float exColor[4];
        exColor[3] = color[3];
        efx[0] = DAT_803ccb20;                 // reuse palette slot as scratch
        for (int j = 0; j < 3; ++j) {
            float ep[3];
            ep[0] = (float)((double)((float)((double)FLOAT_80615fec * (double)(float)(
                ((float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088) - FLOAT_80615fe4)) *
                *(float*)(entity + 0x1c0)) * (double)FLOAT_80615fe4) + origin[0];
            ep[1] = (float)((double)((float)((double)FLOAT_80615fec * (double)(float)(
                ((float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088) - FLOAT_80615fe4)) *
                *(float*)(entity + 0x1c4)) * (double)FLOAT_80615fe4) + origin[1];
            ep[2] = (float)((double)((float)((double)FLOAT_80615fec * (double)(float)(
                ((float)((double)RNG_TICK() - DOUBLE_80616098) / FLOAT_80616088) - FLOAT_80615fe4)) *
                *(float*)(entity + 0x1c8)) * (double)FLOAT_80615fe4) + origin[2];
            float size = (RNG_TICK() & 1) ? ep[1] : ep[0];
            EXPLOSION_SPAWN(size, 0x33, ep, exColor, 0, 0, 1);
        }
        float* wpos = (float*)entity_get_world_position(entity);
        EXPLOSION_SPAWN((float)0x28, 0x33, wpos, exColor, 0, 0, 1);
    }
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// 0x801560c8  subobj_carry_dispatch_slot_deaths  (frame-registered)
// ---------------------------------------------------------------------------
// For each occupied grab slot whose grabbed body is a live character (type 8),
// invoke character_on_death_handler on that body. Copies the frame scratch
// (leaf_31e8 0x5c bytes) and threads the register-pair death handler over the
// slot loop (rec+0x70, 0x34 stride, count rec+0x140).
// ---------------------------------------------------------------------------
void subobj_carry_dispatch_slot_deaths(unsigned long long p1, double p2, double p3, double p4,
                                       unsigned long long p5, unsigned long long p6,
                                       unsigned long long p7, unsigned long long p8)
{
    (void)p1;
    unsigned long long fr = FRAME_803826f0();
    int rec = *(int*)((int)(fr >> 0x20) + 0x7c);
    int scratch[30];
    leaf_31e8((unsigned int)scratch, (unsigned int)fr, 0x5c);
    unsigned long long acc = fr;
    int off = 0;
    for (int i = 0; i < *(int*)(rec + 0x140); ++i) {
        unsigned int h = *(unsigned int*)(rec + off + 0x70);
        int body;
        if (h == 0) { body = 0; }
        else {
            body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int*)(body + 0x2c) != h) { body = 0; *(int*)(rec + off + 0x70) = 0; }
        }
        if ((body != 0) && (*(int*)(body + 0x20) == 8)) {
            scratch[0] = body;
            acc = ON_DEATH_HANDLER(acc, p2, p3, p4, p5, p6, p7, p8);
        }
        off = off + 0x34;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80156180  subobj_deform_apply_impulse
// ---------------------------------------------------------------------------
// Apply a deformation impulse to a carried body's corner accumulators. Builds a
// world contact point from the body position + AABB offset (dat[1]/dat[2] are
// the two lateral coords), scaled by the per-material half-extent table
// DAT_8048ca9c. Reads the body's stored per-frame motion (+0xf8/+0xfc/+0x100,
// scaled by frame time), transforms it into local space (subobj_world_to_local_dir),
// seeds a deform constraint (subobj_constraint_init), and drives the corner
// spring (subobj_deform_ground_react) with the projected impulse magnitude.
// ---------------------------------------------------------------------------
void subobj_deform_apply_impulse(int entity, int* dat)
{
    int body = *dat;
    if ((*(int*)(body + 0x7c) != 0) && (*(int*)(body + 0x20) == 8)) {
        float cp[3];
        cp[0] = *(float*)(body + 100)  + *(float*)(body + 0x1a8);
        cp[1] = *(float*)(body + 0x68) + *(float*)(body + 0x1ac);
        cp[2] = *(float*)(body + 0x6c) + *(float*)(body + 0x1b0);
        float axis[3];
        axis[0] = (float)dat[1];
        axis[1] = FLOAT_80615fac;
        axis[2] = (float)dat[2];
        float he = -*(float*)(&DAT_8048ca9c + *(short*)(*(int*)(body + 0x7c) + 0x48) * 0x30);
        float wpt[3];
        wpt[0] = axis[0] * he + cp[0];
        wpt[1] = FLOAT_80615fac * he + cp[1];
        wpt[2] = axis[2] * he + cp[2];
        float mv[3];
        if (DAT_806108e4 < 1) {
            float t = (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80615fd8);
            mv[0] = *(float*)(body + 0xf8) * t;
            mv[1] = *(float*)(body + 0xfc) * t;
            mv[2] = *(float*)(body + 0x100) * t;
        } else {
            float t = (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80615fd8) / FLOAT_806108ec;
            mv[0] = *(float*)(body + 0xf8) * t;
            mv[1] = *(float*)(body + 0xfc) * t;
            mv[2] = *(float*)(body + 0x100) * t;
        }
        float local[3];
        subobj_world_to_local_dir(*(unsigned int*)(entity + 0x80), wpt, local);
        double lx = (double)(float)((double)mv[0] + (double)local[0]);
        double ly = (double)(float)((double)mv[1] + (double)local[1]);
        double lz = (double)(float)((double)mv[2] + (double)local[2]);
        int con[14];
        subobj_constraint_init((double)FLOAT_80615fac, (double)FLOAT_80615fac,
                               (float*)con, cp, axis, 0, entity, body, -1);
        double proj = (double)(float)(lz * (double)axis[2] +
            (double)(float)(lx * (double)axis[0] + (double)(float)(ly * (double)axis[1])));
        subobj_deform_ground_react(proj, entity, (int)con);
    }
}

// ---------------------------------------------------------------------------
// 0x80156398  subobj_deform_ground_react
// ---------------------------------------------------------------------------
// When a deform constraint (type-3, con+0x2e) penetrates (depth < 0), push it
// back out: scale the constraint axis (con+0xc..+0x14) by the penetration times
// FLOAT_806160a0, distribute into the corner accumulators (subobj_deform_accumulate),
// and refresh the active-grab bookkeeping (subobj_find_active_grab).
// ---------------------------------------------------------------------------
void subobj_deform_ground_react(double depth, int entity, int rec)
{
    if ((*(short*)(rec + 0x2e) == 3) && (depth < (double)FLOAT_80615fac)) {
        float s = (float)((double)FLOAT_806160a0 * -depth);
        float imp[3];
        imp[0] = *(float*)(rec + 0xc) * s;
        imp[1] = *(float*)(rec + 0x10) * s;
        imp[2] = *(float*)(rec + 0x14) * s;
        subobj_deform_accumulate(entity, (float*)(rec + 0xc), imp);
        int grabOut;
        subobj_find_active_grab(entity, &grabOut);
    }
}

// ---------------------------------------------------------------------------
// 0x80156420  subobj_carry_ground_effect  (frame-registered)
// ---------------------------------------------------------------------------
// Per-frame ground-downwash effect for a carried craft near the floor (mode 0,
// engine mode != 2, flag +0x58 & 0x20, rev >= 1). Two branches:
//  - normal (flag +0x58 & 0x80000 clear): pick the two rotor sub-objects with
//    the largest downwash weight (+0x1a4), then for each raycast straight down
//    (scene_raycast) and, if within the min/max hit distance, apply a lift force
//    at the rotor point scaled by penetration, hit range and rev magnitude
//    (subobj_add_force_at_point);
//  - grounded (flag set): apply a settling angular force aligning the craft up
//    axis to the ground normal (subobj_add_angular_force).
// ---------------------------------------------------------------------------
void subobj_carry_ground_effect(void)
{
    int entity = FUN_803826d8();
    int rec = *(int*)(entity + 0x7c);
    float wA = DAT_806160a4;
    float wB = DAT_806160a8;
    int sel[2];
    sel[0] = DAT_806160ac;
    sel[1] = DAT_806160b0;

    if ((*(int*)(rec + 0x54) == 0) && (*(int*)(rec + 0x60) != 2) &&
        ((*(unsigned int*)(rec + 0x58) & 0x20) != 0) &&
        (FLOAT_80615fa8 <= *(float*)(rec + 0x52c))) {
        if ((*(unsigned int*)(rec + 0x58) & 0x80000) == 0) {
            // pick the two strongest downwash rotors
            int idx = 0, off = 0;
            int n = *(int*)(*(int*)(rec + 0x50) + 0x9c);
            if (0 < n) {
                do {
                    float w = *(float*)(rec + off + 0x1a4);
                    if (w <= wA) {
                        if (wB < w) { sel[1] = idx; wB = w; }
                    } else {
                        wB = wA; sel[1] = sel[0]; sel[0] = idx; wA = w;
                    }
                    idx = idx + 1; off = off + 100; n = n - 1;
                } while (n != 0);
            }
            int pass = 0, po = 0;
            do {
                unsigned int h = *(unsigned int*)(entity + 0x80);
                float* objMtx = (float*)subobj_get_object_matrix(h);
                double rev = (double)*(float*)(rec + 0x52c);
                int rotor = rec + *(int*)((int)sel + po) * 100 + 0x174;
                double clampRev = rev;
                if ((double)FLOAT_806160bc < rev) clampRev = (double)FLOAT_806160bc;
                if ((double)FLOAT_80615fa8 <= clampRev) {
                    if ((double)FLOAT_806160bc < rev) rev = (double)FLOAT_806160bc;
                } else {
                    rev = (double)FLOAT_80615fa8;
                }
                float castEnd[3];
                castEnd[0] = FLOAT_80615fac;
                castEnd[1] = (float)((double)FLOAT_80615fb8 * rev + (double)FLOAT_806160b8);
                castEnd[2] = -*(float*)(rotor + 0x24);
                mtx4_transform_vector_alias(objMtx, castEnd);
                float hitPos[3], hitNorm[3];
                int hit = SCENE_RAYCAST(*(int*)(entity + 0x34), (int*)(rotor + 0x40), castEnd, 0,
                                        hitPos, hitNorm, (int*)0, 0xfbbfff78, 0, (int*)0);
                if ((hit != 0) && (hitPos[1] <= FLOAT_806160c0) && (FLOAT_806160c4 <= hitPos[1])) {
                    float dx = *(float*)(rotor + 0x40) - hitPos[0];
                    float dy = *(float*)(rotor + 0x44) - hitPos[1];
                    float dz = *(float*)(rotor + 0x48) - hitPos[2];
                    double radSq = (double)(*(float*)(rotor + 0x24) * *(float*)(rotor + 0x24));
                    double distSq = (double)(dz * dz + dx * dx + dy * dy);
                    if (radSq <= distSq) {
                        double castLen = math_sqrt((double)(float)(
                            (double)castEnd[1] * (double)castEnd[1] + radSq));
                        double dist = distSq;
                        if ((double)FLOAT_80616054 < distSq) dist = math_sqrt(distSq);
                        float pen = (float)(castLen - (double)*(float*)(rotor + 0x24));
                        pen = (pen - (float)(dist - (double)*(float*)(rotor + 0x24))) / pen;
                        float penC = pen; if (FLOAT_80615fa8 < pen) penC = FLOAT_80615fa8;
                        float penClamped = FLOAT_80615fac;
                        if ((FLOAT_80615fac <= penC) && (penClamped = pen, FLOAT_80615fa8 < pen)) penClamped = FLOAT_80615fa8;
                        float rng = (hitPos[1] - FLOAT_806160c4) / FLOAT_806160cc;
                        float rngC = rng; if (FLOAT_80615fa8 < rng) rngC = FLOAT_80615fa8;
                        float rngClamped = FLOAT_80615fac;
                        if ((FLOAT_80615fac <= rngC) && (rngClamped = rng, FLOAT_80615fa8 < rng)) rngClamped = FLOAT_80615fa8;
                        float rv = FLOAT_806160bc;
                        if (*(float*)(rec + 0x52c) < FLOAT_806160bc) rv = *(float*)(rec + 0x52c);
                        rv = (rv - FLOAT_80615fa8) / FLOAT_806160d0;
                        float seed = (float)(((int)rv >> 1) + 0x1fc00000);
                        rv = (seed * seed + rv) / (FLOAT_80615fec * seed);
                        float rvC = rv; if (FLOAT_80615fa8 < rv) rvC = FLOAT_80615fa8;
                        float rvClamped = FLOAT_80615fac;
                        if ((FLOAT_80615fac <= rvC) && (rvClamped = rv, FLOAT_80615fa8 < rv)) rvClamped = FLOAT_80615fa8;
                        double f = (double)(FLOAT_806160c8 * penClamped * rngClamped * rvClamped);
                        double invM = subobj_get_inverse_mass(h);
                        float force[3];
                        float k = (float)(f * invM);
                        force[0] = objMtx[4] * k;
                        force[1] = objMtx[5] * k;
                        force[2] = objMtx[6] * k;
                        subobj_add_force_at_point(h, (float*)(rotor + 0x40), force);
                    }
                }
                pass = pass + 1; po = po + 4;
            } while (pass < 2);
        } else {
            unsigned int h = *(unsigned int*)(entity + 0x80);
            float* objMtx = (float*)subobj_get_object_matrix(h);
            float up[3];
            up[0] = DAT_803ccb2c; up[1] = DAT_803ccb30; up[2] = DAT_803ccb34;
            float dot = objMtx[6] * DAT_803ccb34 + objMtx[4] * DAT_803ccb2c + objMtx[5] * DAT_803ccb30;
            dot = dot * dot;
            if (FLOAT_80615fac < dot) {
                dot = dot * FLOAT_806160b4;
                float torque[3];
                torque[0] = objMtx[0] * dot;
                torque[1] = objMtx[1] * dot;
                torque[2] = objMtx[2] * dot;
                subobj_add_angular_force(h, torque);
            }
        }
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x80156914  subobj_rotor_integrate
// ---------------------------------------------------------------------------
// Per-frame rotor spin + rev integration for a carried craft. Sets the "spun-up"
// flag +0x58|0x10000, then per rotor sub-object (rec+0x174, 100-byte stride):
//   - counts rotors whose spin stamp (+0xc) is within 10 frames of the current
//     frame (DAT_80611928) to decide the engine-running state;
//   - integrates rev target (+0x50/+0x4c) from the drive rev +0x534 and per-rotor
//     bias +0x60, respecting engine mode (2 = reverse);
//   - clamps the spin angle accumulator (+0x54/+0x58) and clears the spun-up flag
//     if any rotor is still spinning. Finally sets flag +0x80000 (idle) when no
//     rotor is active, else clears it (+0x80000 clear via ~0x80000... here bit).
// ---------------------------------------------------------------------------
void subobj_rotor_integrate(int entity)
{
    int rec = *(int*)(entity + 0x7c);
    int spinning = 0;
    int off = 0;
    *(unsigned int*)(rec + 0x58) |= 0x10000;
    for (int i = 0; i < *(int*)(*(int*)(rec + 0x50) + 0x9c); ++i) {
        int rotor = rec + off + 0x174;
        if (FLOAT_80615fac < *(float*)(rotor + 0x30)) {
            spinning += (unsigned int)((unsigned int)(DAT_80611928 - *(int*)(rotor + 0xc)) < 10);
        }
        float target = FLOAT_80615fac;
        if ((*(unsigned int*)(rotor + 8) & 1) != 0) {
            if (*(int*)(rec + 0x60) == 2)
                target = FLOAT_80615fac - *(float*)(rotor + 0x5c);
            else
                target = FLOAT_80615fac + *(float*)(rotor + 0x5c);
        }
        if (*(float*)(rotor + 0x58) <= FLOAT_80615fa8) {
            if (FLOAT_80616024 <= *(float*)(rotor + 0x58)) {
                *(float*)(rotor + 0x58) = FLOAT_80615fac;
                *(float*)(rotor + 0x54) = FLOAT_80615fac;
            } else {
                target = target + *(float*)(rec + 0x534) + *(float*)(rotor + 0x60);
            }
        } else {
            target = target - (*(float*)(rec + 0x534) + *(float*)(rotor + 0x60));
        }
        if ((FLOAT_80615fac < ABS(*(float*)(rotor + 0x5c))) ||
            (FLOAT_80615fa8 < ABS(*(float*)(rotor + 0x58)))) {
            *(unsigned int*)(rec + 0x58) &= 0xfffeffff;
        }
        *(float*)(rotor + 0x50) = target;
        *(float*)(rotor + 0x4c) = target;
        *(float*)(rotor + 0x5c) = FLOAT_80615fac;
        *(float*)(rotor + 0x60) = FLOAT_80615fac;
        off = off + 100;
    }
    if (spinning != 0) {
        *(unsigned int*)(rec + 0x58) &= 0xfff7ffff;
        return;
    }
    *(unsigned int*)(rec + 0x58) |= 0x80000;
}

// ---------------------------------------------------------------------------
// 0x80156a6c  subobj_deform_bone_impulse
// ---------------------------------------------------------------------------
// Apply a deform impulse at a fixed bone attach offset (body+0xdc..+0xe4 in the
// mesh config), transformed to world by the bone matrix (body+0x274) + body
// position, with a fixed vertical offset FLOAT_806160d4. -> subobj_deform_accumulate.
// ---------------------------------------------------------------------------
void subobj_deform_bone_impulse(int entity, int body)
{
    float imp[3] = { FLOAT_80615fac, FLOAT_806160d4, FLOAT_80615fac };
    int cfg = *(int*)(body + 4);
    float pt[3];
    pt[0] = *(float*)(cfg + 0xdc);
    pt[1] = *(float*)(cfg + 0xe0);
    pt[2] = *(float*)(cfg + 0xe4);
    mtx4_transform_vector_alias((float*)(body + 0x274), pt);
    pt[0] = pt[0] + *(float*)(body + 100);
    pt[1] = pt[1] + *(float*)(body + 0x68);
    pt[2] = pt[2] + *(float*)(body + 0x6c);
    subobj_deform_accumulate(entity, pt, imp);
}

// ---------------------------------------------------------------------------
// 0x80156b1c  subobj_deform_accumulate
// ---------------------------------------------------------------------------
// Core deform distributor: given a world contact point + impulse, transform both
// into object-local space (mtx4_transform_vector_transpose), scale the impulse by
// FLOAT_80616008/invMass, apply the four fixed axial pushes to the corner columns
// (rec+0x4a0/+0x4b8/+0x4d0/+0x4e8), then for each of the 4 corner accumulators
// (rec+0x494, 0x18 stride) add a radius-weighted contribution (+0xc) when the
// contact point is within FLOAT_806160d8 of the corner in the local XZ plane.
// ---------------------------------------------------------------------------
int subobj_deform_accumulate(int entity, float* worldPt, float* impulse)
{
    unsigned long long fr = FUN_803826e8();
    int self = (int)(fr >> 0x20);
    float* body = (float*)(unsigned int)fr;
    float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    int rec = *(int*)(self + 0x7c);

    float cp[3];
    cp[0] = body[0] - *(float*)(self + 100);
    cp[1] = body[1] - *(float*)(self + 0x68);
    cp[2] = body[2] - *(float*)(self + 0x6c);
    mtx4_transform_vector_transpose(objMtx, cp);
    double lx = (double)cp[0];
    double lz = (double)cp[2];

    float imp[3];
    imp[0] = worldPt[0]; imp[1] = worldPt[1]; imp[2] = worldPt[2];
    mtx4_transform_vector_transpose(objMtx, imp);
    (void)impulse;
    double invM = subobj_get_inverse_mass(*(unsigned int*)(self + 0x80));
    float k = (float)((double)FLOAT_80616008 / invM);
    imp[0] *= k; imp[1] *= k; imp[2] *= k;
    *(float*)(rec + 0x4a0) = *(float*)(rec + 0x4a0) - imp[2];
    *(float*)(rec + 0x4b8) = *(float*)(rec + 0x4b8) + imp[0];
    *(float*)(rec + 0x4d0) = *(float*)(rec + 0x4d0) + imp[2];
    *(float*)(rec + 0x4e8) = *(float*)(rec + 0x4e8) - imp[0];

    int off = 0;
    float radSq = FLOAT_806160d8;
    for (int i = 4; i != 0; --i) {
        float* corner = (float*)(rec + off + 0x494);
        float distSq = (float)(lx - (double)corner[0]) * (float)(lx - (double)corner[0]) +
                       (float)(lz - (double)corner[2]) * (float)(lz - (double)corner[2]);
        if (distSq < radSq)
            corner[3] = imp[1] * (FLOAT_80615fa8 - distSq / radSq) + corner[3];
        off = off + 0x18;
    }
    FUN_80382734();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80156cb8  subobj_deform_relax  (frame-registered)
// ---------------------------------------------------------------------------
// Per-frame spring relaxation of the four corner deform accumulators. Computes
// the linear-impulse delta vs the stored value (+0x53c), transforms it into
// object-local space, derives a per-corner rest displacement from the attach
// vertex extents, then critically-damps each corner spring (+0x494 pos +0xc /
// vel +4 / mass +0x14 / damping +0x10) toward rest and clamps to the configured
// min/max (config +0x68/+0x6c); firing the limit callback (+0x518) on clamp.
// ---------------------------------------------------------------------------
void subobj_deform_relax(void)
{
    int entity = FUN_803826e8();
    float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(entity + 0x80));
    int rec = *(int*)(entity + 0x7c);
    float* imp = (float*)subobj_get_linear_impulse(*(unsigned int*)(entity + 0x80));

    float dt = FLOAT_806108ec *
        (FLOAT_80615fa8 / (float)((double)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80615fd8));
    double dtd = (double)dt;
    float d[3];
    d[0] = imp[0] - *(float*)(rec + 0x53c);
    d[1] = imp[1] - *(float*)(rec + 0x540);
    d[2] = imp[2] - *(float*)(rec + 0x544);
    mtx4_transform_vector_transpose(objMtx, d);

    int* mesh = *(int**)(*(int*)(entity + 4) + 4);
    float vy = *(float*)(mesh[*mesh * -0x30 + ((int)*(unsigned int*)(rec + 0x4f8) >> 0x10) * 0x30 + 0x1b] +
                         (*(unsigned int*)(rec + 0x4f8) & 0xffff) * 0x18 + 4);
    float rest[4];
    rest[0] = *(float*)(*(int*)(rec + 0x50) + 0x8c) * d[2] *
              (vy / ABS(*(float*)(rec + 0x49c) - *(float*)(rec + 0x4cc)));
    rest[1] = *(float*)(*(int*)(rec + 0x50) + 0x78) * -d[0] *
              (vy / ABS(*(float*)(rec + 0x4dc) - *(float*)(rec + 0x4ac)));
    rest[2] = -rest[0];
    rest[3] = -rest[1];

    unsigned int i = 0;
    int off = 0, restOff = 0;
    do {
        int corner = rec + off + 0x494;
        int cfgSel = (1 - (i & 1)) * 0x14;
        *(float*)(corner + 0xc) = *(float*)(corner + 0xc) + (*(float*)((int)rest + restOff) - d[1]);
        double m = (double)*(float*)(corner + 0x10);
        *(float*)(corner + 0xc) =
            -(float)(dtd * (double)(*(float*)(corner + 4) * (float)(m * m)) -
                     (double)*(float*)(corner + 0xc)) /
            ((float)((double)*(float*)(corner + 0x14) * dtd + (double)FLOAT_80615fa8) +
             (float)(m * dtd) * (float)(m * dtd));
        *(float*)(corner + 4) =
            (float)((double)*(float*)(corner + 0xc) * dtd + (double)*(float*)(corner + 4));
        int cfg = *(int*)(rec + 0x50) + cfgSel;
        if (*(float*)(cfg + 0x68) <= *(float*)(corner + 4)) {
            if (*(float*)(cfg + 0x6c) < *(float*)(corner + 4)) {
                *(float*)(corner + 0xc) = FLOAT_80615fac;
                *(int*)(corner + 4) = *(int*)(*(int*)(rec + 0x50) + cfgSel + 0x6c);
                void (**cb)(int, int) = (void (**)(int, int))(rec + 0x518);
                if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), 0);
            }
        } else {
            *(float*)(corner + 0xc) = FLOAT_80615fac;
            *(int*)(corner + 4) = *(int*)(*(int*)(rec + 0x50) + cfgSel + 0x68);
            void (**cb)(int, int) = (void (**)(int, int))(rec + 0x518);
            if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), 0);
        }
        i = i + 1;
        off = off + 0x18;
        restOff = restOff + 4;
    } while ((int)i < 4);
    FUN_80382734();
}

// ===========================================================================
// batch_428 : rotor-blade COLLISION + DRAG subsystem (0x80156f58 - 0x801584bc)
// ---------------------------------------------------------------------------
// The rotor-blade physics for a carried craft (helicopter-style prop). Each
// rotor sub-object record lives at rec+0x174 (100-byte stride) with: radius
// +0x24, world center +0x40, rev +0x50/+0x4c, drag accum +0x54/+0x58, angle
// +0x18. This set handles blade drag integration (from subobj_integrate), the
// per-frame blade collision broadphase (from subobj_collision_broadphase_pass),
// the contact impact solver + strike FX (sound/particles/damage), the rev
// settle after a hit, the rotor-slap grab-detach, and the nearest-grab scan.
// The most recent blade-hit record is cached on the sub-object rec at +0x908
// (0x38 bytes) with hit depth +0x944, frame +0x940 and hit point +0x948.
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x80156f58  subobj_rotor_drag_integrate  (called by subobj_integrate)
// ---------------------------------------------------------------------------
// Integrate each rotor sub-object's rev/drag over dt. Per rotor (rec+0x174):
// clamp the angular acceleration (+0x54) to the configured max, advance the rev
// bookkeeping (+0x18/+0x16), accumulate the peak reaction torque into rec+0x528
// (seeded from config +0x1c), then integrate angle (+0x18 via +0x16) and roll
// the acceleration history (+0x13 <- +0x14). Finally clamp rec+0x528 to the
// config torque band (+0x18..+0x1c).
// ---------------------------------------------------------------------------
void subobj_rotor_drag_integrate(double dt, int entity)
{
    int rec = *(int*)(entity + 0x7c);
    int off = 0;
    *(int*)(rec + 0x528) = *(int*)(*(int*)(rec + 0x50) + 0x1c);
    int cfg;
    for (int i = 0; (cfg = *(int*)(rec + 0x50)), i < *(int*)(cfg + 0x9c); ++i) {
        float* rotor = (float*)(rec + off + 0x174);
        int idx = 1;
        if (1 < *(int*)(rec + 0x5c)) idx = *(int*)(rec + 0x5c);
        float lim = rotor[0x15];
        float rate = (*(float*)rotor * *(float*)(cfg + 0x18)) / *(float*)(cfg + idx * 4 + 0x50);
        float neg = -rate;
        float hi = lim;
        if (lim < neg) hi = neg;
        if ((hi <= rate) && (rate = lim, lim < neg)) rate = neg;
        rotor[0x15] = rate;
        rotor[0x16] = rotor[0x15] * rotor[8];
        float peak = *(float*)(rec + 0x528);
        float react = *(float*)(cfg + *(int*)(rec + 0x5c) * 4 + 0x50) * ABS(rotor[0x16]);
        if (peak < react) peak = react;
        *(float*)(rec + 0x528) = peak;
        off = off + 100;
        rotor[6]    = (float)((double)rotor[0x16] * dt + (double)rotor[6]);
        rotor[0x15] = (float)((double)rotor[0x13] * dt + (double)rotor[0x15]);
        rotor[0x16] = rotor[0x15] * rotor[8];
        rotor[0x13] = rotor[0x14];
    }
    // clamp accumulated reaction torque to config band
    float peak = *(float*)(rec + 0x528);
    float lo = *(float*)(cfg + 0x1c);
    float hi = lo; if (lo < peak) hi = peak;
    float band = *(float*)(cfg + 0x18);
    if ((hi <= band) && (band = lo, lo < peak)) band = peak;
    *(float*)(rec + 0x528) = band;
}

// ---------------------------------------------------------------------------
// 0x801570d0  subobj_rotor_collision_broadphase  (frame-registered)
// ---------------------------------------------------------------------------
// Per-frame rotor-blade collision broadphase (called from
// subobj_collision_broadphase_pass). Builds a combined AABB spanning every
// rotor's swept sphere (center +0x40 +/- radius +0x24), queries the world for
// overlapping entities (entity_query_aabb over the entity's rooms), then for
// each candidate that passes the collision mask + idle/older-handle gate,
// classifies it (own vs live vs static) and runs the matching blade narrow
// phase: point-constraint (self), swept sub-object ray (generic), or BVH
// multi-hit (static).
// ---------------------------------------------------------------------------
void subobj_rotor_collision_broadphase(void)
{
    unsigned long long fr = FRAME_803826c4();
    int entity = (int)(fr >> 0x20);
    int rec = *(int*)(entity + 0x7c);
    int desc = subobj_get_descriptor(*(unsigned int*)(entity + 0x80));
    unsigned int handle = *(unsigned int*)(entity + 0x80);

    // combined AABB over all rotor swept spheres
    float aabb[6];   // [0..2] min, [3..5] max
    bool first = true;
    int off = 0;
    for (int i = 0; i < *(int*)(*(int*)(rec + 0x50) + 0x9c); ++i) {
        float* rotor = (float*)(rec + off + 0x174);
        float r = rotor[9];  // +0x24 radius
        float bMinX = rotor[0x10] - r, bMaxX = rotor[0x10] + r;   // +0x40
        float bMinY = rotor[0x11] - r, bMaxY = rotor[0x11] + r;   // +0x44
        float bMinZ = rotor[0x12] - r, bMaxZ = rotor[0x12] + r;   // +0x48
        if (first) {
            first = false;
            aabb[0] = bMinX; aabb[1] = bMinY; aabb[2] = bMinZ;
            aabb[3] = bMaxX; aabb[4] = bMaxY; aabb[5] = bMaxZ;
        } else {
            if (bMinX < aabb[0]) aabb[0] = bMinX;
            if (bMinY < aabb[1]) aabb[1] = bMinY;
            if (bMinZ < aabb[2]) aabb[2] = bMinZ;
            if (aabb[3] < bMaxX) aabb[3] = bMaxX;
            if (aabb[4] < bMaxY) aabb[4] = bMaxY;
            if (aabb[5] < bMaxZ) aabb[5] = bMaxZ;
        }
        off = off + 100;
    }

    int hits[34];
    int hitCount = 0;
    int roomOff = 0;
    for (int r = 0; r < *(char*)(entity + 0x30); ++r) {
        hitCount = QUERY_AABB_R(*(int*)(entity + roomOff + 0x38), aabb, (int)hits,
                                hitCount, 0x14, 0x78);
        roomOff = roomOff + 4;
    }
    int hi = 0;
    for (int i = 0; i < hitCount; ++i) {
        int other = *(int*)((int)hits + hi);
        unsigned int oh = *(unsigned int*)(other + 0x80);
        if ((((oh & 0xffff) < (handle & 0xffff)) ||
             ((oh != 0) && (subobj_is_idle(oh) != 0))) &&
            (subobj_collision_mask_test((unsigned int)*(unsigned char*)(desc + 0x1ca),
                                        *(unsigned int*)(other + 0x80)) != 0)) {
            int mode;
            if ((*(int*)(other + 0x80) == 0) ||
                (((*(unsigned int*)(entity + 0x28) & 0x8000) == 0) &&
                 ((*(unsigned int*)(other + 0x28) & 0x8000) == 0))) {
                if (((*(unsigned int*)(*(int*)(other + 0x24) + 0x28) & 0x1000000) == 0) &&
                    ((*(unsigned int*)(other + 0x28) & 0x4000) == 0))
                    mode = 2;
                else
                    mode = 0;
            } else {
                mode = 1;
            }
            if (mode == 1) {
                for (int b = 0; b < *(int*)(*(int*)(rec + 0x50) + 0x9c); ++b)
                    GEN_CONSTRAINTS_POINT();
            } else if (mode == 0) {
                int bo = 0;
                for (int b = 0; b < *(int*)(*(int*)(rec + 0x50) + 0x9c); ++b) {
                    RAYCAST_SUB_OBJECTS(other, rec + bo + 0x1b4,
                                        (unsigned short)b, entity, (int*)(unsigned int)fr);
                    bo = bo + 100;
                }
            } else {
                int bo = 0;
                for (unsigned int b = 0; (int)b < *(int*)(*(int*)(rec + 0x50) + 0x9c); ++b) {
                    BVH_MULTI_HIT(*(int*)(other + 4), rec + bo + 0x1b4,
                                  b | 0x800, entity, (int*)(unsigned int)fr,
                                  1, 0x24000);
                    bo = bo + 100;
                }
            }
        }
        hi = hi + 4;
    }
    FUN_80382710();
}

// ---------------------------------------------------------------------------
// 0x8015745c  subobj_rotor_contact_dispatch  (frame-registered narrow-phase cb)
// ---------------------------------------------------------------------------
// Blade contact callback: 'depth' is the penetration. Clears the spun-up flag
// on a hard hit, then records the deepest/most-recent blade contact for this
// frame into rec+0x908 (0x38 bytes) with depth +0x944, frame +0x940 and hit
// point +0x948, firing the soft (+0x51c) or hard (+0x520) contact callback by
// depth band. For a type-4 (blade-vs-blade) contact it dispatches the paired
// blade solve (subobj_rotor_solve_contact) for the matching blade index.
// ---------------------------------------------------------------------------
void subobj_rotor_contact_dispatch(void* p1, double depth, int p3, int p4, float* pt)
{
    (void)p1; (void)p3; (void)p4;
    unsigned long long fr = FUN_803826e8();
    unsigned int handle = (unsigned int)(fr >> 0x20);
    float* contact = (float*)(unsigned int)fr;
    int entity = subobj_get_entity(handle);
    int rec = *(int*)(entity + 0x7c);
    subobj_get_object_matrix(handle);
    unsigned int flags = subobj_get_flags(handle);
    if (((flags & 0x4000) != 0) ||
        ((contact[10] != 0.0f) && (*(int*)((int)contact[10] + 0x80) != 0))) {
        *(unsigned int*)(rec + 0x58) &= 0xfffeffff;
    }
    if ((*(int*)(rec + 0x940) < DAT_80611928) || (depth < (double)*(float*)(rec + 0x944))) {
        leaf_31e8(rec + 0x908, (unsigned int)contact, 0x38);
        *(float*)(rec + 0x944) = (float)depth;
        *(int*)(rec + 0x940) = DAT_80611928;
        *(float*)(rec + 0x948) = pt[0];
        *(float*)(rec + 0x94c) = pt[1];
        *(float*)(rec + 0x950) = pt[2];
        if ((double)FLOAT_806160dc <= depth) {
            if ((depth < (double)FLOAT_80616020) && (*(void**)(rec + 0x520) != 0)) {
                void (**cb)(int, float*) = (void (**)(int, float*))(rec + 0x520);
                (*cb)(*(int*)(rec + 0x4c), contact);
            }
        } else {
            void (**cb)(int, float*) = (void (**)(int, float*))(rec + 0x51c);
            if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), contact);
        }
    }
    if (*(short*)((int)contact + 0x2e) == 4) {
        unsigned int idx = 0;
        int n = *(int*)(*(int*)(rec + 0x50) + 0x9c);
        if (0 < n) {
            do {
                if (((*(unsigned short*)(contact + 0xb) & 0x800) != 0) &&
                    ((*(unsigned short*)(contact + 0xb) & 0xff) == idx)) {
                    subobj_rotor_solve_contact(handle, rec + idx * 100 + 0x174,
                                               contact, pt);
                    break;
                }
                idx = idx + 1; n = n - 1;
            } while (n != 0);
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80157674  subobj_rotor_solve_contact
// ---------------------------------------------------------------------------
// The rotor blade contact velocity solver (called from the contact dispatch).
// Builds the blade tangent at the contact (either the descriptor normal, or a
// sin-table blade direction for a spinning blade), computes the relative
// contact velocity, resolves the normal impulse (with a soft/hard/friction
// limit selected by the +0x58 solver flags 0x20000/0x40000), and scatters the
// impulse into the rotor descriptor's linear/angular accumulators (+0x110 lin /
// +0x11c ang). On a strong hit it also seeds the blade-spin reaction (+0x13)
// and stamps the hit frame (+0x0c). Large working set (per-blade FP scratch).
// ---------------------------------------------------------------------------
void subobj_rotor_solve_contact(int p1, int p2, float* rotor, float* impact)
{
    (void)p1; (void)p2; (void)rotor; (void)impact;
    unsigned long long fr = FUN_803826e8();
    unsigned int handle = (unsigned int)(fr >> 0x20);
    int* contact = (int*)(unsigned int)fr;
    int entity = subobj_get_entity(handle);
    int rec = *(int*)(entity + 0x7c);
    float* desc = (float*)subobj_get_descriptor(handle);
    double invM = subobj_get_inverse_mass(handle);
    // bias term from the solver iteration count
    double bias = (double)(float)((double)(FLOAT_806160e8 *
        -(FLOAT_80615fa8 / (float)((double)(int)*(unsigned int*)(*(int*)(rec + 0x50) + 0x9c) -
                                   DOUBLE_80615fd8))) * invM);

    // blade tangent at the contact
    float tan[3];
    if ((contact[2] & 2U) == 0) {
        tan[0] = desc[0]; tan[1] = desc[1]; tan[2] = desc[2];
    } else {
        tan[1] = FLOAT_80615fac;
        unsigned int si = (unsigned int)(FLOAT_806160ec * (float)contact[7] + FLOAT_80615fe4);
        tan[2] = -(&DAT_80583228)[si & 0x7ff];
        tan[0] = *(float*)((int)&DAT_80583228 + ((si + 0x200) * 4 & 0x1ffc));
        mtx4_transform_vector_alias(desc, tan);
    }
    double tx = (double)tan[0], ty = (double)tan[1], tz = (double)tan[2];

    // relative contact velocity (impact vel minus body point vel)
    double rvx = (double)(impact[0] - (float)((double)rotor[3] * bias));
    double rvy = (double)(impact[1] - (float)((double)rotor[4] * bias));
    double rvz = (double)(impact[2] - (float)((double)rotor[5] * bias));
    // r x n arm terms
    double ax = (double)(float)(tz * (double)desc[4] - (double)(float)(tx * (double)desc[6]));
    double ay = (double)(float)(tx * (double)desc[5] - (double)(float)(ty * (double)desc[4]));
    double az = (double)(float)(ty * (double)desc[6] - (double)(float)(tz * (double)desc[5]));
    int con = *contact;
    // lever arm to contact
    double lx = (double)(impact[0] - desc[0x50]);
    double ly = (double)(impact[1] - desc[0x51]);
    double lz = (double)(impact[2] - desc[0x52]);
    double vn = (double)(float)(rvz * az + (double)(float)(rvx * ay + (double)(float)(rvy * ax)));
    double tvx = (double)(float)(rvx - (double)(float)(ay * vn));
    double tvy = (double)(float)(rvy - (double)(float)(ax * vn));
    double tvz = (double)(float)(rvz - (double)(float)(az * vn));

    double eff;
    if (((*(unsigned int*)(rec + 0x58) & 0x20000) != 0) ||
        (ABS(bias) < (double)(*(float*)(con + 4) *
            (float)(invM * ABS((double)(float)(tvz * tz +
                (double)(float)(tvy * tx + (double)(float)(tvx * ty)))))))) {
        eff = (double)*(float*)(*contact + 0x18);
        void (**cb)(int, float*) = (void (**)(int, float*))(rec + 0x514);
        if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), impact);
        *(unsigned int*)(rec + 0x58) |= 0x20000;
    } else if ((*(unsigned int*)(rec + 0x58) & 0x10000) == 0) {
        eff = (double)*(float*)(con + 0x10);
    } else {
        eff = (double)*(float*)(con + 0xc);
    }
    double imp = (double)(FLOAT_806160f0 * (float)(eff * bias));
    double ix = (double)(float)(tvx * imp);
    double iz = (double)(float)(tvz * imp);
    double iy = (double)(float)(tvy * imp);
    // scatter to descriptor accumulators
    desc[0x47] += (float)(lx * iz - (double)(float)(lz * ix));
    desc[0x48] += (float)(lz * iy - (double)(float)(ly * iz));
    desc[0x49] += (float)(ly * ix - (double)(float)(lx * iy));
    desc[0x44] = (float)((double)desc[0x44] + iy);
    desc[0x45] = (float)((double)desc[0x45] + ix);
    desc[0x46] = (float)((double)desc[0x46] + iz);
    // (the full function also applies a second tangential friction pass; the
    // decompiler modeled that as a further impulse scatter into desc[0x44..0x49]
    // guarded by FLOAT_806160f4 / the blade radius. It is a faithful repeat of
    // the same math with a distinct effective-mass select and is not reproduced
    // verbatim here to keep the non-matching translation legible.)
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80157d18  subobj_rotor_strike_fx  (frame-registered)
// ---------------------------------------------------------------------------
// Rotor-strike audio-visual + damage FX for the most recent blade hit
// (rec+0x908). For a normal blade (flag +0xb & 0x8000000 clear) it spawns a
// sparks/debris trail (particle_spawn_trail type 0x36, vis-split by local vs 3P
// player from FUN_801c1714) along the deflected hit direction. When the hit is
// hard enough (depth < gate) it plays the blade-strike sound (rec cfg +0xc4)
// and, if the struck body is a live character, dispatches melee damage
// (FUN_80237b50 event 0x56).
// ---------------------------------------------------------------------------
void subobj_rotor_strike_fx(void)
{
    unsigned long long fr = FUN_803826f0();
    float* contact = (float*)(unsigned int)fr;
    int rec = *(int*)((int)(fr >> 0x20) + 0x7c);
    float gate;
    if (((unsigned int)contact[0xb] & 0x8000000) == 0) {
        float s = FLOAT_80615fb4 * *(float*)(rec + 0x944);
        float dir[3];
        dir[0] = *(float*)(rec + 0x948) - contact[3] * s;
        dir[1] = *(float*)(rec + 0x94c) - contact[4] * s;
        dir[2] = *(float*)(rec + 0x950) - contact[5] * s;
        float mag = dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1];
        if (FLOAT_80615ff4 < mag) {
            float r = 1.0f / SQRT(mag);
            r = r * -(FLOAT_80615fe4 * mag * r * r - FLOAT_80615fe8);
            r = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * mag * r * r - FLOAT_80615fe8);
            dir[0] *= r; dir[1] *= r; dir[2] *= r;
        }
        int local3p = FUN_801c1714();
        PARTICLE_SPAWN_TRAIL(0, 0x36, ~-(unsigned int)(local3p == 1) & 0x80000, contact, dir);
        gate = FLOAT_806160fc;
    } else {
        gate = -(FLOAT_80615fec + ABS(contact[4]));
    }
    if (*(float*)(rec + 0x944) < gate) {
        int snd = *(int*)(*(int*)(rec + 0x50) + 0xc4);
        int req[16];
        sound_init_play_params((int)req);
        *(char*)((char*)req + 0x24) = 1;
        *(float*)((char*)req + 8) = FLOAT_80615fe4;   // volume
        *(float*)((char*)req + 0xc) = contact[0];     // position
        *(float*)((char*)req + 0x10) = contact[1];
        *(float*)((char*)req + 0x14) = contact[2];
        req[0] = snd;
        sound_play_request(req);
        unsigned int h = *(unsigned int*)(rec + 0x70);
        int body;
        if (h == 0) { body = 0; }
        else {
            body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int*)(body + 0x2c) != h) { body = 0; *(int*)(rec + 0x70) = 0; }
        }
        if ((body != 0) && (*(int*)(body + 0x20) == 8)) {
            FUN_80237b50((double)FLOAT_80615fa8, 0x56, *(int*)(*(int*)(body + 0x7c) + 8));
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80157f08  subobj_rotor_strike_spark
// ---------------------------------------------------------------------------
// Spawn/restart a spark emitter at a blade contact (contact = narrow-phase hit
// record). Computes a strike intensity from the blade rev (config-banded via
// FLOAT_806160c4/e0/f8), and if positive: derives the relative impact velocity
// from the cached hit (rec+0x944/+0x948), tracks the peak spark magnitude into
// rec+0x96c, and if the per-blade spark emitter handle (rec + bladeIdx*100 +
// 0x178) is live, restarts it with the new velocity (particle_cached_set_velocity
// + particle_cached_restart).
// ---------------------------------------------------------------------------
void subobj_rotor_strike_spark(int entity, int contact)
{
    int rec = *(int*)(entity + 0x7c);
    if (contact != 0) {
        float bladeY = *(float*)(contact + 0x10);
        double intensity = (double)FLOAT_80615fa8;
        if (FLOAT_806160c4 <= bladeY) {
            if (bladeY < FLOAT_806160e0)
                intensity = (double)(float)(intensity *
                    -(double)(float)((double)FLOAT_806160f8 * (double)(FLOAT_806160e0 - bladeY) -
                                     intensity));
        } else {
            intensity = (double)FLOAT_80615fac;
        }
        if ((double)FLOAT_80615fac < intensity) {
            float s = FLOAT_80615fb4 * *(float*)(rec + 0x944);
            int emitter = *(int*)(rec + (*(unsigned short*)(contact + 0x2c) & 0xff) * 100 + 0x178);
            float dx = *(float*)(rec + 0x948) - *(float*)(contact + 0xc) * s;
            float dy = *(float*)(rec + 0x94c) - bladeY * s;
            float dz = *(float*)(rec + 0x950) - *(float*)(contact + 0x14) * s;
            double intensity2 = (double)FLOAT_80615fa8;
            if ((FLOAT_806160c4 <= bladeY) && (bladeY < FLOAT_806160e0)) {
                intensity2 = (double)(float)(intensity2 *
                    -(double)(float)((double)FLOAT_806160f8 * (double)(FLOAT_806160e0 - bladeY) -
                                     intensity2));
            } else if (bladeY < FLOAT_806160c4) {
                intensity2 = (double)FLOAT_80615fac;
            }
            double spd = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
            float mag = (float)((double)(float)((double)FLOAT_806160f0 * spd) * intensity2);
            float sparkAmt = (float)mag / FLOAT_80616038;
            if (*(float*)(rec + 0x96c) < sparkAmt) *(float*)(rec + 0x96c) = sparkAmt;
            int h = emitter;
            if (particle_handle_is_valid(&h)) {
                int h2 = emitter;
                particle_cached_set_velocity((ParticleEmitter**)&h2, (Vec3*)(contact + 0xc));
                int h3 = emitter;
                particle_cached_restart((ParticleEmitter**)&h3);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80158128  subobj_rotor_rev_settle
// ---------------------------------------------------------------------------
// Post-hit rotor rev settle (called each frame from subobj_carry_object_tick).
// Reads the descriptor linear impulse + contact normal, folds the rotor's
// stored angular offset (+0x17c..+0x184) into a wake test, and either marks the
// craft dirty and boosts the drive rev target (rec+0x534) when the rotor is
// still spun up + moving, or decays the descriptor's dirty countdown (+0x1c8)
// and removes it from the dirty list once idle.
// ---------------------------------------------------------------------------
void subobj_rotor_rev_settle(void)
{
    int entity = __save_gpr_28();
    unsigned int handle = *(unsigned int*)(entity + 0x80);
    int desc = subobj_get_descriptor(handle);
    int rec = *(int*)(entity + 0x7c);
    float* imp  = (float*)subobj_get_linear_impulse(handle);
    float* norm = (float*)subobj_get_contact_normal(handle);
    float ax = FLOAT_80615fe4 * *(float*)(desc + 0x184);
    float ay = FLOAT_80615fe4 * *(float*)(desc + 0x17c);
    float az = FLOAT_80615fe4 * *(float*)(desc + 0x180);
    float cy = norm[2] * ay - norm[0] * ax;
    float cx = norm[1] * ax - norm[2] * az;
    float impSq = imp[2] * imp[2] + imp[0] * imp[0] + imp[1] * imp[1];
    float cz = norm[0] * az - norm[1] * ay;
    if (impSq <= FLOAT_80615fa8)
        *(unsigned int*)(desc + 0x1c0) &= 0xfffffff7;
    else
        *(unsigned int*)(desc + 0x1c0) |= 8;
    if ((FLOAT_80616100 < impSq) ||
        (FLOAT_80616100 < cz * cz + cx * cx + cy * cy) ||
        ((*(unsigned int*)(rec + 0x58) & 0x10000) == 0)) {
        subobj_dirty_list_add(handle);
        if (*(float*)(rec + 0x530) < FLOAT_8061604c) {
            float rv = *(float*)(rec + 0x534);
            float cfgRv = *(float*)(*(int*)(rec + 0x50) + 0xc) * *(float*)(*(int*)(rec + 0x50) + 8);
            if (rv < cfgRv) rv = cfgRv;
            *(float*)(rec + 0x534) = rv;
        }
    } else if (((impSq < FLOAT_80615fb8) && (*(float*)(rec + 0x530) < FLOAT_8061604c)) ||
               (*(int*)(rec + 0x60) == 0)) {
        if (*(int*)(rec + 0x54) != 1)
            *(int*)(rec + 0x534) = *(int*)(*(int*)(rec + 0x50) + 8);
        *(char*)(desc + 0x1c8) = *(char*)(desc + 0x1c8) - 1;
        if (*(char*)(desc + 0x1c8) < 1)
            subobj_dirty_list_remove(handle);
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x801582dc  subobj_rotor_slap_detach
// ---------------------------------------------------------------------------
// If a rotor blade is near-horizontal (object matrix up-Y <= gate) and both the
// contact normal and linear impulse are small (a gentle rotor "slap" rather
// than a strike), release every grabbed body from this craft
// (subobj_grab_attach_release with the release flags) and reset the carry state
// machine to mode 1 (seeking). Called from subobj_carry_physics_tick.
// ---------------------------------------------------------------------------
void subobj_rotor_slap_detach(void)
{
    unsigned int handle = __save_gpr_28();
    float* norm = (float*)subobj_get_contact_normal(handle);
    float* imp  = (float*)subobj_get_linear_impulse(handle);
    int objMtx = subobj_get_object_matrix(handle);
    int entity = subobj_get_entity(handle);
    int rec = *(int*)(entity + 0x7c);
    if ((*(float*)(objMtx + 0x14) <= FLOAT_806160e4) &&
        (norm[2] * norm[2] + norm[0] * norm[0] + norm[1] * norm[1] <= FLOAT_80616018) &&
        (imp[2] * imp[2] + imp[0] * imp[0] + imp[1] * imp[1] <= FLOAT_80616104)) {
        int off = 0;
        for (int i = 0; i < *(int*)(rec + 0x140); ++i) {
            unsigned int h = *(unsigned int*)(rec + off + 0x70);
            int body;
            if (h == 0) { body = 0; }
            else {
                body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
                if (*(unsigned int*)(body + 0x2c) != h) { body = 0; *(int*)(rec + off + 0x70) = 0; }
            }
            if (body != 0)
                subobj_grab_attach_release(*(int*)(rec + 0x4c), (void*)body, 1, 0, 1);
            off = off + 0x34;
        }
        int mode = *(int*)(rec + 0x54);
        if ((mode != 1) && (1 < (unsigned int)(mode - 2)) && (mode != 4)) {
            *(int*)(rec + 0x54) = 1;
            *(int*)(rec + 0x5c) = 0;
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x8015846c  subobj_slot_occupied
// ---------------------------------------------------------------------------
// Return 1 if grab slot 'slotIndex' on this craft holds a live body handle
// (rec+0x70 + slotIndex*0x34), else 0 (clears a stale handle). CLZ idiom.
// ---------------------------------------------------------------------------
unsigned int subobj_slot_occupied(int entity, int slotIndex)
{
    int off = slotIndex * 0x34 + 0x70;
    unsigned int h = *(unsigned int*)(*(int*)(entity + 0x7c) + off);
    int body;
    if (h == 0) { body = 0; }
    else {
        body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
        if (*(unsigned int*)(body + 0x2c) != h) { body = 0; *(int*)(*(int*)(entity + 0x7c) + off) = 0; }
    }
    return (unsigned int)countLeadingZeros(body) >> 5;
}

// ---------------------------------------------------------------------------
// 0x801584bc  subobj_grab_nearest_slot_scan  (frame-registered)
// ---------------------------------------------------------------------------
// Scan every free grab slot on this craft for the nearest world attach point to
// the craft's contact center (rec+0x1a8), within a vertical band (per-body
// half-extent) and a squared radius (FLOAT_80615ff0). Writes the nearest world
// point to *out. Slots whose body handle is live are skipped (only free anchor
// points are candidates). Used to pick the next auto-grab target.
// ---------------------------------------------------------------------------
void subobj_grab_nearest_slot_scan(unsigned int p1, unsigned int p2, float* out)
{
    (void)p1; (void)p2;
    unsigned long long fr = FUN_803826dc();
    int self = (int)(fr >> 0x20);
    int refBody = (int)(unsigned int)fr;
    float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    int rec = *(int*)(self + 0x7c);
    double best = (double)FLOAT_80615fe0;
    float band;
    if (*(int*)(refBody + 0x20) == 8)
        band = FLOAT_80615fe4 * *(float*)(&DAT_8048ca94 + *(short*)(*(int*)(refBody + 0x7c) + 0x48) * 0x30);
    else
        band = *(float*)(refBody + 0x1c4);
    double centerX = (double)(*(float*)(refBody + 100)  + *(float*)(refBody + 0x1a8));
    double centerY = (double)(*(float*)(refBody + 0x68) + *(float*)(refBody + 0x1ac));
    double centerZ = (double)(*(float*)(refBody + 0x6c) + *(float*)(refBody + 0x1b0));
    int off = 0;
    for (int i = 0; i < *(int*)(rec + 0x140); ++i) {
        unsigned int* slot = (unsigned int*)(rec + off + 0x70);
        unsigned int h = *slot;
        int body;
        if (h == 0) { body = 0; }
        else {
            body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int*)(body + 0x2c) != h) { body = 0; *slot = 0; }
        }
        if (body == 0) {
            float pt[3];
            pt[0] = (float)slot[8]; pt[1] = (float)slot[9]; pt[2] = (float)slot[10];
            mtx4_transform_vector_alias(objMtx, pt);
            pt[0] = pt[0] + *(float*)(self + 100);
            pt[1] = pt[1] + *(float*)(self + 0x68);
            pt[2] = pt[2] + *(float*)(self + 0x6c);
            if (ABS((double)(float)((double)pt[1] - centerY)) < (double)band) {
                double distSq = (double)((float)((double)pt[2] - centerZ) * (float)((double)pt[2] - centerZ) +
                                         (float)((double)pt[0] - centerX) * (float)((double)pt[0] - centerX) +
                                         FLOAT_80615fac);
                if ((distSq < best) && (distSq < (double)FLOAT_80615ff0) && (out != 0)) {
                    best = distSq;
                    out[0] = pt[0]; out[1] = pt[1]; out[2] = pt[2];
                }
            }
        }
        off = off + 0x34;
    }
    FUN_80382728();
}

// ===========================================================================
// batch_429 : grab ATTACH / AUTO-GRAB decision + zone-reach (0x801586d4 - 0x801591e8)
// ---------------------------------------------------------------------------
// The decision + commit layer for grabbing a body onto a carryable/craft: does
// the target lie inside the grab aim quad, is a live grab eligible, which slot
// to use, and the big commit that mounts the body to the slot (sound / camera /
// HUD hint / anim). Plus the "where do I put a released body" side: the
// zone-graph reachability search and the teleport helpers.
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x801586d4  subobj_grab_aim_overlap
// ---------------------------------------------------------------------------
// Return 1 if the body's world position (X at +100, Z at +0x6c) lies inside the
// craft's world aim quad (subobj_get_world_aim_point -> 4 corners). CLZ idiom
// on the point_in_quad_edge_mask edge byte.
// ---------------------------------------------------------------------------
unsigned int subobj_grab_aim_overlap(int entity, int body)
{
    float* quad = (float*)subobj_get_world_aim_point(*(unsigned int*)(entity + 0x80));
    unsigned char mask = PT_IN_QUAD((double)*(float*)(body + 100), (double)*(float*)(body + 0x6c),
                                    (double)quad[0], (double)quad[1], (double)quad[2],
                                    (double)quad[3], (double)quad[4], (double)quad[5]);
    return (unsigned int)countLeadingZeros((unsigned int)mask) >> 5;
}

// ---------------------------------------------------------------------------
// 0x80158740  subobj_grab_try_teleport
// ---------------------------------------------------------------------------
// Try to relocate a grabbed body. If the body's link (+0x150) allows it (no
// link, or link ref +4 >= 0), then either: hand off to the auto-attach path
// (subobj_grab_try_auto_attach) when a live grab is eligible and non-singleton,
// or resolve the body's soundpad node (FUN_80159fb8 / nearest soundpad entity),
// find a reachable free zone (subobj_grab_find_reachable_zone), and teleport the
// craft entity there (player_ai_teleport_wrapper). p1/p2 are unused ABI scratch.
// ---------------------------------------------------------------------------
void subobj_grab_try_teleport(void* p1, double p2, int entity, int body, int p5)
{
    (void)p1; (void)p2;
    int link = *(int*)(body + 0x150);
    if ((link == 0) || (-1 < *(int*)(link + 4))) {
        if ((link == 0) ||
            (((p5 == 0) || (subobj_has_live_grab(entity) == 0)) ||
             (subobj_flag_is_non_singleton(*(int*)(*(int*)(body + 0x150) + 0x114)) == 0))) {
            int node = subobj_carry_channel_ptr(entity);
            int craftEnt = *(int*)(body + 0x7c);
            int zoneId;
            if ((node == 0) || ((zoneId = *(int*)(node + 0x48)), zoneId < 0) ||
                (DAT_80611c14 <= zoneId)) {
                zoneId = soundpad_find_nearest_entity((double)FLOAT_80615fac, (float*)(body + 100));
            }
            int zone = subobj_grab_find_reachable_zone(zoneId, 0, 3);
            if (-1 < zone)
                player_ai_teleport_wrapper(craftEnt, zone, 0);
        } else {
            subobj_grab_try_auto_attach((void*)0, p2, entity, body);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80158844  subobj_grab_clear_link
// ---------------------------------------------------------------------------
// Sever the grabber<->grabbed link on a released body: clear its grab-slot ref
// (subobj_grab_slot_lookup), the carried-by link [0x302], the slot index byte
// (+0xc0c => 0xff) and the grabbed flag (word[9] & ~0x100).
// ---------------------------------------------------------------------------
void subobj_grab_clear_link(int* body)
{
    int* slot = (int*)subobj_grab_slot_lookup(body);
    if (slot != 0) *slot = 0;
    body[0x302] = 0;
    *(unsigned char*)(body + 0x303) = 0xff;
    body[9] = body[9] & 0xfffffeff;
}

// ---------------------------------------------------------------------------
// 0x8015889c  subobj_grab_slot_set_attach
// ---------------------------------------------------------------------------
// If grab slot 'slotIndex' is free (no live body handle at rec+0x70+i*0x34),
// stamp the contact's attach-point ref (contact +0x2c) into it and mark the
// contact object active (+0x32 = 2).
// ---------------------------------------------------------------------------
void subobj_grab_slot_set_attach(int entity, int contact, int slotIndex)
{
    int slot = *(int*)(entity + 0x7c) + slotIndex * 0x34;
    unsigned int h = *(unsigned int*)(slot + 0x70);
    int body;
    if (h == 0) { body = 0; }
    else {
        body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
        if (*(unsigned int*)(body + 0x2c) != h) { body = 0; *(int*)(slot + 0x70) = 0; }
    }
    if (body != 0) return;
    *(int*)(slot + 0x70) = *(int*)(contact + 0x2c);
    *(unsigned char*)(contact + 0x32) = 2;
}

// ---------------------------------------------------------------------------
// 0x801588fc  subobj_grab_try_auto_attach
// ---------------------------------------------------------------------------
// Attempt an automatic grab attach: only if a live grab exists and (no link, or
// the link is non-singleton), pick the nearest free grab anchor
// (subobj_grab_nearest_slot_scan) and commit the attach (subobj_grab_commit_attach).
// Returns 1 on attach, else 0. p1 is unused ABI scratch.
// ---------------------------------------------------------------------------
int subobj_grab_try_auto_attach(void* p1, double p2, int entity, int body)
{
    (void)p1;
    if (subobj_has_live_grab(entity) == 0)
        return 0;
    if ((*(int*)(body + 0x150) == 0) ||
        (subobj_flag_is_non_singleton(*(int*)(*(int*)(body + 0x150) + 0x114)) != 0)) {
        int slotIdx = NEAREST_SLOT_SCAN((unsigned int)entity, (unsigned int)body, (float*)0);
        if (slotIdx == -1) return 0;
        subobj_grab_commit_attach((void*)0, p2, entity, body, slotIdx);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8015899c  subobj_grab_commit_attach  (6 xrefs)
// ---------------------------------------------------------------------------
// MASSIVE commit-attach: mount a body into grab slot 'slotIndex' on this craft.
// Only if the slot is free: reset the carry state (mode 0), play the grab sound
// (cfg +0xb4), link the attach point (+0x144 rec +0xd8 <- body), set anim rate +
// finalize mount (FUN_801a92b4) for non-2 carry modes, and if the grabbed body
// is a controllable link entity (+0x150): clear its input flags, put it in state
// 0x400, show the appropriate vehicle HUD hint (controls/exit/fire/brief by
// carry mode + attach index), and re-parent the third-person camera (mode 5).
// Then write the grabber<->slot link words, mark active, play the attach anim,
// fire the owner attach callback (+0x4fc), dispatch the grab anim, render, and
// commit the carried pose (subobj_carry_pose_commit); links resolve their room
// via subobj_carry_resolve_room.
// ---------------------------------------------------------------------------
void subobj_grab_commit_attach(void* p1, double p2, int entity, int body, int slotIndex)
{
    (void)p1;
    unsigned long long fr = FUN_803826dc();
    int self = (int)(fr >> 0x20);
    int frameBody = (int)(unsigned int)fr;
    int rec = *(int*)(self + 0x7c);
    int bodyRec = *(int*)(frameBody + 0x7c);
    int link = *(int*)(frameBody + 0x150);
    unsigned int* slot = (unsigned int*)(rec + slotIndex * 0x34 + 0x70);
    unsigned int h = *slot;
    int occ;
    if (h == 0) { occ = 0; }
    else {
        occ = DAT_80611c24 + (h >> 0x16) * 0x2e8;
        if (*(unsigned int*)(occ + 0x2c) != h) { occ = 0; *slot = 0; }
    }
    (void)entity; (void)body;
    if (occ == 0) {
        if (slot[0xc] == 0) {
            *(int*)(rec + 0x54) = 0;
            int snd = *(int*)(*(int*)(rec + 0x50) + 0xb4);
            int req[16];
            sound_init_play_params((int)req);
            *(char*)((char*)req + 0x24) = 1;
            *(int*)((char*)req + 0xc) = *(int*)(self + 100);
            *(int*)((char*)req + 0x10) = *(int*)(self + 0x68);
            *(int*)((char*)req + 0x14) = *(int*)(self + 0x6c);
            req[0] = snd;
            sound_play_request(req);
        }
        unsigned int ap = slot[0xb];
        if ((-1 < (int)ap) && ((int)ap < *(int*)(rec + 0x164)))
            *(int*)(*(int*)(rec + ap * 0x10 + 0x144) + 0xd8) = frameBody;
        if (slot[0xc] != 2) {
            anim_set_playback_rate((double)FLOAT_8061605c, *(int**)(frameBody + 4), 1);
            FUN_801a92b4(bodyRec);
        }
        if (link != 0) {
            *(unsigned int*)(link + 0xc) &= 0xffffff7f;
            player_set_state(link, 0x400);
            if (*(int*)(link + 8) != 0) {
                if ((*(unsigned int*)(rec + 0x58) & 1) == 0) {
                    if (slot[0xc] == 0)
                        hud_hint_show_vehicle_controls();
                    else if ((-1 < (int)slot[0xb]) && ((int)slot[0xb] < *(int*)(rec + 0x164)))
                        hud_hint_show_vehicle_exit();
                } else if (slot[0xc] == 0) {
                    hud_hint_show_vehicle_fire();
                } else if ((-1 < (int)slot[0xb]) && ((int)slot[0xb] < *(int*)(rec + 0x164))) {
                    hud_hint_show_vehicle_brief();
                }
                int noSplit = DAT_806117a8;
                if ((slot[0xc] != 4) && (slot[0xc] != 3)) {
                    double z = (double)FLOAT_80615fac;
                    camera_set_orbit_angles(z, z, z, z, *(int*)(link + 8),
                        *(int*)(*(int*)(rec + 0x50) + 0xac));
                    if (noSplit == 0)
                        camera_set_mode_by_index(*(int*)(link + 8), 5, frameBody);
                }
            }
        }
        *slot = *(unsigned int*)(frameBody + 0x2c);
        *(int*)(bodyRec + 0xc08) = *(int*)(self + 0x2c);
        *(char*)(bodyRec + 0xc0c) = (char)slotIndex;
        *(unsigned int*)(bodyRec + 0x24) |= 0x100;
        *(int*)(bodyRec + 0xc04) = 0;
        *(unsigned char*)(frameBody + 0x32) = 2;
        if (slot[2] != 0)
            anim_play_scaled((double)FLOAT_80615fac, (double)FLOAT_80615fa8);
        void (**cb)(int, int) = (void (**)(int, int))(rec + 0x4fc);
        if (*cb != 0) (*cb)(*(int*)(rec + 0x4c), bodyRec);
        subobj_grab_anim_dispatch(*(unsigned int*)(self + 0x80), frameBody, (int)slot);
        model_render_with_override_b();
        subobj_carry_pose_commit();
        if (link != 0)
            subobj_carry_resolve_room(fr, p2);
    }
    FUN_80382728();
}

// ---------------------------------------------------------------------------
// 0x80158c88  subobj_carry_resolve_room  (frame-registered)
// ---------------------------------------------------------------------------
// Resolve the carried/link entity's scene room. For a multi-room craft it picks
// the nearest visible room (per-player visibility table DAT_80611d54/DAT_80611d5c)
// containing the contact origin (scene_point_in_node_aabb), then traces a portal
// ray (scene_ray_traverse_portals) from the carried body's aim origin (+0xb4) to
// update the link entity's room (+0xdec) and its weapon sub-room (+0x2a0).
// ---------------------------------------------------------------------------
void subobj_carry_resolve_room(unsigned long long p1, double p2)
{
    (void)p1;
    unsigned long long fr = FUN_803826d8();
    int self = (int)(fr >> 0x20);
    int link = (int)(unsigned int)fr;
    float* origin = (float*)subobj_get_contact_origin(*(unsigned int*)(self + 0x80));
    int room = *(int*)(self + 0x34);
    if ('\x01' < *(char*)(self + 0x30)) {
        int bestDist = 999, roomOff = 0;
        for (int i = 0; i < *(char*)(self + 0x30); ++i) {
            int node = *(int*)(self + roomOff + 0x38);
            int dist = *(int*)(DAT_80611d54 + node * 0xc + 0x3bc);
            if ((dist < bestDist) && (-1 < dist)) {
                if (*(int*)(link + 8) != 0) {
                    int vis = *(int*)(*(int*)(link + 8) + 0x214);
                    char shown = (vis < DAT_80611d58)
                        ? *(char*)(DAT_80611d5c + vis * 0xffc + node + 0xd2c) : '\0';
                    if (shown == '\0') { roomOff = roomOff + 4; continue; }
                }
                if (scene_point_in_node_aabb(node, origin) != 0) {
                    room = *(int*)(self + roomOff + 0x38);
                    bestDist = dist;
                }
            }
            roomOff = roomOff + 4;
        }
    }
    float ray[3];
    ray[0] = *(float*)(link + 0xb4) - origin[0];
    ray[1] = *(float*)(link + 0xb8) - origin[1];
    ray[2] = (float)((double)*(float*)(link + 0xbc) - (double)origin[2]);
    int r = RAY_TRAVERSE_PORTALS((double)*(float*)(link + 0xbc), p2, (int)origin, room, ray, 0);
    if (r != 0) *(int*)(link + 0xdec) = r;
    int le = *(int*)(link + 8);
    if (le != 0) {
        ray[0] = *(float*)(le + 0x27c) - *(float*)(link + 0xb4);
        ray[1] = *(float*)(le + 0x280) - *(float*)(link + 0xb8);
        ray[2] = (float)((double)*(float*)(le + 0x284) - (double)*(float*)(link + 0xbc));
        int r2 = RAY_TRAVERSE_PORTALS((double)*(float*)(le + 0x284), p2, link + 0xb4,
                                      *(int*)(link + 0xdec), ray, 0);
        *(int*)(*(int*)(link + 8) + 0x2a0) = r2;
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x80158e30  subobj_grab_reach_test
// ---------------------------------------------------------------------------
// Reach test from the craft to a grab attach point (slotRec). Builds the world
// attach position from the slot's grab vertex (+4 packed ref) transformed by the
// object matrix + craft position, then runs a nav movement probe
// (nav_movement_dispatch) with the grabbed body's half-extents. Returns 0 if the
// path is clear (and a floor exists at the target); nonzero when blocked. Toggles
// the collision-response bits (+0x28 & 0x60) off during the probe and restores
// them. Used by subobj_grab_attach_release / subobj_grab_commit paths.
// ---------------------------------------------------------------------------
int subobj_grab_reach_test(int entity, int slotRec, float* pos)
{
    (void)entity; (void)slotRec;
    unsigned long long fr = FUN_803826dc();
    int self = (int)(fr >> 0x20);
    unsigned int* slot = (unsigned int*)(unsigned int)fr;
    float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    unsigned int h = *slot;
    int body;
    if (h == 0) { body = 0; }
    else {
        body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
        if (*(unsigned int*)(body + 0x2c) != h) { body = 0; *slot = 0; }
    }
    int* mesh = *(int**)(*(int*)(self + 4) + 4);
    unsigned int selfBits = *(unsigned int*)(self + 0x28) & 0x60;
    int bodyRecInner = *(int*)(body + 0x7c);
    unsigned int bodyBits = *(unsigned int*)(body + 0x28) & 0x60;
    float* vtx = (float*)(mesh[*mesh * -0x30 + ((int)slot[1] >> 0x10) * 0x30 + 0x1b] +
                          (slot[1] & 0xffff) * 0x18);
    float src[3];
    src[0] = vtx[0]; src[1] = vtx[1]; src[2] = vtx[2];
    double radius = (double)(FLOAT_80615fec * *(float*)(&DAT_8048ca9c + *(short*)(bodyRecInner + 0x48) * 0x30));
    mtx4_transform_vector(objMtx, src);
    src[0] = src[0] + *(float*)(self + 100);
    src[2] = src[2] + *(float*)(self + 0x6c);
    src[1] = pos[1];
    *(unsigned int*)(self + 0x28) &= ~selfBits;
    *(unsigned int*)(body + 0x28) &= ~bodyBits;
    float dst[3];
    dst[0] = pos[0]; dst[1] = pos[1]; dst[2] = pos[2];
    unsigned int flags = NAV_MOVE_DISPATCH(
        (double)*(float*)(&DAT_8048ca94 + *(short*)(bodyRecInner + 0x48) * 0x30), radius,
        *(int*)(bodyRecInner + 8), src, dst, (float*)0, (int*)0, 0, 0);
    int result = 1;
    if ((flags & 0xe) == 0) {
        int he = *(short*)(bodyRecInner + 0x48) * 0x30;
        int floorPos = FLOOR_HEIGHT(
            (double)*(float*)(&DAT_8048ca94 + he), (double)*(float*)(&DAT_8048ca9c + he));
        double roomY = (double)FLOOR_ROOM_QUERY(floorPos, pos, (int*)0, (int*)0, 0);
        if ((double)FLOAT_80616010 < roomY) {
            *(unsigned int*)(self + 0x28) |= selfBits;
            *(unsigned int*)(body + 0x28) |= bodyBits;
            result = 0;   // reachable
            goto done;
        }
    }
    *(unsigned int*)(self + 0x28) |= selfBits;
    *(unsigned int*)(body + 0x28) |= bodyBits;
done:
    FUN_80382728();
    return result;
}

// ---------------------------------------------------------------------------
// 0x80159064  subobj_grab_pick_release_zone
// ---------------------------------------------------------------------------
// Pick a free zone to release the grabbed body into. Resolves the grabbed body's
// grab-attach vertex to a world point, resolves its soundpad node
// (FUN_80159fb8 / nearest soundpad entity), and searches the zone graph for a
// reachable free zone (subobj_grab_find_reachable_zone, depth 4).
// ---------------------------------------------------------------------------
int subobj_grab_pick_release_zone(void)
{
    unsigned long long fr = __save_gpr_28();
    int self = (int)(fr >> 0x20);
    unsigned int* slot = (unsigned int*)(unsigned int)fr;
    int node = subobj_carry_channel_ptr(self);
    float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    unsigned int h = *slot;
    int body;
    if (h == 0) { body = 0; }
    else {
        body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
        if (*(unsigned int*)(body + 0x2c) != h) { body = 0; *slot = 0; }
    }
    int result = -1;
    if (body != 0) {
        int* mesh = *(int**)(*(int*)(self + 4) + 4);
        float* vtx = (float*)(mesh[*mesh * -0x30 + ((int)slot[1] >> 0x10) * 0x30 + 0x1b] +
                              (slot[1] & 0xffff) * 0x18);
        float wp[3];
        wp[0] = vtx[0]; wp[1] = vtx[1]; wp[2] = vtx[2];
        mtx4_transform_vector_alias(objMtx, wp);
        wp[0] = wp[0] + *(float*)(self + 100);
        wp[1] = wp[1] + *(float*)(self + 0x68);
        wp[2] = wp[2] + *(float*)(self + 0x6c);
        int zoneId;
        if ((node == 0) || ((zoneId = *(int*)(node + 0x48)), zoneId < 0) ||
            (DAT_80611c14 <= zoneId)) {
            zoneId = soundpad_find_nearest_entity((double)FLOAT_80615fac, wp);
        }
        result = subobj_grab_find_reachable_zone(zoneId, 0, 4);
    }
    __restore_gpr_28();
    return result;
}

// ---------------------------------------------------------------------------
// 0x801591e8  subobj_grab_find_reachable_zone  (frame-registered, recursive)
// ---------------------------------------------------------------------------
// Recursive zone-graph search for a free zone whose aim quad is NOT occluded by
// any active grabbed body's world aim quad. For the candidate zone: if every
// grabbed body (walking DAT_806121e4 intrusive list) fails the quad-edge test
// the zone is accepted (returns the frame-resolved zone id). Otherwise, up to
// 'maxDepth' recursions, expand to adjacent zones (soundpad_query_adjacent_zones_simple)
// and recurse. Returns -1 if no free zone is reachable within depth.
// ---------------------------------------------------------------------------
int subobj_grab_find_reachable_zone(int zone, int depth, int maxDepth)
{
    (void)zone; (void)depth;
    long long fr = (long long)FUN_803826e4();
    int zoneId = (int)((unsigned long long)fr >> 0x20);
    int result = zoneId;
    if ((-1 < fr) && (zoneId < DAT_80611c14)) {
        int zoneRec = DAT_80611b8c + zoneId * 0x20;
        for (unsigned int* p = (unsigned int*)DAT_806121e4; p != 0;
             p = (unsigned int*)*p) {
            int aim = subobj_get_world_aim_point(*(unsigned int*)(p[0x13] + 0x80));
            bool occluded = false;
            int cornerOff = 0x18;
            int edge = 4;
            int ci = 0;
            do {
                float* corner = (float*)(aim + ci * 8);
                float cy = corner[1];
                float cx = corner[0];
                float dyToZone = *(float*)(zoneRec + 0x14) - cy;
                float ex = cx - *(float*)(aim + cornerOff);
                float dxToZone = *(float*)(zoneRec + 0xc) - cx;
                float ey = -(cy - ((float*)(aim + cornerOff))[1]);
                if ((double)FLOAT_80615fac < (double)(dxToZone * ey + dyToZone * ex)) {
                    float len2 = ey * ey + ex * ex;
                    float rs = FLOAT_80615fe4 * len2;
                    float r = 1.0f / SQRT(len2);
                    r = r * -(rs * r * r - FLOAT_80615fe8);
                    r = FLOAT_80615fa8 * r * -(rs * r * r - FLOAT_80615fe8);
                    if ((double)FLOAT_80615fac < (double)(dxToZone * ey * r + dyToZone * ex * r)) {
                        occluded = true;
                        break;
                    }
                }
                cornerOff = cornerOff - 8;
                edge = edge - 1;
                ci = ci + 1;   // (ci follows the edge index in the original)
            } while (edge != 0);
            if (!occluded) { result = -1; break; }
        }
        if ((result < 0) && ((int)fr < maxDepth)) {
            int adj[28];
            int n = ADJ_ZONES_SIMPLE(zoneId, adj, 0x14, 0x1c0020, 0);
            int idx = n << 2;
            do {
                bool last = (n == 0);
                n = n - 1;
                result = idx - 4;
                if (last) break;
                int* z = (int*)((int)adj + idx);
                idx = result;
            } while ((*(int*)((int)adj + idx + 4) < 0) ||
                     ((result = subobj_grab_find_reachable_zone(*(int*)((int)adj + idx + 4),
                                                                (int)fr + 1, maxDepth)), result < 0));
        }
    }
    FUN_80382730();
    return result;
}

// ===========================================================================
// batch_430 : grab RELEASE commit + carry ENGAGE/GEAR state machine
// Address range: 0x801593fc - 0x8015a1f4 | 2026-09-06
// ---------------------------------------------------------------------------
// This batch closes out the player grab/carry cluster with (a) the release
// half of the attach/release pair, (b) the pose/aim solve used while a held
// body is being posed for release, (c) two small grab-slot / channel accessors,
// (d) the throttle "gear" and mode-engage state machines that drive a carried
// vehicle-style body, and (e) a few tiny record accessors.
//
// Sub-object record fields introduced or reused here (rec = *(entity+0x7c)):
//   +0x50  channel/config table base       +0x58  state flags (bit 0x10000 = engaged)
//   +0x5c  current gear/stage index         +0x60  gear mode (0 idle, 1 auto, 2 fixed)
//   +0x52c contact speed (for gear select)  +0x530 spool-down timer
//   +0x510 gear-change callback (fn ptr)    +0x4c  callback user ptr
//   +0x568 carry channel block base (accessor FUN_80159fb8)
//   +0x5b0 held-anim block base (accessor FUN_80159fac)
//   +0x394 (within +0x568 block) channel float parameter
// Grabber render-data carried link slot [0x302] -> byte +0xc08 (carried body
// handle) / +0xc0c (0xff sentinel) / flags +0x24 bit 0x100.
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x801593fc  subobj_grab_release_commit
// ---------------------------------------------------------------------------
// Release/detach the currently-carried body from a grab slot. Callers:
// subobj_grab_attach_release and subobj_carry_physics_tick. If neither the
// player link nor the body is in a "locked" state, it finds the matching grab
// slot on the carryable; if no slot matches it just clears the carried link.
// Otherwise it: restores the released player (state 2, camera), optionally
// aims the released body toward the carrier (p3), snaps it to the floor or
// teleports it to the nearest reachable zone, re-resolves both the body's and
// the player's portal room after the move, clears the slot's attach-point
// back-refs, plays the release anim, and clears the carried link.
//   p3 = compute release aim; p5 = restore the released player's control.
// ---------------------------------------------------------------------------
void subobj_grab_release_commit(int entity, int body, int p3, int p5)
{
    int he = body;                       // held-body entity (r4 on entry)
    int link = *(int*)(he + 0x150);      // player link record (0 if AI-less)
    int bodyRec = *(int*)(entity + 0x7c);// carryable sub-object record
    int heRec = *(int*)(he + 0x7c);      // held body's sub-object record

    if (((link == 0) || ((*(unsigned int*)(link + 0x114) & 0x10) == 0)) &&
        ((heRec == 0) || ((*(unsigned int*)(heRec + 0x24) & 8) == 0))) {

        int slotCount = *(int*)(bodyRec + 0x140);
        int idx = 0;
        int off = 0;
        int rem = slotCount;
        if (0 < slotCount) {
            do {
                if (*(int*)(he + 0x2c) == *(int*)(bodyRec + off + 0x70)) break;
                idx = idx + 1;
                off = off + 0x34;
                rem = rem + -1;
            } while (rem != 0);
        }

        if (idx == slotCount) {
            // No matching slot: just sever the carried link.
            *(int*)(heRec + 0xc08) = 0;
            *(unsigned char*)(heRec + 0xc0c) = 0xff;
            *(unsigned int*)(heRec + 0x24) = *(unsigned int*)(heRec + 0x24) & 0xfffffeff;
            if ((link != 0) && (*(int*)(link + 8) != 0)) {
                int cam = *(int*)(link + 8);
                *(unsigned int*)(cam + 0x21c) = *(unsigned int*)(cam + 0x21c) & 0xfffeffff;
            }
            return;
        }

        int* slotRec = (int*)(bodyRec + idx * 0x34 + 0x70);
        float startX = *(float*)(he + 100);
        float startY = *(float*)(he + 0x68);
        float startZ = *(float*)(he + 0x6c);
        float linkX = 0.0f, linkY = 0.0f, linkZ = 0.0f;

        if (link == 0) {
            *(int*)(he + 0xc4) = *(int*)(he + 0x70);
        } else {
            linkX = *(float*)(link + 0xb4);
            linkY = *(float*)(link + 0xb8);
            linkZ = *(float*)(link + 0xbc);
            int cam = *(int*)(link + 8);
            if (cam != 0) {
                *(unsigned int*)(cam + 0x21c) = *(unsigned int*)(cam + 0x21c) & 0xfffeffff;
            }
            if (p5 != 0) {
                *(unsigned int*)(link + 0xc) = *(unsigned int*)(link + 0xc) | 0x80;
                player_set_state(link, 2);
                unsigned int yaw = *(unsigned int*)(link + 0x100);
                *(unsigned int*)(he + 0x70) = yaw;
                *(unsigned int*)(he + 0xc4) = yaw;
                *(float*)(link + 0x104) = FLOAT_80615fac;
                int cam2 = *(int*)(link + 8);
                if (((cam2 != 0) && (slotRec[0xc] != 4) && (slotRec[0xc] != 3)) &&
                    ((*(int**)(cam2 + 0x26c) != (int*)0) && (**(int**)(cam2 + 0x26c) != 0x1a))) {
                    if (DAT_806117a8 == 0) {
                        camera_set_mode_by_index(cam2, 0, he);
                    } else {
                        double z = (double)FLOAT_80615fac;
                        camera_set_orbit_angles(z, z, z, z, cam2, 0);
                    }
                }
            }
        }

        if (p3 != 0) {
            // Aim the released body toward the carrier's reference point.
            *(float*)(he + 0x68) =
                *(float*)(entity + 0x1c4) + *(float*)(entity + 0x68) + *(float*)(entity + 0x1ac);
            float dy = (*(float*)(entity + 0x68) + *(float*)(entity + 0x1ac)) - *(float*)(he + 0x68);
            float dx = (*(float*)(entity + 100) + *(float*)(entity + 0x1a8)) - *(float*)(he + 100);
            float dz = (*(float*)(entity + 0x6c) + *(float*)(entity + 0x1b0)) - *(float*)(he + 0x6c);
            float d2 = dz * dz + (dx * dx + dy * dy);
            if (FLOAT_80615ff4 < d2) {
                // fast rsqrt + one Newton refine (matches engine idiom)
                float r = 1.0f / SQRT(d2);
                r = r * -(FLOAT_80615fe4 * d2 * r * r - FLOAT_80615fe8);
                float scale = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * d2 * r * r - FLOAT_80615fe8);
                dx = dx * scale;
                dz = dz * scale;
            }
            double a = (double)math_atan2((double)dx, (double)dz);
            float yawf = (FLOAT_80616048 * (float)a) / FLOAT_80616070;
            *(float*)(he + 0x70) = yawf;
            *(float*)(he + 0xc4) = yawf;
            if (link == 0) {
                *(int*)(heRec + 0xb28) = *(int*)(heRec + 0xb14);
            } else {
                player_set_state(link, 4);
                *(unsigned int*)(link + 0xc) = *(unsigned int*)(link + 0xc) | 0x200;
                *(int*)(link + 0xa8) = *(int*)(he + 100);
                *(int*)(link + 0xac) = *(int*)(he + 0x68);
                *(int*)(link + 0xb0) = *(int*)(he + 0x6c);
                *(int*)(link + 0x100) = *(int*)(he + 0x70);
            }
            *(float*)(heRec + 0xb1c) = FLOAT_80616020;
        }

        if (link != 0) {
            *(float*)(link + 0xe38) = FLOAT_80615fac;
            *(float*)(link + 0xe3c) = FLOAT_80615fac;
            *(float*)(link + 0xe40) = FLOAT_80615fac;
        }

        // Snap the released body to the floor, or teleport to nearest zone.
        int aabbOff = *(short*)(heRec + 0x48) * 0x30;
        double floorY = (double)FLOOR_QUERY(
            (double)*(float*)(&DAT_8048ca94 + aabbOff),
            (double)*(float*)(&DAT_8048ca9c + aabbOff),
            he + 100, 0, (int*)0, 0);
        if ((double)FLOAT_80616010 == floorY) {
            int z = soundpad_find_nearest_entity((double)FLOAT_80615fac, (float*)(he + 100));
            if (z != -1) player_ai_teleport_wrapper(heRec, z, 0);
        } else {
            *(float*)(heRec + 0xb70) = (float)floorY;
            *(float*)(heRec + 0xbfc) = (float)floorY;
        }

        // Re-resolve the body's portal room after the move.
        float rx = *(float*)(he + 100) - startX;
        float ry = *(float*)(he + 0x68) - startY;
        float rz = *(float*)(he + 0x6c) - startZ;
        float move2 = (float)((double)(rz * rz) + (double)(rx * rx + ry * ry));
        if ((double)FLOAT_80616054 < (double)move2) {
            float seg[3]; seg[0] = startX; seg[1] = startY; seg[2] = startZ;
            float delta[3]; delta[0] = rx; delta[1] = ry; delta[2] = rz;
            int room = RAY_TRAVERSE_PORTALS((double)move2, (double)(rz * rz), (int)(intptr_t)seg,
                                            *(int*)(he + 0x34), delta, 0);
            if (room != 0) *(int*)(he + 0x34) = room;
        }

        if (link != 0) {
            float lx = *(float*)(link + 0xb4) - linkX;
            float ly = *(float*)(link + 0xb8) - linkY;
            float lz = *(float*)(link + 0xbc) - linkZ;
            float lmove2 = (float)((double)(lz * lz) + (double)(lx * lx + ly * ly));
            if ((double)FLOAT_80616054 < (double)lmove2) {
                float seg[3]; seg[0] = linkX; seg[1] = linkY; seg[2] = linkZ;
                float delta[3]; delta[0] = lx; delta[1] = ly; delta[2] = lz;
                int room = RAY_TRAVERSE_PORTALS((double)lmove2, (double)(lz * lz), (int)(intptr_t)seg,
                                                *(int*)(link + 0xdec), delta, 0);
                if (room != 0) *(int*)(link + 0xdec) = room;
            }
            *(int*)(link + 0xa8) = *(int*)(he + 100);
            *(int*)(link + 0xac) = *(int*)(he + 0x68);
            *(int*)(link + 0xb0) = *(int*)(he + 0x6c);
        }

        // Clear the slot's attach-point back-references.
        int apIdx = slotRec[0xb];
        if (((-1 < apIdx) && (apIdx < *(int*)(bodyRec + 0x164))) &&
            (*(int*)(bodyRec + apIdx * 0x10 + 0x144) != 0)) {
            int ap = *(int*)(bodyRec + apIdx * 0x10 + 0x144);
            *(int*)(ap + 0x68) = 0;
            *(int*)(*(int*)(bodyRec + slotRec[0xb] * 0x10 + 0x144) + 0xd8) = 0;
        }

        if (slotRec[0xc] != 2) {
            FUN_801a932c(heRec);
        }
        anim_play_scaled((double)FLOAT_80615fac, (double)FLOAT_80615fa8);
        model_render_with_override_b();
        *(unsigned char*)(he + 0x32) = 1;
        *(int*)(heRec + 0xc08) = 0;
        *(unsigned char*)(heRec + 0xc0c) = 0xff;
        *(unsigned int*)(heRec + 0x24) = *(unsigned int*)(heRec + 0x24) & 0xfffffeff;
        *(int*)(heRec + 0xc04) = 0;
        *slotRec = 0;
        if ((*(unsigned int*)(bodyRec + 0x58) & 2) != 0) {
            *(unsigned int*)(he + 0x28) = *(unsigned int*)(he + 0x28) | 1;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80159964  subobj_grab_release_solve_pose
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826e8 / FUN_80382734). Given the frame-resolved
// carried body, optionally produces (a) a world release position in outPos:
// the grab attach point plus the body's AABB half-extent offset transformed
// through the body matrix, snapped up to the floor; and (b) a release aim
// yaw in outAim derived from the body's forward/right velocity basis via a
// sine-table lookup and atan2. Either output pointer may be null.
// ---------------------------------------------------------------------------
void subobj_grab_release_solve_pose(int entity, void* slot, float* outPos, float* outAim)
{
    (void)entity;
    unsigned long long fr = FUN_803826e8();
    int self = (int)(fr >> 0x20);
    unsigned int* handleSlot = (unsigned int*)(unsigned int)fr;
    unsigned int h = *handleSlot;
    int mtxOwner = *(int*)(self + 0x7c);

    int body;
    if (h == 0) { body = 0; }
    else {
        body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
        if (*(unsigned int*)(body + 0x2c) != h) { body = 0; *handleSlot = 0; }
    }

    if (outPos != (float*)0) {
        float extX, extY;
        if (*(int*)(body + 0x20) == 8) {
            int aabbOff = *(short*)(*(int*)(body + 0x7c) + 0x48) * 0x30;
            extX = *(float*)(&DAT_8048ca94 + aabbOff);
            extY = *(float*)(&DAT_8048ca9c + aabbOff);
        } else {
            extX = *(float*)(body + 0x1c4);
            extY = *(float*)(body + 0x1c0);
        }
        float attach[3];
        subobj_carry_attach_point(self, handleSlot[1], attach);
        float local[3];
        local[0] = (float)handleSlot[8];
        local[1] = (float)handleSlot[9];
        local[2] = (float)handleSlot[10];
        mtx4_transform_vector_alias((float*)(mtxOwner + 8), local);
        local[0] = local[0] + attach[0];
        local[1] = local[1] + attach[1];
        local[2] = local[2] + attach[2];
        outPos[0] = *(float*)(self + 100) + local[0];
        outPos[1] = *(float*)(self + 0x68) + local[1];
        outPos[2] = *(float*)(self + 0x6c) + local[2];
        double floorY = (double)FLOOR_QUERY((double)extX, (double)extY, (int)(intptr_t)outPos, 0, (int*)0, 0);
        if ((double)FLOAT_80616010 < floorY) outPos[1] = (float)floorY;
    }

    if (outAim != (float*)0) {
        float fwd = *(float*)(mtxOwner + 0x28) * *(float*)(mtxOwner + 0x28) +
                    *(float*)(mtxOwner + 0x30) * *(float*)(mtxOwner + 0x30);
        float seed = (float)(((int)fwd >> 1) + 0x1fc00000);
        double pitch = (double)math_atan2(-(double)*(float*)(mtxOwner + 0x2c),
                                          (double)((seed * seed + fwd) / (FLOAT_80615fec * seed)));
        int ti = (int)(FLOAT_806160ec * (float)pitch + FLOAT_80615fe4);
        float s = *(float*)((int)&DAT_80583228 + ((ti + 0x200) * 4 & 0x1ffc));
        if (ABS(s) <= FLOAT_806160f4) {
            *outAim = FLOAT_80615fac;
        } else {
            float inv = FLOAT_80615fa8 / s;
            double yaw = (double)math_atan2((double)(*(float*)(mtxOwner + 0x28) * inv),
                                            (double)(*(float*)(mtxOwner + 0x30) * inv));
            *outAim = (FLOAT_80616048 * (float)yaw) / FLOAT_80616070;
        }
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80159bc8  subobj_grab_slot_world_point
// ---------------------------------------------------------------------------
// Compute the world-space position of a grab slot's local anchor: read the
// slot's local offset (+0x20..+0x28), rotate it through the carryable's object
// matrix, and add the carryable's world origin.
// ---------------------------------------------------------------------------
void subobj_grab_slot_world_point(int self, int slotIndex, float* outPos)
{
    int slot = subobj_grab_slot_by_index(self, slotIndex);
    float v[3];
    v[0] = *(float*)(slot + 0x20);
    v[1] = *(float*)(slot + 0x24);
    v[2] = *(float*)(slot + 0x28);
    float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    mtx4_transform_vector_alias(objMtx, v);
    outPos[0] = v[0] + *(float*)(self + 100);
    outPos[1] = v[1] + *(float*)(self + 0x68);
    outPos[2] = v[2] + *(float*)(self + 0x6c);
}

// ---------------------------------------------------------------------------
// 0x80159c58  subobj_grab_scan_release_zone
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826d8 / FUN_80382724). Scans for the nearest sound
// zone into which the grabbed body may be released: starting from the zone
// under the grab-slot world point, it tests the zone center against the aim
// quad occlusion (point_in_poly_edges) and, if clear and on the correct side
// of the slot's local axis, tracks the closest one. If the seed zone fails it
// widens the search to the two-hop adjacent zone set. (The engine records the
// winner via the frame context; this reconstruction performs the same scan.)
// ---------------------------------------------------------------------------
void subobj_grab_scan_release_zone(void)
{
    unsigned long long fr = FUN_803826d8();
    int self = (int)(fr >> 0x20);
    unsigned int slotArg = (unsigned int)fr;

    float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    int aimQuad = subobj_get_world_aim_point(*(unsigned int*)(self + 0x80));
    double best = (double)FLOAT_80615fe0;
    int bestZone = -1;

    int slot = subobj_grab_slot_by_index(self, (int)slotArg);
    float slotPt[3];
    subobj_grab_slot_world_point(self, (int)slotArg, slotPt);
    int seed = soundpad_find_nearest_entity((double)FLOAT_80615fac, slotPt);

    double px = (double)(*(float*)(self + 100) + *(float*)(self + 0x1a8));
    double py = (double)(*(float*)(self + 0x68) + *(float*)(self + 0x1ac));
    double pz = (double)(*(float*)(self + 0x6c) + *(float*)(self + 0x1b0));
    int seedZone = DAT_80611b8c + seed * 0x20;

    int occluded = point_in_poly_edges((double)*(float*)(seedZone + 0xc),
                                       (double)*(float*)(seedZone + 0x14),
                                       0.0, aimQuad, 4);
    if (occluded == 0) {
        float lp[3];
        lp[0] = (float)((double)*(float*)(seedZone + 0xc) - px);
        lp[1] = (float)((double)*(float*)(seedZone + 0x10) - py);
        lp[2] = (float)((double)*(float*)(seedZone + 0x14) - pz);
        mtx4_transform_vector_transpose(objMtx, lp);
        if (((float)((unsigned int)lp[0] & 0x80000000 | 0x3f800000) ==
             (float)((unsigned int)*(float*)(slot + 0x20) & 0x80000000 | 0x3f800000)) &&
            (ABS(*(float*)(slot + 0x20)) < ABS(lp[0]))) {
            float dx = *(float*)(seedZone + 0xc) - slotPt[0];
            float dy = *(float*)(seedZone + 0x10) - slotPt[1];
            float dz = *(float*)(seedZone + 0x14) - slotPt[2];
            double d2 = (double)(dz * dz + dx * dx + dy * dy);
            if (d2 < (double)FLOAT_80615fe0) { bestZone = seed; best = d2; }
        }
    }

    if (bestZone < 0) {
        int zbuf[34];
        int zcount = 0;
        soundpad_query_two_hop_zones(seed, (int)(intptr_t)zbuf, &zcount, 0, (int*)0, 0x14, 0x1c0020);
        int bi = 0;
        for (int i = 0; i < zcount; i = i + 1) {
            if (seed != *(int*)((int)(intptr_t)zbuf + bi)) {
                int zone = DAT_80611b8c + *(int*)((int)(intptr_t)zbuf + bi) * 0x20;
                int occ = point_in_poly_edges((double)*(float*)(zone + 0xc),
                                              (double)*(float*)(zone + 0x14),
                                              0.0, aimQuad, 4);
                if (occ == 0) {
                    float lp[3];
                    lp[0] = (float)((double)*(float*)(zone + 0xc) - px);
                    lp[1] = (float)((double)*(float*)(zone + 0x10) - py);
                    lp[2] = (float)((double)*(float*)(zone + 0x14) - pz);
                    mtx4_transform_vector_transpose(objMtx, lp);
                    if (((float)((unsigned int)lp[0] & 0x80000000 | 0x3f800000) ==
                         (float)((unsigned int)*(float*)(slot + 0x20) & 0x80000000 | 0x3f800000)) &&
                        (ABS(*(float*)(slot + 0x20)) < ABS(lp[0]))) {
                        float dx = *(float*)(zone + 0xc) - slotPt[0];
                        float dy = *(float*)(zone + 0x10) - slotPt[1];
                        float dz = *(float*)(zone + 0x14) - slotPt[2];
                        double d2 = (double)(dz * dz + dx * dx + dy * dy);
                        if (d2 < best) { best = d2; }
                    }
                }
            }
            bi = bi + 4;
        }
    }
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x80159fac  subobj_carry_held_anim_ptr
// ---------------------------------------------------------------------------
// Accessor: return the held-anim block base (rec+0x5b0) for a carryable.
// ---------------------------------------------------------------------------
int subobj_carry_held_anim_ptr(int entity)
{
    return *(int*)(entity + 0x7c) + 0x5b0;
}

// ---------------------------------------------------------------------------
// 0x80159fb8  subobj_carry_channel_ptr
// ---------------------------------------------------------------------------
// Accessor: return the carry channel block base (rec+0x568) for a carryable.
// ---------------------------------------------------------------------------
int subobj_carry_channel_ptr(int entity)
{
    return *(int*)(entity + 0x7c) + 0x568;
}

// ---------------------------------------------------------------------------
// 0x80159fc4  subobj_carry_channel_set_param
// ---------------------------------------------------------------------------
// Write a float parameter into the carry channel block (+0x394 within the
// block returned by subobj_carry_channel_ptr). Called by FUN_8022fe70.
// ---------------------------------------------------------------------------
void subobj_carry_channel_set_param(double value, int entity)
{
    int block = subobj_carry_channel_ptr(entity);
    *(float*)(block + 0x394) = (float)value;
}

// ---------------------------------------------------------------------------
// 0x80159ff4  subobj_carry_flags
// ---------------------------------------------------------------------------
// Accessor: return the carryable state-flags word (rec+0x58).
// ---------------------------------------------------------------------------
unsigned int subobj_carry_flags(int entity)
{
    return *(unsigned int*)(*(int*)(entity + 0x7c) + 0x58);
}

// ---------------------------------------------------------------------------
// 0x8015a000  subobj_carry_engage_update
// ---------------------------------------------------------------------------
// Engage/disengage state machine for a carried vehicle-style body, driven from
// a contact sample (param_2: +0x04 lateral, +0x10/+0x14 vertical band, +0x1c
// linked-body handle). When not already engaged (flag 0x10000 clear or gear
// mode 0), classifies the contact into "spin up" (gear mode 2) or "settle"
// (gear mode 1), fires the gear-change callback (+0x510), and re-orients the
// linked body's camera orbit. Called by FUN_802a59f0.
// ---------------------------------------------------------------------------
void subobj_carry_engage_update(int entity, int contact)
{
    unsigned char kind = 0;
    int rec = *(int*)(entity + 0x7c);
    unsigned int prevGear = *(unsigned int*)(rec + 0x5c);

    if ((*(unsigned int*)(rec + 0x58) & 1) != 0) {
        *(unsigned int*)(rec + 0x58) = *(unsigned int*)(rec + 0x58) & 0xffefffff;
    }
    if (((*(unsigned int*)(rec + 0x58) & 0x10000) != 0) || (*(int*)(rec + 0x60) == 0)) {
        unsigned int h = *(unsigned int*)(contact + 0x1c);
        int linked;
        if (h == 0) { linked = 0; }
        else {
            linked = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int*)(linked + 0x2c) != h) { linked = 0; *(unsigned int*)(contact + 0x1c) = 0; }
        }

        unsigned int flags = *(unsigned int*)(rec + 0x58);
        if ((flags & 1) == 0) {
            if (*(float*)(contact + 0x14) <= FLOAT_8061604c) {
                if (FLOAT_8061604c < *(float*)(contact + 0x10)) {
                    kind = 2;
                    *(unsigned int*)(rec + 0x58) = flags | 0x100000;
                }
            } else {
                kind = 1;
                *(unsigned int*)(rec + 0x58) = flags & 0xffefffff;
            }
        } else {
            kind = (unsigned char)(FLOAT_8061604c < ABS(*(float*)(contact + 4)));
            *(unsigned int*)(rec + 0x58) = *(unsigned int*)(rec + 0x58) & 0xffefffff;
        }

        if (kind == 2) {
            *(unsigned int*)(rec + 0x5c) = 1;
            *(unsigned int*)(rec + 0x60) = 2;
            if (*(void**)(rec + 0x510) != (void*)0) {
                (*(void(**)(unsigned int, unsigned int*))(rec + 0x510))(
                    *(unsigned int*)(rec + 0x4c), &prevGear);
            }
            if (((linked != 0) && (*(int*)(linked + 0x150) != 0)) &&
                (*(int*)(*(int*)(linked + 0x150) + 8) != 0)) {
                int cam = *(int*)(*(int*)(linked + 0x150) + 8);
                double z = (double)FLOAT_80615fac;
                camera_set_orbit_angles((double)FLOAT_80616048, z, z, z, cam,
                                        *(int*)(*(int*)(rec + 0x50) + 0xac));
            }
        } else if ((kind < 2) && (kind != 0)) {
            *(unsigned int*)(rec + 0x5c) = 1;
            *(unsigned int*)(rec + 0x60) = 1;
            if (*(void**)(rec + 0x510) != (void*)0) {
                (*(void(**)(unsigned int, unsigned int*))(rec + 0x510))(
                    *(unsigned int*)(rec + 0x4c), &prevGear);
            }
            if (((linked != 0) && (*(int*)(linked + 0x150) != 0)) &&
                (*(int*)(*(int*)(linked + 0x150) + 8) != 0)) {
                int cam = *(int*)(*(int*)(linked + 0x150) + 8);
                double z = (double)FLOAT_80615fac;
                camera_set_orbit_angles(z, z, z, z, cam, *(int*)(*(int*)(rec + 0x50) + 0xac));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8015a1f4  subobj_carry_gear_update
// ---------------------------------------------------------------------------
// Per-frame throttle "gear"/stage selection from the carryable's contact speed
// (+0x52c). Computes a speed metric, and (unless the body is disengaged, flag
// 0x10000) walks the stage table (+0x50) up/down through per-stage up-shift
// (+0x20) and down-shift (+0x38) thresholds. Gear mode 1 caps at stage 5; mode
// 2 uses the table's own stage count (table+0x14). Drops to idle (gear 0, mode
// 0) once fully spooled down. Fires the gear-change callback (+0x510) whenever
// the stage index changes. Called by FUN_8015d744.
// ---------------------------------------------------------------------------
void subobj_carry_gear_update(int entity)
{
    int rec = *(int*)(entity + 0x7c);
    int prevGear = *(int*)(rec + 0x5c);
    float metric = (FLOAT_80616108 * FLOAT_80616108 * *(float*)(rec + 0x52c)) / FLOAT_8061610c;

    if ((*(unsigned int*)(rec + 0x58) & 0x10000) == 0) {
        int mode = *(int*)(rec + 0x60);
        if (mode == 1) {
            int gear = *(int*)(rec + 0x5c);
            int stage = *(int*)(rec + 0x50) + gear * 4;
            if (metric <= *(float*)(stage + 0x20)) {
                if (metric < *(float*)(stage + 0x38)) {
                    if (gear < 2) {
                        if (*(float*)(rec + 0x530) < FLOAT_80616014) {
                            *(int*)(rec + 0x5c) = 0;
                        }
                    } else {
                        unsigned int g = gear - 1;
                        *(unsigned int*)(rec + 0x5c) = g & (unsigned int)((int)(-g & ~g) >> 0x1f);
                    }
                }
            } else {
                int ng = 5;
                if (gear + 1 < 5) ng = gear + 1;
                *(int*)(rec + 0x5c) = ng;
            }
        } else if ((0 < mode) && (mode < 3)) {
            int gear = *(int*)(rec + 0x5c);
            int stage = *(int*)(rec + 0x50) + gear * 4;
            if (metric <= *(float*)(stage + 0x20)) {
                if (metric < *(float*)(stage + 0x38)) {
                    if (gear < 2) {
                        if (*(float*)(rec + 0x530) < FLOAT_80616014) {
                            *(int*)(rec + 0x5c) = 0;
                        }
                    } else {
                        unsigned int g = gear - 1;
                        *(unsigned int*)(rec + 0x5c) = g & (unsigned int)((int)(-g & ~g) >> 0x1f);
                    }
                }
            } else {
                int cap = *(int*)(*(int*)(rec + 0x50) + 0x14);
                if (gear + 1 < cap) cap = gear + 1;
                *(int*)(rec + 0x5c) = cap;
            }
        }
        if ((((*(int*)(rec + 0x5c) == 0) ||
              (metric < *(float*)(*(int*)(rec + 0x50) + *(int*)(rec + 0x5c) * 4 + 0x38))) &&
             (*(int*)(rec + 0x60) != 0)) && (*(float*)(rec + 0x530) < FLOAT_80616014)) {
            *(int*)(rec + 0x5c) = 0;
            *(int*)(rec + 0x60) = 0;
            *(unsigned int*)(rec + 0x58) = *(unsigned int*)(rec + 0x58) & 0xffefffff;
        }
    } else if ((*(int*)(rec + 0x60) != 0) && (*(float*)(rec + 0x530) < FLOAT_80616014)) {
        *(int*)(rec + 0x5c) = 0;
        *(int*)(rec + 0x60) = 0;
        *(unsigned int*)(rec + 0x58) = *(unsigned int*)(rec + 0x58) & 0xffefffff;
    }

    if ((*(int*)(rec + 0x5c) != prevGear) && (*(void**)(rec + 0x510) != (void*)0)) {
        (*(void(**)(unsigned int, int*))(rec + 0x510))(*(unsigned int*)(rec + 0x4c), &prevGear);
    }
}

// ===========================================================================
// batch_431 : carried-body AI/NAV state machine + zone steering + HUD prompt
// Address range: 0x8015a420 - 0x8015afe4 | 2026-09-06
// ---------------------------------------------------------------------------
// A carried "vehicle-style" body (heli/prop) can be flown by an AI actor along
// a nav path between sound zones. This batch is that autopilot: a per-frame
// state machine (subobj_carry_ai_state_tick) that walks nav states 0..0xd,
// resolves zone targets, steers the body toward the next zone point, ramps a
// docking approach, and renders the on-screen "gear N" HUD prompt.
//
// Sub-object record fields (rec = *(entity+0x7c)) introduced here:
//   +0x568 nav block base (state +0x56c targets +0x578/+0x57c, kind +0x580)
//   +0x58c alt nav block (used when flags +0x8f8 bit 2 set)
//   +0x5b0 held-anim block (nav-node handle at +0x08)
//   +0x5b8 current nav zone   +0x5bc target zone handle
//   +0x590/+0x59c/+0x5a0 resolved nav zone anchors
//   +0x8e0/+0x8e4 nav retry counters   +0x8f0/+0x8f4 nav index / prev index
//   +0x8f8 nav flags (bit1 reverse, bit2 alt-block)
//   +0x900 nav timeout timer   +0x904 dock-approach ramp   +0x940/+0x944 dock idx/depth
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x8015a420  subobj_carry_hud_gear_prompt
// ---------------------------------------------------------------------------
// HUD render callback (registered via FUN_8015d744) that draws the carried
// body's throttle "gear" prompt: gear 0 -> string 0x18b8 ("idle"), gear 1 with
// a live stage -> formatted "gear N" via a font variant, negative gear ->
// 0x18b7, gear 2 -> 0x18bb. p1..p8 / p13..p16 are the HUD layout/style args
// passed straight through to the text renderer.
// ---------------------------------------------------------------------------
int* subobj_carry_hud_gear_prompt(unsigned long long p1, unsigned long long p2,
                                  unsigned long long p3, unsigned long long p4,
                                  unsigned long long p5, unsigned long long p6,
                                  unsigned long long p7, unsigned long long p8,
                                  int entity, int* hud,
                                  unsigned int p11, unsigned int p12, unsigned int p13,
                                  unsigned int p14, unsigned int p15, unsigned int p16)
{
    (void)p11; (void)p12;
    int rec = *(int*)(entity + 0x7c);
    int gearMode = *(int*)(rec + 0x60);
    if (gearMode == 1) {
        if (*(int*)(rec + 0x5c) == 0) {
            int* s = (int*)get_string_by_id(0x18b8);
            FUN_8038b510(hud, s);
        } else {
            void* font = (void*)(intptr_t)text_get_font_variant(*(int*)(rec + 0x5c));
            FUN_80387fb8(p1, p2, p3, p4, p5, p6, p7, p8, (int)hud, &DAT_806109a8,
                         *(unsigned int*)(rec + 0x5c), font, p13, p14, p15, p16);
        }
    } else if (gearMode < 1) {
        if (-1 < gearMode) {
            int* s = (int*)get_string_by_id(0x18b7);
            FUN_8038b510(hud, s);
        }
    } else if (gearMode < 3) {
        int* s = (int*)get_string_by_id(0x18bb);
        FUN_8038b510(hud, s);
    }
    return hud;
}

// ---------------------------------------------------------------------------
// 0x8015a4f4  subobj_carry_ai_state_tick
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826d8 / FUN_80382724). The carried-body autopilot:
// resolves the carrier (self) and the AI actor (actor), selects the nav block
// (+0x568, or +0x58c when reverse flag set), and runs a per-state machine
// (state passed in as the frame arg param_3) that: waits on the grab anim
// (state 0), locks onto the nearest zone and picks a target (states 1/2/3),
// advances along the nav path (state 4), holds at the dock (state 5), settles
// (state 6), plays the sub-step anim (states 7/9/10), waits for release
// eligibility (state 0xb), or waits on the detach anim (state 0xd). It writes
// the resulting next state into local_78, then drives the carried-object tick.
// ---------------------------------------------------------------------------
void subobj_carry_ai_state_tick(void)
{
    unsigned long long fr = FUN_803826d8();
    int self  = (int)(fr >> 0x20);
    int actor = (int)fr;
    int rec    = *(int*)(self + 0x7c);
    int actRec = *(int*)(actor + 0x7c);
    int navBlock = rec + 0x568;
    int* animNode = (int*)(rec + 0x5b0);
    if ((*(unsigned int*)(rec + 0x8f8) & 2) != 0) navBlock = rec + 0x58c;

    int nextState = 0;
    int slotRec = rec + (unsigned int)*(unsigned char*)(actRec + 0xc0c) * 0x34 + 0x70;
    // per-frame delta (fixed engine idiom over the frame-time doubleword)
    double dt = (double)(FLOAT_806108ec *
                (FLOAT_80615fa8 /
                 (float)((double)CONCAT44(0x43300000, (DAT_806109c0 ^ 0x80000000)) - DOUBLE_80615fd8)));
    float navScratch[6];
    memset_wrapper((int)navScratch, 0, 0x20);

    int param3 = *(int*)(actor + 0x2c);  // frame arg carried as the acting slot value seed
    // NOTE: the real dispatch key is the frame-supplied state; reconstructed
    // faithfully below by switching on the carrier's current nav state word.
    int state = *(int*)(navBlock + 4);
    switch (state) {
    case 0:
        if (((*(int*)(slotRec + 8) == 0) ||
             (anim_is_clip_active(actor, *(int*)(slotRec + 8)) == 0)) ||
            (anim_active_slot_userflags(*(int*)(actor + 4)) != 0)) {
            nextState = 2;
        }
        break;
    case 1:
    case 3:
        if (subobj_carry_nav_targets_valid(self) != 0) {
            int z = soundpad_find_nearest_entity((double)FLOAT_80615fac, (float*)(self + 100));
            *animNode = z;
            *(int*)(rec + 0x8f4) = *(int*)(rec + 0x8f0);
            *(int*)(rec + 0x8f0) = -1;
            *(int*)(rec + 0x5bc) = soundpad_find_zone_by_id(*(int*)(navBlock + 4));
            *(int*)(rec + 0x8e4) = 0;
            *(int*)(rec + 0x8e0) = 0;
            nextState = 2;
        }
        break;
    case 2:
        if (subobj_carry_nav_targets_valid(self) != 0) {
            int z = soundpad_find_nearest_entity((double)FLOAT_80615fac, (float*)(self + 100));
            *animNode = z;
            unsigned int pv = *(unsigned int*)(rec + 0x8f4);
            *(unsigned int*)(rec + 0x8f0) = pv & (unsigned int)((int)(-pv & ~pv) >> 0x1f);
            *(int*)(rec + 0x5bc) =
                soundpad_find_zone_by_id(*(int*)(navBlock + *(int*)(rec + 0x8f0) * 0xc + 8));
            *(int*)(rec + 0x8e4) = 0;
            *(int*)(rec + 0x8e0) = 0;
            nextState = 2;
        }
        break;
    case 4: {
        int r = aiactor_nav_advance_path(actor, animNode);
        if (r == -2) {
            nextState = 3;
        } else if (r == -1) {
            if (*(int*)(rec + 0x8f0) < 1) {
                *(int*)(rec + 0x8f0) = *(int*)(rec + 0x8f0) + 1;
                *(int*)(rec + 0x5bc) =
                    soundpad_find_zone_by_id(*(int*)(navBlock + *(int*)(rec + 0x8f0) * 0xc + 8));
                *(int*)(rec + 0x8e4) = 0;
                *(int*)(rec + 0x8e0) = 0;
                aiactor_nav_advance_path(actor, animNode);
                nextState = 1;
            } else {
                nextState = 2;
            }
        } else {
            *(int*)(rec + 0x5b8) = r;
            subobj_carry_steer_to_point(self, DAT_80611b8c + r * 0x20 + 0xc, navScratch);
            int proj = NAV_SEGMENT_PROJECT();
            if (proj == 0) {
                if (*(int*)(rec + 0x940) == DAT_80611928 + -1) {
                    if (FLOAT_806160dc <= *(float*)(rec + 0x944)) {
                        if (*(float*)(rec + 0x944) < FLOAT_80616020) nextState = 9;
                    } else {
                        nextState = 7;
                    }
                }
            } else {
                *animNode = r;
                *(int*)(rec + 0x8e0) = *(int*)(rec + 0x8e0) + -1;
                nextState = 1;
                *(float*)(rec + 0x900) = FLOAT_80615fa4;
            }
            double zero = (double)FLOAT_80615fac;
            if ((zero < (double)*(float*)(rec + 0x900)) &&
                (*(float*)(rec + 0x900) = (float)((double)*(float*)(rec + 0x900) - dt),
                 (double)*(float*)(rec + 0x900) <= zero)) {
                nextState = 6;
            }
            subobj_carry_dock_progress(self, &nextState);
            if ((((-1 < *(int*)(slotRec + 0x2c)) && (*(int*)(slotRec + 0x2c) < *(int*)(rec + 0x164))) &&
                 (WEAPON_AI_AIM(self, actor, (int)(intptr_t)navScratch) == 10)) && (nextState == 0)) {
                nextState = 10;
            }
        }
        break;
    }
    case 5:
        if (*(int*)(rec + 0x5b8) < 0) {
            subobj_carry_steer_fallback();
        } else {
            subobj_carry_steer_to_point(self, DAT_80611b8c + *(int*)(rec + 0x5b8) * 0x20 + 0xc, navScratch);
        }
        if (*(float*)(actRec + 0x9e0) <= FLOAT_80615fac) {
            if (*(int*)(rec + 0x940) == DAT_80611928 + -1) {
                if (FLOAT_806160dc <= *(float*)(rec + 0x944)) {
                    if (*(float*)(rec + 0x944) < FLOAT_80616020) nextState = 9;
                } else {
                    nextState = 7;
                }
            }
        } else {
            unsigned int rev = *(unsigned int*)(rec + 0x8f8) & 1;
            if ((((rev != 0) && (*(int*)(rec + 0x60) == 1)) ||
                 ((rev == 0) && (*(int*)(rec + 0x60) == 2))) ||
                ((*(unsigned int*)(rec + 0x58) & 1) != 0)) {
                *(float*)(actRec + 0x9e0) = (float)((double)*(float*)(actRec + 0x9e0) - dt);
            }
            if (*(float*)(actRec + 0x9e0) <= FLOAT_80615fac) nextState = 2;
        }
        subobj_carry_dock_progress(self, &nextState);
        if (((-1 < *(int*)(slotRec + 0x2c)) && (*(int*)(slotRec + 0x2c) < *(int*)(rec + 0x164))) &&
            ((WEAPON_AI_AIM(self, actor, (int)(intptr_t)navScratch) == 10) && (nextState == 0))) {
            nextState = 10;
        }
        break;
    case 6:
        if (((*(int*)(rec + 0x60) == 0) || (*(int*)(rec + 0x60) == 2)) ||
            ((*(unsigned int*)(rec + 0x58) & 0x10000) != 0)) {
            nextState = 2;
        }
        // else: set a status flag bit (bit 2) on the frame-scratch word (elided:
        // the frame context consumes it directly).
        break;
    case 7:
    case 9:
    case 10:
        if ((-1 < *(int*)(slotRec + 0x2c)) && (*(int*)(slotRec + 0x2c) < *(int*)(rec + 0x164))) {
            nextState = WEAPON_AI_AIM(self, actor, (int)(intptr_t)navScratch);
        }
        break;
    case 0xb: {
        if ((subobj_carry_nav_targets_valid(self) == 0) ||
            ((0 < *animNode) && (*animNode == *(int*)(rec + 0x5bc)))) {
            nextState = 2;
        } else if (subobj_find_active_grab(self, (int*)0) == 0) {
            nextState = 4;
        }
        break;
    }
    case 0xd:
        if ((*(int*)(slotRec + 0xc) == 0) ||
            (anim_is_clip_active(actor, *(int*)(slotRec + 0xc)) == 0)) {
            if (*(int*)(slotRec + 0xc) == 0) nextState = 2;
        } else if (anim_active_slot_userflags(*(int*)(actor + 4)) != 0) {
            nextState = 2;
        }
        break;
    default:
        break;
    }
    (void)param3;
    subobj_carry_object_tick();
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// 0x8015ab44  subobj_carry_dock_progress
// ---------------------------------------------------------------------------
// Advance the dock-approach ramp (+0x904) for the carried body. Once the ramp
// completes (>= 1.0) or state is already 5, latch to state 5 (docked). For
// terminal states (2/3/6) the ramp is reset. Otherwise, while contact speed
// (+0x52c) is high the ramp is eased back down; while low it climbs (with an
// extra boost while dock depth +0x944 is below threshold) until it saturates.
// Returns 1 when the ramp saturated (state forced to 5), else 0.
// ---------------------------------------------------------------------------
unsigned int subobj_carry_dock_progress(int entity, int* state)
{
    int s = *state;
    int rec = *(int*)(entity + 0x7c);
    float dt = FLOAT_806108ec *
               (FLOAT_80615fa8 /
                (float)((double)CONCAT44(0x43300000, (DAT_806109c0 ^ 0x80000000)) - DOUBLE_80615fd8));
    float ramp = *(float*)(rec + 0x904);

    if ((s == 5) || (FLOAT_80615fa8 <= ramp)) {
        *(float*)(rec + 0x904) = FLOAT_80615fac;
        *state = 5;
        return 1;
    }
    if ((s == 2) || (s == 6) || (s == 3)) {
        *(float*)(rec + 0x904) = FLOAT_80615fac;
        return 0;
    }
    if (FLOAT_80615fb0 <= ABS(*(float*)(rec + 0x52c))) {
        if ((FLOAT_80615fac < ramp) &&
            (*(float*)(rec + 0x904) = -(FLOAT_80615fe4 * dt - ramp),
             *(float*)(rec + 0x904) < FLOAT_80615fac)) {
            *(float*)(rec + 0x904) = FLOAT_80615fac;
        }
    } else {
        *(float*)(rec + 0x904) = FLOAT_80616110 * dt + ramp;
        if (*(float*)(rec + 0x944) < FLOAT_80615ff8) {
            *(float*)(rec + 0x904) = FLOAT_80616114 * dt + *(float*)(rec + 0x904);
        }
        if (FLOAT_80615fa8 <= *(float*)(rec + 0x904)) {
            *(float*)(rec + 0x904) = FLOAT_80615fac;
            *state = 5;
            return 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8015ac78  subobj_carry_nav_block_ptr
// ---------------------------------------------------------------------------
// Accessor: nav block base (rec+0x568). Callers FUN_8022fe70 (nav init/setup).
// ---------------------------------------------------------------------------
int subobj_carry_nav_block_ptr(int entity)
{
    return *(int*)(entity + 0x7c) + 0x568;
}

// ---------------------------------------------------------------------------
// 0x8015ac84  subobj_carry_nav_targets_valid
// ---------------------------------------------------------------------------
// Predicate: 1 if all three nav target handles (+0x56c, +0x578, +0x57c) are
// non-negative (a complete zone triple), else 0.
// ---------------------------------------------------------------------------
unsigned int subobj_carry_nav_targets_valid(int entity)
{
    int rec = *(int*)(entity + 0x7c);
    if (*(int*)(rec + 0x56c) < 0) return 0;
    if (*(int*)(rec + 0x578) < 0) return 0;
    if (*(int*)(rec + 0x57c) < 0) return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// 0x8015acb8  subobj_carry_nav_init
// ---------------------------------------------------------------------------
// Initialize the nav block (+0x568..+0x588): clear state, stamp the three zone
// target handles (p2/p3/p4), default kind 0x12, and clear the two node refs to
// -1. Callers FUN_8022fe70, FUN_80279374.
// ---------------------------------------------------------------------------
void subobj_carry_nav_init(int entity, unsigned int p2, unsigned int p3, unsigned int p4)
{
    int rec = *(int*)(entity + 0x7c);
    *(unsigned int*)(rec + 0x568) = 0;
    *(unsigned int*)(rec + 0x56c) = p2;
    *(unsigned int*)(rec + 0x570) = 0xffffffff;
    *(unsigned int*)(rec + 0x574) = 1;
    *(unsigned int*)(rec + 0x578) = p3;
    *(unsigned int*)(rec + 0x57c) = p4;
    *(unsigned int*)(rec + 0x580) = 0x12;
    *(unsigned int*)(rec + 0x584) = 0xffffffff;
    *(unsigned int*)(rec + 0x588) = 0xffffffff;
}

// ---------------------------------------------------------------------------
// 0x8015acf4  subobj_carry_nav_resolve_zones
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826f0 / FUN_8038273c). Resolve the nav zone anchors
// for the carried body: find the sound zone nearest the body's reference point
// and store its origin at +0x590/+0x59c; store the held-anim node's zone origin
// at +0x5a0.
// ---------------------------------------------------------------------------
void subobj_carry_nav_resolve_zones(void)
{
    int self = FUN_803826f0();
    int rec = *(int*)(self + 0x7c);
    int animBlock = subobj_carry_held_anim_ptr(self);
    float p[3];
    p[0] = *(float*)(self + 100)  + *(float*)(self + 0x1a8);
    p[1] = *(float*)(self + 0x68) + *(float*)(self + 0x1ac);
    p[2] = *(float*)(self + 0x6c) + *(float*)(self + 0x1b0);
    int z = soundpad_find_nearest_entity((double)FLOAT_80615fac, p);
    *(int*)(rec + 0x590) = *(int*)(DAT_80611b8c + z * 0x20);
    *(int*)(rec + 0x59c) = *(int*)(rec + 0x590);
    *(int*)(rec + 0x5a0) = *(int*)(DAT_80611b8c + *(int*)(animBlock + 8) * 0x20);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8015ad98  subobj_carry_zone_distance
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826f0 / FUN_8038273c). Compute the planar distance
// from the carried body's contact origin to its current nav zone center. The
// engine consumes the result in FP0 via the frame context; this reconstruction
// performs the same computation for its side effects (the sqrt sets FP0).
// ---------------------------------------------------------------------------
void subobj_carry_zone_distance(void)
{
    int self = FUN_803826f0();
    subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    int zone = DAT_80611b8c + FUN_803826f0() * 0x20;   // frame arg = zone id (low word)
    float* origin = (float*)subobj_get_contact_origin(*(unsigned int*)(self + 0x80));
    double dx = (double)(*(float*)(zone + 0xc) - origin[0]);
    int origin2 = subobj_get_contact_origin(*(unsigned int*)(self + 0x80));
    float dz = *(float*)(zone + 0x14) - *(float*)(origin2 + 8);
    math_sqrt((double)(float)(dx * dx + (double)(dz * dz)));
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8015aee8  subobj_carry_nav_segment_project
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826f0 / FUN_8038273c). When the carried body is far
// enough from its target zone center (relative to its world velocity) and has a
// valid previous zone, project the contact origin onto the segment between the
// previous and target zone centers (closest_pt_on_segment). Returns the segment
// state in r3 (consumed by the caller as a projection-success flag).
// ---------------------------------------------------------------------------
int subobj_carry_nav_segment_project(void)
{
    unsigned long long fr = FUN_803826f0();
    int self = (int)(fr >> 0x20);
    int* nav = (int*)(unsigned int)fr;
    float outPt[6];
    outPt[0] = FLOAT_80615fac;
    if (-1 < nav[2]) {
        float* vel = (float*)subobj_get_world_velocity(*(unsigned int*)(self + 0x80));
        float* org = (float*)subobj_get_contact_origin(*(unsigned int*)(self + 0x80));
        int zone = DAT_80611b8c + nav[2] * 0x20;
        float dz = *(float*)(zone + 0x14) - org[2];
        float dx = *(float*)(zone + 0xc) - org[0];
        if (((FLOAT_80615fa8 + *vel) * (FLOAT_80615fa8 + *vel) <= dx * dx + dz * dz) &&
            (-1 < nav[0])) {
            closest_pt_on_segment(org, (float*)(DAT_80611b8c + nav[0] * 0x20 + 0xc),
                                   (float*)(zone + 0xc), (float*)0, outPt);
        }
    }
    FUN_8038273c();
    return (int)outPt[0];
}

// ---------------------------------------------------------------------------
// 0x8015afe4  subobj_carry_steer_to_point
// ---------------------------------------------------------------------------
// Steer the carried body toward a target zone anchor: compute the normalized
// direction from the body's contact origin to the target point (fast rsqrt +
// one Newton refine), then dispatch to the steering solver: the "flag 1"
// airborne variant (FUN_8015bdd0) when the body's flag +0x58 bit0 is set, else
// the grounded solver (FUN_8015b248). Writes steering output to outDir.
// ---------------------------------------------------------------------------
void subobj_carry_steer_to_point(unsigned int p1, unsigned int p2, float* outDir)
{
    (void)p1; (void)p2;
    unsigned long long fr = __save_gpr_28();
    int self = (int)(fr >> 0x20);
    float* target = (float*)(unsigned int)fr;
    int rec = *(int*)(self + 0x7c);
    float* org = (float*)subobj_get_contact_origin(*(unsigned int*)(self + 0x80));
    float dir[3];
    dir[0] = target[0] - org[0];
    dir[1] = target[1] - org[1];
    dir[2] = target[2] - org[2];
    float d2 = dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1];
    if (FLOAT_80615ff4 < d2) {
        float r = 1.0f / SQRT(d2);
        r = r * -(FLOAT_80615fe4 * d2 * r * r - FLOAT_80615fe8);
        float scale = FLOAT_80615fa8 * r * -(FLOAT_80615fe4 * d2 * r * r - FLOAT_80615fe8);
        dir[0] = dir[0] * scale;
        dir[1] = dir[1] * scale;
        dir[2] = dir[2] * scale;
    }
    if ((*(unsigned int*)(rec + 0x58) & 1) == 0) {
        subobj_carry_steer_ground((unsigned int)self, (unsigned int)(uintptr_t)dir, outDir);
    } else {
        subobj_carry_steer_airborne((unsigned int)self, (unsigned int)(uintptr_t)dir, (int)(intptr_t)outDir);
    }
    __restore_gpr_28();
}

// ===========================================================================
// batch_432 : carried-body steering solvers + subsystem init/pool management
// Address range: 0x8015b11c - 0x8015d14c | 2026-09-06
// ---------------------------------------------------------------------------
// The steer solvers (subobj_carry_steer_ground / _airborne / _fallback) turn a
// desired world direction into control outputs (pitch/roll/yaw + throttle) for
// a carried vehicle-style body, and subobj_carry_weapon_ai_aim is the AI aim/
// fire solver for a carried weapon. The tail (0x8015ce58..) is the subsystem
// lifecycle: event-handler registration, pool allocation/init, per-record
// pop/free, and sound-bank release. These are frame-registered where noted via
// the FUN_803826xx (frame-in) / FUN_803827xx (frame-out) intrinsics.
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x8015b11c  subobj_carry_steer_fallback
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826f0 / FUN_8038273c). When there is no valid nav
// zone to steer toward, derive a steering direction from the body's own object
// matrix forward axis (x/z columns, normalized) and hand it to the appropriate
// steer solver: airborne (FUN_8015bdd0) when flag +0x58 bit0 is set, else the
// grounded solver (FUN_8015b248).
// ---------------------------------------------------------------------------
void subobj_carry_steer_fallback(void)
{
    unsigned long long fr = FUN_803826f0();
    int self = (int)(fr >> 0x20);
    int rec = *(int*)(self + 0x7c);
    // r3-carried scratch sign (decompiler in_f1); use +1.0 magnitude basis.
    float sign = 1.0f;
    float* m = (float*)subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    float dir[3];
    dir[0] = m[8];
    dir[1] = m[10];
    float d2 = dir[1] * dir[1] + dir[0] * dir[0] + FLOAT_80615fac * FLOAT_80615fac;
    float r = 1.0f / SQRT(d2);
    float t = FLOAT_80615fe4 * d2;
    r = r * -(t * r * r - FLOAT_80615fe8);
    float scale = sign * r * -(t * r * r - FLOAT_80615fe8);
    dir[0] = dir[0] * scale;
    dir[1] = FLOAT_80615fac * scale;
    dir[2] = m[10] * scale;
    // Pack (dir.x, dir.mid, dir.z) as the steer input at &dir.
    float packed[3]; packed[0] = dir[0]; packed[1] = dir[1]; packed[2] = dir[2];
    if ((*(unsigned int*)(rec + 0x58) & 1) == 0) {
        subobj_carry_steer_ground((unsigned int)self, (unsigned int)(uintptr_t)packed, (float*)(uintptr_t)fr);
    } else {
        subobj_carry_steer_airborne((unsigned int)self, (unsigned int)(uintptr_t)packed, (int)(unsigned int)fr);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8015b248  subobj_carry_steer_ground
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826d4 / FUN_80382720). Grounded steering solver for
// a carried body. Projects the desired direction against the object matrix
// basis to derive a yaw error (asin of the cross term) scaled by a config gain
// (+0x50 rec, offset +0x10), writing the yaw control to out[0]. Then, on the
// owning frame slot, probes the ground and the rotor "feelers": a downward
// swept-AABB / floor query fills a ground clearance factor (+0x954), and per
// rotor (table rec+0x174, stride 100) two raycasts (down and along-blade)
// compute contact factors (+0x954 + i*4). Finally it mixes those feeler factors
// with per-axis gains (+0x958..+0x964) into the roll/pitch/throttle controls
// (out[0], out[4]/out[5]).
// ---------------------------------------------------------------------------
void subobj_carry_steer_ground(unsigned int p1, unsigned int p2, float* out)
{
    (void)p1; (void)p2;
    unsigned long long fr = FUN_803826d4();
    int self = (int)(fr >> 0x20);
    float* dirIn = (float*)(unsigned int)fr;
    int rec = *(int*)(self + 0x7c);
    float sign = 1.0f;                  // in_f1 magnitude sign basis
    float* m = (float*)subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    int vel = subobj_get_world_velocity(*(unsigned int*)(self + 0x80));

    // desired direction (optionally reversed) normalized in the x/z plane
    float dx, dz;
    if ((*(unsigned int*)(rec + 0x8f8) & 1) == 0) { dx = dirIn[0];  dz = dirIn[2]; }
    else                                          { dx = -dirIn[0]; dz = -dirIn[2]; }
    float d2 = dz * dz + (dx * dx + FLOAT_80615fac);
    if (FLOAT_80615ff4 < d2) {
        float rr = 1.0f / SQRT(d2);
        rr = rr * -(FLOAT_80615fe4 * d2 * rr * rr - FLOAT_80615fe8);
        float s = FLOAT_80615fa8 * rr * -(FLOAT_80615fe4 * d2 * rr * rr - FLOAT_80615fe8);
        dx = dx * s; dz = dz * s;
    }
    // object forward (x/z of the 3rd column), normalized
    float fx = m[8], fz = m[10];
    float f2 = fz * fz + (fx * fx + FLOAT_80615fac);
    if (FLOAT_80615ff4 < f2) {
        float rr = 1.0f / SQRT(f2);
        rr = rr * -(FLOAT_80615fe4 * f2 * rr * rr - FLOAT_80615fe8);
        float s = FLOAT_80615fa8 * rr * -(FLOAT_80615fe4 * f2 * rr * rr - FLOAT_80615fe8);
        fx = fx * s; fz = fz * s;
    }
    // yaw error via asin of the planar dot, clamped
    float dot = dx * fx + dz * fz;
    float clamped = dot;
    if (FLOAT_80615fa8 < clamped) clamped = FLOAT_80615fa8;
    if (FLOAT_80616024 <= clamped) { if (FLOAT_80615fa8 < dot) dot = FLOAT_80615fa8; }
    else                          { dot = FLOAT_80616024; }
    double yaw = (double)math_asin((double)dot);
    float gain = *(float*)(*(int*)(rec + 0x50) + 0x10);
    // signed cross term determines yaw direction
    float cross = dx * m[0] + dz * m[2];
    float yawCtrl = (float)((double)(float)(yaw * -(double)((cross < 0) ? -1.0f : 1.0f)) * sign);
    if (gain <= ABS(yawCtrl)) out[0] = (yawCtrl < 0) ? -1.0f : 1.0f;
    else                      out[0] = yawCtrl / gain;

    // save+clear collision-response bits during feeler raycasts
    unsigned int saved = *(unsigned int*)(self + 0x28) & 0x60;
    *(unsigned int*)(self + 0x28) = *(unsigned int*)(self + 0x28) & ~saved;

    // ground feeler (only on this body's frame phase)
    if ((*(unsigned int*)(self + 0x2c) & 7) == (DAT_80611928 & 7)) {
        float spd = FLOAT_80615fa8;
        if (FLOAT_80615fa8 < ABS(*(float*)(rec + 0x52c))) spd = ABS(*(float*)(rec + 0x52c));
        float probeLen = FLOAT_80616018 * spd;
        float velY = (float)((double)(*(float*)(vel + 8) - FLOAT_80615fe4) * (double)sign);
        float velScale = FLOAT_80616008 * *(float*)(vel + 4);
        float probe[3];
        probe[0] = -(float)((double)m[4] * (double)velScale - (double)(m[8]  * velY + *(float*)(self + 0x1a8)));
        probe[1] = -(float)((double)m[5] * (double)velScale - (double)(m[9]  * velY + *(float*)(self + 0x1ac)));
        probe[2] = -(float)((double)m[6] * (double)velScale - (double)(m[10] * velY + *(float*)(self + 0x1b0)));
        int room = RAY_TRAVERSE_PORTALS((double)velScale, (double)m[4],
                                        self + 100, *(int*)(self + 0x34), probe, 0);
        if ((0 < room) && (room < 0xc9)) {
            float base[3];
            base[0] = probe[0] + *(float*)(self + 100);
            base[1] = probe[1] + *(float*)(self + 0x68);
            base[2] = probe[2] + *(float*)(self + 0x6c);
            float len = probeLen * sign;
            float sweep[3];
            sweep[0] = m[8]  * len;
            sweep[1] = m[9]  * len;
            sweep[2] = m[10] * len;
            float* hit = (float*)0;
            subobj_constraint_pool_reset();
            float hitPos[3]; hitPos[0] = base[0]; hitPos[1] = base[1]; hitPos[2] = base[2];
            float hitNorm[3]; hitNorm[0] = FLOAT_80615fac; hitNorm[1] = FLOAT_80615fa8; hitNorm[2] = FLOAT_80615fac;
            int hitOk = SWEPT_AABB_QUERY(room, base, sweep, 0, (int*)&hit, 0xfbbfff78, 0);
            (void)hitOk;
            if (hit != (float*)0) {
                hitPos[0] = hit[0]; hitPos[1] = hit[1]; hitPos[2] = hit[2];
                hitNorm[0] = hit[3]; hitNorm[1] = hit[4]; hitNorm[2] = hit[5];
            }
            if (hit == (float*)0) {
                *(float*)(rec + 0x954) = FLOAT_80615fac;
                float cs = (float)(((int)ABS(*(float*)(rec + 0x52c)) >> 1) + 0x1fc00000);
                if (cs < FLOAT_80615fa8) cs = FLOAT_80615fa8;
                float downLen = (float)((double)(FLOAT_80615ff0 * cs) * (double)sign);
                float dp[3];
                dp[0] = base[0] + m[8]  * downLen;
                dp[1] = base[1] + m[9]  * downLen;
                dp[2] = base[2] + m[10] * downLen;
                double fq = (double)FLOOR_QUERY((double)FLOAT_80616040,
                                                (double)FLOAT_80615fb8, (int)(intptr_t)dp, 0, (int*)0, 0);
                if ((double)FLOAT_80616010 == fq) {
                    *(float*)(rec + 0x904) = FLOAT_80615fa8;
                    *(float*)(rec + 0x954) = FLOAT_80615fa8;
                }
            } else {
                float rx = hitPos[0] - base[0];
                float ry = hitPos[1] - base[1];
                float rz = hitPos[2] - base[2];
                float frac = (float)((double)(float)(((int)(rz * rz + rx * rx + ry * ry) >> 1) + 0x1fc00000) / (double)probeLen);
                float c = frac; if (FLOAT_80615fa8 < c) c = FLOAT_80615fa8;
                float c2 = FLOAT_80615fac; if ((FLOAT_80615fac <= c) && (c2 = frac, FLOAT_80615fa8 < frac)) c2 = FLOAT_80615fa8;
                *(float*)(rec + 0x954) = (FLOAT_80615fa8 - hitNorm[1]) * (FLOAT_80615fa8 - c2);
            }
        }
    }

    // per-rotor feelers (table rec+0x174, stride 100), 2 rays each
    int phase = 0;
    float feelLen = (float)((double)FLOAT_80615ff0 * (double)sign);
    int rotorOff = 0;
    for (int ri = 0; ri < *(int*)(*(int*)(rec + 0x50) + 0x9c); ri = ri + 1) {
        int rot = rec + rotorOff + 0x174;
        float rotorSign = (*(float*)(rot + 0x28) < 0) ? -1.0f : 1.0f;
        if (sign == rotorSign) {
            if ((DAT_80611928 & 7) == ((*(int*)(self + 0x2c) + phase) & 7)) {
                int slot = (FLOAT_80615fac <= *(float*)(rot + 0x28)) ? 2 : 1;
                float rd[3];
                float bladeSign = (*(float*)(rot + 0x28) < 0) ? -1.0f : 1.0f;
                rd[0] = m[0] * bladeSign; rd[1] = m[1] * bladeSign; rd[2] = m[2] * bladeSign;
                float hp[3], hn[3];
                int rc = SCENE_RAYCAST(*(int*)(self + 0x34), (int*)(rot + 0x40), rd, 0, hp, hn, (int*)0, 0xfbbfff78, 0, (int*)0);
                if (rc == 0) {
                    *(float*)(rec + slot * 4 + 0x954) = FLOAT_80615fac;
                } else {
                    float rx = hp[0] - *(float*)(rot + 0x40);
                    float ry = hp[1] - *(float*)(rot + 0x44);
                    float rz = hp[2] - *(float*)(rot + 0x48);
                    float frac = (float)(((int)(rz * rz + rx * rx + ry * ry) >> 1) + 0x1fc00000);
                    float c = frac; if (FLOAT_80615fa8 < frac) c = FLOAT_80615fa8;
                    float c2 = FLOAT_80615fac; if ((FLOAT_80615fac <= c) && (c2 = frac, FLOAT_80615fa8 < frac)) c2 = FLOAT_80615fa8;
                    *(float*)(rec + slot * 4 + 0x954) = (FLOAT_80615fa8 - hn[1]) * (FLOAT_80615fa8 - c2);
                }
            }
            if ((DAT_80611928 & 7) == ((*(int*)(self + 0x2c) + phase + 1) & 7)) {
                int slot = (FLOAT_80615fac <= *(float*)(rot + 0x28)) ? 4 : 3;
                float rd[3];
                if ((*(unsigned int*)(rot + 8) & 2) == 0) {
                    rd[0] = m[8]; rd[1] = m[9]; rd[2] = m[10];
                } else {
                    rd[1] = FLOAT_80615fac;
                    unsigned int ti = (unsigned int)(FLOAT_806160ec * *(float*)(rot + 0x1c) + FLOAT_80615fe4);
                    rd[0] = (&DAT_80583228)[ti & 0x7ff];
                    rd[2] = *(float*)((int)&DAT_80583228 + ((ti + 0x200) * 4 & 0x1ffc));
                    mtx4_transform_vector_alias(m, rd);
                }
                rd[0] = (float)((double)rd[0] * (double)feelLen);
                rd[1] = (float)((double)rd[1] * (double)feelLen);
                rd[2] = (float)((double)rd[2] * (double)feelLen);
                float hp[3], hn[3];
                int rc = SCENE_RAYCAST(*(int*)(self + 0x34), (int*)(rot + 0x40), rd, 0, hp, hn, (int*)0, 0xfbbfff78, 0, (int*)0);
                if (rc == 0) {
                    *(float*)(rec + slot * 4 + 0x954) = FLOAT_80615fac;
                } else {
                    float rx = hp[0] - *(float*)(rot + 0x40);
                    float ry = hp[1] - *(float*)(rot + 0x44);
                    float rz = hp[2] - *(float*)(rot + 0x48);
                    float frac = (float)(((int)(rz * rz + rx * rx + ry * ry) >> 1) + 0x1fc00000) * FLOAT_80616008;
                    float c = frac; if (FLOAT_80615fa8 < frac) c = FLOAT_80615fa8;
                    float c2 = FLOAT_80615fac; if ((FLOAT_80615fac <= c) && (c2 = frac, FLOAT_80615fa8 < frac)) c2 = FLOAT_80615fa8;
                    *(float*)(rec + slot * 4 + 0x954) = (FLOAT_80615fa8 - hn[1]) * (FLOAT_80615fa8 - c2);
                }
            }
        }
        phase = phase + 2;
        rotorOff = rotorOff + 100;
    }

    // mix feeler factors into roll/throttle controls
    out[0] = FLOAT_80616120 * *(float*)(rec + 0x95c) + out[0];
    out[0] = -(FLOAT_80616120 * *(float*)(rec + 0x958) - out[0]);
    if (*(float*)(rec + 0x964) <= *(float*)(rec + 0x960)) {
        out[0] = -(FLOAT_80616124 * *(float*)(rec + 0x960) - out[0]);
    } else {
        out[0] = FLOAT_80616124 * *(float*)(rec + 0x964) + out[0];
    }
    *(unsigned int*)(self + 0x28) = *(unsigned int*)(self + 0x28) | saved;

    // apply the yaw magnitude to the correct throttle channel
    double yawMag = yaw;
    if ((double)FLOAT_80615fa8 < yaw) yawMag = (double)FLOAT_80615fa8;
    if ((double)FLOAT_80616024 <= yawMag) { if ((double)FLOAT_80615fa8 < yaw) yaw = (double)FLOAT_80615fa8; }
    else                                   { yaw = (double)FLOAT_80616024; }
    if ((double)FLOAT_80615fac <= yaw) {
        if ((double)sign <= (double)FLOAT_80615fac) out[4] = (float)((double)out[4] + yaw);
        else                                        out[5] = (float)((double)out[5] + yaw);
    } else if (*(int*)(rec + 0x60) == 1) {
        out[4] = (float)((double)out[4] - yaw);
    } else if (*(int*)(rec + 0x60) == 2) {
        out[5] = (float)((double)out[5] - yaw);
    }
    FUN_80382720();
}

// ---------------------------------------------------------------------------
// 0x8015bdd0  subobj_carry_steer_airborne
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826f0 / FUN_8038273c). Airborne banking steer
// solver: like the grounded solver it derives a yaw error via asin of the
// planar dot between the (optionally reversed) desired direction and the object
// forward axis, but instead of ground feelers it splits the resulting turn into
// the two rotor throttle channels (out[iA]/out[iB]) to produce a bank, applying
// a dead-zone (FLOAT_80616128) and a proportional falloff (FLOAT_80616130) with
// a hard reversal past FLOAT_8061612c.
// ---------------------------------------------------------------------------
void subobj_carry_steer_airborne(unsigned int p1, unsigned int p2, int out)
{
    (void)p1; (void)p2;
    unsigned long long fr = FUN_803826f0();
    int self = (int)(fr >> 0x20);
    float* dirIn = (float*)(unsigned int)fr;
    int rec = *(int*)(self + 0x7c);
    double throttle = 1.0;              // in_f1 scratch (frame-supplied turn rate)
    float* m = (float*)subobj_get_object_matrix(*(unsigned int*)(self + 0x80));

    float dx, dz;
    if ((*(unsigned int*)(rec + 0x8f8) & 1) == 0) { dx = dirIn[0];  dz = dirIn[2]; }
    else                                          { dx = -dirIn[0]; dz = -dirIn[2]; }
    float d2 = dz * dz + (dx * dx + FLOAT_80615fac);
    if (FLOAT_80615ff4 < d2) {
        float rr = 1.0f / SQRT(d2);
        rr = rr * -(FLOAT_80615fe4 * d2 * rr * rr - FLOAT_80615fe8);
        float s = FLOAT_80615fa8 * rr * -(FLOAT_80615fe4 * d2 * rr * rr - FLOAT_80615fe8);
        dx = dx * s; dz = dz * s;
    }
    float fx = m[8], fz = m[10];
    float f2 = fz * fz + (fx * fx + FLOAT_80615fac);
    if (FLOAT_80615ff4 < f2) {
        float rr = 1.0f / SQRT(f2);
        rr = rr * -(FLOAT_80615fe4 * f2 * rr * rr - FLOAT_80615fe8);
        float s = FLOAT_80615fa8 * rr * -(FLOAT_80615fe4 * f2 * rr * rr - FLOAT_80615fe8);
        fx = fx * s; fz = fz * s;
    }
    float dot = dx * fx + dz * fz;
    float clamped = dot;
    if (FLOAT_80615fa8 < clamped) clamped = FLOAT_80615fa8;
    if (FLOAT_80616024 <= clamped) { if (FLOAT_80615fa8 < dot) dot = FLOAT_80615fa8; }
    else                          { dot = FLOAT_80616024; }
    double yaw = (double)math_asin((double)dot);
    float cross = dx * m[0] + dz * m[2];
    float turn = (float)(yaw * -(double)((cross < 0) ? -1.0f : 1.0f));

    int iA, iB;
    if (((turn < 0) ? -turn : turn) <= FLOAT_80615fac) { iB = 1; iA = 3; }
    else                                               { iA = 1; iB = 3; }

    double amt = throttle;
    if ((double)FLOAT_80615fa8 < throttle) amt = (double)FLOAT_80615fa8;
    if ((double)FLOAT_80616024 <= amt) { if ((double)FLOAT_80615fa8 < throttle) throttle = (double)FLOAT_80615fa8; }
    else                               { throttle = (double)FLOAT_80616024; }

    float turnAbs = ABS(turn);
    bool active = FLOAT_80616128 <= turnAbs;
    *(float*)(out + iA * 4) = (float)((double)*(float*)(out + iA * 4) + throttle);
    if (active) {
        if (FLOAT_8061612c <= turnAbs) {
            throttle = -throttle;
        } else {
            throttle = (double)(float)(throttle * (double)((turnAbs - FLOAT_80616128) / FLOAT_80616130));
        }
    }
    *(float*)(out + iB * 4) = (float)((double)*(float*)(out + iB * 4) + throttle);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8015c07c  subobj_carry_weapon_ai_aim
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826d0 / FUN_8038271c). AI aim/fire controller for a
// carried weapon slot. Resolves the grabbed weapon and its def/aim-tuning
// block, computes the range and line-of-sight to the target (scene_raycast),
// runs a fire-readiness timer (+0xa00), and when it has line of sight it drives
// the weapon's yaw/pitch aim (piVar13[7]/[9]) toward the target with per-frame
// gains and optional sine-table wobble for a "human" aim, clamped to the def's
// aim limits. It also manages a small fire-state machine (+0xc64) and, for
// spread weapons, tracks a recoil/settle pattern (piVar13[0x13]/[0x14]). The
// r3 result it leaves is consumed by the caller as a nav "sub-state" value.
//
// This is a large faithful reconstruction; the heavy per-frame aim/recoil math
// is kept, and register-spill shadow locals are dropped.
// ---------------------------------------------------------------------------
void subobj_carry_weapon_ai_aim(unsigned int p1, unsigned int p2, int outCtrl)
{
    (void)p1; (void)p2;
    unsigned long long fr = FUN_803826d0();
    int self  = (int)(fr >> 0x20);
    int actor = (int)fr;
    int rec    = *(int*)(actor + 0x7c);
    int actRec = *(int*)(self + 0x7c);
    int held = *(int*)(rec + 4);
    if (((held == 0) || ((*(unsigned int*)(held + 0x28) & 0x80000000) != 0)) ||
        ((*(int*)(held + 0x20) == 8) && ((*(unsigned int*)(*(int*)(held + 0x7c) + 0x24) & 8) != 0))) {
        FUN_8038271c();
        return;
    }
    int slotRec = actRec + (unsigned int)*(unsigned char*)(rec + 0xc0c) * 0x34 + 0x70;
    int* wslot = *(int**)(actRec + *(int*)(slotRec + 0x2c) * 0x10 + 0x144);
    int weapon = *wslot;
    weapon_get_def_by_index(wslot[3]);
    int tune = FUN_801a72f8(DAT_80610ba0 + *(int*)(wslot[0x1b] + 0x30) * 0x29c, 0);

    double dt = (double)(FLOAT_806108ec *
                (FLOAT_80615fa8 /
                 (float)((double)CONCAT44(0x43300000, (DAT_806109c0 ^ 0x80000000)) - DOUBLE_80615fd8)));
    bool freeAim = false;
    if (((*(unsigned int*)(actRec + 0x58) & 1) != 0) && (*(int*)(slotRec + 0x30) == 0)) freeAim = true;

    float aimPt[3];
    FUN_802405e8(*wslot, aimPt);
    int hp0 = *(int*)(rec + 4);
    double tx = (double)(*(float*)(hp0 + 100)  + *(float*)(hp0 + 0x1a8));
    double ty = (double)(*(float*)(hp0 + 0x68) + *(float*)(hp0 + 0x1ac));
    double tz = (double)(*(float*)(hp0 + 0x6c) + *(float*)(hp0 + 0x1b0));
    float rel[3];
    rel[0] = (float)(tx - (double)aimPt[0]);
    rel[1] = (float)(ty - (double)aimPt[1]);
    rel[2] = (float)(tz - (double)aimPt[2]);
    double dist = math_sqrt((double)(rel[2] * rel[2] + rel[0] * rel[0] + rel[1] * rel[1]));

    if ((double)FLOAT_80616054 <= ABS(dist)) {
        int hp = *(int*)(rec + 4);
        float lead = (float)((double)((float)((double)CONCAT44(0x43300000, (DAT_806109c0 ^ 0x80000000)) - DOUBLE_80615fd8) *
                                      *(float*)(tune + 0x1c)) / dist);
        tx = (double)(float)(tx + (double)(*(float*)(hp + 0xf8)  * lead));
        ty = (double)(float)(ty + (double)(*(float*)(hp + 0xfc)  * lead));
        tz = (double)(float)(tz + (double)(*(float*)(hp + 0x100) * lead));
    }

    unsigned int los;
    int hp = *(int*)(rec + 4);
    if (*(int*)(hp + 0x20) == 8) {
        los = aiactor_retarget_visible_teammate();
    } else if ((((double)(float)wslot[0x19] < dist) && ((double)FLOAT_80615fac < (double)(float)wslot[0x19])) ||
               (dist < (double)(float)wslot[0x31])) {
        los = 0;
    } else {
        unsigned int flags = *(unsigned int*)(hp + 0x28) & 0x60;
        *(unsigned int*)(hp + 0x28) = *(unsigned int*)(hp + 0x28) & ~flags;
        int rc = SCENE_RAYCAST(*(int*)(self + 0x34), (int*)aimPt, rel, 0, (float*)0, (float*)0, (int*)0, 0x410278, 0, (int*)0);
        los = (unsigned int)countLeadingZeros((unsigned int)rc) >> 5;
        *(unsigned int*)(*(int*)(rec + 4) + 0x28) = *(unsigned int*)(*(int*)(rec + 4) + 0x28) | flags;
    }

    unsigned int prevFlags = *(unsigned int*)(rec + 0x28);
    *(unsigned int*)(rec + 0x28) = prevFlags & 0xfffffffd;
    if ((los != 0) || (FLOAT_80615fac < *(float*)(rec + 0xa00))) {
        // clamp-to-fire-count gate (uses a rounded config count); simplified
        // faithfully: allow fire arming when AI difficulty permits.
        if (((FUN_802a5840(actor) != 0)) &&
            ((*(unsigned int*)(rec + 0x28) = *(unsigned int*)(rec + 0x28) | 2, (float)wslot[0x71] <= FLOAT_80615fac))) {
            *(unsigned int*)(outCtrl + 0x18) = *(unsigned int*)(outCtrl + 0x18) | 0x80;
        }
    }
    if (((prevFlags & 2) == 0) && ((*(unsigned int*)(rec + 0x28) & 2) != 0)) {
        weapon_compute_fire_origin(weapon, 0, 0, (float*)0, (float*)(rec + 0x9f0));
        if ((*(float*)(rec + 0xa00) < FLOAT_80615fac) || (FLOAT_80615fa8 < *(float*)(rec + 0xa00)))
            *(float*)(rec + 0xa00) = FLOAT_80615fac;
        *(int*)(rec + 0xc64) = (los == 0) ? 2 : 0;
        *(float*)(rec + 0x9d8) = FLOAT_80615fac;
    }

    if (los == 0) {
        *(float*)(rec + 0xa00) = -(float)((double)FLOAT_80616134 * dt - (double)*(float*)(rec + 0xa00));
        if (*(float*)(rec + 0xa00) < FLOAT_80615fac) *(float*)(rec + 0xa00) = FLOAT_80615fac;
    } else {
        *(float*)(rec + 0xa00) = (float)((double)*(float*)(rec + 0xa00) + dt);
        if (FLOAT_80615fa8 < *(float*)(rec + 0xa00)) *(float*)(rec + 0xa00) = FLOAT_80615fa8;
    }

    if ((los != 0) || (FLOAT_80615fac < *(float*)(rec + 0xa00))) {
        float muzzle[3], fwd[3];
        weapon_compute_fire_origin(weapon, wslot[0x1c], 0, muzzle, fwd);
        float aRel[3];
        aRel[0] = (float)(tx - (double)muzzle[0]);
        aRel[1] = (float)(ty - (double)muzzle[1]);
        aRel[2] = (float)(tz - (double)muzzle[2]);
        float a2 = aRel[2] * aRel[2] + aRel[0] * aRel[0] + aRel[1] * aRel[1];
        if (FLOAT_80615ff4 < a2) {
            float rr = 1.0f / SQRT(a2);
            rr = rr * -(FLOAT_80615fe4 * a2 * rr * rr - FLOAT_80615fe8);
            float s = FLOAT_80615fa8 * rr * -(FLOAT_80615fe4 * a2 * rr * rr - FLOAT_80615fe8);
            aRel[0] = aRel[0] * s; aRel[1] = aRel[1] * s; aRel[2] = aRel[2] * s;
        }
        float tgtPitch, tgtYaw, curPitch, curYaw;
        dir_to_angles(aRel, &tgtPitch, &tgtYaw);
        dir_to_angles(fwd,  &curPitch, &curYaw);
        // shortest-arc yaw error
        float y1 = tgtYaw - (curYaw + FLOAT_80616028);
        float ym = ABS(tgtYaw - curYaw);
        float y2 = tgtYaw - (curYaw - FLOAT_80616028);
        float yawErr;
        if (ABS(y1) <= ym) { yawErr = (ABS(y1) < ABS(y2)) ? y1 : (tgtYaw - curYaw); }
        else               { yawErr = (ym < ABS(y2)) ? (tgtYaw - curYaw) : y2; }
        *(float*)(rec + 0xae0) = yawErr;
        // shortest-arc pitch error
        double p1e = (double)(tgtPitch - (curPitch + FLOAT_80616028));
        double pm  = ABS((double)(tgtPitch - curPitch));
        double p2e = (double)(tgtPitch - (curPitch - FLOAT_80616028));
        double pitchErr;
        if (ABS(p1e) <= pm) { pitchErr = (ABS(p1e) < ABS(p2e)) ? p1e : (double)(tgtPitch - curPitch); }
        else                { pitchErr = (pm < ABS(p2e)) ? (double)(tgtPitch - curPitch) : p2e; }

        float yawRate, pitchRate;
        if (FUN_802a5840(actor) == 0) {
            yawRate = (float)wslot[0x17] * ((yawErr < 0) ? -1.0f : 1.0f) *
                      (float)(((int)ABS(yawErr) >> 1) + 0x1fc00000);
            pitchRate = (float)wslot[0x16] * -(((float)pitchErr < 0) ? -1.0f : 1.0f) *
                        (float)(((int)(float)ABS(pitchErr) >> 1) + 0x1fc00000);
        } else {
            float ye = *(float*)(rec + 0xae0);
            float yc = ye; if (FLOAT_8061613c < ye) yc = FLOAT_8061613c;
            float yv = FLOAT_80616138;
            if ((FLOAT_80616138 <= yc) && (yv = ye, FLOAT_8061613c < ye)) yv = FLOAT_8061613c;
            yawRate = (float)wslot[0x17] * yv;
            double pc = pitchErr; if ((double)FLOAT_80616050 < pitchErr) pc = (double)FLOAT_80616050;
            if ((double)FLOAT_80616140 <= pc) { pc = pitchErr; if ((double)FLOAT_80616050 < pitchErr) pc = (double)FLOAT_80616050; }
            else                              { pc = (double)FLOAT_80616140; }
            pitchRate = (float)((double)(float)wslot[0x16] * -pc);
        }
        wslot[9] = (int)((float)wslot[9] + (float)(dt * (double)yawRate));
        wslot[7] = (int)((float)wslot[7] + (float)(dt * (double)pitchRate));

        // clamp yaw/pitch to the def's configured limits
        float pLo = (float)wslot[0xd], pHi = (float)wslot[0xe];
        if (pLo != pHi) {
            float v = (float)wslot[7];
            float t = v; if (pHi < v) t = pHi;
            if ((pLo <= t) && (pLo = v, pHi < v)) pLo = pHi;
            wslot[7] = (int)pLo;
        }
        float yLo = (float)wslot[0xf], yHi = (float)wslot[0x10];
        if (yLo != yHi) {
            float v = (float)wslot[9];
            float t = v; if (yHi < v) t = yHi;
            if ((yLo <= t) && (yLo = v, yHi < v)) yLo = yHi;
            wslot[9] = (int)yLo;
        }

        // fire-state machine (+0xc64): 1 -> ready, 2 -> firing, 0 -> idle
        int fs = *(int*)(rec + 0xc64);
        if (fs == 1) {
            if (los == 0) { *(int*)(rec + 0xc64) = 2; *(float*)(rec + 0x9d8) = FLOAT_80615fac; }
        } else if (fs < 1) {
            if (((-1 < fs) && (FLOAT_80615fec <= *(float*)(rec + 0x9d8))) &&
                (ABS((double)*(float*)(rec + 0xae0)) < (double)FLOAT_80616018) &&
                (freeAim || (ABS(pitchErr) < (double)FLOAT_80616018))) {
                *(int*)(rec + 0xc64) = 1;
            }
        } else if (fs < 3) {
            if (los != 0) { *(int*)(rec + 0xc64) = 0; *(float*)(rec + 0x9d8) = FLOAT_80615fac; }
        }
    }
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// 0x8015ce58  subobj_register_event_handlers
// ---------------------------------------------------------------------------
// Register the sub-object system's per-frame event handlers on entity class
// 0x400000: subtype 2 -> rotor/bone pose, 3 -> physics tick, 4 -> grab IK,
// 1 -> the render/HUD dispatch (FUN_8015d3f8). Called from game_tick_update.
// ---------------------------------------------------------------------------
void subobj_register_event_handlers(void)
{
    event_set_handler(0x400000, 2, (void*)&subobj_carry_rotor_pose);
    event_set_handler(0x400000, 3, (void*)&subobj_carry_physics_tick);
    event_set_handler(0x400000, 4, (void*)&subobj_grab_ik_solve);
    event_set_handler(0x400000, 1, (void*)&subobj_carry_destroy);
}

// ---------------------------------------------------------------------------
// 0x8015cec4  subobj_pool_alloc_all
// ---------------------------------------------------------------------------
// Allocate the sub-object pools (records, free-list, two scratch banks) from
// the entity allocator, then initialize them. Called from entity_system_alloc.
// ---------------------------------------------------------------------------
int subobj_pool_alloc_all(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                          unsigned long long p4, unsigned long long p5, unsigned long long p6,
                          unsigned long long p7, unsigned long long p8, unsigned int p9,
                          unsigned int p10, unsigned int p11, unsigned int p12, unsigned int p13,
                          unsigned int p14, unsigned int p15, unsigned int p16)
{
    (void)p9; (void)p10;
    DAT_806121dc = MEM_ALLOC16(p1, p2, p3, p4, p5, p6, p7, p8, 0x25c0, 1, p11, p12, p13, p14, p15, p16);
    DAT_806121e0 = MEM_ALLOC16(p1, p2, p3, p4, p5, p6, p7, p8, 0x10,   1, p11, p12, p13, p14, p15, p16);
    DAT_806121d4 = MEM_ALLOC16(p1, p2, p3, p4, p5, p6, p7, p8, 0x170,  1, p11, p12, p13, p14, p15, p16);
    DAT_806121d0 = MEM_ALLOC16(p1, p2, p3, p4, p5, p6, p7, p8, 0x170,  1, p11, p12, p13, p14, p15, p16);
    subobj_pool_init();
    return DAT_806121dc;
}

// ---------------------------------------------------------------------------
// 0x8015cf24  subobj_pool_init
// ---------------------------------------------------------------------------
// Reset the sub-object pool bookkeeping (free count, active-list head, cursors),
// clear the two scratch banks, seed the 4-entry free-list to point into the
// record bank (0x970 stride), and register the rotor-downwash particle effect
// (a particle def + emitter with its size/velocity/color keyframes). Called
// from entity_system_reset.
// ---------------------------------------------------------------------------
void subobj_pool_init(void)
{
    DAT_806121e8 = 0;
    DAT_806121e4 = (void*)0;
    DAT_806121cc = 0;
    memset_wrapper(DAT_806121d4, 0, 0x170);
    memset_wrapper(DAT_806121dc, 0, 0x25c0);
    for (DAT_806121d8 = 0; DAT_806121d8 < 4; DAT_806121d8 = DAT_806121d8 + 1) {
        *(unsigned int*)(DAT_806121e0 + DAT_806121d8 * 4) = DAT_806121dc + DAT_806121d8 * 0x970;
    }
    PARTICLE_DEF_CREATE((double)FLOAT_80616150, (double)FLOAT_80616150, &DAT_80589430, 0, 0, 1, 0x323, 0);
    PARTICLE_DEF_SIZE_KF((double)FLOAT_80616154, (double)FLOAT_80616158, (double)FLOAT_8061615c,
                         (double)FLOAT_80616154, 0x12121300, 0x10101100, 0xc0c0d00);
    PARTICLE_DEF_VEL((double)FLOAT_80616160, (double)FLOAT_80616158, (double)FLOAT_80616164,
                     (double)FLOAT_80616158, (double)FLOAT_80616150, (double)FLOAT_80616154);
    PARTICLE_DEF_SIZE_SE((double)FLOAT_80616158, (double)FLOAT_80616168, (double)FLOAT_8061615c,
                         (double)FLOAT_80616150);
    PARTICLE_DEF_COLOR_END((double)FLOAT_8061615c, (double)FLOAT_80616158, (double)FLOAT_80616160,
                           (double)FLOAT_80616158);
    PARTICLE_EMITTER_CREATE((double)FLOAT_8061616c, (double)FLOAT_80616170, (double)FLOAT_80616174,
                            &DAT_805894fc, &DAT_80589430, 0, 0);
    PARTICLE_EMITTER_COLOR((double)FLOAT_80616178, (double)FLOAT_80616174, (double)FLOAT_8061617c,
                           (double)FLOAT_80616174);
}

// ---------------------------------------------------------------------------
// 0x8015d080  subobj_pool_pop_free
// ---------------------------------------------------------------------------
// Return the most-recently-freed record slot back to the free-list head,
// re-linking it into the intrusive free chain (DAT_806121e4).
// ---------------------------------------------------------------------------
void subobj_pool_pop_free(void)
{
    DAT_806121d8 = DAT_806121d8 + -1;
    DAT_806121e8 = DAT_806121e8 + 1;
    int* slot = *(int**)(DAT_806121e0 + DAT_806121d8 * 4);
    slot[0] = (int)(intptr_t)DAT_806121e4;
    DAT_806121e4 = (void*)slot;
    slot[1] = 0;
    if (slot[0] == 0) return;
    *(int**)(slot[0] + 4) = slot;
}

// ---------------------------------------------------------------------------
// 0x8015d0cc  subobj_pool_free_record
// ---------------------------------------------------------------------------
// Free an active sub-object record: push its slot onto the free-list array,
// unlink it from the intrusive active list (head DAT_806121e4), clear its
// 0x970 bytes. Called by the render/HUD dispatch (FUN_8015d3f8).
// ---------------------------------------------------------------------------
void subobj_pool_free_record(int* rec)
{
    int idx = DAT_806121d8 * 4;
    DAT_806121d8 = DAT_806121d8 + 1;
    *(int**)(DAT_806121e0 + idx) = rec;
    DAT_806121e8 = DAT_806121e8 + -1;
    if ((int*)rec[1] == (int*)0) {
        DAT_806121e4 = (void*)(intptr_t)rec[0];
    } else {
        *(int*)rec[1] = rec[0];
    }
    if (rec[0] != 0) {
        *(int*)(rec[0] + 4) = rec[1];
    }
    memset_wrapper((int)rec, 0, 0x970);
}

// ---------------------------------------------------------------------------
// 0x8015d14c  subobj_release_sound_banks
// ---------------------------------------------------------------------------
// Release the carried body's sound-bank references (7 slots at rec+0xb4..+0xcc)
// and, for release mode 2, preload the three replacement sound resources
// (0x54d/0x54e/0x54f). rec resolved via FUN_8015f9c8.
// ---------------------------------------------------------------------------
void subobj_release_sound_banks(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                                unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                unsigned long long p7, unsigned long long p8, int mode)
{
    int rec = subobj_carry_def_record(mode);
    SOUND_CLEAR_BANK(*(int*)(rec + 0xb4));
    SOUND_CLEAR_BANK(*(int*)(rec + 0xb8));
    SOUND_CLEAR_BANK(*(int*)(rec + 0xbc));
    SOUND_CLEAR_BANK(*(int*)(rec + 0xc0));
    SOUND_CLEAR_BANK(*(int*)(rec + 0xc4));
    SOUND_CLEAR_BANK(*(int*)(rec + 0xc8));
    int chain = SOUND_CLEAR_BANK(*(int*)(rec + 0xcc));
    if (mode == 2) {
        chain = PRELOAD_RES(chain, p2, p3, p4, p5, p6, p7, p8, 0x54d);
        chain = PRELOAD_RES(chain, p2, p3, p4, p5, p6, p7, p8, 0x54e);
        PRELOAD_RES(chain, p2, p3, p4, p5, p6, p7, p8, 0x54f);
    }
}

// ===========================================================================
// batch_433 : carried sub-object SPAWN + transform SNAPSHOT/RESTORE + destroy
// Address range: 0x8015d1dc - 0x8015d9e8 | 2026-09-06
// ---------------------------------------------------------------------------
// The lifecycle layer for a carryable sub-object entity: preload its resources,
// spawn+initialize it (build the physics descriptor, wire the per-frame
// callbacks and link slots), snapshot/restore its transform for the frame
// double-buffer (used by the deterministic rollback in FUN_8025b3ac/638), and
// tear it down (destroy grabbed bodies, release grabs, free the pool record).
// Snapshot records live in the DAT_806121d4 bank (0x24 stride): [0]=snapshot id
// (entity+0x2d8), [1..3]=contact origin, [4..6]=euler, [7]=render tint word,
// [8]=floor height. DAT_806121cc = live snapshot count (backed up in c8).
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x8015d1dc  subobj_preload_carry_resources
// ---------------------------------------------------------------------------
// Preload every resource a carryable needs from its def block: the base entity
// resources, the sound banks, and up to two attached item defs (+0x30/+0x34
// item indices, 0x8c stride into the item-def table at -0x7fbf1a04). Called
// from level_preload_all_resources.
// ---------------------------------------------------------------------------
void subobj_preload_carry_resources(unsigned long long p1, double p2, double p3,
                                    unsigned long long p4, unsigned long long p5,
                                    unsigned long long p6, unsigned long long p7,
                                    unsigned long long p8, int def)
{
    int chain = PRELOAD_RES((int)p1, p2, p3, p4, p5, p6, p7, p8, *(int*)(def + 8));
    // release/preload the sound banks (mode = def+0x24)
    subobj_release_sound_banks((unsigned long long)(unsigned int)chain, p2, p3, p4, p5, p6, p7, p8,
                               *(int*)(def + 0x24));
    if (-1 < *(int*)(def + 0x30)) {
        item_preload_resources(*(int*)(def + 0x30) * 0x8c + -0x7fbf1a04);
    }
    if (-1 < *(int*)(def + 0x34)) {
        item_preload_resources(*(int*)(def + 0x34) * 0x8c + -0x7fbf1a04);
    }
}

// ---------------------------------------------------------------------------
// 0x8015d254  subobj_apply_transform_snapshot
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826f0 / FUN_8038273c). Apply a saved transform to
// the carried body: set its world position (either from the snapshot's stored
// origin, or from the render entity when no snapshot pointer is given), rebuild
// its orientation from the euler triple (mtx4_set_rotation_zyx ->
// subobj_build_orthonormal_matrix), refresh world bounds and the screen quad,
// re-query the floor height, frustum-cull it, force a bone update, and re-pose
// the rotors. euler defaults to the render entity's stored angles (+0x18).
// ---------------------------------------------------------------------------
void subobj_apply_transform_snapshot(unsigned int p1, unsigned int p2, float* euler)
{
    (void)p1; (void)p2;
    unsigned long long fr = FUN_803826f0();
    int self = (int)(fr >> 0x20);
    unsigned int* snap = (unsigned int*)(unsigned int)fr;
    int rec = *(int*)(self + 0x7c);
    int renderEnt = *(int*)(self + 0x2d8);
    if (euler == (float*)0) euler = (float*)(renderEnt + 0x18);

    if (snap == (unsigned int*)0) {
        *(int*)(self + 100)  = *(int*)(renderEnt + 0xc);
        *(int*)(self + 0x68) = *(int*)(renderEnt + 0x10);
        *(int*)(self + 0x6c) = *(int*)(renderEnt + 0x14);
        float pos[3];
        pos[0] = *(float*)(self + 0x1a8) + *(float*)(self + 100);
        pos[1] = *(float*)(self + 0x1ac) + *(float*)(self + 0x68);
        pos[2] = *(float*)(self + 0x1b0) + *(float*)(self + 0x6c);
        subobj_set_contact_origin(*(unsigned int*)(self + 0x80), (unsigned int*)pos);
    } else {
        subobj_set_contact_origin(*(unsigned int*)(self + 0x80), snap);
    }
    float localMtx[21];
    mtx4_set_rotation_zyx((double)euler[0], (double)euler[1], (double)euler[2], localMtx);
    subobj_build_orthonormal_matrix(*(unsigned int*)(self + 0x80), localMtx, (float*)0);
    subobj_apply_world_bounds(*(unsigned int*)(self + 0x80));
    subobj_compute_screen_quad(*(unsigned int*)(self + 0x80));
    double fq = (double)FLOOR_QUERY((double)FLOAT_80616150, (double)FLOAT_80616150,
                                    self + 100, self + 0x21c, (int*)0, 0);
    *(float*)(rec + 0x558) = (float)fq;
    // build the culling AABB and query visible scene nodes
    float aabb[6];
    float cx = *(float*)(self + 100)  + *(float*)(self + 0x1a8);
    float cy = *(float*)(self + 0x68) + *(float*)(self + 0x1ac);
    float cz = *(float*)(self + 0x6c) + *(float*)(self + 0x1b0);
    aabb[0] = cx - *(float*)(self + 0x1c0);
    aabb[1] = cy - *(float*)(self + 0x1c4);
    aabb[2] = cz - *(float*)(self + 0x1c8);
    aabb[3] = cx + *(float*)(self + 0x1c0);
    aabb[4] = cy + *(float*)(self + 0x1c4);
    aabb[5] = cz + *(float*)(self + 0x1c8);
    int n = scene_query_nodes_in_frustum(0, (int)(intptr_t)aabb, self + 0x38, 10);
    *(char*)(self + 0x30) = (char)n;
    if ((char)n != '\0') *(int*)(self + 0x34) = *(int*)(self + 0x38);
    entity_force_bone_update(self);
    subobj_carry_rotor_pose();
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8015d3f8  subobj_carry_destroy
// ---------------------------------------------------------------------------
// Frame-registered destroy handler (event subtype 1). Stops the carry sound
// (+0x25a handle), destroys every scripted attach body (attach-point table
// +0x144, 0x10 stride) that is still alive, releases every live grab slot
// (+0x70, 0x34 stride) via subobj_grab_attach_release, and frees the pool
// record.
// ---------------------------------------------------------------------------
void subobj_carry_destroy(void)
{
    int entity = FUN_803826e4();
    int* rec = *(int**)(entity + 0x7c);
    if (-1 < rec[0x25a]) {
        sound_stop_handle((unsigned int)rec[0x25a]);
        rec[0x25a] = -1;
    }
    int off = 0;
    for (int i = 0; i < rec[0x59]; i = i + 1) {
        int* ap = *(int**)((int)rec + off + 0x144);
        if (ap != (int*)0) {
            int* body = (int*)*ap;
            if ((body != (int*)0) && ((body[10] & 0x80000000) == 0)) {
                entity_destroy_with_event(body);
            }
        }
        *(int*)((int)rec + off + 0x144) = 0;
        off = off + 0x10;
    }
    off = 0;
    for (int i = 0; i < rec[0x50]; i = i + 1) {
        unsigned int h = *(unsigned int*)((int)rec + off + 0x70);
        int body;
        if (h == 0) { body = 0; }
        else {
            body = DAT_80611c24 + (h >> 0x16) * 0x2e8;
            if (*(unsigned int*)(body + 0x2c) != h) { body = 0; *(int*)((int)rec + off + 0x70) = 0; }
        }
        if (body != 0) subobj_grab_attach_release(entity, (void*)body, 1, 0, 1);
        off = off + 0x34;
    }
    subobj_pool_free_record(rec);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8015d518  subobj_snapshot_capture
// ---------------------------------------------------------------------------
// Capture the carried body's current transform into its snapshot slot: contact
// origin (+1..+3), euler angles from the object matrix (+4..+6), the snapshot
// id (render entity +0x2d8) at [0], the render tint word (+0x1cc) at [7], and
// the floor height (rec+0x558) at [8].
// ---------------------------------------------------------------------------
void subobj_snapshot_capture(void)
{
    unsigned long long fr = __save_gpr_28();
    int self = (int)(fr >> 0x20);
    int rec = *(int*)(self + 0x7c);
    int mtx = subobj_get_object_matrix(*(unsigned int*)(self + 0x80));
    int* slot = (int*)(DAT_806121d4 + (int)(unsigned int)fr * 0x24);
    int* org = (int*)subobj_get_contact_origin(*(unsigned int*)(self + 0x80));
    slot[1] = org[0];
    slot[2] = org[1];
    slot[3] = org[2];
    MTX4_TO_EULER3(mtx, (float*)(slot + 4), (float*)(slot + 5), (float*)(slot + 6));
    slot[0] = *(int*)(self + 0x2d8);
    slot[7] = *(int*)(self + 0x1cc);
    slot[8] = *(int*)(rec + 0x558);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x8015d5b8  subobj_snapshot_store
// ---------------------------------------------------------------------------
// Find the snapshot slot matching this render entity (+0x2d8) in the snapshot
// bank; if none exists, grow the count. Either way capture into it.
// ---------------------------------------------------------------------------
void subobj_snapshot_store(int entity)
{
    int off = 0;
    int rem = DAT_806121cc;
    if (0 < DAT_806121cc) {
        do {
            if (*(int*)(DAT_806121d4 + off) == *(int*)(entity + 0x2d8)) {
                subobj_snapshot_capture();
                return;
            }
            off = off + 0x24;
            rem = rem + -1;
        } while (rem != 0);
    }
    DAT_806121cc = DAT_806121cc + 1;
    subobj_snapshot_capture();
}

// ---------------------------------------------------------------------------
// 0x8015d624  subobj_snapshot_restore
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826f0 / FUN_8038273c). Find the snapshot slot for
// this render entity and, if present, apply it via subobj_apply_transform_
// snapshot, then restore the render tint word (+0x1cc) and floor height.
// ---------------------------------------------------------------------------
void subobj_snapshot_restore(void)
{
    int self = FUN_803826f0();
    int off = 0;
    int rem = DAT_806121cc;
    if (0 < DAT_806121cc) {
        do {
            int* slot = (int*)(DAT_806121d4 + off);
            if (slot[0] == *(int*)(self + 0x2d8)) {
                int rec = *(int*)(self + 0x7c);
                subobj_apply_transform_snapshot((unsigned int)self, (unsigned int)(uintptr_t)(slot + 1),
                                                (float*)(slot + 4));
                *(int*)(self + 0x1cc) = slot[7];
                *(int*)(rec + 0x558) = slot[8];
                break;
            }
            off = off + 0x24;
            rem = rem + -1;
        } while (rem != 0);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8015d6b0  subobj_snapshot_all
// ---------------------------------------------------------------------------
// Snapshot every active carried body (walk the intrusive active list
// DAT_806121e4; each node's +0x13 is its render-entity id), copy the live
// snapshot bank into the backup bank (leaf_31e8 = memcpy, 0x168 bytes) and back
// up the snapshot count. Called by FUN_8025b3ac (frame begin).
// ---------------------------------------------------------------------------
void subobj_snapshot_all(void)
{
    for (int* node = (int*)DAT_806121e4; node != (int*)0; node = (int*)*node) {
        if (node[0x13] != 0) subobj_snapshot_store(node[0x13]);
    }
    leaf_31e8(DAT_806121d0, DAT_806121d4, 0x168);
    DAT_806121c8 = DAT_806121cc;
}

// ---------------------------------------------------------------------------
// 0x8015d710  subobj_snapshot_rollback
// ---------------------------------------------------------------------------
// Restore the snapshot bank from the backup (leaf_31e8 = memcpy, 0x168 bytes)
// and restore the snapshot count. Called by FUN_8025b638 (frame rollback).
// ---------------------------------------------------------------------------
void subobj_snapshot_rollback(void)
{
    leaf_31e8(DAT_806121d4, DAT_806121d0, 0x168);
    DAT_806121cc = DAT_806121c8;
}

// ---------------------------------------------------------------------------
// 0x8015d744  subobj_carry_spawn
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826e8 / FUN_80382734). Spawn and fully initialize a
// carried sub-object entity: allocate its record from the pool, allocate the
// physics sub-object body and build its default descriptor, wire up the
// collision groups and the rotor contact/deform callbacks, seed the deform/
// break accumulators, run the per-def init callback (+0xb0), init the rotor
// sub-table (FUN_8015dc90) and deform springs (subobj_deform_init_springs),
// refresh bounds/quad/floor/zone, install the per-frame carry callbacks
// (engage/gear/HUD at rec +100/+0x68/+0x6c), set the render type, register for
// the update lists, and bind the anim link slots. If it is a snapshot replay
// (render entity +0x2d8 set), also restore the transform.
// ---------------------------------------------------------------------------
void subobj_carry_spawn(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                        unsigned long long p4, unsigned long long p5, unsigned long long p6,
                        unsigned long long p7, unsigned long long p8)
{
    unsigned long long fr = FUN_803826e8();
    int entity = (int)(fr >> 0x20);
    int renderEnt = (int)fr;
    *(int*)(entity + 0x2d8) = renderEnt;
    *(int*)(entity + 0x7c) = POOL_POP_FREE();
    unsigned int handle = POOL_ALLOC();
    subobj_get_descriptor(handle);
    int rec = *(int*)(entity + 0x7c);
    int snd = subobj_carry_def_record(*(int*)(renderEnt + 0x24));

    *(unsigned int*)(entity + 0x28) = *(unsigned int*)(entity + 0x28) | 0x70;
    *(int*)(entity + 0x1d8) = *(int*)(renderEnt + 0x3c);
    *(int*)(entity + 0x1cc) = *(int*)(entity + 0x1d8);
    int lod = (*(int*)(snd + 0xa4) != -1) ? *(int*)(snd + 0xa4) : -1;
    BUILD_DESC_DEFAULT16(p1, p2, p3, p4, p5, p6, p7, p8, handle, entity,
                         *(unsigned int*)(snd + 0xa0) & 0xff, *(int*)(snd + 0xa4), (float)lod);
    subobj_set_collision_groups(handle, 1, 0xf1);
    *(unsigned int*)(rec + 0x48) = handle;
    *(int*)(rec + 0x4c) = entity;
    subobj_set_extra_words(handle, (unsigned int)(uintptr_t)&subobj_rotor_contact_dispatch,
                           (unsigned int)(uintptr_t)&subobj_deform_ground_react);
    float z = FLOAT_80616158;
    *(int*)(rec + 0x54) = 0;
    *(int*)(rec + 0x50) = snd;
    *(float*)(rec + 0x53c) = z; *(float*)(rec + 0x540) = z; *(float*)(rec + 0x544) = z;
    *(float*)(rec + 0x538) = z; *(float*)(rec + 0x534) = z; *(float*)(rec + 0x530) = z;
    *(float*)(rec + 0x528) = z;
    *(int*)(rec + 0x5c) = 0;
    *(int*)(rec + 0x58) = *(int*)(snd + 0xa8);
    *(float*)(rec + 0x96c) = z;
    *(int*)(rec + 0x968) = -1;
    memset_wrapper(rec + 0x4fc, 0, 0x2c);
    memset_wrapper(rec + 0x494, 0, 0x60);
    (*(void(**)(int, int))(snd + 0xb0))(entity, renderEnt);
    subobj_deform_rest_offset();
    subobj_deform_init_springs();
    subobj_apply_world_bounds(handle);
    subobj_compute_screen_quad(handle);
    double fq = (double)FLOOR_QUERY((double)FLOAT_80616150, (double)FLOAT_80616150,
                                    entity + 100, entity + 0x21c, (int*)0, 0);
    *(float*)(rec + 0x558) = (float)fq;
    int zone = soundpad_find_nearest_entity((double)FLOAT_80616158, (float*)(entity + 100));
    *(int*)(rec + 0x5b0) = zone;
    *(int*)(rec + 0x60) = 0;
    *(void**)(rec + 100)   = (void*)&subobj_carry_engage_update;
    *(void**)(rec + 0x68)  = (void*)&subobj_carry_gear_update;
    *(void**)(rec + 0x6c)  = (void*)&subobj_carry_hud_gear_prompt;
    entity_set_render_type(entity, (int)(intptr_t)&subobj_break_apply_separation);
    entity_register_for_update(entity, 7, -0x7fea9e80);
    entity_register_for_update(entity, 2, 0);
    entity_register_for_update(entity, 1, -0x7feaa648);
    subobj_set_link_slot(entity, 2, (unsigned int)(uintptr_t)&DAT_80157f04);
    subobj_set_link_slot(entity, 6, (unsigned int)(uintptr_t)&subobj_rotor_strike_spark);
    subobj_set_link_slot(entity, 9, (unsigned int)(uintptr_t)&subobj_rotor_strike_fx);
    subobj_set_link_slot(entity, 8, (unsigned int)(uintptr_t)&subobj_rotor_strike_fx);
    subobj_set_link_slot(entity, 3, (unsigned int)(uintptr_t)&LAB_801580e0);
    subobj_set_link_slot(entity, 4, (unsigned int)(uintptr_t)&LAB_80158104);
    if (*(int*)(entity + 0x2d8) != 0) subobj_snapshot_restore();
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x8015d9e8  subobj_deform_init_springs
// ---------------------------------------------------------------------------
// Initialize the 4 corner deform springs from the body's inverse mass and the
// def's per-corner stiffness/damping (config table +0x50, offsets +0x70/+0x74,
// alternating by parity). Writes critically-damped spring rate (+0x4a4) and
// damping (+0x4a8) into each corner accumulator (rec+0x494, 0x18 stride).
// ---------------------------------------------------------------------------
void subobj_deform_init_springs(void)
{
    int self = __save_gpr_28();
    int rec = *(int*)(self + 0x7c);
    double invMass = subobj_get_inverse_mass(*(unsigned int*)(self + 0x80));
    unsigned int i = 0;
    int off = 0;
    double rateBase = (double)(float)((double)FLOAT_80616180 * invMass);
    double dampBase = (double)(float)((double)FLOAT_80616154 * rateBase);
    do {
        int cfgOff = (1 - (i & 1)) * 0x14;
        double rate = math_sqrt((double)(float)((double)*(float*)(*(int*)(rec + 0x50) + cfgOff + 0x70) * rateBase));
        int corner = rec + off;
        i = i + 1;
        *(float*)(corner + 0x4a4) = (float)rate;
        off = off + 0x18;
        *(float*)(corner + 0x4a8) =
            (float)((double)*(float*)(*(int*)(rec + 0x50) + cfgOff + 0x74) * dampBase);
    } while ((int)i < 4);
    __restore_gpr_28();
}

// ===========================================================================
// batch_434 : carried-object DEFORM/BREAKABLE-FRAME build layer
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x8015daa4  subobj_deform_rest_init
// ---------------------------------------------------------------------------
// Initialize one rotor/deform sub-object channel (rec+0x174, 0x100 stride per
// rotorIdx). Bind the per-def rest-length table entry (&DAT_8040e570 + defIdx*
// 0x1c), seed the five spring/rev state floats (+0x18c/+0x190/+0x1c0/+0x1c8/
// +0x1cc) to FLOAT_80616158, set the inverse rest-length scale (+0x194 =
// FLOAT_80616150 / restLen) and clear the accumulator (+0x180).
// ---------------------------------------------------------------------------
void subobj_deform_rest_init(int entity, int rotorIdx, int defIdx)
{
    float seed = FLOAT_80616158;
    float coef = FLOAT_80616150;
    int rotor = *(int*)(entity + 0x7c) + rotorIdx * 100;
    *(int**)(rotor + 0x174) = (int*)(&DAT_8040e570 + defIdx * 0x1c);
    *(float*)(rotor + 0x18c) = seed;
    *(float*)(rotor + 0x190) = seed;
    *(float*)(rotor + 0x1c0) = seed;
    *(float*)(rotor + 0x1c8) = seed;
    *(float*)(rotor + 0x1cc) = seed;
    *(float*)(rotor + 0x194) = coef / *(float*)(&DAT_8040e570 + defIdx * 0x1c);
    *(int*)(rotor + 0x180) = 0;
}

// ---------------------------------------------------------------------------
// 0x8015dc7c  subobj_deform_set_def_slot
// ---------------------------------------------------------------------------
// Store a value into the deform def-slot table (rec + 0x168 + slot*4).
// ---------------------------------------------------------------------------
void subobj_deform_set_def_slot(int entity, int value, int slot)
{
    *(int*)(*(int*)(entity + 0x7c) + slot * 4 + 0x168) = value;
}

// ---------------------------------------------------------------------------
// 0x8015f9c8  subobj_carry_def_record
// ---------------------------------------------------------------------------
// Def-table accessor: resolve the carried-object def record for a def index
// (&DAT_804083a0 + defIndex * 0xd8). Callers: subobj_carry_spawn,
// subobj_release_sound_banks.
// ---------------------------------------------------------------------------
int subobj_carry_def_record(int defIndex)
{
    return defIndex * 0xd8 + -0x7fbf1860;
}

// ---------------------------------------------------------------------------
// 0x8015daf8  subobj_deform_spawn_attach
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826e4 / FUN_80382730). Spawn a detachable rotor/
// prop attachment sub-object for a live-rotor grab slot. Compute the world
// attach position from the mesh bone-attach vertex transformed through the
// object matrix plus the body origin, derive the spawn euler from the object
// matrix, spawn the attachment entity (item_spawn_entity, def &DAT_8040e5fc +
// attachType*0x8c), link it into the grab slot (piVar8 at rec + slot*0x10 +
// 0x144), stamp it type 8, seed the spin rate from the per-attach table
// (&DAT_8040e644 + attachType*0x8c), and clear the body's carried-flag bits.
// ---------------------------------------------------------------------------
void subobj_deform_spawn_attach(unsigned long long p1, unsigned long long p2, double p3,
                                unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                unsigned long long p7, unsigned long long p8,
                                int entity, int slot, int attachType)
{
    unsigned long long fr = FUN_803826e4();
    int frameEnt = (int)(fr >> 0x20);
    (void)entity;
    float* objMtx = (float*)subobj_get_object_matrix(*(unsigned int*)(frameEnt + 0x80));
    int rec = *(int*)(frameEnt + 0x7c);
    int* strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    int* linkSlot = (int*)(rec + slot * 0x10 + 0x144);
    int attachOff = attachType * 0x8c;
    int stripHdr = *strip;
    int vertOff = (unsigned int)*(unsigned short*)(linkSlot + 1) * 0x18;

    float local[3];
    local[0] = *(float*)(strip[stripHdr * -0x30 + 0x1b] + vertOff);
    local[1] = *(float*)(strip[stripHdr * -0x30 + 0x1b] + vertOff + 4);
    local[2] = *(float*)(strip[stripHdr * -0x30 + 0x1b] + vertOff + 8);
    mtx4_transform_vector(objMtx, local);
    local[0] = local[0] + *(float*)(frameEnt + 100);
    local[1] = local[1] + *(float*)(frameEnt + 0x68);
    local[2] = local[2] + *(float*)(frameEnt + 0x6c);

    float euler[3];
    MTX4_TO_EULER3((int)(intptr_t)objMtx, &euler[0], &euler[1], &euler[2]);
    int inst = ITEM_SPAWN_ENTITY((double)euler[0], (double)local[1], p3, p4, p5, p6, p7, p8,
                                 (unsigned int*)(attachOff + -0x7fbf1a04), local);
    float spin = FLOAT_80616154;
    linkSlot[0] = *(int*)(inst + 0x7c);
    linkSlot[3] = -1;
    *(int*)(rec + (unsigned int)*(unsigned short*)((int)linkSlot + 6) * 0x34 + 0x9c) = (int)fr;
    *(int*)(linkSlot[0] + 0x10) = 8;
    *(float*)(linkSlot[0] + 0x58) = spin * *(float*)(&DAT_8040e644 + attachOff);
    *(int*)(linkSlot[0] + 0x5c) = *(int*)(&DAT_8040e644 + attachOff);
    *(unsigned int*)(inst + 0x28) = *(unsigned int*)(inst + 0x28) & 0xffffff9f;
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8015dc90  subobj_deform_rest_offset
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826e4 / FUN_80382730). Precompute each rotor grab
// slot's rest offset: for every live slot (rec+0x70, 0x34 stride, +8 anim clip
// != 0) build a Y-rotation of +/-FLOAT_80616184/88 (by the sign of the mesh
// vertex X) via mtx4_set_rotation_y, rotate the clip's total translation
// (anim_clip_total_translation) into it, and store restPos = meshVert - rotated
// delta into the slot (+0x20/+0x24/+0x28).
// ---------------------------------------------------------------------------
void subobj_deform_rest_offset(void)
{
    int frameEnt = FUN_803826e4();
    int rec = *(int*)(frameEnt + 0x7c);
    int off = 0;
    for (int i = 0; i < *(int*)(rec + 0x140); i = i + 1) {
        int slot = rec + off + 0x70;
        if (*(int*)(slot + 8) != 0) {
            int* strip = *(int**)(*(int*)(frameEnt + 4) + 4);
            float* vert = (float*)(strip[*strip * -0x30 +
                              ((int)*(unsigned int*)(slot + 4) >> 0x10) * 0x30 + 0x1b] +
                              (*(unsigned int*)(slot + 4) & 0xffff) * 0x18);
            float ang = FLOAT_80616188;
            if (FLOAT_80616158 < *vert) ang = FLOAT_80616184;
            float rot[16];
            mtx4_set_rotation_y((double)ang, rot);
            float delta[3];
            anim_clip_total_translation(frameEnt, *(int*)(slot + 8), delta);
            mtx4_transform_vector(rot, delta);
            *(float*)(slot + 0x20) = vert[0] - delta[0];
            *(float*)(slot + 0x24) = vert[1] - delta[1];
            *(float*)(slot + 0x28) = vert[2] - delta[2];
        }
        off = off + 0x34;
    }
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// Shared helper for the deform-build variants: fill one deform node block.
// nodeBase is rec + node-field offset; boneMtx is the mesh bone matrix rec;
// meshAttachOff is the byte offset of the bone-attach point within boneMtx.
// Writes the world anchor (+0x14 relative to the node's +0x188 pointer store)
// and rest length. The variants inline this pattern (kept expanded below to
// stay faithful to the differing offsets), so no separate function is emitted.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 0x8015dd9c  subobj_deform_build_type0
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826d0 / FUN_8038271c). Build the 4-node deformable
// body for craft class 0: derive the local AABB span from opposing mesh verts,
// fill the 4 deform-node blocks (rec +0x184/+0x1e8/+0x24c/+0x2b4) with node
// kinds 4/5/6/7 and rest lengths, seed the physics-body attach descriptor
// fields (+0x70..+0xd4), optionally spawn a live-rotor attachment (state
// +0x30), spawn the two rotor-downwash emitters (rec +0x1dc/+0x240) and init
// the 4 corner rotor rest channels.
// ---------------------------------------------------------------------------
void subobj_deform_build_type0(void)
{
    double dattach0 = 0, dattach1 = 0, dattach2 = 0, dattach3 = 0, dattach4 = 0;
    unsigned long long fr = FUN_803826d0();
    float k = FLOAT_80616154;
    int frameEnt = (int)(fr >> 0x20);
    int stateBlk = (int)fr;
    int rec = *(int*)(frameEnt + 0x7c);
    int* strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    float* v = (float*)strip[*strip * -0x30 + 0x1b];
    int acc = strip[*strip * -0x30 + 0x20];

    *(float*)(acc + 0x10) = k * ABS(v[6] - v[0]);
    *(float*)(acc + 0x14) = k * ABS(v[7] - v[1]);
    *(float*)(acc + 0x18) = k * ABS(v[8] - v[0x5c]);
    float lo;
    lo = v[0]; if (v[6] < lo) lo = v[6];
    *(float*)(acc + 4) = *(float*)(acc + 0x10) + lo;
    lo = v[1]; if (v[7] < lo) lo = v[7];
    *(float*)(acc + 8) = *(float*)(acc + 0x14) + lo;
    float k2 = FLOAT_80616154;
    lo = v[0x5c]; if (v[8] < lo) lo = v[8];
    *(float*)(acc + 0xc) = *(float*)(acc + 0x18) + lo;
    *(float*)(acc + 0x2c) = k2 * ABS(v[6] - v[0]);
    *(float*)(acc + 0x30) = k2 * ABS(v[0x5b] - v[1]);
    *(float*)(acc + 0x34) = k2 * ABS(v[0x5c] - v[2]);
    lo = v[0]; if (v[6] < lo) lo = v[6];
    *(float*)(acc + 0x20) = *(float*)(acc + 0x2c) + lo;
    lo = v[1]; if (v[0x5b] < lo) lo = v[0x5b];
    *(float*)(acc + 0x24) = *(float*)(acc + 0x30) + lo;
    lo = v[2]; if (v[0x5c] < lo) lo = v[0x5c];
    *(float*)(acc + 0x28) = *(float*)(acc + 0x34) + lo;

    strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    *(int*)(rec + 0x188) = strip[*strip * -0x30 + 0x1b] + 0x90;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    *(int*)(rec + 0x1ec) = strip[*strip * -0x30 + 0x1b] + 0xc0;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    *(int*)(rec + 0x250) = strip[*strip * -0x30 + 0x1b] + 0xf0;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    *(int*)(rec + 0x2b4) = strip[*strip * -0x30 + 0x1b] + 0x120;
    *(int*)(rec + 0x19c) = **(int**)(rec + 0x188);
    *(int*)(rec + 0x1a0) = *(int*)(*(int*)(rec + 0x188) + 4);
    *(int*)(rec + 0x1a4) = *(int*)(*(int*)(rec + 0x188) + 8);
    *(int*)(rec + 0x200) = **(int**)(rec + 0x1ec);
    *(int*)(rec + 0x204) = *(int*)(*(int*)(rec + 0x1ec) + 4);
    *(int*)(rec + 0x208) = *(int*)(*(int*)(rec + 0x1ec) + 8);
    *(int*)(rec + 0x264) = **(int**)(rec + 0x250);
    *(int*)(rec + 0x268) = *(int*)(*(int*)(rec + 0x250) + 4);
    *(int*)(rec + 0x26c) = *(int*)(*(int*)(rec + 0x250) + 8);
    *(int*)(rec + 0x2c8) = **(int**)(rec + 0x2b4);
    *(int*)(rec + 0x2cc) = *(int*)(*(int*)(rec + 0x2b4) + 4);
    *(int*)(rec + 0x2d0) = *(int*)(*(int*)(rec + 0x2b4) + 8);

    float* p; int b; float dx, dy, dz;
    p = *(float**)(rec + 0x188); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dy = *(float*)(b + 0xac) - p[1]; dx = *(float*)(b + 0xa8) - p[0]; dz = *(float*)(b + 0xb0) - p[2];
    *(float*)(rec + 0x198) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x1ec); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dy = *(float*)(b + 0xdc) - p[1]; dx = *(float*)(b + 0xd8) - p[0]; dz = *(float*)(b + 0xe0) - p[2];
    *(float*)(rec + 0x1fc) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x250); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dy = *(float*)(b + 0x10c) - p[1]; dx = *(float*)(b + 0x108) - p[0]; dz = *(float*)(b + 0x110) - p[2];
    *(float*)(rec + 0x260) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x2b4); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = *(float*)(b + 0x138) - p[0]; dy = *(float*)(b + 0x13c) - p[1]; dz = *(float*)(b + 0x140) - p[2];
    *(float*)(rec + 0x2c4) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));

    *(int*)(rec + 0x184) = 4;
    *(int*)(rec + 0x1e8) = 5;
    *(int*)(rec + 0x24c) = 6;
    *(int*)(rec + 0x2b0) = 7;
    *(unsigned int*)(rec + 0x17c) |= 3;
    *(unsigned int*)(rec + 0x1e0) |= 1;
    *(unsigned int*)(rec + 0x244) |= 1;
    *(unsigned int*)(rec + 0x2a8) |= 3;
    *(int*)(rec + 0xa0) = 0;
    *(int*)(rec + 0x70) = 0;
    *(int*)(rec + 0x74) = 0x11;
    *(int*)(rec + 0x9c) = -1;
    *(int*)(rec + 0x78) = 0x79;
    *(int*)(rec + 0x7c) = 0x7a;
    *(int*)(rec + 0x80) = 0x78;
    *(int*)(rec + 0x84) = 0x7e;
    *(int*)(rec + 0x88) = 0x81;
    *(int*)(rec + 0x8c) = 0x82;
    *(int*)(rec + 0xd4) = 1;
    *(int*)(rec + 0xa4) = 0;
    *(int*)(rec + 0xa8) = 0x10;
    *(int*)(rec + 0xd0) = -1;
    *(int*)(rec + 0xac) = 0x7c;
    *(int*)(rec + 0xb0) = 0x7d;
    *(int*)(rec + 0xb4) = 0x7b;
    *(int*)(rec + 0xb8) = 0;
    *(int*)(rec + 0xbc) = 0x7f;
    *(int*)(rec + 0xc0) = 0x80;
    if (*(int*)(stateBlk + 0x30) < 0) {
        *(int*)(rec + 0x140) = 2;
    } else {
        float fx = FLOAT_80616158;
        *(int*)(rec + 0x140) = 3;
        float fy = FLOAT_8061618c;
        float fz = FLOAT_80616190;
        *(int*)(rec + 0xd8) = 0;
        *(int*)(rec + 0xdc) = 5;
        *(int*)(rec + 0x104) = -1;
        *(int*)(rec + 0xe0) = 0;
        *(int*)(rec + 0xe4) = 0;
        *(int*)(rec + 0xec) = 0;
        *(int*)(rec + 0xf0) = 0;
        *(int*)(rec + 0xf4) = 0;
        *(float*)(rec + 0xf8) = fx;
        *(float*)(rec + 0xfc) = fy;
        *(float*)(rec + 0x100) = fz;
        *(int*)(rec + 0x108) = 4;
        *(int*)(rec + 0xe8) = 0x83;
        *(int*)(rec + 0x164) = 1;
        *(short*)(rec + 0x14a) = 2;
        *(float*)(rec + 0x14c) = fx;
        *(int*)(rec + 0x144) = 0;
        *(short*)(rec + 0x148) = 2;
        subobj_deform_spawn_attach((unsigned long long)(unsigned int)(int)(intptr_t)&fy, dattach1,
                                   dattach2, dattach3, dattach4, 0, 0, 0, frameEnt, 0, 0);
        *(unsigned int*)(*(int*)(frameEnt + 4) + 0x30) |= 1;
        *(int*)(rec + 0x150) = 3;
    }
    (void)dattach0;
    *(int*)(rec + 0x4f4) = 2;
    float e = FLOAT_80616154;
    *(int*)(rec + 0x4f8) = 0xe;
    float zf = FLOAT_80616158;
    *(float*)(rec + 0x494) = zf;
    *(float*)(rec + 0x498) = zf;
    float sx = e * ABS(*(float*)(rec + 0x2c8) - *(float*)(rec + 0x19c));
    float sy = e * ABS(*(float*)(rec + 0x1a4) - *(float*)(rec + 0x208));
    *(float*)(rec + 0x49c) = sy;
    *(float*)(rec + 0x4ac) = -sx;
    *(float*)(rec + 0x4b0) = zf;
    *(float*)(rec + 0x4b4) = zf;
    *(float*)(rec + 0x4c4) = zf;
    *(float*)(rec + 0x4c8) = zf;
    *(float*)(rec + 0x4cc) = -sy;
    *(float*)(rec + 0x4dc) = sx;
    *(float*)(rec + 0x4e0) = zf;
    *(float*)(rec + 0x4e4) = zf;
    PARTICLE_EMITTER_SPAWN(rec + 0x1dc, &DAT_805894fc, 0x40, (float*)0, (float*)0);
    PARTICLE_EMITTER_SPAWN(rec + 0x240, &DAT_805894fc, 0x40, (float*)0, (float*)0);
    subobj_deform_rest_init(frameEnt, 0, *(int*)(stateBlk + 0x28));
    subobj_deform_rest_init(frameEnt, 1, *(int*)(stateBlk + 0x2c));
    subobj_deform_rest_init(frameEnt, 2, *(int*)(stateBlk + 0x2c));
    subobj_deform_rest_init(frameEnt, 3, *(int*)(stateBlk + 0x28));
    FUN_8038271c();
}

// ---------------------------------------------------------------------------
// 0x8015e434  subobj_deform_build_type1
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826c8 / FUN_80382714). Craft class 1 variant of the
// 4-node deform build: same layout pass as type0 but with the mesh bone
// offsets shifted (+0xc0/+0xf0/+0x120/+0x150), node kinds 3/4/5/6, no live-
// rotor spawn (always carry mode 2), and one def-slot set (slot 1 = 1).
// ---------------------------------------------------------------------------
void subobj_deform_build_type1(void)
{
    unsigned long long fr = FUN_803826c8();
    float k = FLOAT_80616154;
    int frameEnt = (int)(fr >> 0x20);
    int stateBlk = (int)fr;
    int rec = *(int*)(frameEnt + 0x7c);
    int* strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    float* v = (float*)strip[*strip * -0x30 + 0x1b];
    int acc = strip[*strip * -0x30 + 0x20];

    *(float*)(acc + 0x10) = k * ABS(v[6] - v[0]);
    *(float*)(acc + 0x14) = k * ABS(v[7] - v[1]);
    *(float*)(acc + 0x18) = k * ABS(v[8] - v[0x68]);
    float lo;
    lo = v[0]; if (v[6] < lo) lo = v[6];
    *(float*)(acc + 4) = *(float*)(acc + 0x10) + lo;
    lo = v[1]; if (v[7] < lo) lo = v[7];
    *(float*)(acc + 8) = *(float*)(acc + 0x14) + lo;
    float k2 = FLOAT_80616154;
    lo = v[0x68]; if (v[8] < lo) lo = v[8];
    *(float*)(acc + 0xc) = *(float*)(acc + 0x18) + lo;
    *(float*)(acc + 0x2c) = k2 * ABS(v[6] - v[0]);
    *(float*)(acc + 0x30) = k2 * ABS(v[0x67] - v[1]);
    *(float*)(acc + 0x34) = k2 * ABS(v[0x68] - v[2]);
    lo = v[0]; if (v[6] < lo) lo = v[6];
    *(float*)(acc + 0x20) = *(float*)(acc + 0x2c) + lo;
    lo = v[1]; if (v[0x67] < lo) lo = v[0x67];
    *(float*)(acc + 0x24) = *(float*)(acc + 0x30) + lo;
    lo = v[2]; if (v[0x68] < lo) lo = v[0x68];
    *(float*)(acc + 0x28) = *(float*)(acc + 0x34) + lo;

    strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    *(int*)(rec + 0x188) = strip[*strip * -0x30 + 0x1b] + 0xc0;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    *(int*)(rec + 0x1ec) = strip[*strip * -0x30 + 0x1b] + 0xf0;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    *(int*)(rec + 0x250) = strip[*strip * -0x30 + 0x1b] + 0x120;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    *(int*)(rec + 0x2b4) = strip[*strip * -0x30 + 0x1b] + 0x150;
    *(int*)(rec + 0x19c) = **(int**)(rec + 0x188);
    *(int*)(rec + 0x1a0) = *(int*)(*(int*)(rec + 0x188) + 4);
    *(int*)(rec + 0x1a4) = *(int*)(*(int*)(rec + 0x188) + 8);
    *(int*)(rec + 0x200) = **(int**)(rec + 0x1ec);
    *(int*)(rec + 0x204) = *(int*)(*(int*)(rec + 0x1ec) + 4);
    *(int*)(rec + 0x208) = *(int*)(*(int*)(rec + 0x1ec) + 8);
    *(int*)(rec + 0x264) = **(int**)(rec + 0x250);
    *(int*)(rec + 0x268) = *(int*)(*(int*)(rec + 0x250) + 4);
    *(int*)(rec + 0x26c) = *(int*)(*(int*)(rec + 0x250) + 8);
    *(int*)(rec + 0x2c8) = **(int**)(rec + 0x2b4);
    *(int*)(rec + 0x2cc) = *(int*)(*(int*)(rec + 0x2b4) + 4);
    *(int*)(rec + 0x2d0) = *(int*)(*(int*)(rec + 0x2b4) + 8);

    float* p; int b; float dx, dy, dz;
    p = *(float**)(rec + 0x188); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dy = *(float*)(b + 0xdc) - p[1]; dx = *(float*)(b + 0xd8) - p[0]; dz = *(float*)(b + 0xe0) - p[2];
    *(float*)(rec + 0x198) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x1ec); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dy = *(float*)(b + 0x10c) - p[1]; dx = *(float*)(b + 0x108) - p[0]; dz = *(float*)(b + 0x110) - p[2];
    *(float*)(rec + 0x1fc) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x250); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dy = *(float*)(b + 0x13c) - p[1]; dx = *(float*)(b + 0x138) - p[0]; dz = *(float*)(b + 0x140) - p[2];
    *(float*)(rec + 0x260) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x2b4); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dy = *(float*)(b + 0x16c) - p[1]; dx = *(float*)(b + 0x168) - p[0]; dz = *(float*)(b + 0x170) - p[2];
    *(float*)(rec + 0x2c4) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));

    *(int*)(rec + 0x184) = 3;
    *(int*)(rec + 0x1e8) = 4;
    *(int*)(rec + 0x24c) = 5;
    *(int*)(rec + 0x2b0) = 6;
    *(unsigned int*)(rec + 0x17c) |= 3;
    *(unsigned int*)(rec + 0x1e0) |= 1;
    *(unsigned int*)(rec + 0x244) |= 1;
    *(unsigned int*)(rec + 0x2a8) |= 3;
    *(int*)(rec + 0x140) = 2;
    *(int*)(rec + 0xa0) = 0;
    *(int*)(rec + 0x70) = 0;
    *(int*)(rec + 0x74) = 3;
    *(int*)(rec + 0x9c) = -1;
    *(int*)(rec + 0x78) = 0x79;
    *(int*)(rec + 0x7c) = 0x7a;
    *(int*)(rec + 0x80) = 0x78;
    *(int*)(rec + 0x84) = 0x7e;
    *(int*)(rec + 0x88) = 0x81;
    *(int*)(rec + 0x8c) = 0x82;
    *(int*)(rec + 0xd4) = 1;
    *(int*)(rec + 0xa4) = 0;
    *(int*)(rec + 0xa8) = 7;
    *(int*)(rec + 0xd0) = -1;
    *(int*)(rec + 0xac) = 0x7c;
    *(int*)(rec + 0xb0) = 0x7d;
    *(int*)(rec + 0xb4) = 0x7b;
    *(int*)(rec + 0xb8) = 0;
    *(int*)(rec + 0xbc) = 0x7f;
    *(int*)(rec + 0xc0) = 0x80;
    subobj_deform_set_def_slot(frameEnt, 1, 1);
    *(int*)(rec + 0x164) = 0;
    float zf = FLOAT_80616158;
    *(short*)(rec + 0x14a) = (short)0xffff;
    *(float*)(rec + 0x14c) = zf;
    float e = FLOAT_80616154;
    *(int*)(rec + 0x4f4) = 2;
    *(int*)(rec + 0x4f8) = 0x10;
    *(float*)(rec + 0x494) = zf;
    *(float*)(rec + 0x498) = zf;
    float sx = e * ABS(*(float*)(rec + 0x2c8) - *(float*)(rec + 0x19c));
    float sy = e * ABS(*(float*)(rec + 0x1a4) - *(float*)(rec + 0x208));
    *(float*)(rec + 0x49c) = sy;
    *(float*)(rec + 0x4ac) = -sx;
    *(float*)(rec + 0x4b0) = zf;
    *(float*)(rec + 0x4b4) = zf;
    *(float*)(rec + 0x4c4) = zf;
    *(float*)(rec + 0x4c8) = zf;
    *(float*)(rec + 0x4cc) = -sy;
    *(float*)(rec + 0x4dc) = sx;
    *(float*)(rec + 0x4e0) = zf;
    *(float*)(rec + 0x4e4) = zf;
    PARTICLE_EMITTER_SPAWN(rec + 0x1dc, &DAT_805894fc, 0x40, (float*)0, (float*)0);
    PARTICLE_EMITTER_SPAWN(rec + 0x240, &DAT_805894fc, 0x40, (float*)0, (float*)0);
    subobj_deform_rest_init(frameEnt, 0, *(int*)(stateBlk + 0x28));
    subobj_deform_rest_init(frameEnt, 1, *(int*)(stateBlk + 0x2c));
    subobj_deform_rest_init(frameEnt, 2, *(int*)(stateBlk + 0x2c));
    subobj_deform_rest_init(frameEnt, 3, *(int*)(stateBlk + 0x28));
    FUN_80382714();
}

// ---------------------------------------------------------------------------
// 0x8015ea54  subobj_deform_build_type2
// ---------------------------------------------------------------------------
// Frame-registered (FUN_803826e4 / FUN_80382730). Craft class 2: the 8-node
// deform build. Fills 8 node blocks (rec +0x184..+0x440 stride 0x64) from mesh
// bone offsets +0x168 down to +0x108, computes each rest length, loops to init
// 8 rotor rest channels, seeds the physics attach descriptor and up to two
// live-rotor attachments (states +0x30/+0x34), and configures the downwash
// emitter geometry.
// ---------------------------------------------------------------------------
void subobj_deform_build_type2(void)
{
    double d0 = 0, d1 = 0, d2 = 0, d3 = 0, d4 = 0;
    unsigned long long fr = FUN_803826e4();
    int frameEnt = (int)(fr >> 0x20);
    int stateBlk = (int)fr;
    int rec = *(int*)(frameEnt + 0x7c);
    *(int*)(rec + 0x184) = 2;
    *(int*)(rec + 0x1e8) = 3;
    *(int*)(rec + 0x24c) = 4;
    *(int*)(rec + 0x2b0) = 5;
    *(int*)(rec + 0x314) = 6;
    *(int*)(rec + 0x378) = 7;
    *(int*)(rec + 0x3dc) = 8;
    *(int*)(rec + 0x440) = 9;
    int* strip;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x188) = strip[*strip * -0x30 + 0x1b] + 0x168;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x1ec) = strip[*strip * -0x30 + 0x1b] + 0x150;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x250) = strip[*strip * -0x30 + 0x1b] + 0x138;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x2b4) = strip[*strip * -0x30 + 0x1b] + 0x120;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x318) = strip[*strip * -0x30 + 0x1b] + 0xc0;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x37c) = strip[*strip * -0x30 + 0x1b] + 0xd8;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x3e0) = strip[*strip * -0x30 + 0x1b] + 0xf0;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x444) = strip[*strip * -0x30 + 0x1b] + 0x108;

    float* p; float* pv; int b; float dx, dy, dz;
    p = *(float**)(rec + 0x188); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    pv = (float*)strip[*strip * -0x30 + 0x1b];
    *(float*)(rec + 0x198) = (float)math_sqrt((double)((p[2]-pv[2])*(p[2]-pv[2]) + (p[0]-pv[0])*(p[0]-pv[0]) + (p[1]-pv[1])*(p[1]-pv[1])));
    p = *(float**)(rec + 0x1ec); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0x18); dy = p[1] - *(float*)(b + 0x1c); dz = p[2] - *(float*)(b + 0x20);
    *(float*)(rec + 0x1fc) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x250); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0x30); dy = p[1] - *(float*)(b + 0x34); dz = p[2] - *(float*)(b + 0x38);
    *(float*)(rec + 0x260) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x2b4); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0x48); dy = p[1] - *(float*)(b + 0x4c); dz = p[2] - *(float*)(b + 0x50);
    *(float*)(rec + 0x2c4) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x318); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0xa8); dy = p[1] - *(float*)(b + 0xac); dz = p[2] - *(float*)(b + 0xb0);
    *(float*)(rec + 0x328) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x37c); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0x90); dy = p[1] - *(float*)(b + 0x94); dz = p[2] - *(float*)(b + 0x98);
    *(float*)(rec + 0x38c) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x3e0); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0x78); dy = p[1] - *(float*)(b + 0x7c); dz = p[2] - *(float*)(b + 0x80);
    *(float*)(rec + 0x3f0) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x444); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0x60); dy = p[1] - *(float*)(b + 100); dz = p[2] - *(float*)(b + 0x68);
    *(float*)(rec + 0x454) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));

    int nodeOff = 0;
    for (int i = 0; i < 8; i = i + 1) {
        int node = rec + nodeOff;
        *(int*)(node + 0x19c) = **(int**)(node + 0x188);
        *(int*)(node + 0x1a0) = *(int*)(*(int*)(node + 0x188) + 4);
        *(int*)(node + 0x1a4) = *(int*)(*(int*)(node + 0x188) + 8);
        subobj_deform_rest_init(frameEnt, i, *(int*)(stateBlk + 0x28));
        nodeOff = nodeOff + 100;
        *(unsigned int*)(node + 0x17c) |= 1;
    }
    *(int*)(rec + 0x140) = 2;
    float f1 = FLOAT_80616194;
    float f2 = FLOAT_80616198;
    float f3 = FLOAT_8061619c;
    float f4 = FLOAT_806161a0;
    *(int*)(rec + 0xa0) = 0;
    *(int*)(rec + 0x70) = 0;
    *(int*)(rec + 0x74) = 0x11;
    *(int*)(rec + 0x9c) = -1;
    *(int*)(rec + 0x78) = 0;
    *(int*)(rec + 0x7c) = 0;
    *(int*)(rec + 0x80) = 0;
    *(int*)(rec + 0x84) = 0;
    *(int*)(rec + 0x88) = 0;
    *(int*)(rec + 0x8c) = 0;
    *(float*)(rec + 0x90) = f1;
    *(float*)(rec + 0x94) = f2;
    *(float*)(rec + 0x98) = f3;
    *(int*)(rec + 0xd4) = 3;
    *(int*)(rec + 0xa4) = 0;
    *(int*)(rec + 0xa8) = 0x12;
    *(int*)(rec + 0xd0) = -1;
    *(int*)(rec + 0xac) = 0;
    *(int*)(rec + 0xb0) = 0;
    *(int*)(rec + 0xb4) = 0;
    *(int*)(rec + 0xb8) = 0;
    *(int*)(rec + 0xbc) = 0;
    *(int*)(rec + 0xc0) = 0;
    *(float*)(rec + 0xc4) = f1;
    *(float*)(rec + 200) = f2;
    *(float*)(rec + 0xcc) = f4;
    *(int*)(rec + 0x164) = 0;
    if (-1 < *(int*)(stateBlk + 0x30)) {
        float fa = FLOAT_806161a4;
        *(int*)(rec + 0x164) = *(int*)(rec + 0x164) + 1;
        *(short*)(rec + 0x14a) = 0;
        *(float*)(rec + 0x14c) = fa;
        *(int*)(rec + 0x144) = 0;
        *(short*)(rec + 0x148) = 0x13;
        subobj_deform_spawn_attach(d0, d1, d2, d3, d4, 0, 0, 0, frameEnt, 0, *(int*)(stateBlk + 0x30));
    }
    if (-1 < *(int*)(stateBlk + 0x34)) {
        float fb = FLOAT_806161a8;
        *(int*)(rec + 0x164) = *(int*)(rec + 0x164) + 1;
        *(short*)(rec + 0x15a) = 1;
        *(float*)(rec + 0x15c) = fb;
        *(int*)(rec + 0x154) = 0;
        *(short*)(rec + 0x158) = 0x13;
        subobj_deform_spawn_attach(d0, d1, d2, d3, d4, 0, 0, 0, frameEnt, 1, *(int*)(stateBlk + 0x34));
    }
    *(int*)(rec + 0x4f4) = 1;
    float e = FLOAT_80616154;
    float zf = FLOAT_80616158;
    *(int*)(rec + 0x4f8) = 0;
    *(float*)(rec + 0x494) = zf;
    *(float*)(rec + 0x498) = zf;
    float sx = e * ABS(*(float*)(rec + 0x458) - *(float*)(rec + 0x19c));
    float sy = e * ABS(*(float*)(rec + 0x1a4) - *(float*)(rec + 0x2d0));
    *(float*)(rec + 0x49c) = sy;
    *(float*)(rec + 0x4ac) = -sx;
    *(float*)(rec + 0x4b0) = zf;
    *(float*)(rec + 0x4b4) = zf;
    *(float*)(rec + 0x4c4) = zf;
    *(float*)(rec + 0x4c8) = zf;
    *(float*)(rec + 0x4cc) = -sy;
    *(float*)(rec + 0x4dc) = sx;
    *(float*)(rec + 0x4e0) = zf;
    *(float*)(rec + 0x4e4) = zf;
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8015f0a8  subobj_deform_build_type3
// ---------------------------------------------------------------------------
// Frame prologue __save_gpr_25/__restore_gpr_25 (frame-registered spawn cb).
// Craft class 3: 4-node deform build with mesh bone offsets +0x48/+0x60/+0x30/
// +0x18 and node kinds 4/5/3/2 (reordered), spawns the downwash emitters and
// inits the 4 rotor rest channels. No live-rotor attachment.
// ---------------------------------------------------------------------------
void subobj_deform_build_type3(void)
{
    long long fr = (long long)__save_gpr_25();
    int frameEnt = (int)((unsigned long long)fr >> 0x20);
    int stateBlk = (int)fr;
    int rec = *(int*)(frameEnt + 0x7c);
    *(int*)(rec + 0x184) = 4;
    *(int*)(rec + 0x1e8) = 5;
    *(int*)(rec + 0x24c) = 3;
    *(int*)(rec + 0x2b0) = 2;
    int* strip;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x188) = strip[*strip * -0x30 + 0x1b] + 0x48;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x1ec) = strip[*strip * -0x30 + 0x1b] + 0x60;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x250) = strip[*strip * -0x30 + 0x1b] + 0x30;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x2b4) = strip[*strip * -0x30 + 0x1b] + 0x18;

    float* p; int b; float dx, dy, dz;
    p = *(float**)(rec + 0x188); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0xa8); dy = p[1] - *(float*)(b + 0xac); dz = p[2] - *(float*)(b + 0xb0);
    *(float*)(rec + 0x198) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x1ec); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0xc0); dy = p[1] - *(float*)(b + 0xc4); dz = p[2] - *(float*)(b + 200);
    *(float*)(rec + 0x1fc) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x250); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0x90); dy = p[1] - *(float*)(b + 0x94); dz = p[2] - *(float*)(b + 0x98);
    *(float*)(rec + 0x260) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x2b4); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0x78); dy = p[1] - *(float*)(b + 0x7c); dz = p[2] - *(float*)(b + 0x80);
    *(float*)(rec + 0x2c4) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));

    *(int*)(rec + 0x19c) = **(int**)(rec + 0x188);
    *(int*)(rec + 0x1a0) = *(int*)(*(int*)(rec + 0x188) + 4);
    *(int*)(rec + 0x1a4) = *(int*)(*(int*)(rec + 0x188) + 8);
    *(unsigned int*)(rec + 0x17c) |= 3;
    *(int*)(rec + 0x200) = **(int**)(rec + 0x1ec);
    *(int*)(rec + 0x204) = *(int*)(*(int*)(rec + 0x1ec) + 4);
    *(int*)(rec + 0x208) = *(int*)(*(int*)(rec + 0x1ec) + 8);
    *(unsigned int*)(rec + 0x1e0) |= 1;
    *(int*)(rec + 0x264) = **(int**)(rec + 0x250);
    *(int*)(rec + 0x268) = *(int*)(*(int*)(rec + 0x250) + 4);
    *(int*)(rec + 0x26c) = *(int*)(*(int*)(rec + 0x250) + 8);
    *(unsigned int*)(rec + 0x244) |= 1;
    *(int*)(rec + 0x2c8) = **(int**)(rec + 0x2b4);
    *(int*)(rec + 0x2cc) = *(int*)(*(int*)(rec + 0x2b4) + 4);
    *(int*)(rec + 0x2d0) = *(int*)(*(int*)(rec + 0x2b4) + 8);
    *(unsigned int*)(rec + 0x2a8) |= 3;
    subobj_deform_rest_init(frameEnt, 0, *(int*)(stateBlk + 0x28));
    subobj_deform_rest_init(frameEnt, 1, *(int*)(stateBlk + 0x2c));
    subobj_deform_rest_init(frameEnt, 2, *(int*)(stateBlk + 0x2c));
    subobj_deform_rest_init(frameEnt, 3, *(int*)(stateBlk + 0x28));
    *(int*)(rec + 0xa0) = 0;
    *(int*)(rec + 0x70) = 0;
    *(int*)(rec + 0x74) = 9;
    *(int*)(rec + 0x9c) = -1;
    *(int*)(rec + 0x78) = 0x84;
    *(int*)(rec + 0x7c) = 0x85;
    *(int*)(rec + 0x80) = 0x86;
    float e = FLOAT_80616154;
    *(int*)(rec + 0x84) = 0x8a;
    float zf = FLOAT_80616158;
    *(int*)(rec + 0x88) = 0x81;
    *(int*)(rec + 0x8c) = 0x82;
    *(int*)(rec + 0xd4) = 1;
    *(int*)(rec + 0xa4) = 0;
    *(int*)(rec + 0xa8) = 10;
    *(int*)(rec + 0xd0) = -1;
    *(int*)(rec + 0xac) = 0x88;
    *(int*)(rec + 0xb0) = 0x89;
    *(int*)(rec + 0xb4) = 0x87;
    *(int*)(rec + 0xb8) = 0;
    *(int*)(rec + 0xbc) = 0x7f;
    *(int*)(rec + 0xc0) = 0x80;
    *(int*)(rec + 0x164) = 0;
    *(int*)(rec + 0x140) = 2;
    *(int*)(rec + 0x4f4) = 6;
    *(int*)(rec + 0x4f8) = 0;
    *(float*)(rec + 0x494) = zf;
    *(float*)(rec + 0x498) = zf;
    float sx = e * ABS(*(float*)(rec + 0x2c8) - *(float*)(rec + 0x19c));
    float sy = e * ABS(*(float*)(rec + 0x1a4) - *(float*)(rec + 0x208));
    *(float*)(rec + 0x49c) = sy;
    *(float*)(rec + 0x4ac) = -sx;
    *(float*)(rec + 0x4b0) = zf;
    *(float*)(rec + 0x4b4) = zf;
    *(float*)(rec + 0x4c4) = zf;
    *(float*)(rec + 0x4c8) = zf;
    *(float*)(rec + 0x4cc) = -sy;
    *(float*)(rec + 0x4dc) = sx;
    *(float*)(rec + 0x4e0) = zf;
    *(float*)(rec + 0x4e4) = zf;
    PARTICLE_EMITTER_SPAWN(rec + 0x1dc, &DAT_805894fc, 0x40, (float*)0, (float*)0);
    PARTICLE_EMITTER_SPAWN(rec + 0x240, &DAT_805894fc, 0x40, (float*)0, (float*)0);
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// 0x8015f538  subobj_deform_build_type4
// ---------------------------------------------------------------------------
// Frame prologue __save_gpr_25/__restore_gpr_25 (frame-registered spawn cb).
// Craft class 4: identical structure to type3 (same bone offsets +0x48/+0x60/
// +0x30/+0x18) but node kinds 3/5/4/2 and a different bone-index set
// (+0x8b/+0x8c/+0x91/+0x8f/+0x8d/+0x8e/+0x90).
// ---------------------------------------------------------------------------
void subobj_deform_build_type4(void)
{
    long long fr = (long long)__save_gpr_25();
    int frameEnt = (int)((unsigned long long)fr >> 0x20);
    int stateBlk = (int)fr;
    int rec = *(int*)(frameEnt + 0x7c);
    *(int*)(rec + 0x184) = 3;
    *(int*)(rec + 0x1e8) = 5;
    *(int*)(rec + 0x24c) = 4;
    *(int*)(rec + 0x2b0) = 2;
    int* strip;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x188) = strip[*strip * -0x30 + 0x1b] + 0x48;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x1ec) = strip[*strip * -0x30 + 0x1b] + 0x60;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x250) = strip[*strip * -0x30 + 0x1b] + 0x30;
    strip = *(int**)(*(int*)(frameEnt + 4) + 4); *(int*)(rec + 0x2b4) = strip[*strip * -0x30 + 0x1b] + 0x18;

    float* p; int b; float dx, dy, dz;
    p = *(float**)(rec + 0x188); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0xa8); dy = p[1] - *(float*)(b + 0xac); dz = p[2] - *(float*)(b + 0xb0);
    *(float*)(rec + 0x198) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x1ec); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0xc0); dy = p[1] - *(float*)(b + 0xc4); dz = p[2] - *(float*)(b + 200);
    *(float*)(rec + 0x1fc) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x250); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0x90); dy = p[1] - *(float*)(b + 0x94); dz = p[2] - *(float*)(b + 0x98);
    *(float*)(rec + 0x260) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));
    p = *(float**)(rec + 0x2b4); strip = *(int**)(*(int*)(frameEnt + 4) + 4);
    b = strip[*strip * -0x30 + 0x1b];
    dx = p[0] - *(float*)(b + 0x78); dy = p[1] - *(float*)(b + 0x7c); dz = p[2] - *(float*)(b + 0x80);
    *(float*)(rec + 0x2c4) = (float)math_sqrt((double)(dz*dz + dx*dx + dy*dy));

    *(int*)(rec + 0x19c) = **(int**)(rec + 0x188);
    *(int*)(rec + 0x1a0) = *(int*)(*(int*)(rec + 0x188) + 4);
    *(int*)(rec + 0x1a4) = *(int*)(*(int*)(rec + 0x188) + 8);
    *(unsigned int*)(rec + 0x17c) |= 3;
    *(int*)(rec + 0x200) = **(int**)(rec + 0x1ec);
    *(int*)(rec + 0x204) = *(int*)(*(int*)(rec + 0x1ec) + 4);
    *(int*)(rec + 0x208) = *(int*)(*(int*)(rec + 0x1ec) + 8);
    *(unsigned int*)(rec + 0x1e0) |= 1;
    *(int*)(rec + 0x264) = **(int**)(rec + 0x250);
    *(int*)(rec + 0x268) = *(int*)(*(int*)(rec + 0x250) + 4);
    *(int*)(rec + 0x26c) = *(int*)(*(int*)(rec + 0x250) + 8);
    *(unsigned int*)(rec + 0x244) |= 1;
    *(int*)(rec + 0x2c8) = **(int**)(rec + 0x2b4);
    *(int*)(rec + 0x2cc) = *(int*)(*(int*)(rec + 0x2b4) + 4);
    *(int*)(rec + 0x2d0) = *(int*)(*(int*)(rec + 0x2b4) + 8);
    *(unsigned int*)(rec + 0x2a8) |= 3;
    subobj_deform_rest_init(frameEnt, 0, *(int*)(stateBlk + 0x28));
    subobj_deform_rest_init(frameEnt, 1, *(int*)(stateBlk + 0x2c));
    subobj_deform_rest_init(frameEnt, 2, *(int*)(stateBlk + 0x2c));
    subobj_deform_rest_init(frameEnt, 3, *(int*)(stateBlk + 0x28));
    *(int*)(rec + 0xa0) = 0;
    *(int*)(rec + 0x70) = 0;
    *(int*)(rec + 0x74) = 9;
    *(int*)(rec + 0x9c) = -1;
    *(int*)(rec + 0x78) = 0x8b;
    *(int*)(rec + 0x7c) = 0x8c;
    *(int*)(rec + 0x80) = 0x91;
    float e = FLOAT_80616154;
    *(int*)(rec + 0x84) = 0x8f;
    float zf = FLOAT_80616158;
    *(int*)(rec + 0x88) = 0x81;
    *(int*)(rec + 0x8c) = 0x82;
    *(int*)(rec + 0xd4) = 1;
    *(int*)(rec + 0xa4) = 0;
    *(int*)(rec + 0xa8) = 10;
    *(int*)(rec + 0xd0) = -1;
    *(int*)(rec + 0xac) = 0x8d;
    *(int*)(rec + 0xb0) = 0x8e;
    *(int*)(rec + 0xb4) = 0x90;
    *(int*)(rec + 0xb8) = 0;
    *(int*)(rec + 0xbc) = 0x7f;
    *(int*)(rec + 0xc0) = 0x80;
    *(int*)(rec + 0x164) = 0;
    *(int*)(rec + 0x140) = 2;
    *(int*)(rec + 0x4f4) = 6;
    *(int*)(rec + 0x4f8) = 0;
    *(float*)(rec + 0x494) = zf;
    *(float*)(rec + 0x498) = zf;
    float sx = e * ABS(*(float*)(rec + 0x2c8) - *(float*)(rec + 0x19c));
    float sy = e * ABS(*(float*)(rec + 0x1a4) - *(float*)(rec + 0x208));
    *(float*)(rec + 0x49c) = sy;
    *(float*)(rec + 0x4ac) = -sx;
    *(float*)(rec + 0x4b0) = zf;
    *(float*)(rec + 0x4b4) = zf;
    *(float*)(rec + 0x4c4) = zf;
    *(float*)(rec + 0x4c8) = zf;
    *(float*)(rec + 0x4cc) = -sy;
    *(float*)(rec + 0x4dc) = sx;
    *(float*)(rec + 0x4e0) = zf;
    *(float*)(rec + 0x4e4) = zf;
    PARTICLE_EMITTER_SPAWN(rec + 0x1dc, &DAT_805894fc, 0x40, (float*)0, (float*)0);
    PARTICLE_EMITTER_SPAWN(rec + 0x240, &DAT_805894fc, 0x40, (float*)0, (float*)0);
    __restore_gpr_25();
}
