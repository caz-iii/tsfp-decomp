// Address: 0x800c517c - 0x800c8f18 | Batch 274 | Date: 2026-08-25
// Projectile rendering + tick functions for various weapon types
// Covers beam rendering, helix/spiral effects, lightning arcs,
// continuous fire, bullet, grenade, and homing projectile logic.

#include "common.h"
#include "player_ai_projectile.h"
#include "gx_fifo.h"
#include "particle.h"
#include "collision.h"
#include "sprite.h"

// ============================================================================
// 0x800c517c | player_ai_projectile_beam_render_quads
// Renders beam weapon billboard quads along the projectile ray direction.
// Uses segmented tristrip with LOD (2-4 segments based on distance).
// Texture 0x33F, GX tristrip submission.
// ============================================================================
void player_ai_projectile_beam_render_quads(void) {
    int slot = FUN_803826bc();
    int texId = sprite_get_texture_id(0x33F);
    int isLocal = player_ai_projectile_is_local_player(slot);

    float timeFrac = 1.0f - *(float*)(slot + 0x1FC);

    float* origin;
    float* direction;
    float beamLength;
    if (isLocal == 0) {
        origin = (float*)(slot + 0x34);
        direction = (float*)(slot + 0x7C);
        beamLength = *(float*)(slot + 0x1EC);
    } else {
        origin = (float*)(slot + 0x28);
        direction = (float*)(slot + 0x70);
        beamLength = *(float*)(slot + 0x1E8);
    }

    FUN_8016e8ac(2);
    FUN_8016f00c(2);
    player_ai_projectile_beam_render();

    // Compute color from time fraction
    float life = *(float*)(slot + 0x1FC);
    float colorBlend = FLOAT_80614cdc * timeFrac;
    int rChannel = FUN_803825c0((float)(FLOAT_80614cf8 * life + colorBlend));
    int gChannel = FUN_803825c0((float)(FLOAT_80614ca8 * life + colorBlend));
    int bChannel = FUN_803825c0((float)(FLOAT_80614cf4 * timeFrac + FLOAT_80614af4 * life));
    int alpha = *(int*)(slot + 0x1F0);
    uint color = (bChannel << 16) | gChannel | (rChannel << 8);

    if (beamLength > 0.0f) {
        // Compute perpendicular basis vectors (cross with up or right)
        float perpX, perpY, perpZ;
        float crossX, crossY, crossZ;
        if (fabsf(direction[1]) < FLOAT_80614cb4) {
            perpX = direction[0] * DAT_803cc6a4 - direction[1] * DAT_803cc6a0;
            perpY = direction[1] * DAT_803cc6a8 - direction[2] * DAT_803cc6a4;
            perpZ = direction[2] * DAT_803cc6a0 - direction[0] * DAT_803cc6a8;
        } else {
            perpX = direction[0] * DAT_803cc698 - direction[1] * DAT_803cc694;
            perpY = direction[1] * DAT_803cc69c - direction[2] * DAT_803cc698;
            perpZ = direction[2] * DAT_803cc694 - direction[0] * DAT_803cc69c;
        }
        // Second cross product for billboard tangent
        crossX = perpZ * direction[2] - perpX * direction[1];
        crossY = perpX * direction[0] - perpY * direction[2];
        crossZ = perpY * direction[1] - perpZ * direction[0];

        // LOD segment count
        int segCount;
        if (beamLength > *(float*)(&DAT_806107bc + isLocal * 4)) {
            segCount = (beamLength >= FLOAT_80614b28) ? 4 : 3;
        } else {
            segCount = 2;
        }

        FUN_80130cd8(*(float**)(DAT_80611d54 + 0x208), (float*)&DAT_8058e3cc);
        DAT_80612250 = 1;
        DAT_80612258 = 0;

        // Render 2 beam strips (dual-sided)
        for (uint i = 0; i < 2; i++) {
            float angleOffset = *(float*)(slot + 0x1E4) +
                FLOAT_80614a78 * (float)((int)i - 0); // per-strip offset
            uint angleLUT = (uint)(FLOAT_80614c2c *
                (*(float*)(slot + 0x1DC) +
                 FLOAT_80614abc * *(float*)(slot + 0x98) +
                 (FLOAT_80614c28 * *(float*)(&DAT_806107c4 + i * 4)) / FLOAT_80614b6c) +
                FLOAT_80614a78);
            float sinVal = ((float*)&DAT_80583228)[angleLUT & 0x7FF];
            float cosVal = *(float*)((int)&DAT_80583228 + ((angleLUT + 0x200) * 4 & 0x1FFC));

            FUN_80172a4c(2, 0x92, 0, (short)(segCount << 1), texId);

            for (int seg = 0; seg < segCount; seg++) {
                bool isEndpoint = (seg == 0) || (seg == 3);
                float dist;
                if (seg == 0) dist = 0.0f;
                else if (seg == segCount - 1) dist = beamLength;
                else if (seg == 1) dist = *(float*)(&DAT_806107bc + isLocal * 4);
                else if (seg == 2) dist = FLOAT_80614cfc;
                else dist = 0.0f;

                float texU = FLOAT_80614acc * dist + angleOffset;
                uint vtxColor = color | (alpha << 24);
                float width = FLOAT_80614a84;
                if (isEndpoint) {
                    vtxColor = color;
                    width = FLOAT_80614a70;
                }

                float offsetA = cosVal * width;
                float offsetB = sinVal * width;
                float posX = dist * direction[0] + origin[0];
                float posY = dist * direction[1] + origin[1];
                float posZ = dist * direction[2] + origin[2];

                // Vertex 1 (+offset)
                GX_FIFO_F32 = posX + perpY * offsetA + crossX * offsetB;
                GX_FIFO_F32 = posY + perpZ * offsetA + crossY * offsetB;
                GX_FIFO_F32 = posZ + perpX * offsetA + crossZ * offsetB;
                GX_FIFO_U32 = BSWAP32(vtxColor);
                GX_FIFO_F32 = texU;
                GX_FIFO_F32 = 0.0f;

                // Vertex 2 (-offset)
                GX_FIFO_F32 = posX - perpY * offsetA - crossX * offsetB;
                GX_FIFO_F32 = posY - perpZ * offsetA - crossY * offsetB;
                GX_FIFO_F32 = posZ - perpX * offsetA - crossZ * offsetB;
                GX_FIFO_U32 = BSWAP32(vtxColor);
                GX_FIFO_F32 = texU;
                GX_FIFO_F32 = 1.0f;
            }
            FUN_80172c54();
        }
    }
    FUN_80382708();
}

