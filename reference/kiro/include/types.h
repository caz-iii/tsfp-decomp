#ifndef TSFP_TYPES_H
#define TSFP_TYPES_H

// GameCube-compatible type definitions for tsfp-decomp
// These match the types used in the Nintendo GameCube SDK

#include <stdint.h>

// Unsigned integer types
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Signed integer types
typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

// Floating point types
typedef float  f32;
typedef double f64;

// Boolean type (GameCube uses int-based BOOL)
typedef int BOOL;
#define TRUE  1
#define FALSE 0

// Ghidra decompiler scalar aliases.
// The decompiler emits these spellings directly; provide them so its output
// compiles without hand-editing every occurrence.
typedef unsigned int       uint;
typedef unsigned short     ushort;
typedef unsigned char      uchar;
typedef unsigned char      byte;
typedef unsigned long      ulong;
typedef unsigned long long ulonglong;
typedef long long          longlong;

// Ghidra "undefined" placeholder aliases. The decompiler emits these for values
// whose type it could not infer; they are raw byte-width scalars. Defining them
// here lets generated_decls.h (and raw decompiler output) compile unmodified.
typedef unsigned char      undefined;
typedef unsigned char      undefined1;
typedef unsigned short     undefined2;
typedef unsigned int       undefined4;
typedef unsigned long long undefined8;

// Common handle types
typedef void* OSThread;
typedef void* OSMutex;
typedef void* OSAlarm;
typedef void* DVDFileInfo;
typedef void* GXFifoObj;
typedef void* GXRenderModeObj;

// Function pointer types
typedef void (*OSAlarmHandler)(void* alarm, void* context);
typedef void (*DVDCallback)(s32 result, DVDFileInfo* fileInfo);

// NULL definition
#ifndef NULL
#define NULL ((void*)0)
#endif

#endif // TSFP_TYPES_H
