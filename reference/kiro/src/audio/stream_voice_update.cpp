// ============================================================================
// stream_voice_update.cpp | batch_504 | Date: 2026-09-06
// Subsystem: audio (streaming-voice per-frame update + DSP mixer-command build).
//
//   stream_voice_update_mix     0x80187da8  recompute volume/pitch, route to slot
//   stream_voice_scale_refresh_f 0x80187e80 scale a value by (refresh*1.5)/0xf
//   stream_voice_scale_refresh_t 0x80187ea8 scale a value by (refresh*3)/0x3f
//   stream_voice_list_insert    0x80187ec8  doubly-linked-list node insert
//   stream_voice_build_mix_cmd  0x80187ee4  build the DSP voice mixer-command record
//
// These sit on top of the batch_477/478 stream-voice slot machinery.  The voice
// record pointer and the output mixer-slot pointer arrive via the spill frames
// (FUN_803826e8 / __save_gpr_25) that the decompiler surfaced as high/low words;
// here they are explicit parameters.
//
// Structurally-faithful non-matching: register-pair spills modelled as params,
// raw DSP/mixer helpers declared as per-file externs matching their call sites.
// ============================================================================
#include "common.h"

extern "C" {
// ---- named cross-module helpers ----
extern int  audio_linear_to_centibels(unsigned int lin);   // linear gain -> centibel attenuation
extern int  stream_time_to_index(double t);                // playback time -> sample index
extern void svslot_route_to_current_player(void);
extern void svslot_set_level(int level, int index);

// ---- raw DSP / mixer helpers (per-file externs) ----
extern unsigned long long FUN_803826e8(void);   // spill frame: voice record ptr (hi word)
extern void  FUN_80382734(void);                 // spill-frame release (matches e8)
extern int   __save_gpr_25(void);                // spill frame: record + slot pair
extern void  __restore_gpr_25(void);
extern void  FUN_8039cca0(void);                 // enter DSP critical section
extern void  FUN_8039ccc8(void);                 // leave DSP critical section
extern int   FUN_803825c0(double x);             // float->fixed sample-rate ratio
extern unsigned int occ_pool_base(void);         // base pan/volume unit
extern int   FUN_803affe4(int voice);            // get current voice volume (centibels)
extern void  FUN_803affbc(int voice, int cb);    // set voice volume (centibels)
extern int   FUN_803b006c(int voice);            // get current voice sample index
extern void  FUN_803b0000(int voice, int idx);   // set voice sample index

extern int   DAT_806109c0;   // display refresh field (0x3c NTSC / 0x32 PAL)
extern float FLOAT_806167c0, FLOAT_806167c4;     // pitch-scale numerator / denominator
extern double DOUBLE_806167b0;                   // int->double bias base
} // extern "C"

// stream_voice_update_mix (0x80187da8)
// Recompute the streaming voice's target volume (centibels) and sample index and
// push them to the mixer.  For a "routed" voice (flags & 0x240) it routes to the
// current player's slot and sets the level; otherwise it writes the volume/index
// directly to the DSP voice at record+8 when they changed.  The voice record
// arrives in the FUN_803826e8 spill frame.
void stream_voice_update_mix(void)
{
    unsigned long long frame = FUN_803826e8();
    int rec = (int)(frame >> 0x20);
    unsigned int routeFlags = *(unsigned int *)(rec + 0x10) & 0x240;

    int cb = audio_linear_to_centibels((unsigned int)frame);
    int idx = stream_time_to_index(0.0);

    FUN_8039cca0();
    if (routeFlags == 0) {
        int curCb = FUN_803affe4(*(int *)(rec + 8));
        if (cb != curCb) {
            FUN_803affbc(*(int *)(rec + 8), cb);
        }
        int curIdx = FUN_803b006c(*(int *)(rec + 8));
        if (idx != curIdx) {
            FUN_803b0000(*(int *)(rec + 8), idx);
        }
    } else {
        svslot_route_to_current_player();
        // (original re-tests routeFlags==0 here; unreachable, preserved as no-op)
        svslot_set_level((int)*(char *)(rec + 0x41), idx);
    }
    FUN_8039ccc8();
    FUN_80382734();
}

// stream_voice_scale_refresh_f (0x80187e80)
// Scale param by (refresh + refresh/2) and divide by 0xf, masked to 16 bits.
unsigned int stream_voice_scale_refresh_f(int param)
{
    int r = DAT_806109c0;
    int scaled = r + (r >> 1) + ((r < 0 && (r & 1) != 0) ? 1 : 0);
    return (unsigned int)((param * scaled) & 0xffff) / 0xf;
}

// stream_voice_scale_refresh_t (0x80187ea8)
// Scale param by (refresh*3) and divide by 0x3f, masked to 16 bits.
unsigned int stream_voice_scale_refresh_t(int param)
{
    return (unsigned int)((param * DAT_806109c0 * 3) & 0xffff) / 0x3f;
}

