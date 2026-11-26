/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe DVB TTML subtitle decoder
 *
 * The pipe takes the TTML documents of a DVB TTML subtitle stream, resolves
 * the styling and the timing of each of them, and outputs one styled text
 * page per intermediate synchronic document, dated on the MPEG timeline.
 *
 * Normative references:
 *  - ETSI EN 303 560 V1.1.1 (2018-05) (TTML subtitling systems)
 *  - EBU Tech 3380 v1.0 (EBU-TT-D subtitling distribution format)
 *  - W3C TTML Profiles for Internet Media Subtitles and Captions 1.0.1
 */

#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_text.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_uref_mgr.h"
#include "upipe-ts/uref_ts_ttml.h"
#include "upipe-ttml/upipe_ttml_dec.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

/** we only accept TTML documents */
#define EXPECTED_FLOW_DEF "block.ttml."

/** maximum period of activation of a segment (EN 303 560 5.2.3.3) */
#define TMPA (UCLOCK_FREQ * 5)
/** an element with no end has one far enough away to outlast any segment, and
 * near enough that adding a duration to it does not overflow */
#define TTML_UNBOUNDED (INT64_MAX / 2)
/** a subtitle stream shall have no more than four regions active at the same
 * time (EN 303 560 4.2.2) */
#define MAX_REGIONS 4
/** upper bound on the runs of a page, which is what the attribute index of
 * uref_text holds */
#define MAX_RUNS 255
/** upper bound on the intermediate synchronic documents of a segment */
#define MAX_ISDS 64
/** default cell resolution (TTML ttp:cellResolution) */
#define DEFAULT_CELL_COLUMNS 32
/** default cell resolution */
#define DEFAULT_CELL_ROWS 15
/** default tick rate when the document does not give one */
#define DEFAULT_TICK_RATE 1
/** referential styling chains no deeper than this */
#define MAX_STYLE_DEPTH 8
/** a document nested deeper than this is not a subtitle */
#define MAX_ELEMENT_DEPTH 16

/** @internal @This is a TTML length unit. */
enum ttml_unit {
    TTML_UNIT_PIXEL,
    TTML_UNIT_CELL,
    TTML_UNIT_PERCENT,
    TTML_UNIT_EM,
};

/** @internal @This is a TTML length. */
struct ttml_length {
    /** value in the unit below */
    double value;
    /** unit of the value */
    enum ttml_unit unit;
};

/** @internal @This is a set of style properties, with a bit per property that
 * was actually given.  Lengths that resolve against the inherited context are
 * kept resolved, in canvas pixels. */
struct ttml_style {
    /** properties that are set */
    uint32_t set;
    /** foreground colour as packed RGBA */
    uint32_t color;
    /** background colour as packed RGBA */
    uint32_t background;
    /** font size in canvas pixels */
    double font_size;
    /** line height in canvas pixels, 0 for the font default */
    double line_height;
    /** horizontal padding of the line backgrounds in canvas pixels */
    double line_padding;
    /** line alignment */
    uint8_t text_align;
    /** block alignment */
    uint8_t display_align;
    /** row alignment */
    uint8_t multi_row_align;
    /** whether the text is emboldened */
    bool bold;
    /** whether the text is sheared */
    bool italic;
    /** whether the text is underlined */
    bool underline;
    /** whether the line backgrounds cover the whole line height */
    bool fill_line_gap;
    /** whether the lines are wrapped */
    bool wrap;
    /** left and top of the region */
    struct ttml_length origin[2];
    /** width and height of the region */
    struct ttml_length extent[2];
    /** padding before, end, after and start */
    struct ttml_length padding[4];
};

#define TTML_PROP_COLOR         (UINT32_C(1) << 0)
#define TTML_PROP_BACKGROUND    (UINT32_C(1) << 1)
#define TTML_PROP_FONT_SIZE     (UINT32_C(1) << 2)
#define TTML_PROP_LINE_HEIGHT   (UINT32_C(1) << 3)
#define TTML_PROP_LINE_PADDING  (UINT32_C(1) << 4)
#define TTML_PROP_TEXT_ALIGN    (UINT32_C(1) << 5)
#define TTML_PROP_DISPLAY_ALIGN (UINT32_C(1) << 6)
#define TTML_PROP_MULTI_ROW     (UINT32_C(1) << 7)
#define TTML_PROP_BOLD          (UINT32_C(1) << 8)
#define TTML_PROP_ITALIC        (UINT32_C(1) << 9)
#define TTML_PROP_UNDERLINE     (UINT32_C(1) << 10)
#define TTML_PROP_FILL_LINE_GAP (UINT32_C(1) << 11)
#define TTML_PROP_WRAP          (UINT32_C(1) << 12)
#define TTML_PROP_ORIGIN        (UINT32_C(1) << 13)
#define TTML_PROP_EXTENT        (UINT32_C(1) << 14)
#define TTML_PROP_PADDING       (UINT32_C(1) << 15)

/** properties a content element inherits from its parent; a background, a
 * padding and the geometry of a region are not among them */
#define TTML_PROP_INHERITED                                                 \
    (TTML_PROP_COLOR | TTML_PROP_FONT_SIZE | TTML_PROP_LINE_HEIGHT |        \
     TTML_PROP_LINE_PADDING | TTML_PROP_TEXT_ALIGN | TTML_PROP_MULTI_ROW |  \
     TTML_PROP_BOLD | TTML_PROP_ITALIC | TTML_PROP_UNDERLINE |              \
     TTML_PROP_FILL_LINE_GAP | TTML_PROP_WRAP)

/** @internal @This is a named style of the document. */
struct ttml_named_style {
    /** value of xml:id, owned */
    char *id;
    /** element it was parsed from */
    xmlNodePtr node;
};

/** @internal @This is a region of the document. */
struct ttml_region {
    /** value of xml:id, owned, or NULL for the region content with no region
     * attribute lands in */
    char *id;
    /** resolved properties */
    struct ttml_style style;
    /** left of the text area in canvas pixels */
    int x;
    /** top of the text area in canvas pixels */
    int y;
    /** width of the text area in canvas pixels */
    int hsize;
    /** height of the text area in canvas pixels */
    int vsize;
};

/** @internal @This is the document being decoded. */
struct ttml_document {
    /** parsed document */
    xmlDocPtr doc;
    /** frame rate numerator, for time expressions in frames */
    unsigned frame_rate_num;
    /** frame rate denominator */
    unsigned frame_rate_den;
    /** tick rate, for time expressions in ticks */
    unsigned tick_rate;
    /** cell resolution columns */
    unsigned cell_columns;
    /** cell resolution rows */
    unsigned cell_rows;
    /** width the document was authored against, for lengths in pixels */
    double authored_hsize;
    /** height the document was authored against */
    double authored_vsize;
    /** width of the root container in canvas pixels */
    int root_hsize;
    /** height of the root container in canvas pixels */
    int root_vsize;
    /** left of the root container in canvas pixels */
    int root_x;
    /** top of the root container in canvas pixels */
    int root_y;
    /** named styles */
    struct ttml_named_style *styles;
    /** number of named styles */
    unsigned nb_styles;
    /** regions */
    struct ttml_region *regions;
    /** number of regions */
    unsigned nb_regions;
};

