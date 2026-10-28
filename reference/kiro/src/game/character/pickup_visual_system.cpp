// Address: 0x800f6774 - 0x800f705c | Batch 308 | Date: 2026-08-25
// Pickup item visual system: fade state machine, blink, bob, spin, glow
// Also includes ai_health_check_state (nearby in binary)
// Subsystem: game (pickup/item visuals)

#include "common.h"
#include "entity.h"
#include "character.h"
#include "math_utils.h"

// ============================================================================
// Pickup Fade State Machine
//
// State layout within pickup data structure:
//   +0x04: flags (0x10=fade active, 0x400=bob, 0x800=glow, 0x1000=spin, 0x1000000=blink)
//   +0x20: state (0=fading in, 1=fully visible, 2=fading out)
//   +0x24: progress timer (accumulates per frame)
//   +0x28: hold/trigger time (when to transition from visible to fadeout)
//   +0x2c: duration (total fade time for current phase)
//   +0x30: final alpha target
//   +0x34: random flicker delay
//   +0x38: random flicker param A
//   +0x3c: random flicker param B
//
// Blink state machine (overlay):
//   +0x40: blink state (0=starting, 1=visible, 2=hidden)
//   +0x44: visible duration
//   +0x48: hidden duration
//   +0x4c: blink timer
//   +0x50: current phase duration
// ============================================================================

// ============================================================================
// Address: 0x800f6774 | Name: pickup_fade_start_fadein
// Initializes fade-in. Sets state=0, resets progress, stores duration/target.
// If currently fading out (state 2), computes remaining and restarts.
// ============================================================================
void pickup_fade_start_fadein(float duration, float targetAlpha, int pPickupData) {
    *(float*)(pPickupData + 0x28) = 0.0f; // Clear hold time

    if ((*(u32*)(pPickupData + 4) & 0x10) == 0) {
        // Not yet active - initialize fresh
        *(u32*)(pPickupData + 4) |= 0x10;     // Enable fade
        *(int*)(pPickupData + 0x20) = 0;       // State: fading in
        *(float*)(pPickupData + 0x24) = 0.0f;  // Reset progress
    } else if (*(int*)(pPickupData + 0x20) == 2) {
        // Currently fading out - reverse with remaining progress
        float remaining = 1.0f - *(float*)(pPickupData + 0x24) / *(float*)(pPickupData + 0x2c);
        *(float*)(pPickupData + 0x24) = remaining * duration;
        *(int*)(pPickupData + 0x20) = 0; // Back to fading in
    }

    *(float*)(pPickupData + 0x30) = targetAlpha; // Final alpha
    *(float*)(pPickupData + 0x2c) = duration;    // Fade duration
}

// ============================================================================
// Address: 0x800f67dc | Name: pickup_fade_start_fadeout
// Begins fade-out. Computes remaining progress from current state
// and sets state=2 (fading out) with the given duration.
// ============================================================================
void pickup_fade_start_fadeout(float duration, int pPickupData) {
    if ((*(u32*)(pPickupData + 4) & 0x10) == 0) {
        // Not active - just set progress to zero
        *(float*)(pPickupData + 0x24) = 0.0f;
    } else if (*(int*)(pPickupData + 0x20) == 0) {
        // Fading in - reverse with remaining
        float remaining = 1.0f - *(float*)(pPickupData + 0x24) / *(float*)(pPickupData + 0x2c);
        *(float*)(pPickupData + 0x24) = remaining * duration;
    } else if (*(int*)(pPickupData + 0x20) == 1) {
        // Fully visible - start fresh
        *(float*)(pPickupData + 0x24) = 0.0f;
    }

    *(int*)(pPickupData + 0x20) = 2;         // State: fading out
    *(float*)(pPickupData + 0x2c) = duration; // Duration
}

// ============================================================================
// Address: 0x800f6840 | Name: pickup_blink_start
// Starts blink/flash sequence. Sets visible/hidden durations, enables
// blink flag 0x1000000, and begins with a fade-in.
// Ignored if already blinking.
// ============================================================================
void pickup_blink_start(float visibleDuration, float hiddenDuration, int pPickupData) {
    if ((*(u32*)(pPickupData + 4) & 0x1000000) != 0) {
        return; // Already blinking
    }

    *(int*)(pPickupData + 0x40) = 0;                   // Blink state: starting
    *(float*)(pPickupData + 0x44) = visibleDuration;   // Visible hold time
    *(float*)(pPickupData + 0x48) = hiddenDuration;    // Hidden hold time
    *(u32*)(pPickupData + 4) |= 0x1000000;             // Enable blink

    // Begin fade-in at speed 2.0, target alpha 1.0
    pickup_fade_start_fadein(2.0f, 1.0f, pPickupData);
}

