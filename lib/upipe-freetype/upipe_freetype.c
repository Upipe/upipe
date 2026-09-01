/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 * Copyright (C) 2018-2019 OpenHeadend S.A.R.L.
 *
 * Authors: Rafaël Carré
 *          Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short freetype2 based text renderer
 */

#include "upipe/ubase.h"
#include "upipe/ubuf_pic.h"
#include "upipe/uref.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_text.h"
#include "upipe/uref_dump.h"
#include "upipe/uref_void.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_flow_format.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_input.h"
#include "upipe-freetype/upipe_freetype.h"

#include <ft2build.h>

#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_CACHE_H
#include FT_ADVANCES_H
#include FT_OUTLINE_H

/** synthetic bold strength, in 26.6 pixels per font pixel */
#define EMBOLDEN_STRENGTH 3
/** synthetic italic shear, in 16.16 */
#define ITALIC_SHEAR 0x3400
/** upper bound on the glyphs laid out for one page */
#define MAX_GLYPHS 1024
/** upper bound on the lines laid out for one region */
#define MAX_LINES 64
/** smallest picture the renderer emits, which is what a page with nothing to
 * draw comes to: a couple of pixels is not a legal size once the chroma is
 * subsampled and the scaler has to work on it, so use the same floor as the
 * DVB subtitle decoder does for an empty subtitle */
#define MIN_PICTURE_SIZE 16
/** upper bound on the regions of one page, well above the four a DVB TTML
 * subtitle stream may have active at once */
#define MAX_PAGE_REGIONS 16
/** upper bound on the runs of one page, which is what the attribute index of
 * uref_text holds */
#define MAX_RUNS 255

/** @internal @This describes a mapped plane of a picture. */
struct upipe_freetype_plane {
    /** line stride */
    size_t stride;
    /** horizontal subsampling, as a shift: every format upipe describes
     * subsamples by a power of two, and a shift per pixel is not a divide */
    uint8_t hshift;
    /** vertical subsampling, as a shift */
    uint8_t vshift;
    /** size of a macropixel */
    uint8_t macropixel_size;
    /** number of octets in a line */
    size_t memset_width;
    /** number of lines */
    size_t lines;
    /** mapped plane, or NULL if the picture has no such plane */
    uint8_t *p;
};

/** @internal @This is the offset of a pixel in a plane. */
#define UPIPE_FREETYPE_OFFSET(Plane, X, Y)                                  \
    (((size_t)(Y) >> (Plane).vshift) * (Plane).stride +                     \
     ((size_t)(X) >> (Plane).hshift) * (Plane).macropixel_size)

/** @internal @This divides by 255 with the rounding a division would give,
 * exact for anything a blend of two octets comes to.
 *
 * @param v value to divide
 * @return the value divided by 255
 */
static inline unsigned upipe_freetype_div255(unsigned v)
{
    v += 0x80;
    return (v + (v >> 8)) >> 8;
}

/** @internal @This describes the picture the text is drawn into. */
struct upipe_freetype_canvas {
    /** picture width */
    size_t width;
    /** picture height */
    size_t height;
    /** number of pixels in a macropixel */
    uint8_t macropixel;
    /** luma plane */
    struct upipe_freetype_plane y;
    /** blue chroma plane */
    struct upipe_freetype_plane u;
    /** red chroma plane */
    struct upipe_freetype_plane v;
    /** alpha plane */
    struct upipe_freetype_plane a;
    /** interleaved chroma plane */
    struct upipe_freetype_plane uv;
};

/** @internal @This is a rectangle, empty when x0 >= x1. */
struct upipe_freetype_box {
    int x0, y0, x1, y1;
};

/** @internal @This is a glyph bitmap, either from the small bitmap cache or
 * rendered from a transformed copy of a cached outline. */
struct upipe_freetype_bitmap {
    /** left bearing */
    int left;
    /** top bearing */
    int top;
    /** bitmap width */
    int width;
    /** bitmap height */
    int height;
    /** octets between two lines of the bitmap */
    int pitch;
    /** coverage values */
    const unsigned char *buffer;
    /** horizontal advance in 16.16 */
    int32_t advance;
    /** owned glyph to release, or NULL when the bitmap belongs to a cache */
    FT_Glyph glyph;
};

/** @internal @This is a styled run of text. */
struct upipe_freetype_run {
    /** UTF-8 text */
    const char *text;
    /** font size in pixels */
    unsigned pixel_size;
    /** style flags */
    uint8_t flags;
    /** whether the run has a background */
    bool has_bg;
    /** foreground colour in YUVA */
    uint8_t fg[4];
    /** background colour in YUVA */
    uint8_t bg[4];
    /** ascender in 26.6 */
    int ascender;
    /** descender in 26.6, counted downwards */
    int descender;
    /** default line height in 26.6 */
    int height;
};

/** @internal @This is a region of the canvas. */
struct upipe_freetype_region {
    /** left of the region */
    int x;
    /** top of the region */
    int y;
    /** width of the region */
    int hsize;
    /** height of the region */
    int vsize;
    /** block alignment */
    uint8_t display_align;
    /** line alignment */
    uint8_t text_align;
    /** row alignment */
    uint8_t multi_row_align;
    /** line height in pixels, or 0 for the font default */
    int line_height;
    /** horizontal padding of the line backgrounds */
    int line_padding;
    /** whether the region has a background */
    bool has_bg;
    /** background colour in YUVA */
    uint8_t bg[4];
    /** whether the lines are wrapped */
    bool wrap;
    /** whether the line backgrounds cover the whole line height */
    bool fill_line_gap;
    /** first run of the region */
    unsigned first_run;
    /** number of runs in the region */
    unsigned nb_runs;
    /** first glyph of the region */
    unsigned first_glyph;
    /** number of glyphs in the region */
    unsigned nb_glyphs;
    /** first line of the region */
    unsigned first_line;
    /** number of lines in the region */
    unsigned nb_lines;
};

/** @internal @This is a shaped glyph. */
struct upipe_freetype_glyph {
    /** glyph index in the font */
    FT_UInt index;
    /** unicode character */
    uint32_t character;
    /** horizontal advance in 16.16 */
    int32_t advance;
    /** kerning with the previous glyph in 16.16 */
    int32_t kerning;
    /** pen position from the start of the line in 16.16 */
    int64_t x;
    /** left bearing */
    int16_t left;
    /** top bearing */
    int16_t top;
    /** bitmap width */
    int16_t width;
    /** bitmap height */
    int16_t height;
    /** run the glyph belongs to */
    unsigned run;
    /** whether the glyph starts a new line */
    bool break_before;
};

/** @internal @This is a laid out line. */
struct upipe_freetype_line {
    /** first glyph of the line */
    unsigned first;
    /** number of glyphs in the line */
    unsigned nb;
    /** width in 16.16 */
    int64_t width;
    /** ascender in 26.6 */
    int ascender;
    /** descender in 26.6 */
    int descender;
    /** height in 26.6 */
    int height;
};

/** @internal @This is where a page is drawn, or the box it would occupy when
 * canvas is NULL: measuring and drawing walk the same code so the picture can
 * not end up smaller than what is drawn into it. */
struct upipe_freetype_target {
    /** picture to draw into, or NULL to measure */
    struct upipe_freetype_canvas *canvas;
    /** left of the picture on the canvas */
    int origin_x;
    /** top of the picture on the canvas */
    int origin_y;
    /** box covered so far */
    struct upipe_freetype_box box;
};

/** upipe_freetype structure */
struct upipe_freetype {
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** attributes / parameters from application */
    struct uref *flow_def_params;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;
    /** list of retained urefs */
    struct uchain urefs;
    /** number of retained urefs */
    unsigned nb_urefs;
    /** maximum number of retained urefs */
    unsigned max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** flow format request */
    struct urequest flow_format_request;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;
    /** cached ubuf */
    struct ubuf *ubuf;
    /** cached text */
    char *text;

    /** request output */
    struct uref *flow_output;

    /** font */
    char *font;
    /** current pixel size */
    unsigned pixel_size;
    /** freetype handle */
    FT_Library library;
    /** freetype cache manager */
    FTC_Manager cache_manager;
    /** freetype cmap cache */
    FTC_CMapCache cmap_cache;
    /** freetype image cache */
    FTC_ImageCache img_cache;
    /** fretype sbit cache */
    FTC_SBitCache sbit_cache;
    /** font handle */
    FT_Face face;
    /** baseline left offset */
    int64_t xoff;
    /** baseline right offset */
    int64_t yoff;
    /** output horizontal size */
    uint64_t hsize;
    /** output vertical size */
    uint64_t vsize;

    /** full range */
    bool fullrange;

    /* Scratch for laying a page out, allocated once with the pipe: a page is
     * bounded by the limits below, so none of it grows with the input. */

    /** regions of the page being laid out */
    struct upipe_freetype_region *regions;
    /** runs of the page being laid out */
    struct upipe_freetype_run *runs;
    /** glyphs of the page being laid out */
    struct upipe_freetype_glyph *glyphs;
    /** lines of the page being laid out */
    struct upipe_freetype_line *lines;

    /** background color YUVA */
    uint8_t background[4];
    /** foreground color YUVA */
    uint8_t foreground[4];

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_freetype_check_ubuf_mgr(struct upipe *upipe,
                                         struct uref *flow_format);

/** @hidden */
static int upipe_freetype_check_flow_format(struct upipe *upipe,
                                            struct uref *flow_format);
/** @hidden */
static int upipe_freetype_check(struct upipe *upipe, struct uref *uref);

/** @hidden */
static bool upipe_freetype_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump);