/** @internal @This is a run of the page being built. */
struct ttml_run {
    /** text, owned */
    char *text;
    /** length of the text */
    size_t text_len;
    /** octets allocated for the text */
    size_t text_size;
    /** font size in canvas pixels */
    unsigned font_size;
    /** foreground colour as packed RGBA */
    uint32_t color;
    /** background colour as packed RGBA */
    uint32_t background;
    /** whether the background is set */
    bool has_background;
    /** style flags of @ref uref_text.h */
    uint8_t flags;
    /** region the run belongs to */
    unsigned region;
    /** properties of the paragraph, which the renderer applies per region */
    struct ttml_style paragraph;
};

/** @internal @This is the state of the page being built. */
struct ttml_page {
    /** runs collected so far */
    struct ttml_run *runs;
    /** number of runs */
    unsigned nb_runs;
    /** whether the next run starts a line */
    bool break_pending;
};

/** @internal @This is the private context of a ttmld pipe. */
struct upipe_ttmld {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** runs of the page being built, allocated with the pipe: a page is
     * bounded by MAX_RUNS, so building one allocates none of its own */
    struct ttml_run *runs;

    /** width of the canvas */
    uint64_t hsize;
    /** height of the canvas */
    uint64_t vsize;

    /** public upipe structure */
    struct upipe upipe;
};


UPIPE_HELPER_UPIPE(upipe_ttmld, upipe, UPIPE_TTMLD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ttmld, urefcount, upipe_ttmld_free)
UPIPE_HELPER_VOID(upipe_ttmld)
UPIPE_HELPER_OUTPUT(upipe_ttmld, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_ttmld, uref_mgr, uref_mgr_request, NULL,
                      upipe_ttmld_register_output_request,
                      upipe_ttmld_unregister_output_request)

/*
 * Attribute and value parsing
 */

/** @internal @This returns an attribute of an element by local name.
 *
 * TTML puts most attributes in one of several namespaces (tts, ttp, itts,
 * ebutts) that no two of them share a local name across, so matching the
 * local name saves carrying the namespace URIs around.
 *
 * @param node element
 * @param name local name of the attribute
 * @return the value, to be released with @ref xmlFree, or NULL
 */
static xmlChar *ttml_get_attr(xmlNodePtr node, const char *name)
{
    for (xmlAttrPtr attr = node->properties; attr != NULL; attr = attr->next)
        if (attr->name != NULL && !strcmp((const char *)attr->name, name))
            return xmlNodeListGetString(node->doc, attr->children, 1);
    return NULL;
}

/** @internal @This tells whether an element has a given name.
 *
 * @param node element
 * @param name local name
 * @return true if the names match
 */
static bool ttml_is(xmlNodePtr node, const char *name)
{
    return node->type == XML_ELEMENT_NODE && node->name != NULL &&
        !strcmp((const char *)node->name, name);
}

/** @internal @This parses a TTML length.
 *
 * @param str value to parse
 * @param length filled in with the parsed length
 * @return true if the value was parsed
 */
static bool ttml_parse_length(const char *str, struct ttml_length *length)
{
    char *end;
    double value = strtod(str, &end);
    if (end == str)
        return false;

    while (*end == ' ')
        end++;

    if (!strncmp(end, "px", 2))
        length->unit = TTML_UNIT_PIXEL;
    else if (*end == 'c')
        length->unit = TTML_UNIT_CELL;
    else if (*end == '%')
        length->unit = TTML_UNIT_PERCENT;
    else if (!strncmp(end, "em", 2))
        length->unit = TTML_UNIT_EM;
    else if (*end == '\0' || *end == ' ')
        length->unit = TTML_UNIT_PIXEL;
    else
        return false;

    length->value = value;
    return true;
}

/** @internal @This parses a pair of TTML lengths, the second defaulting to the
 * first.
 *
 * @param str value to parse
 * @param lengths filled in with the two parsed lengths
 * @return true if the first value was parsed
 */
static bool ttml_parse_lengths(const char *str, struct ttml_length lengths[2])
{
    if (!ttml_parse_length(str, &lengths[0]))
        return false;

    const char *second = str;
    while (*second == ' ')
        second++;
    while (*second != '\0' && *second != ' ')
        second++;
    while (*second == ' ')
        second++;

    if (*second == '\0' || !ttml_parse_length(second, &lengths[1]))
        lengths[1] = lengths[0];
    return true;
}

/** @internal @This resolves a TTML length to canvas pixels.
 *
 * @param length length to resolve
 * @param document document the length belongs to
 * @param percent_ref reference for a percentage
 * @param em_ref reference for an em
 * @param vertical whether the length is along the vertical axis
 * @return the length in canvas pixels
 */
static double ttml_resolve_length(const struct ttml_length *length,
                                  const struct ttml_document *document,
                                  double percent_ref, double em_ref,
                                  bool vertical)
{
    switch (length->unit) {
        case TTML_UNIT_PIXEL:
            /* a pixel of the raster the document was authored against */
            return length->value * (vertical ?
                    document->root_vsize / document->authored_vsize :
                    document->root_hsize / document->authored_hsize);
        case TTML_UNIT_CELL:
            return length->value * (vertical ?
                    (double)document->root_vsize / document->cell_rows :
                    (double)document->root_hsize / document->cell_columns);
        case TTML_UNIT_PERCENT:
            return length->value * percent_ref / 100;
        case TTML_UNIT_EM:
            return length->value * em_ref;
    }
    return 0;
}

/** @internal @This parses a TTML colour expression.
 *
 * @param str value to parse
 * @param rgba_p filled in with the colour as packed RGBA
 * @return true if the value was parsed
 */
static bool ttml_parse_color(const char *str, uint32_t *rgba_p)
{
    static const struct {
        const char *name;
        uint32_t rgba;
    } named[] = {
        { "transparent", 0x00000000 },
        { "black",       0x000000ff },
        { "silver",      0xc0c0c0ff },
        { "gray",        0x808080ff },
        { "grey",        0x808080ff },
        { "white",       0xffffffff },
        { "maroon",      0x800000ff },
        { "red",         0xff0000ff },
        { "purple",      0x800080ff },
        { "fuchsia",     0xff00ffff },
        { "magenta",     0xff00ffff },
        { "green",       0x008000ff },
        { "lime",        0x00ff00ff },
        { "olive",       0x808000ff },
        { "yellow",      0xffff00ff },
        { "navy",        0x000080ff },
        { "blue",        0x0000ffff },
        { "teal",        0x008080ff },
        { "aqua",        0x00ffffff },
        { "cyan",        0x00ffffff },
    };

    while (*str == ' ')
        str++;

    if (*str == '#') {
        const char *digits = str + 1;
        size_t len = strspn(digits, "0123456789abcdefABCDEF");
        if (len != 6 && len != 8)
            return false;
        uint32_t v = strtoul(digits, NULL, 16);
        *rgba_p = len == 6 ? (v << 8) | 0xff : v;
        return true;
    }

    if (!strncmp(str, "rgb", 3)) {
        bool alpha = str[3] == 'a';
        unsigned r, g, b, a = 255;
        int n = alpha ?
            sscanf(str + 4, " ( %u , %u , %u , %u )", &r, &g, &b, &a) :
            sscanf(str + 3, " ( %u , %u , %u )", &r, &g, &b);
        if (n < (alpha ? 4 : 3))
            return false;
        *rgba_p = ((r & 0xff) << 24) | ((g & 0xff) << 16) |
                  ((b & 0xff) << 8) | (a & 0xff);
        return true;
    }

    for (size_t i = 0; i < UBASE_ARRAY_SIZE(named); i++)
        if (!strcasecmp(str, named[i].name)) {
            *rgba_p = named[i].rgba;
            return true;
        }

    return false;
}

