// =============================================================================
// sound_ambient.cpp - Ambient / averaged positional sound source update
// Address range: 0x80010fcc - 0x8001110c
// Batch: 378 | Date: 2026-09-05
//
// Drives a single 3D "ambience" sound source whose position/level is derived
// from an averaged per-entity distance metric across the active entity list.
//   * ambient_weight_lookup   (0x80010fcc) key->weight table scan
//   * ambient_source_update    (0x80011008) average metric + push to 3D source
// Callers: FUN_801f10d4, FUN_8021effc (per-frame ambience drivers).
// =============================================================================
#include "common.h"
#include "soundpad.h"   // pulls generated_decls.h (scalar_step_toward_snap, sound_update_3d_source)

// ---- raw symbols referenced from this file (per-file externs) -------------
// Scratch-stack alloc/free pair used to obtain a temporary float[4] work area.
extern "C" unsigned long long FUN_803826e4(void);  // returns work ptr (in low 32b)
extern "C" void FUN_80382730(void);                // release scratch frame

extern int DAT_80612610;   // active entity count
extern int DAT_80611d78;   // base of entity record array (stride 0x141c)

// ---- shared numeric constants (rodata @ 0x806137b8..) ---------------------
extern float FLOAT_806137b8;  // 1.0
extern float FLOAT_806137bc;  // 0.5
extern float FLOAT_806137c0;  // 0.0
extern float FLOAT_806137c4;  // 0.025 (snap step)
extern float FLOAT_806137c8;  // 50.0  (pitch / range constant)
extern double DOUBLE_806137d0; // uint->double conversion magic bias

// ambient_weight_lookup (0x80010fcc)
// Walk a key/behaviour table (terminated by key == -100). If the special key
// -0x65 is seen, the running weight drops to 0.5; if the target key is found,
// the current weight is returned. If the table terminates first, the caller's
// default weight (passthrough) is returned.
double ambient_weight_lookup(double passthrough, int targetKey, int* table)
{
    int key;
    double weight;

    weight = (double)FLOAT_806137b8;  // start at 1.0
    do {
        key = *table;
        if (key == -100) {
            return passthrough;
        }
        if (key == -0x65) {
            weight = (double)FLOAT_806137bc;  // 0.5
        } else if (key == targetKey) {
            return weight;
        }
        table = table + 1;
    } while (true);
}

// ambient_source_update (0x80011008)
// Average an ambient weight across every live entity (those with a non-negative
// flag at record+4), snap the smoothed level toward it, and push the result to
// the shared 3D ambience source.
void ambient_source_update(unsigned int unusedA, unsigned int unusedB, int* keyTable)
{
    float* work;
    unsigned int count;
    int i;
    int rec;
    double weight;
    double level;
    double sample;
    unsigned long long scratch;

    (void)unusedA;
    (void)unusedB;

    scratch = FUN_803826e4();
    work = (float*)(unsigned long)scratch;
    level = (double)FLOAT_806137c0;  // 0.0

    if (keyTable != (int*)0) {
        count = 0;
        rec = 0;
        weight = (double)FLOAT_806137c0;
        for (i = 0; i < DAT_80612610; i = i + 1) {
            if (-1 < *(int*)(DAT_80611d78 + rec + 4)) {
                // record+8 -> entity ptr; +0x2a0 -> that entity's type key
                sample = ambient_weight_lookup(
                    weight,
                    *(int*)(*(int*)(DAT_80611d78 + rec + 8) + 0x2a0),
                    keyTable);
                level = (double)(float)(level + sample);
                count = count + 1;
            }
            rec = rec + 0x141c;
        }
        // divide accumulated level by the live-entity count (uint->double)
        level = (double)(float)(level / (double)(float)(
            (double)CONCAT44(0x43300000, count ^ 0x80000000) - DOUBLE_806137d0));
    }

    // smooth toward target and store into work[0]
    scalar_step_toward_snap(level, (double)FLOAT_806137c4, work);

    // push to the ambience 3D source. The generated header declares
    // sound_update_3d_source(range, pitch, volume, sample_id, handle_ptr,
    // position, flags); the original call is under-typed, so we map the
    // averaged level to 'range', constants to pitch/volume, and the work
    // vector (work+1) as the position. sample_id/handle/flags are 0/NULL.
    sound_update_3d_source((double)work[0],
                           (double)FLOAT_806137c8,
                           (double)FLOAT_806137b8,
                           0,
                           (u32*)0,
                           work + 1,
                           0);

    FUN_80382730();
}