// ============================================================================
// Address: 0x800f6890 | Name: pickup_blink_stop
// Stops blink sequence: clears blink flag and initiates final fade-out.
// ============================================================================
void pickup_blink_stop(int pPickupData) {
    *(u32*)(pPickupData + 4) &= ~0x01000000; // Clear blink flag
    pickup_fade_start_fadeout(2.0f, pPickupData);
}

// ============================================================================
// Address: 0x800f68c0 | Name: pickup_fade_get_alpha
// Returns current opacity (0.0 to 1.0) based on fade state machine.
//   State 0 (fading in):  1.0 - (progress / duration)  [starts at 1, goes to 0]
//   State 1 (fully visible): returns 0.0
//   State 2 (fading out): progress / duration [starts at 0, goes to 1]
//   Not active: returns 1.0
// ============================================================================
float pickup_fade_get_alpha(int pPickupData) {
    if ((*(u32*)(pPickupData + 4) & 0x10) == 0) {
        return 1.0f; // Not active - fully opaque
    }

    int state = *(int*)(pPickupData + 0x20);

    if (state == 1) {
        return 0.0f; // Fully visible phase
    }

    if (state > 0) {
        if (state > 2) {
            return 1.0f; // Invalid state
        }
        // State 2: fading out - return progress ratio
        return *(float*)(pPickupData + 0x24) / *(float*)(pPickupData + 0x2c);
    }

    if (state < 0) {
        return 1.0f; // Invalid state
    }

    // State 0: fading in - return inverse progress
    return 1.0f - (*(float*)(pPickupData + 0x24) / *(float*)(pPickupData + 0x2c));
}