/** @internal @This parses a TTML time expression.
 *
 * @param str value to parse
 * @param document document the expression belongs to
 * @param time_p filled in with the time in @ref UCLOCK_FREQ units
 * @return true if the value was parsed
 */
static bool ttml_parse_time(const char *str,
                            const struct ttml_document *document,
                            int64_t *time_p)
{
    unsigned hh, mm;
    double ss;

    while (*str == ' ')
        str++;

    /* clock-time: hh:mm:ss[.fraction], or hh:mm:ss:frames when a fourth
     * colon separated field follows the seconds */
    if (sscanf(str, "%u:%u:%lf", &hh, &mm, &ss) == 3) {
        double seconds = hh * 3600.0 + mm * 60.0 + ss;

        const char *p = strchr(str, ':');
        p = p != NULL ? strchr(p + 1, ':') : NULL;
        const char *frames = p != NULL ? strchr(p + 1, ':') : NULL;
        if (frames != NULL && document->frame_rate_num)
            seconds = hh * 3600.0 + mm * 60.0 + (unsigned)ss +
                strtod(frames + 1, NULL) * document->frame_rate_den /
                document->frame_rate_num;

        *time_p = (int64_t)(seconds * UCLOCK_FREQ);
        return true;
    }

    /* offset-time: a number followed by a metric */
    char *end;
    double value = strtod(str, &end);
    if (end == str)
        return false;
    while (*end == ' ')
        end++;

    double seconds;
    if (!strncmp(end, "ms", 2))
        seconds = value / 1000;
    else if (*end == 'h')
        seconds = value * 3600;
    else if (*end == 'm')
        seconds = value * 60;
    else if (*end == 's' || *end == '\0')
        seconds = value;
    else if (*end == 'f')
        seconds = document->frame_rate_num ?
            value * document->frame_rate_den / document->frame_rate_num : 0;
    else if (*end == 't')
        seconds = document->tick_rate ? value / document->tick_rate : 0;
    else
        return false;

    *time_p = (int64_t)(seconds * UCLOCK_FREQ);
    return true;
}

/*
 * Style resolution
 */

/** @internal @This applies the style attributes of an element.
 *
 * A length that resolves against the inherited context is resolved here, so
 * the style holds the font size the element computes to and a percentage on a
 * child resolves against it.
 *
 * @param document document the element belongs to
 * @param node element to read the attributes of
 * @param style style to apply them to
 */
static void ttml_apply_attrs(const struct ttml_document *document,
                             xmlNodePtr node, struct ttml_style *style)
{
    xmlChar *value;
    double cell = (double)document->root_vsize / document->cell_rows;

#define GET(Name) (value = ttml_get_attr(node, Name)) != NULL

    if (GET("color")) {
        if (ttml_parse_color((const char *)value, &style->color))
            style->set |= TTML_PROP_COLOR;
        xmlFree(value);
    }
    if (GET("backgroundColor")) {
        if (ttml_parse_color((const char *)value, &style->background))
            style->set |= TTML_PROP_BACKGROUND;
        xmlFree(value);
    }
    if (GET("fontSize")) {
        struct ttml_length lengths[2];
        if (ttml_parse_lengths((const char *)value, lengths)) {
            /* only the vertical size is used: the renderer has one font */
            double size = ttml_resolve_length(&lengths[1], document,
                    style->font_size ?: cell, style->font_size ?: cell, true);
            if (size > 0) {
                style->font_size = size;
                style->set |= TTML_PROP_FONT_SIZE;
            }
        }
        xmlFree(value);
    }
    if (GET("lineHeight")) {
        struct ttml_length length;
        if (!strcasecmp((const char *)value, "normal")) {
            style->line_height = 0;
            style->set |= TTML_PROP_LINE_HEIGHT;
        } else if (ttml_parse_length((const char *)value, &length)) {
            style->line_height = ttml_resolve_length(&length, document,
                    style->font_size ?: cell, style->font_size ?: cell, true);
            style->set |= TTML_PROP_LINE_HEIGHT;
        }
        xmlFree(value);
    }
    if (GET("linePadding")) {
        struct ttml_length length;
        if (ttml_parse_length((const char *)value, &length)) {
            style->line_padding = ttml_resolve_length(&length, document,
                    document->root_hsize, style->font_size ?: cell, false);
            style->set |= TTML_PROP_LINE_PADDING;
        }
        xmlFree(value);
    }
    if (GET("textAlign")) {
        const char *v = (const char *)value;
        style->set |= TTML_PROP_TEXT_ALIGN;
        if (!strcmp(v, "center"))
            style->text_align = UREF_TEXT_ALIGN_CENTER;
        else if (!strcmp(v, "right") || !strcmp(v, "end"))
            style->text_align = UREF_TEXT_ALIGN_END;
        else
            style->text_align = UREF_TEXT_ALIGN_START;
        xmlFree(value);
    }
    if (GET("displayAlign")) {
        const char *v = (const char *)value;
        style->set |= TTML_PROP_DISPLAY_ALIGN;
        if (!strcmp(v, "center"))
            style->display_align = UREF_TEXT_DISPLAY_ALIGN_CENTER;
        else if (!strcmp(v, "after"))
            style->display_align = UREF_TEXT_DISPLAY_ALIGN_AFTER;
        else
            style->display_align = UREF_TEXT_DISPLAY_ALIGN_BEFORE;
        xmlFree(value);
    }
    if (GET("multiRowAlign")) {
        const char *v = (const char *)value;
        style->set |= TTML_PROP_MULTI_ROW;
        if (!strcmp(v, "start"))
            style->multi_row_align = UREF_TEXT_MULTI_ROW_ALIGN_START;
        else if (!strcmp(v, "center"))
            style->multi_row_align = UREF_TEXT_MULTI_ROW_ALIGN_CENTER;
        else if (!strcmp(v, "end"))
            style->multi_row_align = UREF_TEXT_MULTI_ROW_ALIGN_END;
        else
            style->multi_row_align = UREF_TEXT_MULTI_ROW_ALIGN_AUTO;
        xmlFree(value);
    }
    if (GET("fontWeight")) {
        style->bold = !strcmp((const char *)value, "bold");
        style->set |= TTML_PROP_BOLD;
        xmlFree(value);
    }
    if (GET("fontStyle")) {
        style->italic = !strcmp((const char *)value, "italic") ||
                        !strcmp((const char *)value, "oblique");
        style->set |= TTML_PROP_ITALIC;
        xmlFree(value);
    }
    if (GET("textDecoration")) {
        style->underline = strstr((const char *)value, "underline") != NULL;
        style->set |= TTML_PROP_UNDERLINE;
        xmlFree(value);
    }
    if (GET("fillLineGap")) {
        style->fill_line_gap = !strcmp((const char *)value, "true");
        style->set |= TTML_PROP_FILL_LINE_GAP;
        xmlFree(value);
    }
    if (GET("wrapOption")) {
        style->wrap = strcmp((const char *)value, "noWrap") != 0;
        style->set |= TTML_PROP_WRAP;
        xmlFree(value);
    }
    if (GET("origin")) {
        if (ttml_parse_lengths((const char *)value, style->origin))
            style->set |= TTML_PROP_ORIGIN;
        xmlFree(value);
    }
    if (GET("extent")) {
        if (ttml_parse_lengths((const char *)value, style->extent))
            style->set |= TTML_PROP_EXTENT;
        xmlFree(value);
    }
    if (GET("padding")) {
        /* one, two or four lengths, before end after start */
        struct ttml_length p[4];
        unsigned n = 0;
        const char *v = (const char *)value;
        while (n < 4 && *v != '\0') {
            if (!ttml_parse_length(v, &p[n]))
                break;
            n++;
            while (*v != '\0' && *v != ' ')
                v++;
            while (*v == ' ')
                v++;
        }
        if (n == 1)
            style->padding[0] = style->padding[1] =
                style->padding[2] = style->padding[3] = p[0];
        else if (n == 2) {
            style->padding[0] = style->padding[2] = p[0];
            style->padding[1] = style->padding[3] = p[1];
        } else if (n == 4)
            memcpy(style->padding, p, sizeof (p));
        if (n == 1 || n == 2 || n == 4)
            style->set |= TTML_PROP_PADDING;
        xmlFree(value);
    }

#undef GET
}