UPIPE_HELPER_UPIPE(upipe_freetype, upipe, UPIPE_FREETYPE_SIGNATURE);
UPIPE_HELPER_OUTPUT(upipe_freetype, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UREFCOUNT(upipe_freetype, urefcount, upipe_freetype_free)
UPIPE_HELPER_FLOW_FORMAT(upipe_freetype, flow_format_request,
                         upipe_freetype_check_flow_format,
                         upipe_freetype_register_output_request,
                         upipe_freetype_unregister_output_request);
UPIPE_HELPER_UBUF_MGR(upipe_freetype, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_freetype_check_ubuf_mgr,
                      upipe_freetype_register_output_request,
                      upipe_freetype_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_freetype, urefs, nb_urefs, max_urefs, blockers,
                   upipe_freetype_handle);
UPIPE_HELPER_FLOW(upipe_freetype, UREF_PIC_FLOW_DEF);

/** @internal @This flushes the cached buffer.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_freetype_flush_cache(struct upipe *upipe)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    ubuf_free(upipe_freetype->ubuf);
    upipe_freetype->ubuf = NULL;
    free(upipe_freetype->text);
    upipe_freetype->text = NULL;
}

/** @internal @This checks the compatibility of a flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_format flow format to test
 * @return an error code
 */
static int upipe_freetype_check_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF));
    UBASE_RETURN(uref_pic_flow_find_chroma(flow_def, "y8", NULL));
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, NULL));
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, NULL));
    return UBASE_ERR_NONE;
}

/** @internal @This loads the freetype font handle.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_freetype_load_face(struct upipe *upipe)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    upipe_freetype->face = NULL;
    if (!upipe_freetype->font)
        return UBASE_ERR_INVALID;

    FTC_ScalerRec scaler;
    memset(&scaler, 0, sizeof (scaler));
    scaler.face_id = upipe_freetype->font;
    scaler.width = upipe_freetype->pixel_size;
    scaler.height = upipe_freetype->pixel_size;
    scaler.pixel = 1;

    FT_Size size;
    if (FTC_Manager_LookupSize(upipe_freetype->cache_manager, &scaler, &size))
        return UBASE_ERR_INVALID;

    upipe_freetype->face = size->face;
    return UBASE_ERR_NONE;
}

/** @internal @This checks the compatibility of the ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format ubuf manager flow format
 * @return an error code
 */
