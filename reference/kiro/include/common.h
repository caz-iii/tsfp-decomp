#ifndef TSFP_COMMON_H
#define TSFP_COMMON_H

// ============================================================================
// common.h - umbrella header for the tsfp-decomp tree
// ----------------------------------------------------------------------------
// Every translation unit includes this (directly or via a module header). It
// pulls in the shared type definitions, the GameCube SDK stubs, and the
// running forward-declaration table, and provides the small set of Ghidra
// decompiler intrinsics/macros used across the decompiled sources.
//
// Raw placeholder symbols (FUN_xxxxxxxx / DAT_xxxxxxxx / FLOAT_xxxxxxxx /
// DOUBLE_xxxxxxxx) are intentionally NOT declared here: the same binary
// address is referenced with different signatures across translation units,
// so each .cpp declares the placeholders it uses locally (see the per-file
// `extern` blocks). This header only owns things that are safe to share.
// ============================================================================

#include "types.h"
#include "sdk_stubs.h"
#include "forward_decls.h"

#include <stdint.h>
#include <stddef.h>

// ----------------------------------------------------------------------------
// Shared domain types
// ----------------------------------------------------------------------------
// These five types are referenced across many modules (directly and through
// generated_decls.h). They are defined here so every translation unit sees a
// consistent layout. Field sets are derived from observed member accesses in
// the decompiled sources; extend as hardening uncovers more fields.

typedef struct Vec3 {
    f32 x;
    f32 y;
    f32 z;
} Vec3;

// Opaque-ish engine object handles. Members observed in the decomp are listed;
// unknown regions are covered by a trailing pad so pointer arithmetic and
// sizeof stay sane. Treated as forward types where only the pointer is used.
typedef struct Entity Entity;
typedef struct Particle Particle;
typedef struct SpriteLoadContext SpriteLoadContext;
typedef struct StreamRequest StreamRequest;

typedef struct ParticleEmitter {
    struct ParticleEmitter* next;
    void*  def;
    u32    flags;
    u32    flags2;
    u32    state;
    s32    active;
    s32    active_count;
    s32    visible;
    s32    render_index;
    s32    render_layer;
    Vec3   pos;
    Vec3   vel;
    Vec3   target;
    Vec3   bounds_min;
    Vec3   bounds_max;
    f32    emit_rate;
    f32    emit_accumulator;
    f32    spawn_radius;
    f32    speed;
    f32    gravity;
    f32    drag;
    f32    lifetime;
    f32    delay;
    f32    scale_x;
    f32    scale_y;
    f32    size_min;
    f32    size_max;
    u32    color_override;
    s32    color_key_count;
    void*  color_keys;
    void*  color_source_entity;
    void*  attached_entity;
} ParticleEmitter;

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// Ghidra decompiler intrinsics
// ----------------------------------------------------------------------------

// CONCAT44(hi, lo): assemble a 64-bit value from two 32-bit halves.
// Ghidra emits this constantly, most often to rebuild a double via the
// 0x43300000 magic-constant trick: (double)CONCAT44(0x43300000, x) - bias.
static inline u64 tsfp_concat44(u32 hi, u32 lo) {
    return ((u64)hi << 32) | (u64)lo;
}
#ifndef CONCAT44
#define CONCAT44(hi, lo) tsfp_concat44((u32)(hi), (u32)(lo))
#endif

// countLeadingZeros(x): number of leading zero bits in a 32-bit word.
// Ghidra uses `countLeadingZeros(v) >> 5` as a branchless (v == 0) test.
static inline u32 tsfp_clz32(u32 x) {
    if (x == 0) {
        return 32u;
    }
    u32 n = 0;
    while ((x & 0x80000000u) == 0u) {
        x <<= 1;
        n++;
    }
    return n;
}
#ifndef countLeadingZeros
#define countLeadingZeros(x) tsfp_clz32((u32)(x))
#endif

// CARRY4(a, b): does the unsigned 32-bit add a + b produce a carry-out?
// Ghidra emits this when reconstructing 64-bit additions from 32-bit halves.
static inline int tsfp_carry4(u32 a, u32 b) {
    return (a + b) < a;
}
#ifndef CARRY4
#define CARRY4(a, b) tsfp_carry4((u32)(a), (u32)(b))
#endif

// SBORROW4(a, b): signed-overflow flag for the 32-bit subtraction a - b.
static inline int tsfp_sborrow4(s32 a, s32 b) {
    s32 r = (s32)((u32)a - (u32)b);
    return ((a ^ b) & (a ^ r)) < 0;
}
#ifndef SBORROW4
#define SBORROW4(a, b) tsfp_sborrow4((s32)(a), (s32)(b))
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

// ----------------------------------------------------------------------------
// Small value macros
// ----------------------------------------------------------------------------
// ABS is used on both integer and floating operands in the decompiled code,
// so keep it as a type-generic macro rather than a typed function.
#ifndef ABS
#define ABS(x) ((x) < 0 ? -(x) : (x))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#endif // TSFP_COMMON_H