/** @internal @This applies the styles an element references, then its own
 * style attributes.
 *
 * The referenced styles are applied onto the inherited context rather than
 * resolved on their own, so a font size given as a percentage in a style
 * element resolves against the size the referencing element inherits.
 *
 * @param upipe description structure of the pipe
 * @param document document the element belongs to
 * @param node element
 * @param style style to apply them to
 * @param depth referential styling depth
 */
static void ttml_apply_style(struct upipe *upipe,
                             const struct ttml_document *document,
                             xmlNodePtr node, struct ttml_style *style,
                             unsigned depth)
{
    if (depth > MAX_STYLE_DEPTH) {
        upipe_warn(upipe, "referential styling is too deep");
        return;
    }

    xmlChar *refs = ttml_get_attr(node, "style");
    if (refs != NULL) {
        const char *id = (const char *)refs;
        while (*id != '\0') {
            while (*id == ' ' || *id == '\t' || *id == '\n' || *id == '\r')
                id++;
            const char *end = id;
            while (*end != '\0' && *end != ' ' && *end != '\t' &&
                   *end != '\n' && *end != '\r')
                end++;
            size_t len = end - id;

            for (unsigned i = 0; len && i < document->nb_styles; i++) {
                const struct ttml_named_style *named = &document->styles[i];
                if (named->id == NULL || strlen(named->id) != len ||
                    memcmp(named->id, id, len))
                    continue;
                ttml_apply_style(upipe, document, named->node, style,
                                 depth + 1);
                break;
            }
            id = end;
        }
        xmlFree(refs);
    }

    ttml_apply_attrs(document, node, style);
}

/*
 * Document parsing
 */

/** @internal @This reads the parameters of the tt element.
 *
 * @param upipe description structure of the pipe
 * @param document document to fill in
 * @param tt root element
 */
static void ttml_read_params(struct upipe *upipe,
                             struct ttml_document *document, xmlNodePtr tt)
{
    struct upipe_ttmld *upipe_ttmld = upipe_ttmld_from_upipe(upipe);
    xmlChar *value;

    document->frame_rate_num = 0;
    document->frame_rate_den = 1;
    document->tick_rate = DEFAULT_TICK_RATE;
    document->cell_columns = DEFAULT_CELL_COLUMNS;
    document->cell_rows = DEFAULT_CELL_ROWS;
    document->root_hsize = upipe_ttmld->hsize;
    document->root_vsize = upipe_ttmld->vsize;
    document->authored_hsize = upipe_ttmld->hsize;
    document->authored_vsize = upipe_ttmld->vsize;
    document->root_x = 0;
    document->root_y = 0;

    if ((value = ttml_get_attr(tt, "cellResolution")) != NULL) {
        unsigned columns, rows;
        if (sscanf((const char *)value, "%u %u", &columns, &rows) == 2 &&
            columns && rows) {
            document->cell_columns = columns;
            document->cell_rows = rows;
        }
        xmlFree(value);
    }

    if ((value = ttml_get_attr(tt, "frameRate")) != NULL) {
        unsigned rate = strtoul((const char *)value, NULL, 10);
        if (rate)
            document->frame_rate_num = rate;
        xmlFree(value);
    }

    if ((value = ttml_get_attr(tt, "frameRateMultiplier")) != NULL) {
        unsigned num, den;
        if (sscanf((const char *)value, "%u %u", &num, &den) == 2 &&
            num && den) {
            document->frame_rate_num *= num;
            document->frame_rate_den *= den;
        }
        xmlFree(value);
    }

    if ((value = ttml_get_attr(tt, "tickRate")) != NULL) {
        unsigned rate = strtoul((const char *)value, NULL, 10);
        if (rate)
            document->tick_rate = rate;
        xmlFree(value);
    } else if (document->frame_rate_num)
        /* TTML 6.2.10: with no tick rate but a frame rate, a tick is a frame */
        document->tick_rate =
            document->frame_rate_num / document->frame_rate_den;

    /* The root container is the whole canvas unless tts:extent says the
     * document was authored against a different raster, in which case it is
     * fitted to the canvas so the aspect ratio is kept. */
    if ((value = ttml_get_attr(tt, "extent")) != NULL) {
        struct ttml_length extent[2];
        if (ttml_parse_lengths((const char *)value, extent) &&
            extent[0].unit == TTML_UNIT_PIXEL &&
            extent[1].unit == TTML_UNIT_PIXEL &&
            extent[0].value >= 1 && extent[1].value >= 1) {
            double scale_h = upipe_ttmld->hsize / extent[0].value;
            double scale_v = upipe_ttmld->vsize / extent[1].value;
            double scale = scale_h < scale_v ? scale_h : scale_v;
            document->authored_hsize = extent[0].value;
            document->authored_vsize = extent[1].value;
            document->root_hsize = extent[0].value * scale;
            document->root_vsize = extent[1].value * scale;
            document->root_x = (upipe_ttmld->hsize - document->root_hsize) / 2;
            document->root_y = (upipe_ttmld->vsize - document->root_vsize) / 2;
        }
        xmlFree(value);
    }
}

/** @internal @This collects the named styles and the regions of a document.
 *
 * @param upipe description structure of the pipe
 * @param document document to fill in
 * @param head head element, or NULL
 * @return an error code
 */
