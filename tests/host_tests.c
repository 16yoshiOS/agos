/* =============================================================================
 *  AGOS - host-side unit tests (tests/host_tests.c)
 *  Build and run with `make test`. These cover the pure logic of the kernel
 *  (no hardware access): memory/string helpers, GDT/IDT descriptor encoding,
 *  the PS/2 scancode decoder, the PIT divisor and the bitmap font.
 *  `host_tests --font` dumps every glyph as ASCII art for eyeballing.
 * ========================================================================== */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "kbd_decode.h"
#include "lib.h"
#include "mouse_decode.h"
#include "pit.h"

/* --- Stubs for symbols that normally come from assembly -------------------- */
void gdt_flush(const struct gdt_ptr *gp) { (void)gp; }
void tss_flush(void) {}
void idt_flush(const struct idt_ptr *ip) { (void)ip; }
uint8_t  kernel_stack_top[1];
uint32_t isr_stub_table[ISR_VECTORS];

/* --- Tiny test harness ----------------------------------------------------- */
static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static uint64_t gdt_u64(const struct gdt_entry *e)
{
    uint64_t v;
    memcpy(&v, e, 8);       /* renamed to k_memcpy: exercises the kernel's copy */
    return v;
}

/* --- lib ------------------------------------------------------------------- */
static void test_lib(void)
{
    unsigned char a[16], b[16];
    char s[16];
    char num[34];

    k_memset(a, 0xAB, sizeof(a));
    CHECK(a[0] == 0xAB && a[15] == 0xAB);

    for (int i = 0; i < 16; i++) a[i] = (unsigned char)i;
    k_memcpy(b, a, 16);
    CHECK(k_memcmp(a, b, 16) == 0);
    b[7] ^= 1;
    CHECK(k_memcmp(a, b, 16) < 0 || k_memcmp(a, b, 16) > 0);
    CHECK(k_memcmp(a, b, 7) == 0);

    /* overlapping moves in both directions */
    for (int i = 0; i < 16; i++) a[i] = (unsigned char)i;
    k_memmove(a + 2, a, 8);                       /* dst > src: backwards copy */
    CHECK(a[2] == 0 && a[9] == 7 && a[0] == 0 && a[1] == 1);
    for (int i = 0; i < 16; i++) a[i] = (unsigned char)i;
    k_memmove(a, a + 2, 8);                       /* dst < src: forwards copy  */
    CHECK(a[0] == 2 && a[7] == 9);

    CHECK(k_strlen("") == 0);
    CHECK(k_strlen("AGOS") == 4);

    s[0] = '\0';
    CHECK(kstrlcat(s, "ab", sizeof(s)) == 2);
    CHECK(kstrlcat(s, "cd", sizeof(s)) == 4);
    CHECK(strcmp(s, "abcd") == 0);

    char small[5];                                /* truncation, always NUL    */
    small[0] = '\0';
    kstrlcat(small, "abc", sizeof(small));
    CHECK(kstrlcat(small, "defgh", sizeof(small)) == 4);
    CHECK(strcmp(small, "abcd") == 0);

    CHECK(kutoa(num, 0, 10, 1) == 1 && strcmp(num, "0") == 0);
    CHECK(kutoa(num, 12345, 10, 1) == 5 && strcmp(num, "12345") == 0);
    CHECK(kutoa(num, 0xBEEF, 16, 8) == 8 && strcmp(num, "0000BEEF") == 0);
    CHECK(kutoa(num, 0xFFFFFFFFu, 16, 1) == 8 && strcmp(num, "FFFFFFFF") == 0);
    CHECK(kutoa(num, 5, 2, 4) == 4 && strcmp(num, "0101") == 0);
    CHECK(kutoa(num, 7, 10, 2) == 2 && strcmp(num, "07") == 0);
}

