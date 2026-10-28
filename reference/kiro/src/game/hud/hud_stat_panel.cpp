// Address: 0x8010993c - 0x8010a2ec | Batch 329 | Date: 2026-08-25
// HUD scoreboard stat panel renderer and individual stat line draw functions
// (score, time, kills, rank).

#include "common.h"
#include "hud.h"
#include "text.h"
#include "sprite.h"

// ============================================================================
// Address: 0x8010993c | hud_draw_stat_panel
// MASSIVE bordered stat panel renderer. Draws a textured frame (sprite 0xdb),
// renders a title label (top portion) and value text (bottom portion) with
// shadow/outline depending on layout mode. Handles text scaling to fit panel
// width, fade alpha, and vertical overflow clamping.
// Parameters:
//   param_2 = Y position, param_3 = alpha/fade, param_6 = use bold font flag,
//   param_7 = value text color
// ============================================================================
void hud_draw_stat_panel(/* complex params */) {
    u8 *pTitle;
    u8 *pValue;
    int iPrevFont;
    int iSpriteId;
    int iTexData;
    u32 uTexW, uTexH;
    double dPanelWidth, dScaleX, dScaleY;
    double dAlpha;
    u32 uFadeColor;

    // Get slot data (title text = upper, value text = lower)
    u64 uSlotData = FUN_803826d4();
    pTitle = (u8 *)(uSlotData >> 32);
    pValue = (u8 *)uSlotData;

    dScaleY = (double)FLOAT_80610830;
    dScaleX = (double)FLOAT_8061082c;
    iPrevFont = text_set_font_table(DAT_80610838);

    // Get panel texture dimensions
    iSpriteId = sprite_get_texture_id(0xdb);
    iTexData = *DAT_80610a20 + iSpriteId * 0x58;
    uTexW = (u32)*(u16 *)(iTexData + 0x4e);
    uTexH = (u32)*(u16 *)(iTexData + 0x52);

    dPanelWidth = (double)(float)((double)FLOAT_806157b4 * dScaleX);

    // Compute panel geometry (left, right, top, bottom edges)
    double dLeft = /* computed from params */;
    double dRight = /* computed from params */;
    double dInnerLeft = /* texture-adjusted */;
    double dInnerRight = /* texture-adjusted */;
    double dTop = /* from param_2 */;
    double dBottom = /* from param_2 + offset */;

    // Compute fade alpha
    uFadeColor = FUN_803825c0((double)(float)((double)FLOAT_806157bc * /* alpha */));
    hud_compute_fade_color(uFadeColor);

    // Clamp to viewport top if overflowing
    float fTopEdge = /* computed */;
    u32 uViewTop = DAT_8057ff50 + *(int *)(DAT_80611748 + 0x14);
    if (fTopEdge < (float)uViewTop) {
        // Shift panel down to fit
        double dShift = (double)((float)uViewTop - fTopEdge);
        if (dShift > 0.0) {
            // Adjust all Y positions
        }
    }

    // Draw border frame
    hud_render_screen_border(dLeft, dTop, dRight, dBottom, /* alpha */);

    // Draw texture corners/edges (4 corner tiles + 2 side fills)
    FUN_8016e77c(iSpriteId);
    FUN_80170b10(/* 6 texture tile draws */);

    // Draw title text (top portion)
    if (pTitle != NULL) {
        double dTitleWidth = text_get_char_width(pTitle);
        double dScale = (double)(float)((double)FLOAT_806157b8 *
                        (float)(dInnerRight - dInnerLeft) / dTitleWidth);

        if ((double)FLOAT_80615720 <= dScale) {
            text_apply_alpha((double)(float)(dScale * dTitleWidth), pTitle, 0);
        } else {
            text_apply_alpha((double)(float)((double)FLOAT_80615720 * dTitleWidth), pTitle, 0);
        }

        double dTitleX = /* centered in panel */;
        double dTitleY = /* top of panel + offset */;

        int iLayout = hud_is_special_layout();
        if (iLayout == 0) {
            u32 uShadowAlpha = FUN_803825c0((double)(float)((double)FLOAT_806157b4 * /* alpha */));
            u32 uTextAlpha = FUN_803825c0((double)(float)((double)FLOAT_806157f0 * /* alpha */));
            text_draw_char_shadowed(dTitleX, dTitleY, pTitle,
                                    uTextAlpha & 0xff | 0x7f7f7f00, uShadowAlpha & 0xff);
        } else {
            u32 uOutlineAlpha = FUN_803825c0((double)(float)((double)FLOAT_806157f0 * /* alpha */));
            text_draw_char_outline(dTitleX, dTitleY, pTitle,
                                   uOutlineAlpha & 0xff | 0x7f7f7f00, uOutlineAlpha);
        }

        text_set_color_rgb(1.0, 1.0, 1.0); // reset
    }

    // Draw value text (bottom portion)
    if (pValue != NULL) {
        if (/* bold font flag */ != 0) {
            leaf_92c0(1); // enable bold
        }

        double dValueWidth = text_get_char_width(pValue);
        double dValueX = /* centered */;
        double dValueY = /* bottom half */;

        int iLayout = hud_is_special_layout();
        if (iLayout == 0) {
            u32 uShadow = FUN_803825c0((double)(float)((double)FLOAT_806157b4 * /* alpha */));
            u32 uText = FUN_803825c0(/* color alpha */);
            text_draw_char_shadowed(dValueX, dValueY, pValue,
                                    /* color */ | uText, uShadow & 0xff);
        } else {
            u32 uOutline = FUN_803825c0((double)(float)((double)FLOAT_806157f0 * /* alpha */));
            u32 uText = FUN_803825c0(/* color alpha */);
            text_draw_char_outline(dValueX, dValueY, pValue,
                                   /* color */ | uText, uOutline);
        }

        if (/* bold font flag */ != 0) {
            leaf_92c0(0); // disable bold
        }
    }

    text_set_font_table(iPrevFont);
    FUN_80382720();
}

