/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/udict.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe-freetype/upipe_freetype.h>

#include <ft2build.h>
#include FT_FREETYPE_H

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

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** request output */
    struct uref *flow_output;

    /** freetype handle */
    FT_Library library;

    /** font handle */
    FT_Face face;

    /** public upipe structure */
    struct upipe upipe;
};

static int upipe_freetype_check(struct upipe *upipe, struct uref *uref);

UPIPE_HELPER_UPIPE(upipe_freetype, upipe, UPIPE_FREETYPE_SIGNATURE);
UPIPE_HELPER_OUTPUT(upipe_freetype, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREFCOUNT(upipe_freetype, urefcount, upipe_freetype_free)
UPIPE_HELPER_UBUF_MGR(upipe_freetype, ubuf_mgr, flow_format, ubuf_mgr_request,
        upipe_freetype_check, upipe_freetype_register_output_request,
                      upipe_freetype_unregister_output_request)

UPIPE_HELPER_FLOW(upipe_freetype, UREF_PIC_FLOW_DEF);

static int upipe_freetype_check(struct upipe *upipe, struct uref *uref)
{
    upipe_freetype_store_flow_def(upipe, uref);
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

    if (upipe_freetype->face)
        FT_Done_Face(upipe_freetype->face);

    FT_Done_FreeType(upipe_freetype->library);

    uref_free(upipe_freetype->flow_output);
    upipe_freetype_clean_urefcount(upipe);
    upipe_freetype_clean_output(upipe);
    upipe_freetype_clean_ubuf_mgr(upipe);
    upipe_freetype_free_flow(upipe);
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

    struct upipe *upipe = upipe_freetype_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    upipe_freetype->flow_output = uref_dup(flow_def);

    if (FT_Init_FreeType(&upipe_freetype->library)) {
        uref_free(upipe_freetype->flow_output);
        upipe_freetype_free_flow(upipe);
        return NULL;
    }

    upipe_freetype->face = NULL;

    upipe_freetype_init_urefcount(upipe);
    upipe_freetype_init_output(upipe);
    upipe_freetype_init_ubuf_mgr(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_freetype_input(struct upipe *upipe, struct uref *uref, struct upump **upump_p)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);

    uint64_t h, v;
    if (!ubase_check(uref_pic_flow_get_hsize(upipe_freetype->flow_output, &h)) ||
            !ubase_check(uref_pic_flow_get_vsize(upipe_freetype->flow_output, &v))) {
        upipe_err_va(upipe, "Could not read output dimensions");
        uref_free(uref);
        return;
    }

    if (!upipe_freetype->ubuf_mgr) {
        struct uref *flow_def = uref_sibling_alloc(uref);
        uref_flow_set_def(flow_def, UREF_PIC_FLOW_DEF);
        uref_pic_flow_set_planes(flow_def, 0);
        uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8");
        uref_pic_flow_add_plane(flow_def, 2, 1, 1, "u8");
        uref_pic_flow_add_plane(flow_def, 2, 1, 1, "v8");
        uref_pic_flow_add_plane(flow_def, 1, 1, 1, "a8");
        uref_pic_flow_set_hsize(flow_def, h);
        uref_pic_flow_set_hsize_visible(flow_def, h);
        uref_pic_flow_set_vsize(flow_def, v);
        uref_pic_flow_set_vsize_visible(flow_def, v);
        uref_pic_flow_set_macropixel(flow_def, 1);
        uref_pic_flow_set_align(flow_def, 16);
        uref_pic_set_progressive(flow_def);
        upipe_freetype_demand_ubuf_mgr(upipe, uref_dup(flow_def));
        upipe_freetype_store_flow_def(upipe, flow_def);
    }

    struct ubuf *ubuf = ubuf_pic_alloc(upipe_freetype->ubuf_mgr, h, v);
    if (!ubuf) {
        upipe_err(upipe, "Could not allocate pic");
        uref_free(uref);
        return;
    }

    ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0);

    size_t stride_a, stride_y;
    if (!ubase_check(ubuf_pic_plane_size(ubuf, "y8", &stride_y, NULL, NULL, NULL)) ||
            !ubase_check(ubuf_pic_plane_size(ubuf, "a8", &stride_a, NULL, NULL, NULL))) {
        upipe_err(upipe, "Could not read ubuf plane sizes");
        ubuf_free(ubuf);
        uref_free(uref);
        return;
    }

    uint8_t *dst;
    uint8_t *dsta;
    if (!ubase_check(ubuf_pic_plane_write(ubuf, "y8", 0, 0, -1, -1, &dst))) {
        upipe_err(upipe, "Could not map luma plane");
        ubuf_free(ubuf);
        uref_free(uref);
        return;
    }
    if (!ubase_check(ubuf_pic_plane_write(ubuf, "a8", 0, 0, -1, -1, &dsta))) {
        upipe_err(upipe, "Could not map alpha plane");
        ubuf_pic_plane_unmap(ubuf, "y8", 0, 0, -1, -1);
        ubuf_free(ubuf);
        uref_free(uref);
        return;
    }

    /* the pen position in 26.6 cartesian space coordinates */
    FT_Vector pen; /* untransformed origin  */
    pen.x = 0;
    pen.y = v*8;

    FT_GlyphSlot slot = upipe_freetype->face->glyph;
    const char *text;
    int r = uref_attr_get_string(uref, &text, UDICT_TYPE_STRING, "text");
    if (!ubase_check(r)) {
        uref_dump(uref, upipe->uprobe);
        text = "fail";
    }
    for (int i = 0; i < strlen(text); i++) {
        FT_Set_Transform(upipe_freetype->face, NULL, &pen);

        /* load glyph image into the slot(erase previous one) */
        if (FT_Load_Char(upipe_freetype->face, text[i], FT_LOAD_RENDER))
            continue;                 /* ignore errors */

        /* now, draw to our target surface(convert position) */
        FT_Bitmap *bitmap = &slot->bitmap;
        FT_Int x = slot->bitmap_left;
        FT_Int y = v - slot->bitmap_top;
        FT_Int x_max = x + bitmap->width;
        if (x_max > h) {
            upipe_err_va(upipe, "clipping x, %"PRIu64" < %d", h, x_max);
            x_max = h;
        }
        FT_Int y_max = y + bitmap->rows;
        if (y_max > v) {
            upipe_err_va(upipe, "clipping y, %"PRIu64" < %d", v, y_max);
            y_max = v;
        }

        for (FT_Int i = (x < 0 ? 0 : x); i < x_max; i++)
            for (FT_Int j = (y < 0 ? 0 : y); j < y_max; j++) {
                dst[j*stride_y + i] |= bitmap->buffer[(j - y) * bitmap->width + (i - x)];
                dsta[j*stride_a + i] |= bitmap->buffer[(j - y) * bitmap->width + (i - x)];
            }

        /* increment pen position */
        pen.x += slot->advance.x;
        pen.y += slot->advance.y;
    }

    unsigned text_w = pen.x / 64;

    if (text_w < h)
        for (int i = 0; i < v; i++) {
            memmove(&dst[i*stride_y + (h - text_w) / 2], &dst[i*stride_y], text_w);
            memset(&dst[i*stride_y], 0, (h - text_w) / 2);
            memmove(&dsta[i*stride_a + (h - text_w) / 2], &dsta[i*stride_a], text_w);
            memset(&dsta[i*stride_a], 0, (h - text_w) / 2);
        }

    ubuf_pic_plane_unmap(ubuf, "y8", 0, 0, -1, -1);
    ubuf_pic_plane_unmap(ubuf, "a8", 0, 0, -1, -1);

    uref_attach_ubuf(uref, ubuf);

    upipe_freetype_output(upipe, uref, upump_p);
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

    if (strcmp(option, "font"))
        return UBASE_ERR_INVALID;

    if (upipe_freetype->face)
        FT_Done_Face(upipe_freetype->face);

    upipe_freetype->face = NULL;

    uint64_t v;
    UBASE_RETURN(uref_pic_flow_get_vsize(upipe_freetype->flow_output, &v));

    if (FT_New_Face(upipe_freetype->library, value, 0, &upipe_freetype->face)) {
        upipe_err_va(upipe, "Couldn't open font %s", value);
        upipe_freetype->face = NULL;
        return UBASE_ERR_EXTERNAL;
    }

    if (FT_Set_Pixel_Sizes(upipe_freetype->face, v, v)) {
        upipe_err(upipe, "Couldn't set pixel size");
        return UBASE_ERR_EXTERNAL;
    }

    return UBASE_ERR_NONE;
}

static int upipe_freetype_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (!flow_def)
        return UBASE_ERR_INVALID;

    if (uref_flow_match_def(flow_def, "void.text."))
        return UBASE_ERR_INVALID;

    // TODO : x/y/offsets

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_freetype_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_freetype_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_freetype_set_output(upipe, output);
        }

        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
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

        default:
            return UBASE_ERR_UNHANDLED;
    }
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