/* --- GDT / TSS --------------------------------------------------------------- */
static void test_gdt(void)
{
    struct gdt_entry e;

    gdt_encode(&e, 0, 0xFFFFF, GDT_ACCESS_KCODE, GDT_GRAN_4K_32);
    CHECK(gdt_u64(&e) == 0x00CF9A000000FFFFull);   /* canonical flat code seg  */
    gdt_encode(&e, 0, 0xFFFFF, GDT_ACCESS_KDATA, GDT_GRAN_4K_32);
    CHECK(gdt_u64(&e) == 0x00CF92000000FFFFull);   /* canonical flat data seg  */
    gdt_encode(&e, 0, 0xFFFFF, GDT_ACCESS_UCODE, GDT_GRAN_4K_32);
    CHECK(gdt_u64(&e) == 0x00CFFA000000FFFFull);
    gdt_encode(&e, 0, 0xFFFFF, GDT_ACCESS_UDATA, GDT_GRAN_4K_32);
    CHECK(gdt_u64(&e) == 0x00CFF2000000FFFFull);

    /* TSS-like descriptor with a non-trivial base and a byte-granular limit */
    gdt_encode(&e, 0x12345678, 103, GDT_ACCESS_TSS, GDT_GRAN_BYTE);
    CHECK(e.base_low == 0x5678 && e.base_mid == 0x34 && e.base_high == 0x12);
    CHECK(e.limit_low == 103 && e.granularity == 0x00 && e.access == 0x89);

    /* limit bits 19:16 land in the low nibble of the granularity byte */
    gdt_encode(&e, 0, 0xABCDE, 0x92, 0xC0);
    CHECK(e.limit_low == 0xBCDE && e.granularity == 0xCA);

    /* selector constants vs table order (index * 8, RPL in low bits) */
    CHECK(GDT_KERNEL_CODE == 1 * 8 && GDT_KERNEL_DATA == 2 * 8);
    CHECK(GDT_USER_CODE == (3 * 8 | 3) && GDT_USER_DATA == (4 * 8 | 3));
    CHECK(GDT_TSS == 5 * 8 && GDT_ENTRIES == 6);
}