static int ttml_read_head(struct upipe *upipe, struct ttml_document *document,
                          xmlNodePtr head)
{
    unsigned nb_styles = 0, nb_regions = 0;
    xmlNodePtr *region_nodes;

    if (head != NULL)
        for (xmlNodePtr n = head->children; n != NULL; n = n->next) {
            if (ttml_is(n, "styling")) {
                for (xmlNodePtr s = n->children; s != NULL; s = s->next)
                    nb_styles += ttml_is(s, "style");
            } else if (ttml_is(n, "layout")) {
                for (xmlNodePtr r = n->children; r != NULL; r = r->next)
                    nb_regions += ttml_is(r, "region");
            }
        }

    /* one spare region for content that names none */
    document->styles = nb_styles ?
        calloc(nb_styles, sizeof (*document->styles)) : NULL;
    document->regions = calloc(nb_regions + 1, sizeof (*document->regions));
    region_nodes = calloc(nb_regions + 1, sizeof (*region_nodes));
    if (unlikely((nb_styles && document->styles == NULL) ||
                 document->regions == NULL || region_nodes == NULL)) {
        free(region_nodes);
        return UBASE_ERR_ALLOC;
    }

    if (head != NULL)
        for (xmlNodePtr n = head->children; n != NULL; n = n->next) {
            if (ttml_is(n, "styling")) {
                for (xmlNodePtr s = n->children; s != NULL; s = s->next) {
                    if (!ttml_is(s, "style"))
                        continue;
                    struct ttml_named_style *named =
                        &document->styles[document->nb_styles++];
                    xmlChar *id = ttml_get_attr(s, "id");
                    named->id = id != NULL ? strdup((const char *)id) : NULL;
                    xmlFree(id);
                    named->node = s;
                }
            } else if (ttml_is(n, "layout")) {
                for (xmlNodePtr r = n->children; r != NULL; r = r->next) {
                    if (!ttml_is(r, "region"))
                        continue;
                    struct ttml_region *region =
                        &document->regions[document->nb_regions];
                    xmlChar *id = ttml_get_attr(r, "id");
                    region->id = id != NULL ? strdup((const char *)id) : NULL;
                    xmlFree(id);
                    region_nodes[document->nb_regions++] = r;
                }
            }
        }

    /* the region content with no region attribute lands in covers the whole
     * root container */
    region_nodes[document->nb_regions++] = NULL;

    double cell = (double)document->root_vsize / document->cell_rows;
    for (unsigned i = 0; i < document->nb_regions; i++) {
        struct ttml_region *region = &document->regions[i];
        struct ttml_style *style = &region->style;

        memset(style, 0, sizeof (*style));
        style->font_size = cell;
        if (region_nodes[i] != NULL)
            ttml_apply_style(upipe, document, region_nodes[i], style, 0);

        double x = 0, y = 0;
        double hsize = document->root_hsize;
        double vsize = document->root_vsize;
        if (style->set & TTML_PROP_ORIGIN) {
            x = ttml_resolve_length(&style->origin[0], document,
                                    document->root_hsize, cell, false);
            y = ttml_resolve_length(&style->origin[1], document,
                                    document->root_vsize, cell, true);
        }
        if (style->set & TTML_PROP_EXTENT) {
            hsize = ttml_resolve_length(&style->extent[0], document,
                                        document->root_hsize, cell, false);
            vsize = ttml_resolve_length(&style->extent[1], document,
                                        document->root_vsize, cell, true);
        }

        /* the padding is inside the region: what is left is the text area */
        double pad[4] = { 0, 0, 0, 0 };
        if (style->set & TTML_PROP_PADDING)
            for (unsigned j = 0; j < 4; j++)
                pad[j] = ttml_resolve_length(&style->padding[j], document,
                        (j & 1) ? hsize : vsize, cell, !(j & 1));

        x += pad[3];
        y += pad[0];
        hsize -= pad[1] + pad[3];
        vsize -= pad[0] + pad[2];

        region->x = document->root_x + (int)(x + 0.5);
        region->y = document->root_y + (int)(y + 0.5);
        if (region->x < 0)
            region->x = 0;
        if (region->y < 0)
            region->y = 0;
        region->hsize = hsize > 0 ? (int)(hsize + 0.5) : 0;
        region->vsize = vsize > 0 ? (int)(vsize + 0.5) : 0;
    }

    free(region_nodes);
    return UBASE_ERR_NONE;
}

/** @internal @This releases a document.
 *
 * @param document document to release
 */
static void ttml_clean(struct ttml_document *document)
{
    for (unsigned i = 0; i < document->nb_styles; i++)
        free(document->styles[i].id);
    for (unsigned i = 0; i < document->nb_regions; i++)
        free(document->regions[i].id);
    free(document->styles);
    free(document->regions);
    if (document->doc != NULL)
        xmlFreeDoc(document->doc);
}

/*
 * Timing
 */

/** @internal @This resolves the active interval of an element.
 *
 * The time containers of TTML content are parallel, so the times of an
 * element are relative to the beginning of its parent and clipped to it.
 *
 * @param document document the element belongs to
 * @param node element
 * @param parent_begin beginning of the parent interval
 * @param parent_end end of the parent interval
 * @param begin_p filled in with the beginning of the interval
 * @param end_p filled in with the end of the interval
 */
static void ttml_resolve_times(const struct ttml_document *document,
                               xmlNodePtr node,
                               int64_t parent_begin, int64_t parent_end,
                               int64_t *begin_p, int64_t *end_p)
{
    int64_t begin = parent_begin;
    int64_t end = parent_end;
    int64_t value;
    xmlChar *attr;

    if ((attr = ttml_get_attr(node, "begin")) != NULL) {
        if (ttml_parse_time((const char *)attr, document, &value))
            begin = parent_begin + value;
        xmlFree(attr);
    }

    if ((attr = ttml_get_attr(node, "end")) != NULL) {
        if (ttml_parse_time((const char *)attr, document, &value))
            end = parent_begin + value;
        xmlFree(attr);
    }

    if ((attr = ttml_get_attr(node, "dur")) != NULL) {
        if (ttml_parse_time((const char *)attr, document, &value) &&
            begin + value < end)
            end = begin + value;
        xmlFree(attr);
    }

    if (begin < parent_begin)
        begin = parent_begin;
    if (end > parent_end)
        end = parent_end;

    *begin_p = begin;
    *end_p = end < begin ? begin : end;
}

/** @internal @This adds a time to the sorted list of ISD boundaries.
 *
 * @param times sorted boundary list
 * @param nb_p number of boundaries, updated
 * @param max maximum number of boundaries
 * @param time time to add
 */
static void ttml_add_time(int64_t *times, unsigned *nb_p, unsigned max,
                          int64_t time)
{
    unsigned i;
    for (i = 0; i < *nb_p; i++) {
        if (times[i] == time)
            return;
        if (times[i] > time)
            break;
    }
    if (*nb_p >= max)
        return;
    memmove(times + i + 1, times + i, (*nb_p - i) * sizeof (*times));
    times[i] = time;
    (*nb_p)++;
}

/** @internal @This collects the ISD boundaries of a subtree.
 *
 * @param document document the element belongs to
 * @param node element
 * @param begin beginning of the parent interval
 * @param end end of the parent interval
 * @param window_begin beginning of the activation window
 * @param window_end end of the activation window
 * @param times sorted boundary list
 * @param nb_p number of boundaries, updated
 * @param max maximum number of boundaries
 * @param depth element depth
 */
static void ttml_collect_times(const struct ttml_document *document,
                               xmlNodePtr node, int64_t begin, int64_t end,
                               int64_t window_begin, int64_t window_end,
                               int64_t *times, unsigned *nb_p, unsigned max,
                               unsigned depth)
{
    if (depth > MAX_ELEMENT_DEPTH)
        return;

    for (xmlNodePtr n = node->children; n != NULL; n = n->next) {
        if (n->type != XML_ELEMENT_NODE)
            continue;

        int64_t child_begin, child_end;
        ttml_resolve_times(document, n, begin, end, &child_begin, &child_end);

        if (child_begin > window_begin && child_begin < window_end)
            ttml_add_time(times, nb_p, max, child_begin);
        if (child_end > window_begin && child_end < window_end)
            ttml_add_time(times, nb_p, max, child_end);

        ttml_collect_times(document, n, child_begin, child_end, window_begin,
                           window_end, times, nb_p, max, depth + 1);
    }
}

/*
 * Page building
 */

/** @internal @This appends the collapsed text of a node to a string.
 *
 * TTML content is authored with xml:space="default", so a run of white space
 * is one space and a space is dropped where a line starts.
 *
 * @param run run to append to
 * @param str text to append
 * @return false on allocation failure
 */
