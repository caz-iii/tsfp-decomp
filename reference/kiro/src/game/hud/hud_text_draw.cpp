// Address: 0x80106d10 - 0x80107fe0 | Batch 327 | Date: 2026-08-25
// HUD text drawing routines: single-line fade, header centered, player name,
// typewriter reveal, image bar, score entry, damage source indicator,
// portrait label lifecycle, and multi-line bottom text.

#include "common.h"
#include "hud.h"
#include "text.h"

// ============================================================================
// Address: 0x80106d10 | hud_text_draw_single_fade
// Draws a single-line HUD text element with alpha fade based on timer progress.
// Uses integer coordinate drawing (text_draw_at_int_coords).
// ============================================================================
void hud_text_draw_single_fade(int pSlot) {
    float fAlpha;
    u32 uColor;
    double dAlpha;
    u32 xPos;
    u32 yPos;
    s64 yPosLL;

    dAlpha = (double)FLOAT_80615718; // 1.0f
    FUN_8016f00c(1);

    float fTimeCurrent = *(float *)(pSlot + 0x148);
    if (fTimeCurrent < *(float *)(pSlot + 0x144)) {
        dAlpha = (double)(FLOAT_80615718 -
                 (*(float *)(pSlot + 0x144) - fTimeCurrent) /
                 (*(float *)(pSlot + 0x150) - fTimeCurrent));
    }

    hud_select_font_for_width();

    xPos = DAT_8057ff4c + *DAT_80611748;
    yPos = (u32)(FLOAT_8061579c +
                -(FLOAT_806157a4 * FLOAT_80611f80 -
                 (float)((double)CONCAT44(0x43300000,
                          DAT_80611748[6] ^ 0x80000000) - DOUBLE_80615738)));
    yPosLL = (s64)(int)yPos;

    uColor = FUN_803825c0((double)(float)((double)FLOAT_80615750 * dAlpha));
    text_draw_at_int_coords(&xPos, &yPos, (u8 *)(pSlot + 4), uColor | 0x64646400);
}

