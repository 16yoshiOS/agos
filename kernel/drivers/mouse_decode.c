/* =============================================================================
 *  AGOS - PS/2 mouse packet decoder (mouse_decode.c)
 * ========================================================================== */
#include "mouse_decode.h"

void mouse_decoder_reset(struct mouse_decoder *d)
{
    d->n = 0;
}

void mouse_decoder_init(struct mouse_decoder *d, uint8_t device_id)
{
    d->id = device_id;
    d->plen = (device_id == 3 || device_id == 4) ? 4 : 3;
    d->n = 0;
}

int mouse_decoder_feed(struct mouse_decoder *d, uint8_t byte, struct mouse_event *ev)
{
    if (d->n == 0 && !(byte & 0x08))
        return 0;                       /* bit 3 is always 1 in byte 0: resync */

    d->buf[d->n++] = byte;
    if (d->n < d->plen)
        return 0;
    d->n = 0;

    uint8_t b0 = d->buf[0];
    int dx = d->buf[1], dy = d->buf[2];

    if (b0 & 0x10)                      /* X sign: 9-bit two's complement      */
        dx -= 256;
    if (b0 & 0x20)
        dy -= 256;
    if (b0 & 0xC0)                      /* overflow: the delta is garbage      */
        dx = dy = 0;

    ev->dx = (int16_t)dx;
    ev->dy = (int16_t)-dy;              /* PS/2 Y grows upwards                */
    ev->buttons = b0 & 0x07;
    ev->wheel = 0;

    if (d->plen == 4) {
        uint8_t z = d->buf[3];

        if (d->id == 4) {               /* 4-bit signed wheel + buttons 4/5    */
            ev->wheel = (int8_t)((z & 0x08) ? (int)(z & 0x07) - 8 : (int)(z & 0x07));
            ev->buttons |= (uint8_t)((z & 0x10) ? MOUSE_BTN_4 : 0);
            ev->buttons |= (uint8_t)((z & 0x20) ? MOUSE_BTN_5 : 0);
        } else {                        /* full signed byte (usually +-1)      */
            int w = (int8_t)z;

            ev->wheel = (int8_t)(w < -8 ? -8 : (w > 7 ? 7 : w));
        }
    }
    return 1;
}