static bool ttml_append_text(struct ttml_run *run, const char *str)
{
    for (const char *p = str; *p != '\0'; p++) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!run->text_len || run->text[run->text_len - 1] == ' ')
                continue;
            c = ' ';
        }

        /* grown by doubling: a character at a time would reallocate for every
         * character of every subtitle */
        if (run->text_len + 2 > run->text_size) {
            size_t size = run->text_size ? run->text_size * 2 : 64;
            char *text = realloc(run->text, size);
            if (unlikely(text == NULL))
                return false;
            run->text = text;
            run->text_size = size;
        }
        run->text[run->text_len++] = c;
        run->text[run->text_len] = '\0';
    }

    return true;
}

/** @internal @This appends text to the page with a given style.
 *
 * @param page page being built
 * @param style computed style of the text
 * @param paragraph style of the paragraph the text belongs to
 * @param region region the text belongs to
 * @param str text to append
 * @return false on allocation failure
 */
static bool ttml_page_append(struct ttml_page *page,
                             const struct ttml_style *style,
                             const struct ttml_style *paragraph,
                             unsigned region, const char *str)
{
    uint8_t flags = 0;
    if (style->bold)
        flags |= UREF_TEXT_RUN_BOLD;
    if (style->italic)
        flags |= UREF_TEXT_RUN_ITALIC;
    if (style->underline)
        flags |= UREF_TEXT_RUN_UNDERLINE;

    unsigned font_size = style->font_size > 1 ?
        (unsigned)(style->font_size + 0.5) : 1;
    uint32_t color = style->set & TTML_PROP_COLOR ? style->color : 0xffffffff;
    bool has_background = !!(style->set & TTML_PROP_BACKGROUND);
    uint32_t background = has_background ? style->background : 0;

    struct ttml_run *run = page->nb_runs ?
        &page->runs[page->nb_runs - 1] : NULL;
    bool same = run != NULL && !page->break_pending && run->region == region &&
        (run->flags & ~UREF_TEXT_RUN_BREAK) == flags &&
        run->font_size == font_size && run->color == color &&
        run->has_background == has_background &&
        run->background == background;

    if (!same) {
        if (page->nb_runs >= MAX_RUNS)
            return true;
        run = &page->runs[page->nb_runs++];
        memset(run, 0, sizeof (*run));
        run->font_size = font_size;
        run->color = color;
        run->background = background;
        run->has_background = has_background;
        run->flags = flags | (page->break_pending ? UREF_TEXT_RUN_BREAK : 0);
        run->region = region;
        run->paragraph = *paragraph;
        page->break_pending = false;
    }

    return ttml_append_text(run, str);
}

/** @internal @This walks the content of an element and adds what is active to
 * the page.
 *
 * @param upipe description structure of the pipe
 * @param page page being built
 * @param document document being decoded
 * @param node element to walk
 * @param style computed style of the element
 * @param paragraph style of the paragraph the element belongs to
 * @param region region the element belongs to
 * @param begin beginning of the interval of the element
 * @param end end of the interval of the element
 * @param isd_begin beginning of the intermediate synchronic document
 * @param depth element depth
 * @return false on allocation failure
 */
static bool ttml_walk_content(struct upipe *upipe, struct ttml_page *page,
                              struct ttml_document *document, xmlNodePtr node,
                              const struct ttml_style *style,
                              const struct ttml_style *paragraph,
                              unsigned region, int64_t begin, int64_t end,
                              int64_t isd_begin, unsigned depth)
{
    if (depth > MAX_ELEMENT_DEPTH)
        return true;

    for (xmlNodePtr n = node->children; n != NULL; n = n->next) {
        if (n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE) {
            if (n->content == NULL)
                continue;
            if (!ttml_page_append(page, style, paragraph, region,
                                  (const char *)n->content))
                return false;
            continue;
        }

        if (n->type != XML_ELEMENT_NODE)
            continue;

        if (ttml_is(n, "br")) {
            page->break_pending = true;
            continue;
        }

        if (!ttml_is(n, "span"))
            continue;

        int64_t child_begin, child_end;
        ttml_resolve_times(document, n, begin, end, &child_begin, &child_end);
        if (isd_begin < child_begin || isd_begin >= child_end)
            continue;

        struct ttml_style child = *style;
        child.set &= TTML_PROP_INHERITED;
        ttml_apply_style(upipe, document, n, &child, 0);

        if (!ttml_walk_content(upipe, page, document, n, &child, paragraph,
                               region, child_begin, child_end, isd_begin,
                               depth + 1))
            return false;
    }

    return true;
}

/** @internal @This finds the region an element belongs to.
 *
 * @param document document being decoded
 * @param node element
 * @return the index of the region
 */
static unsigned ttml_find_region(const struct ttml_document *document,
                                 xmlNodePtr node)
{
    for (xmlNodePtr n = node; n != NULL && n->type == XML_ELEMENT_NODE;
         n = n->parent) {
        xmlChar *id = ttml_get_attr(n, "region");
        if (id == NULL)
            continue;
        for (unsigned i = 0; i < document->nb_regions; i++) {
            if (document->regions[i].id == NULL ||
                strcmp(document->regions[i].id, (const char *)id))
                continue;
            xmlFree(id);
            return i;
        }
        xmlFree(id);
    }
    /* the region content with no region attribute lands in is the last one */
    return document->nb_regions - 1;
}

/** @internal @This collects the paragraphs active at a given time.
 *
 * @param upipe description structure of the pipe
 * @param page page being built
 * @param document document being decoded
 * @param node element to walk
 * @param style computed style of the element
 * @param begin beginning of the interval of the element
 * @param end end of the interval of the element
 * @param isd_begin beginning of the intermediate synchronic document
 * @param depth element depth
 * @return false on allocation failure
 */
static bool ttml_walk_body(struct upipe *upipe, struct ttml_page *page,
                           struct ttml_document *document, xmlNodePtr node,
                           const struct ttml_style *style,
                           int64_t begin, int64_t end, int64_t isd_begin,
                           unsigned depth)
{
    if (depth > MAX_ELEMENT_DEPTH)
        return true;

    for (xmlNodePtr n = node->children; n != NULL; n = n->next) {
        if (n->type != XML_ELEMENT_NODE)
            continue;

        bool paragraph = ttml_is(n, "p");
        if (!paragraph && !ttml_is(n, "div"))
            continue;

        int64_t child_begin, child_end;
        ttml_resolve_times(document, n, begin, end, &child_begin, &child_end);
        if (isd_begin < child_begin || isd_begin >= child_end)
            continue;

        struct ttml_style child = *style;
        child.set &= TTML_PROP_INHERITED;
        ttml_apply_style(upipe, document, n, &child, 0);

        if (!paragraph) {
            if (!ttml_walk_body(upipe, page, document, n, &child, child_begin,
                                child_end, isd_begin, depth + 1))
                return false;
            continue;
        }

        /* a paragraph starts on its own line */
        unsigned before = page->nb_runs;
        page->break_pending = before != 0;
        unsigned region = ttml_find_region(document, n);

        if (!ttml_walk_content(upipe, page, document, n, &child, &child,
                               region, child_begin, child_end, isd_begin,
                               depth + 1))
            return false;

        /* an empty paragraph does not open a line */
        if (page->nb_runs == before)
            page->break_pending = false;
    }

    return true;
}

/*
 * Output
 */