// ============================================================================
// Address: 0x80106e0c | hud_text_draw_header_centered
// Draws a top-screen centered header message. Selects font based on player
// count and round timer state. Uses drop shadow rendering.
// ============================================================================
void hud_text_draw_header_centered(int pSlot) {
    int iVar1;
    u8 *pScaledText;
    u32 uColor;
    double dWidth;
    double dHeight;

    FUN_8016f00c(1);

    // Select larger font for 10+ players with active round timer
    if (((DAT_805015bd == '\n') && (1 < DAT_80612610)) &&
        (iVar1 = game_get_round_timer(), iVar1 != 0)) {
        text_set_font_table(0x12);
    } else {
        text_set_font_table(0x15);
    }

    DAT_80611654 = 1; // enable centered mode flag

    // Compute viewport-scaled width and check if text fits
    float fViewWidth = (float)((double)CONCAT44(0x43300000,
                       (DAT_80611748[1] - DAT_8057ff54) -
                       (DAT_8057ff4c + *DAT_80611748) ^ 0x80000000) - DOUBLE_80615738);
    float fViewLeft = (float)((double)CONCAT44(0x43300000,
                      DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738);

    pScaledText = text_check_scale(
        (double)(FLOAT_806157a8 * fViewWidth + fViewLeft),
        (u8 *)(pSlot + 4));

    // Center horizontally
    double dCenterX = (double)(float)((double)CONCAT44(0x43300000,
                      DAT_8057ff4c + *DAT_80611748 +
                      ((DAT_80611748[1] - DAT_8057ff54) -
                       (DAT_8057ff4c + *DAT_80611748) >> 1) ^ 0x80000000) -
                      DOUBLE_80615738);

    dHeight = text_get_char_height(pScaledText);
    u32 uTopY = hud_get_top_y();

    text_draw_char_drop_shadow(
        dCenterX,
        (double)(float)((double)(float)((double)CONCAT44(0x43300000, uTopY ^ 0x80000000) -
                 DOUBLE_80615738) - dHeight),
        pScaledText, 0x5f5f5fff, 0xf0f0f7f);

    DAT_80611654 = 0; // disable centered mode
}

// ============================================================================
// Address: 0x80106fb0 | hud_text_draw_player_name
// Draws a player name at the top of screen with fade, considering game state
// (game over / sudden death). Applies alpha fade for multiplayer transitions.
// ============================================================================
void hud_text_draw_player_name(void) {
    float fTimeCurrent;
    bool bIsEndState;
    int iSlot;
    int iPlayerIndex;
    u32 uColor;
    u32 uTopY;
    double dAlpha;
    double dTextWidth;

    iSlot = FUN_803826e4();
    iPlayerIndex = *(int *)(DAT_80611d74 + 4);
    dAlpha = (double)FLOAT_80615718; // 1.0f
    bIsEndState = false;

    int iGameState = FUN_801c1714();
    if ((iGameState == 4) ||
        ((iGameState = FUN_801c1714(), iGameState == 3 && (DAT_80611d74 != 0)) &&
         (0 < *(int *)(DAT_80611d74 + 4)))) {
        bIsEndState = true;
    }

    FUN_8016f00c(1);
    fTimeCurrent = *(float *)(iSlot + 0x148);
    if (fTimeCurrent < *(float *)(iSlot + 0x144)) {
        dAlpha = (double)(FLOAT_80615718 -
                 (*(float *)(iSlot + 0x144) - fTimeCurrent) /
                 (*(float *)(iSlot + 0x150) - fTimeCurrent));
    }

    // Multiplayer round transition fade
    if ((*(u32 *)(&DAT_80578688 + iPlayerIndex * 0x177c) & 3) != 0) {
        // Apply additional fade from round transition timer
        double dFade = (double)FLOAT_80615718;
        // ... (clamped fade computation)
        dAlpha = (double)(float)((float)dAlpha * (float)dFade);
    }

    uColor = FUN_803825c0((double)(float)((double)FLOAT_80615750 * dAlpha));
    hud_select_font_for_width();
    uTopY = hud_get_top_y();

    if (bIsEndState) {
        // Apply viewport-width alpha fade for end-state animation
        text_apply_alpha_fade(
            (double)(FLOAT_80615778 * FLOAT_806157ac *
                    (float)((double)CONCAT44(0x43300000,
                     (DAT_80611748[1] - DAT_8057ff54) -
                     (DAT_8057ff4c + *DAT_80611748) ^ 0x80000000) - DOUBLE_80615738)),
            (u8 *)(iSlot + 4), 0, 1);
    }

    dTextWidth = text_get_char_width((u8 *)(iSlot + 4));

    // Draw centered with shadow
    double dCenterX = (double)(float)((double)CONCAT44(0x43300000,
                      DAT_8057ff4c + *DAT_80611748 +
                      ((DAT_80611748[1] - DAT_8057ff54) -
                       (DAT_8057ff4c + *DAT_80611748) >> 1) ^ 0x80000000) -
                      DOUBLE_80615738);

    double dDrawX = -(double)(float)((double)FLOAT_80615734 * dTextWidth -
                    (double)(float)dCenterX);

    float fDrawY = (float)FLOAT_80611674 * /* vertical offset */ + (float)uTopY;

    text_draw_char_shadowed(dDrawX, (double)fDrawY,
                            (u8 *)(iSlot + 4), uColor | 0x64646400, uColor | 0x1e1e1e00);

    if (bIsEndState) {
        double d1 = (double)FLOAT_80615718;
        text_set_color_rgb(d1, d1, d1); // reset color to white
    }
    FUN_80382730();
}

// ============================================================================
// Address: 0x8010728c | hud_text_draw_typewriter
// Draws text with a typewriter reveal effect - characters are progressively
// shown based on elapsed time. Copies string to local buffer and truncates.
// ============================================================================
void hud_text_draw_typewriter(void) {
    int iSlot;
    int iPlayerIndex;
    u32 uRevealCount;
    u32 uTotalChars;
    char *pEnd;
    u8 *pScaledText;
    u32 uColor;
    double dAlpha;
    int aLocalBuf[128]; // local string buffer

    iSlot = FUN_803826ec();
    dAlpha = (double)FLOAT_80615718;
    iPlayerIndex = *(int *)(DAT_80611d74 + 4);

    FUN_8016f00c(1);
    FUN_8038b510(aLocalBuf, (int *)(iSlot + 4)); // strcpy to local

    // Determine reveal character count
    if (*(int *)(&DAT_80579d90 + iPlayerIndex * 0x177c) == 2) {
        uRevealCount = text_strlen((char *)aLocalBuf);
    } else {
        // Rate-based reveal: characters = rate * elapsed_time
        uRevealCount = (u32)(FLOAT_80615784 * *(float *)(iSlot + 0x144));
    }

    // Clamp to actual string length
    uTotalChars = FUN_8038b5c8((int)aLocalBuf);
    if (uTotalChars < uRevealCount) {
        uRevealCount = FUN_8038b5c8((int)aLocalBuf);
    }

    // Truncate string at reveal point
    pEnd = text_advance_chars((char *)aLocalBuf, uRevealCount);
    *pEnd = '\0';

    // Compute fade alpha
    float fTimeCurrent = *(float *)(iSlot + 0x148);
    if (fTimeCurrent < *(float *)(iSlot + 0x144)) {
        dAlpha = (double)(FLOAT_80615718 -
                 (*(float *)(iSlot + 0x144) - fTimeCurrent) /
                 (*(float *)(iSlot + 0x150) - fTimeCurrent));
    }

    uColor = FUN_803825c0((double)(float)((double)FLOAT_80615750 * dAlpha));
    hud_select_font_for_width();

    // Position: viewport left + 0x14 padding
    u32 uXBase = DAT_8057ff4c + *DAT_80611748 + 0x14;

    int iLayout = FUN_801c1714();
    u32 uYOffset = 0x28;
    if (iLayout == 1) {
        uYOffset = 0x50;
    }

    // Compute draw position with scale
    u32 uWidth = (DAT_80611748[1] - DAT_8057ff54) - (DAT_8057ff4c + *DAT_80611748) - 0x28;
    pScaledText = text_check_scale(
        (double)(float)((double)CONCAT44(0x43300000, uWidth ^ 0x80000000) - DOUBLE_80615738),
        (u8 *)aLocalBuf);

    // Draw with shadow
    text_draw_char_shadowed(/* x */, /* y */, pScaledText,
                            uColor | 0x64646400, uColor | 0x1e1e1e00);
    FUN_80382738();
}

// ============================================================================
// Address: 0x801074e0 | hud_text_draw_image_bar
// Draws a HUD image/texture bar element with quadratic alpha fade easing.
// Likely used for progress bars or status indicators.
// ============================================================================
void hud_text_draw_image_bar(int pSlot) {
    u32 uAlpha;
    u32 uBaseY;
    double dAlpha;

    dAlpha = (double)FLOAT_80615718; // 1.0f
    double dTimeCurrent = (double)*(float *)(pSlot + 0x148);

    if (dTimeCurrent < (double)*(float *)(pSlot + 0x144)) {
        // Quadratic ease-out: alpha = 1 - (t/T)^2
        double dRatio = (double)((float)((double)*(float *)(pSlot + 0x144) - dTimeCurrent) /
                        (float)((double)*(float *)(pSlot + 0x150) - dTimeCurrent));
        dAlpha = -(double)(float)(dRatio * dRatio - FLOAT_80615718);
    }

    uAlpha = FUN_803825c0((double)(float)((double)FLOAT_80615750 * dAlpha));
    uBaseY = hud_select_base_y();

    if (DAT_806109c4 == 0x200) {
        // Fixed-width mode (512px)
        FUN_802f3a8c(/* params */
                     *(u32 *)(pSlot + 0x164), 0, 0x55, uBaseY, 0x1ad, uBaseY, uAlpha, 1);
    } else {
        // Dynamic-width mode
        FUN_802f3a8c(/* params */
                     *(u32 *)(pSlot + 0x164), 0, 0x55, uBaseY,
                     DAT_806109c4 - 0x55, uBaseY, uAlpha, 1);
    }
}

// ============================================================================
// Address: 0x801075a0 | hud_text_draw_score_entry
// Draws a player score/kill notification entry. Handles two-line split when
// text is too wide for viewport, applies team color to score portion.
// ============================================================================
void hud_text_draw_score_entry(void) {
    // Complex function: formats player name + score text
    // If combined width exceeds viewport, splits into two lines
    // Applies team-specific color from color table at DAT_803f2c7c
    // Uses quadratic fade easing for alpha

    int *pSlot;
    int iScoreData;
    bool bIsEndState;
    bool bSplitLines;
    u32 uColorMain, uColorShadow;
    u8 aNameBuf[256];
    u8 aScoreBuf[256];

    pSlot = (int *)FUN_803826d4();
    int iContextPtr = FUN_80229ee8();
    double dAlpha = (double)FLOAT_80615718;
    bIsEndState = false;

    int iGameState = FUN_801c1714();
    if ((iGameState == 4) ||
        ((iGameState = FUN_801c1714(), iGameState == 3 && (DAT_80611d74 != 0)) &&
         (0 < *(int *)(DAT_80611d74 + 4)))) {
        bIsEndState = true;
    }

    bSplitLines = false;
    iScoreData = DAT_80611d90 + pSlot[0x5a] * 0xea0;

    // Handle type 0xc (special context switch for split-screen)
    int iSavedContext = 0;
    int *pSavedViewport = NULL;
    if (*pSlot == 0xc) {
        if (iContextPtr == 0) return;
        iSavedContext = DAT_80611d54;
        pSavedViewport = DAT_80611748;
        DAT_80611748 = (int *)(iContextPtr + 0x224);
        DAT_80611d54 = iContextPtr;
    }

    FUN_8016f00c(1);

    // Quadratic fade
    float fTime = (float)pSlot[0x52];
    if (fTime < (float)pSlot[0x51]) {
        fTime = ((float)pSlot[0x51] - fTime) / ((float)pSlot[0x54] - fTime);
        dAlpha = -(double)(fTime * fTime - FLOAT_80615718);
    }

    uColorMain = FUN_803825c0((double)(float)((double)FLOAT_80615750 * dAlpha));
    uColorMain = uColorMain & 0xff | 0x7f7f7f00;
    uColorShadow = uColorMain & 0xff | 0x1e1e1e00;

    text_set_font_table(DAT_80610838);
    FUN_8038b510((int *)aNameBuf, pSlot + 1); // copy player name

    // Measure combined width
    double dScoreWidth = text_get_char_width(*(u8 **)(iScoreData + 0x70));
    double dNameWidth = text_get_char_width(aNameBuf);

    float fViewWidth = FLOAT_80615778 * FLOAT_806157ac *
                       (float)((double)CONCAT44(0x43300000,
                        (DAT_80611748[1] - DAT_8057ff54) -
                        (DAT_8057ff4c + *DAT_80611748) ^ 0x80000000) - DOUBLE_80615738);

    bSplitLines = fViewWidth < (float)(dNameWidth + dScoreWidth);

    if (bSplitLines) {
        bSplitLines = true;
        FUN_8038b510((int *)aScoreBuf, *(int **)(iScoreData + 0x70));
    } else if ((DAT_805015c0 & 1) == 0) {
        // Append separator + score to name buffer
        FUN_8038b4a0((int)aNameBuf, -0x7f9ef7ac); // separator string
        FUN_8038b4a0((int)aNameBuf, *(int *)(iScoreData + 0x70));
    } else {
        bSplitLines = true;
        FUN_8038b4a0((int)aNameBuf, -0x7f9ef7ac);
        FUN_8038b510((int *)aScoreBuf, *(int **)(iScoreData + 0x70));
    }

    u32 uRowY = hud_get_offset_row_y(2 - !bSplitLines);
    double dDrawY = (double)(float)((double)CONCAT44(0x43300000, uRowY ^ 0x80000000) -
                    DOUBLE_80615738);

    // Draw name line (and optionally score on second line with team color)
    if (bSplitLines) {
        // Two-line drawing with team color on score line
        u32 uTeamColor = FUN_80132348(dAlpha,
            (&DAT_803f2c7c)[*(int *)(iScoreData + 0x84) * 5]);
        // ... draw both lines with appropriate colors
    } else {
        // Single line: name + score combined
        // ... draw single line
    }

    // Restore context if split-screen
    if (*pSlot == 0xc) {
        DAT_80611748 = pSavedViewport;
        DAT_80611d54 = iSavedContext;
    }
    FUN_80382720();
}

// ============================================================================
// Address: 0x80107c24 | hud_text_draw_damage_source
// Draws the damage source indicator showing who hit you. Uses damage alpha
// from screen effects and team color from the attacker's team data.
// ============================================================================
void hud_text_draw_damage_source(void) {
    u32 *pYCursor;
    int iPrevFont;
    u32 uAlpha;
    u32 uTeamColor;
    double dDamageAlpha;
    double dColumnWidth;
    u8 aTextBuf[256];

    pYCursor = (u32 *)FUN_803826ec();
    iPrevFont = text_set_font_table(DAT_80610838);

    dColumnWidth = (double)(FLOAT_806157b4 * FLOAT_8061082c);
    dDamageAlpha = screen_effect_get_damage_alpha();

    int iAttackerData = *(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c);
    uTeamColor = (&DAT_803f2c7c)[*(int *)(iAttackerData + 0x84) * 5];

    FUN_8016f00c(1);

    // Format attacker name text
    FUN_80387fb8(dDamageAlpha, /* params */, (int)aTextBuf,
                 &DAT_80610858, *(u32 *)(iAttackerData + 0x70));

    double dTextWidth = text_get_char_width(aTextBuf);
    text_set_font_table(0x13);

    // Scale text to fit column
    double dScale = (double)(float)((double)(float)((double)FLOAT_806157b8 * dColumnWidth) /
                    dTextWidth);

    float fDrawX;
    if ((double)FLOAT_80615720 <= dScale) {
        text_apply_alpha((double)(float)(dScale * dTextWidth), aTextBuf, 0);
        double dFinalWidth = text_get_char_width(aTextBuf);
        fDrawX = FLOAT_80615734 * (float)(dColumnWidth - dFinalWidth) +
                 (float)((double)CONCAT44(0x43300000,
                  DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738);
    } else {
        text_apply_alpha((double)(float)((double)FLOAT_80615720 * dTextWidth), aTextBuf, 0);
        fDrawX = (float)((double)CONCAT44(0x43300000,
                  DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738);
    }

    // Draw with team color + damage-based alpha
    uAlpha = FUN_803825c0((double)(float)((double)FLOAT_806157bc * dDamageAlpha));
    text_draw_char((double)fDrawX,
                   (double)(float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) -
                   DOUBLE_80615738),
                   aTextBuf, uTeamColor & 0xffffff00 | uAlpha);

    // Advance Y cursor by text height
    double dHeight = text_get_char_height(aTextBuf);
    *pYCursor = (u32)((double)(float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) -
                DOUBLE_80615738) + dHeight);

    double d1 = (double)FLOAT_80615718;
    text_set_color_rgb(d1, d1, d1); // reset to white
    text_set_font_table(iPrevFont);
    FUN_80382738();
}

// ============================================================================
// Address: 0x80107e44 | hud_portrait_label_tick
// Per-frame tick for the HUD portrait/label system. Manages a 3-state fade
// machine per player: state 0 = fade-in, state 1 = hold, state 2 = fade-out.
// ============================================================================
void hud_portrait_label_tick(int iPlayerIndex) {
    float fDt;
    int iOffset;
    int *pSlot;

    if (iPlayerIndex < 0) return;

    iOffset = iPlayerIndex * 0x177c;
    pSlot = &DAT_80579de8 + iPlayerIndex * 0x5df;

    if (*pSlot != 0) {
        // Advance fade timer
        *(float *)(&DAT_80579dec + iOffset) +=
            FLOAT_806108e8 * (FLOAT_80615718 /
            (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615738));

        *(float *)(&DAT_80579df8 + iOffset) = FLOAT_80615718; // default alpha = 1.0

        if (*(int *)(&DAT_80579df4 + iOffset) == 0) {
            // State 0: fade-in
            *(float *)(&DAT_80579df8 + iOffset) =
                *(float *)(&DAT_80579dec + iOffset) / *(float *)(&DAT_80579df0 + iOffset);

            if (*(float *)(&DAT_80579df0 + iOffset) < *(float *)(&DAT_80579dec + iOffset)) {
                // Transition to hold state
                *(int *)(&DAT_80579df4 + iOffset) = 1;
                *(float *)(&DAT_80579dec + iOffset) = 0.0f;
                *(float *)(&DAT_80579df8 + iOffset) = FLOAT_80615718;
            }
        } else if ((*(int *)(&DAT_80579df4 + iOffset) == 2) &&
                   (*(float *)(&DAT_80579df8 + iOffset) =
                        FLOAT_80615718 - *(float *)(&DAT_80579dec + iOffset) /
                        *(float *)(&DAT_80579df0 + iOffset),
                    *(float *)(&DAT_80579df0 + iOffset) < *(float *)(&DAT_80579dec + iOffset))) {
            // Fade-out complete: clear slot
            memset_wrapper((int)pSlot, 0, 0x14);
        }
    }

    // If slot is empty, try to assign a new portrait
    if (*pSlot == 0) {
        int *pPortraitData = FUN_8022da98();
        if ((pPortraitData != NULL) && ((DAT_80611934 & 8) == 0)) {
            *pSlot = DAT_806128ac + *pPortraitData * 0x4c;
            *(float *)(&DAT_80579dec + iOffset) = 0.0f;
            *(float *)(&DAT_80579df0 + iOffset) = FLOAT_80615748; // fade duration
            *(int *)(&DAT_80579df4 + iOffset) = 0; // state = fade-in
        }
    }
}

// ============================================================================
// Address: 0x80107fa0 | hud_portrait_label_fadeout
// Triggers fade-out on a portrait label slot if the entity ID matches.
// Sets state to 2 (fade-out) and resets timer.
// ============================================================================
void hud_portrait_label_fadeout(int iPlayerIndex, int iEntityId) {
    if (iPlayerIndex < 0) return;

    int iOffset = iPlayerIndex * 0x177c;

    // Only trigger if current slot matches the entity
    if ((&DAT_80579de8)[iPlayerIndex * 0x5df] != iEntityId) return;

    *(int *)(&DAT_80579df4 + iOffset) = 2;       // state = fade-out
    *(float *)(&DAT_80579dec + iOffset) = 0.0f;   // reset timer
    *(float *)(&DAT_80579df0 + iOffset) = FLOAT_80615748; // fade duration
}

// ============================================================================
// Address: 0x80107fe0 | hud_text_draw_multiline_bottom
// Draws multi-line text at the bottom of screen with slide-in animation using
// a sine table lookup. Supports special prefix characters:
//   '~' = highlight color (green tint)
//   '`' = extra vertical spacing
// ============================================================================
void hud_text_draw_multiline_bottom(void) {
    int iSlot;
    int iGameState;
    char *pScaledText;
    u32 uLineCount;
    char *pNewline;
    u32 uYPos;
    u32 uSlideOffset;
    u32 uCharIndex;
    u8 *pLineStart;
    u32 uColorMain, uColorShadow;

    iSlot = FUN_803826d0();
    iGameState = FUN_801c1714();
    int iPrevFont = text_set_font_table(*(int *)(&DAT_8040cf48 + iGameState * 4));

    // Scale text to viewport width
    float fViewWidth = (float)((double)CONCAT44(0x43300000,
                       (DAT_80611748[1] - DAT_8057ff54) -
                       (DAT_8057ff4c + *DAT_80611748) ^ 0x80000000) - DOUBLE_80615738);
    pScaledText = text_check_scale((double)fViewWidth, (u8 *)(iSlot + 4));

    FUN_8016f00c(1);

    // Count newlines to determine line count
    uLineCount = 1;
    for (char *p = pScaledText; *p != '\0'; p++) {
        if (*p == '\n') uLineCount++;
    }

    uYPos = hud_get_bottom_row_y(uLineCount);

    // Compute slide-in offset from sine table based on timer
    float fFadeTime = *(float *)(iSlot + 0x144);
    float fHoldTime = *(float *)(iSlot + 0x14c);

    if (fHoldTime <= fFadeTime) {
        float fProgress = fFadeTime - (fHoldTime + *(float *)(iSlot + 0x148));
        if (fProgress < 0.0f) {
            uSlideOffset = 0;
        } else {
            // Sine table slide-out
            int iTableIndex = (int)(FLOAT_806157c0 * FLOAT_806157c4 *
                              FLOAT_80615734 * (fProgress / fHoldTime) + FLOAT_80615734);
            uSlideOffset = (u32)-((FLOAT_80615718 -
                           *(float *)(&DAT_80583228 + ((iTableIndex + 0x200) * 4 & 0x1ffcU))) *
                           fViewWidth);
        }
    } else {
        // Sine table slide-in
        int iTableIndex = (int)(FLOAT_806157c0 * FLOAT_806157c4 *
                          FLOAT_80615734 * (fFadeTime / fHoldTime) + FLOAT_80615734) & 0x7ff;
        uSlideOffset = (u32)((FLOAT_80615718 - *(float *)(&DAT_80583228 + iTableIndex * 4)) *
                       fViewWidth);
    }

    // Draw each line
    uCharIndex = 0;
    do {
        pNewline = FUN_8038b308((int)pScaledText, '\n');

        // Select colors based on layout
        int iSpecial = hud_is_special_layout();
        if (iSpecial == 0) {
            uColorMain = 0x7f7f7f7f;
            uColorShadow = 0x160a3240;
        } else {
            uColorMain = 0x40;
            uColorShadow = 0x7f7f7f7f;
        }

        if (pNewline != NULL) *pNewline = '\0';

        // Handle prefix characters
        if (pScaledText[uCharIndex] == '~') {
            // Highlight: green-tinted color
            uColorShadow = 0x5080;
            uColorMain = 0x40787880;
            uCharIndex++;
        } else if (pScaledText[uCharIndex] == '`') {
            // Extra spacing
            uCharIndex++;
            uYPos = (u32)(FLOAT_80611674 * FLOAT_80615734 +
                    (float)((double)CONCAT44(0x43300000, uYPos ^ 0x80000000) - DOUBLE_80615738));
        }

        pLineStart = (u8 *)(pScaledText + uCharIndex);
        if (*pLineStart != 0) {
            double dLineWidth = text_get_char_width(pLineStart);

            // Center text horizontally with slide offset
            float fCenterX = (float)((double)CONCAT44(0x43300000,
                             DAT_8057ff4c + *DAT_80611748 +
                             ((DAT_80611748[1] - DAT_8057ff54) -
                              (DAT_8057ff4c + *DAT_80611748) >> 1) ^ 0x80000000) -
                             DOUBLE_80615738);

            float fDrawX = (float)uSlideOffset +
                           -(float)(dLineWidth * (double)FLOAT_80615734 - (double)fCenterX);

            text_draw_char_shadowed(
                (double)fDrawX,
                (double)(float)((double)CONCAT44(0x43300000, uYPos ^ 0x80000000) - DOUBLE_80615738),
                pLineStart, uColorMain, uColorShadow);
        }

        // Advance to next line
        int iLineLen = FUN_8038b5c8((int)pLineStart);
        uCharIndex = iLineLen + uCharIndex + 1;
        uYPos = (u32)((float)((double)CONCAT44(0x43300000, uYPos ^ 0x80000000) - DOUBLE_80615738) +
                FLOAT_80611674);

        if (pNewline != NULL) *pNewline = '\n';

        u32 uTotalLen = FUN_8038b5c8((int)pScaledText);
    } while (uCharIndex < uTotalLen);

    text_set_font_table(iPrevFont);
    FUN_8038271c();
}
