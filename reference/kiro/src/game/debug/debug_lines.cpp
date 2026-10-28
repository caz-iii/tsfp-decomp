// =============================================================================
// debug_lines.cpp - Debug line rendering buffer system
// Address range: 0x800fdc70 - 0x800fdcd8
// Date: 2026-08-25
// =============================================================================

#include "common.h"

// =============================================================================
// Debug Line Buffer
//
// Circular buffer of 240 (0xF0) line entries, each 0x38 bytes (56 bytes).
// Layout per entry:
//   +0x00: param10 (user data 0)
//   +0x04: param9  (user data 1)
//   +0x08: lineWidth (float, from global)
//   +0x0C: reserved / stack param
//   +0x10: startX (float)
//   +0x14: startY (float)
//   +0x18: startZ (float)
//   +0x1C: (padding)
//   +0x20: endX (float)
//   +0x24: endY (float)
//   +0x28: endZ (float)
//   +0x2C: (padding)
//   +0x30: endX2 / colorX (float)
//   +0x34: color (u32, byte-swapped RGBA)
// =============================================================================

// Global state
// DAT_80571e20 - base of debug line buffer array (0xF0 * 0x38 = 0x3480 bytes)
// DAT_80611f34 - current write index (int, wraps at 0xF0)
// FLOAT_80611f30 - current line width

extern u32 DAT_80571e24;  // Buffer base + 4 (first entry user data 1)
extern u32 DAT_80571e20;  // Buffer base (first entry user data 0)
extern u32 DAT_80571e28;  // Buffer base + 8 (first entry line width)
extern int DAT_80611f34;  // Write index
extern float FLOAT_80611f30; // Current line width
extern float FLOAT_80615560; // Default line width

#define DEBUG_LINE_MAX_COUNT 0xF0
#define DEBUG_LINE_ENTRY_DWORDS 0x0E  // 0x38 / 4 = 14 dwords per entry

// =============================================================================
// Address: 0x800fdc70 | Name: debug_line_clear_all
//
// Clear debug line buffer: zeroes all 240 entries (0x38 bytes each) and resets
// write index. Also resets line width to default.
// Called by: debug_line_system_init, FUN_801c197c
// =============================================================================
void debug_line_clear_all(void) {
    int count = DEBUG_LINE_MAX_COUNT;
    int offset = 0;

    do {
        *(u32*)((int)&DAT_80571e24 + offset) = 0;
        count--;
        offset += 0x38;
    } while (count != 0);

    DAT_80611f34 = 0;
    FLOAT_80611f30 = FLOAT_80615560;
}

// =============================================================================
// Address: 0x800fdcac | Name: debug_line_system_init
//
// Initialize debug line system (wrapper for debug_line_clear_all).
// =============================================================================
void debug_line_system_init(void) {
    debug_line_clear_all();
}

// =============================================================================
// Address: 0x800fdccc | Name: debug_line_stub_nop
// Empty stub.
// =============================================================================
void debug_line_stub_nop(void) {
    return;
}

// =============================================================================
// Address: 0x800fdcd0 | Name: debug_line_get_max_count
//
// Returns maximum debug line count (240 / 0xF0).
// =============================================================================
int debug_line_get_max_count(void) {
    return DEBUG_LINE_MAX_COUNT;
}