static int upipe_freetype_check_ubuf_mgr(struct upipe *upipe,
                                         struct uref *flow_format)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    if (flow_format) {
        int err = upipe_freetype_check_flow_def(upipe, flow_format);
        if (unlikely(!ubase_check(err))) {
            uref_free(flow_format);
            return err;
        }

        uref_pic_flow_get_hsize(flow_format, &upipe_freetype->hsize);
        uref_pic_flow_get_vsize(flow_format, &upipe_freetype->vsize);
        upipe_freetype_flush_cache(upipe);
        /* Not advertised as the output flow definition yet.  A page is only
         * ever a crop out of this format, and a first negotiation at the full
         * size settles on a chain that does not scale, which no later crop
         * gets a chance to undo.  Whichever path renders first advertises the
         * geometry it actually produces. */
        uref_free(flow_format);
    }

    if (upipe_freetype_check_input(upipe))
        return UBASE_ERR_NONE;

    if (upipe_freetype_output_input(upipe)) {
        upipe_freetype_unblock_input(upipe);
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This checks the compatibility of a flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_format flow format to test
 * @return an error code
 */
static int upipe_freetype_check_flow_format(struct upipe *upipe,
                                            struct uref *flow_format)
{
    int err = upipe_freetype_check_flow_def(upipe, flow_format);
    if (unlikely(!ubase_check(err))) {
        uref_free(flow_format);
        return err;
    }
    upipe_freetype_require_ubuf_mgr(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This checks the freetype pipe state.
 *
 * @param upipe description structure of the pipe
 * @param flow_format output flow format
 * @return an error code
 */
static int upipe_freetype_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    if (flow_format)
        uref_free(flow_format);

    if (!upipe_freetype->ubuf_mgr &&
        urequest_get_opaque(&upipe_freetype->flow_format_request,
                            struct upipe *) != upipe) {
        struct uref *flow_format = uref_dup(upipe_freetype->flow_output);
        UBASE_ALLOC_RETURN(flow_format);
        upipe_freetype_require_flow_format(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_freetype_free(struct upipe *upipe)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    upipe_throw_dead(upipe);

    upipe_freetype_flush_cache(upipe);
    free(upipe_freetype->regions);
    free(upipe_freetype->runs);
    free(upipe_freetype->glyphs);
    free(upipe_freetype->lines);
    FTC_Manager_Done(upipe_freetype->cache_manager);
    FT_Done_FreeType(upipe_freetype->library);
    free(upipe_freetype->font);

    uref_free(upipe_freetype->flow_output);
    upipe_freetype_clean_input(upipe);
    upipe_freetype_clean_urefcount(upipe);
    upipe_freetype_clean_output(upipe);
    upipe_freetype_clean_ubuf_mgr(upipe);
    upipe_freetype_clean_flow_format(upipe);
    upipe_freetype_free_flow(upipe);
}

/** @internal @This is called by freetype to translate face id to real face.
 *
 * @param face_id face id to translate
 * @param library freetype library handle
 * @param data private data
 * @param face translated font face
 * @return a freetype error code
 */
static FT_Error upipe_freetype_face_requester(FTC_FaceID face_id,
                                              FT_Library library,
                                              FT_Pointer data,
                                              FT_Face *face)
{
    return FT_New_Face(library, face_id, 0, face);
}

/** @internal @This allocates a freetype pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_freetype_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct uref *flow_def;

    struct upipe *upipe =
        upipe_freetype_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_freetype_init_urefcount(upipe);
    upipe_freetype_init_output(upipe);
    upipe_freetype_init_flow_format(upipe);
    upipe_freetype_init_ubuf_mgr(upipe);
    upipe_freetype_init_input(upipe);

    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    upipe_freetype->flow_output = flow_def;
    upipe_freetype->ubuf = NULL;
    upipe_freetype->text = NULL;
    upipe_freetype->library = NULL;
    upipe_freetype->cache_manager = NULL;
    upipe_freetype->face = NULL;
    upipe_freetype->xoff = 0;
    upipe_freetype->yoff = 0;
    upipe_freetype->font = NULL;
    upipe_freetype->pixel_size = 0;
    upipe_freetype->fullrange =
        ubase_check(uref_pic_flow_get_full_range(flow_def));

    /* Laying a page out allocates nothing: the scratch is bounded and comes
     * with the pipe. */
    upipe_freetype->regions =
        malloc(MAX_PAGE_REGIONS * sizeof (*upipe_freetype->regions));
    upipe_freetype->runs = malloc(MAX_RUNS * sizeof (*upipe_freetype->runs));
    upipe_freetype->glyphs =
        malloc(MAX_GLYPHS * sizeof (*upipe_freetype->glyphs));
    upipe_freetype->lines =
        malloc(MAX_PAGE_REGIONS * MAX_LINES * sizeof (*upipe_freetype->lines));
    if (unlikely(upipe_freetype->regions == NULL ||
                 upipe_freetype->runs == NULL ||
                 upipe_freetype->glyphs == NULL ||
                 upipe_freetype->lines == NULL)) {
        upipe_release(upipe);
        return NULL;
    }
    /* black */
    upipe_freetype->background[0] = upipe_freetype->fullrange ? 0 : 16;
    upipe_freetype->background[1] = 0x80;
    upipe_freetype->background[2] = 0x80;
    upipe_freetype->background[3] = 0xff;
    /* white */
    upipe_freetype->foreground[0] = upipe_freetype->fullrange ? 255 : 240;
    upipe_freetype->foreground[1] = 0x80;
    upipe_freetype->foreground[2] = 0x80;
    upipe_freetype->foreground[3] = 0xff;

    upipe_throw_ready(upipe);

    if (FT_Init_FreeType(&upipe_freetype->library)) {
        upipe_release(upipe);
        return NULL;
    }

    if (FTC_Manager_New(upipe_freetype->library,
                        0, 0, 0,
                        upipe_freetype_face_requester,
                        upipe,
                        &upipe_freetype->cache_manager)) {
        upipe_release(upipe);
        return NULL;
    }

    if (FTC_CMapCache_New(upipe_freetype->cache_manager,
                          &upipe_freetype->cmap_cache) ||
        FTC_ImageCache_New(upipe_freetype->cache_manager,
                           &upipe_freetype->img_cache) ||
        FTC_SBitCache_New(upipe_freetype->cache_manager,
                          &upipe_freetype->sbit_cache)) {
        upipe_release(upipe);
        return NULL;
    }

    uint64_t vsize;
    if (ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize))) {
        upipe_freetype->pixel_size = vsize > UINT_MAX ? UINT_MAX : vsize;
        upipe_freetype->yoff = vsize - vsize / 8;
    }

    return upipe;
}

/** @internal @This reads a unicode character from a string
 *
 * @param str the string to read
 * @param i the string position to read, returns the number of bytes read or 0 if error
 * @return the Unicode character number
 */
static uint32_t unicode_character(const char *str, size_t *i)
{
    uint32_t c = 0;

    unsigned char c1 = (unsigned char)str[0];
    if (c1 == '\0') {
        /* EOS */
        goto error;
    } else if (!(c1 & 0x80)) {
        /* 1 byte : ASCII */
        *i = 1;
        c = c1;
    } else {
        if (!(c1 & 0x40))
            goto error;

        unsigned char c2 = (unsigned char)str[1];
        if ((c2 & 0xc0) != 0x80)
            goto error;

        if (!(c1 & 0x20)) {
            *i = 2;
            c = ((c1 & 0x1f) << 6) | (c2 & 0x3f);
        } else {
            unsigned char c3 = (unsigned char)str[2];
            if ((c3 & 0xc0) != 0x80)
                goto error;

            if (!(c1 & 0x10)) {
                *i = 3;
                c = ((c1 & 0xf) << 12) | ((c2 & 0x3f) << 6) | (c3 & 0x3f);
            } else {
                unsigned char c4 = (unsigned char)str[3];
                if ((c4 & 0xc0) != 0x80)
                    goto error;
                *i = 4;
                c = ((c1 & 0x7) << 18) | ((c2 & 0x3f) << 12) |
                    ((c3 & 0x3f) << 6) | (c4 & 0x3f);
            }
        }
    }

    return c;

error:
    *i = 0;
    return 0;
}

/** @internal @This sends a probe with the new text.
 *
 * @param upipe description structure of the pipe
 * @param text new text input
 * @return an error code
 */
static int upipe_freetype_throw_new_text(struct upipe *upipe, const char *text)
{
    return upipe_throw(upipe, UPROBE_FREETYPE_NEW_TEXT,
                       UPIPE_FREETYPE_SIGNATURE, text);
}

/** @internal @This grows a range around what it already covers until it spans
 * at least a minimum, without leaving the canvas.
 *
 * @param begin beginning of the range
 * @param end end of the range
 * @param min minimum span
 * @param bound size of the canvas along that axis
 */
static void upipe_freetype_grow(int *begin, int *end, int min, int bound)
{
    if (min > bound)
        min = bound;
    while (*end - *begin < min) {
        if (*begin > 0)
            (*begin)--;
        else if (*end < bound)
            (*end)++;
        else
            break;
        if (*end - *begin < min && *end < bound)
            (*end)++;
    }
}

/** @internal @This extends a box.
 *
 * @param box box to extend
 * @param x0 left of the added rectangle
 * @param y0 top of the added rectangle
 * @param x1 right of the added rectangle
 * @param y1 bottom of the added rectangle
 */
static void upipe_freetype_box_add(struct upipe_freetype_box *box,
                                   int x0, int y0, int x1, int y1)
{
    if (x0 >= x1 || y0 >= y1)
        return;
    if (box->x0 >= box->x1 || box->y0 >= box->y1) {
        box->x0 = x0;
        box->y0 = y0;
        box->x1 = x1;
        box->y1 = y1;
        return;
    }
    if (x0 < box->x0)
        box->x0 = x0;
    if (y0 < box->y0)
        box->y0 = y0;
    if (x1 > box->x1)
        box->x1 = x1;
    if (y1 > box->y1)
        box->y1 = y1;
}

/** @internal @This maps the planes of a picture.
 *
 * @param upipe description structure of the pipe
 * @param ubuf picture to map
 * @param canvas filled in with the mapped planes
 * @return an error code
 */
static int upipe_freetype_map(struct upipe *upipe, struct ubuf *ubuf,
                              struct upipe_freetype_canvas *canvas)
{
    memset(canvas, 0, sizeof (*canvas));

    UBASE_RETURN(ubuf_pic_size(ubuf, &canvas->width, &canvas->height,
                               &canvas->macropixel))

    const char *chroma;
    ubuf_pic_foreach_plane(ubuf, chroma) {
        struct upipe_freetype_plane *plane = NULL;

        if (!strcmp(chroma, "y8"))
            plane = &canvas->y;
        else if (!strcmp(chroma, "u8"))
            plane = &canvas->u;
        else if (!strcmp(chroma, "v8"))
            plane = &canvas->v;
        else if (!strcmp(chroma, "a8"))
            plane = &canvas->a;
        else if (!strcmp(chroma, "u8v8"))
            plane = &canvas->uv;
        else {
            upipe_warn_va(upipe, "unsupported plane %s", chroma);
            continue;
        }

        uint8_t hsub, vsub;
        if (unlikely(!ubase_check(ubuf_pic_plane_size(ubuf, chroma,
                            &plane->stride, &hsub, &vsub,
                            &plane->macropixel_size)))) {
            upipe_warn_va(upipe, "fail to get plane %s size", chroma);
            continue;
        }

        plane->hshift = plane->vshift = 0;
        while ((1U << plane->hshift) < hsub)
            plane->hshift++;
        while ((1U << plane->vshift) < vsub)
            plane->vshift++;
        if (unlikely((1U << plane->hshift) != hsub ||
                     (1U << plane->vshift) != vsub)) {
            upipe_warn_va(upipe, "plane %s subsamples by %"PRIu8"x%"PRIu8,
                          chroma, hsub, vsub);
            continue;
        }

        plane->memset_width = canvas->width * plane->macropixel_size /
            hsub / canvas->macropixel;
        plane->lines = canvas->height >> plane->vshift;

        if (unlikely(!ubase_check(ubuf_pic_plane_write(ubuf, chroma, 0, 0,
                                                       -1, -1, &plane->p)))) {
            upipe_warn_va(upipe, "fail to map %s plane", chroma);
            plane->p = NULL;
        }
    }
    return UBASE_ERR_NONE;
}

/** @internal @This unmaps the planes of a picture.
 *
 * @param ubuf mapped picture
 * @param canvas mapped planes
 */
static void upipe_freetype_unmap(struct ubuf *ubuf,
                                 struct upipe_freetype_canvas *canvas)
{
    if (canvas->y.p)
        ubuf_pic_plane_unmap(ubuf, "y8", 0, 0, -1, -1);
    if (canvas->u.p)
        ubuf_pic_plane_unmap(ubuf, "u8", 0, 0, -1, -1);
    if (canvas->v.p)
        ubuf_pic_plane_unmap(ubuf, "v8", 0, 0, -1, -1);
    if (canvas->a.p)
        ubuf_pic_plane_unmap(ubuf, "a8", 0, 0, -1, -1);
    if (canvas->uv.p)
        ubuf_pic_plane_unmap(ubuf, "u8v8", 0, 0, -1, -1);
}

/** @internal @This fills a picture with a single colour.
 *
 * @param canvas mapped planes
 * @param colour colour in YUVA
 */
static void upipe_freetype_fill(struct upipe_freetype_canvas *canvas,
                                const uint8_t colour[4])
{
#define FILL_PLANE(Plane, Val)                                              \
    if (Plane.p) {                                                          \
        uint8_t *buf = Plane.p;                                             \
        for (size_t i = 0; i < Plane.lines; i++) {                          \
            memset(buf, Val, Plane.memset_width);                           \
            buf += Plane.stride;                                            \
        }                                                                   \
    }

    FILL_PLANE(canvas->y, colour[0])
    FILL_PLANE(canvas->u, colour[1])
    FILL_PLANE(canvas->v, colour[2])
    FILL_PLANE(canvas->a, colour[3])
#undef FILL_PLANE

    if (canvas->uv.p) {
        uint8_t *buf = canvas->uv.p;
        for (size_t i = 0; i < canvas->uv.lines; i++) {
            for (size_t j = 0; j + 1 < canvas->uv.memset_width; j += 2) {
                buf[j] = colour[1];
                buf[j + 1] = colour[2];
            }
            buf += canvas->uv.stride;
        }
    }
}

/** @internal @This composites a colour onto a pixel.
 *
 * @param canvas mapped planes
 * @param x horizontal position
 * @param y vertical position
 * @param colour colour in YUVA
 * @param coverage coverage of the pixel by the source
 */
static void upipe_freetype_blend(struct upipe_freetype_canvas *canvas,
                                 int x, int y, const uint8_t colour[4],
                                 uint8_t coverage)
{
    if (x < 0 || y < 0 || (size_t)x >= canvas->width ||
        (size_t)y >= canvas->height)
        return;

    unsigned as = coverage == 0xff ? colour[3] :
        upipe_freetype_div255(colour[3] * coverage);
    if (!as)
        return;

    uint8_t *alpha = NULL;
    unsigned ad = 0xff;
    if (canvas->a.p) {
        alpha = canvas->a.p + UPIPE_FREETYPE_OFFSET(canvas->a, x, y);
        ad = *alpha;
    }

    /* The colour is straight, not premultiplied, so the destination has to be
     * weighted by its own alpha and the result scaled back by the alpha it
     * comes to: mixing it in directly would drag the edge of a glyph towards
     * the colour behind a fully transparent canvas.  Both ends of that are
     * the common cases and neither divides.  Nothing underneath leaves the
     * source colour as it stands, and an opaque result is a plain mix. */
    if (ad == 0) {
        if (alpha)
            *alpha = as;

#define SET_PLANE(Plane, Val)                                               \
        if (Plane.p)                                                        \
            Plane.p[UPIPE_FREETYPE_OFFSET(Plane, x, y)] = (Val);

        SET_PLANE(canvas->y, colour[0])
        SET_PLANE(canvas->u, colour[1])
        SET_PLANE(canvas->v, colour[2])
#undef SET_PLANE

        if (canvas->uv.p) {
            size_t off = UPIPE_FREETYPE_OFFSET(canvas->uv, x, y);
            canvas->uv.p[off] = colour[1];
            canvas->uv.p[off + 1] = colour[2];
        }
        return;
    }

    unsigned wd = upipe_freetype_div255(ad * (0xff - as));
    unsigned ao = as + wd;
    if (alpha)
        *alpha = ao;
    if (unlikely(!ao))
        return;

#define BLEND_PLANE(Plane, Val)                                             \
    if (Plane.p) {                                                          \
        size_t off = UPIPE_FREETYPE_OFFSET(Plane, x, y);                    \
        unsigned v = Plane.p[off] * wd + (Val) * as;                        \
        Plane.p[off] = ao == 0xff ? upipe_freetype_div255(v) : v / ao;      \
    }

    BLEND_PLANE(canvas->y, colour[0])
    BLEND_PLANE(canvas->u, colour[1])
    BLEND_PLANE(canvas->v, colour[2])
#undef BLEND_PLANE

    if (canvas->uv.p) {
        size_t off = UPIPE_FREETYPE_OFFSET(canvas->uv, x, y);
        unsigned u = canvas->uv.p[off] * wd + colour[1] * as;
        unsigned v = canvas->uv.p[off + 1] * wd + colour[2] * as;
        canvas->uv.p[off] = ao == 0xff ? upipe_freetype_div255(u) : u / ao;
        canvas->uv.p[off + 1] =
            ao == 0xff ? upipe_freetype_div255(v) : v / ao;
    }
}

/** @internal @This draws a rectangle, or adds it to the measured box.
 *
 * @param target picture to draw into or box to extend
 * @param x left of the rectangle on the canvas
 * @param y top of the rectangle on the canvas
 * @param w width of the rectangle
 * @param h height of the rectangle
 * @param colour colour in YUVA
 */
static void upipe_freetype_target_rect(struct upipe_freetype_target *target,
                                       int x, int y, int w, int h,
                                       const uint8_t colour[4])
{
    if (w <= 0 || h <= 0 || !colour[3])
        return;

    if (target->canvas == NULL) {
        upipe_freetype_box_add(&target->box, x, y, x + w, y + h);
        return;
    }

    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            upipe_freetype_blend(target->canvas, x + i - target->origin_x,
                                 y + j - target->origin_y, colour, 0xff);
}

/** @internal @This draws a glyph bitmap, or adds it to the measured box.
 *
 * @param target picture to draw into or box to extend
 * @param x horizontal pen position on the canvas
 * @param y baseline position on the canvas
 * @param left left bearing of the glyph
 * @param top top bearing of the glyph
 * @param width width of the glyph bitmap
 * @param height height of the glyph bitmap
 * @param pitch octets between two lines of the glyph bitmap
 * @param buffer coverage values, or NULL when measuring
 * @param colour colour in YUVA
 */
static void upipe_freetype_target_glyph(struct upipe_freetype_target *target,
                                        int x, int y, int left, int top,
                                        int width, int height, int pitch,
                                        const unsigned char *buffer,
                                        const uint8_t colour[4])
{
    int x0 = x + left;
    int y0 = y - top;

    if (target->canvas == NULL || buffer == NULL) {
        upipe_freetype_box_add(&target->box, x0, y0, x0 + width, y0 + height);
        return;
    }

    for (int j = 0; j < height; j++)
        for (int i = 0; i < width; i++)
            upipe_freetype_blend(target->canvas, x0 + i - target->origin_x,
                                 y0 + j - target->origin_y, colour,
                                 buffer[j * pitch + i]);
}

/** @internal @This looks up the font at a given size.
 *
 * @param upipe description structure of the pipe
 * @param pixel_size size in pixels
 * @param size_p filled in with the font size handle
 * @return an error code
 */
static int upipe_freetype_lookup_size(struct upipe *upipe,
                                      unsigned pixel_size, FT_Size *size_p)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    FTC_ScalerRec scaler;

    memset(&scaler, 0, sizeof (scaler));
    scaler.face_id = upipe_freetype->font;
    scaler.width = pixel_size;
    scaler.height = pixel_size;
    scaler.pixel = 1;

    if (FTC_Manager_LookupSize(upipe_freetype->cache_manager, &scaler, size_p))
        return UBASE_ERR_INVALID;
    return UBASE_ERR_NONE;
}

