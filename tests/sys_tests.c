/* =============================================================================
 *  AGOS - memory / accounting / RTC tests (tests/sys_tests.c)
 *  Runs the real pmm.c, heap.c, acct.c, cpu.c and rtc.c on the build host. The
 *  PMM works on a malloc'ed arena that stands in for physical memory.
 * ========================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acct.h"
#include "cpu.h"
#include "fake_io.h"
#include "heap.h"
#include "multiboot.h"
#include "pmm.h"
#include "rtc.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* PIT clock stub for acct/cpu: tests advance time by hand. */
static uint32_t fake_us;
uint32_t pit_clock_us(void) { return fake_us; }

#define ARENA_SIZE (64u * 1024 * 1024)
static uint8_t *arena;

/* --- Build a fake Multiboot info + memory map inside the arena ------------------- */
static struct multiboot_info *make_mbi(uint32_t top_of_ram)
{
    struct multiboot_info *mbi = (struct multiboot_info *)(arena + 0x10000);
    struct multiboot_mmap_entry *e = (struct multiboot_mmap_entry *)(arena + 0x10100);

    memset(mbi, 0, sizeof(*mbi));
    e[0] = (struct multiboot_mmap_entry){ 20, 0, 0, 0x9FC00, 0, MB_MMAP_AVAILABLE };
    e[1] = (struct multiboot_mmap_entry){ 20, 0x9FC00, 0, 0x60400, 0, 2 };       /* reserved */
    e[2] = (struct multiboot_mmap_entry){ 20, 0x100000, 0, top_of_ram - 0x100000, 0, MB_MMAP_AVAILABLE };
    mbi->flags = MBI_FLAG_MMAP;
    mbi->mmap_addr = 0x10100;
    mbi->mmap_length = 3 * 24;
    return mbi;
}

static void test_pmm(void)
{
    struct pmm_stats st, st2;
    struct multiboot_info *mbi = make_mbi(0x1000000);        /* 16 MiB machine */
    uint32_t kend = 0x160000;                                /* 0x60000 of kernel */

    pmm_virt_offset = (uintptr_t)arena;
    CHECK(pmm_init(mbi, kend));
    pmm_get_stats(&st);
    CHECK(st.total_kb == (0x9FC00 + 0xF00000) / 1024);       /* 640K-ish + 15 MiB */
    CHECK(st.kernel_kb == 0x60000 / 1024);
    CHECK(st.meta_kb == 4);                                  /* 4096 frames -> 512 B -> one page */
    /* free = region above 1 MiB minus kernel and bitmap and mbi pages */
    CHECK(st.free_kb < 15 * 1024 && st.free_kb > 15 * 1024 - 0x60000 / 1024 - 64);

    uint32_t a = pmm_alloc(1);
    CHECK(a >= kend && (a & 0xFFF) == 0);
    uint32_t b = pmm_alloc(10);
    CHECK(b > a && (b & 0xFFF) == 0);
    pmm_get_stats(&st2);
    CHECK(st.free_kb - st2.free_kb == 11 * 4);

    pmm_free(b, 10);
    uint32_t c = pmm_alloc(10);                              /* reuses the hole   */
    CHECK(c == b);
    pmm_free(c, 10);
    pmm_free(a, 1);
    pmm_get_stats(&st2);
    CHECK(st2.free_kb == st.free_kb);

    pmm_free(a, 1);                                          /* double free is harmless */
    pmm_free(0x1000, 1);                                     /* below 1 MiB ignored     */
    pmm_get_stats(&st2);
    CHECK(st2.free_kb == st.free_kb);

    /* exhaustion */
    uint32_t big = pmm_alloc(st.free_kb / 4 + 1);
    CHECK(big == 0);
    uint32_t all = pmm_alloc(st.free_kb / 4);
    CHECK(all != 0);                                         /* contiguous remainder fits */
    pmm_get_stats(&st2);
    CHECK(st2.free_kb == 0);
    CHECK(pmm_alloc(1) == 0);
    pmm_free(all, st.free_kb / 4);
    pmm_get_stats(&st2);
    CHECK(st2.free_kb == st.free_kb);

    /* The mbi itself and the mmap must be reserved: their frames are below 1 MiB
     * here, so check the "never below 1 MiB" rule via alloc results. */
    for (int i = 0; i < 100; i++)
        CHECK(pmm_alloc(1) >= 0x100000);
}