/** @internal @This builds and outputs the page of one intermediate synchronic
 * document.
 *
 * @param upipe description structure of the pipe
 * @param document document being decoded
 * @param body body element, or NULL
 * @param body_begin beginning of the body interval
 * @param body_end end of the body interval
 * @param isd_begin beginning of the intermediate synchronic document
 * @param isd_end end of the intermediate synchronic document
 * @param mediatime segment media time
 * @param pts presentation timestamp of the segment
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ttmld_output_isd(struct upipe *upipe,
                                   struct ttml_document *document,
                                   xmlNodePtr body,
                                   int64_t body_begin, int64_t body_end,
                                   int64_t isd_begin, int64_t isd_end,
                                   uint64_t mediatime, uint64_t pts,
                                   struct upump **upump_p)
{
    struct upipe_ttmld *upipe_ttmld = upipe_ttmld_from_upipe(upipe);

    /* The runs come with the pipe: a page is bounded by MAX_RUNS, so an
     * intermediate synchronic document allocates none of its own. */
    struct ttml_page page;
    memset(&page, 0, sizeof (page));
    page.runs = upipe_ttmld->runs;

    if (body != NULL) {
        struct ttml_style style;
        memset(&style, 0, sizeof (style));
        /* EBU-TT-D authors against a default font size of one cell */
        style.font_size = (double)document->root_vsize / document->cell_rows;
        style.wrap = true;
        style.set = TTML_PROP_WRAP;
        ttml_apply_style(upipe, document, body, &style, 0);
        if (!ttml_walk_body(upipe, &page, document, body, &style, body_begin,
                            body_end, isd_begin, 0)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            goto end;
        }
    }

    struct uref *uref = uref_alloc(upipe_ttmld->uref_mgr);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        goto end;
    }

    /* EN 303 560 5.2.4.1: the media time of the segment is the instant of the
     * TTML timeline the PTS of its PES packet corresponds to */
    uref_clock_set_pts_prog(uref, pts + isd_begin - mediatime);
    uref_clock_set_duration(uref, isd_end - isd_begin);
    UBASE_FATAL(upipe, uref_text_set_canvas_hsize(uref, upipe_ttmld->hsize))
    UBASE_FATAL(upipe, uref_text_set_canvas_vsize(uref, upipe_ttmld->vsize))

    /* the runs of a region have to be contiguous, so walk the regions in
     * layout order and take the runs of each */
    uint8_t nb_regions = 0;
    uint8_t nb_runs = 0;
    for (unsigned i = 0; i < document->nb_regions &&
                         nb_regions < MAX_REGIONS; i++) {
        uint8_t first = nb_runs;
        const struct ttml_style *paragraph = NULL;

        for (unsigned j = 0; j < page.nb_runs; j++) {
            const struct ttml_run *run = &page.runs[j];
            if (run->region != i || run->text == NULL || run->text[0] == '\0')
                continue;

            if (paragraph == NULL)
                paragraph = &run->paragraph;

            UBASE_FATAL(upipe, uref_text_set_run_text(uref, run->text,
                                                      nb_runs))
            UBASE_FATAL(upipe, uref_text_set_run_font_size(uref,
                        run->font_size, nb_runs))
            UBASE_FATAL(upipe, uref_text_set_run_color(uref, run->color,
                                                       nb_runs))
            if (run->has_background)
                UBASE_FATAL(upipe, uref_text_set_run_background(uref,
                            run->background, nb_runs))
            /* the first run of a region opens its first line */
            uint8_t flags = nb_runs == first ?
                run->flags & ~UREF_TEXT_RUN_BREAK : run->flags;
            if (flags)
                UBASE_FATAL(upipe, uref_text_set_run_flags(uref, flags,
                                                           nb_runs))
            nb_runs++;
        }

        if (nb_runs == first)
            continue;

        const struct ttml_region *region = &document->regions[i];
        const struct ttml_style *style = &region->style;
        uint8_t index = nb_regions++;

        UBASE_FATAL(upipe, uref_text_set_region_x(uref, region->x, index))
        UBASE_FATAL(upipe, uref_text_set_region_y(uref, region->y, index))
        UBASE_FATAL(upipe, uref_text_set_region_hsize(uref, region->hsize,
                                                      index))
        UBASE_FATAL(upipe, uref_text_set_region_vsize(uref, region->vsize,
                                                      index))
        UBASE_FATAL(upipe, uref_text_set_region_runs(uref, nb_runs - first,
                                                     index))

        if (style->set & TTML_PROP_DISPLAY_ALIGN)
            UBASE_FATAL(upipe, uref_text_set_region_display_align(uref,
                        style->display_align, index))
        if (style->set & TTML_PROP_BACKGROUND)
            UBASE_FATAL(upipe, uref_text_set_region_background(uref,
                        style->background, index))

        /* Alignment and line styling are content properties: EBU-TT-D puts
         * them on the paragraph as often as on the region.  Take what the
         * paragraph gives and fall back on the region. */
        const struct ttml_style *from =
            paragraph->set & TTML_PROP_TEXT_ALIGN ? paragraph : style;
        if (from->set & TTML_PROP_TEXT_ALIGN)
            UBASE_FATAL(upipe, uref_text_set_region_text_align(uref,
                        from->text_align, index))

        from = paragraph->set & TTML_PROP_MULTI_ROW ? paragraph : style;
        if (from->set & TTML_PROP_MULTI_ROW)
            UBASE_FATAL(upipe, uref_text_set_region_multi_row_align(uref,
                        from->multi_row_align, index))

        from = paragraph->set & TTML_PROP_FILL_LINE_GAP ? paragraph : style;
        if (from->set & TTML_PROP_FILL_LINE_GAP && from->fill_line_gap)
            UBASE_FATAL(upipe, uref_text_set_region_fill_line_gap(uref, index))

        from = paragraph->set & TTML_PROP_WRAP ? paragraph : style;
        if (from->set & TTML_PROP_WRAP && !from->wrap)
            UBASE_FATAL(upipe, uref_text_set_region_no_wrap(uref, index))

        from = paragraph->set & TTML_PROP_LINE_HEIGHT ? paragraph : style;
        if (from->set & TTML_PROP_LINE_HEIGHT && from->line_height > 0)
            UBASE_FATAL(upipe, uref_text_set_region_line_height(uref,
                        (uint64_t)(from->line_height + 0.5), index))

        from = paragraph->set & TTML_PROP_LINE_PADDING ? paragraph : style;
        if (from->set & TTML_PROP_LINE_PADDING && from->line_padding > 0)
            UBASE_FATAL(upipe, uref_text_set_region_line_padding(uref,
                        (uint64_t)(from->line_padding + 0.5), index))
    }

    upipe_dbg_va(upipe, "page %"PRId64"..%"PRId64" ms: %u laid out run(s), "
            "%"PRIu8" region(s), %"PRIu8" run(s) kept",
            isd_begin / (int64_t)(UCLOCK_FREQ / 1000),
            isd_end / (int64_t)(UCLOCK_FREQ / 1000),
            page.nb_runs, nb_regions, nb_runs);

    /* a page with no region clears the screen */
    UBASE_FATAL(upipe, uref_text_set_regions(uref, nb_regions))
    upipe_ttmld_output(upipe, uref, upump_p);

end:
    for (unsigned i = 0; i < page.nb_runs; i++)
        free(page.runs[i].text);
}