/** @internal @This releases a glyph bitmap.
 *
 * @param bitmap glyph bitmap
 */
static void upipe_freetype_put_bitmap(struct upipe_freetype_bitmap *bitmap)
{
    if (bitmap->glyph != NULL) {
        FT_Done_Glyph(bitmap->glyph);
        bitmap->glyph = NULL;
    }
    bitmap->buffer = NULL;
}

/** @internal @This renders a glyph.
 *
 * Only one font file is configured, so bold and italic are synthesized by
 * emboldening and shearing a copy of the cached outline.  A run in the plain
 * style is taken from the small bitmap cache instead, which is already
 * rendered.
 *
 * @param upipe description structure of the pipe
 * @param pixel_size size in pixels
 * @param index glyph index in the font
 * @param flags style flags of the run
 * @param bitmap filled in with the glyph bitmap
 * @return an error code
 */
static int upipe_freetype_get_bitmap(struct upipe *upipe,
                                     unsigned pixel_size, FT_UInt index,
                                     uint8_t flags,
                                     struct upipe_freetype_bitmap *bitmap)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    FTC_ImageTypeRec type;

    memset(bitmap, 0, sizeof (*bitmap));

    type.face_id = upipe_freetype->font;
    type.width = pixel_size;
    type.height = pixel_size;
    type.flags = FT_LOAD_DEFAULT;

    if (!(flags & (UREF_TEXT_RUN_BOLD | UREF_TEXT_RUN_ITALIC))) {
        FTC_SBit sbit;
        /* a glyph the small bitmap cache would not hold comes back with no
         * buffer, and is rendered from the outline below */
        if (!FTC_SBitCache_Lookup(upipe_freetype->sbit_cache, &type, index,
                                  &sbit, NULL) && sbit->buffer != NULL) {
            bitmap->left = sbit->left;
            bitmap->top = sbit->top;
            bitmap->width = sbit->width;
            bitmap->height = sbit->height;
            bitmap->pitch = sbit->pitch;
            bitmap->buffer = sbit->buffer;
            bitmap->advance = sbit->xadvance << 16;
            return UBASE_ERR_NONE;
        }
    }

    FT_Glyph cached;
    if (FTC_ImageCache_Lookup(upipe_freetype->img_cache, &type, index,
                              &cached, NULL))
        return UBASE_ERR_INVALID;

    int32_t advance = cached->advance.x;

    FT_Glyph glyph;
    if (FT_Glyph_Copy(cached, &glyph))
        return UBASE_ERR_INVALID;

    if (glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_OutlineGlyph outline = (FT_OutlineGlyph)glyph;
        if (flags & UREF_TEXT_RUN_BOLD) {
            FT_Pos strength = pixel_size * EMBOLDEN_STRENGTH;
            FT_Outline_Embolden(&outline->outline, strength);
            /* 26.6 to 16.16 */
            advance += strength << 10;
        }
        if (flags & UREF_TEXT_RUN_ITALIC) {
            FT_Matrix matrix = { 0x10000, ITALIC_SHEAR, 0, 0x10000 };
            FT_Outline_Transform(&outline->outline, &matrix);
        }
    }

    if (FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, NULL, 1)) {
        FT_Done_Glyph(glyph);
        return UBASE_ERR_INVALID;
    }

    FT_BitmapGlyph slot = (FT_BitmapGlyph)glyph;
    bitmap->left = slot->left;
    bitmap->top = slot->top;
    bitmap->width = slot->bitmap.width;
    bitmap->height = slot->bitmap.rows;
    bitmap->pitch = slot->bitmap.pitch;
    bitmap->buffer = slot->bitmap.buffer;
    bitmap->advance = advance;
    bitmap->glyph = glyph;
    return UBASE_ERR_NONE;
}

/** @internal @This shapes the runs of a region into glyphs.
 *
 * @param upipe description structure of the pipe
 * @param region region to shape
 * @param runs run array
 * @param glyphs glyph array to fill in
 * @param max maximum number of glyphs to produce
 * @return the number of glyphs produced
 */
static unsigned upipe_freetype_shape(struct upipe *upipe,
                                     const struct upipe_freetype_region *region,
                                     const struct upipe_freetype_run *runs,
                                     struct upipe_freetype_glyph *glyphs,
                                     unsigned max)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    unsigned nb = 0;
    unsigned failed = 0;

    for (unsigned r = 0; r < region->nb_runs; r++) {
        const struct upipe_freetype_run *run = &runs[region->first_run + r];

        FT_Size size;
        if (unlikely(!ubase_check(upipe_freetype_lookup_size(upipe,
                            run->pixel_size, &size)))) {
            upipe_warn_va(upipe, "no font at %u pixels, dropping \"%s\"",
                          run->pixel_size, run->text);
            continue;
        }
        FT_Face face = size->face;
        FT_Bool use_kerning = FT_HAS_KERNING(face);
        FT_UInt previous = 0;
        bool break_before = !!(run->flags & UREF_TEXT_RUN_BREAK);

        for (size_t i = 0; run->text[i] != '\0' && nb < max;) {
            size_t char_size = 0;
            uint32_t c = unicode_character(&run->text[i], &char_size);
            if (char_size == 0)
                break;
            i += char_size;

            if (c == '\n' || c == '\r') {
                break_before = true;
                previous = 0;
                continue;
            }

            FT_UInt index = FTC_CMapCache_Lookup(upipe_freetype->cmap_cache,
                                                 upipe_freetype->font, -1, c);

            struct upipe_freetype_bitmap bitmap;
            if (unlikely(!ubase_check(upipe_freetype_get_bitmap(upipe,
                                run->pixel_size, index, run->flags,
                                &bitmap)))) {
                failed++;
                continue;
            }

            struct upipe_freetype_glyph *glyph = &glyphs[nb++];
            glyph->index = index;
            glyph->character = c;
            glyph->advance = bitmap.advance;
            glyph->kerning = 0;
            glyph->x = 0;
            glyph->left = bitmap.left;
            glyph->top = bitmap.top;
            glyph->width = bitmap.width;
            glyph->height = bitmap.height;
            glyph->run = region->first_run + r;
            glyph->break_before = break_before;
            break_before = false;

            if (use_kerning && previous) {
                FT_Vector delta;
                FT_Get_Kerning(face, previous, index, FT_KERNING_DEFAULT,
                               &delta);
                /* 26.6 to 16.16 */
                glyph->kerning = delta.x << 10;
            }
            previous = index;

            upipe_freetype_put_bitmap(&bitmap);
        }
    }

    if (unlikely(failed))
        upipe_warn_va(upipe, "%u character(s) had no glyph in the font",
                      failed);

    return nb;
}

/** @internal @This breaks the glyphs of a region into lines.
 *
 * @param region region to lay out
 * @param runs run array
 * @param glyphs glyph array of the region, pen positions are filled in
 * @param nb_glyphs number of glyphs
 * @param lines line array to fill in
 * @param max maximum number of lines to produce
 * @return the number of lines produced
 */
static unsigned upipe_freetype_break_lines(
        const struct upipe_freetype_region *region,
        const struct upipe_freetype_run *runs,
        struct upipe_freetype_glyph *glyphs, unsigned nb_glyphs,
        struct upipe_freetype_line *lines, unsigned max)
{
    int avail = region->hsize - 2 * region->line_padding;
    if (avail < 0)
        avail = 0;
    int64_t limit = (int64_t)avail << 16;

    unsigned nb_lines = 0;
    unsigned first = 0;
    int64_t x = 0;
    /* index of the glyph after the last space seen on the line, where the
     * line is broken when it overflows */
    unsigned wrap = 0;
    int64_t wrap_x = 0;

