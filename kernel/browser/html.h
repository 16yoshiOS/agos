/* =============================================================================
 *  AGOS - HTML 4.01 mini-engine (browser/html.h / html.c)
 *  -----------------------------------------------------------------------------
 *  Parses a text buffer and lays out a single-column flow of blocks:
 *    <a href="...">text</a>      -> inline link, accent colour + underline
 *    <button>text</button>       -> rounded rect with centred label
 *    <img src="..." >            -> placeholder box; when raw pixel data is
 *                                   attached (see image.h) it is blitted
 *    <p>, <h1>, plain text       -> word-wrapped paragraphs
 *  Unknown tags are skipped; malformed input can never run past the buffer.
 *
 *  The parser builds a flat array of render items (no dynamic tree): the
 *  browser walks the array once per frame and paints. Layout happens during
 *  parse so item boxes are final when html_parse() returns.
 * ========================================================================== */
#ifndef AGOS_HTML_H
#define AGOS_HTML_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "image.h"

enum {
    HTML_TEXT = 0,          /* paragraph run                        */
    HTML_HEADING,           /* h1-sized run                         */
    HTML_LINK,              /* <a>: text + href                     */
    HTML_BUTTON,            /* <button>: label                      */
    HTML_IMAGE,             /* <img>: decoded bitmap or placeholder */
};

#define HTML_MAX_ITEMS   96
#define HTML_MAX_REF     32         /* href / src attribute value   */
#define HTML_MAX_RUN    128         /* chars per text-ish item      */

struct html_item {
    uint8_t  type;
    char     text[HTML_MAX_RUN];
    char     ref[HTML_MAX_REF];
    int      x, y, w, h;            /* layout box (device px)       */
    const struct bitmap *bmp;       /* <img>: decoded pixels or NULL*/
};

struct html_doc {
    struct html_item items[HTML_MAX_ITEMS];
    int count;
    int content_h;                  /* laid-out height, px          */
};

/* Parse `html` (len bytes; need not be NUL-terminated) into `doc`.
 * Returns false only on internal overflow — garbage markup degrades to
 * plain text instead. */
bool html_parse(const char *html, size_t len, struct html_doc *doc);

/* Resolve <img src="..."> against the built-in asset table
 * (browser/assets.c). Returns NULL for unknown names. */
const struct bitmap *html_asset_lookup(const char *src);

#endif /* AGOS_HTML_H */
