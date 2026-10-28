// Address: 0x80354fb8 | Name: weather_pick_strike_point | Date: 2026-09-12
//
// Weather lightning strike-point picker. Given a level "strike volume" block
// (passed via the caller's saved GPRs), pick a random sub-zone and a random
// grid cell within it, then emit the cell's world position (or packed short
// coords, depending on the block's 0x10 flag) into a 5-int output record:
//   out[0] = block id (owner)   out[1] = zone index
//   out[2..4] = world position (x, y, z as ints/floats)
// Called from FUN_80355638 (weather_lightning bolt spawn).
//
// The block layout (puVar13 = *(uint**)(*(int*)(blockId + 4) + 4)):
//   [0]      zone count
//   [3]      flags (bit 0x10 => packed 16-bit coords, else 32-bit floats)
//   [0xb..d] origin offset (x, y, z) for the packed-coord path
//   per-zone stride 0x30 dwords, base at +0 offset (zoneBase + zoneIdx*0x30):
//     +1   cell-array base pointer
//     +0x15 cell-list head pointer (entries of 8 bytes; +3 sign bit = end)
//   cell entry (8 bytes): +2 sub-count (byte), +6 cell index (u16)

#include "common.h"

// scalar / vector constants
extern double DOUBLE_8061add0;  // int->double bias
extern float  FLOAT_8061adc8;   // packed-coord scale

// helpers (raw addresses; typed to the call sites)
extern int           rng_counter_tick(void);
extern void          memset_wrapper(int dst, int val, int len);
extern unsigned long long __save_gpr_25(void);
extern void          __restore_gpr_25(void);

// ============================================================================
// weather_pick_strike_point  (0x80354fb8)
// blockId is passed in the high word of the __save_gpr_25 result (matching the
// original ABI where the argument arrived in a callee-saved register); the low
// word is a "deterministic" flag (0 => pick a random zone). out is the 5-int
// result record.
// ============================================================================
void weather_pick_strike_point(unsigned int param_1, unsigned int param_2, int* out)
{
    unsigned long long saved = __save_gpr_25();
    int blockId = (int)(saved >> 0x20);
    int lowFlag = (int)saved;

    int zoneIdx = 0;
    unsigned int* block = *(unsigned int**)(*(int*)(blockId + 4) + 4);
    unsigned int used = 0;
    unsigned int zoneCount = *block;

    memset_wrapper((int)out, 0, 0x14);

    if (lowFlag == 0) {
        unsigned int r = (unsigned int)rng_counter_tick();
        zoneIdx = (int)(r - (r / *block) * *block);
    }

    unsigned int cellHead = block[zoneCount * -0x30 + zoneIdx * 0x30 + 0x15];

    // Count cells until the terminator (entry+3 sign bit set).
    for (unsigned int p = cellHead; (p != 0 && (-1 < *(char*)(p + 3))); p += 8) {
        used += 1;
    }

    if (used != 0) {
        unsigned int r = (unsigned int)rng_counter_tick();
        int cell = cellHead + (r - (r / used) * used) * 8;

        if (*(char*)(cell + 2) != '\0') {
            unsigned int rr = (unsigned int)rng_counter_tick();
            int sub = (int)(rr - (rr / *(unsigned char*)(cell + 2)) * (unsigned int)*(unsigned char*)(cell + 2));

            int rowBase;
            if ((block[3] & 0x10) == 0) {
                rowBase = block[zoneCount * -0x30 + zoneIdx * 0x30 + 1] + (unsigned int)*(unsigned short*)(cell + 6) * 0xc;
            } else {
                rowBase = block[zoneCount * -0x30 + zoneIdx * 0x30 + 1] + (unsigned int)*(unsigned short*)(cell + 6) * 6;
            }

            out[0] = blockId;
            out[1] = zoneIdx;
            double bias = DOUBLE_8061add0;
            float scale = FLOAT_8061adc8;

            if ((block[3] & 0x10) == 0) {
                int* cellPos = (int*)(rowBase + sub * 0xc);
                out[2] = cellPos[0];
                out[3] = cellPos[1];
                out[4] = cellPos[2];
            } else {
                short* packed = (short*)(rowBase + sub * 6);
                out[2] = (int)(scale * (float)((double)CONCAT44(0x43300000, (unsigned int)((int)packed[0] ^ 0x80000000)) - bias) + (float)block[0xb]);
                out[3] = (int)(scale * (float)((double)CONCAT44(0x43300000, (unsigned int)((int)packed[1] ^ 0x80000000)) - bias) + (float)block[0xc]);
                out[4] = (int)(scale * (float)((double)CONCAT44(0x43300000, (unsigned int)((int)packed[2] ^ 0x80000000)) - bias) + (float)block[0xd]);
            }
        }
    }

    __restore_gpr_25();
    (void)param_1;
    (void)param_2;
}
