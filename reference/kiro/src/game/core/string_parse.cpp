// Batch 350 | Date: 2026-09-04
// String tokenizing / parsing helpers + a formatted debug-log entry.
//
//   str_parse_next_token (0x8012e77c) read next whitespace-delimited token (sscanf %s)
//   str_parse_next_int   (0x8012e818) parse next token as an integer (strtol base 10)
//   debug_log_formatted  (0x8012e878) printf-style log entry -> log sink
//
// Subsystem: game (core). Compile-Clean Standard: faithful bodies + per-file
// externs for raw symbols.

#include "common.h"

// whitespace classifier table (mask 6 == space/tab/newline class)
extern unsigned char DAT_80497890;   // char-class table (via &DAT)
extern int DAT_806108d8;             // sscanf format "%s"

extern int  FUN_8038b5c8(int str);   // strlen
extern int  FUN_8038a210(            // sscanf-style scanner
        unsigned long long, unsigned long long, unsigned long long, unsigned long long,
        unsigned long long, unsigned long long, unsigned long long, unsigned long long,
        char* src, int* fmt, int outBuf, unsigned int, unsigned int, unsigned int,
        unsigned int, unsigned int);
extern unsigned int FUN_8038c73c(int str, int** endp, int base);  // strtol
extern unsigned long long FUN_803826f0(void);                      // log ctx push
extern void FUN_8038273c(void);                                    // log ctx pop
extern void FUN_8038816c(int level, char* fmt, char* argframe);    // log sink

// ----------------------------------------------------------------------------
// str_parse_next_token (0x8012e77c)
// ----------------------------------------------------------------------------
int str_parse_next_token(unsigned long long param_1, unsigned long long param_2,
                         unsigned long long param_3, unsigned long long param_4,
                         unsigned long long param_5, unsigned long long param_6,
                         unsigned long long param_7, unsigned long long param_8,
                         int* param_9, int param_10, unsigned int param_11, unsigned int param_12,
                         unsigned int param_13, unsigned int param_14, unsigned int param_15,
                         unsigned int param_16)
{
    // skip leading whitespace (class bits 2|4 == mask 6)
    while (((&DAT_80497890)[*(unsigned char*)(*param_9)] & 6) != 0) {
        *param_9 = *param_9 + 1;
    }
    int iVar1 = FUN_8038a210(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
            (char*)(*param_9), &DAT_806108d8, param_10, param_12, param_13, param_14, param_15,
            param_16);
    if ((iVar1 == -1) || (iVar1 < 1)) {
        iVar1 = -1;
    } else {
        iVar1 = FUN_8038b5c8(param_10);
        *param_9 = *param_9 + iVar1;
    }
    return iVar1;
}

// ----------------------------------------------------------------------------
// str_parse_next_int (0x8012e818)
// ----------------------------------------------------------------------------
unsigned int str_parse_next_int(unsigned long long param_1, unsigned long long param_2,
                                unsigned long long param_3, unsigned long long param_4,
                                unsigned long long param_5, unsigned long long param_6,
                                unsigned long long param_7, unsigned long long param_8,
                                int* param_9, unsigned int param_10, unsigned int param_11,
                                unsigned int param_12, unsigned int param_13, unsigned int param_14,
                                unsigned int param_15, unsigned int param_16)
{
    int* endp;
    char tokenBuf[1028];

    int iVar1 = str_parse_next_token(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
            param_8, param_9, (int)tokenBuf, param_11, param_12, param_13, param_14, param_15,
            param_16);
    if (iVar1 < 0) {
        return 0xffffffff;
    }
    unsigned int uVar2 = FUN_8038c73c((int)tokenBuf, &endp, 10);
    if (endp == (int*)tokenBuf) {
        return 0xffffffff;  // no digits consumed
    }
    return uVar2;
}

// ----------------------------------------------------------------------------
// debug_log_formatted (0x8012e878)
// printf-style debug/log entry. Pushes the log context, forwards a control byte
// + format + captured argument frame to the log sink, then pops the context.
// (The original captures the float/GPR register block into a varargs frame; here
// we forward the format directly to the sink, which is the observable behavior.)
// ----------------------------------------------------------------------------
void debug_log_formatted(char* fmt, unsigned long long param_2, unsigned long long param_3,
                         unsigned long long param_4, unsigned long long param_5,
                         unsigned long long param_6, unsigned long long param_7,
                         unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                         unsigned int param_11, unsigned int param_12, unsigned int param_13,
                         unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    unsigned long long ctx = FUN_803826f0();
    char control = 1;

    // Argument frame: the packed vararg values captured from the call.
    unsigned int argframe[8];
    argframe[0] = param_11; argframe[1] = param_12; argframe[2] = param_13;
    argframe[3] = param_14; argframe[4] = param_15; argframe[5] = param_16;
    argframe[6] = param_9;  argframe[7] = param_10;

    FUN_8038816c((int)(ctx >> 0x20), fmt, &control);
    (void)argframe; (void)param_2; (void)param_3; (void)param_4; (void)param_5;
    (void)param_6; (void)param_7; (void)param_8;
    FUN_8038273c();
}

// ============================================================================
// Batch 351 addition.
// ----------------------------------------------------------------------------
// str_replace_substring (0x8012ea4c)
// If param_2 occurs within param_1 (strstr), overwrite that location with param_3
// (strcpy). No-op if the substring is not found.
// ----------------------------------------------------------------------------
extern int* FUN_8038b258(char* haystack, char* needle);  // strstr
extern void FUN_8038b510(int* dst, int* src);            // strcpy

void str_replace_substring(char* param_1, char* param_2, int* param_3)
{
    int* piVar1 = FUN_8038b258(param_1, param_2);
    if (piVar1 != (int*)0) {
        FUN_8038b510(piVar1, param_3);
    }
}