// ============================================================================
// Address: 0x800f6924 | Name: pickup_visual_tick
// Main per-frame tick for pickup visual effects.
// Handles blink state machine, fade advancement, random collision shape
// changes for flickering visual effect.
// ============================================================================
void pickup_visual_tick(int pPickupData) {
    // Frame delta computation
    float frameDelta = g_frame_rate_scale *
                       (1.0f / (float)((double)frame_counter_combined - DOUBLE_OFFSET));

    // --- Blink state machine ---
    if ((*(u32*)(pPickupData + 4) & 0x1000000) != 0) {
        int blinkState = *(int*)(pPickupData + 0x40);

        if (blinkState == 0) {
            // State 0: just started - begin fade-in and transition to visible
            pickup_fade_start_fadein(2.0f, 0.25f, pPickupData);
            *(float*)(pPickupData + 0x4c) = 0.0f;
            // Random visible duration (1.0 + random * range)
            float rndDuration = *(float*)(pPickupData + 0x44) *
                               (1.0f + ((float)(rng_next() - RNG_OFFSET) / RNG_RANGE - 1.0f));
            *(float*)(pPickupData + 0x50) = rndDuration;
            *(int*)(pPickupData + 0x40) = 1; // -> visible
        } else if (blinkState == 1) {
            // State 1: visible hold - count up timer
            *(float*)(pPickupData + 0x4c) += frameDelta;
            if (*(float*)(pPickupData + 0x50) < *(float*)(pPickupData + 0x4c)) {
                // Timer expired - start fade-out
                pickup_fade_start_fadeout(2.0f, pPickupData);
                *(float*)(pPickupData + 0x4c) = 0.0f;
                float rndDuration = *(float*)(pPickupData + 0x48) *
                                   (1.0f + ((float)(rng_next() - RNG_OFFSET) / RNG_RANGE - 1.0f));
                *(float*)(pPickupData + 0x50) = rndDuration;
                *(int*)(pPickupData + 0x40) = 2; // -> hidden
            }
        } else if (blinkState == 2) {
            // State 2: hidden wait - count up timer
            *(float*)(pPickupData + 0x4c) += frameDelta;
            if (*(float*)(pPickupData + 0x50) < *(float*)(pPickupData + 0x4c)) {
                *(int*)(pPickupData + 0x40) = 0; // -> restart cycle
            }
        }
    }

    // --- Fade state advancement ---
    if ((*(u32*)(pPickupData + 4) & 0x10) != 0) {
        *(float*)(pPickupData + 0x24) += frameDelta;

        int fadeState = *(int*)(pPickupData + 0x20);

        if (fadeState == 1) {
            // Fully visible: handle random flicker delays
            if (*(float*)(pPickupData + 0x34) <= 0.0f) {
                // Check random chance to start flicker
                float chance = rng_float();
                float threshold = FLICKER_CHANCE * *(float*)(pPickupData + 0x30) *
                                  (float)(frame_counter_combined - DOUBLE_OFFSET);
                if (chance < threshold) {
                    *(float*)(pPickupData + 0x38) = 0.0f * rng_float();
                    *(float*)(pPickupData + 0x3c) = 0.25f * rng_float();
                    *(float*)(pPickupData + 0x34) = 2.0f * rng_float() + 2.0f;
                }
            } else {
                *(float*)(pPickupData + 0x34) -= frameDelta;
                if (*(float*)(pPickupData + 0x38) <= 0.0f) {
                    *(float*)(pPickupData + 0x3c) -= frameDelta;
                    if (*(float*)(pPickupData + 0x3c) < 0.0f) {
                        *(float*)(pPickupData + 0x38) = 0.0f * rng_float();
                        *(float*)(pPickupData + 0x3c) = 0.25f * rng_float();
                    }
                } else {
                    *(float*)(pPickupData + 0x38) -= frameDelta;
                }
            }

            // Check hold time trigger
            if (*(float*)(pPickupData + 0x28) > 0.0f &&
                *(float*)(pPickupData + 0x28) <= *(float*)(pPickupData + 0x24)) {
                *(float*)(pPickupData + 0x24) = 0.0f;
                *(int*)(pPickupData + 0x20) = 2;      // -> fading out
                *(float*)(pPickupData + 0x2c) = 0.5f; // Quick fade
            }
        } else if (fadeState == 0) {
            // Fading in: check if complete
            if (*(float*)(pPickupData + 0x2c) <= *(float*)(pPickupData + 0x24)) {
                *(float*)(pPickupData + 0x24) = 0.0f;
                *(int*)(pPickupData + 0x20) = 1;       // -> fully visible
                *(float*)(pPickupData + 0x34) = 0.0f;  // Reset flicker
            }
        } else if (fadeState == 2) {
            // Fading out: check if complete
            if (*(float*)(pPickupData + 0x2c) <= *(float*)(pPickupData + 0x24)) {
                *(float*)(pPickupData + 0x24) = 0.0f;
                *(u32*)(pPickupData + 4) &= ~0x10; // Clear fade flag
            }
        }

        // --- Collision shape flickering ---
        if ((*(u32*)(pPickupData + 4) & 0x10) != 0) {
            float alpha = pickup_fade_get_alpha(pPickupData);
            int shapeVal = (int)(SHAPE_SCALE * alpha);

            if (shapeVal == 0) {
                character_set_collision_shape(pPickupData, 2, 0x13, 4, 0x10);
                character_set_collision_shape(pPickupData, 4, 0x10, 4, 0x20);
            } else if (*(int*)(pPickupData + 0x20) == 1 || g_frame_counter_raw < 1) {
                character_set_collision_shape(pPickupData, 3, 0xf, 4, (char)shapeVal);
            } else if ((shapeVal > 4 && shapeVal < 7) ||
                       (shapeVal > 14 && shapeVal < 17) ||
                       (shapeVal > 19 && shapeVal < 22) ||
                       (shapeVal > 36 && shapeVal < 39)) {
                u32 rndShape = rng_next() % 0x6b + 0x14;
                character_set_collision_shape(pPickupData, 3, 0x13, 4, (char)rndShape);
            }
        }
    }
}

// ============================================================================
// Address: 0x800f6d68 | Name: ai_health_check_state
// AI health evaluation callback. When health state is 1 (damaged),
// checks if health is below 50% threshold to trigger urgency state.
// Computes approach speed and calls AI state transition.
// ============================================================================
void ai_health_check_state(u32* pAIData) {
    if (pAIData[8] != 1) {
        return; // Only process in "damaged" state
    }

    float health = (float)pAIData[9];
    float alpha = 1.0f; // Default full urgency
    u32 nextState = 1;  // Default: stay in current state

    if (health >= 0.5f) {
        // Health above threshold - check secondary conditions
        float param1 = (float)pAIData[0xd];
        float param2 = (float)pAIData[0xe];
        if (param1 > 0.0f && param2 > 0.0f) {
            nextState = 2; // Transition to flee/seek health
        }
    } else {
        // Health below threshold - urgent state transition
        nextState = 2;
        float urgency = -(AI_HEALTH_SPEED * (AI_HEALTH_SCALE * health) - 1.0f);
        alpha = urgency;
    }

    ai_set_state_transition(alpha, /*speed param*/ 0.0f, pAIData, nextState);
}