/* --- IDT ------------------------------------------------------------------------ */
static void test_idt(void)
{
    struct idt_entry e;

    idt_encode(&e, 0xDEADBEEF, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    CHECK(e.offset_low == 0xBEEF && e.offset_high == 0xDEAD);
    CHECK(e.selector == 0x08 && e.zero == 0 && e.type_attr == 0x8E);

    CHECK(ISR_VECTORS == 48 && IRQ_VECTOR(0) == 32 && IRQ_VECTOR(15) == 47);
}

/* --- PIT -------------------------------------------------------------------------- */
static void test_pit(void)
{
    CHECK(pit_divisor_for(100) == 11932);          /* 1193182/100 = 11931.8    */
    CHECK(pit_divisor_for(1000) == 1193);
    CHECK(pit_divisor_for(0) == 65535);            /* clamped, never 0         */
    CHECK(pit_divisor_for(1) == 65535);
    CHECK(pit_divisor_for(10000000) == 1);
    /* The default divisor yields 99.998 Hz; it must report 100, not 99
     * (regression: truncating division made the uptime clock 1% slow). */
    CHECK(pit_rate_for_divisor(pit_divisor_for(100)) == 100);
    CHECK(pit_rate_for_divisor(pit_divisor_for(1000)) == 1000);
    CHECK(pit_rate_for_divisor(pit_divisor_for(18)) == 18);
}

/* --- Keyboard decoder ---------------------------------------------------------------- */
static int feed(struct kbd_decoder *d, uint8_t sc, struct key_event *ev)
{
    return kbd_decoder_feed(d, sc, ev);
}

static void test_keyboard(void)
{
    struct kbd_decoder d;
    struct key_event ev;

    kbd_decoder_init(&d);

    /* plain letters and digits, press and release */
    CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 'a' && ev.pressed && ev.mods == 0);
    CHECK(feed(&d, 0x9E, &ev) && ev.keycode == 'a' && !ev.pressed);
    CHECK(feed(&d, 0x02, &ev) && ev.keycode == '1');
    CHECK(feed(&d, 0x0B, &ev) && ev.keycode == '0');
    CHECK(feed(&d, 0x39, &ev) && ev.keycode == ' ');
    CHECK(feed(&d, 0x1C, &ev) && ev.keycode == '\n');
    CHECK(feed(&d, 0x0E, &ev) && ev.keycode == '\b');
    CHECK(feed(&d, 0x0F, &ev) && ev.keycode == '\t');
    CHECK(feed(&d, 0x01, &ev) && ev.keycode == 27);

    /* Shift: letters, symbols, and release */
    CHECK(!feed(&d, 0x2A, &ev));                                   /* LShift down */
    CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 'A' && (ev.mods & KMOD_SHIFT));
    CHECK(feed(&d, 0x02, &ev) && ev.keycode == '!');
    CHECK(feed(&d, 0x28, &ev) && ev.keycode == '"');
    CHECK(feed(&d, 0x35, &ev) && ev.keycode == '?');
    CHECK(!feed(&d, 0xAA, &ev));                                   /* LShift up   */
    CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 'a' && !(ev.mods & KMOD_SHIFT));

    /* both shifts: releasing one keeps shift active */
    feed(&d, 0x2A, &ev); feed(&d, 0x36, &ev); feed(&d, 0xAA, &ev);
    CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 'A');
    feed(&d, 0xB6, &ev);
    CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 'a');

    /* Caps Lock: letters only, shift inverts */
    CHECK(!feed(&d, 0x3A, &ev));
    CHECK(!feed(&d, 0xBA, &ev));                                   /* release is not a toggle */
    CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 'A' && (ev.mods & KMOD_CAPS));
    CHECK(feed(&d, 0x02, &ev) && ev.keycode == '1');               /* digits unaffected       */
    feed(&d, 0x2A, &ev);
    CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 'a');               /* caps + shift = lower    */
    CHECK(feed(&d, 0x02, &ev) && ev.keycode == '!');               /* but symbols shift       */
    feed(&d, 0xAA, &ev);
    feed(&d, 0x3A, &ev); feed(&d, 0xBA, &ev);                      /* caps off                */
    CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 'a');

    /* Ctrl + letter -> control code */
    feed(&d, 0x1D, &ev);
    CHECK(feed(&d, 0x2E, &ev) && ev.keycode == 3 && (ev.mods & KMOD_CTRL));   /* Ctrl+C */
    CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 1);                            /* Ctrl+A */
    feed(&d, 0x9D, &ev);
    CHECK(feed(&d, 0x2E, &ev) && ev.keycode == 'c');

    /* extended keys */
    CHECK(!feed(&d, 0xE0, &ev));
    CHECK(feed(&d, 0x48, &ev) && ev.keycode == KEY_UP && ev.extended && ev.pressed);
    CHECK(!feed(&d, 0xE0, &ev));
    CHECK(feed(&d, 0xC8, &ev) && ev.keycode == KEY_UP && !ev.pressed);
    feed(&d, 0xE0, &ev); CHECK(feed(&d, 0x50, &ev) && ev.keycode == KEY_DOWN);
    feed(&d, 0xE0, &ev); CHECK(feed(&d, 0x4B, &ev) && ev.keycode == KEY_LEFT);
    feed(&d, 0xE0, &ev); CHECK(feed(&d, 0x4D, &ev) && ev.keycode == KEY_RIGHT);
    feed(&d, 0xE0, &ev); CHECK(feed(&d, 0x53, &ev) && ev.keycode == KEY_DEL);
    feed(&d, 0xE0, &ev); CHECK(feed(&d, 0x1C, &ev) && ev.keycode == '\n');     /* keypad Enter */

    /* extended Ctrl/Alt update modifiers without producing events */
    CHECK(!feed(&d, 0xE0, &ev)); CHECK(!feed(&d, 0x1D, &ev));                  /* RCtrl down   */
    CHECK(feed(&d, 0x2E, &ev) && ev.keycode == 3);
    CHECK(!feed(&d, 0xE0, &ev)); CHECK(!feed(&d, 0x9D, &ev));                  /* RCtrl up     */
    CHECK(feed(&d, 0x2E, &ev) && ev.keycode == 'c');

    /* PrintScreen "fake shift" bytes are swallowed */
    CHECK(!feed(&d, 0xE0, &ev)); CHECK(!feed(&d, 0x2A, &ev));
    CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 'a');

    /* Pause/Break: E1 1D 45 E1 9D C5 produces nothing at all */
    {
        static const uint8_t pause[] = { 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5 };
        int produced = 0;
        for (unsigned i = 0; i < sizeof(pause); i++)
            produced += feed(&d, pause[i], &ev);
        CHECK(produced == 0);
        CHECK(feed(&d, 0x1E, &ev) && ev.keycode == 'a');   /* decoder back in sync */
    }

    /* function keys */
    CHECK(feed(&d, 0x3B, &ev) && ev.keycode == KEY_F1);
    CHECK(feed(&d, 0x44, &ev) && ev.keycode == KEY_F10);
    CHECK(feed(&d, 0x57, &ev) && ev.keycode == KEY_F11);
    CHECK(feed(&d, 0x58, &ev) && ev.keycode == KEY_F12);

    /* unmapped / marker bytes */
    CHECK(!feed(&d, 0x00, &ev));
    CHECK(!feed(&d, 0xFF, &ev));
    CHECK(!feed(&d, 0x59, &ev));

    /* every mapped scancode 0x01..0x39 behaves: modifiers silent, rest printable */
    int printable = 0;
    kbd_decoder_init(&d);
    for (uint8_t sc = 0x01; sc <= 0x39; sc++) {
        if (feed(&d, sc, &ev)) {
            printable++;
            CHECK(ev.keycode != 0 && ev.keycode < 0x80);
        }
        feed(&d, (uint8_t)(sc | 0x80), &ev);
    }
    /* 0x39 entries minus LCtrl(1D) LShift(2A) RShift(36) LAlt(38) = 57 - 4 = 53 */
    CHECK(printable == 53);
}