static void test_heap(void)
{
    struct heap_stats hs;
    struct pmm_stats before, after;
    struct multiboot_info *mbi = make_mbi(0x4000000);        /* 64 MiB            */

    pmm_virt_offset = (uintptr_t)arena;
    CHECK(pmm_init(mbi, 0x180000));
    pmm_get_stats(&before);
    heap_init();

    void *p1 = kmalloc(1);
    void *p2 = kmalloc(100);
    void *p3 = kzalloc(5000);
    CHECK(p1 && p2 && p3);
    CHECK(((uintptr_t)p1 & 15) == 0 && ((uintptr_t)p2 & 15) == 0 && ((uintptr_t)p3 & 15) == 0);
    for (int i = 0; i < 5000; i++)
        CHECK(((uint8_t *)p3)[i] == 0);

    memset(p2, 0xAB, 100);
    p2 = krealloc(p2, 4000);
    CHECK(p2 != NULL);
    for (int i = 0; i < 100; i++)
        CHECK(((uint8_t *)p2)[i] == 0xAB);

    char *s = kstrdup("hello");
    CHECK(s && !strcmp(s, "hello"));

    heap_get_stats(&hs);
    CHECK(hs.allocs == 4 && hs.used_bytes >= 5000 + 4000);

    kfree(p1); kfree(p2); kfree(p3); kfree(s);
    kfree(NULL);
    kfree(p1);                                               /* double free ignored */
    heap_get_stats(&hs);
    CHECK(hs.allocs == 0 && hs.used_bytes == 0);

    /* owner accounting */
    heap_set_owner(3);
    void *o = kmalloc(1000);
    heap_set_owner(0);
    heap_get_stats(&hs);
    CHECK(hs.owner_bytes[3] >= 1000 && hs.owner_bytes[0] == 0);
    kfree(o);                                                /* freed under owner 0, still credited to 3 */
    heap_get_stats(&hs);
    CHECK(hs.owner_bytes[3] == 0);

    /* big allocation forces a dedicated chunk, and freeing it returns the RAM */
    heap_get_stats(&hs);
    uint32_t chunk_before = hs.chunk_bytes;
    void *big = kmalloc(3 * 1024 * 1024);
    CHECK(big != NULL);
    heap_get_stats(&hs);
    CHECK(hs.chunk_bytes >= chunk_before + 3 * 1024 * 1024);
    memset(big, 0x5A, 3 * 1024 * 1024);
    kfree(big);
    heap_get_stats(&hs);
    CHECK(hs.chunk_bytes == chunk_before);

    /* randomized stress with content verification */
    enum { N = 400 };
    struct { uint8_t *p; uint32_t n; uint8_t tag; } live[N];
    memset(live, 0, sizeof(live));
    srand(12345);
    for (int it = 0; it < 40000; it++) {
        int i = rand() % N;

        if (live[i].p) {
            for (uint32_t k = 0; k < live[i].n; k++) {
                if (live[i].p[k] != live[i].tag) {
                    CHECK(0 && "heap corruption");
                    break;
                }
            }
            kfree(live[i].p);
            live[i].p = NULL;
        } else {
            uint32_t n = (uint32_t)(rand() % 3000) + 1;

            if (rand() % 50 == 0)
                n = (uint32_t)(rand() % 300000) + 1;
            live[i].p = kmalloc(n);
            if (!live[i].p) { CHECK(0 && "stress alloc failed"); break; }
            live[i].n = n;
            live[i].tag = (uint8_t)(rand() & 0xFF);
            memset(live[i].p, live[i].tag, n);
        }
    }
    for (int i = 0; i < N; i++)
        kfree(live[i].p);
    heap_get_stats(&hs);
    CHECK(hs.allocs == 0 && hs.used_bytes == 0);
    CHECK(hs.chunk_bytes <= 2 * 1024 * 1024);                /* shrank back down  */

    /* Everything but at most one retained chunk is back in the PMM. */
    pmm_get_stats(&after);
    CHECK(before.free_kb - after.free_kb <= hs.chunk_bytes / 1024);
}

static void test_acct(void)
{
    uint8_t hist[ACCT_HISTORY];

    heap_init();
    acct_init();                                             /* last_sample = fake_us */
    int app = acct_register("Browser");
    CHECK(app == 1 && acct_alive(app) && !strcmp(acct_name(app), "Browser"));
    CHECK(acct_alive(0) && !strcmp(acct_name(0), "kernel_task"));

    /* One second: 600 ms idle, 250 ms in the app, 150 ms elsewhere (kernel). */
    uint32_t t0 = fake_us;
    acct_idle_begin();
    fake_us = t0 + 600000;
    acct_idle_end();
    acct_task_enter(app);
    CHECK(heap_get_owner() == (uint8_t)app);
    fake_us += 250000;
    acct_task_leave();
    CHECK(heap_get_owner() == 0);
    fake_us = t0 + 1000000;
    CHECK(acct_tick());
    CHECK(acct_cpu_permille() == 400);                       /* 1 - 0.6 idle      */
    CHECK(acct_task_permille(app) == 250);
    CHECK(acct_task_permille(0) == 150);

    /* Not a full second yet: no new sample. */
    acct_idle_begin();
    fake_us += 500000;
    CHECK(!acct_tick());

    /* A fully idle second (the idle stretch still in progress is counted). */
    fake_us += 500000;
    CHECK(acct_tick());
    CHECK(acct_cpu_permille() <= 10);
    CHECK(acct_task_permille(app) == 0);

    unsigned n = acct_history(hist);
    CHECK(n == 2 && hist[0] == 40 && hist[1] <= 1);

    /* nested enter/leave charges each task for its own span */
    int other = acct_register("Settings");
    acct_idle_end();
    uint32_t t1 = fake_us;
    acct_task_enter(app);
    fake_us += 100000;
    acct_task_enter(other);
    fake_us += 300000;
    acct_task_leave();
    fake_us += 100000;
    acct_task_leave();
    fake_us = t1 + 1000000;
    CHECK(acct_tick());
    CHECK(acct_task_permille(app) == 200 && acct_task_permille(other) == 300);

    /* history is a sliding window */
    for (int i = 0; i < 70; i++) {
        fake_us += 1000000;
        acct_tick();
    }
    CHECK(acct_history(hist) == ACCT_HISTORY);

    acct_unregister(app);
    CHECK(!acct_alive(app));
    CHECK(acct_register("again") == app);                    /* slot is reused    */
}