    for (unsigned i = 0; i < nb_glyphs && nb_lines < max; i++) {
        int64_t advance = glyphs[i].advance;
        if (i != first)
            advance += glyphs[i].kerning;

        bool overflow = region->wrap && limit > 0 && i != first &&
            glyphs[i].character != ' ' && x + advance > limit;

        if (glyphs[i].break_before || overflow) {
            unsigned end = i;
            int64_t width = x;
            if (overflow && wrap > first) {
                /* break after the last space, which is left at the end of the
                 * line where it does not show */
                end = wrap;
                width = wrap_x;
            }

            /* a break on the very first glyph is the start of the block, not
             * an empty line above it */
            if (end > first || nb_lines) {
                struct upipe_freetype_line *line = &lines[nb_lines++];
                line->first = first;
                line->nb = end - first;
                line->width = width;
            }

            first = end;
            x = 0;
            wrap = first;
            wrap_x = 0;
            for (unsigned j = first; j <= i; j++) {
                glyphs[j].x = x;
                x += glyphs[j].advance + (j != first ? glyphs[j].kerning : 0);
                if (glyphs[j].character == ' ') {
                    wrap = j + 1;
                    wrap_x = glyphs[j].x;
                }
            }
            continue;
        }

        glyphs[i].x = x;
        x += advance;
        if (glyphs[i].character == ' ') {
            wrap = i + 1;
            wrap_x = glyphs[i].x;
        }
    }

    if (first < nb_glyphs && nb_lines < max) {
        struct upipe_freetype_line *line = &lines[nb_lines++];
        line->first = first;
        line->nb = nb_glyphs - first;
        line->width = x;
    }

    for (unsigned i = 0; i < nb_lines; i++) {
        struct upipe_freetype_line *line = &lines[i];
        line->ascender = 0;
        line->descender = 0;
        line->height = 0;
        for (unsigned j = line->first; j < line->first + line->nb; j++) {
            const struct upipe_freetype_run *run = &runs[glyphs[j].run];
            if (run->ascender > line->ascender)
                line->ascender = run->ascender;
            if (run->descender > line->descender)
                line->descender = run->descender;
            if (run->height > line->height)
                line->height = run->height;
        }
        if (region->line_height)
            line->height = region->line_height << 6;
        if (line->height < line->ascender + line->descender)
            line->height = line->ascender + line->descender;
    }

    return nb_lines;
}

/** @internal @This draws a region, or measures the box it occupies.
 *
 * @param upipe description structure of the pipe
 * @param target picture to draw into or box to extend
 * @param region region to render
 * @param runs run array
 * @param glyphs glyph array
 * @param lines line array
 */
static void upipe_freetype_render_region(struct upipe *upipe,
        struct upipe_freetype_target *target,
        const struct upipe_freetype_region *region,
        const struct upipe_freetype_run *runs,
        const struct upipe_freetype_glyph *glyphs,
        const struct upipe_freetype_line *lines)
{
    if (region->has_bg)
        upipe_freetype_target_rect(target, region->x, region->y,
                                   region->hsize, region->vsize, region->bg);

    int64_t block_height = 0;
    for (unsigned i = 0; i < region->nb_lines; i++)
        block_height += lines[region->first_line + i].height;

    int block_y = region->y;
    switch (region->display_align) {
        case UREF_TEXT_DISPLAY_ALIGN_CENTER:
            block_y += (region->vsize - (int)(block_height >> 6)) / 2;
            break;
        case UREF_TEXT_DISPLAY_ALIGN_AFTER:
            block_y += region->vsize - (int)(block_height >> 6);
            break;
        default:
            break;
    }

    int avail = region->hsize - 2 * region->line_padding;
    if (avail < 0)
        avail = 0;

    uint8_t align = region->text_align;
    switch (region->multi_row_align) {
        case UREF_TEXT_MULTI_ROW_ALIGN_START:
            align = UREF_TEXT_ALIGN_START;
            break;
        case UREF_TEXT_MULTI_ROW_ALIGN_CENTER:
            align = UREF_TEXT_ALIGN_CENTER;
            break;
        case UREF_TEXT_MULTI_ROW_ALIGN_END:
            align = UREF_TEXT_ALIGN_END;
            break;
        default:
            break;
    }

    int64_t line_top = (int64_t)block_y << 6;
    for (unsigned l = 0; l < region->nb_lines; l++) {
        const struct upipe_freetype_line *line = &lines[region->first_line + l];
        int width = (int)((line->width + 0x8000) >> 16);

        int x0 = region->x + region->line_padding;
        if (align == UREF_TEXT_ALIGN_CENTER)
            x0 += (avail - width) / 2;
        else if (align == UREF_TEXT_ALIGN_END)
            x0 += avail - width;

        /* centre the ascender and descender of the line in its line box */
        int64_t baseline = line_top +
            (line->height - line->ascender - line->descender) / 2 +
            line->ascender;

        int box_y0, box_y1;
        if (region->fill_line_gap) {
            box_y0 = (int)(line_top >> 6);
            box_y1 = (int)((line_top + line->height) >> 6);
        } else {
            box_y0 = (int)((baseline - line->ascender) >> 6);
            box_y1 = (int)((baseline + line->descender) >> 6);
        }

        /* backgrounds first: they are drawn per group of adjacent glyphs
         * sharing a colour so a span keeps a continuous box */
        unsigned g = line->first;
        while (g < line->first + line->nb) {
            const struct upipe_freetype_run *run = &runs[glyphs[g].run];
            unsigned h = g;
            while (h < line->first + line->nb &&
                   runs[glyphs[h].run].has_bg == run->has_bg &&
                   !memcmp(runs[glyphs[h].run].bg, run->bg, 4))
                h++;

            if (run->has_bg) {
                int bx0 = x0 + (int)(glyphs[g].x >> 16);
                const struct upipe_freetype_glyph *last = &glyphs[h - 1];
                int bx1 = x0 + (int)((last->x + last->advance + 0xffff) >> 16);
                /* ebutts:linePadding widens the box at both ends of the line */
                if (g == line->first)
                    bx0 -= region->line_padding;
                if (h == line->first + line->nb)
                    bx1 += region->line_padding;
                upipe_freetype_target_rect(target, bx0, box_y0,
                                           bx1 - bx0, box_y1 - box_y0,
                                           run->bg);
            }
            g = h;
        }

        for (unsigned i = line->first; i < line->first + line->nb; i++) {
            const struct upipe_freetype_glyph *glyph = &glyphs[i];
            const struct upipe_freetype_run *run = &runs[glyph->run];
            int pen = x0 + (int)(glyph->x >> 16);

            if (target->canvas == NULL) {
                upipe_freetype_target_glyph(target, pen,
                        (int)(baseline >> 6), glyph->left, glyph->top,
                        glyph->width, glyph->height, 0, NULL, run->fg);
                continue;
            }

            struct upipe_freetype_bitmap bitmap;
            if (unlikely(!ubase_check(upipe_freetype_get_bitmap(upipe,
                                run->pixel_size, glyph->index, run->flags,
                                &bitmap))))
                continue;
            upipe_freetype_target_glyph(target, pen, (int)(baseline >> 6),
                    bitmap.left, bitmap.top, bitmap.width, bitmap.height,
                    bitmap.pitch, bitmap.buffer, run->fg);
            upipe_freetype_put_bitmap(&bitmap);
        }

        /* underlines last so they are not cut by a glyph background */
        g = line->first;
        while (g < line->first + line->nb) {
            const struct upipe_freetype_run *run = &runs[glyphs[g].run];
            unsigned h = g;
            while (h < line->first + line->nb && glyphs[h].run == glyphs[g].run)
                h++;

            if (run->flags & UREF_TEXT_RUN_UNDERLINE) {
                int thickness = run->pixel_size / 16;
                if (thickness < 1)
                    thickness = 1;
                int ux0 = x0 + (int)(glyphs[g].x >> 16);
                const struct upipe_freetype_glyph *last = &glyphs[h - 1];
                int ux1 = x0 + (int)((last->x + last->advance + 0xffff) >> 16);
                int uy = (int)((baseline + run->descender / 2) >> 6);
                upipe_freetype_target_rect(target, ux0, uy, ux1 - ux0,
                                           thickness, run->fg);
            }
            g = h;
        }

        line_top += line->height;
    }
}
/** @internal @This reads the regions and runs of a page.
 *
 * @param upipe description structure of the pipe
 * @param uref page description
 * @param regions region array to fill in
 * @param nb_regions number of regions
 * @param runs run array to fill in
 * @param nb_runs_p filled in with the number of runs
 * @return an error code
 */