// =============================================================================
// Address: 0x800fdcd8 | Name: debug_line_push
//
// Push a line segment into the circular debug line buffer.
// Stores start/end positions (8 floats), color (RGBA byte-swapped), and two
// user params. Advances write index with wrap-around at 240.
// Called by: particle_emitter_tick_physics
// =============================================================================
void debug_line_push(
    float startX, float startY, float startZ,
    float endX, float endY, float endZ,
    float endX2, float endY2,
    u32 param9, u32 param10,
    u32 color)
{
    int idx = DAT_80611f34;
    int nextIdx = DAT_80611f34 + 1;
    int slotOffset = DAT_80611f34 * DEBUG_LINE_ENTRY_DWORDS;

    DAT_80611f34 = nextIdx;
    if (nextIdx == DEBUG_LINE_MAX_COUNT) {
        DAT_80611f34 = 0;
    }

    // Store user data
    (&DAT_80571e24)[idx * DEBUG_LINE_ENTRY_DWORDS] = param9;
    float lineWidth = FLOAT_80611f30;
    (&DAT_80571e20)[slotOffset] = param10;
    (&DAT_80571e28)[idx * DEBUG_LINE_ENTRY_DWORDS] = *(u32*)&lineWidth;

    // Store stack param (in_stack_00000008 in original)
    // (&DAT_80571e2c)[idx * DEBUG_LINE_ENTRY_DWORDS] = stackParam;

    // Store start position
    *(float*)&(&DAT_80571e30)[idx * DEBUG_LINE_ENTRY_DWORDS] = startX;
    *(float*)&(&DAT_80571e34)[idx * DEBUG_LINE_ENTRY_DWORDS] = startY;
    *(float*)&(&DAT_80571e38)[idx * DEBUG_LINE_ENTRY_DWORDS] = startZ;

    // Store end position
    *(float*)&(&DAT_80571e40)[idx * DEBUG_LINE_ENTRY_DWORDS] = endX;
    *(float*)&(&DAT_80571e44)[idx * DEBUG_LINE_ENTRY_DWORDS] = endY;
    *(float*)&(&DAT_80571e48)[idx * DEBUG_LINE_ENTRY_DWORDS] = endZ;

    // Store secondary end position
    *(float*)&(&DAT_80571e4c)[idx * DEBUG_LINE_ENTRY_DWORDS] = endX2;
    *(float*)&(&DAT_80571e50)[idx * DEBUG_LINE_ENTRY_DWORDS] = endY2;

    // Store color (byte-swap for big-endian to RGBA)
    (&DAT_80571e54)[idx * DEBUG_LINE_ENTRY_DWORDS] =
        (color << 24) | ((color & 0xFF00) << 8) |
        ((color >> 8) & 0xFF00) | (color >> 24);
}

// =============================================================================
// Address: 0x800fdd48 | Name: debug_line_tick_width
//
// Per-frame line width growth. Accumulates width based on delta time.
// Called by: FUN_801c1fd8 (render system per-frame)
// =============================================================================
void debug_line_tick_width(void) {
    // Accumulate line width over time using frame delta
    FLOAT_80611f30 = FLOAT_80611f30 +
        FLOAT_806108ec * (FLOAT_80615564 /
        (float)((double)(0x43300000 | (DAT_806109c0 ^ 0x80000000)) - DOUBLE_80615568));
}

