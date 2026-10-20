/* =============================================================================
 *  AGOS - fake port-I/O layer for host-side hardware simulation tests
 *  Force-included by `make test` together with -DAGOS_IO_H (which disables the
 *  real kernel/io.h). Replaces inb/outb with a software model of the PIC, the
 *  PS/2 controller and a write log, so the real pic.c / pit.c / keyboard.c /
 *  isr.c can run on the build host.
 * ========================================================================== */
#ifndef FAKE_IO_H
#define FAKE_IO_H

#include <stdint.h>

#define IO_LOG_MAX 8192

struct io_write { uint16_t port; uint8_t val; };

extern struct io_write io_log[IO_LOG_MAX];  /* every outb except POST port 0x80 */
extern int             io_log_n;

extern uint8_t fake_isr_master;             /* what reading PIC1 cmd returns    */
extern uint8_t fake_isr_slave;              /* what reading PIC2 cmd returns    */
extern uint8_t fake_kbd_bytes[256];         /* scancodes waiting at port 0x60   */
extern int     fake_kbd_count, fake_kbd_pos;

void    outb(uint16_t port, uint8_t value);
uint8_t inb(uint16_t port);

static inline void io_wait(void)  { outb(0x80, 0); }
static inline void cpu_cli(void)  {}
static inline void cpu_sti(void)  {}
static inline void cpu_hlt(void)  {}

#endif /* FAKE_IO_H */
