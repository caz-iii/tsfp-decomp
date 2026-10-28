// Address: 0x8010aa00 - 0x8010b914 | Batch 330 | Date: 2026-08-25
// HUD gameplay icon rendering: compass/directional indicators, radar panel,
// powerup status icons, weapon icon, and ammo type icons.

#include "common.h"
#include "hud.h"
#include "sprite.h"

// ============================================================================
// Address: 0x8010aa00 | hud_draw_compass_indicator
// Draws a rotated directional arrow sprite (0x10b) pointing toward a tracked
// target entity. Computes angle from player-to-target delta using atan2,
// applies rotation via sine table lookup for screen-space positioning.
// ============================================================================
void hud_draw_compass_indicator(void) {
    float fDeltaX, fDeltaY, fDeltaZ;
    float fDistance, fAngle;
    float fScreenX, fScreenY;
    float *pTargetPos;
    int iSpriteId;
    int iTexData;
    u32 uTexW, uTexH;

    double dBaseX = (double)FUN_803826f0();

    // Y position: top of radar area
    double dBaseY = (double)(FLOAT_806157f8 * FLOAT_80610830 +
                   (float)((double)CONCAT44(0x43300000,
                    DAT_8057ff50 + *(int *)(DAT_80611748 + 0x14) ^ 0x80000000) - DOUBLE_80615738));

    iSpriteId = sprite_get_texture_id(0x10b);
    FUN_8016e77c(iSpriteId);

    // Get target position from extra_r4 (entity position)
    pTargetPos = /* extraout_r4 */;
    fDeltaX = pTargetPos[0] - *(float *)(DAT_80611d74 + 0xa8);
    fDeltaY = pTargetPos[1] - *(float *)(DAT_80611d74 + 0xac);
    fDeltaZ = pTargetPos[2] - *(float *)(DAT_80611d74 + 0xb0);

    // Compute distance
    fDistance = FUN_80131538((double)(float)(fDeltaZ * fDeltaZ +
               (double)(float)(fDeltaX * fDeltaX + fDeltaY * fDeltaY)));

    // Compute angle (atan2 of rotated XZ)
    fAngle = FUN_80390aa4(
        (double)(float)(fDeltaX * FLOAT_806157fc - fDeltaZ * FLOAT_80615800),
        (double)(float)(fDeltaX * FLOAT_80615800 + fDeltaZ * FLOAT_806157fc));

    // Get texture dimensions
    iTexData = *DAT_80610a20 + iSpriteId * 0x58;
    uTexW = (u32)*(u16 *)(iTexData + 0x4e);
    uTexH = (u32)*(u16 *)(iTexData + 0x52);
    int iHalfW = (int)uTexH >> 1;
    int iHalfH = (int)uTexW >> 1;

    // Compute screen position from angle + distance (clamped)
    float fRadius = FLOAT_80615804 * (float)((double)fDistance / (double)FLOAT_80615808);
    u32 uTableIdx = (u32)(FLOAT_806157c0 * fAngle + FLOAT_80615734);

    fScreenX = (float)((double)(FLOAT_8061082c *
               -(fRadius * (float)(&DAT_80583228)[uTableIdx & 0x7ff])) + dBaseX);
    fScreenY = (float)((double)(FLOAT_80610830 *
               -(fRadius * *(float *)((int)&DAT_80583228 + ((uTableIdx + 0x200) * 4 & 0x1ffc)))) +
               dBaseY);

    // Draw rotated sprite
    FUN_80170410((int)(fScreenX - (float)iHalfH), (int)(fScreenY - (float)iHalfW),
                 (int)(fScreenX + (float)iHalfH), (int)(fScreenY + (float)iHalfW),
                 0, 0, uTexW << 5, uTexH << 5);

    FUN_8038273c();
}