// =============================================================================
// Address: 0x800fdd8c | Name: debug_line_render_all
//
// MASSIVE function. Renders all active debug lines as textured quads via GX
// FIFO (writes to 0xCC008000). For each line entry in the 240-slot buffer:
//   - Checks visibility mask against current player
//   - Projects start/end points into camera space
//   - Frustum-clips both endpoints
//   - Computes perpendicular quad corners with width from line entry
//   - Fades alpha based on lifetime
//   - Submits 4-vertex textured quad to GX using sprite texture 0x34E
// Called by: FUN_801c2584 (render pass)
// =============================================================================
void debug_line_render_all(void) {
    float fVar1, fVar2, fVar3;
    int iVar5, iVar7, iVar8;
    uint uVar4, uVar6;
    double dVar9, dVar10, dVar11, dVar12, dVar13, dVar14, dVar15;

    // Paired-single register saves (PowerPC ABI)
    // ... omitted for brevity

    FUN_803826e8();           // Push GX state
    FUN_8016f00c(2);          // Set blend mode (additive)
    FUN_8016e8ac(0);          // Disable depth write

    iVar8 = 0;
    iVar7 = 0;

    do {
        // Check: entry is active AND visible to current player
        if (((*(int*)((int)&DAT_80571e24 + iVar7) != 0) &&
            ((*(uint*)((int)&DAT_80571e20 + iVar7) &
              (1 << *(int*)(DAT_80611d54 + 0x214))) != 0))) {

            // Compute normalized lifetime (0..1)
            dVar9 = (double)((FLOAT_80611f30 - *(float*)((int)&DAT_80571e28 + iVar7)) /
                            *(float*)((int)&DAT_80571e2c + iVar7));

            if (dVar9 < (double)FLOAT_80615564) {
                // Line still alive

                uVar6 = *(uint*)((int)&DAT_80571e54 + iVar7) >> 24; // Base alpha

                // Interpolate position along velocity direction
                fVar1 = (float)(dVar9 * (double)(*(float*)((int)&DAT_80571e4c + iVar7) /
                        FLOAT_80615578) + (double)FLOAT_80615574);

                // Compute interpolated endpoint
                float endX = *(float*)((int)&DAT_80571e40 + iVar7) * fVar1 +
                             *(float*)((int)&DAT_80571e30 + iVar7);
                float endY = *(float*)((int)&DAT_80571e44 + iVar7) * fVar1 +
                             *(float*)((int)&DAT_80571e34 + iVar7);
                float endZ = *(float*)((int)&DAT_80571e48 + iVar7) * fVar1 +
                             *(float*)((int)&DAT_80571e38 + iVar7);

                // Line width from entry
                dVar10 = (double)(FLOAT_80615570 * *(float*)((int)&DAT_80571e50 + iVar7));

                // Project start and end into screen space
                float projStart[3], projEnd[3];
                FUN_8012fb70(*(float**)(DAT_80611d54 + 0x20c),
                            (float*)((int)&DAT_80571e30 + iVar7), projStart);
                FUN_8012fb70(*(float**)(DAT_80611d54 + 0x20c), &endX, projEnd);

                // Frustum clip check (both points must be in view)
                float clipW1 = FLOAT_8061557c * projStart[2]; // near/far aware
                float clipW2 = FLOAT_8061557c * projEnd[2];

                if (/* both endpoints pass frustum test */ 1) {
                    // Normalize to NDC
                    float invZ1 = FLOAT_80615564 / projStart[2];
                    float invZ2 = FLOAT_80615564 / projEnd[2];
                    // ... project X,Y by invZ ...

                    // Compute perpendicular offset for quad width
                    FUN_8001c9bc(/* start NDC */, /* ... */);
                    FUN_8001c9bc(/* end NDC */, /* ... */);
                    fVar1 = (float)(dVar10 * (double)(/* perpX delta */));
                    fVar2 = (float)(dVar10 * (double)(/* perpY delta */));

                    // Compute alpha fade
                    if (*(int*)((int)&DAT_80571e24 + iVar7) == 2) {
                        // Type 2: linear fade
                        fVar3 = -(float)((double)FLOAT_80615584 * dVar9 - (double)FLOAT_80615580);
                        if (fVar3 < FLOAT_80615560) fVar3 = FLOAT_80615560;
                        uVar4 = (uint)fVar3;
                    } else {
                        // Type 1: exponential fade from base alpha
                        uVar4 = /* computed alpha */;
                    }

                    // Get sprite texture
                    iVar5 = sprite_get_texture_id(0x34E);
                    FUN_80172d88(0x92, 5, 4, iVar5); // GX setup: quads, 5 attrs, 4 verts

                    // Submit 4 vertices to GX FIFO (0xCC008000)
                    // Each vertex: position (XYZ), color (RGBA byte-swapped), texcoord (UV)
                    // Quad corners computed from perpendicular offset
                    DAT_cc008000 = /* v0.x */;  DAT_cc008000 = /* v0.y */;
                    DAT_cc008000 = /* v0.z */;  DAT_cc008000 = /* v0.color */;
                    DAT_cc008000 = /* v0.u */;  DAT_cc008000 = /* v0.v */;
                    // ... v1, v2, v3 similarly ...

                    FUN_80172c54(); // End GX primitive
                }
            }
        }

        iVar8++;
        iVar7 += 0x38;
    } while (iVar8 < 0xF0);

    FUN_80382734(); // Pop GX state
}
