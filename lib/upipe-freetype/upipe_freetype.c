/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 * Copyright (C) 2018-2019 OpenHeadend S.A.R.L.
 *
 * Authors: Rafaël Carré
 *          Arnaud de Turckheim
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short freetype2 based text renderer
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_void.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/udict.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow_format.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-freetype/upipe_freetype.h>

#include <ft2build.h>

#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_CACHE_H

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

    /** full range */
    bool fullrange;

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
UPIPE_HELPER_OUTPUT(upipe_freetype, output, flow_def, output_state, request_list)
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

/** @internal @This checks the compatibility of the ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format ubuf manager flow format
 * @return an error code
 */
static int upipe_freetype_check_ubuf_mgr(struct upipe *upipe,
                                         struct uref *flow_format)
{
    if (flow_format) {
        int err = upipe_freetype_check_flow_def(upipe, flow_format);
        if (unlikely(!ubase_check(err))) {
            uref_free(flow_format);
            return err;
        }

        upipe_freetype_flush_cache(upipe);
        upipe_freetype_store_flow_def(upipe, flow_format);
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
    free(upipe_freetype->font);
    FTC_Manager_Done(upipe_freetype->cache_manager);
    FT_Done_FreeType(upipe_freetype->library);

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
    struct upipe *upipe = data;
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    return FT_New_Face(upipe_freetype->library, upipe_freetype->font, 0, face);
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
    int ret;

    if (!upipe_freetype->ubuf_mgr)
        return false;

    struct uref *flow_format = upipe_freetype->flow_format;
    uint64_t hsize, vsize;
    if (!ubase_check(uref_pic_flow_get_hsize(flow_format, &hsize)) ||
        !ubase_check(uref_pic_flow_get_vsize(flow_format, &vsize))) {
        upipe_err_va(upipe, "Could not read output dimensions");
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

    struct ubuf *ubuf = ubuf_pic_alloc(upipe_freetype->ubuf_mgr, hsize, vsize);
    if (!ubuf) {
        upipe_err(upipe, "Could not allocate pic");
        uref_free(uref);
        return true;
    }

    size_t width, height;
    uint8_t macropixel;
    ret = ubuf_pic_size(ubuf, &width, &height, &macropixel);
    if (unlikely(!ubase_check(ret))) {
        upipe_err(upipe, "fail to get pic buffer size");
        ubuf_free(ubuf);
        uref_free(uref);
        return true;
    }

    struct plane {
        size_t stride;
        uint8_t hsub;
        uint8_t vsub;
        uint8_t macropixel_size;
        size_t memset_width;
        uint8_t *p;
    };
    struct plane y;
    struct plane u;
    struct plane v;
    struct plane a;

    memset(&y, 0, sizeof (y));
    memset(&u, 0, sizeof (u));
    memset(&v, 0, sizeof (v));
    memset(&a, 0, sizeof (a));

    const char *chroma;
    ubuf_pic_foreach_plane(ubuf, chroma) {
        struct plane *plane = NULL;

        if (!strcmp(chroma, "y8")) {
            plane = &y;
        } else if (!strcmp(chroma, "u8")) {
            plane = &u;
        } else if (!strcmp(chroma, "v8")) {
            plane = &v;
        } else if (!strcmp(chroma, "a8")) {
            plane = &a;
        } else {
            upipe_warn_va(upipe, "unsupported plane %s", chroma);
            continue;
        }

        ret = ubuf_pic_plane_size(ubuf, chroma, &plane->stride,
                                  &plane->hsub, &plane->vsub,
                                  &plane->macropixel_size);
        if (unlikely(!ubase_check(ret))) {
            upipe_warn_va(upipe, "fail to get plane %s size", chroma);
            continue;
        }
        plane->memset_width = width * plane->macropixel_size /
            plane->hsub / macropixel;

        ret = ubuf_pic_plane_write(ubuf, chroma, 0, 0, -1, -1, &plane->p);
        if (unlikely(!ubase_check(ret))) {
            upipe_warn_va(upipe, "fail to map %s plane", chroma);
            plane->p = NULL;
            continue;
        }
    }

    if (y.p) {
        uint8_t *buf = y.p;
        for (size_t i = 0; i < height / y.vsub; i++) {
            memset(buf, upipe_freetype->background[0], y.memset_width);
            buf += y.stride;
        }
    }

    if (u.p) {
        uint8_t *buf = u.p;
        for (size_t i = 0; i < height / u.vsub; i++) {
            memset(buf, upipe_freetype->background[1], u.memset_width);
            buf += u.stride;
        }
    }

    if (v.p) {
        uint8_t *buf = v.p;
        for (size_t i = 0; i < height / v.vsub; i++) {
            memset(buf, upipe_freetype->background[2], v.memset_width);
            buf += v.stride;
        }
    }

    if (a.p) {
        uint8_t *buf = a.p;
        for (size_t i = 0; i < height / a.vsub; i++) {
            memset(buf, upipe_freetype->background[3], a.memset_width);
            buf += a.stride;
        }
    }

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
        FT_Glyph glyph = NULL;
        FTC_ImageTypeRec type;
        type.face_id = upipe_freetype->font;
        type.width = upipe_freetype->pixel_size;
        type.height = upipe_freetype->pixel_size;
        type.flags = FT_LOAD_DEFAULT;
        FTC_SBit sbit;
        if (FTC_SBitCache_Lookup(upipe_freetype->sbit_cache,
                                 &type, index, &sbit, NULL))
            continue;

        int left, top, width, height, xadvance, yadvance;
        unsigned char *buffer;
        if (!sbit->buffer) {
            if (FTC_ImageCache_Lookup(upipe_freetype->img_cache,
                                      &type, index, &glyph, NULL))
                continue;

            if (FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, 0, 0))
                continue;

            FT_BitmapGlyph slot = (FT_BitmapGlyph)glyph;
            left = slot->left;
            top = slot->top;
            width = slot->bitmap.width;
            height = slot->bitmap.rows;
            xadvance = glyph->advance.x;
            yadvance = glyph->advance.y;
            buffer = slot->bitmap.buffer;
        }
        else {
            left = sbit->left;
            top = sbit->top;
            width = sbit->width;
            height = sbit->height;
            /* scale to 16.16 */
            xadvance = sbit->xadvance << 16;
            yadvance = sbit->yadvance << 16;
            buffer = sbit->buffer;
        }
        FT_Int xpos = (xoff >> 16) + left;
        FT_Int ypos = (yoff >> 16) - top;

        for (FT_Int i = 0; i < width; i++) {
            if (xpos + i < 0 || xpos + i >= hsize)
                continue;

            for (FT_Int j = 0; j < height; j++) {
                if (ypos + j < 0 || ypos + j >= vsize)
                    continue;

                uint8_t px = buffer[j * width + i] * upipe_freetype->foreground[3] / 0xff;

#define DO_PLANE(Plane, Val)                                                \
                if (Plane.p) {                                              \
                    FT_Int p_y = (ypos + j) / Plane.vsub * Plane.stride;    \
                    FT_Int p_x = (xpos + i) / Plane.hsub;                   \
                    FT_Int p = p_y + p_x;                                   \
                    Plane.p[p] = (Plane.p[p] * (0xff - px) + Val * px) / 0xff; \
                }

                DO_PLANE(y, upipe_freetype->foreground[0]);
                DO_PLANE(u, upipe_freetype->foreground[1]);
                DO_PLANE(v, upipe_freetype->foreground[2]);
                DO_PLANE(a, 0xff);
            }
        }

        /* increment pen position */
        xoff += xadvance;
        yoff += yadvance;

        previous = index;

        FT_Done_Glyph(glyph);
    }

