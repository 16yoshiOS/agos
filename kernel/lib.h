/* =============================================================================
 *  AGOS - Minimal freestanding runtime library (lib.h / lib.c)
 *  The few libc-style primitives the kernel needs. No allocation, no 64-bit
 *  arithmetic (so no libgcc helper calls are ever required).
 * ========================================================================== */
#ifndef AGOS_LIB_H
#define AGOS_LIB_H

#include <stddef.h>
#include <stdint.h>

/* GCC may emit calls to these four even in freestanding code. */
void  *memset(void *dst, int value, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);

/* Append src to dst. dst has room for `cap` bytes including the NUL. Output is
 * always NUL-terminated (when cap > 0) and truncated if it does not fit.
 * Returns the resulting length of dst. */
size_t kstrlcat(char *dst, const char *src, size_t cap);

/* Convert an unsigned value to text in base 2..16 (uppercase digits), left
 * padded with zeros to at least `min_digits` digits. `buf` must hold at least
 * 34 bytes. Returns the number of characters written (excluding the NUL). */
size_t kutoa(char *buf, uint32_t value, unsigned base, unsigned min_digits);

#endif /* AGOS_LIB_H */
