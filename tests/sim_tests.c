/* =============================================================================
 *  AGOS - hardware-simulation tests (tests/sim_tests.c)
 *  Runs the REAL isr.c, pic.c, pit.c, keyboard.c and kbd_decode.c on the build
 *  host against a software model of the I/O ports (tests/fake_io.h). It cannot
 *  prove the code works on silicon, but it does verify the exact register
 *  programming sequences, EOI rules, spurious-IRQ handling and the
 *  scancode -> event path that no CPU-less unit test would reach.
 * ========================================================================== */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "isr.h"
#include "keyboard.h"
#include "mouse.h"
#include "pic.h"
#include "pit.h"
#include "ps2.h"

/* --- Stub for the panic path (panic.c needs graphics; not under test) --------
 * The real function is noreturn, so the stub must not return either: it jumps
 * back into the test via longjmp, just as the CPU would never come back. */
static int panic_calls;
static uint32_t panic_vector;
static jmp_buf panic_env;
void kernel_panic_regs(const struct regs *r)
{
    panic_calls++;
    panic_vector = r->int_no;
    longjmp(panic_env, 1);
}

/* Dispatch where a panic is an acceptable outcome. */
static void dispatch_may_panic(struct regs *r)
{
    if (setjmp(panic_env) == 0)
        isr_dispatch(r);
}

/* --- Harness ----------------------------------------------------------------- */
static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void log_reset(void) { io_log_n = 0; }

static int log_has(uint16_t port, uint8_t val)
{
    for (int i = 0; i < io_log_n; i++)
        if (io_log[i].port == port && io_log[i].val == val)
            return 1;
    return 0;
}

static int log_index(uint16_t port, uint8_t val)
{
    for (int i = 0; i < io_log_n; i++)
        if (io_log[i].port == port && io_log[i].val == val)
            return i;
    return -1;
}

static void irq(unsigned n)
{
    struct regs r;
    memset(&r, 0, sizeof(r));
    r.int_no = IRQ_VECTOR(n);
    isr_dispatch(&r);
}

static void kbd_push(uint8_t b) { fake_kbd_bytes[fake_kbd_count] = b; fake_kbd_aux[fake_kbd_count++] = 0; }
static void aux_push(uint8_t b) { fake_kbd_bytes[fake_kbd_count] = b; fake_kbd_aux[fake_kbd_count++] = 1; }

/* The real ps2.c installs this for IRQ1/IRQ12; ps2_init() itself needs a device. */
static void ps2_irq_for_test(struct regs *r) { (void)r; ps2_service(); }

/* --- Tests -------------------------------------------------------------------- */
static void test_pic_remap(void)
{
    static const struct io_write expect[] = {
        {0x20, 0x11}, {0xA0, 0x11},     /* ICW1: init + ICW4 needed            */
        {0x21, 0x20}, {0xA1, 0x28},     /* ICW2: vector offsets 0x20 / 0x28    */
        {0x21, 0x04}, {0xA1, 0x02},     /* ICW3: slave on IRQ2 / cascade id 2  */
        {0x21, 0x01}, {0xA1, 0x01},     /* ICW4: 8086 mode                     */
        {0x21, 0xFF}, {0xA1, 0xFF},     /* everything masked                   */
    };

    log_reset();
    pic_remap(0x20, 0x28);
    CHECK(io_log_n == (int)(sizeof(expect) / sizeof(expect[0])));
    for (int i = 0; i < io_log_n && i < (int)(sizeof(expect) / sizeof(expect[0])); i++)
        CHECK(io_log[i].port == expect[i].port && io_log[i].val == expect[i].val);

    /* unmasking a slave IRQ must also open the cascade line (IRQ2) */
    pic_clear_mask(9);
    CHECK(fake_mask_slave == 0xFD);          /* bit 1 cleared                        */
    CHECK(!(fake_mask_master & 0x04));       /* cascade open                         */
    pic_set_mask(9);
    CHECK(fake_mask_slave == 0xFF);
}

