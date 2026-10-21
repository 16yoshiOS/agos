/* =============================================================================
 *  AGOS - Themes implementation (gui/theme.c)
 * ========================================================================== */
#include "theme.h"
#include "gfx.h"
#include "font.h"

const struct theme themes[THEME_COUNT] = {
    /* Light */ {
        "Light",
        0xE8E8E8,   /* desktop      */
        0xF6F6F6,   /* menubar      */
        0x262629,   /* menubar_text */
        0x262629,   /* text         */
        0x6E6E73,   /* subtext      */
        0xFFFFFF,   /* card         */
        0xF2F2F7,   /* card_alt     */
        0xCFCFD4,   /* border       */
        0x007AFF,   /* accent       */
        0xD8D8DC,   /* toggle_off   */
        0x0A5BFF,   /* link         */
        0xEDEDF2,   /* button       */
        0x1C1C1E,   /* button_text  */
    },
    /* Dark  */ {
        "Dark",
        0x1E1E20,
        0x2C2C2E,
        0xEBEBF0,
        0xEBEBF0,
        0x98989D,
        0x2C2C2E,
        0x3A3A3C,
        0x48484A,
        0x0A84FF,
        0x48484A,
        0x409CFF,
        0x48484A,
        0xF2F2F7,
    },
};

static int g_theme;                         /* index into themes[]            */

const struct theme *theme_current(void)     { return &themes[g_theme]; }
const struct theme *theme_by_index(int idx) { return &themes[idx & 1]; }
int  theme_index(void)                      { return g_theme; }
void theme_set(int idx)                     { g_theme = idx & 1; }
void theme_toggle(void)                     { g_theme ^= 1; }

/* --- Window chrome ------------------------------------------------------------ */

/* Filled disc of the given diameter (drawn row by row with an integer circle
 * equation; diameters here are small so the sqrt-free loop is plenty fast). */
static void fill_disc(int cx, int cy, int d, uint32_t rgb)
{
    int rad = d / 2;

    for (int dy = -rad; dy <= rad; dy++) {
        int half = 0;

        while ((half + 1) * (half + 1) + dy * dy <= rad * rad)
            half++;
        gfx_fill_rect(cx - half, cy + dy, half * 2 + 1, 1, rgb);
    }
}

void ui_draw_traffic_lights(const struct theme *t, int x, int y)
{
    static const uint32_t c[3] = { 0xFF5F57, 0xFEBC2E, 0x28C840 };

    (void)t;
    for (int i = 0; i < 3; i++)
        fill_disc(x + 5 + i * 18, y + 5, 11, c[i]);
}

void ui_draw_window(const struct theme *t, int x, int y, int w, int h,
                    const char *title)
{
    const int r = 8;                        /* corner radius                  */

    gfx_fill_rect(x, y, w, h, t->card);

    /* Frame: four straight edges, shortened so the corners stay rounded. */
    gfx_fill_rect(x + r,      y,         w - 2 * r, 1, t->border);
    gfx_fill_rect(x + r,      y + h - 1, w - 2 * r, 1, t->border);
    gfx_fill_rect(x,          y + r,     1, h - 2 * r, t->border);
    gfx_fill_rect(x + w - 1,  y + r,     1, h - 2 * r, t->border);

    /* Corner arcs: carve a staircase of desktop-coloured pixels out of the
     * card body and frame. Cheap pseudo-anti-aliasing without blending. */
    for (int dy = 0; dy < r; dy++) {
        int dx = r - 1 - dy;                /* px trimmed from each side edge */

        gfx_fill_rect(x + dx,      y + dy,      1, 1, t->desktop);
        gfx_fill_rect(x + w - 1 - dx, y + dy,   1, 1, t->desktop);
        gfx_fill_rect(x + dx,      y + h - 1 - dy, 1, 1, t->desktop);
        gfx_fill_rect(x + w - 1 - dx, y + h - 1 - dy, 1, 1, t->desktop);
    }

    /* Title bar strip + centred title */
    gfx_fill_rect(x + 1, y + 1, w - 2, 22, t->card_alt);
    if (title) {
        int tw = gfx_string_width(title, 1);
        gfx_draw_string(x + (w - tw) / 2, y + 8, title, t->subtext, 1);
    }
    gfx_fill_rect(x + 1, y + 23, w - 2, 1, t->border);
    ui_draw_traffic_lights(t, x + 10, y + 7);
}

void ui_draw_toggle(const struct theme *t, int x, int y, bool on)
{
    const int W = 44, H = 24;
    const int rad = H / 2;                  /* cap radius of the capsule      */

    /* Capsule track: per-row horizontal inset from the round ends. */
    for (int row = 0; row < H; row++) {
        int dy = row - rad + 1;             /* distance from cap centre       */
        int trim = 0;

        if (dy > 0)
            dy = H - 1 - row - rad;         /* mirror to the bottom cap       */
        if (dy < -rad) dy = -rad;
        /* largest dx with dx^2 + dy^2 <= rad^2 -> inset = rad - dx */
        {
            int half = 0;
            int ady = dy < 0 ? -dy : dy;

            while ((half + 1) * (half + 1) + ady * ady <= rad * rad)
                half++;
            trim = rad - half;
        }
        if (trim > W / 2) trim = W / 2;
        gfx_fill_rect(x + trim, y + row, W - 2 * trim, 1,
                      on ? t->accent : t->toggle_off);
    }

    /* Knob: white disc riding on the right/left end. */
    fill_disc(on ? x + W - rad - 1 : x + rad, y + H / 2, H - 6, 0xFFFFFF);
}

void ui_draw_slider(const struct theme *t, int x, int y, int w, int value)
{
    const int TH = 4;                       /* track height                   */
    const int KW = 12, KH = 14;             /* knob size                      */
    int v = value;
    int knob;

    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    knob = (w - KW) * v / 100;

    gfx_fill_rect(x, y, w, TH, t->toggle_off);
    gfx_fill_rect(x, y, knob + KW / 2, TH, t->accent);
    gfx_fill_rect(x + knob, y - (KH - TH) / 2, KW, KH, 0xFFFFFF);
    gfx_stroke_rect(x + knob, y - (KH - TH) / 2, KW, KH, t->border);
}
