/* =============================================================================
 *  AGOS - Kernel panic (panic.c)
 *  Must stay self-contained: no allocation, no interrupts, no dependence on
 *  any state that the fault could have corrupted beyond the display itself.
 * ========================================================================== */
#include "panic.h"
#include "gfx.h"
#include "io.h"
#include "lib.h"
#include "serial.h"
#include "vgatext.h"

static const char *const exception_names[32] = {
    "Divide error (#DE)",           "Debug (#DB)",
    "Non-maskable interrupt",       "Breakpoint (#BP)",
    "Overflow (#OF)",               "Bound range exceeded (#BR)",
    "Invalid opcode (#UD)",         "Device not available (#NM)",
    "Double fault (#DF)",           "Coprocessor segment overrun",
    "Invalid TSS (#TS)",            "Segment not present (#NP)",
    "Stack-segment fault (#SS)",    "General protection fault (#GP)",
    "Page fault (#PF)",             "Reserved",
    "x87 floating point (#MF)",     "Alignment check (#AC)",
    "Machine check (#MC)",          "SIMD floating point (#XM)",
    "Virtualization (#VE)",         "Control protection (#CP)",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection (#HV)",   "VMM communication (#VC)",
    "Security exception (#SX)",     "Reserved",
};

static uint32_t read_cr2(void)
{
    uint32_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

/* "<label>0x12345678" into out (cap bytes). */
static void fmt_reg(char *out, size_t cap, const char *label, uint32_t value)
{
    char hex[34];

    out[0] = '\0';
    kstrlcat(out, label, cap);
    kutoa(hex, value, 16, 8);
    kstrlcat(out, "0x", cap);
    kstrlcat(out, hex, cap);
}

/* Show up to `n` lines on the best available display, then park the CPU. */
static __attribute__((noreturn)) void show_and_halt(const char *title,
                                                    const char *lines[], int n)
{
    if (gfx_ready()) {
        int y = 48;

        gfx_clear(0x1C1C1E);
        gfx_fill_rect(0, 0, gfx_width(), 8, 0xFF453A);
        gfx_draw_string(32, y, title, 0xFF453A, 3);
        y += 24 + 24;
        for (int i = 0; i < n; i++, y += 24)
            gfx_draw_string(32, y, lines[i], 0xEBEBF0, 2);
        gfx_draw_string(32, y + 16, "System halted. Please reset the machine.", 0x98989D, 2);
        gfx_present();
    } else {
        vgatext_clear(VGATEXT_ATTR(15, 4));            /* white on red        */
        vgatext_puts(2, 1, title, VGATEXT_ATTR(14, 4));
        for (int i = 0; i < n; i++)
            vgatext_puts(2, 3 + i, lines[i], VGATEXT_ATTR(15, 4));
        vgatext_puts(2, 4 + n, "System halted. Please reset the machine.", VGATEXT_ATTR(15, 4));
    }
    cpu_halt_forever();
}

void kernel_panic(const char *message)
{
    cpu_cli();
    serial_puts("\n*** AGOS KERNEL PANIC: ");
    serial_puts(message);
    serial_puts("\n");

    const char *lines[1] = { message };
    show_and_halt("AGOS kernel panic", lines, 1);
}

void kernel_panic_regs(const struct regs *r)
{
    static char buf[10][48];            /* static: the stack may be unusable  */
    const char *lines[10];
    int n = 0;
    uint32_t vec = r->int_no;

    cpu_cli();

    buf[n][0] = '\0';
    kstrlcat(buf[n], vec < 32 ? exception_names[vec] : "Unexpected interrupt", sizeof(buf[n]));
    lines[n] = buf[n]; n++;

    fmt_reg(buf[n], sizeof(buf[n]), "Vector   ", vec);          lines[n] = buf[n]; n++;
    fmt_reg(buf[n], sizeof(buf[n]), "Error cd ", r->err_code);  lines[n] = buf[n]; n++;
    fmt_reg(buf[n], sizeof(buf[n]), "EIP      ", r->eip);       lines[n] = buf[n]; n++;
    fmt_reg(buf[n], sizeof(buf[n]), "CS       ", r->cs);        lines[n] = buf[n]; n++;
    fmt_reg(buf[n], sizeof(buf[n]), "EFLAGS   ", r->eflags);    lines[n] = buf[n]; n++;
    fmt_reg(buf[n], sizeof(buf[n]), "EAX      ", r->eax);       lines[n] = buf[n]; n++;
    fmt_reg(buf[n], sizeof(buf[n]), "ESP      ", r->esp_dummy); lines[n] = buf[n]; n++;
    if (vec == 14) {                    /* page fault: faulting address in CR2 */
        fmt_reg(buf[n], sizeof(buf[n]), "CR2      ", read_cr2());
        lines[n] = buf[n]; n++;
    }

    serial_puts("\n*** AGOS KERNEL PANIC: ");
    for (int i = 0; i < n; i++) {
        serial_puts("\n  ");
        serial_puts(lines[i]);
    }
    serial_puts("\n");

    show_and_halt("AGOS kernel panic", lines, n);
}