/** @internal @This decodes a TTML document.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ttmld_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_ttmld *upipe_ttmld = upipe_ttmld_from_upipe(upipe);

    if (unlikely(!upipe_ttmld->hsize || !upipe_ttmld->vsize)) {
        upipe_warn(upipe, "no canvas size set");
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_ttmld_demand_uref_mgr(upipe))) {
        uref_free(uref);
        return;
    }

    uint64_t pts;
    if (unlikely(!ubase_check(uref_clock_get_pts_prog(uref, &pts)))) {
        upipe_warn(upipe, "undated TTML segment");
        uref_free(uref);
        return;
    }

    uint64_t mediatime = 0;
    uref_ts_ttml_get_mediatime(uref, &mediatime);

    /* The demuxer hands over a document in a single block, either the payload
     * of the segment or a freshly inflated one, so it maps in one piece and
     * the parser reads it where it lies. */
    const uint8_t *buffer;
    int size = -1;
    if (unlikely(!ubase_check(uref_block_read(uref, 0, &size, &buffer)))) {
        upipe_warn(upipe, "could not read TTML document");
        uref_free(uref);
        return;
    }

    size_t total;
    if (unlikely(!ubase_check(uref_block_size(uref, &total)) ||
                 (size_t)size != total)) {
        upipe_warn(upipe, "segmented TTML document");
        uref_block_unmap(uref, 0);
        uref_free(uref);
        return;
    }

    struct ttml_document document;
    memset(&document, 0, sizeof (document));
    document.doc = xmlReadMemory((const char *)buffer, size, NULL, "UTF-8",
                                 XML_PARSE_NONET | XML_PARSE_NOWARNING |
                                 XML_PARSE_NOERROR | XML_PARSE_NOCDATA);
    uref_block_unmap(uref, 0);
    uref_free(uref);
    if (unlikely(document.doc == NULL)) {
        upipe_warn(upipe, "could not parse TTML document");
        return;
    }

    xmlNodePtr tt = xmlDocGetRootElement(document.doc);
    if (unlikely(tt == NULL || !ttml_is(tt, "tt"))) {
        upipe_warn(upipe, "not a TTML document");
        ttml_clean(&document);
        return;
    }

    ttml_read_params(upipe, &document, tt);

    xmlNodePtr head = NULL, body = NULL;
    for (xmlNodePtr n = tt->children; n != NULL; n = n->next) {
        if (ttml_is(n, "head"))
            head = n;
        else if (ttml_is(n, "body"))
            body = n;
    }

    if (unlikely(!ubase_check(ttml_read_head(upipe, &document, head)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        ttml_clean(&document);
        return;
    }

    /* A segment is active from its PTS until the next one or TMPA later,
     * whichever comes first (EN 303 560 5.2.3.3), so nothing outside that
     * window is ever shown. */
    int64_t window_begin = mediatime;
    int64_t window_end = mediatime + TMPA;

    /* The times in the document are on the timeline of the document, which
     * begins at zero; the segment media time is the point of that timeline the
     * PTS corresponds to.  Resolving them against the window instead would
     * offset every one of them by the media time and leave nothing inside it. */
    int64_t body_begin = 0, body_end = TTML_UNBOUNDED;
    if (body != NULL)
        ttml_resolve_times(&document, body, 0, TTML_UNBOUNDED,
                           &body_begin, &body_end);

    int64_t times[MAX_ISDS + 1];
    unsigned nb_times = 0;
    ttml_add_time(times, &nb_times, MAX_ISDS + 1, window_begin);
    ttml_add_time(times, &nb_times, MAX_ISDS + 1, window_end);
    if (body != NULL)
        ttml_collect_times(&document, body, body_begin, body_end, window_begin,
                           window_end, times, &nb_times, MAX_ISDS + 1, 0);

    upipe_dbg_va(upipe, "document of %u byte(s): root %dx%d, %u style(s), "
            "%u region(s), body %"PRId64"..%"PRId64" ms, window %"PRId64
            "..%"PRId64" ms, %u intermediate synchronic document(s)",
            (unsigned)size, document.root_hsize, document.root_vsize,
            document.nb_styles, document.nb_regions,
            body_begin / (UCLOCK_FREQ / 1000),
            body_end == TTML_UNBOUNDED ? -1 : body_end / (UCLOCK_FREQ / 1000),
            window_begin / (UCLOCK_FREQ / 1000),
            window_end / (UCLOCK_FREQ / 1000),
            nb_times ? nb_times - 1 : 0);

    for (unsigned i = 0; i + 1 < nb_times; i++)
        upipe_ttmld_output_isd(upipe, &document, body, body_begin, body_end,
                               times[i], times[i + 1], mediatime, pts,
                               upump_p);

    ttml_clean(&document);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ttmld_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    UBASE_FATAL(upipe, uref_flow_set_def(flow_def_dup, UREF_TEXT_FLOW_DEF))
    upipe_ttmld_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the canvas size.
 *
 * @param upipe description structure of the pipe
 * @param hsize width of the canvas
 * @param vsize height of the canvas
 * @return an error code
 */
static int _upipe_ttmld_set_canvas_size(struct upipe *upipe,
                                        uint64_t hsize, uint64_t vsize)
{
    struct upipe_ttmld *upipe_ttmld = upipe_ttmld_from_upipe(upipe);
    if (unlikely(!hsize || !vsize))
        return UBASE_ERR_INVALID;
    upipe_ttmld->hsize = hsize;
    upipe_ttmld->vsize = vsize;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ttmld_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ttmld_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ttmld_set_flow_def(upipe, flow_def);
        }
        case UPIPE_TTMLD_SET_CANVAS_SIZE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TTMLD_SIGNATURE)
            uint64_t hsize = va_arg(args, uint64_t);
            uint64_t vsize = va_arg(args, uint64_t);
            return _upipe_ttmld_set_canvas_size(upipe, hsize, vsize);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a ttmld pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ttmld_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ttmld_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ttmld *upipe_ttmld = upipe_ttmld_from_upipe(upipe);
    upipe_ttmld_init_urefcount(upipe);
    upipe_ttmld_init_output(upipe);
    upipe_ttmld_init_uref_mgr(upipe);
    /* no canvas until the application says what raster the regions are
     * positioned on: guessing one puts every subtitle in the wrong place */
    upipe_ttmld->hsize = 0;
    upipe_ttmld->vsize = 0;

    upipe_ttmld->runs = calloc(MAX_RUNS, sizeof (*upipe_ttmld->runs));
    if (unlikely(upipe_ttmld->runs == NULL)) {
        upipe_ttmld_clean_uref_mgr(upipe);
        upipe_ttmld_clean_output(upipe);
        upipe_ttmld_clean_urefcount(upipe);
        upipe_ttmld_free_void(upipe);
        return NULL;
    }

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ttmld_free(struct upipe *upipe)
{
    struct upipe_ttmld *upipe_ttmld = upipe_ttmld_from_upipe(upipe);

    upipe_throw_dead(upipe);

    free(upipe_ttmld->runs);
    upipe_ttmld_clean_uref_mgr(upipe);
    upipe_ttmld_clean_output(upipe);
    upipe_ttmld_clean_urefcount(upipe);
    upipe_ttmld_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ttmld_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TTMLD_SIGNATURE,

    .upipe_alloc = upipe_ttmld_alloc,
    .upipe_input = upipe_ttmld_input,
    .upipe_control = upipe_ttmld_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ttmld pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ttmld_mgr_alloc(void)
{
    return &upipe_ttmld_mgr;
}
