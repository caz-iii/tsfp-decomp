// aiactor_lipsync.cpp
// Procedural mouth-flap / lip-sync animation for talking AI characters.
// Address range 0x801bfc18 - 0x801c01b8 (batch_583). Introduced by the voice
// bark system: aiactor_play_bark_3d (0x80142d2c) starts a lip-sync session via
// lipsync_begin, aiactor_instance_tick (0x80135ecc) advances it per frame via
// lipsync_tick, and the mouth-bone matrix is driven each frame by
// lipsync_apply_mouth_bone.
//
// A session is a 0x414-byte record drawn from a fixed 3-slot pool at
// DAT_805c1a28 (the same block object_anim_defs_reset_state / batch_582 clears).
// Record layout used here:
//   +0x00  int   keyCount          (number of envelope keyframes)
//   +0x04  int   curKeyIndex       (playback cursor)
//   +0x08  float phase             (elapsed time within the envelope)
//   +0x0c  float rate              (advance rate = FLOAT_80616e5c / duration)
//   +0x10  keyframe[keyCount]      (0x08 stride: {float time, u8 openAmount})
//   +0x410 int   userData          (opaque tag stored at begin)
//
// lipsync_build_envelope parses the spoken string into per-character mouth-open
// amounts (vowels open wide, consonants less, punctuation closes), keeps a
// greedy-thinned subset of "loud" keyframes, then quantizes each retained open
// amount to a byte and writes normalized keyframe times into the record.
//
// Non-matching: the 0x43300000 / ^0x80000000 int->double magic-bias idioms are
// reduced to ordinary int->float conversions; the wide FP-context PPC ABI spill
// frame (FUN_803826f0 / FUN_8038273c) is preserved as calls so the ROM stack
// shape is retained.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {
// Fixed 3-slot lip-sync session pool (0x414 bytes/slot); shared with the
// object-weapon anim-defs reset (object_anim_defs_reset_state).
extern int DAT_805c1a28;
// Per-frame time base + int->float normalizers used by the envelope math.
extern float  FLOAT_806108ec;  // frame delta (seconds)
extern int    DAT_806109c0;    // frame-rate denominator (ticks/second)
extern float  FLOAT_80616e40;  // 0.0 baseline open amount
extern float  FLOAT_80616e44;  // lowercase-vowel / uppercase-consonant open amount
extern float  FLOAT_80616e48;  // lowercase-consonant open amount
extern float  FLOAT_80616e4c;  // uppercase-vowel open amount
extern float  FLOAT_80616e50;  // greedy-thin seed threshold (-inf-ish)
extern float  FLOAT_80616e54;  // 0.5 midpoint blend weight
extern float  FLOAT_80616e58;  // sign-flip emphasis multiplier
extern float  FLOAT_80616e5c;  // 1.0
extern float  FLOAT_80616e60;  // open-amount -> byte quantize scale (255)
extern double DOUBLE_80616e68; // int->double bias constant
extern double DOUBLE_80616e70; // int->double bias constant (byte dequant)
} // extern "C"

// Wide FP-context PPC ABI spill frame used by the string-fed builders.
// FUN_803826f0 returns the caller's string arg in the high half of a register
// pair; FUN_8038273c closes the frame.
static inline unsigned long long __ls_frame_open(void)
{
    return ((unsigned long long (*)(void))0x803826f0)();
}
static inline void __ls_frame_close(void)
{
    ((void (*)(void))0x8038273c)();
}