/* --- Font ------------------------------------------------------------------------------ */
static void test_font(void)
{
    const uint8_t *space = font_glyph(' ');
    const uint8_t *missing = font_glyph('\x01');
    int space_empty = 1;

    for (int i = 0; i < 8; i++) if (space[i]) space_empty = 0;
    CHECK(space_empty);

    /* every printable glyph except space has at least one pixel, and the bottom
     * row is blank for non-descenders is NOT required - only non-emptiness */
    for (int c = 0x21; c <= 0x7E; c++) {
        const uint8_t *g = font_glyph((char)c);
        int any = 0;
        for (int i = 0; i < 8; i++) if (g[i]) any = 1;
        CHECK(any);
    }
    CHECK(font_glyph((char)0x7F) == missing);
    CHECK(font_glyph((char)0x80) == missing);

    /* no two printable glyphs may be identical (catches copy/paste slips) */
    for (int a = 0x21; a <= 0x7E; a++)
        for (int b = a + 1; b <= 0x7E; b++)
            CHECK(memcmp(font_glyph((char)a), font_glyph((char)b), 8) != 0);
}

static void dump_font(void)
{
    for (int c = 0x21; c <= 0x7E; c++) {
        const uint8_t *g = font_glyph((char)c);
        printf("'%c' (0x%02X)\n", c, c);
        for (int r = 0; r < 8; r++) {
            for (int x = 0; x < 8; x++)
                putchar((g[r] >> x) & 1 ? '#' : '.');
            putchar('\n');
        }
    }
}


/* --- ksnprintf / string helpers ---------------------------------------------------- */
static void test_format(void)
{
    char b[64];

    CHECK(ksnprintf(b, sizeof(b), "%d MB free of %d MB", 72, 256) == 20 && !strcmp(b, "72 MB free of 256 MB"));
    ksnprintf(b, sizeof(b), "%5d|%-5d|%05d|%x|%X|%u", 42, 42, 42, 255, 255, 4000000000u);
    CHECK(!strcmp(b, "   42|42   |00042|ff|FF|4000000000"));
    ksnprintf(b, sizeof(b), "%d %d", -7, (int)0x80000000);
    CHECK(!strcmp(b, "-7 -2147483648"));
    ksnprintf(b, sizeof(b), "[%s][%10s][%-6s][%.3s][%c][%%]", "ab", "right", "left", "truncated", 'x');
    CHECK(!strcmp(b, "[ab][     right][left  ][tru][x][%]"));
    ksnprintf(b, sizeof(b), "%*d|%-*d|%.*s", 6, 12, 4, 7, 2, "hello");
    CHECK(!strcmp(b, "    12|7   |he"));
    ksnprintf(b, sizeof(b), "%s", (char *)0);
    CHECK(!strcmp(b, "(null)"));

    char small[8];                                   /* truncation: terminated, length reported */
    CHECK(ksnprintf(small, sizeof(small), "%s", "0123456789") == 10);
    CHECK(!strcmp(small, "0123456"));
    CHECK(ksnprintf(NULL, 0, "%d", 12345) == 5);

    CHECK(kstrcmp("a", "b") < 0 && kstrcmp("b", "a") > 0 && kstrcmp("x", "x") == 0);
    CHECK(kstrncmp("abcd", "abzz", 2) == 0 && kstrncmp("abcd", "abzz", 3) != 0);
    CHECK(kstrcasecmp("HeLLo", "hello") == 0 && kstrncasecmp("HTTP://", "http", 4) == 0);
    CHECK(kstrchr("hello", 'l') && kstrchr("hello", 'l') - "hello" == 2 && !kstrchr("hello", 'z'));
    CHECK(kstrstr("hello world", "o w") != NULL && !kstrstr("hello", "xyz"));
    char c[8];
    CHECK(kstrlcpy(c, "toolongstring", sizeof(c)) == 13 && !strcmp(c, "toolong"));
    CHECK(kstrtoul("0x1F", NULL, 0) == 31 && kstrtoul("  123abc", NULL, 10) == 123);
    const char *end;
    CHECK(kstrtoul("777z", &end, 8) == 511 && *end == 'z');
}

