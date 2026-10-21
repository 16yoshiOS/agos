/* =============================================================================
 *  AGOS - macOS-style themes and window chrome (gui/theme.h / theme.c)
 *  Two complete palettes (Light / Dark) plus the reusable widgets the desktop,
 *  the Settings window and the browser draw with: rounded cards, toggles,
 *  sliders, title bars. Colours are plain 0xRRGGBB; gfx.c packs them for the
 *  actual screen format.
 * ========================================================================== */
#ifndef AGOS_THEME_H
#define AGOS_THEME_H

#include <stdbool.h>
#include <stdint.h>

#define MENUBAR_H 24                        /* menu bar height, in pixels     */

enum { THEME_LIGHT = 0, THEME_DARK = 1, THEME_COUNT = 2 };

struct theme {
    const char *name;
    uint32_t desktop;                       /* wallpaper background           */
    uint32_t menubar;                       /* top bar fill                   */
    uint32_t menubar_text;                  /* menu titles / clock            */
    uint32_t text;                          /* primary label colour           */
    uint32_t subtext;                       /* secondary / caption colour     */
    uint32_t card;                          /* window & panel body            */
    uint32_t card_alt;                      /* inset list rows                */
    uint32_t border;                        /* hairlines around widgets       */
    uint32_t accent;                        /* selection / on-state blue      */
    uint32_t toggle_off;                    /* switch track when off          */
    uint32_t link;                          /* hyperlink text (<a>)           */
    uint32_t button;                        /* <button> face                  */
    uint32_t button_text;                   /* <button> label                 */
};

extern const struct theme themes[THEME_COUNT];

const struct theme *theme_current(void);
const struct theme *theme_by_index(int idx);
int  theme_index(void);                     /* THEME_LIGHT or THEME_DARK      */
void theme_set(int idx);
void theme_toggle(void);                    /* Light <-> Dark                 */

/* --- Window chrome ---------------------------------------------------------- */
/* Rounded "macOS" card with a title bar and traffic lights. The corners are
 * carved out with t->desktop-coloured pixels, so windows look right only on
 * the plain desktop fill (which is all agos needs today). */
void ui_draw_window(const struct theme *t, int x, int y, int w, int h,
                    const char *title);

/* Traffic-light buttons (decorative), anchored at the window's top-left. */
void ui_draw_traffic_lights(const struct theme *t, int x, int y);

/* iOS-style toggle switch: 44x24 capsule track + white knob. `on` colours
 * the track with the accent. Purely visual state indicator. */
void ui_draw_toggle(const struct theme *t, int x, int y, bool on);

/* Horizontal slider: track + filled portion + square knob. value in [0,100]. */
void ui_draw_slider(const struct theme *t, int x, int y, int w, int value);

#endif /* AGOS_THEME_H */