// ============================================================================
// 0x800c56d4 | player_ai_projectile_debris_init
// Initializes 50 debris particle entries with random positions/velocities.
// Populates a 0x20-stride particle array at DAT_80568aa4.
// Sets endpoint position from direction * FLOAT_80614b28.
// ============================================================================
void player_ai_projectile_debris_init(void) {
    int slot = FUN_803826e4();

    *(float*)(slot + 0x1E4) = 0.0f;
    *(int*)(slot + 0x1F0) = 0;
    *(float*)(slot + 0x1DC) = 0.0f;
    *(float*)(slot + 0x1F4) = 0.0f;
    *(float*)(slot + 0x1F8) = FLOAT_80614c28;

    // Set endpoint = origin + direction * beamMaxLength
    float maxLen = FLOAT_80614b28;
    *(float*)(slot + 0x1A0) = maxLen * *(float*)(slot + 0x64) + *(float*)(slot + 0x40);
    *(float*)(slot + 0x1A4) = maxLen * *(float*)(slot + 0x68) + *(float*)(slot + 0x44);
    *(float*)(slot + 0x1A8) = maxLen * *(float*)(slot + 0x6C) + *(float*)(slot + 0x48);
    *(int*)(slot + 0x1C) = *(int*)(slot + 0x40);
    *(int*)(slot + 0x20) = *(int*)(slot + 0x44);
    *(int*)(slot + 0x24) = *(int*)(slot + 0x48);

    // Initialize 50 debris particles with random positions + zero velocity
    for (int i = 0; i < 50; i++) {
        int offset = i * 0x20;
        float* pos = (float*)((int)&DAT_80568aa4 + offset);

        uint rng = FUN_8013104c();
        pos[0] = FLOAT_80614a9c * (((float)(rng) / FLOAT_80614bf4) - FLOAT_80614a78);
        rng = FUN_8013104c();
        *(float*)((int)&DAT_80568aa8 + offset) =
            FLOAT_80614a9c * (((float)(rng) / FLOAT_80614bf4) - FLOAT_80614a78);
        rng = FUN_8013104c();
        *(float*)((int)&DAT_80568aac + offset) =
            FLOAT_80614a9c * (((float)(rng) / FLOAT_80614bf4) - FLOAT_80614a78);

        // Scale positions
        pos[0] *= FLOAT_80614ad8;
        *(float*)((int)&DAT_80568aa8 + offset) *= FLOAT_80614ad8;
        *(float*)((int)&DAT_80568aac + offset) *= FLOAT_80614ad8;

        // Zero velocity
        *(float*)((int)&DAT_80568ab0 + offset) = 0.0f;
        *(float*)((int)&DAT_80568ab4 + offset) = 0.0f;
        *(float*)((int)&DAT_80568ab8 + offset) = 0.0f;

        // Size
        *(float*)((int)&DAT_80568ac0 + offset) = FLOAT_80614abc;
    }
    FUN_80382730();
}