// ============================================================================
// Address: 0x800f6de4 | Name: pickup_bob_tick
// Per-frame vertical bob animation. Timer counts down; computes sine-wave
// Y offset scaled by entity bob amplitude. Stops when timer reaches zero.
// ============================================================================
void pickup_bob_tick(int* pPickupData) {
    int pEntity = *pPickupData;

    // Frame delta: count down timer
    float frameDelta = g_frame_rate_scale *
                       (1.0f / (float)((double)frame_counter_combined - DOUBLE_OFFSET));

    pPickupData[0x2a] = (int)-(frameDelta - (float)pPickupData[0x2a]);
    float timer = (float)pPickupData[0x2a];

    if (timer > 0.0f) {
        // Active: compute bob offset
        float amplitude = *(float*)(pEntity + 0xb0c);
        float bobThreshold = BOB_THRESHOLD; // FLOAT_80615488

        if (timer > bobThreshold) {
            // Ramping up
            float scale = 2.0f * (timer - bobThreshold) * 2.0f + 2.0f;
            entity_set_y_offset(amplitude * scale, pEntity); // FUN_8002413c
        } else if (timer < 0.5f) {
            // Ramping down
            float scale = 2.0f * (0.5f - timer) * 2.0f + 2.0f;
            entity_set_y_offset(amplitude * scale, pEntity);
        }
    } else {
        // Timer expired - stop bob
        pPickupData[1] &= ~0x400;        // Clear bob flag
        pPickupData[0x2a] = 0;            // Zero timer
        entity_clear_y_offset(pEntity);   // FUN_800241d8
    }
}

// ============================================================================
// Address: 0x800f6ec4 | Name: pickup_spin_tick
// Per-frame spin animation. Advances rotation timer, computes angle
// from sine table, updates entity rotation and angular velocity.
// Also updates player_set_character_scale if entity is player-owned.
// Resets on timer expiry (clears flag 0x1000).
// ============================================================================
void pickup_spin_tick(int* pPickupData) {
    int pEntity = *pPickupData;

    // Frame delta: count down timer
    float frameDelta = g_frame_rate_scale *
                       (1.0f / (float)((double)frame_counter_combined - DOUBLE_OFFSET));

    pPickupData[0x2b] = (int)-(frameDelta - (float)pPickupData[0x2b]);

    if ((float)pPickupData[0x2b] > 0.0f) {
        // Active: compute rotation
        float angle = sin_table_lookup(*(s16*)(pEntity + 0x48)); // FUN_803641f8
        *(float*)(pEntity + 0xb14) = 0.5f * angle;
        *(float*)(pEntity + 0xad0) = *(float*)(pEntity + 0xb14);

        // Update player scale if player-owned
        if (*(int*)(pEntity + 8) < g_max_players) {
            float scaledAngle = 0.5f * angle;
            player_set_character_scale(scaledAngle,
                                       g_player_data_base + *(int*)(pEntity + 8) * PLAYER_STRIDE);
        }

        // Angular velocity
        *(float*)(pEntity + 0xb2c) =
            (SPIN_SCALE * *(float*)(pEntity + 0xb14)) /
            (SPIN_RATE * (float)((double)frame_counter_combined - DOUBLE_OFFSET));
    } else {
        // Timer expired - stop spin
        pPickupData[1] &= ~0x1000; // Clear spin flag

        float angle = sin_table_lookup(*(s16*)(pEntity + 0x48));
        *(float*)(pEntity + 0xb14) = angle;
        *(float*)(pEntity + 0xad0) = *(float*)(pEntity + 0xb14);
        pPickupData[0x2b] = 0; // Zero timer

        if (*(int*)(pEntity + 8) < g_max_players) {
            player_set_character_scale(*(float*)(pEntity + 0xb14),
                                       g_player_data_base + *(int*)(pEntity + 8) * PLAYER_STRIDE);
        }

        *(float*)(pEntity + 0xb2c) =
            (SPIN_SCALE * *(float*)(pEntity + 0xb14)) /
            (SPIN_RATE * (float)((double)frame_counter_combined - DOUBLE_OFFSET));
    }
}

// ============================================================================
// Address: 0x800f705c | Name: pickup_glow_decay_tick
// Per-frame glow decay. Decreases glow timer at frame rate.
// Clears glow flag (0x800) when timer reaches zero.
// ============================================================================
void pickup_glow_decay_tick(int pPickupData) {
    float frameDelta = g_frame_rate_scale *
                       (1.0f / (float)((double)frame_counter_combined - DOUBLE_OFFSET));

    *(float*)(pPickupData + 0xb0) = -(frameDelta - *(float*)(pPickupData + 0xb0));

    if (*(float*)(pPickupData + 0xb0) <= 0.0f) {
        *(u32*)(pPickupData + 4) &= ~0x800; // Clear glow flag
        *(float*)(pPickupData + 0xb0) = 0.0f;
    }
}
