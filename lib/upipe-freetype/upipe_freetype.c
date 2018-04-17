/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
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
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/udict.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_urefcount.h>
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

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

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

    /** public upipe structure */
    struct upipe upipe;
};

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
UPIPE_HELPER_UBUF_MGR(upipe_freetype, ubuf_mgr, flow_format, ubuf_mgr_request,
        upipe_freetype_check, upipe_freetype_register_output_request,
                      upipe_freetype_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_freetype, urefs, nb_urefs, max_urefs, blockers,
                   upipe_freetype_handle);
UPIPE_HELPER_FLOW(upipe_freetype, UREF_PIC_FLOW_DEF);

/** @internal @This checks the compatibility of a flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_format flow format to test
 * @return an error code
 */
static int upipe_freetype_check_flow_format(struct upipe *upipe,
                                            struct uref *flow_format)
{
    UBASE_RETURN(uref_flow_match_def(flow_format, UREF_PIC_FLOW_DEF));
    UBASE_RETURN(uref_pic_flow_find_chroma(flow_format, "y8", NULL));
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_format, NULL));
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_format, NULL));
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

    if (flow_format) {
        ubase_assert(upipe_freetype_check_flow_format(upipe, flow_format));
        upipe_freetype_store_flow_def(upipe, flow_format);
    }

    if (!upipe_freetype->ubuf_mgr) {
        upipe_freetype_require_ubuf_mgr(upipe,
                                        uref_dup(upipe_freetype->flow_output));
        return UBASE_ERR_NONE;
    }

    if (upipe_freetype_check_input(upipe))
        return UBASE_ERR_NONE;

    upipe_freetype_output_input(upipe);
    upipe_freetype_unblock_input(upipe);
    if (upipe_freetype_check_input(upipe))
        upipe_release(upipe);
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

    free(upipe_freetype->font);
    FTC_Manager_Done(upipe_freetype->cache_manager);
    FT_Done_FreeType(upipe_freetype->library);

    uref_free(upipe_freetype->flow_output);
    upipe_freetype_clean_input(upipe);
    upipe_freetype_clean_urefcount(upipe);
    upipe_freetype_clean_output(upipe);
    upipe_freetype_clean_ubuf_mgr(upipe);
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
    upipe_freetype_init_ubuf_mgr(upipe);
    upipe_freetype_init_input(upipe);

    struct upipe_freetype *upipe_freetype = upipe_freetype_from_upipe(upipe);
    upipe_freetype->flow_output = flow_def;
    upipe_freetype->library = NULL;
    upipe_freetype->cache_manager = NULL;
    upipe_freetype->face = NULL;
    upipe_freetype->xoff = 0;
    upipe_freetype->yoff = 0;
    upipe_freetype->font = NULL;
    upipe_freetype->pixel_size = 0;

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
    if (unlikely(
            !ubase_check(upipe_freetype_check_flow_format(upipe, flow_def)) ||
            !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize)))) {
        upipe_err(upipe, "invalid flow format");
        upipe_release(upipe);
        return NULL;
    }
    upipe_freetype->pixel_size = vsize > UINT_MAX ? UINT_MAX : vsize;
    upipe_freetype->yoff = vsize - vsize / 8;

    return upipe;
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

    struct uref *flow_format = upipe_freetype->flow_format;
    uint64_t h, v;
    if (!ubase_check(uref_pic_flow_get_hsize(flow_format, &h)) ||
        !ubase_check(uref_pic_flow_get_vsize(flow_format, &v))) {
        upipe_err_va(upipe, "Could not read output dimensions");
        uref_free(uref);
        return true;
    }

    const char *text;
    int r = uref_attr_get_string(uref, &text, UDICT_TYPE_STRING, "text");
    if (!ubase_check(r) || !text) {
        uref_dump(uref, upipe->uprobe);
        text = "fail";
    }
    size_t length = strlen(text);

    struct ubuf *ubuf = ubuf_pic_alloc(upipe_freetype->ubuf_mgr, h, v);
    if (!ubuf) {
        upipe_err(upipe, "Could not allocate pic");
        uref_free(uref);
        return true;
    }

    ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0);

    bool has_alpha = false;
    if (ubase_check(uref_pic_flow_find_chroma(flow_format, "a8", NULL)))
        has_alpha = true;

    size_t stride_a, stride_y;
    if (!ubase_check(
            ubuf_pic_plane_size(ubuf, "y8", &stride_y, NULL, NULL, NULL))) {
        upipe_err(upipe, "Could not read ubuf luma plane sizes");
        ubuf_free(ubuf);
        uref_free(uref);
        return true;
    }

    if (has_alpha &&
        !ubase_check(
            ubuf_pic_plane_size(ubuf, "a8", &stride_a, NULL, NULL, NULL))) {
        upipe_err(upipe, "Could not read ubuf alpha plane sizes");
        ubuf_free(ubuf);
        uref_free(uref);
        return true;
    }

    uint8_t *dst;
    uint8_t *dsta;
    if (!ubase_check(ubuf_pic_plane_write(ubuf, "y8", 0, 0, -1, -1, &dst))) {
        upipe_err(upipe, "Could not map luma plane");
        ubuf_free(ubuf);
        uref_free(uref);
        return true;
    }
    if (has_alpha &&
        !ubase_check(ubuf_pic_plane_write(ubuf, "a8", 0, 0, -1, -1, &dsta))) {
        upipe_err(upipe, "Could not map alpha plane");
        ubuf_pic_plane_unmap(ubuf, "y8", 0, 0, -1, -1);
        ubuf_free(ubuf);
        uref_free(uref);
        return true;
    }

    FT_Bool use_kerning = FT_HAS_KERNING(upipe_freetype->face);
    FT_UInt previous = 0;
    /* scale offset to 16.16 */
    int64_t xoff = upipe_freetype->xoff << 16;
    int64_t yoff = upipe_freetype->yoff << 16;
    for (int i = 0; i < length; i++) {
        FT_UInt index = FTC_CMapCache_Lookup(upipe_freetype->cmap_cache,
                                             upipe_freetype->font, -1, text[i]);
        if (unlikely(!index))
            continue;

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
        FT_Int x = (xoff >> 16) + left;
        FT_Int y = (yoff >> 16) - top;

        for (FT_Int i = 0; i < width; i++) {
            if (x + i < 0 || x + i >= h)
                continue;

            for (FT_Int j = 0; j < height; j++) {
                if (y + j < 0 || y + j >= v)
                    continue;

                dst[(y + j) * stride_y + x + i] |= buffer[j * width + i];
                if (has_alpha)
                    dsta[(y + i) * stride_a + x + i] |= buffer[(j * width + i)];
            }
        }

        /* increment pen position */
        xoff += xadvance;
        yoff += yadvance;

        previous = index;

        FT_Done_Glyph(glyph);
    }

    ubuf_pic_plane_unmap(ubuf, "y8", 0, 0, -1, -1);
    if (has_alpha)
        ubuf_pic_plane_unmap(ubuf, "a8", 0, 0, -1, -1);

    uref_attach_ubuf(uref, ubuf);

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

        uint64_t v;
        UBASE_RETURN(uref_pic_flow_get_vsize(upipe_freetype->flow_output, &v));

        if (FTC_Manager_LookupFace(upipe_freetype->cache_manager,
                                   upipe_freetype->font,
                                   &upipe_freetype->face)) {
            upipe_err_va(upipe, "Couldn't open font %s", value);
            upipe_freetype->face = NULL;
            return UBASE_ERR_EXTERNAL;
        }
        return UBASE_ERR_NONE;
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
    size_t length = str ? strlen(str) : 0;

    struct upipe_freetype_bbox bbox;
    bbox.x = 0;
    bbox.y = 0;
    bbox.width = 0;
    bbox.height = 0;

    FT_Bool use_kerning = FT_HAS_KERNING(upipe_freetype->face);
    FT_UInt previous = 0;
    FT_Pos yMax = 0;
    int64_t width = 0;
    for (int i = 0; i < length; i++) {
        FT_UInt index = FTC_CMapCache_Lookup(upipe_freetype->cmap_cache,
                                             upipe_freetype->font, -1, str[i]);
        if (unlikely(!index))
            return UBASE_ERR_EXTERNAL;

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
            return UBASE_ERR_EXTERNAL;

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