// ============================================================================
// 0x800c58bc | player_ai_projectile_continuous_tick
// Per-frame tick for continuous-fire weapon projectiles.
// Raycasts along direction, applies damage on hit, computes cross-product
// basis vectors, updates position/endpoint, spawns trail particles.
// ============================================================================
void player_ai_projectile_continuous_tick(int* pSlot) {
    if (DAT_806108e4 == 0) return;

    pSlot[0x28] = 0;

    // Compute oscillation amplitude from time
    float frac = FLOAT_80614c28 * (float)pSlot[0x26];
    float osc1 = (uint)(FLOAT_80614c2c * (FLOAT_80614ce4 * frac) + FLOAT_80614a78);
    float osc2 = (uint)(FLOAT_80614c2c * (FLOAT_80614ce8 * frac) + FLOAT_80614a78);
    int oscIdx = (int)(FLOAT_80614c2c * (FLOAT_80614abc * frac) + FLOAT_80614a78);

    // Composite oscillation for opacity
    float opacity = FLOAT_80614b50 *
        (FLOAT_80614a78 + *(float*)((int)&DAT_80583228 + ((oscIdx + 0x200) * 4 & 0x1FFC))) +
        FLOAT_80614aa0 * (FLOAT_80614a78 + ((float*)&DAT_80583228)[(uint)osc2 & 0x7FF]) +
        (FLOAT_80614acc * (FLOAT_80614a78 + ((float*)&DAT_80583228)[(uint)osc1 & 0x7FF]) + FLOAT_80614a78);
    opacity = clamp(opacity, 0.0f, 1.0f);
    pSlot[0x80] = (int)opacity;

    pSlot[0x77] = (int)((float)(FUN_8013104c() - DOUBLE_80614bf8) / FLOAT_80614bf4);
    pSlot[0x7C] = FUN_8013104c() % 0x7F;

    if (pSlot[0x54] == 0) {
        *(float*)(*(int*)(*(int*)(pSlot[0x42] + 0x7C) + 0x6C) + 0x108) = 0.0f;
        pSlot[0x54] = 1;
    }

    // Advance position
    float speed = FLOAT_80614bd4 * (float)pSlot[0x26];
    pSlot[7] = (int)((float)pSlot[0x19] * speed);
    pSlot[8] = (int)((float)pSlot[0x1A] * speed);
    pSlot[9] = (int)((float)pSlot[0x1B] * speed);
    pSlot[7] = (int)((float)pSlot[7] + (float)pSlot[0x10]);
    pSlot[8] = (int)((float)pSlot[8] + (float)pSlot[0x11]);
    pSlot[9] = (int)((float)pSlot[9] + (float)pSlot[0x12]);

    // Raycast step
    float deltaX = (float)pSlot[0x19] * FLOAT_806108ec * (1.0f / (float)(DAT_806109c0));
    float deltaY = (float)pSlot[0x1A] * FLOAT_806108ec * (1.0f / (float)(DAT_806109c0));
    float deltaZ = (float)pSlot[0x1B] * FLOAT_806108ec * (1.0f / (float)(DAT_806109c0));
    float rayDir[3] = { deltaX, deltaY, deltaZ };
    float hitPos[3], hitNormal[3];
    int damageInfo[2] = { 0, 0 };

    int hitEntity = player_ai_fire_raycast_simple(pSlot, rayDir, hitPos, hitNormal, damageInfo);
    pSlot[0x28] = hitEntity;

    if (pSlot[0x28] == 0) {
        // No hit - extend position
        pSlot[7] = (int)((float)pSlot[7] + deltaX);
        pSlot[8] = (int)((float)pSlot[8] + deltaY);
        pSlot[9] = (int)((float)pSlot[9] + deltaZ);

        // Distance check - deactivate if too far
        float dx = (float)pSlot[7] - (float)pSlot[0x10];
        float dy = (float)pSlot[8] - (float)pSlot[0x11];
        float dz = (float)pSlot[9] - (float)pSlot[0x12];
        if (FLOAT_80614d00 < (dz * dz + dx * dx + dy * dy)) {
            int attached = pSlot[0x42];
            if (attached != 0 && *(int*)(attached + 0x20) == 0x200) {
                *(float*)(*(int*)(*(int*)(attached + 0x7C) + 0x6C) + 0x108) = 0.0f;
            }
            player_ai_slot_deactivate();
            return;
        }
    }

    if (damageInfo[0] > 0) {
        player_ai_on_damage_received(pSlot, (int)damageInfo);
    }

    if (pSlot[0x28] != 0) {
        collision_result_restore((int)auStack_1cc);
        if (*(int*)(pSlot[0x28] + 0x20) == 8) {
            player_ai_projectile_play_impact_sound();
            player_ai_voice_pitch_update(pSlot);
        }
        // Occasional trail particle
        uint rng = FUN_8013104c();
        if ((rng & 0xF) == 0) {
            int trailType = FUN_801c1714();
            particle_spawn_trail(0, 0x36, ~-(uint)(trailType == 1) & 0x80000, (float*)(pSlot + 7), hitNormal);
        }
        // Dispatch damage
        switch_slot_reset((int)local_1ac);
        float dmgValue = switch_slot_load_from_def((int)local_1ac, pSlot);
        local_1ac[0] = pSlot[0x28];
        damage_resolve_and_dispatch(dmgValue, ...);
        FUN_801f1cd8(pSlot[0x3C], pSlot + 7, hitNormal);

        int attached = pSlot[0x42];
        if (attached != 0 && *(int*)(attached + 0x20) == 0x200) {
            *(float*)(*(int*)(*(int*)(attached + 0x7C) + 0x6C) + 0x108) = 0.0f;
        }
        player_ai_slot_deactivate();
    }

    // Update drift oscillation
    if ((float)pSlot[0x79] < 1.0f) {
        float driftAmp = FLOAT_80614ac4 * (float)pSlot[0x79];
        float driftSq = driftAmp * driftAmp;
        float dampedDrift = fmaxf(FLOAT_80614bac, driftSq);
        uint rng2 = FUN_8013104c();
        float rngNorm = FLOAT_806108ec * (1.0f / (float)(DAT_806109c0)) *
            (FLOAT_80614ad8 * ((float)(rng2) / FLOAT_80614bf4) + FLOAT_80614b20);
        pSlot[0x79] = (int)((float)(rngNorm * dampedDrift + (float)pSlot[0x79]));
        pSlot[0x79] = (int)clamp((float)pSlot[0x79], 0.0f, 1.0f);
    }

    // Advance angle offsets
    pSlot[0x7D] = (int)(FLOAT_80614cec * FLOAT_806108ec + (float)pSlot[0x7D]);
    pSlot[0x7E] = (int)(FLOAT_80614cf0 * FLOAT_806108ec + (float)pSlot[0x7E]);

    // Compute perpendicular basis (cross product with world up/right)
    float dirY = (float)pSlot[0x1A];
    if (fabsf(dirY) < FLOAT_80614cb4) {
        pSlot[0x71] = (int)(dirY * DAT_803cc6c0 - (float)pSlot[0x1B] * DAT_803cc6bc);
        pSlot[0x72] = (int)((float)pSlot[0x1B] * DAT_803cc6b8 - (float)pSlot[0x19] * DAT_803cc6c0);
        pSlot[0x73] = (int)((float)pSlot[0x19] * DAT_803cc6bc - dirY * DAT_803cc6b8);
    } else {
        pSlot[0x71] = (int)(dirY * DAT_803cc6b4 - (float)pSlot[0x1B] * DAT_803cc6b0);
        pSlot[0x72] = (int)((float)pSlot[0x1B] * DAT_803cc6ac - (float)pSlot[0x19] * DAT_803cc6b4);
        pSlot[0x73] = (int)((float)pSlot[0x19] * DAT_803cc6b0 - dirY * DAT_803cc6ac);
    }

    // Second cross for full orthogonal basis
    pSlot[0x74] = (int)((float)pSlot[0x72] * (float)pSlot[0x1B] - (float)pSlot[0x73] * (float)pSlot[0x1A]);
    pSlot[0x75] = (int)((float)pSlot[0x73] * (float)pSlot[0x19] - (float)pSlot[0x71] * (float)pSlot[0x1B]);
    pSlot[0x76] = (int)((float)pSlot[0x71] * (float)pSlot[0x1A] - (float)pSlot[0x72] * (float)pSlot[0x19]);

    // Set endpoint
    float endLen = FLOAT_80614b28;
    pSlot[0x68] = (int)(endLen * (float)pSlot[0x19] + (float)pSlot[0x10]);
    pSlot[0x69] = (int)(endLen * (float)pSlot[0x1A] + (float)pSlot[0x11]);
    pSlot[0x6A] = (int)(endLen * (float)pSlot[0x1B] + (float)pSlot[0x12]);
}

