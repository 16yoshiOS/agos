/* =============================================================================
 *  AGOS - PS/2 controller (8042) driver (ps2.c)
 * -----------------------------------------------------------------------------
 *  Init sequence (see the 8042 / "PS/2 Controller" documentation):
 *    1. disable both ports, flush the output buffer
 *    2. read the configuration byte, turn IRQs off (translation is left as the
 *       firmware set it: the keyboard decoder expects scancode set 1)
 *    3. enable both ports, reset-and-configure the mouse (defaults, wheel probe
 *       via the 200/100/80 and 200/200/80 sample-rate magic, 100 Hz, 8 count/mm)
 *    4. enable streaming, then enable IRQ1/IRQ12 in the configuration byte
 * ========================================================================== */
#include "ps2.h"
#include "io.h"
#include "isr.h"
#include "keyboard.h"
#include "mouse.h"
#include "pic.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define ST_OUT_FULL 0x01
#define ST_IN_FULL  0x02
#define ST_AUX      0x20                /* byte in the output buffer is from the mouse */

#define CMD_READ_CFG   0x20
#define CMD_WRITE_CFG  0x60
#define CMD_DISABLE_AUX 0xA7
#define CMD_ENABLE_AUX  0xA8
#define CMD_DISABLE_KBD 0xAD
#define CMD_ENABLE_KBD  0xAE
#define CMD_WRITE_AUX   0xD4

#define CFG_IRQ1     0x01
#define CFG_IRQ12    0x02
#define CFG_KBD_CLK_OFF 0x10
#define CFG_AUX_CLK_OFF 0x20

#define DEV_ACK   0xFA
#define DEV_RESEND 0xFE

static bool    mouse_ok;
static uint8_t mouse_id;

static bool wait_input_empty(void)
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(PS2_STATUS) & ST_IN_FULL))
            return true;
    return false;
}

static bool wait_output_full(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(PS2_STATUS) & ST_OUT_FULL)
            return true;
    return false;
}

static void flush_output(void)
{
    for (int i = 0; i < 64 && (inb(PS2_STATUS) & ST_OUT_FULL); i++) {
        (void)inb(PS2_DATA);
        io_wait();
    }
}

static void ctrl_cmd(uint8_t cmd)
{
    wait_input_empty();
    outb(PS2_CMD, cmd);
}

static uint8_t read_config(void)
{
    ctrl_cmd(CMD_READ_CFG);
    return wait_output_full() ? inb(PS2_DATA) : 0;
}

static void write_config(uint8_t cfg)
{
    ctrl_cmd(CMD_WRITE_CFG);
    wait_input_empty();
    outb(PS2_DATA, cfg);
}

/* Send one byte to the mouse. */
static void mouse_write(uint8_t b)
{
    ctrl_cmd(CMD_WRITE_AUX);
    wait_input_empty();
    outb(PS2_DATA, b);
}

static int mouse_read(void)
{
    if (!wait_output_full())
        return -1;
    return inb(PS2_DATA);
}

/* Send a command byte and require an ACK (retrying on RESEND). */
static bool mouse_cmd(uint8_t b)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        int r;

        mouse_write(b);
        r = mouse_read();
        if (r == DEV_ACK)
            return true;
        if (r != DEV_RESEND)
            return false;
    }
    return false;
}

static bool mouse_set_rate(uint8_t rate)
{
    return mouse_cmd(0xF3) && mouse_cmd(rate);
}

static int mouse_get_id(void)
{
    if (!mouse_cmd(0xF2))
        return -1;
    return mouse_read();
}

static bool mouse_init(void)
{
    int id;

    /* Reset: ACK, then self-test result 0xAA and device id 0x00. Some
     * emulators skip the reset answer, so only the ACK is mandatory. */
    mouse_write(0xFF);
    if (mouse_read() != DEV_ACK)
        return false;
    (void)mouse_read();                  /* 0xAA self-test passed               */
    (void)mouse_read();                  /* device id                           */
    flush_output();

    if (!mouse_cmd(0xF6))                /* set defaults                        */
        return false;

    /* Wheel mouse magic knock: sample rates 200, 100, 80 -> id 3. */
    mouse_id = 0;
    if (mouse_set_rate(200) && mouse_set_rate(100) && mouse_set_rate(80)) {
        id = mouse_get_id();
        if (id == 3)
            mouse_id = 3;
    }
    /* Five-button knock: 200, 200, 80 -> id 4. */
    if (mouse_id == 3 && mouse_set_rate(200) && mouse_set_rate(200) && mouse_set_rate(80)) {
        id = mouse_get_id();
        if (id == 4)
            mouse_id = 4;
    }

    (void)mouse_set_rate(100);           /* 100 reports per second              */
    (void)(mouse_cmd(0xE8) && mouse_cmd(0x03));   /* 8 counts per mm            */
    (void)mouse_cmd(0xE6);               /* 1:1 scaling                         */

    if (!mouse_cmd(0xF4))                /* enable data reporting               */
        return false;
    flush_output();
    return true;
}

void ps2_service(void)
{
    for (int i = 0; i < 32; i++) {
        uint8_t status = inb(PS2_STATUS);
        uint8_t byte;

        if (!(status & ST_OUT_FULL))
            break;
        byte = inb(PS2_DATA);
        if (status & ST_AUX)
            mouse_feed_byte(byte);
        else
            kbd_feed_byte(byte);
    }
}

static void ps2_irq(struct regs *r)
{
    (void)r;
    ps2_service();
}

void ps2_init(void)
{
    uint8_t cfg;

    kbd_init();
    mouse_ok = false;
    mouse_id = 0;
    mouse_setup(0);

    ctrl_cmd(CMD_DISABLE_KBD);
    ctrl_cmd(CMD_DISABLE_AUX);
    flush_output();

    cfg = read_config();
    cfg &= (uint8_t)~(CFG_IRQ1 | CFG_IRQ12);
    write_config(cfg);

    ctrl_cmd(CMD_ENABLE_KBD);
    ctrl_cmd(CMD_ENABLE_AUX);
    cfg = (uint8_t)(read_config() & ~(CFG_KBD_CLK_OFF | CFG_AUX_CLK_OFF));
    write_config(cfg);
    flush_output();

    mouse_ok = mouse_init();
    if (mouse_ok)
        mouse_setup(mouse_id);

    isr_register_handler(IRQ_VECTOR(1), ps2_irq);
    cfg = (uint8_t)(read_config() | CFG_IRQ1);
    if (mouse_ok) {
        isr_register_handler(IRQ_VECTOR(12), ps2_irq);
        cfg |= CFG_IRQ12;
    }
    write_config(cfg);
    flush_output();

    pic_clear_mask(1);
    if (mouse_ok)
        pic_clear_mask(12);              /* also opens the cascade (IRQ2)       */
}

bool    ps2_mouse_present(void) { return mouse_ok; }
uint8_t ps2_mouse_id(void)      { return mouse_id; }
