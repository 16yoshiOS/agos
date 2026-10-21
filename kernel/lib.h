/* =============================================================================
 *  AGOS - Minimal freestanding runtime library (lib.h / lib.c)
 *  The libc-style primitives the kernel needs. No allocation here (see
 *  mm/heap.h), no 64-bit arithmetic (so no libgcc helper calls are required),
 *  no floating point.
 * ========================================================================== */
#ifndef AGOS_LIB_H
#define AGOS_LIB_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* GCC may emit calls to these four even in freestanding code. */
void  *memset(void *dst, int value, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);

/* --- Strings ---------------------------------------------------------------- */
int    kstrcmp(const char *a, const char *b);
int    kstrncmp(const char *a, const char *b, size_t n);
int    kstrcasecmp(const char *a, const char *b);
int    kstrncasecmp(const char *a, const char *b, size_t n);
char  *kstrchr(const char *s, int c);
char  *kstrstr(const char *hay, const char *needle);

/* Copy src into dst (cap bytes incl. NUL, always terminated when cap > 0).
 * Returns strlen(src), so `kstrlcpy(...) >= cap` means truncation. */
size_t kstrlcpy(char *dst, const char *src, size_t cap);

/* Append src to dst. Output is always NUL-terminated (when cap > 0) and
 * truncated if it does not fit. Returns the resulting length of dst. */
size_t kstrlcat(char *dst, const char *src, size_t cap);

/* Convert an unsigned value to text in base 2..16 (uppercase digits), left
 * padded with zeros to at least `min_digits` digits. `buf` must hold at least
 * 34 bytes. Returns the number of characters written (excluding the NUL). */
size_t kutoa(char *buf, uint32_t value, unsigned base, unsigned min_digits);

/* strtoul-style parser: optional leading blanks, optional 0x prefix when base
 * is 0 or 16. *end (if not NULL) receives the first unparsed character. */
uint32_t kstrtoul(const char *s, const char **end, int base);

/* --- Formatting (no floating point) -------------------------------------------
 * Supported: %% %c %s %d %i %u %x %X, flags '-' and '0', width (number or *),
 * precision for %s (.N or .*), and the length modifiers l/h/z (ignored: every
 * integer is 32-bit). Output is always NUL-terminated when cap > 0; the return
 * value is the length the full output would have had (like snprintf). */
int ksnprintf(char *buf, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
int kvsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);

/* --- ctype -------------------------------------------------------------------- */
static inline int kisdigit(int c) { return c >= '0' && c <= '9'; }
static inline int kisalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline int kisalnum(int c) { return kisdigit(c) || kisalpha(c); }
static inline int kisspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
static inline int kisxdigit(int c)
{
    return kisdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static inline int ktolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static inline int ktoupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

/* --- Bulk 32-bit memory ops (pixel fills / copies) -------------------------------
 * REP STOSD / MOVSD on x86, plain loops on the build host (unit tests). */
static inline void mem_fill32(uint32_t *dst, uint32_t value, size_t n)
{
#if defined(__i386__)
    __asm__ volatile ("cld; rep stosl"
                      : "+D"(dst), "+c"(n)
                      : "a"(value)
                      : "memory");
#else
    while (n--)
        *dst++ = value;
#endif
}

static inline void mem_copy32(uint32_t *dst, const uint32_t *src, size_t n)
{
#if defined(__i386__)
    __asm__ volatile ("cld; rep movsl"
                      : "+D"(dst), "+S"(src), "+c"(n)
                      :
                      : "memory");
#else
    while (n--)
        *dst++ = *src++;
#endif
}

#endif /* AGOS_LIB_H */