// ============================================================================
// 0x800c5f64 | player_ai_projectile_helix_beam_render
// Renders helix/spiral beam effect with camera shake and rotating quads.
// 20 iteration loop with 4 quads each, plus 50 billboard sparks.
// ============================================================================
void player_ai_projectile_helix_beam_render(void) {
    // [MASSIVE function - spiral beam with camera shake + billboard debris]
    // Computes beam distance, applies camera shake to all players,
    // builds perpendicular cross product basis from direction vector,
    // submits 20 sets of 4 rotating quads along the beam axis,
    // then renders 50 billboard sparks (texture 0x28F) at beam endpoint.
    // GX strip rendering with FLOAT_80614d08 rotation rate.
    // See Ghidra decompilation for full vertex math.
}

// ============================================================================
// 0x800c69e4 | player_ai_projectile_helix_debris_render
// Renders helix beam with physics-driven debris particles.
// Similar to helix_beam_render but with velocity integration on particles.
// ============================================================================
void player_ai_projectile_helix_debris_render(void) {
    // [MASSIVE function - helix beam with physics debris]
    // When t==0.0: initializes 50 debris particles (same as debris_init).
    // When 0 < t <= 1.0: renders rotating quads along beam axis,
    // integrates particle positions using velocity with damping,
    // and renders 50 camera-facing billboard sparks.
    // Physics: applies directional force * dt^2 / distance for attraction,
    // then integrates velocity -> position with constant damping factor.
}