static void test_pit(void)
{
    log_reset();
    pic_remap(0x20, 0x28);
    log_reset();
    pit_init(100);

    /* channel 0, lobyte/hibyte, mode 2 (rate generator), binary; divisor 11932 = 0x2E9C */
    CHECK(io_log[0].port == 0x43 && io_log[0].val == 0x34);
    CHECK(io_log[1].port == 0x40 && io_log[1].val == 0x9C);
    CHECK(io_log[2].port == 0x40 && io_log[2].val == 0x2E);
    CHECK(!(fake_mask_master & 0x01));       /* IRQ0 unmasked                        */
    CHECK(pit_hz() == 100 && pit_ticks() == 0 && pit_uptime_ms() == 0);

    log_reset();
    irq(0);
    CHECK(pit_ticks() == 1);
    CHECK(io_log_n == 1 && io_log[0].port == 0x20 && io_log[0].val == 0x20);  /* EOI */

    for (int i = 0; i < 99; i++)
        irq(0);
    CHECK(pit_ticks() == 100);
    CHECK(pit_uptime_ms() == 1000);     /* 100 ticks at 100 Hz = exactly 1 s    */
    for (int i = 0; i < 50; i++)
        irq(0);
    CHECK(pit_uptime_ms() == 1500);
}

static void test_keyboard(void)
{
    struct key_event ev;

    pic_remap(0x20, 0x28);
    fake_kbd_count = fake_kbd_pos = 0;
    kbd_init();
    isr_register_handler(IRQ_VECTOR(1), ps2_irq_for_test);
    pic_clear_mask(1);
    CHECK(!(fake_mask_master & 0x02));              /* IRQ1 unmasked            */
    CHECK(kbd_poll(&ev) == 0);

    /* press and release 'a' */
    kbd_push(0x1E); kbd_push(0x9E);
    log_reset();
    irq(1);
    CHECK(io_log_n == 1 && io_log[0].port == 0x20 && io_log[0].val == 0x20);  /* EOI */
    irq(1);
    CHECK(kbd_poll(&ev) && ev.keycode == 'a' && ev.pressed);
    CHECK(kbd_poll(&ev) && ev.keycode == 'a' && !ev.pressed);
    CHECK(kbd_poll(&ev) == 0);

    /* Shift+H e l l o: modifiers produce no event but change the next one */
    fake_kbd_count = fake_kbd_pos = 0;
    kbd_push(0x2A); kbd_push(0x23); kbd_push(0xAA); kbd_push(0x12);
    for (int i = 0; i < 4; i++)
        irq(1);
    CHECK(kbd_poll(&ev) && ev.keycode == 'H');
    CHECK(kbd_poll(&ev) && ev.keycode == 'e');
    CHECK(kbd_poll(&ev) == 0);

    /* queue overflow: 63 usable slots, the rest are counted as dropped */
    fake_kbd_count = fake_kbd_pos = 0;
    for (int i = 0; i < 70; i++)
        kbd_push(0x1E);
    for (int i = 0; i < 70; i++)
        irq(1);
    int got = 0;
    while (kbd_poll(&ev))
        got++;
    CHECK(got == 63);
    CHECK(kbd_dropped() == 7);
}

static void test_eoi_and_spurious(void)
{
    static int called;
    void h(struct regs *r) { (void)r; called++; }

    /* slave IRQ 14: EOI to slave first, then master */
    isr_register_handler(IRQ_VECTOR(14), h);
    log_reset();
    called = 0;
    irq(14);
    CHECK(called == 1);
    CHECK(io_log_n == 2 && io_log[0].port == 0xA0 && io_log[1].port == 0x20);

    /* spurious IRQ7: ISR bit 7 clear -> no handler, NO EOI at all */
    isr_register_handler(IRQ_VECTOR(7), h);
    fake_isr_master = 0x00;
    log_reset();
    called = 0;
    irq(7);
    CHECK(called == 0);
    CHECK(!log_has(0x20, 0x20) && !log_has(0xA0, 0x20));

    /* real IRQ7: ISR bit 7 set -> handler runs and is acknowledged */
    fake_isr_master = 0x80;
    log_reset();
    irq(7);
    CHECK(called == 1);
    CHECK(log_has(0x20, 0x20));
    fake_isr_master = 0x00;

    /* spurious IRQ15: master (cascade) gets EOI, slave must NOT */
    isr_register_handler(IRQ_VECTOR(15), h);
    fake_isr_slave = 0x00;
    log_reset();
    called = 0;
    irq(15);
    CHECK(called == 0);
    CHECK(log_has(0x20, 0x20) && !log_has(0xA0, 0x20));

    /* real IRQ15: handler runs, EOI to slave then master */
    fake_isr_slave = 0x80;
    log_reset();
    irq(15);
    CHECK(called == 1);
    CHECK(log_index(0xA0, 0x20) >= 0 && log_index(0xA0, 0x20) < log_index(0x20, 0x20));
    fake_isr_slave = 0x00;

    /* handler removal */
    isr_register_handler(IRQ_VECTOR(14), NULL);
    log_reset();
    called = 0;
    irq(14);
    CHECK(called == 0 && log_has(0xA0, 0x20));  /* still acknowledged          */
}

