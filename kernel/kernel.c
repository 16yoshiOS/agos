/* =============================================================================
 *  AGOS - Kernel entry point (kernel.c)
 * -----------------------------------------------------------------------------
 *  Step 2: the CPU-side foundation. Boot sequence:
 *
 *    boot/start.asm -> kernel_main(magic, mbi)
 *      1. serial console up (kernel log)
 *      2. verify Multiboot handoff
 *      3. GDT + TSS      own segment descriptors, replace GRUB's
 *      4. IDT            48 gates: 32 exceptions + 16 IRQs
 *      5. PIC remap      IRQs 0..15 -> vectors 32..47
 *      6. PIT @ 100 Hz   IRQ0 system tick
 *      7. PS/2 keyboard  IRQ1 -> key events
 *      8. STI            interrupts on
 *      9. framebuffer    GRUB's linear mode (or VGA text fallback)
 *     10. demo screen    "Welcome to AGOS" + live uptime/RAM/key status
 *
 *  Keys in the demo: type to echo text, Backspace edits, Tab toggles the
 *  Light/Dark theme.
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

#include "font.h"
#include "gdt.h"
#include "gfx.h"
#include "idt.h"
#include "io.h"
#include "isr.h"
#include "keyboard.h"
#include "lib.h"
#include "multiboot.h"
#include "panic.h"
#include "pic.h"
#include "pit.h"
#include "serial.h"
#include "vgatext.h"

/* ---------------------------------------------------------------------------
 *  Themes (macOS-inspired Light / Dark palettes, 0xRRGGBB)
 * ------------------------------------------------------------------------ */
struct theme {
    uint32_t desktop, menubar, text, subtext, card, border, accent;
};

static const struct theme themes[2] = {
    /* Light */ { 0xE8E8E8, 0xF6F6F6, 0x262629, 0x6E6E73, 0xFFFFFF, 0xCFCFD4, 0x007AFF },
    /* Dark  */ { 0x1E1E20, 0x2C2C2E, 0xEBEBF0, 0x98989D, 0x2C2C2E, 0x48484A, 0x0A84FF },
};

/* ---------------------------------------------------------------------------
 *  Demo state
 * ------------------------------------------------------------------------ */
#define TYPED_MAX 28

static struct {
    int      dark;                      /* 0 = light, 1 = dark                */
    char     typed[TYPED_MAX + 1];
    size_t   typed_len;
    char     last_key[24];
    uint32_t total_ram_kb;
    int      fb_w, fb_h;
} ui;

/* ---------------------------------------------------------------------------
 *  Small formatting helpers (no printf in a freestanding kernel)
 * ------------------------------------------------------------------------ */

/* "<label><decimal>" -> out */
static void fmt_label_dec(char *out, size_t cap, const char *label, uint32_t v)
{
    char num[34];

    out[0] = '\0';
    kstrlcat(out, label, cap);
    kutoa(num, v, 10, 1);
    kstrlcat(out, num, cap);
}

/* Uptime as HH:MM:SS. */
static void fmt_uptime(char *out, size_t cap, uint32_t seconds)
{
    char part[34];

    out[0] = '\0';
    kutoa(part, seconds / 3600, 10, 2);        kstrlcat(out, part, cap);
    kstrlcat(out, ":", cap);
    kutoa(part, (seconds / 60) % 60, 10, 2);   kstrlcat(out, part, cap);
    kstrlcat(out, ":", cap);
    kutoa(part, seconds % 60, 10, 2);          kstrlcat(out, part, cap);
}

/* ---------------------------------------------------------------------------
 *  Memory size from the Multiboot memory map (sum of "available" regions).
 *  Falls back to mem_upper. Returns KiB; saturates instead of overflowing.
 * ------------------------------------------------------------------------ */
static uint32_t usable_ram_kb(const struct multiboot_info *mbi)
{
    uint32_t kb = 0;

    if (mbi->flags & MBI_FLAG_MMAP) {
        uint32_t p   = mbi->mmap_addr;
        uint32_t end = mbi->mmap_addr + mbi->mmap_length;

        while (p < end) {
            const struct multiboot_mmap_entry *e = (const struct multiboot_mmap_entry *)(uintptr_t)p;

            if (e->type == MB_MMAP_AVAILABLE) {
                /* A region of 4 GiB or more does not fit 32-bit KiB math: saturate. */
                uint32_t region_kb = e->len_hi ? 0xFFFFFFFFu : (e->len_lo >> 10);
                kb = (kb > 0xFFFFFFFFu - region_kb) ? 0xFFFFFFFFu : kb + region_kb;
            }
            p += e->size + 4;
        }
        if (kb)
            return kb;
    }
    if (mbi->flags & MBI_FLAG_MEM)
        return mbi->mem_lower + mbi->mem_upper;
    return 0;
}

/* ---------------------------------------------------------------------------
 *  Key handling
 * ------------------------------------------------------------------------ */
