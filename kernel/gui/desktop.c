/* =============================================================================
 *  AGOS - GUI shell implementation (gui/desktop.c) — see desktop.h for the
 *  menu bar / Settings window / Tab-toggles-theme contract.
 * ========================================================================== */
#include "desktop.h"
#include "gfx.h"
#include "font.h"
#include "lib.h"

/* ---------------------------------------------------------------------------
 *  Shell state
 * ------------------------------------------------------------------------ */
#define TYPED_MAX 60

static struct {
    bool  settings_open;                    /* Settings window visible        */
    int   appearance_row;                   /* 0 = Light, 1 = Dark selected   */
    int   brightness;                       /* fake slider value 0..100       */
    char  typed[TYPED_MAX + 1];
    size_t typed_len;
    struct desktop_status st;
} ds = {
    .settings_open = true,
    .appearance_row = 0,
    .brightness = 80,
    .st = { "--:--:--", "-", "-", "-" },
};

void desktop_set_status(const struct desktop_status *st)
{
    ds.st = *st;
}

/* ---------------------------------------------------------------------------
 *  Menu Bar (24 px tall, full width)
 * ------------------------------------------------------------------------ */
static void draw_menubar(const struct theme *t, int W, const char *clock)
{
    gfx_fill_rect(0, 0, W, MENUBAR_H, t->menubar);
    gfx_fill_rect(0, MENUBAR_H, W, 1, t->border);          /* hairline shadow */

    /* "logo": a filled disc standing in for the apple mark. */
    gfx_fill_rect(10, 7, 10, 10, t->menubar_text);
    gfx_draw_string(28, 8, "AGOS", t->menubar_text, 1);

    static const char *menus[] = { "File", "Edit", "View", "Window", "Help" };
    int x = 28 + gfx_string_width("AGOS", 1) + 20;

    for (unsigned i = 0; i < sizeof(menus) / sizeof(menus[0]); i++) {
        gfx_draw_string(x, 8, menus[i], t->menubar_text, 1);
        x += gfx_string_width(menus[i], 1) + 18;
    }

    /* Right side: theme name + clock */
    if (clock && clock[0])
        gfx_draw_string(W - 12 - gfx_string_width(clock, 1), 8, clock,
                        t->menubar_text, 1);
    {
        const char *tn = t->name;
        int tw = gfx_string_width(tn, 1);
        gfx_fill_rect(W - 12 - gfx_string_width(clock ? clock : "", 1) - tw - 24,
                      5, tw + 12, 14, t->card_alt);
        gfx_stroke_rect(W - 12 - gfx_string_width(clock ? clock : "", 1) - tw - 24,
                        5, tw + 12, 14, t->border);
        gfx_draw_string(W - 12 - gfx_string_width(clock ? clock : "", 1) - tw - 18,
                        8, tn, t->subtext, 1);
    }
}

/* ---------------------------------------------------------------------------
 *  Settings window ("Appearance" pane, macOS Ventura style)
 * ------------------------------------------------------------------------ */