// ============================================================================
// 0x800c740c | player_ai_projectile_bullet_tick
// Standard bullet projectile per-frame tick.
// Raycasts along direction, applies damage on hit, handles ricochet.
// ============================================================================
void player_ai_projectile_bullet_tick(int* pSlot) {
    if (DAT_806108e4 <= 0) return;

    // Skip check: if flag 0x200 set and time exceeded threshold
    if ((DAT_805015c0 & 0x200) != 0 && (float)pSlot[0x26] > FLOAT_80614c80) {
        player_ai_slot_deactivate();
        return;
    }

    pSlot[0x28] = 0;

    // Compute opacity oscillation (same composite as continuous_tick)
    float frac = FLOAT_80614c28 * (float)pSlot[0x26];
    // ... oscillation computation ...
    pSlot[0x80] = (int)clamp(opacity, 0.0f, 1.0f);
    pSlot[0x77] = (int)((float)(FUN_8013104c()) / FLOAT_80614bf4);
    pSlot[0x7C] = FUN_8013104c() % 0x7F;

    // Set endpoint from direction * maxLen
    float maxLen = FLOAT_80614b28;
    float rayX = maxLen * (float)pSlot[0x19];
    float rayY = maxLen * (float)pSlot[0x1A];
    float rayZ = maxLen * (float)pSlot[0x1B];
    pSlot[0x68] = (int)((float)pSlot[7] + rayX);
    pSlot[0x69] = (int)((float)pSlot[8] + rayY);
    pSlot[0x6A] = (int)((float)pSlot[9] + rayZ);

    // Raycast
    float rayDir[3] = { rayX, rayY, rayZ };
    int damageInfo[2] = { 0, 0 };
    int hitEntity = player_ai_fire_raycast_simple(pSlot, rayDir, (float*)(pSlot + 0x68), hitNormal, damageInfo);
    pSlot[0x28] = hitEntity;

    if (damageInfo[0] > 0) {
        player_ai_on_damage_received(pSlot, (int)damageInfo);
    }

    // Compute distance traveled
    float distSq = ((float)pSlot[0x68] - (float)pSlot[7]) * ((float)pSlot[0x68] - (float)pSlot[7]) +
                   ((float)pSlot[0x69] - (float)pSlot[8]) * ((float)pSlot[0x69] - (float)pSlot[8]);
    pSlot[0x3D] = (int)distSq; // approximate

    if (pSlot[0x28] == 0) {
        // No hit - set endpoint at full range
        pSlot[0x68] = (int)((float)pSlot[7] + rayX);
        pSlot[0x69] = (int)((float)pSlot[8] + rayY);
        pSlot[0x6A] = (int)((float)pSlot[9] + rayZ);
    } else {
        collision_result_restore((int)auStack_1b0);
        if (*(int*)(pSlot[0x28] + 0x20) == 8) {
            player_ai_voice_pitch_update(pSlot);
        }
        // Occasional impact spark
        uint rng = FUN_8013104c();
        if (((pSlot[0x42] != 0) && ((rng & 3) == 0)) ||
            ((pSlot[0x42] == 0) && (rng == (rng / 20) * 20))) {
            player_ai_impact_dispatch(pSlot, *pSlot, pSlot[0x28],
                (float*)(pSlot + 0x68), hitNormal, hitNormal, pSlot[0x42]);
        }
        // Dispatch damage
        switch_slot_reset((int)local_190);
        float dmg = switch_slot_load_from_def((int)local_190, pSlot);
        local_190[0] = pSlot[0x28];
        damage_resolve_and_dispatch(dmg, ...);

        // Ricochet: if shooter entity exists and hit surface is backface
        if (pSlot[0x43] != 0 && local_1a8 == 2 &&
            (*(uint*)(*(int*)(pSlot[0x28] + 0x24) + 0x28) & 0x8000) != 0) {
            // Apply secondary damage to shooter
            float bounceDir[3] = { -hitNormal[0], -hitNormal[1], -hitNormal[2] };
            switch_slot_reset((int)local_190);
            float dmg2 = switch_slot_load_from_def((int)local_190, pSlot);
            local_190[0] = pSlot[0x43];
            damage_resolve_and_dispatch(dmg2, ...);
            bRicochet = true;
        }
    }

    // Update rotation
    float rngAngle = FUN_8013104c();
    pSlot[0x79] = (int)(-(FLOAT_806108ec * (1.0f / (float)(DAT_806109c0)) *
        (FLOAT_80614ad8 * ((float)(rngAngle) / FLOAT_80614bf4) + FLOAT_80614b20) - (float)pSlot[0x79]));
    while ((float)pSlot[0x79] < 0.0f) {
        pSlot[0x79] = (int)((float)pSlot[0x79] + 1.0f);
    }

    // Advance per-axis oscillation angles
    for (int a = 0; a < 2; a++) {
        float rate = (a == 0) ? FLOAT_80614cec : FLOAT_80614cf0;
        *(float*)((int)pSlot + 500 + a * 4) += FLOAT_806108ec * rate;
    }

    // Ricochet feedback factor
    if (bRicochet) {
        pSlot[0x7F] = (int)((1.0f / (float)(DAT_806109c0)) * FLOAT_80614b20 * FLOAT_806108ec + (float)pSlot[0x7F]);
        if (1.0f < (float)pSlot[0x7F]) pSlot[0x7F] = (int)1.0f;
    } else {
        pSlot[0x7F] = (int)(-(FLOAT_806108ec * (1.0f / (float)(DAT_806109c0)) - (float)pSlot[0x7F]));
        if ((float)pSlot[0x7F] < 0.0f) pSlot[0x7F] = (int)0.0f;
    }

    // Compute cross-product basis (same pattern as continuous_tick)
    // ...
}