// ============================================================================
// Address: 0x80109f7c | hud_draw_score_stat
// Draws the "Score" stat panel in the scoreboard. Formats the player's current
// score and renders via hud_draw_stat_panel. Handles negative score display
// with red coloring (0x7f000060).
// String ID 0x15f3 = "Score"
// ============================================================================
void hud_draw_score_stat(/* params including X offset */) {
    u32 *pYCursor;
    int iScore;
    u32 uColor;
    u8 aTextBuf[256];

    u64 uSlot = FUN_803826f0();
    pYCursor = (u32 *)uSlot;

    // Get player score from round data
    iScore = *(int *)((int)(uSlot >> 32) + 0xa6c);
    uColor = 0x7f7f7f60; // default gray
    if (iScore < 0) {
        uColor = 0x7f000060; // negative: red
    }

    // Format score text (special handling for game mode 0x64)
    if ((DAT_805015bd == '\n') && (DAT_80612614 == 100)) {
        u32 uFormatVal = FUN_80349614(); // special format
        FUN_80387fb8(/* format */, (int)aTextBuf, &DAT_80610864, uFormatVal, iScore);
    } else {
        FUN_80387fb8(/* format */, (int)aTextBuf, &DAT_80610864, iScore, iScore);
    }

    int iLabel = get_string_by_id(0x15f3); // "Score"

    // Draw panel at computed position
    float fX = (float)((double)CONCAT44(0x43300000,
               DAT_8057ff4c + *DAT_80611748 + /* xOffset */ ^ 0x80000000) - DOUBLE_80615738);
    float fY = (float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) - DOUBLE_80615738);

    int iHeight = hud_draw_stat_panel(fX, fY, /* alpha */, iLabel, aTextBuf, 1, uColor);
    *pYCursor += iHeight;

    FUN_8038273c();
}

