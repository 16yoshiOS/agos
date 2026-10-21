/* =============================================================================
 *  AGOS - CPU identification (sys/cpu.c)
 * ========================================================================== */
#include "cpu.h"
#include "lib.h"
#include "pit.h"

static struct cpu_info info;

#if defined(__i386__)

static inline void do_cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static inline void do_rdtsc(uint32_t *lo, uint32_t *hi)
{
    __asm__ volatile ("rdtsc" : "=a"(*lo), "=d"(*hi));
}

static uint32_t read_eflags(void)
{
    uint32_t f;
    __asm__ volatile ("pushfl; popl %0" : "=r"(f));
    return f;
}

static void write_eflags(uint32_t f)
{
    __asm__ volatile ("pushl %0; popfl" : : "r"(f) : "cc");
}

/* CPUID exists iff EFLAGS bit 21 (ID) can be toggled. */
static bool cpuid_supported(void)
{
    uint32_t orig = read_eflags();

    write_eflags(orig ^ (1u << 21));
    bool toggled = ((read_eflags() ^ orig) & (1u << 21)) != 0;
    write_eflags(orig);
    return toggled;
}

/* 486+ can toggle EFLAGS bit 18 (AC); a 386 cannot. */
static bool is_486_or_better(void)
{
    uint32_t orig = read_eflags();

    write_eflags(orig ^ (1u << 18));
    bool toggled = ((read_eflags() ^ orig) & (1u << 18)) != 0;
    write_eflags(orig);
    return toggled;
}

#else   /* ---- build host (unit tests): no real CPUID ------------------------ */

static inline void do_cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    (void)leaf;
    *a = *b = *c = *d = 0;
}
static inline void do_rdtsc(uint32_t *lo, uint32_t *hi) { *lo = *hi = 0; }
static bool cpuid_supported(void) { return false; }
static bool is_486_or_better(void) { return true; }

#endif

static void store_u32(char *dst, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        dst[i] = (char)(v >> (8 * i));
}

/* Name for CPUs that predate the brand string (or hide it). */
static void synthesize_brand(void)
{
    const char *v = info.vendor;
    const char *name = NULL;

    if (!kstrcmp(v, "GenuineIntel")) {
        switch (info.family) {
        case 4:  name = "Intel 486"; break;
        case 5:  name = info.model >= 4 ? "Intel Pentium MMX" : "Intel Pentium"; break;
        case 6:  name = info.model >= 7 ? "Intel Pentium III"
                       : info.model >= 3 ? "Intel Pentium II" : "Intel Pentium Pro"; break;
        case 15: name = "Intel Pentium 4"; break;
        default: name = "Intel processor"; break;
        }
    } else if (!kstrcmp(v, "AuthenticAMD")) {
        switch (info.family) {
        case 4:  name = "AMD 486"; break;
        case 5:  name = info.model >= 6 ? "AMD K6" : "AMD K5"; break;
        case 6:  name = "AMD Athlon"; break;
        default: name = "AMD processor"; break;
        }
    } else if (!kstrcmp(v, "CyrixInstead")) {
        name = "Cyrix processor";
    } else if (!kstrcmp(v, "CentaurHauls")) {
        name = "VIA/Centaur processor";
    } else if (!kstrcmp(v, "TransmetaCPU") || !kstrcmp(v, "GenuineTMx86")) {
        name = "Transmeta Crusoe";
    }
    if (name)
        kstrlcpy(info.brand, name, sizeof(info.brand));
    else
        ksnprintf(info.brand, sizeof(info.brand), "%s family %u", v, info.family);
}