// ============================================================================
// 0x800c7ac8 | player_ai_projectile_lightning_render
// Renders lightning/electric beam effect with arcing quads.
// Endpoint flare billboard + 16 arc segments + 16 point flares.
// ============================================================================
void player_ai_projectile_lightning_render(void) {
    // [MASSIVE function - lightning arc beam]
    // Uses texture 0x340 for endpoint flare (projected billboard),
    // texture 0x33A for arc segments, texture 0x28D for point flares.
    // 16 arc segments with random cubic-damped displacement from beam axis.
    // 2 endpoint flares rendered as screen-space projected quads.
    // Each arc segment uses sinusoidal displacement + rotation matrix.
    // Color computed from FUN_80132348 with per-vertex randomization.
    // See Ghidra decompilation for full GX vertex submission.
}

// ============================================================================
// 0x800c8714 | player_ai_projectile_spawn_glow_effects
// Spawns 4 layered billboard glow effects on a projectile entity.
// Used for rocket/grenade glow trail visuals.
// ============================================================================
void player_ai_projectile_spawn_glow_effects(int pSlot) {
    player_ai_projectile_clear_effect_counter(pSlot);

    // Layer 1: main glow (effect 0x53)
    *(int*)(pSlot + 0xAC) = particle_spawn_billboard(
        pSlot + 0xAC, 0x53, 0, (int*)(pSlot + 0x1C), 0xFFFF);
    particle_slot_set_opacity(FLOAT_80614d2c, *(int**)(pSlot + 0xAC));

    // Layer 2: secondary glow (effect 0x53, dimmer)
    *(int*)(pSlot + 0xB0) = particle_spawn_billboard(
        pSlot + 0xB0, 0x53, 0, (int*)(pSlot + 0x1C), 0xFFFF);
    particle_slot_set_opacity(FLOAT_80614b84, *(int**)(pSlot + 0xB0));

    // Layer 3: inner core (effect 0x54)
    *(int*)(pSlot + 0xB4) = particle_spawn_billboard(
        pSlot + 0xB4, 0x54, 0, (int*)(pSlot + 0x1C), 0xFFFF);
    particle_slot_set_opacity(FLOAT_80614d30, *(int**)(pSlot + 0xB4));

    // Layer 4: outer halo (effect 0x55)
    *(int*)(pSlot + 0xB8) = particle_spawn_billboard(
        pSlot + 0xB8, 0x55, 0, (int*)(pSlot + 0x1C), 0xFFFF);
    particle_slot_set_opacity(FLOAT_80614d34, *(int**)(pSlot + 0xB8));
}

