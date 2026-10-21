/* =============================================================================
 *  AGOS - GUI shell: menu bar, desktop, Settings window (gui/desktop.h)
 *  The macOS-style chrome drawn every frame:
 *    - 24-px Menu Bar with the logo mark, app menus and a live clock;
 *    - a rounded "Settings" card whose Light/Dark toggle is wired to the same
 *      theme state the Tab key flips;
 *    - a small Dock strip.
 *  Keyboard events arrive from keyboard.c's queue; the Tab scancode (0x0F)
 *  toggles the theme and repaints the whole framebuffer.
 * ========================================================================== */
#ifndef AGOS_DESKTOP_H
#define AGOS_DESKTOP_H

#include <stdbool.h>

#include "kbd_decode.h"
#include "theme.h"

/* One frame: full framebuffer rebuild for the current theme. Cheap enough
 * at these resolutions and only called when something actually changed. */
void desktop_render(void);

/* Feed one decoded key event in. Returns true if the screen must be redrawn
 * (theme flipped, text edited...). */
bool desktop_handle_key(const struct key_event *ev);

/* Live status strings shown in the Settings window (uptime / RAM / display).
 * Filled by kernel.c once per second. */
struct desktop_status {
    char uptime[16];                        /* "HH:MM:SS"                     */
    char ram[32];                           /* "12 MB usable"                 */
    char display[32];                       /* "1024x768x32 S3 LFB"           */
    char last_key[24];                      /* "Tab sc=0f"                    */
};
void desktop_set_status(const struct desktop_status *st);

#endif /* AGOS_DESKTOP_H */