static void draw_settings(const struct theme *t, int W, int H)
{
    int w = 420, h = 300;
    int x = (W - w) / 2;
    int y = MENUBAR_H + 24;

    if (!ds.settings_open || h > H - y - 8)
        return;

    ui_draw_window(t, x, y, w, h, "System Settings");

    /* Sidebar */
    gfx_fill_rect(x + 1, y + 24, 120, h - 25, t->card_alt);
    gfx_fill_rect(x + 121, y + 24, 1, h - 25, t->border);
    static const char *side[] = { "General", "Appearance", "Desktop" };
    for (int i = 0; i < 3; i++) {
        int ry = y + 40 + i * 26;
        if (i == 1) {                       /* selected row = accent pill     */
            gfx_fill_rect(x + 8, ry - 4, 106, 20, t->accent);
            gfx_draw_string(x + 16, ry, side[i], 0xFFFFFF, 1);
        } else {
            gfx_draw_string(x + 16, ry, side[i], t->text, 1);
        }
    }

    /* Content: Appearance */
    int cx = x + 140;
    gfx_draw_string(cx, y + 40, "Appearance:", t->text, 1);

    /* Two swatch cards: Light | Dark; selected gets an accent ring. */
    for (int i = 0; i < 2; i++) {
        int sx = cx + i * 96;
        int sy = y + 60;
        const struct theme *sw = &themes[i];

        gfx_fill_rect(sx, sy, 80, 52, sw->desktop);
        gfx_fill_rect(sx, sy, 80, 10, sw->menubar);
        gfx_fill_rect(sx + 8, sy + 18, 64, 26, sw->card);
        gfx_stroke_rect(sx + 8, sy + 18, 64, 26, sw->border);
        gfx_stroke_rect(sx - 1, sy - 1, 82, 54, sw->border);
        if (theme_index() == i)
            gfx_stroke_rect(sx - 3, sy - 3, 86, 58, t->accent);
        gfx_draw_string(sx + (80 - gfx_string_width(sw->name, 1)) / 2,
                        sy + 56, sw->name, t->subtext, 1);
    }

    /* Toggle row: "Dark Mode" switch mirrors the current theme. */
    gfx_draw_string(cx, y + 148, "Dark Mode", t->text, 1);
    ui_draw_toggle(t, cx + 200, y + 140, theme_index() == THEME_DARK);

    /* Slider row: "Brightness" (decorative). */
    gfx_draw_string(cx, y + 188, "Brightness", t->text, 1);
    ui_draw_slider(t, cx + 90, y + 192, 150, ds.brightness);

    /* Status block under the window content */
    gfx_draw_string(cx, y + 224, ds.st.uptime,  t->subtext, 1);
    gfx_draw_string(cx, y + 240, ds.st.ram,     t->subtext, 1);
    gfx_draw_string(cx, y + 256, ds.st.display, t->subtext, 1);
    gfx_draw_string(cx, y + 272, ds.st.last_key, t->subtext, 1);
}

/* ---------------------------------------------------------------------------
 *  Dock strip
 * ------------------------------------------------------------------------ */
static void draw_dock(const struct theme *t, int W, int H)
{
    const int n = 4, icon = 28, gap = 10;
    int total = n * icon + (n - 1) * gap + 24;
    int x = (W - total) / 2;
    int y = H - icon - 20;

    gfx_fill_rect(x, y - 8, total, icon + 16, t->card);
    gfx_stroke_rect(x, y - 8, total, icon + 16, t->border);
    for (int i = 0; i < n; i++) {
        static const uint32_t cols[4] = { 0x007AFF, 0x34C759, 0xFF9F0A, 0xFF3B30 };
        gfx_fill_rect(x + 12 + i * (icon + gap), y, icon, icon, cols[i]);
    }
}

/* ---------------------------------------------------------------------------
 *  Frame
 * ------------------------------------------------------------------------ */
void desktop_render(void)
{
    const struct theme *t = theme_current();
    const int W = gfx_width(), H = gfx_height();

    gfx_clear(t->desktop);
    draw_menubar(t, W, ds.st.uptime);
    draw_settings(t, W, H);
    draw_dock(t, W, H);

    /* Hint line, bottom-left above the dock */
    gfx_draw_string(12, H - 60, "Tab: theme   Enter: settings   Type: echo",
                    t->subtext, 1);
    if (ds.typed_len)
        gfx_draw_string(12, H - 44, ds.typed, t->text, 1);

    gfx_present();                          /* back buffer -> VRAM / FB       */
}

/* ---------------------------------------------------------------------------
 *  Input
 * ------------------------------------------------------------------------ */
bool desktop_handle_key(const struct key_event *ev)
{
    if (!ev->pressed)
        return false;

    /* Tab (scancode set-1 0x0F): flip Light/Dark and repaint everything. */
    if (ev->keycode == '\t' || ev->scancode == 0x0F) {
        theme_toggle();
        ds.appearance_row = theme_index();
        return true;
    }

    if (ev->keycode == '\n') {              /* Enter shows/hides Settings     */
        ds.settings_open = !ds.settings_open;
        return true;
    }

    if (ev->keycode >= 0x20 && ev->keycode < 0x7F) {
        if (ds.typed_len < TYPED_MAX) {
            ds.typed[ds.typed_len++] = (char)ev->keycode;
            ds.typed[ds.typed_len] = '\0';
        }
        return true;
    }
    if (ev->keycode == '\b') {
        if (ds.typed_len)
            ds.typed[--ds.typed_len] = '\0';
        return true;
    }
    return false;
}