// ============================================================================
// 0x800c87fc | player_ai_projectile_grenade_tick
// Grenade/rocket projectile per-frame tick.
// Advances by velocity*dt, raycasts for collision, dispatches on hit.
// ============================================================================
void player_ai_projectile_grenade_tick(int* pSlot) {
    if (DAT_806108e4 == 0) return;

    // Compute movement delta = velocity * dt
    float deltaX = (float)pSlot[0x19] * FLOAT_806108ec;
    float deltaY = (float)pSlot[0x1A] * FLOAT_806108ec;
    float deltaZ = (float)pSlot[0x1B] * FLOAT_806108ec;

    int damageInfo[2] = { 0, 0 };
    float hitPos[3], hitNormal[3];
    int hitEntity = player_ai_fire_raycast(pSlot, &deltaX, hitPos, hitNormal, damageInfo);

    if (damageInfo[0] > 0) {
        player_ai_on_damage_received(pSlot, (int)damageInfo);
    }

    // Normalize direction
    float speed = sqrtf((float)pSlot[0x1B] * (float)pSlot[0x1B] +
                        (float)pSlot[0x19] * (float)pSlot[0x19] +
                        (float)pSlot[0x1A] * (float)pSlot[0x1A]);
    float invSpeed = 1.0f / speed;
    float dirX = (float)pSlot[0x19] * invSpeed;
    float dirY = (float)pSlot[0x1A] * invSpeed;
    float dirZ = (float)pSlot[0x1B] * invSpeed;

    if (hitEntity == 0) {
        // No hit: advance position
        pSlot[7]    = (int)((float)pSlot[7] + deltaX);
        pSlot[8]    = (int)((float)pSlot[8] + deltaY);
        pSlot[9]    = (int)((float)pSlot[9] + deltaZ);
        pSlot[0xD]  = (int)((float)pSlot[0xD] + deltaX);
        pSlot[0xE]  = (int)((float)pSlot[0xE] + deltaY);
        pSlot[0xF]  = (int)((float)pSlot[0xF] + deltaZ);
        pSlot[0x3D] = (int)((float)pSlot[0x3D] + deltaX*deltaX + deltaY*deltaY + deltaZ*deltaZ);

        // Check lifetime expiry
        if ((float)pSlot[0x26] >= (float)pSlot[0x5F]) {
            player_ai_projectile_spawn_emitter(pSlot, 0, (float*)(pSlot + 7), &dirX);
            player_ai_slot_deactivate();
        } else if (pSlot[0x2A] != 0) {
            // Update attached entity position + rotation
            *(int*)(pSlot[0x2A] + 0x64) = pSlot[7];
            *(int*)(pSlot[0x2A] + 0x68) = pSlot[8];
            *(int*)(pSlot[0x2A] + 0x6C) = pSlot[9];
            float angVel = *(float*)(pSlot[0x2A] + 0xD0) * FLOAT_806108ec +
                           *(float*)(pSlot[0x2A] + 0xCC);
            float wrapped = FUN_80390324((angVel - 0.0f) / FLOAT_80614b70);
            *(float*)(pSlot[0x2A] + 0xCC) = -(FLOAT_80614b70 * wrapped - angVel);
            FUN_80130730((float*)(pSlot[0x2A] + 0x274), &dirX);
            FUN_8012f05c(*(float*)(pSlot[0x2A] + 0xCC), afStack_1cc);
            FUN_80130bd4(afStack_1cc, (float*)(pSlot[0x2A] + 0x274), (float*)(pSlot[0x2A] + 0x274));
        }
    } else {
        // Hit: compute reflection, dispatch damage
        float dot = 2.0f * (dirX * hitNormal[0] + dirY * hitNormal[1] + dirZ * hitNormal[2]);
        float reflX = dirX - hitNormal[0] * dot;
        float reflY = dirY - hitNormal[1] * dot;
        float reflZ = dirZ - hitNormal[2] * dot;

        pSlot[0x3D] = (int)((float)pSlot[0x3D] + /* dist to hit */);

        player_ai_impact_dispatch(pSlot, *pSlot, hitEntity, hitPos, hitNormal, &reflX, pSlot[0x42]);
        player_ai_alert_fire_radius(FLOAT_80614b20, (int)pSlot, *(int*)(hitEntity + 0x20), hitPos);

        switch_slot_reset((int)local_18c);
        float dmg = switch_slot_load_from_def((int)local_18c, pSlot);
        local_18c[0] = hitEntity;
        damage_resolve_and_dispatch(dmg, ...);

        player_ai_projectile_proximity_detonate();
        player_ai_projectile_spawn_emitter(pSlot, hitEntity, hitPos, hitNormal);
        player_ai_slot_deactivate();
    }
}