/* --- Mouse packet decoder ---------------------------------------------------------- */
static void test_mouse_decode(void)
{
    struct mouse_decoder d;
    struct mouse_event e;

    mouse_decoder_init(&d, 0);
    CHECK(!mouse_decoder_feed(&d, 0x09, &e));
    CHECK(!mouse_decoder_feed(&d, 10, &e));
    CHECK(mouse_decoder_feed(&d, 3, &e) && e.dx == 10 && e.dy == -3 && e.buttons == 1);

    /* negative deltas via the sign bits (9-bit two's complement) */
    CHECK(!mouse_decoder_feed(&d, 0x08 | 0x10 | 0x20 | 0x02, &e));
    CHECK(!mouse_decoder_feed(&d, 0xF6, &e));            /* -10 */
    CHECK(mouse_decoder_feed(&d, 0xFD, &e) && e.dx == -10 && e.dy == 3 && e.buttons == 2);

    /* a byte without the sync bit is dropped while waiting for a packet start */
    CHECK(!mouse_decoder_feed(&d, 0x00, &e));
    CHECK(!mouse_decoder_feed(&d, 0x0C, &e));            /* middle button, sync ok */
    CHECK(!mouse_decoder_feed(&d, 0, &e));
    CHECK(mouse_decoder_feed(&d, 0, &e) && e.buttons == 4 && e.dx == 0);

    /* overflow bits: motion discarded, buttons kept */
    CHECK(!mouse_decoder_feed(&d, 0x08 | 0x40 | 0x01, &e));
    CHECK(!mouse_decoder_feed(&d, 200, &e));
    CHECK(mouse_decoder_feed(&d, 100, &e) && e.dx == 0 && e.dy == 0 && e.buttons == 1);

    /* reset drops a half packet */
    mouse_decoder_feed(&d, 0x09, &e);
    mouse_decoder_reset(&d);
    CHECK(!mouse_decoder_feed(&d, 0x08, &e));            /* starts fresh with this one */
    CHECK(!mouse_decoder_feed(&d, 1, &e));
    CHECK(mouse_decoder_feed(&d, 1, &e) && e.dx == 1 && e.dy == -1);

    /* IntelliMouse (id 3): 4-byte packets, signed wheel */
    mouse_decoder_init(&d, 3);
    mouse_decoder_feed(&d, 0x08, &e); mouse_decoder_feed(&d, 0, &e); mouse_decoder_feed(&d, 0, &e);
    CHECK(mouse_decoder_feed(&d, 0x01, &e) && e.wheel == 1);
    mouse_decoder_feed(&d, 0x08, &e); mouse_decoder_feed(&d, 0, &e); mouse_decoder_feed(&d, 0, &e);
    CHECK(mouse_decoder_feed(&d, 0xFE, &e) && e.wheel == -2);

    /* Explorer (id 4): 4-bit wheel + buttons 4/5 */
    mouse_decoder_init(&d, 4);
    mouse_decoder_feed(&d, 0x09, &e); mouse_decoder_feed(&d, 0, &e); mouse_decoder_feed(&d, 0, &e);
    CHECK(mouse_decoder_feed(&d, 0x10 | 0x0F, &e) && e.wheel == -1 &&
          (e.buttons & MOUSE_BTN_4) && !(e.buttons & MOUSE_BTN_5) && (e.buttons & MOUSE_BTN_LEFT));
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--font") == 0) {
        dump_font();
        return 0;
    }

    test_lib();
    test_gdt();
    test_idt();
    test_pit();
    test_keyboard();
    test_font();
    test_format();
    test_mouse_decode();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