// ===========================================================================
// Address: 0x801bfc18 | Name: lipsync_build_envelope | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Build the mouth-open envelope for the spoken string returned by the spill
// frame. Classifies each character into an open amount (vowels widest,
// consonants medium, punctuation a closed beat), trims trailing silence,
// greedily thins to the loudest ~0x80 keyframes, then writes quantized
// {normalizedTime, openByte} keyframes and the key count into the session
// record (high half of the spill-frame pair).
// ===========================================================================
void lipsync_build_envelope(void)
{
    unsigned long long frame = __ls_frame_open();
    int *record = (int *)(unsigned int)(frame >> 0x20);
    const char *text = (const char *)(unsigned int)frame;

    int keep[200];           // per-key retained flag
    float openAmt[201];      // per-key open amount
    memset_wrapper((int)(keep + 1), 0, 800);

    int count = 1;
    openAmt[1] = FLOAT_80616e40;
    // --- classify each character into an open amount ---
    for (const char *p = text; *p != '\0'; ++p) {
        char c = *p;
        if (c == 'a' || c == 'i' || c == 'o' || c == 'u' || (c >= '0' && c <= '9')) {
            openAmt[++count] = FLOAT_80616e44;          // lowercase vowel / digit
        } else if (c >= 'a' && c <= 'z') {
            openAmt[++count] = FLOAT_80616e48;          // lowercase consonant
        } else if (c == 'A' || c == 'I' || c == 'O' || c == 'U') {
            openAmt[++count] = FLOAT_80616e4c;          // uppercase vowel
        } else if (c >= 'A' && c <= 'Z') {
            openAmt[++count] = FLOAT_80616e44;          // uppercase consonant
        } else if (c == '\'') {
            continue;                                    // apostrophe: no beat
        } else if (c == '.' || c == '!' || c == '?') {
            openAmt[count + 1] = FLOAT_80616e40;         // sentence end: two closed beats
            openAmt[count + 2] = FLOAT_80616e40;
            count += 2;
        } else {
            openAmt[++count] = FLOAT_80616e40;           // other: one closed beat
        }
        if (count > 0xc3) break;
    }

    // --- trim trailing closed beats ---
    while (count > 1 && openAmt[count] <= FLOAT_80616e40) {
        --count;
    }

    unsigned int total = (unsigned int)(count + 1);
    keep[1] = 1;
    openAmt[count + 1] = FLOAT_80616e40;
    keep[total] = 1;

    // --- greedily retain the loudest peaks (up to 0x80 keyframes) ---
    int kept = 2;
    do {
        int best = -1;
        float bestScore = FLOAT_80616e50;
        for (int i = 1; i <= count - 1; ++i) {
            if (keep[i + 1] == 0) {
                float prev = openAmt[i];
                float cur = openAmt[i + 1];
                float next = openAmt[i + 2];
                float score = ABS(cur - (prev + next) * FLOAT_80616e54);
                // emphasize local extrema (open surrounded by closed or vice versa)
                if ((prev < cur) == (next < cur)) {
                    score *= FLOAT_80616e58;
                }
                if (bestScore < score) {
                    best = i;
                    bestScore = score;
                }
            }
        }
        if (best == -1) break;
        ++kept;
        keep[best + 1] = 1;
    } while (kept < 0x80);

    // --- emit retained keyframes: normalized time + quantized open byte ---
    float denom = (float)(int)total - FLOAT_80616e5c;
    int emitted = 0;
    int outOff = 0;
    for (unsigned int i = 0; i < total; ++i) {
        if (keep[i + 1] != 0) {
            ++emitted;
            int openByte = (int)(FLOAT_80616e60 * openAmt[i + 1]);
            *(float *)((int)record + outOff + 0x10) = (float)(int)i / denom;
            *(char *)((int)record + outOff + 0x14) = (char)openByte;
            outOff += 8;
        }
    }
    *record = emitted;

    __ls_frame_close();
}

// ===========================================================================
// Address: 0x801bff9c | Name: lipsync_begin | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Start a lip-sync session for the current talking object (only when its model
// type is 0x7c, the talking-head variant). Claims the first free 0x414-byte
// slot from the 3-entry pool DAT_805c1a28, links it at object+0x3a5*4, clears
// it, builds the envelope from the spoken string, stores the advance rate
// (FLOAT_80616e5c / duration) and the caller's userData tag (param_3).
// The FP-context 'duration' arrives in f1 (an incoming register arg); it is
// expressed here as an explicit parameter.
// ===========================================================================
void lipsync_begin(double duration, unsigned int param_1, unsigned int param_2, unsigned int param_3)
{
    (void)param_1; (void)param_2;
    int *object = (int *)(unsigned int)(__ls_frame_open() >> 0x20);

    if (*(int *)(*(int *)(*(int *)(*object + 4) + 0x80) + 4) == 0x7c) {
        if (object[0x3a5] == 0) {
            int off = 0;
            for (int slot = 3; slot != 0; --slot) {
                if (*(int *)((int)&DAT_805c1a28 + off) == 0) {
                    object[0x3a5] = (int)&DAT_805c1a28 + off;
                    break;
                }
                off += 0x414;
            }
        }
        if (object[0x3a5] != 0) {
            memset_wrapper(object[0x3a5], 0, 0x414);
            lipsync_build_envelope();
            *(float *)(object[0x3a5] + 0xc) = (float)((double)FLOAT_80616e5c / duration);
            *(unsigned int *)(object[0x3a5] + 0x410) = param_3;
        }
    }
    __ls_frame_close();
}