static int upipe_freetype_read_page(struct upipe *upipe, struct uref *uref,
                                    struct upipe_freetype_region *regions,
                                    uint8_t nb_regions,
                                    struct upipe_freetype_run *runs,
                                    unsigned *nb_runs_p)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    unsigned nb_runs = 0;

    for (uint8_t r = 0; r < nb_regions; r++) {
        struct upipe_freetype_region *region = &regions[r];
        uint64_t v;
        uint8_t u;

        memset(region, 0, sizeof (*region));

        UBASE_RETURN(uref_text_get_region_x(uref, &v, r))
        region->x = v;
        UBASE_RETURN(uref_text_get_region_y(uref, &v, r))
        region->y = v;
        UBASE_RETURN(uref_text_get_region_hsize(uref, &v, r))
        region->hsize = v;
        UBASE_RETURN(uref_text_get_region_vsize(uref, &v, r))
        region->vsize = v;

        region->display_align = UREF_TEXT_DISPLAY_ALIGN_BEFORE;
        if (ubase_check(uref_text_get_region_display_align(uref, &u, r)))
            region->display_align = u;
        region->text_align = UREF_TEXT_ALIGN_START;
        if (ubase_check(uref_text_get_region_text_align(uref, &u, r)))
            region->text_align = u;
        region->multi_row_align = UREF_TEXT_MULTI_ROW_ALIGN_AUTO;
        if (ubase_check(uref_text_get_region_multi_row_align(uref, &u, r)))
            region->multi_row_align = u;
        if (ubase_check(uref_text_get_region_line_height(uref, &v, r)))
            region->line_height = v;
        if (ubase_check(uref_text_get_region_line_padding(uref, &v, r)))
            region->line_padding = v;
        if (ubase_check(uref_text_get_region_background(uref, &v, r))) {
            uint8_t rgba[4] = { v >> 24, v >> 16, v >> 8, v };
            ubuf_pic_rgba_to_yuva(rgba, upipe_freetype->fullrange ? 1 : 0,
                                  region->bg);
            region->has_bg = !!rgba[3];
        }
        region->wrap = !ubase_check(uref_text_get_region_no_wrap(uref, r));
        region->fill_line_gap =
            ubase_check(uref_text_get_region_fill_line_gap(uref, r));

        UBASE_RETURN(uref_text_get_region_runs(uref, &u, r))
        region->first_run = nb_runs;
        region->nb_runs = u;

        for (uint8_t k = 0; k < u; k++) {
            struct upipe_freetype_run *run = &runs[nb_runs];
            uint8_t index = nb_runs;

            memset(run, 0, sizeof (*run));
            UBASE_RETURN(uref_text_get_run_text(uref, &run->text, index))
            UBASE_RETURN(uref_text_get_run_font_size(uref, &v, index))
            run->pixel_size = v ? v : 1;
            uref_text_get_run_flags(uref, &run->flags, index);

            uint8_t rgba[4] = { 0xff, 0xff, 0xff, 0xff };
            if (ubase_check(uref_text_get_run_color(uref, &v, index))) {
                rgba[0] = v >> 24;
                rgba[1] = v >> 16;
                rgba[2] = v >> 8;
                rgba[3] = v;
            }
            ubuf_pic_rgba_to_yuva(rgba, upipe_freetype->fullrange ? 1 : 0,
                                  run->fg);

            if (ubase_check(uref_text_get_run_background(uref, &v, index))) {
                uint8_t bg[4] = { v >> 24, v >> 16, v >> 8, v };
                ubuf_pic_rgba_to_yuva(bg, upipe_freetype->fullrange ? 1 : 0,
                                      run->bg);
                run->has_bg = !!bg[3];
            }

            FT_Size size;
            if (ubase_check(upipe_freetype_lookup_size(upipe,
                                run->pixel_size, &size))) {
                run->ascender = size->metrics.ascender;
                run->descender = -size->metrics.descender;
                run->height = size->metrics.height;
            }
            if (run->ascender <= 0)
                run->ascender = (run->pixel_size * 4 / 5) << 6;
            if (run->descender < 0)
                run->descender = (run->pixel_size / 5) << 6;
            if (run->height <= 0)
                run->height = (run->pixel_size * 5 / 4) << 6;

            nb_runs++;
        }
    }

    *nb_runs_p = nb_runs;
    return UBASE_ERR_NONE;
}

/** @internal @This renders a styled text page.
 *
 * The picture is cropped to what is actually drawn and the distances to the
 * edges of the canvas are put on the flow definition as pic paddings, the
 * same convention as the DVB subtitle decoder: downstream rebuilds the canvas
 * from the size and the paddings, so a subtitle that covers a couple of lines
 * is not rescaled and alpha blended over the whole picture on every frame.
 *
 * @param upipe description structure of the pipe
 * @param uref page description
 * @param nb_regions number of regions of the page
 * @param upump_p reference to pump that generated the buffer
 * @return true if the buffer was output
 */
static bool upipe_freetype_handle_page(struct upipe *upipe, struct uref *uref,
                                       uint8_t nb_regions,
                                       struct upump **upump_p)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    /* the size the ubuf manager was negotiated for is the canvas, unless the
     * page carries one of its own */
    uint64_t canvas_hsize = 0;
    uint64_t canvas_vsize = 0;
    uref_pic_flow_get_hsize(upipe_freetype->flow_format, &canvas_hsize);
    uref_pic_flow_get_vsize(upipe_freetype->flow_format, &canvas_vsize);
    uref_text_get_canvas_hsize(uref, &canvas_hsize);
    uref_text_get_canvas_vsize(uref, &canvas_vsize);
    if (unlikely(!canvas_hsize || !canvas_vsize)) {
        upipe_warn(upipe, "no canvas size");
        uref_free(uref);
        return true;
    }

    if (unlikely(nb_regions > MAX_PAGE_REGIONS)) {
        upipe_warn_va(upipe, "%"PRIu8" regions, keeping %u",
                      nb_regions, MAX_PAGE_REGIONS);
        nb_regions = MAX_PAGE_REGIONS;
    }

    unsigned nb_runs = 0;
    for (uint8_t r = 0; r < nb_regions; r++) {
        uint8_t runs;
        if (ubase_check(uref_text_get_region_runs(uref, &runs, r)))
            nb_runs += runs;
    }

    if (unlikely(nb_runs > MAX_RUNS)) {
        upipe_warn_va(upipe, "too many runs (%u)", nb_runs);
        uref_free(uref);
        return true;
    }

    struct upipe_freetype_region *regions = upipe_freetype->regions;
    struct upipe_freetype_run *runs = upipe_freetype->runs;
    struct upipe_freetype_glyph *glyphs = upipe_freetype->glyphs;
    struct upipe_freetype_line *lines = upipe_freetype->lines;

    if (unlikely(!ubase_check(upipe_freetype_read_page(upipe, uref, regions,
                                  nb_regions, runs, &nb_runs)))) {
        upipe_warn(upipe, "invalid page");
        uref_dump(uref, upipe->uprobe);
        uref_free(uref);
        return true;
    }

    unsigned nb_glyphs = 0;
    unsigned nb_lines = 0;
    for (uint8_t r = 0; r < nb_regions; r++) {
        struct upipe_freetype_region *region = &regions[r];

        region->first_glyph = nb_glyphs;
        region->nb_glyphs = upipe_freetype_shape(upipe, region, runs,
                glyphs + nb_glyphs, MAX_GLYPHS - nb_glyphs);
        nb_glyphs += region->nb_glyphs;

        region->first_line = nb_lines;
        region->nb_lines = upipe_freetype_break_lines(region, runs,
                glyphs + region->first_glyph, region->nb_glyphs,
                lines + nb_lines, MAX_LINES);
        nb_lines += region->nb_lines;

        /* the line glyph indexes are relative to the region */
        for (unsigned l = 0; l < region->nb_lines; l++)
            lines[region->first_line + l].first += region->first_glyph;
    }

    struct upipe_freetype_target target;
    memset(&target, 0, sizeof (target));
    for (uint8_t r = 0; r < nb_regions; r++)
        upipe_freetype_render_region(upipe, &target, &regions[r], runs,
                                     glyphs, lines);

    struct upipe_freetype_box box = target.box;
    if (box.x0 < 0)
        box.x0 = 0;
    if (box.y0 < 0)
        box.y0 = 0;
    if (box.x1 > (int)canvas_hsize)
        box.x1 = canvas_hsize;
    if (box.y1 > (int)canvas_vsize)
        box.y1 = canvas_vsize;

    upipe_dbg_va(upipe, "page of %"PRIu8" region(s) and %u run(s): %u glyph(s) "
            "on %u line(s), box %d,%d..%d,%d of a %"PRIu64"x%"PRIu64" canvas",
            nb_regions, nb_runs, nb_glyphs, nb_lines,
            box.x0, box.y0, box.x1, box.y1, canvas_hsize, canvas_vsize);

    if (box.x0 >= box.x1 || box.y0 >= box.y1) {
        /* Nothing to show, but the subpicture still has to replace the
         * previous one downstream.  Only a page with no region at all is
         * meant to be empty: anything else laid out something and lost it. */
        if (nb_regions)
            upipe_warn_va(upipe, "%"PRIu8" region(s) and %u run(s) drew "
                    "nothing, %u glyph(s)", nb_regions, nb_runs, nb_glyphs);
        box.x0 = box.y0 = box.x1 = box.y1 = 0;
    }

    /* A page that draws a single narrow glyph is as small as one that draws
     * nothing, so put the floor on every picture rather than on the empty
     * one. */
    upipe_freetype_grow(&box.x0, &box.x1, MIN_PICTURE_SIZE, canvas_hsize);
    upipe_freetype_grow(&box.y0, &box.y1, MIN_PICTURE_SIZE, canvas_vsize);

    /* keep the crop on even boundaries for a possible 4:2:0 conversion */
    box.x0 &= ~1;
    box.y0 &= ~1;
    box.x1 = (box.x1 + 1) & ~1;
    box.y1 = (box.y1 + 1) & ~1;
    if (box.x1 > (int)canvas_hsize)
        box.x1 = (int)(canvas_hsize & ~(uint64_t)1);
    if (box.y1 > (int)canvas_vsize)
        box.y1 = (int)(canvas_vsize & ~(uint64_t)1);

    int hsize = box.x1 - box.x0;
    int vsize = box.y1 - box.y0;

    struct ubuf *ubuf = ubuf_pic_alloc(upipe_freetype->ubuf_mgr, hsize, vsize);
    if (unlikely(ubuf == NULL)) {
        upipe_err(upipe, "could not allocate picture");
        uref_free(uref);
        return true;
    }

    struct upipe_freetype_canvas canvas;
    if (unlikely(!ubase_check(upipe_freetype_map(upipe, ubuf, &canvas)))) {
        upipe_err(upipe, "could not map picture");
        ubuf_free(ubuf);
        uref_free(uref);
        return true;
    }

    uint8_t clear[4] = {
        upipe_freetype->background[0], upipe_freetype->background[1],
        upipe_freetype->background[2],
        /* a page composites onto the video, so start transparent when there
         * is somewhere to say so */
        canvas.a.p != NULL ? 0 : upipe_freetype->background[3]
    };
    upipe_freetype_fill(&canvas, clear);

    target.canvas = &canvas;
    target.origin_x = box.x0;
    target.origin_y = box.y0;
    for (uint8_t r = 0; r < nb_regions; r++)
        upipe_freetype_render_region(upipe, &target, &regions[r], runs,
                                     glyphs, lines);

    upipe_freetype_unmap(ubuf, &canvas);

    /* Built from the negotiated picture format rather than from the flow
     * definition last stored, which is the previous page's geometry. */
    struct uref *flow_def = uref_dup(upipe_freetype->flow_format);
    if (unlikely(flow_def == NULL)) {
        ubuf_free(ubuf);
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }
    UBASE_FATAL(upipe, uref_pic_flow_set_hsize(flow_def, hsize))
    UBASE_FATAL(upipe, uref_pic_flow_set_vsize(flow_def, vsize))
    UBASE_FATAL(upipe, uref_pic_flow_set_hsize_visible(flow_def, hsize))
    UBASE_FATAL(upipe, uref_pic_flow_set_vsize_visible(flow_def, vsize))
    UBASE_FATAL(upipe, uref_pic_set_lpadding(flow_def, box.x0))
    UBASE_FATAL(upipe, uref_pic_set_rpadding(flow_def, canvas_hsize - box.x1))
    UBASE_FATAL(upipe, uref_pic_set_tpadding(flow_def, box.y0))
    UBASE_FATAL(upipe, uref_pic_set_bpadding(flow_def, canvas_vsize - box.y1))

    upipe_dbg_va(upipe, "picture %dx%d padded %d/%"PRIu64"/%d/%"PRIu64
            " out of a %"PRIu64"x%"PRIu64" canvas",
            hsize, vsize, box.x0, canvas_hsize - box.x1,
            box.y0, canvas_vsize - box.y1, canvas_hsize, canvas_vsize);

    upipe_freetype_store_flow_def(upipe, flow_def);

    uref_attach_ubuf(uref, ubuf);
    upipe_freetype_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This tries to output input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer to output
 * @param upump_p reference to pump that generated the buffer
 * @return true if the buffer was output
 */