// ============================================================================
// Address: 0x8010acb8 | hud_draw_radar_panel
// MASSIVE radar/minimap panel renderer. Draws a bordered frame (sprite 0xe0),
// background image (sprite 0x10a), then iterates tracked targets rendering
// directional indicators via hud_draw_compass_indicator.
// Renders up to 3 primary markers + additional markers from a secondary list.
// ============================================================================
void hud_draw_radar_panel(void) {
    int iSpriteId;
    int iTexData;
    u32 uTexW, uTexH;
    double dLeft, dTop, dRight, dBottom;
    double dPanelWidth, dPanelHeight;

    FUN_803826e8();
    dPanelWidth = (double)(FLOAT_80615734 * FLOAT_8061082c);
    dPanelHeight = (double)(FLOAT_80615734 * FLOAT_80610830);

    FUN_8016f00c(1);

    // Draw frame (sprite 0xe0) - 3-slice vertical
    iSpriteId = sprite_get_texture_id(0xe0);
    iTexData = *DAT_80610a20 + iSpriteId * 0x58;
    uTexH = (u32)*(u16 *)(iTexData + 0x52);
    uTexW = (u32)*(u16 *)(iTexData + 0x4e);

    // Compute panel geometry
    double dFrameH = (double)(float)(FLOAT_80610830 *
                    (float)(dPanelHeight * (float)((double)CONCAT44(0x43300000,
                     uTexH ^ 0x80000000) - DOUBLE_80615738)));
    dTop = (double)(float)((double)FLOAT_806157d4 * (double)FLOAT_80610830 +
           (float)((double)CONCAT44(0x43300000,
            DAT_8057ff50 + DAT_80611748[5] ^ 0x80000000) - DOUBLE_80615738));
    double dMid = (double)(float)((float)dTop + (float)dFrameH);
    double dLow = (double)(float)((double)FLOAT_8061580c * (double)FLOAT_80610830 + (float)dMid);
    dBottom = (double)(float)((float)dLow + (float)dFrameH);

    // Right-aligned panel X
    dLeft = /* computed from viewport right edge */;
    dRight = /* left + panel width */;

    // Draw border
    hud_render_screen_border(dLeft, dTop, dRight, dBottom, (double)FLOAT_80615718);

    // Draw 3-slice frame texture
    FUN_8016e77c(iSpriteId);
    FUN_80170b10(dLeft, dTop, dRight, dMid, 0, 0, uTexW << 4, uTexH << 4);
    FUN_80170b10(dLeft, dMid, dRight, dLow, 0, uTexH << 4, uTexW << 4, uTexH << 4);
    FUN_80170b10(dLeft, dLow, dRight, dBottom, 0, uTexH << 4, uTexW << 4, 0);

    // Draw background (sprite 0x10a) with clip rect
    iSpriteId = sprite_get_texture_id(0x10a);
    iTexData = *DAT_80610a20 + iSpriteId * 0x58;
    uTexW = (u32)*(u16 *)(iTexData + 0x4e);
    uTexH = (u32)*(u16 *)(iTexData + 0x52);

    FUN_8016f0e4(/* clip rect */);
    // ... draw background tiles

    FUN_8016e77c(iSpriteId);
    // 4 background tile draws
    FUN_80170b10(/* 4 tile positions */);

    // Draw tracked target indicators
    int iMaxTargets = *(int *)(DAT_806128c0 + 0x28);
    for (int i = 0; i <= iMaxTargets; i++) {
        if ((i < 3) && (*(int *)(DAT_806128c0 + i * 4 + 0x34) != 0)) {
            hud_draw_compass_indicator();
        }
    }

    // Draw secondary target list
    int iSecondaryCount = *(int *)(DAT_806128c0 + 0x40);
    for (int i = 0; i < iSecondaryCount; i++) {
        if (*(int *)(DAT_806128c0 + i * 4 + 0x44) != 0) {
            hud_draw_compass_indicator();
        }
    }

    // Restore clip rect
    FUN_8016f0e4(*DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
    FUN_80382734();
}

// ============================================================================
// Address: 0x8010b224 | hud_draw_powerup_indicators
// Draws 4 horizontal powerup status icons with fade alpha:
//   Sprite 0xf0 = Health (cyan 0x5060), flag bit 0x10
//   Sprite 0xf1 = Armor (yellow 0x604000), flag bit 0x1000
//   Sprite 0xf2 = Speed (blue 0x6010), flag bit 0x400
//   Sprite 0xf3 = Damage (purple 0x1060), flag bit 0x800
// Alpha fades from pickup_fade_get_alpha or timer-based ratio.
// ============================================================================
void hud_draw_powerup_indicators(void) {
    u32 *pYCursor;
    int iPlayerData;
    u32 uFlags;
    double dAlpha;
    double dIconSize;
    float fX, fY;

    pYCursor = (u32 *)FUN_803826e0();
    double dFadeAlpha = (double)FLOAT_80615750;
    dIconSize = (double)(FLOAT_80615814 * (float)((double)FLOAT_806157b4 * (double)FLOAT_8061082c));

    iPlayerData = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    int iPowerupData = iPlayerData + 0xc68;
    double dIconHeight = (double)(FLOAT_80610830 * (float)(dIconSize / (double)FLOAT_8061082c));

    u32 uColor = ((int)(dFadeAlpha * /* alpha */) & 0x3fc) << 22 | 0x7f7f7f;

    fX = (float)((double)CONCAT44(0x43300000,
         DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738);
    fY = (float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) - DOUBLE_80615738);

    uFlags = *(u32 *)(iPlayerData + 0xc6c);

    // Icon 1: Health powerup (sprite 0xf0, flag 0x10)
    if ((uFlags & 0x10) == 0) {
        dAlpha = 0.0;
    } else {
        dAlpha = pickup_fade_get_alpha(iPowerupData);
        dAlpha = (double)(FLOAT_80615718 - (float)dAlpha);
        // Clamp to [0, 1]
    }
    int iSprite = sprite_get_texture_id(0xf0);
    FUN_80132228(dAlpha, uColor | 0x5060, uColor); // cyan
    FUN_8016e77c(iSprite);
    FUN_80170b10(fX, fY, fX + dIconSize, fY + dIconHeight, /* UV */);
    fX += dIconSize;

    // Icon 2: Armor powerup (sprite 0xf1, flag 0x1000)
    if ((uFlags & 0x1000) == 0) {
        dAlpha = 0.0;
    } else {
        dAlpha = (double)(*(float *)(iPlayerData + 0xd14) / FLOAT_80615784);
        // Clamp to [0, 1]
    }
    iSprite = sprite_get_texture_id(0xf1);
    FUN_80132228(dAlpha, uColor | 0x604000, uColor); // yellow
    FUN_8016e77c(iSprite);
    FUN_80170b10(fX, fY, fX + dIconSize, fY + dIconHeight, /* UV */);
    fX += dIconSize;

    // Icon 3: Speed powerup (sprite 0xf2, flag 0x400)
    if ((uFlags & 0x400) == 0) {
        dAlpha = 0.0;
    } else {
        dAlpha = (double)(*(float *)(iPlayerData + 0xd10) / FLOAT_806157cc);
        // Clamp to [0, 1]
    }
    iSprite = sprite_get_texture_id(0xf2);
    FUN_80132228(dAlpha, uColor | 0x6010, uColor); // blue
    FUN_8016e77c(iSprite);
    FUN_80170b10(fX, fY, fX + dIconSize, fY + dIconHeight, /* UV */);
    fX += dIconSize;

    // Icon 4: Damage powerup (sprite 0xf3, flag 0x800)
    if ((uFlags & 0x800) == 0) {
        dAlpha = 0.0;
    } else {
        dAlpha = (double)(*(float *)(iPlayerData + 0xd18) / FLOAT_806157cc);
        // Clamp to [0, 1]
    }
    iSprite = sprite_get_texture_id(0xf3);
    FUN_80132228(dAlpha, uColor | 0x1060, uColor); // purple
    FUN_8016e77c(iSprite);
    FUN_80170b10(fX, fY, fX + dIconSize, fY + dIconHeight, /* UV */);

    // Advance Y cursor
    *pYCursor = (int)((double)(float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) -
                DOUBLE_80615738) + dIconHeight);
    FUN_8038272c();
}