// ===========================================================================
// Address: 0x801c007c | Name: lipsync_release | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Free a lip-sync session handle: clear the pool slot's first word (marking it
// available) and null the object's link pointer.
// ===========================================================================
void lipsync_release(int *pLink)
{
    if ((int *)*pLink != (int *)0) {
        *(int *)*pLink = 0;
    }
    *pLink = 0;
}

// ===========================================================================
// Address: 0x801c009c | Name: lipsync_tick | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Per-frame lip-sync advance for an AI actor (from aiactor_instance_tick). If
// the linked voice sound is no longer playing, release the session once the
// phase has advanced past the first beat. Otherwise advance the phase by
// frameDelta * rate * (1/framerate) and step the keyframe cursor forward,
// releasing when the last keyframe is reached.
// ===========================================================================
void lipsync_tick(int actor)
{
    int session = *(int *)(actor + 0xe94);
    if (session == 0) return;

    unsigned int voiceHandle = *(unsigned int *)(session + 0x410);
    if ((int)voiceHandle < 0 || sound_is_playing(voiceHandle) == 0 ||
        sound_is_handle_valid(*(unsigned int *)(session + 0x410)) == 0) {
        // voice finished: release once past the opening beat
        if (FLOAT_80616e40 < *(float *)(session + 8)) {
            lipsync_release((int *)(actor + 0xe94));
        }
        return;
    }

    // advance phase
    session = *(int *)(actor + 0xe94);
    *(float *)(session + 8) =
        FLOAT_806108ec * *(float *)(session + 0xc) *
        (FLOAT_80616e5c / (float)DAT_806109c0) +
        *(float *)(session + 8);

    // step keyframe cursor
    for (;;) {
        session = *(int *)(actor + 0xe94);
        if (*(float *)(session + 8) < *(float *)(session + *(int *)(session + 4) * 8 + 0x18)) {
            return;
        }
        *(int *)(session + 4) = *(int *)(session + 4) + 1;
        if (((int *)session)[1] >= *(int *)session + -1) break;
    }
    lipsync_release((int *)(actor + 0xe94));
}

// ===========================================================================
// Address: 0x801c01b8 | Name: lipsync_apply_mouth_bone | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Drive the mouth bone of a talking object (model type 0x7c). Interpolates the
// open amount between the current and next keyframe by phase, converts it to a
// Z-rotation, and pre-multiplies it into the mouth bone's palette matrix.
// ===========================================================================
void lipsync_apply_mouth_bone(int *object)
{
    int session = object[0x3a5];
    if (session == 0) return;
    if (*(int *)(*(int *)(*(int *)(*object + 4) + 0x80) + 4) != 0x7c) return;

    float *curKey = (float *)(session + *(int *)(session + 4) * 8 + 0x10);
    float *nextKey = (float *)(session + (*(int *)(session + 4) + 1) * 8 + 0x10);
    unsigned int curByte = *(unsigned char *)((char *)curKey + 4);
    unsigned int nextByte = *(unsigned char *)((char *)nextKey + 4);
    float t0 = *curKey;
    // fraction between the two keyframe times
    float frac = (*(float *)(session + 8) - t0) / (*nextKey - t0);

    float rot[16];
    float openAmount = frac * ((float)nextByte / FLOAT_80616e60) +
                       (FLOAT_80616e5c - frac) * ((float)curByte / FLOAT_80616e60);
    mtx4_set_rotation_z((double)(FLOAT_80616e54 * openAmount), rot);

    // mouth bone palette matrix = bonePalette[*(model+0x84)+0x58] (0x40 stride)
    float *boneMtx = (float *)(*(int *)(*(int *)(*object + 4) + 0x10) +
                               *(int *)(*(int *)(*(int *)(*object + 4) + 0x84) + 0x58) * 0x40);
    mtx4_multiply(rot, boneMtx, boneMtx);
}