// stream_voice_list_insert (0x80187ec8)
// Insert node into a doubly-linked list after head: node->next = head->next,
// head->next->prev = node (via node[0]=head... matching the raw pointer writes),
// head->next = node.  Layout: [0]=next, [1]=prev.
void stream_voice_list_insert(int *head, int *node)
{
    *(int **)(*head + 4) = node;   // head->next->prev = node
    *node = *head;                 // node->next = head->next
    node[1] = (int)head;           // node->prev = head
    *head = (int)node;             // head->next = node
}

// stream_voice_build_mix_cmd (0x80187ee4)
// Build the DSP voice mixer-command record into the output slot (slot+0x1a6..).
// Computes a fixed-point sample-rate ratio from the record's rate field via the
// 2^52-bias dequant + FUN_803825c0, sets a stereo/mono tag (10 when the record's
// id field == 0x7f7f), copies the 0x14-entry mix matrix, and stores the ratio.
// Two paths: param_3==0 uses a halved pan base (occ_pool_base) and a shorter
// header; param_3!=0 copies the record's own pan block and, for mono (tag 0),
// duplicates a 3-word tail and sets flag 0x100000.  Record ptr = spill high word,
// output slot = spill low word.
void stream_voice_build_mix_cmd(unsigned int param_1, unsigned int param_2, int param_3)
{
    unsigned long long frame = (unsigned long long)__save_gpr_25();
    int rec  = (int)(frame >> 0x20);
    int slot = (int)frame;

    float ratef = (float)((double)(float)((double)(unsigned long long)
                     ((unsigned int)*(int *)(rec + 4)) - DOUBLE_806167b0));
    int ratio = FUN_803825c0((double)(FLOAT_806167c0 * (ratef / FLOAT_806167c4)));
    unsigned short tag = (unsigned short)(-(short)(*(short *)(rec + 2) == 0x7f7f) & 10);
    unsigned short ratioHi = (unsigned short)((unsigned int)ratio >> 0x10);

    if (param_3 == 0) {
        unsigned int pan = occ_pool_base();
        unsigned int a = *(unsigned int *)(rec + 0xc);
        unsigned int b = *(unsigned int *)(rec + 0x10);
        unsigned short *src = (unsigned short *)(rec + 0x14);
        if (tag == 10) pan >>= 1;

        FUN_8039cca0();
        *(unsigned short *)(slot + 0x1a6) = 0;
        *(unsigned short *)(slot + 0x1a8) = tag;
        *(short *)(slot + 0x1aa) = (short)(pan >> 0x10);
        *(short *)(slot + 0x1ac) = (short)pan;
        *(short *)(slot + 0x1ae) = (short)(a >> 0x10);
        *(short *)(slot + 0x1b0) = (short)a;
        *(short *)(slot + 0x1b2) = (short)(b >> 0x10);
        *(short *)(slot + 0x1b4) = (short)b;

        unsigned short *dst = (unsigned short *)(slot + 0x1b6);
        for (int i = 0; i < 0x14; ++i) dst[i] = src[i];
        dst[0x14] = ratioHi;
        dst[0x15] = (unsigned short)ratio;
        dst[0x16] = 0; dst[0x17] = 0; dst[0x18] = 0; dst[0x19] = 0; dst[0x1a] = 0;

        *(unsigned int *)(slot + 0x1c) |= 0x61000;
        FUN_8039ccc8();
    } else {
        unsigned int a = *(unsigned int *)(rec + 8);
        unsigned int b = *(unsigned int *)(rec + 0xc);
        unsigned int c = *(unsigned int *)(rec + 0x10);
        unsigned short *src = (unsigned short *)(rec + 0x14);

        FUN_8039cca0();
        *(unsigned short *)(slot + 0x1a6) = 1;
        *(unsigned short *)(slot + 0x1a8) = tag;
        *(short *)(slot + 0x1aa) = (short)(a >> 0x10);
        *(short *)(slot + 0x1ac) = (short)a;
        *(short *)(slot + 0x1ae) = (short)(b >> 0x10);
        *(short *)(slot + 0x1b0) = (short)b;
        *(short *)(slot + 0x1b2) = (short)(c >> 0x10);
        *(short *)(slot + 0x1b4) = (short)c;

        unsigned short *dst = (unsigned short *)(slot + 0x1b6);
        for (int i = 0; i < 0x14; ++i) dst[i] = src[i];
        dst[0x14] = ratioHi;
        dst[0x15] = (unsigned short)ratio;
        dst[0x16] = 0; dst[0x17] = 0; dst[0x18] = 0; dst[0x19] = 0; dst[0x1a] = 0;

        *(unsigned int *)(slot + 0x1c) |= 0x61000;
        if (tag == 0) {
            // mono: duplicate the 3-word pan tail and set the extra-routing flag
            dst[0x1b] = src[0x14 + 1];
            dst[0x1c] = src[0x14 + 2];
            dst[0x1d] = src[0x14 + 3];
            *(unsigned int *)(slot + 0x1c) |= 0x100000;
        }
        FUN_8039ccc8();
    }
    __restore_gpr_25();
}
