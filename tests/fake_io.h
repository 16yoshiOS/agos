/* =============================================================================
 *  AGOS - fake port-I/O layer for host-side hardware simulation tests
 *  Force-included by `make test` together with -DAGOS_IO_H (which disables the
 *  real kernel/io.h). Replaces port I/O with a software model of the PIC, the
 *  PS/2 controller, the PIT counter latch and the CMOS, plus optional hooks so
 *  individual tests (PCI, S3 registers) can model their own devices.
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
extern uint8_t fake_mask_master, fake_mask_slave;   /* last value on 0x21 / 0xA1 */

extern uint8_t fake_kbd_bytes[256];         /* bytes waiting at port 0x60       */
extern uint8_t fake_kbd_aux[256];           /* 1 = byte came from the mouse     */
extern int     fake_kbd_count, fake_kbd_pos;

extern uint8_t  fake_cmos[128];             /* CMOS registers                   */
extern uint16_t fake_pit_count;             /* value a PIT latch read returns   */

/* Device hooks: return 1 and set *val if handled. */
extern int (*fake_in_hook)(uint16_t port, int width, uint32_t *val);
extern int (*fake_out_hook)(uint16_t port, int width, uint32_t val);

void     outb(uint16_t port, uint8_t value);
uint8_t  inb(uint16_t port);
void     outw(uint16_t port, uint16_t value);
uint16_t inw(uint16_t port);
void     outl(uint16_t port, uint32_t value);
uint32_t inl(uint16_t port);

static inline void io_wait(void)  { outb(0x80, 0); }
static inline void cpu_cli(void)  {}
static inline void cpu_sti(void)  {}
static inline void cpu_hlt(void)  {}
static inline uint32_t cpu_irq_save(void) { return 0; }
static inline void cpu_irq_restore(uint32_t f) { (void)f; }

#endif /* FAKE_IO_H */
