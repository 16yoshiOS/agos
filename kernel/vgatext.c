/* =============================================================================
 *  AGOS - VGA text-mode fallback (vgatext.c)
 * ========================================================================== */
#include "vgatext.h"

#define VGATEXT_MEM ((volatile uint16_t *)0xB8000)

void vgatext_clear(uint8_t attr)
{
    for (int i = 0; i < VGATEXT_COLS * VGATEXT_ROWS; i++)
        VGATEXT_MEM[i] = (uint16_t)((attr << 8) | ' ');
}

void vgatext_puts(int col, int row, const char *s, uint8_t attr)
{
    if (row < 0 || row >= VGATEXT_ROWS)
        return;
    for (; *s && col < VGATEXT_COLS; s++, col++) {
        if (col >= 0)
            VGATEXT_MEM[row * VGATEXT_COLS + col] = (uint16_t)((attr << 8) | (uint8_t)*s);
    }
}
