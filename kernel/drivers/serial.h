/* =============================================================================
 *  AGOS - COM1 serial debug console (serial.h / serial.c)
 *  The kernel log. Visible with `qemu -serial stdio` and in most hypervisors;
 *  invaluable because it keeps working when the graphics path is broken.
 * ========================================================================== */
#ifndef AGOS_SERIAL_H
#define AGOS_SERIAL_H

#include <stdint.h>

void serial_init(void);                 /* 115200 baud, 8N1, FIFO on          */
void serial_putc(char c);
void serial_puts(const char *s);        /* '\n' is sent as "\r\n"             */
void serial_put_hex(uint32_t v);        /* "0x" + 8 hex digits                */
void serial_put_dec(uint32_t v);

#endif /* AGOS_SERIAL_H */
