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

/* --- Strings ---------------------------------------------------------------- */

int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (uint8_t)*a - (uint8_t)*b;
}

int kstrncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    return n ? (uint8_t)*a - (uint8_t)*b : 0;
}

int kstrcasecmp(const char *a, const char *b)
{
    while (*a && ktolower((uint8_t)*a) == ktolower((uint8_t)*b)) {
        a++;
        b++;
    }
    return ktolower((uint8_t)*a) - ktolower((uint8_t)*b);
}

int kstrncasecmp(const char *a, const char *b, size_t n)
{
    while (n && *a && ktolower((uint8_t)*a) == ktolower((uint8_t)*b)) {
        a++;
        b++;
        n--;
    }
    return n ? ktolower((uint8_t)*a) - ktolower((uint8_t)*b) : 0;
}

char *kstrchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return NULL;
    }
}

char *kstrstr(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);

    if (nl == 0)
        return (char *)hay;
    for (; *hay; hay++) {
        if (*hay == *needle && kstrncmp(hay, needle, nl) == 0)
            return (char *)hay;
    }
    return NULL;
}

size_t kstrlcpy(char *dst, const char *src, size_t cap)
{
    size_t len = strlen(src);

    if (cap) {
        size_t n = len < cap - 1 ? len : cap - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
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

uint32_t kstrtoul(const char *s, const char **end, int base)
{
    uint32_t v = 0;

    while (kisspace((uint8_t)*s))
        s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        kisxdigit((uint8_t)s[2])) {
        s += 2;
        base = 16;
    }
    if (base == 0)
        base = (s[0] == '0') ? 8 : 10;

    for (;; s++) {
        int c = (uint8_t)*s, d;

        if (kisdigit(c))
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * (uint32_t)base + (uint32_t)d;
    }
    if (end)
        *end = s;
    return v;
}

/* --- Formatting ------------------------------------------------------------------- */

int kvsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    size_t n = 0;                        /* characters the full output has    */

#define PUT(ch) do { if (cap && n + 1 < cap) buf[n] = (char)(ch); n++; } while (0)

    for (; *fmt; fmt++) {
        int left = 0, zero = 0, width = 0, prec = -1;
        char conv;

        if (*fmt != '%') {
            PUT(*fmt);
            continue;
        }
        fmt++;

        for (;; fmt++) {                 /* flags                              */
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                zero = 1;
            else
                break;
        }
        if (*fmt == '*') {               /* width                              */
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            fmt++;
        } else {
            while (kisdigit((uint8_t)*fmt))
                width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {               /* precision                          */
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                while (kisdigit((uint8_t)*fmt))
                    prec = prec * 10 + (*fmt++ - '0');
            }
        }
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z')
            fmt++;

        conv = *fmt;
        if (conv == '\0')
            break;

        char        numbuf[40];
        const char *body = numbuf;
        size_t      blen = 0;
        char        sign = 0;

        switch (conv) {
        case '%':
            numbuf[0] = '%';
            blen = 1;
            zero = 0;
            break;
        case 'c':
            numbuf[0] = (char)va_arg(ap, int);
            blen = 1;
            zero = 0;
            break;
        case 's':
            body = va_arg(ap, const char *);
            if (!body)
                body = "(null)";
            blen = strlen(body);
            if (prec >= 0 && (size_t)prec < blen)
                blen = (size_t)prec;
            zero = 0;
            break;
        case 'd':
        case 'i': {
            int32_t  v = va_arg(ap, int32_t);
            uint32_t m = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;

            if (v < 0)
                sign = '-';
            blen = kutoa(numbuf, m, 10, 1);
            break;
        }
        case 'u':
            blen = kutoa(numbuf, va_arg(ap, uint32_t), 10, 1);
            break;
        case 'x':
        case 'X': {
            blen = kutoa(numbuf, va_arg(ap, uint32_t), 16, 1);
            if (conv == 'x') {
                for (size_t i = 0; i < blen; i++)
                    numbuf[i] = (char)ktolower((uint8_t)numbuf[i]);
            }
            break;
        }
        default:                          /* unknown: print verbatim           */
            PUT('%');
            PUT(conv);
            continue;
        }

        size_t total = blen + (sign ? 1 : 0);
        size_t pad   = (width > 0 && (size_t)width > total) ? (size_t)width - total : 0;

        if (!left && !zero)
            while (pad--) PUT(' ');
        if (sign)
            PUT(sign);
        if (!left && zero)
            while (pad--) PUT('0');
        for (size_t i = 0; i < blen; i++)
            PUT(body[i]);
        if (left)
            while (pad--) PUT(' ');
    }
#undef PUT

    if (cap)
        buf[n < cap ? n : cap - 1] = '\0';
    return (int)n;
}

int ksnprintf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = kvsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}