static void describe_key(const struct key_event *ev)
{
    char hex[34];

    ui.last_key[0] = '\0';
    if (ev->keycode >= 0x20 && ev->keycode < 0x7F) {
        char c[2] = { (char)ev->keycode, '\0' };
        kstrlcat(ui.last_key, "'", sizeof(ui.last_key));
        kstrlcat(ui.last_key, c, sizeof(ui.last_key));
        kstrlcat(ui.last_key, "' ", sizeof(ui.last_key));
    } else if (ev->keycode == '\n') {
        kstrlcat(ui.last_key, "Enter ", sizeof(ui.last_key));
    } else if (ev->keycode == '\b') {
        kstrlcat(ui.last_key, "Backspace ", sizeof(ui.last_key));
    } else if (ev->keycode == '\t') {
        kstrlcat(ui.last_key, "Tab ", sizeof(ui.last_key));
    } else if (ev->keycode == 27) {
        kstrlcat(ui.last_key, "Esc ", sizeof(ui.last_key));
    }
    kstrlcat(ui.last_key, "sc=", sizeof(ui.last_key));
    kutoa(hex, ev->scancode, 16, 2);
    kstrlcat(ui.last_key, hex, sizeof(ui.last_key));
}

/* Returns 1 if the screen needs a redraw. */
static int handle_key(const struct key_event *ev)
{
    if (!ev->pressed)
        return 0;

    describe_key(ev);

    if (ev->keycode == '\t') {
        ui.dark ^= 1;                               /* theme switch demo      */
    } else if (ev->keycode == '\b') {
        if (ui.typed_len)
            ui.typed[--ui.typed_len] = '\0';
    } else if (ev->keycode >= 0x20 && ev->keycode < 0x7F) {
        if (ui.typed_len < TYPED_MAX) {
            ui.typed[ui.typed_len++] = (char)ev->keycode;
            ui.typed[ui.typed_len] = '\0';
        }
    }

    serial_puts("key: ");
    serial_puts(ui.last_key);
    serial_puts("\n");
    return 1;
}

/* ---------------------------------------------------------------------------
 *  Rendering
 * ------------------------------------------------------------------------ */
static void build_status_lines(char lines[6][48])
{
    uint32_t up_s = pit_uptime_ms() / 1000;
    char     tmp[34];

    lines[0][0] = '\0';
    kstrlcat(lines[0], "Uptime  : ", 48);
    fmt_uptime(tmp, sizeof(tmp), up_s);
    kstrlcat(lines[0], tmp, 48);

    fmt_label_dec(lines[1], 48, "Ticks   : ", pit_ticks());

    lines[2][0] = '\0';
    kstrlcat(lines[2], "RAM     : ", 48);
    kutoa(tmp, (ui.total_ram_kb + 512) / 1024, 10, 1);
    kstrlcat(lines[2], tmp, 48);
    kstrlcat(lines[2], " MB usable", 48);

    lines[3][0] = '\0';
    if (gfx_ready()) {
        kstrlcat(lines[3], "Display : ", 48);
        kutoa(tmp, (uint32_t)ui.fb_w, 10, 1);  kstrlcat(lines[3], tmp, 48);
        kstrlcat(lines[3], "x", 48);
        kutoa(tmp, (uint32_t)ui.fb_h, 10, 1);  kstrlcat(lines[3], tmp, 48);
        kstrlcat(lines[3], "x32", 48);
    } else {
        kstrlcat(lines[3], "Display : VGA text (no framebuffer)", 48);
    }

    lines[4][0] = '\0';
    kstrlcat(lines[4], "Last key: ", 48);
    kstrlcat(lines[4], ui.last_key[0] ? ui.last_key : "-", 48);

    lines[5][0] = '\0';
    kstrlcat(lines[5], "Typed   : ", 48);
    kstrlcat(lines[5], ui.typed, 48);
    kstrlcat(lines[5], "_", 48);
}