    if (y.p)
        ubuf_pic_plane_unmap(ubuf, "y8", 0, 0, -1, -1);
    if (u.p)
        ubuf_pic_plane_unmap(ubuf, "u8", 0, 0, -1, -1);
    if (v.p)
        ubuf_pic_plane_unmap(ubuf, "v8", 0, 0, -1, -1);
    if (a.p)
        ubuf_pic_plane_unmap(ubuf, "a8", 0, 0, -1, -1);

    ubuf_free(upipe_freetype->ubuf);
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
 * @param a alpha componenet
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
 * @param a alpha componenet
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

        free(upipe_freetype->font);
        upipe_freetype->font = value ? strdup(value) : NULL;

        if (FTC_Manager_LookupFace(upipe_freetype->cache_manager,
                                   upipe_freetype->font,
                                   &upipe_freetype->face)) {
            upipe_err_va(upipe, "Couldn't open font %s", value);
            upipe_freetype->face = NULL;
            return UBASE_ERR_EXTERNAL;
        }
        upipe_freetype_flush_cache(upipe);
        return UBASE_ERR_NONE;
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
    if (!flow_def)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "void.text."));

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

    FT_Size size;
    FTC_ScalerRec scaler;
    scaler.face_id = upipe_freetype->face;
    scaler.width = upipe_freetype->pixel_size;
    scaler.height = upipe_freetype->pixel_size;
    scaler.pixel = 1;
    if (FTC_Manager_LookupSize(upipe_freetype->cache_manager,
                               &scaler, &size)) {
        upipe_err(upipe, "fail to get size");
        return UBASE_ERR_EXTERNAL;
    }
    upipe_freetype_flush_cache(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the x and y minimum and maximum values of the rendered
 * characters.
 *
 * @param upipe description structure of the pipe
 * @param str a string with the rendered characters
 * @param bbox_p filled with x and y mininum and maximum value
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

        if (!i)
            bbox.x = ft_bbox.xMin;
        if (ft_bbox.yMin < bbox.y)
            bbox.y = ft_bbox.yMin;
        if (ft_bbox.yMax > yMax)
            yMax = ft_bbox.yMax;
        width += glyph->advance.x;

        previous = index;
    }

    if (yMax > bbox.y)
        bbox.height = yMax - bbox.y;
    bbox.height = yMax - bbox.y;

    if (width > 0)
        /* get width ceil and downscale 16.16 integer */
        bbox.width = (width + 0xffff) >> 16;

    if (bbox_p)
        *bbox_p = bbox;

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