static bool upipe_freetype_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    if (!upipe_freetype->ubuf_mgr)
        return false;

    uint8_t nb_regions;
    if (ubase_check(uref_text_get_regions(uref, &nb_regions))) {
        if (unlikely(upipe_freetype->font == NULL)) {
            upipe_warn(upipe, "no font set");
            uref_free(uref);
            return true;
        }
        return upipe_freetype_handle_page(upipe, uref, nb_regions, upump_p);
    }

    if (unlikely(!upipe_freetype->face)) {
        upipe_warn(upipe, "no font set");
        uref_free(uref);
        return true;
    }

    const char *text;
    int r = uref_void_get_text(uref, &text);
    if (!ubase_check(r) || !text) {
        uref_dump(uref, upipe->uprobe);
        text = "fail";
    }

    if (upipe_freetype->text && !strcmp(upipe_freetype->text, text)) {
        /* cache hit */
        uref_attach_ubuf(uref, ubuf_dup(upipe_freetype->ubuf));
        upipe_freetype_output(upipe, uref, upump_p);
        return true;
    }

    if (unlikely(!ubase_check(upipe_freetype_throw_new_text(upipe, text))))
        upipe_warn(upipe, "fail to send probe");

    uint64_t hsize = upipe_freetype->hsize;
    uint64_t vsize = upipe_freetype->vsize;
    struct ubuf *ubuf = ubuf_pic_alloc(upipe_freetype->ubuf_mgr, hsize, vsize);
    if (!ubuf) {
        upipe_err(upipe, "Could not allocate pic");
        uref_free(uref);
        return true;
    }

    struct upipe_freetype_canvas canvas;
    if (unlikely(!ubase_check(upipe_freetype_map(upipe, ubuf, &canvas)))) {
        upipe_err(upipe, "fail to map pic buffer");
        ubuf_free(ubuf);
        uref_free(uref);
        return true;
    }
    upipe_freetype_fill(&canvas, upipe_freetype->background);

    struct upipe_freetype_target target;
    memset(&target, 0, sizeof (target));
    target.canvas = &canvas;

    FT_Bool use_kerning = FT_HAS_KERNING(upipe_freetype->face);
    FT_UInt previous = 0;
    /* scale offset to 16.16 */
    int64_t xoff = upipe_freetype->xoff << 16;
    int64_t yoff = upipe_freetype->yoff << 16;

    for (size_t i = 0; text[i] != '\0';) {
        size_t char_size = 0;
        uint32_t c = unicode_character(&text[i], &char_size);
        if (char_size == 0)
            break;

        i += char_size;

        FT_UInt index = FTC_CMapCache_Lookup(upipe_freetype->cmap_cache,
                                             upipe_freetype->font, -1, c);

        if (use_kerning && previous) {
            FT_Vector delta;
            FT_Get_Kerning(upipe_freetype->face, previous, index,
                           FT_KERNING_DEFAULT, &delta);
            /* delta is 26.6, scale to 16.16 */
            xoff += delta.x << 10;
        }

        struct upipe_freetype_bitmap bitmap;
        if (unlikely(!ubase_check(upipe_freetype_get_bitmap(upipe,
                            upipe_freetype->pixel_size, index, 0, &bitmap))))
            continue;

        upipe_freetype_target_glyph(&target, xoff >> 16, yoff >> 16,
                bitmap.left, bitmap.top, bitmap.width, bitmap.height,
                bitmap.pitch, bitmap.buffer, upipe_freetype->foreground);

        /* increment pen position */
        xoff += bitmap.advance;
        previous = index;

        upipe_freetype_put_bitmap(&bitmap);
    }

    upipe_freetype_unmap(ubuf, &canvas);

    /* This path fills the negotiated format, so that is what it advertises. */
    if (upipe_freetype->flow_def == NULL) {
        struct uref *flow_def = uref_dup(upipe_freetype->flow_format);
        if (unlikely(flow_def == NULL)) {
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return true;
        }
        upipe_freetype_store_flow_def(upipe, flow_def);
    }

    upipe_freetype_flush_cache(upipe);
    upipe_freetype->ubuf = ubuf;
    upipe_freetype->text = strdup(text);
    uref_attach_ubuf(uref, ubuf_dup(upipe_freetype->ubuf));
    upipe_freetype_output(upipe, uref, upump_p);
    return true;
}

/** @internal
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_freetype_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    if (!upipe_freetype_check_input(upipe)) {
        upipe_freetype_hold_input(upipe, uref);
        upipe_freetype_block_input(upipe, upump_p);
    }
    else if (!upipe_freetype_handle(upipe, uref, upump_p)) {
        upipe_freetype_hold_input(upipe, uref);
        upipe_freetype_block_input(upipe, upump_p);
        upipe_use(upipe);
    }
}

/** @internal @This sets the background color.
 *
 * @param upipe description structure of the pipe
 * @param r red component
 * @param g green component
 * @param b blue component
 * @param a alpha component
 * @return an error code
 */
static int _upipe_freetype_set_background_color(struct upipe *upipe,
                                                uint8_t r, uint8_t g,
                                                uint8_t b, uint8_t a)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    uint8_t rgba[4] = { r, g, b, a };
    ubuf_pic_rgba_to_yuva(rgba, upipe_freetype->fullrange ? 1 : 0,
                          upipe_freetype->background);
    upipe_freetype_flush_cache(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the foreground color.
 *
 * @param upipe description structure of the pipe
 * @param r red component
 * @param g green component
 * @param b blue component
 * @param a alpha component
 * @return an error code
 */
static int _upipe_freetype_set_foreground_color(struct upipe *upipe,
                                                uint8_t r, uint8_t g,
                                                uint8_t b, uint8_t a)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    uint8_t rgba[4] = { r, g, b, a };
    ubuf_pic_rgba_to_yuva(rgba, upipe_freetype->fullrange ? 1 : 0,
                          upipe_freetype->foreground);
    upipe_freetype_flush_cache(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This sets a freetype option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param value value of the option
 * @return an error code
 */
static int upipe_freetype_set_option(struct upipe *upipe,
                                     const char *option, const char *value)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    if (!strcmp(option, "font")) {
        if (value && upipe_freetype->font &&
            !strcmp(value, upipe_freetype->font))
            return UBASE_ERR_NONE;
        if (!value && !upipe_freetype->font)
            return UBASE_ERR_NONE;

        upipe_freetype->face = NULL;
        FTC_Manager_RemoveFaceID(upipe_freetype->cache_manager,
                                 upipe_freetype->font);
        free(upipe_freetype->font);
        upipe_freetype->font = value ? strdup(value) : NULL;
        upipe_freetype_flush_cache(upipe);
        return upipe_freetype_load_face(upipe);
    }
    else if (!strcmp(option, "foreground-color")) {
        uint8_t rgba[4] = { 0xff, 0xff, 0xff, 0xff };
        if (!value)
            return _upipe_freetype_set_foreground_color(
                upipe, rgba[0], rgba[1], rgba[2], rgba[3]);

        int ret = ubuf_pic_parse_rgba(value, rgba);
        if (unlikely(!ubase_check(ret)))
            return ret;

        return _upipe_freetype_set_foreground_color(upipe, rgba[0], rgba[1],
                                                    rgba[2], rgba[3]);
    }
    else if (!strcmp(option, "background-color")) {
        uint8_t rgba[4] = { 0, 0, 0, 0xff };

        if (!value)
            return _upipe_freetype_set_background_color(
                upipe, rgba[0], rgba[1], rgba[2], rgba[3]);
        int ret = ubuf_pic_parse_rgba(value, rgba);
        if (unlikely(!ubase_check(ret)))
            return ret;

        return _upipe_freetype_set_background_color(upipe, rgba[0], rgba[1],
                                                    rgba[2], rgba[3]);
    }
    return UBASE_ERR_INVALID;
}

/** @internal @This sets the input flow def.
 *
 * @param upipe description structure of the pipe
 * @param flow_def new flow def to set
 * @return an error code
 */
static int upipe_freetype_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    if (!flow_def)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "void.text."));

    struct uref *flow_format = uref_dup(upipe_freetype->flow_output);
    UBASE_ALLOC_RETURN(flow_format);

    if (urequest_get_opaque(&upipe_freetype->ubuf_mgr_request,
                            struct upipe *) != NULL) {
        upipe_freetype_unregister_output_request(
            upipe, &upipe_freetype->ubuf_mgr_request);
        urequest_clean(&upipe_freetype->ubuf_mgr_request);
        upipe_freetype_clean_ubuf_mgr(upipe);
        upipe_freetype_init_ubuf_mgr(upipe);
    }
    upipe_freetype_require_flow_format(upipe, flow_format);
    // TODO : x/y/offsets

    return UBASE_ERR_NONE;
}

