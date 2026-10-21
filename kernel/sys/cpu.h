/* =============================================================================
 *  AGOS - CPU identification (sys/cpu.h, sys/cpu.c)
 *  CPUID when available (vendor, family/model/stepping, brand string, feature
 *  flags), a 386/486 probe via EFLAGS when it is not, and the clock speed
 *  measured by counting TSC cycles against the PIT.
 * ========================================================================== */
#ifndef AGOS_CPU_H
#define AGOS_CPU_H

#include <stdbool.h>
#include <stdint.h>

struct cpu_info {
    bool     has_cpuid;
    bool     has_fpu, has_tsc, has_cx8, has_cmov, has_mmx, has_sse, has_sse2;
    char     vendor[13];                /* "GenuineIntel", ... or "unknown"    */
    char     brand[49];                 /* marketing name (CPUID or derived)   */
    const char *class_name;             /* "386", "486", "Pentium", ...        */
    uint32_t family, model, stepping;
    uint32_t max_leaf;
    uint32_t mhz;                       /* measured; 0 = unknown               */
};

/* Identify the CPU. Safe to call before interrupts/timer are set up. */
void cpu_detect(void);

/* Measure the clock speed (needs the PIT running and interrupts enabled; takes
 * about 50 ms). Leaves mhz = 0 if the CPU has no TSC. */
void cpu_calibrate(void);

const struct cpu_info *cpu_get(void);

/* "MMX, SSE, SSE2" style summary of the interesting feature flags. */
void cpu_feature_string(char *buf, unsigned cap);

/* Unit tests / host harness: replace the detected data. */
void cpu_set_for_test(const struct cpu_info *info);

#endif /* AGOS_CPU_H */