// ============================================================================
// Address: 0x8010a0ac | hud_draw_time_stat
// Draws the "Time" stat panel. Formats elapsed/remaining time using
// time_format_short. Handles different game modes:
//   Mode 0 = player score as time, Mode 2/3 = round timer countdown.
// String ID 0x15a0 = timer label, 0x15f2 = "Time"
// ============================================================================
void hud_draw_time_stat(/* params including X offset */) {
    u32 *pYCursor;
    int iTimeValue;
    int iLabel;
    u8 aTextBuf[256];

    u64 uSlot = FUN_803826ec();
    pYCursor = (u32 *)uSlot;

    iTimeValue = 0;
    iLabel = 0;

    if (DAT_805015bd == 0) {
        // Mode 0: score as time
        iTimeValue = score_get_player(*(int *)((int)(uSlot >> 32) + 8));
        iLabel = get_string_by_id(0x15f2); // "Time"
    } else if (DAT_805015bd == 2 || DAT_805015bd == 3) {
        // Mode 2/3: countdown timer
        if (DAT_80611934 == 8) {
            iTimeValue = DAT_806118e4 + DAT_806109c0 * -3;
        } else {
            iTimeValue = DAT_80611928 + DAT_806109c0 * -3;
        }
        iLabel = get_string_by_id(0x15a0);
    }

    if (iTimeValue < 0) iTimeValue = 0;

    time_format_short(/* format */, (int)aTextBuf, iTimeValue);

    float fX = (float)((double)CONCAT44(0x43300000,
               DAT_8057ff4c + *DAT_80611748 + /* xOffset */ ^ 0x80000000) - DOUBLE_80615738);
    float fY = (float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) - DOUBLE_80615738);

    int iHeight = hud_draw_stat_panel(fX, fY, /* alpha */, iLabel, aTextBuf, 1, 0x7f7f7f60);
    *pYCursor += iHeight;

    FUN_80382738();
}

// ============================================================================
// Address: 0x8010a218 | hud_draw_kills_stat
// Draws the "Kills" stat panel. Reads kill count from player data offset 0xa60
// and formats as integer.
// String ID 0x159f = "Kills"
// ============================================================================
void hud_draw_kills_stat(/* params */) {
    int iLabel;
    u8 aTextBuf[256];

    // Format kills count
    int iKills = (int)*(short *)(/* player_data */ + 0xa60);
    FUN_80387fb8(/* format */, (int)aTextBuf, &DAT_80610864, iKills);

    iLabel = get_string_by_id(0x159f); // "Kills"

    float fX = (float)((double)CONCAT44(0x43300000,
               DAT_8057ff4c + *DAT_80611748 + /* xOffset */ ^ 0x80000000) - DOUBLE_80615738);
    float fY = (float)((double)CONCAT44(0x43300000, */* yCursor */ ^ 0x80000000) - DOUBLE_80615738);

    int iHeight = hud_draw_stat_panel(fX, fY, /* alpha */, iLabel, aTextBuf, 1, 0x7f7f7f60);
    */* yCursor */ += iHeight;
}

// ============================================================================
// Address: 0x8010a2ec | hud_draw_rank_stat
// Draws the "Rank" stat panel. Formats rank ordinal (1st, 2nd, 3rd...) using
// hud_format_rank_string from player data offset 0xa68.
// String ID 0x15f8 = "Rank"
// ============================================================================
void hud_draw_rank_stat(/* params */) {
    int iLabel;
    u8 aTextBuf[8]; // rank string is short

    // Format rank as ordinal string
    int iRank = *(int *)(/* player_data */ + 0xa68);
    hud_format_rank_string(/* format */, aTextBuf, iRank, 0, 1);

    iLabel = get_string_by_id(0x15f8); // "Rank"

    float fX = (float)((double)CONCAT44(0x43300000,
               DAT_8057ff4c + *DAT_80611748 + /* xOffset */ ^ 0x80000000) - DOUBLE_80615738);
    float fY = (float)((double)CONCAT44(0x43300000, */* yCursor */ ^ 0x80000000) - DOUBLE_80615738);

    int iHeight = hud_draw_stat_panel(fX, fY, /* alpha */, iLabel, aTextBuf, 0, 0x7f7f7f60);
    */* yCursor */ += iHeight;
}