void cpu_detect(void)
{
    uint32_t a, b, c, d;

    memset(&info, 0, sizeof(info));
    kstrlcpy(info.vendor, "unknown", sizeof(info.vendor));
    info.class_name = "x86";

    if (!cpuid_supported()) {
        bool is486 = is_486_or_better();

        info.family = is486 ? 4 : 3;
        info.class_name = is486 ? "486" : "386";
        kstrlcpy(info.brand, is486 ? "i486-class (no CPUID)" : "i386-class (no CPUID)",
                 sizeof(info.brand));
        return;
    }

    info.has_cpuid = true;
    do_cpuid(0, &a, &b, &c, &d);
    info.max_leaf = a;
    store_u32(info.vendor + 0, b);
    store_u32(info.vendor + 4, d);
    store_u32(info.vendor + 8, c);
    info.vendor[12] = '\0';

    if (info.max_leaf >= 1) {
        uint32_t sig;

        do_cpuid(1, &a, &b, &c, &d);
        sig = a;
        info.stepping = sig & 0xF;
        info.model    = (sig >> 4) & 0xF;
        info.family   = (sig >> 8) & 0xF;
        if (info.family == 0xF)
            info.family += (sig >> 20) & 0xFF;
        if (((sig >> 8) & 0xF) == 6 || ((sig >> 8) & 0xF) == 0xF)
            info.model |= ((sig >> 16) & 0xF) << 4;

        info.has_fpu  = (d >> 0)  & 1;
        info.has_tsc  = (d >> 4)  & 1;
        info.has_cx8  = (d >> 8)  & 1;
        info.has_cmov = (d >> 15) & 1;
        info.has_mmx  = (d >> 23) & 1;
        info.has_sse  = (d >> 25) & 1;
        info.has_sse2 = (d >> 26) & 1;
    }

    switch (info.family) {
    case 4:  info.class_name = "486"; break;
    case 5:  info.class_name = "Pentium"; break;
    case 6:  info.class_name = "P6"; break;
    case 15: info.class_name = "NetBurst"; break;
    default: info.class_name = info.family > 6 ? "Modern x86" : "x86"; break;
    }

    /* Brand string: extended leaves 0x80000002..4 (48 bytes). */
    do_cpuid(0x80000000u, &a, &b, &c, &d);
    if (a >= 0x80000004u) {
        char raw[49];

        for (uint32_t i = 0; i < 3; i++) {
            do_cpuid(0x80000002u + i, &a, &b, &c, &d);
            store_u32(raw + i * 16 + 0, a);
            store_u32(raw + i * 16 + 4, b);
            store_u32(raw + i * 16 + 8, c);
            store_u32(raw + i * 16 + 12, d);
        }
        raw[48] = '\0';

        const char *p = raw;
        while (*p == ' ')
            p++;
        kstrlcpy(info.brand, p, sizeof(info.brand));
        /* Trim trailing blanks. */
        for (size_t n = strlen(info.brand); n && info.brand[n - 1] == ' '; n--)
            info.brand[n - 1] = '\0';
    }
    if (!info.brand[0])
        synthesize_brand();
}

void cpu_calibrate(void)
{
    uint32_t lo0, hi0, lo1, hi1, t0, now;

    info.mhz = 0;
    if (!info.has_tsc)
        return;

    t0 = pit_clock_us();
    while (pit_clock_us() == t0)         /* start on a fresh microsecond tick   */
        ;
    t0 = pit_clock_us();
    do_rdtsc(&lo0, &hi0);
    do {
        now = pit_clock_us();
    } while ((uint32_t)(now - t0) < 50000u);
    do_rdtsc(&lo1, &hi1);

    uint32_t us = now - t0;
    uint32_t cycles = lo1 - lo0;         /* < 2^32 up to ~85 GHz at 50 ms       */

    (void)hi0;
    (void)hi1;
    if (us)
        info.mhz = (cycles + us / 2) / us;
}

const struct cpu_info *cpu_get(void)
{
    return &info;
}

void cpu_feature_string(char *buf, unsigned cap)
{
    buf[0] = '\0';
    if (info.has_fpu)  { kstrlcat(buf, "FPU", cap); }
    if (info.has_tsc)  { kstrlcat(buf, buf[0] ? ", TSC" : "TSC", cap); }
    if (info.has_cmov) { kstrlcat(buf, buf[0] ? ", CMOV" : "CMOV", cap); }
    if (info.has_mmx)  { kstrlcat(buf, buf[0] ? ", MMX" : "MMX", cap); }
    if (info.has_sse)  { kstrlcat(buf, buf[0] ? ", SSE" : "SSE", cap); }
    if (info.has_sse2) { kstrlcat(buf, buf[0] ? ", SSE2" : "SSE2", cap); }
    if (!buf[0])
        kstrlcat(buf, "none reported", cap);
}

void cpu_set_for_test(const struct cpu_info *src)
{
    info = *src;
}