static void render_graphics(void)
{
    const struct theme *t = &themes[ui.dark];
    char lines[6][48];
    char clock[16];
    const int W = gfx_width(), H = gfx_height();

    gfx_clear(t->desktop);

    /* Menu bar */
    gfx_fill_rect(0, 0, W, 28, t->menubar);
    gfx_fill_rect(0, 28, W, 1, t->border);
    gfx_draw_string(16, 10, "AGOS", t->text, 1);
    gfx_draw_string(16 + 5 * FONT_W + 8, 10, ui.dark ? "Dark" : "Light", t->subtext, 1);
    fmt_uptime(clock, sizeof(clock), pit_uptime_ms() / 1000);
    gfx_draw_string(W - 16 - gfx_string_width(clock, 1), 10, clock, t->text, 1);

    /* Title */
    static const char title[]    = "Welcome to AGOS";
    static const char subtitle[] = "Step 2: GDT, IDT, PIC, PIT and keyboard are online";
    int title_scale = (W >= 640) ? 4 : 2;
    int ty = H / 2 - 150;
    if (ty < 48)
        ty = 48;
    gfx_draw_string((W - gfx_string_width(title, title_scale)) / 2, ty, title, t->text, title_scale);

    int sub_scale = (gfx_string_width(subtitle, 2) <= W - 32) ? 2 : 1;
    gfx_draw_string((W - gfx_string_width(subtitle, sub_scale)) / 2,
                    ty + FONT_H * title_scale + 16, subtitle, t->subtext, sub_scale);

    /* Status card */
    int card_w = W - 64 < 640 ? W - 64 : 640;
    int card_h = 6 * 24 + 32;
    int cx = (W - card_w) / 2;
    int cy = ty + FONT_H * title_scale + 72;

    build_status_lines(lines);
    gfx_fill_rect(cx, cy, card_w, card_h, t->card);
    gfx_stroke_rect(cx, cy, card_w, card_h, t->border);
    gfx_fill_rect(cx, cy, 4, card_h, t->accent);
    for (int i = 0; i < 6; i++)
        gfx_draw_string(cx + 24, cy + 16 + i * 24, lines[i], t->text, 2);

    gfx_draw_string((W - gfx_string_width("Tab: switch theme   Type: echo text", 1)) / 2,
                    cy + card_h + 16, "Tab: switch theme   Type: echo text", t->subtext, 1);

    gfx_present();
}

static void render_text(void)
{
    char lines[6][48];
    const uint8_t attr = VGATEXT_ATTR(0, 7);            /* black on light gray */

    vgatext_clear(attr);
    vgatext_puts(2, 2, "Welcome to AGOS", VGATEXT_ATTR(1, 7));
    vgatext_puts(2, 3, "Step 2: GDT, IDT, PIC, PIT and keyboard are online", attr);

    build_status_lines(lines);
    for (int i = 0; i < 6; i++)
        vgatext_puts(2, 6 + i, lines[i], attr);
}

static void render(void)
{
    if (gfx_ready())
        render_graphics();
    else
        render_text();
}

/* ---------------------------------------------------------------------------
 *  Kernel entry point (called from boot/start.asm)
 * ------------------------------------------------------------------------ */
__attribute__((noreturn))
void kernel_main(uint32_t magic, const struct multiboot_info *mbi)
{
    serial_init();
    serial_puts("\nAGOS Step 2 booting\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        /* Not started by a Multiboot loader: mbi is meaningless. */
        serial_puts("fatal: not booted via Multiboot, magic=");
        serial_put_hex(magic);
        serial_puts("\n");
        vgatext_clear(VGATEXT_ATTR(15, 4));
        vgatext_puts(2, 2, "AGOS: boot with a Multiboot loader (GRUB).", VGATEXT_ATTR(15, 4));
        cpu_halt_forever();
    }

    if (mbi->flags & MBI_FLAG_LOADER) {
        serial_puts("loader: ");
        serial_puts((const char *)(uintptr_t)mbi->boot_loader_name);
        serial_puts("\n");
    }

    gdt_init();         serial_puts("gdt: ok\n");
    idt_init();         serial_puts("idt: ok\n");
    pic_remap(IRQ_BASE, IRQ_BASE + 8);
                        serial_puts("pic: remapped to 0x20/0x28\n");
    pit_init(PIT_TICK_HZ);
                        serial_puts("pit: ");
                        serial_put_dec(pit_hz());
                        serial_puts(" Hz\n");
    kbd_init();         serial_puts("kbd: ok\n");

    ui.total_ram_kb = usable_ram_kb(mbi);
    serial_puts("ram: ");
    serial_put_dec(ui.total_ram_kb / 1024);
    serial_puts(" MB usable\n");

    cpu_sti();          serial_puts("interrupts: on\n");

    if (gfx_init(mbi)) {
        ui.fb_w = gfx_width();
        ui.fb_h = gfx_height();
        serial_puts("gfx: ");
        serial_put_dec((uint32_t)ui.fb_w);
        serial_puts("x");
        serial_put_dec((uint32_t)ui.fb_h);
        serial_puts("x32 linear framebuffer\n");
    } else {
        serial_puts("gfx: no 32-bpp framebuffer, using VGA text fallback\n");
    }

    render();

    uint32_t last_sec = pit_uptime_ms() / 1000;
    for (;;) {
        int dirty = 0;
        struct key_event ev;

        cpu_hlt();                                  /* sleep until an IRQ     */

        while (kbd_poll(&ev))
            dirty |= handle_key(&ev);

        uint32_t sec = pit_uptime_ms() / 1000;
        if (sec != last_sec) {
            last_sec = sec;
            dirty = 1;
            serial_puts("uptime: ");
            serial_put_dec(sec);
            serial_puts(" s, ticks=");
            serial_put_dec(pit_ticks());
            serial_puts("\n");
        }

        if (dirty)
            render();
    }
}