// ============================================================================
// Address: 0x8010a3c0 | hud_draw_award_stat
// Draws an award/accolade stat panel. Gets award string pair from FUN_8022b714,
// only draws if a valid award exists.
// ============================================================================
void hud_draw_award_stat(double dAlpha, u32 *pYCursor) {
    int iLabel;
    u32 uValue;

    FUN_8022b714(&iLabel, &uValue, 1);
    if (iLabel != 0) {
        float fX = (float)((double)CONCAT44(0x43300000,
                   DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738);
        float fY = (float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) - DOUBLE_80615738);

        int iHeight = hud_draw_stat_panel((double)fX, (double)fY, dAlpha,
                                          iLabel, uValue, 0, 0x7f7f7f60);
        *pYCursor += iHeight;
    }
}

// ============================================================================
// Address: 0x8010a484 | hud_draw_game_time_stat
// Draws a time stat panel formatted as MM:SS.T (minutes:seconds.tenths).
// param_10 selects between best time (0) and current time (1).
// String 0x15f4 = "Best Time", 0x15f6 = "Time"
// ============================================================================
void hud_draw_game_time_stat(double dAlpha, u32 *pYCursor, int iUseCurrent) {
    u32 uTicks;
    int iMinutes, iSeconds, iTenths;
    u32 uFormatted;
    int iLabel;

    if (iUseCurrent == 0) {
        uTicks = FUN_8022b45c(); // best time
    } else {
        uTicks = FUN_8022b4f4(); // current time
    }

    // Convert ticks to time components
    float fTicks = (float)((double)CONCAT44(0x43300000, uTicks ^ 0x80000000) - DOUBLE_80615738);
    int iTotalFrames = (int)((double)FLOAT_806157d4 *
                      (double)(fTicks / (float)((double)CONCAT44(0x43300000,
                       DAT_806109c0 ^ 0x80000000) - DOUBLE_80615738)));
    int iTotalSeconds = (int)uTicks / (int)DAT_806109c0;

    // Format as "%02d:%02d.%01d"
    uFormatted = FUN_8012e878(/* format string "s__02d__02d__01d_8040d088" */,
                              (iTotalSeconds / 60) % 60,  // minutes
                              iTotalSeconds % 60,          // seconds
                              iTotalFrames % 10);          // tenths

    // Select label
    iLabel = 0x15f4; // "Best Time"
    if (iUseCurrent != 0) {
        iLabel = 0x15f6; // "Time"
    }
    iLabel = get_string_by_id(iLabel);

    float fX = (float)((double)CONCAT44(0x43300000,
               DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738);
    float fY = (float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) - DOUBLE_80615738);

    int iHeight = hud_draw_stat_panel((double)fX, (double)fY, dAlpha,
                                      iLabel, uFormatted, 1, 0x7f7f7f60);
    *pYCursor += iHeight;
}

