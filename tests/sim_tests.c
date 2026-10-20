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
#include "pic.h"
#include "pit.h"

/* --- Fake I/O model --------------------------------------------------------- */
struct io_write io_log[IO_LOG_MAX];
int             io_log_n;
uint8_t         fake_isr_master, fake_isr_slave;
uint8_t         fake_kbd_bytes[256];
int             fake_kbd_count, fake_kbd_pos;
static uint8_t  mask_master = 0xFF, mask_slave = 0xFF;   /* last value on 0x21/0xA1 */

void outb(uint16_t port, uint8_t value)
{
    if (port == 0x80)
        return;
    if (io_log_n < IO_LOG_MAX)
        io_log[io_log_n++] = (struct io_write){ port, value };
    if (port == 0x21) mask_master = value;
    if (port == 0xA1) mask_slave  = value;
}

uint8_t inb(uint16_t port)
{
    switch (port) {
    case 0x60: return fake_kbd_pos < fake_kbd_count ? fake_kbd_bytes[fake_kbd_pos++] : 0;
    case 0x64: return fake_kbd_pos < fake_kbd_count ? 0x01 : 0x00;
    case 0x21: return mask_master;
    case 0xA1: return mask_slave;
    case 0x20: return fake_isr_master;
    case 0xA0: return fake_isr_slave;
    default:   return 0;
    }
}

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

static void kbd_push(uint8_t b) { fake_kbd_bytes[fake_kbd_count++] = b; }

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
    CHECK(mask_slave == 0xFD);          /* bit 1 cleared                        */
    CHECK(!(mask_master & 0x04));       /* cascade open                         */
    pic_set_mask(9);
    CHECK(mask_slave == 0xFF);
}

static void test_pit(void)
{
    log_reset();
    pic_remap(0x20, 0x28);
    log_reset();
    pit_init(100);

    /* channel 0, lobyte/hibyte, mode 3, binary; divisor 11932 = 0x2E9C */
    CHECK(io_log[0].port == 0x43 && io_log[0].val == 0x36);
    CHECK(io_log[1].port == 0x40 && io_log[1].val == 0x9C);
    CHECK(io_log[2].port == 0x40 && io_log[2].val == 0x2E);
    CHECK(!(mask_master & 0x01));       /* IRQ0 unmasked                        */
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
    kbd_push(0x55);                     /* stale byte left by firmware          */
    kbd_init();
    CHECK(fake_kbd_pos == fake_kbd_count);          /* init drained it          */
    CHECK(!(mask_master & 0x02));                   /* IRQ1 unmasked            */
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

int main(void)
{
    test_pic_remap();
    test_pit();
    test_keyboard();
    test_eoi_and_spurious();
    test_exceptions();

    printf("%d simulated-hardware checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