// ============================================================================
// 0x800c8ba4 | player_ai_projectile_homing_tick
// Homing/guided projectile per-frame tick.
// Normalizes direction, accelerates, raycasts, detonates on impact.
// ============================================================================
void player_ai_projectile_homing_tick(int* pSlot) {
    if (DAT_806108e4 == 0) return;

    // Normalize velocity to get direction
    float speed = sqrtf((float)pSlot[0x1B] * (float)pSlot[0x1B] +
                        (float)pSlot[0x19] * (float)pSlot[0x19] +
                        (float)pSlot[0x1A] * (float)pSlot[0x1A]);
    float invSpeed = 1.0f / speed;
    float dirX = (float)pSlot[0x19] * invSpeed;
    float dirY = (float)pSlot[0x1A] * invSpeed;
    float dirZ = (float)pSlot[0x1B] * invSpeed;

    int damageInfo[2] = { 0, 0 };

    // Acceleration = direction * speed_factor * dt
    float accelFactor = (1.0f / (float)(DAT_806109c0)) * (float)pSlot[0x22] * FLOAT_806108ec;
    float accelX = dirX * accelFactor;
    float accelY = dirY * accelFactor;
    float accelZ = dirZ * accelFactor;

    float hitPos[3], hitNormal[3];
    int hitEntity = player_ai_fire_raycast(pSlot, &accelX, hitPos, hitNormal, damageInfo);

    if (damageInfo[0] > 0) {
        player_ai_on_damage_received(pSlot, (int)damageInfo);
    }

    if (hitEntity == 0) {
        // No hit: advance position
        pSlot[7]    = (int)((float)pSlot[7] + accelX);
        pSlot[8]    = (int)((float)pSlot[8] + accelY);
        pSlot[9]    = (int)((float)pSlot[9] + accelZ);
        pSlot[0xD]  = (int)((float)pSlot[0xD] + accelX);
        pSlot[0xE]  = (int)((float)pSlot[0xE] + accelY);
        pSlot[0xF]  = (int)((float)pSlot[0xF] + accelZ);
        pSlot[0x3D] = (int)((float)pSlot[0x3D] + accelFactor * accelFactor);

        // Update attached entity position + angular velocity
        if (pSlot[0x2A] != 0) {
            *(int*)(pSlot[0x2A] + 0x64) = pSlot[7];
            *(int*)(pSlot[0x2A] + 0x68) = pSlot[8];
            *(int*)(pSlot[0x2A] + 0x6C) = pSlot[9];
            float angVel = *(float*)(pSlot[0x2A] + 0xD0) * FLOAT_806108ec +
                           *(float*)(pSlot[0x2A] + 0xCC);
            float wrapped = FUN_80390324((angVel - 0.0f) / FLOAT_80614b70);
            *(float*)(pSlot[0x2A] + 0xCC) = -(FLOAT_80614b70 * wrapped - angVel);
            FUN_80130730((float*)(pSlot[0x2A] + 0x274), &dirX);
            FUN_8012f05c(*(float*)(pSlot[0x2A] + 0xCC), afStack_1cc);
            FUN_80130bd4(afStack_1cc, (float*)(pSlot[0x2A] + 0x274), (float*)(pSlot[0x2A] + 0x274));
        }
    } else {
        // Hit: compute reflection and dispatch
        float dot = 2.0f * (dirX * hitNormal[0] + dirY * hitNormal[1] + dirZ * hitNormal[2]);
        float reflX = dirX - hitNormal[0] * dot;
        float reflY = dirY - hitNormal[1] * dot;
        float reflZ = dirZ - hitNormal[2] * dot;

        pSlot[0x3D] = (int)((float)pSlot[0x3D] + /* dist to hit */);

        switch_slot_reset((int)local_18c);
        float dmg = switch_slot_load_from_def((int)local_18c, pSlot);
        local_18c[0] = hitEntity;
        damage_resolve_and_dispatch(dmg, ...);

        player_ai_impact_dispatch(pSlot, *pSlot, hitEntity, hitPos, hitNormal, &reflX, pSlot[0x42]);
        player_ai_alert_fire_radius(FLOAT_80614b20, (int)pSlot, *(int*)(hitEntity + 0x20), hitPos);
        player_ai_explosion_spawn(10, *pSlot, (float*)(pSlot + 7), NULL, pSlot[0x43], 0, 1);
        player_ai_projectile_proximity_detonate();
        player_ai_slot_deactivate();
    }
}
