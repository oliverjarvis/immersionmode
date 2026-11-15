#ifndef JPR_UTIL_H
#define JPR_UTIL_H

#include "types.h"

void *jpr_memcpy(void *dst, const void *src, u32 n);
void *jpr_memset(void *dst, int value, u32 n);

/* True for a 4-byte-aligned address in a region game objects live in: the heap, the old linear
 * heap or the linear heap. Used before following a pointer read from a game object. */
int jpr_plausible_ptr(u32 p);

/* Appends code point cp as UTF-8 at out[*at] if it fits in cap; returns 0, or -1 when it does not. */
int jpr_utf8_put(u8 *out, u32 *at, u32 cap, u32 cp);

/* Writes n in decimal with a terminating NUL (at most 11 bytes); returns the digits written. */
u32 jpr_format_u32(char *out, u32 n);

/* A full memory barrier (ARMv6 data memory barrier) between the panel's thread and the worker. */
static inline void jpr_barrier(void) {
#if JPR_HOST
    __asm__ volatile("" ::: "memory");
#else
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 5" ::"r"(0) : "memory");
#endif
}

#endif
