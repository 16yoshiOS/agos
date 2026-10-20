/* =============================================================================
 *  AGOS - Minimal freestanding runtime library (lib.c)
 * ========================================================================== */
#include "lib.h"

/* memset/memcpy use REP string instructions directly. This also avoids the
 * classic trap of the compiler turning a C loop *inside* memset into a call to
 * memset (the Makefile additionally passes -fno-tree-loop-distribute-patterns).
 * The direction flag is cleared explicitly: callers may be interrupt context. */
void *memset(void *dst, int value, size_t n)
{
    void *ret = dst;
    __asm__ volatile ("cld; rep stosb"
                      : "+D"(dst), "+c"(n)
                      : "a"((uint8_t)value)
                      : "memory");
    return ret;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    void *ret = dst;
    __asm__ volatile ("cld; rep movsb"
                      : "+D"(dst), "+S"(src), "+c"(n)
                      :
                      : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t       *d = dst;
    const uint8_t *s = src;

    if (d == s || n == 0)
        return dst;
    if (d < s)                      /* forward copy is safe when dst < src   */
        return memcpy(dst, src, n);

    d += n;                         /* otherwise copy backwards              */
    s += n;
    while (n--)
        *--d = *--s;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = a;
    const uint8_t *pb = b;

    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i])
            return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

size_t kstrlcat(char *dst, const char *src, size_t cap)
{
    size_t len = 0;

    if (cap == 0)
        return 0;
    while (len < cap - 1 && dst[len])
        len++;
    while (len < cap - 1 && *src)
        dst[len++] = *src++;
    dst[len] = '\0';
    return len;
}

size_t kutoa(char *buf, uint32_t value, unsigned base, unsigned min_digits)
{
    static const char digits[] = "0123456789ABCDEF";
    char   tmp[33];
    size_t n = 0;

    if (base < 2 || base > 16)
        base = 10;

    do {
        tmp[n++] = digits[value % base];
        value /= base;
    } while (value && n < sizeof(tmp));

    while (n < min_digits && n < sizeof(tmp))
        tmp[n++] = '0';

    for (size_t i = 0; i < n; i++)      /* digits were produced LSB first    */
        buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}