// ============================================================================
// Address: 0x8010a5fc | hud_draw_challenge_stat
// Draws a challenge/objective stat panel with an associated minimap icon.
// String 0x657. Only draws if FUN_8022b770 returns non-zero (challenge active).
// ============================================================================
void hud_draw_challenge_stat(double dAlpha, u32 *pYCursor) {
    double dIconX, dIconY, dIconW, dIconH;

    dIconW = (double)(FLOAT_806157f4 * FLOAT_8061082c);
    dIconH = (double)(FLOAT_806157f4 * FLOAT_80610830);

    // Compute icon position (right-aligned, below panel)
    dIconX = (double)(FLOAT_80615734 * FLOAT_8061082c *
             (float)((double)FLOAT_806157b4 - dIconW) +
             (float)((double)CONCAT44(0x43300000,
              DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738));
    dIconY = (double)(FLOAT_80615734 * FLOAT_80610830 *
             (float)((double)FLOAT_806157cc - dIconH) +
             (float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) - DOUBLE_80615738));

    int iChallengeData = FUN_8022b770();
    if (iChallengeData != 0) {
        int iLabel = get_string_by_id(0x657);
        int iHeight = hud_draw_stat_panel(
            (double)(float)((double)CONCAT44(0x43300000,
             DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738),
            (double)(float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) - DOUBLE_80615738),
            dAlpha, iLabel, 0, 0, 0x7f7f7f60);
        *pYCursor += iHeight;

        // Render challenge icon
        FUN_802c8a14(iChallengeData, (int)dIconX, (int)dIconY, (int)dIconW, (int)dIconH);
    }
}

// ============================================================================
// Address: 0x8010a7c0 | hud_draw_accuracy_stats
// Draws two stat panels: "Accuracy" (string 0x666) and "Headshots" (string 0x664).
// Gets values from FUN_80228f1c (accuracy%) and FUN_80228f10 (headshot count).
// ============================================================================
void hud_draw_accuracy_stats(void) {
    u32 *pYCursor;
    u32 uAccuracy, uHeadshots;
    int iLabel;

    pYCursor = (u32 *)FUN_803826ec();
    double dAlpha = /* extraout_f1 */;

    uAccuracy = FUN_80228f1c();
    uHeadshots = FUN_80228f10();

    // Format accuracy string
    u32 uAccText = FUN_8012e878(&DAT_80610864, uAccuracy);

    // Draw "Accuracy" panel (positioned with horizontal offset)
    iLabel = get_string_by_id(0x666);
    float fTopY = (float)((double)CONCAT44(0x43300000,
                  DAT_8057ff50 + DAT_80611748[5] ^ 0x80000000) - DOUBLE_80615738);
    float fOffsetX = FLOAT_80615758 * FLOAT_8061082c +
                     (float)((double)CONCAT44(0x43300000,
                      DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738);

    hud_draw_stat_panel((double)fOffsetX, (double)fTopY, dAlpha, iLabel, uAccText, 0, 0x7f7f7f60);

    // Format headshots string
    u32 uHeadText = FUN_8012e878(&DAT_80610864, uHeadshots);

    // Draw "Headshots" panel
    iLabel = get_string_by_id(0x664);
    float fX = (float)((double)CONCAT44(0x43300000,
               DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738);
    float fY = (float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) - DOUBLE_80615738);

    int iHeight = hud_draw_stat_panel((double)fX, (double)fY, dAlpha,
                                      iLabel, uHeadText, 0, 0x7f7f7f60);
    *pYCursor += iHeight;
    FUN_80382738();
}

// ============================================================================
// Address: 0x8010a930 | hud_draw_streak_stat
// Draws the "Streak" stat panel. Gets current streak value from FUN_80229434.
// String 0x663 = "Streak"
// ============================================================================
void hud_draw_streak_stat(double dAlpha, u32 *pYCursor) {
    int iStreakValue = FUN_80229434();
    u32 uFormatted = FUN_8012e878(&DAT_80610864, iStreakValue);

    int iLabel = get_string_by_id(0x663); // "Streak"

    float fX = (float)((double)CONCAT44(0x43300000,
               DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738);
    float fY = (float)((double)CONCAT44(0x43300000, *pYCursor ^ 0x80000000) - DOUBLE_80615738);

    int iHeight = hud_draw_stat_panel((double)fX, (double)fY, dAlpha,
                                      iLabel, uFormatted, 0, 0x7f7f7f60);
    *pYCursor += iHeight;
}
