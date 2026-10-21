/* =============================================================================
 *  AGOS - COM1 serial debug console (serial.c)
 * ========================================================================== */
#include "serial.h"
#include "io.h"
#include "lib.h"

#define COM1 0x3F8
#define REG_DATA      (COM1 + 0)        /* THR / RBR, or divisor low (DLAB=1)  */
#define REG_IER       (COM1 + 1)        /* or divisor high (DLAB=1)            */
#define REG_FCR       (COM1 + 2)
#define REG_LCR       (COM1 + 3)
#define REG_MCR       (COM1 + 4)
#define REG_LSR       (COM1 + 5)

#define LSR_TX_EMPTY  0x20

void serial_init(void)
{
    outb(REG_IER, 0x00);                /* no serial interrupts               */
    outb(REG_LCR, 0x80);                /* DLAB on: program the baud divisor  */
    outb(REG_DATA, 0x01);               /* divisor 1 = 115200 baud (low)      */
    outb(REG_IER,  0x00);               /*                         (high)     */
    outb(REG_LCR, 0x03);                /* DLAB off, 8 data bits, no parity, 1 stop */
    outb(REG_FCR, 0xC7);                /* enable + clear FIFOs, 14-byte trigger */
    outb(REG_MCR, 0x0B);                /* DTR, RTS, OUT2                     */
}

void serial_putc(char c)
{
    /* Bounded wait: if there is no UART the loop ends instead of hanging. */
    for (int spin = 0; spin < 100000; spin++) {
        if (inb(REG_LSR) & LSR_TX_EMPTY)
            break;
    }
    outb(REG_DATA, (uint8_t)c);
}

void serial_puts(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s);
    }
}

void serial_put_hex(uint32_t v)
{
    char buf[34];

    kutoa(buf, v, 16, 8);
    serial_puts("0x");
    serial_puts(buf);
}

void serial_put_dec(uint32_t v)
{
    char buf[34];

    kutoa(buf, v, 10, 1);
    serial_puts(buf);
}