static void test_exceptions(void)
{
    static int hits;
    static uint32_t seen_eip;
    void h(struct regs *r) { hits++; seen_eip = r->eip; }

    struct regs r;
    memset(&r, 0, sizeof(r));

    isr_register_handler(3, h);                 /* #BP handled by a handler     */
    r.int_no = 3;
    r.eip = 0xC0FFEE;
    panic_calls = 0;
    isr_dispatch(&r);
    CHECK(hits == 1 && seen_eip == 0xC0FFEE && panic_calls == 0);

    r.int_no = 13;                              /* unhandled #GP -> panic       */
    dispatch_may_panic(&r);
    CHECK(panic_calls == 1 && panic_vector == 13);

    r.int_no = 0;                               /* unhandled #DE -> panic       */
    dispatch_may_panic(&r);
    CHECK(panic_calls == 2 && panic_vector == 0);

    isr_register_handler(3, NULL);
    r.int_no = 3;
    dispatch_may_panic(&r);
    CHECK(panic_calls == 3);

    /* exceptions must never touch the PIC (and must not fall through into the
     * IRQ path: that is what this catches) */
    log_reset();
    r.int_no = 14;
    dispatch_may_panic(&r);
    CHECK(panic_calls == 4 && panic_vector == 14);
    CHECK(io_log_n == 0);

    /* out-of-range registration is ignored, not a buffer overflow */
    isr_register_handler(200, h);
    isr_register_handler(48, h);
}


static void test_mouse_and_routing(void)
{
    struct mouse_event m;
    struct key_event k;

    pic_remap(0x20, 0x28);
    kbd_init();
    mouse_setup(0);
    isr_register_handler(IRQ_VECTOR(1), ps2_irq_for_test);
    isr_register_handler(IRQ_VECTOR(12), ps2_irq_for_test);
    pic_clear_mask(12);
    CHECK(!(fake_mask_slave & 0x10));                /* IRQ12 = slave bit 4      */
    CHECK(!(fake_mask_master & 0x04));               /* cascade opened           */

    /* A key press arrives in the middle of a mouse packet: the controller's
     * AUX status bit routes every byte to the right decoder. */
    fake_kbd_count = fake_kbd_pos = 0;
    aux_push(0x29);                                  /* sync | L button | Y negative */
    kbd_push(0x1E);                                  /* 'a' make code            */
    aux_push(5);                                     /* dx = +5                  */
    aux_push(0xFE);                                  /* dy = -2 (PS/2 Y is up)   */
    log_reset();
    irq(12);                                         /* one IRQ drains the buffer */
    CHECK(fake_kbd_pos == fake_kbd_count);
    CHECK(kbd_poll(&k) && k.keycode == 'a' && k.pressed);
    CHECK(mouse_poll(&m) && m.dx == 5 && m.dy == 2 && m.buttons == 1);   /* Y flipped */
    CHECK(mouse_poll(&m) == 0);
    CHECK(log_has(0xA0, 0x20) && log_has(0x20, 0x20));   /* slave + master EOI    */

    /* wheel mouse: 4-byte packets */
    mouse_setup(3);
    fake_kbd_count = fake_kbd_pos = 0;
    aux_push(0x08); aux_push(0); aux_push(0); aux_push(0xFF);   /* wheel -1      */
    irq(12);
    CHECK(mouse_poll(&m) && m.wheel == -1 && m.dx == 0);
}

static void test_clock(void)
{
    uint32_t a, b;

    pic_remap(0x20, 0x28);
    pit_init(100);                                   /* divisor 11932             */
    fake_pit_count = 11932;                          /* counter just reloaded     */
    a = pit_clock_us();
    fake_pit_count = 11932 / 2;                      /* halfway through the tick  */
    b = pit_clock_us();
    CHECK(b - a > 4900 && b - a < 5100);             /* ~5 ms                     */

    irq(0);                                          /* next tick                 */
    fake_pit_count = 11932;
    a = pit_clock_us();
    CHECK(a - b > 4900 && a - b < 5200);             /* half tick + epsilon       */

    /* counter wrapped but the IRQ has not run yet: the clock must not go back */
    fake_pit_count = 100;
    b = pit_clock_us();
    fake_pit_count = 11900;                          /* wrapped around            */
    a = pit_clock_us();
    CHECK((int32_t)(a - b) >= 0);
}

int main(void)
{
    test_pic_remap();
    test_pit();
    test_keyboard();
    test_mouse_and_routing();
    test_clock();
    test_eoi_and_spurious();
    test_exceptions();

    printf("%d simulated-hardware checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