// ============================================================================
// Address: 0x8010b720 | hud_draw_weapon_icon
// Draws the current weapon/item icon (sprite 0xde) at the right side of the
// HUD. Only draws if player has an active weapon (offset 0x80 != 0).
// Adjusts icon size with aspect ratio preservation.
// ============================================================================
void hud_draw_weapon_icon(void) {
    int *pYCursor;
    int iSpriteId;
    int iTexData;
    u32 uTexW, uTexH;
    u32 uScaledW, uScaledH;
    int iDrawX, iDrawY;

    pYCursor = (int *)FUN_803826dc();

    if (*(int *)(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c) + 0x80) == 0) {
        FUN_80382728();
        return;
    }

    // Compute X position (right-aligned center)
    double dRightCenter = (double)CONCAT44(0x43300000,
                          DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738;
    float fCenterX = FLOAT_80615734 * FLOAT_806157b4 * FLOAT_8061082c;

    int iBaseY = *pYCursor;
    double dAlpha = /* extraout_f1 */;

    // Compute alpha from DAT_80611a51
    FUN_803825c0((double)(float)(dAlpha *
                (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80611a51) - DOUBLE_80615740)));

    iSpriteId = sprite_get_texture_id(0xde);
    iTexData = *DAT_80610a20 + iSpriteId * 0x58;
    uTexW = (u32)*(u16 *)(iTexData + 0x4e);
    uTexH = (u32)*(u16 *)(iTexData + 0x52);

    uScaledW = (u32)((float)((double)CONCAT44(0x43300000, uTexW ^ 0x80000000) - DOUBLE_80615738) *
               FLOAT_8061082c);
    uScaledH = (int)((float)((double)CONCAT44(0x43300000, uTexH ^ 0x80000000) - DOUBLE_80615738) *
               FLOAT_80610830);

    iDrawX = (int)(fCenterX + (float)dRightCenter) -
             (((int)uScaledW >> 1) + (u32)((int)uScaledW < 0 && (uScaledW & 1) != 0));

    if (dAlpha > 0.0) {
        FUN_8016e77c(iSpriteId);
        FUN_80170410(iDrawX, iBaseY, iDrawX + uScaledW, iBaseY + uScaledH,
                     0, 0, uTexW << 4, uTexH << 4);
    }

    // Advance Y cursor with spacing
    *pYCursor = uScaledH + (4 >> (DAT_80611748[0xd] & 0x3f)) + *pYCursor;
    FUN_80382728();
}