/** @internal @This sets the freetype pixel size.
 *
 * @param upipe description structure of the pipe
 * @param pixel_size pixel size to set
 * @return an error code
 */
static int _upipe_freetype_set_pixel_size(struct upipe *upipe,
                                          uint64_t pixel_size)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    upipe_freetype->pixel_size = pixel_size;
    upipe_freetype_flush_cache(upipe);
    return upipe_freetype_load_face(upipe);
}

/** @internal @This gets the x and y minimum and maximum values of the rendered
 * characters.
 *
 * @param upipe description structure of the pipe
 * @param str a string with the rendered characters
 * @param bbox_p filled with x and y minimum and maximum value
 * @return an error code
 */
static int _upipe_freetype_get_bbox(struct upipe *upipe,
                                    const char *str,
                                    struct upipe_freetype_bbox *bbox_p)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    struct upipe_freetype_bbox bbox;
    bbox.x = 0;
    bbox.y = 0;
    bbox.width = 0;
    bbox.height = 0;

    FT_Bool use_kerning = FT_HAS_KERNING(upipe_freetype->face);
    FT_UInt previous = 0;
    FT_Pos yMax = 0;
    bool first = true;
    int64_t width = 0;

    for (size_t i = 0; str[i] != '\0';) {
        size_t char_size = 0;
        uint32_t c = unicode_character(&str[i], &char_size);
        if (char_size == 0)
            break;
        i += char_size;

        FT_UInt index = FTC_CMapCache_Lookup(upipe_freetype->cmap_cache,
                                             upipe_freetype->font, -1, c);
        if (use_kerning && previous) {
            FT_Vector delta;
            FT_Get_Kerning(upipe_freetype->face, previous, index,
                           FT_KERNING_DEFAULT, &delta);
            /* delta is 26.6, scale to 16.16 */
            width += delta.x << 10;
        }

        FTC_ImageTypeRec type;
        type.face_id = upipe_freetype->font;
        type.width = upipe_freetype->pixel_size;
        type.height = upipe_freetype->pixel_size;
        type.flags = FT_LOAD_DEFAULT;
        FT_Glyph glyph;
        if (FTC_ImageCache_Lookup(upipe_freetype->img_cache,
                                  &type, index,
                                  &glyph, NULL))
            continue;

        FT_BBox ft_bbox;
        FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS, &ft_bbox);

        if (first) {
            bbox.x = ft_bbox.xMin;
            first = false;
        }
        if (ft_bbox.yMin < bbox.y)
            bbox.y = ft_bbox.yMin;
        if (ft_bbox.yMax > yMax)
            yMax = ft_bbox.yMax;
        width += glyph->advance.x;

        previous = index;
    }

    if (yMax > bbox.y)
        bbox.height = yMax - bbox.y;

    if (width > 0)
        /* get width ceil and downscale 16.16 integer */
        bbox.width = (width + 0xffff) >> 16;

    if (bbox_p)
        *bbox_p = bbox;

    return UBASE_ERR_NONE;
}

/** @internal @This gets the unscaled advance value for a string.
 *
 * @param upipe description structure of the pipe
 * @param str string to get the advance value
 * @param advance_p filled with the compute advance value
 * @param units_per_EM_p filled with the units per EM of the font
 * @return an error code
 */
static int _upipe_freetype_get_advance(struct upipe *upipe,
                                       const char *str,
                                       uint64_t *advance_p,
                                       uint64_t *units_per_EM_p)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    if (!upipe_freetype->face ||
        !FT_IS_SCALABLE(upipe_freetype->face))
        return UBASE_ERR_INVALID;

    uint64_t total_advance = 0;
    uint64_t units_per_EM = upipe_freetype->face->units_per_EM;

    FT_Bool use_kerning = FT_HAS_KERNING(upipe_freetype->face);
    FT_UInt previous = 0;

    for (size_t i = 0; str[i] != '\0';) {
        size_t char_size = 0;
        uint32_t c = unicode_character(&str[i], &char_size);
        if (char_size == 0)
            break;
        i += char_size;

        FT_UInt index = FTC_CMapCache_Lookup(upipe_freetype->cmap_cache,
                                             upipe_freetype->font, -1, c);
        if (use_kerning && previous) {
            FT_Vector delta;
            FT_Get_Kerning(upipe_freetype->face, previous, index,
                           FT_KERNING_UNSCALED, &delta);
            total_advance += delta.x;
        }

        FT_Fixed advance;
        if (!FT_Get_Advance(upipe_freetype->face, index, FT_LOAD_NO_SCALE,
                            &advance))
            total_advance += advance;

        previous = index;
    }

    if (advance_p)
        *advance_p = total_advance;
    if (units_per_EM_p)
        *units_per_EM_p = units_per_EM;

    return UBASE_ERR_NONE;
}

/** @internal @This gets global unscaled metrics for the fonts.
 *
 * @param upipe description structure of the pipe
 * @param metrics filled with the string metrics
 * @return an error code
 */
static int _upipe_freetype_get_metrics(struct upipe *upipe,
                                       struct upipe_freetype_metrics *metrics)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    if (!upipe_freetype->face ||
        !FT_IS_SCALABLE(upipe_freetype->face))
        return UBASE_ERR_INVALID;

    struct upipe_freetype_metrics m;
    m.x.min = upipe_freetype->face->bbox.xMin;
    m.x.max = upipe_freetype->face->bbox.xMax;
    m.y.min = upipe_freetype->face->bbox.yMin;
    m.y.max = upipe_freetype->face->bbox.yMax;
    m.units_per_EM = upipe_freetype->face->units_per_EM;

    if (metrics)
        *metrics = m;

    return UBASE_ERR_NONE;
}

/** @internal @This sets the baseline offsets in the picture buffer.
 *
 * @param upipe description structure of the pipe
 * @param xoff offset from the left of the buffer
 * @param yoff offset from the top of the buffer
 * @return an error code
 */
static int _upipe_freetype_set_baseline(struct upipe *upipe,
                                        int64_t xoff, int64_t yoff)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    upipe_freetype->xoff = xoff;
    upipe_freetype->yoff = yoff;
    upipe_freetype_flush_cache(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the current text.
 *
 * @param upipe description structure of the pipe
 * @param text_p filled with the current text
 * @return an error code
 */
static int _upipe_freetype_get_text(struct upipe *upipe, const char **text_p)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    if (text_p)
        *text_p = upipe_freetype->text;
    return UBASE_ERR_NONE;
}

/** @internal @This flushes the freetype pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_freetype_flush(struct upipe *upipe)
{
    bool flushed = upipe_freetype_flush_input(upipe);
    if (flushed)
        upipe_release(upipe);
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_freetype_control_real(struct upipe *upipe,
                                       int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_freetype_control_ubuf_mgr(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_freetype_control_output(upipe, command, args));

    switch (command) {
        case UPIPE_FLUSH:
            upipe_freetype_flush(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref *);
            return upipe_freetype_set_flow_def(upipe, uref);
        }

        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *value  = va_arg(args, const char *);
            return upipe_freetype_set_option(upipe, option, value);
        }
    }

    if (command >= UPIPE_CONTROL_LOCAL &&
        ubase_get_signature(args) == UPIPE_FREETYPE_SIGNATURE) {
        UBASE_SIGNATURE_CHECK(args, UPIPE_FREETYPE_SIGNATURE);

        switch (command) {
            case UPIPE_FREETYPE_GET_BBOX: {
                const char *str = va_arg(args, const char *);
                struct upipe_freetype_bbox *bbox_p =
                    va_arg(args, struct upipe_freetype_bbox *);
                return _upipe_freetype_get_bbox(upipe, str, bbox_p);
            }

            case UPIPE_FREETYPE_SET_PIXEL_SIZE: {
                unsigned pixel_size = va_arg(args, unsigned);
                return _upipe_freetype_set_pixel_size(upipe, pixel_size);
            }

            case UPIPE_FREETYPE_SET_BASELINE: {
                int64_t xoff = va_arg(args, int64_t);
                int64_t yoff = va_arg(args, int64_t);
                return _upipe_freetype_set_baseline(upipe, xoff, yoff);
            }

            case UPIPE_FREETYPE_GET_TEXT: {
                const char **text_p = va_arg(args, const char **);
                return _upipe_freetype_get_text(upipe, text_p);
            }

            case UPIPE_FREETYPE_GET_METRICS: {
                struct upipe_freetype_metrics *metrics =
                    va_arg(args, struct upipe_freetype_metrics *);
                return _upipe_freetype_get_metrics(upipe, metrics);
            }

            case UPIPE_FREETYPE_GET_ADVANCE: {
                const char *str = va_arg(args, const char *);
                uint64_t *advance_p = va_arg(args, uint64_t *);
                uint64_t *units_per_EM_p = va_arg(args, uint64_t *);
                return _upipe_freetype_get_advance(upipe, str, advance_p,
                                                   units_per_EM_p);
            }
        }
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This handles control commands and checks the pipe state.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_freetype_control(struct upipe *upipe,
                                  int command, va_list args)
{
    UBASE_RETURN(upipe_freetype_control_real(upipe, command, args));
    return upipe_freetype_check(upipe, NULL);
}

/** upipe_freetype */
static struct upipe_mgr upipe_freetype_mgr = {
    .refcount = NULL,
    .signature = UPIPE_FREETYPE_SIGNATURE,

    .upipe_alloc = upipe_freetype_alloc,
    .upipe_input = upipe_freetype_input,
    .upipe_control = upipe_freetype_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for freetype pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_freetype_mgr_alloc(void)
{
    return &upipe_freetype_mgr;
}
