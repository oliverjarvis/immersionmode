#ifndef JPR_LIBC_STRING_H
#define JPR_LIBC_STRING_H

#include <stddef.h>

/* The few C library functions BearSSL uses; gcc may also call them for large copies. */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#endif