// ============================================================================
// Address: 0x8010b914 | hud_draw_ammo_icons
// Draws up to 4 vertical ammo/inventory type icons. Each icon type is
// resolved via FUN_8010e690:
//   Type 0x1b = primary ammo
//   Type 0x19 = secondary ammo
//   Type 0x1a = grenades
//   Type 0x1c = special
// Icon size adjusts for 3+ players (0x18 vs 0x20 pixels).
// Aspect ratio is preserved from texture dimensions.
// ============================================================================
void hud_draw_ammo_icons(void) {
    int *pYCursor;
    int iSpriteId;
    int iTexData;
    u32 uTexW, uTexH;
    int iDrawX, iDrawY;
    u32 uIconSize;
    int iHalfSize;
    int iScaledH;

    pYCursor = (int *)FUN_803826d0();
    double dAlpha = /* extraout_f1 */;
    player_ai_projectile_count_owned();

    // Select icon size based on player count
    int iPlayerCount = FUN_801c1714();
    uIconSize = 0x20; // 32px
    if (iPlayerCount > 2) {
        uIconSize = 0x18; // 24px for 3+ players
    }
    iHalfSize = (int)uIconSize >> 1;

    // X position: right-aligned center
    iDrawX = (int)(FLOAT_80615734 * FLOAT_806157b4 * FLOAT_8061082c +
             (float)((double)CONCAT44(0x43300000,
              DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738));

    iDrawY = *pYCursor;
    FUN_803825c0((double)(float)((double)FLOAT_80615750 * dAlpha));

    // Icon 1: Primary ammo (type 0x1b)
    iSpriteId = FUN_8010e690(0x1b);
    if ((/* count > 0 */ ) && (iSpriteId != -1)) {
        int iTexId = sprite_get_texture_id(iSpriteId);
        iTexData = *DAT_80610a20 + iTexId * 0x58;
        uTexW = (u32)*(u16 *)(iTexData + 0x4e);
        uTexH = (u32)*(u16 *)(iTexData + 0x52);

        // Compute scaled height preserving aspect ratio
        u32 uScaledW = (u32)((float)((double)CONCAT44(0x43300000, uIconSize ^ 0x80000000) -
                       DOUBLE_80615738) * FLOAT_8061082c);
        iScaledH = (int)(FLOAT_80610830 *
                   (float)((double)CONCAT44(0x43300000,
                    (int)((float)((double)CONCAT44(0x43300000, uIconSize ^ 0x80000000) -
                          DOUBLE_80615738) *
                    ((float)((double)CONCAT44(0x43300000, uTexH ^ 0x80000000) - DOUBLE_80615738) /
                     (float)((double)CONCAT44(0x43300000, uTexW ^ 0x80000000) - DOUBLE_80615738)))
                    ^ 0x80000000) - DOUBLE_80615738));

        FUN_8016e77c(iTexId);
        FUN_80170410(iDrawX - iHalfSize, iDrawY, iDrawX + iHalfSize, iDrawY + iScaledH,
                     0, 0, uTexW << 4, uTexH << 4);
        iDrawY += iScaledH + (4 >> (DAT_80611748[0xd] & 0x3f));
    }

    // Icon 2: Secondary ammo (type 0x19) - same pattern
    // Icon 3: Grenades (type 0x1a) - same pattern
    // Icon 4: Special (type 0x1c) - same pattern
    // ... (identical draw logic for each)

    *pYCursor = iDrawY;
    FUN_8038271c();
}