static void test_cpu(void)
{
    char buf[64];
    struct cpu_info ci;

    memset(&ci, 0, sizeof(ci));
    ci.has_fpu = ci.has_tsc = ci.has_mmx = ci.has_sse = 1;
    cpu_set_for_test(&ci);
    cpu_feature_string(buf, sizeof(buf));
    CHECK(!strcmp(buf, "FPU, TSC, MMX, SSE"));
    ci.has_fpu = ci.has_tsc = ci.has_mmx = ci.has_sse = 0;
    cpu_set_for_test(&ci);
    cpu_feature_string(buf, sizeof(buf));
    CHECK(!strcmp(buf, "none reported"));

    cpu_detect();                                            /* host build: no CPUID */
    CHECK(cpu_get()->family == 4 && cpu_get()->mhz == 0);
}

static void test_rtc(void)
{
    struct rtc_time t;

    /* BCD, 24-hour: 2026-10-21 09:41:07 (a Wednesday) */
    memset(fake_cmos, 0, sizeof(fake_cmos));
    fake_cmos[0x00] = 0x07; fake_cmos[0x02] = 0x41; fake_cmos[0x04] = 0x09;
    fake_cmos[0x07] = 0x21; fake_cmos[0x08] = 0x10; fake_cmos[0x09] = 0x26;
    fake_cmos[0x32] = 0x20; fake_cmos[0x0B] = 0x02;
    CHECK(rtc_read(&t));
    CHECK(t.year == 2026 && t.mon == 10 && t.mday == 21);
    CHECK(t.hour == 9 && t.min == 41 && t.sec == 7 && t.wday == 3);

    /* binary, 24-hour */
    fake_cmos[0x00] = 59; fake_cmos[0x02] = 59; fake_cmos[0x04] = 23;
    fake_cmos[0x07] = 31; fake_cmos[0x08] = 12; fake_cmos[0x09] = 99;
    fake_cmos[0x32] = 19; fake_cmos[0x0B] = 0x06;
    CHECK(rtc_read(&t));
    CHECK(t.year == 1999 && t.mon == 12 && t.mday == 31 && t.hour == 23 && t.wday == 5);

    /* BCD, 12-hour with PM bit: 11:30 PM -> 23:30, and 12:05 AM -> 00:05 */
    fake_cmos[0x04] = 0x91; fake_cmos[0x02] = 0x30; fake_cmos[0x0B] = 0x00;
    fake_cmos[0x09] = 0x26; fake_cmos[0x32] = 0x20; fake_cmos[0x08] = 0x01; fake_cmos[0x07] = 0x01;
    fake_cmos[0x00] = 0x00;
    CHECK(rtc_read(&t) && t.hour == 23 && t.min == 30);
    fake_cmos[0x04] = 0x12; fake_cmos[0x02] = 0x05;
    CHECK(rtc_read(&t) && t.hour == 0 && t.min == 5);
    fake_cmos[0x04] = 0x92;                                  /* 12 PM = noon */
    CHECK(rtc_read(&t) && t.hour == 12);

    /* a dead chip reads as all 0xFF */
    memset(fake_cmos, 0xFF, sizeof(fake_cmos));
    CHECK(!rtc_read(&t));

    CHECK(rtc_day_of_week(2000, 1, 1) == 6);                 /* Saturday */
    CHECK(rtc_day_of_week(2024, 2, 29) == 4);                /* Thursday */
    CHECK(rtc_day_of_week(1970, 1, 1) == 4);
}

int main(void)
{
    arena = aligned_alloc(4096, ARENA_SIZE);
    if (!arena) { puts("cannot allocate arena"); return 2; }
    memset(arena, 0, ARENA_SIZE);

    test_pmm();
    test_heap();
    test_acct();
    test_cpu();
    test_rtc();

    printf("%d system checks, %d failures\n", checks, failures);
    free(arena);
    return failures ? 1 : 0;
}
