/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe swscale thumbnail gallery module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-swscale/upipe_sws_thumbs.h>
#include <upipe-av/upipe_av_pixfmt.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <libswscale/swscale.h>

/** Picture size*/
struct picsize {
    size_t hsize;
    size_t vsize;
};

/** @hidden */
static bool upipe_sws_thumbs_handle(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p);
/** @hidden */
static int upipe_sws_thumbs_check(struct upipe *upipe, struct uref *flow_format);

/** upipe_sws_thumbs structure with swscale parameters */ 
struct upipe_sws_thumbs {
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow */
    struct uref *flow_def_input;
    /** attributes added by the pipe */
    struct uref *flow_def_attr;
    /** output pipe */
    struct upipe *output;
    /** output flow */
    struct uref *flow_def;
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

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** swscale image conversion context */
    struct SwsContext *convert_ctx;

    /** output thumb size */
    struct picsize *thumbsize;
    /** thumb image ratio */
    struct urational thumbratio;
    /** thumbs per row/col */
    struct picsize *thumbnum;

    /** input pixel format */
    enum PixelFormat input_pix_fmt;
    /** requested output pixel format */
    enum PixelFormat output_pix_fmt;
    /** input chroma map */
    const char *input_chroma_map[UPIPE_AV_MAX_PLANES];
    /** output chroma map */
    const char *output_chroma_map[UPIPE_AV_MAX_PLANES];

    /** current thumb gallery */
    struct uref *gallery;
    /** thumb counter */
    int counter;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_sws_thumbs, upipe, UPIPE_SWS_THUMBS_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_sws_thumbs, urefcount, upipe_sws_thumbs_free)
UPIPE_HELPER_FLOW(upipe_sws_thumbs, "pic.");
UPIPE_HELPER_OUTPUT(upipe_sws_thumbs, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_sws_thumbs, flow_def_input, flow_def_attr)
UPIPE_HELPER_UBUF_MGR(upipe_sws_thumbs, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_sws_thumbs_check,
                      upipe_sws_thumbs_register_output_request,
                      upipe_sws_thumbs_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_sws_thumbs, urefs, nb_urefs, max_urefs, blockers, upipe_sws_thumbs_handle)

/** @internal @This configures swscale context
 *
 * @param upipe_sws_thumbs upipe_sws_thumbs structure
 */
static inline bool upipe_sws_thumbs_set_context(struct upipe *upipe, 
                                         struct picsize *srcsize,
                                         struct picsize *dstsize) {
    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);

    upipe_verbose_va(upipe, "%zux%zu => %zux%zu",
          srcsize->hsize, srcsize->vsize, dstsize->hsize, dstsize->vsize);

    upipe_sws_thumbs->convert_ctx = sws_getCachedContext(upipe_sws_thumbs->convert_ctx,
                srcsize->hsize, srcsize->vsize, upipe_sws_thumbs->input_pix_fmt,
                dstsize->hsize, dstsize->vsize, upipe_sws_thumbs->output_pix_fmt,
                SWS_GAUSS, NULL, NULL, NULL);

    if (unlikely(!upipe_sws_thumbs->convert_ctx)) {
        upipe_err(upipe, "could not get swscale context");
        return false;
    }

    return true;
}

/** @internal @This flushes current thumbs gallery.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static inline void upipe_sws_thumbs_flush(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);
    struct uref *gallery = upipe_sws_thumbs->gallery;
    if (likely(gallery)) {
        upipe_sws_thumbs->counter = 0;
        upipe_sws_thumbs->gallery = NULL;
        upipe_sws_thumbs_output(upipe, gallery, upump_p);
    }
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_sws_thumbs_handle(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs =
        upipe_sws_thumbs_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_sws_thumbs_store_flow_def(upipe, NULL);
        uref = upipe_sws_thumbs_store_flow_def_input(upipe, uref);
        upipe_sws_thumbs_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_sws_thumbs->flow_def == NULL)
        return false;

    /* check parameters */
    if (unlikely(!upipe_sws_thumbs->thumbsize ||
                 !upipe_sws_thumbs->thumbnum)) {
        upipe_warn(upipe, "thumbs size/num not set, dropping picture");
        uref_free(uref);
        return true;
    }

    const char **planes;
    struct uref *gallery;
    struct ubuf *ubuf;
    struct picsize inputsize, pos, surface, margins, *thumbsize, *thumbnum;
    size_t stride;
    int strides[4], dstrides[4];
    const uint8_t *slices[4];
    uint8_t *dslices[4];
    int counter, ret, i;
    struct urational ratio, sar;

    /* set picture sizes */
    memset(&inputsize, 0, sizeof(struct picsize));
    uref_pic_size(uref, &inputsize.hsize, &inputsize.vsize, NULL);
    thumbsize = upipe_sws_thumbs->thumbsize;
    thumbnum = upipe_sws_thumbs->thumbnum;

    /* input picture ratio */
    sar.num = sar.den = 1;
    uref_pic_flow_get_sar(upipe_sws_thumbs->flow_def_input, &sar);
    if (unlikely(!sar.num || !sar.den)) {
        sar.num = sar.den = 1;
    }

    ratio.num = inputsize.hsize * sar.num;
    ratio.den = inputsize.vsize * sar.den;
    urational_simplify(&ratio);

    /* scaled size */
    counter = upipe_sws_thumbs->counter;
    surface.hsize = thumbsize->hsize;
    surface.vsize = thumbsize->vsize;

    if (ratio.num * upipe_sws_thumbs->thumbratio.den > 
        ratio.den * upipe_sws_thumbs->thumbratio.num ) {
        surface.vsize = (surface.hsize * ratio.den) / ratio.num;
        if (surface.vsize & 1) surface.vsize--;
    } else if (ratio.num * upipe_sws_thumbs->thumbratio.den < 
               ratio.den * upipe_sws_thumbs->thumbratio.num ){
        surface.hsize = (surface.vsize * ratio.num) / ratio.den;
        if (surface.hsize & 1) surface.hsize--;
    }
    
    /* margins */
    margins.hsize = (thumbsize->hsize - surface.hsize)/2;
    margins.vsize = (thumbsize->vsize - surface.vsize)/2;
    if (margins.hsize & 1) margins.hsize--;
    if (margins.vsize & 1) margins.vsize--;

    /* position in gallery */
    pos.hsize = thumbsize->hsize*(counter % thumbnum->hsize);
    pos.vsize = thumbsize->vsize*(counter / thumbnum->hsize);

    /* get sws context */
    if (unlikely(!upipe_sws_thumbs_set_context(upipe, &inputsize, &surface))) {
        uref_free(uref);
        return true;
    }

    /* allocate dest ubuf */
    gallery = upipe_sws_thumbs->gallery;
    if (unlikely(!gallery)) {
        upipe_sws_thumbs->gallery = uref_dup(uref);
        gallery = upipe_sws_thumbs->gallery;
        if (unlikely(!gallery)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return true;
        }
        ubuf = ubuf_pic_alloc(upipe_sws_thumbs->ubuf_mgr,
                                           thumbsize->hsize * thumbnum->hsize,
                                           thumbsize->vsize * thumbnum->vsize);
        if (unlikely(!ubuf)) {
            uref_free(gallery);
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return true;
        }
        ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0);
        uref_attach_ubuf(gallery, ubuf);
    }

    /* map input */
    memset(slices, 0, sizeof(slices));
    memset(strides, 0, sizeof(strides));
    planes = upipe_sws_thumbs->input_chroma_map;
    for (i=0; i < UPIPE_AV_MAX_PLANES && planes[i]; i++) {
        uref_pic_plane_read(uref, planes[i], 0, 0, -1, -1, &slices[i]);
        uref_pic_plane_size(uref, planes[i], &stride, NULL, NULL, NULL);
        strides[i] = stride;
    }
    /* map output */
    memset(dslices, 0, sizeof(dslices));
    memset(dstrides, 0, sizeof(dstrides));
    planes = upipe_sws_thumbs->output_chroma_map;
    for (i=0; i < UPIPE_AV_MAX_PLANES && planes[i]; i++) {
        uref_pic_plane_write(gallery, planes[i],
                         pos.hsize + margins.hsize, pos.vsize + margins.vsize,
                         surface.hsize, surface.vsize, &dslices[i]);
        uref_pic_plane_size(gallery, planes[i], &stride, NULL, NULL, NULL);
        dstrides[i] = stride;
    }

    /* fire ! */
    ret = sws_scale(upipe_sws_thumbs->convert_ctx,
                    (const uint8_t *const*) slices, strides, 0, inputsize.vsize,
                    dslices, dstrides);

    /* unmap pictures */
    planes = upipe_sws_thumbs->input_chroma_map;
    for (i=0; i < 4 && planes[i]; i++) {
        uref_pic_plane_unmap(uref, planes[i], 0, 0, -1, -1);
    }
    planes = upipe_sws_thumbs->output_chroma_map;
    for (i=0; i < 4 && planes[i]; i++) {
        uref_pic_plane_unmap(gallery, planes[i], 0, 0, -1, -1);
    }

    /* clean */
    uref_free(uref);
    if (unlikely(ret <= 0)) {
        upipe_warn(upipe, "error during sws conversion");
        return true;
    }

    /* output if gallery is complete */
    counter++;
    counter = counter % (thumbnum->hsize*thumbnum->vsize);
    upipe_sws_thumbs->counter = counter;
    if (unlikely(counter == 0)) {
        upipe_sws_thumbs_flush(upipe, upump_p);
    }
    return true;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_sws_thumbs_input(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p)
{
    if (!upipe_sws_thumbs_check_input(upipe)) {
        upipe_sws_thumbs_hold_input(upipe, uref);
        upipe_sws_thumbs_block_input(upipe, upump_p);
    } else if (!upipe_sws_thumbs_handle(upipe, uref, upump_p)) {
        upipe_sws_thumbs_hold_input(upipe, uref);
        upipe_sws_thumbs_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This sets output pictures size
 * @param upipe description structure of the pipe
 * @param hsize horizontal size
 * @param vsize vertical size
 * @return an error code
 */
static bool _upipe_sws_thumbs_set_size(struct upipe *upipe,
                                       int hsize, int vsize, int cols, int rows)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);
    if (unlikely(hsize <= 0 || vsize <= 0 || cols <= 0 || rows <= 0)) {
        upipe_warn_va(upipe, "invalid size %dx%d %dx%d", hsize, vsize, cols, rows);
        return UBASE_ERR_INVALID;
    }
    upipe_sws_thumbs->thumbsize = realloc(upipe_sws_thumbs->thumbsize,
                                 sizeof(struct picsize));
    upipe_sws_thumbs->thumbnum = realloc(upipe_sws_thumbs->thumbnum,
                                 sizeof(struct picsize));
    if (unlikely(!upipe_sws_thumbs->thumbsize || !upipe_sws_thumbs->thumbnum)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_sws_thumbs->thumbsize->hsize = hsize;
    upipe_sws_thumbs->thumbsize->vsize = vsize;
    upipe_sws_thumbs->thumbnum->hsize = cols;
    upipe_sws_thumbs->thumbnum->vsize = rows;

    /* compute new thumb ratio */
    upipe_sws_thumbs->thumbratio.num = hsize;
    upipe_sws_thumbs->thumbratio.den = vsize;
    urational_simplify(&upipe_sws_thumbs->thumbratio);

    struct uref *flow = uref_dup(upipe_sws_thumbs->flow_def_attr);
    if (unlikely(!flow)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    uref_pic_flow_set_hsize(flow, hsize * cols);
    uref_pic_flow_set_hsize_visible(flow, (uint64_t)hsize * cols);
    uref_pic_flow_set_vsize(flow, (uint64_t)vsize * rows);
    uref_pic_flow_set_vsize_visible(flow, (uint64_t)vsize * rows);
    UBASE_FATAL(upipe, uref_pic_flow_set_align(flow, 16))

    flow = upipe_sws_thumbs_store_flow_def_attr(upipe, flow);
    if (flow != NULL) {
        upipe_sws_thumbs_store_flow_def(upipe, flow);
    }

    upipe_dbg_va(upipe, "new output size: %dx%d (%dx%d * %dx%d)",
                 hsize*cols, vsize*rows, hsize, vsize, cols, rows);
    return UBASE_ERR_NONE;
}

/** @internal @This retrieves output pictures size.
 *
 * @param upipe description structure of the pipe
 * @param hsize_p horizontal size
 * @param vsize_p vertical size
 * @return an error code
 */
static int _upipe_sws_thumbs_get_size(struct upipe *upipe,
                           int *hsize_p, int *vsize_p, int *cols_p, int *rows_p)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);
    if (unlikely(!upipe_sws_thumbs->thumbsize || !upipe_sws_thumbs->thumbnum)) {
        return UBASE_ERR_INVALID;
    }
    if (hsize_p) {
        *hsize_p = upipe_sws_thumbs->thumbsize->hsize;
    }
    if (vsize_p) {
        *vsize_p = upipe_sws_thumbs->thumbsize->vsize;
    }
    if (cols_p) {
        *cols_p = upipe_sws_thumbs->thumbnum->hsize;
    }
    if (rows_p) {
        *rows_p = upipe_sws_thumbs->thumbnum->vsize;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_sws_thumbs_check(struct upipe *upipe,
                                  struct uref *flow_format)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs =
        upipe_sws_thumbs_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_sws_thumbs_store_flow_def(upipe, flow_format);

    if (upipe_sws_thumbs->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_sws_thumbs_check_input(upipe);
    upipe_sws_thumbs_output_input(upipe);
    upipe_sws_thumbs_unblock_input(upipe);
    if (was_buffered && upipe_sws_thumbs_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_sws_thumbs_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This requires a ubuf manager by proxy, and amends the flow
 * format.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_sws_thumbs_amend_ubuf_mgr(struct upipe *upipe,
                                    struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    uint64_t align;
    if (!ubase_check(uref_pic_flow_get_align(flow_format, &align)) || !align)
        uref_pic_flow_set_align(flow_format, 16);

    if (align % 16) {
        align = align * 16 / ubase_gcd(align, 16);
        uref_pic_flow_set_align(flow_format, align);
    }

    struct urequest ubuf_mgr_request;
    urequest_set_opaque(&ubuf_mgr_request, request);
    urequest_init_ubuf_mgr(&ubuf_mgr_request, flow_format,
                           upipe_sws_thumbs_provide_output_proxy, NULL);
    upipe_throw_provide_request(upipe, &ubuf_mgr_request);
    urequest_clean(&ubuf_mgr_request);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_sws_thumbs_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))

    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);
    if ((upipe_sws_thumbs->input_pix_fmt =
                upipe_av_pixfmt_from_flow_def(flow_def, NULL,
                            upipe_sws_thumbs->input_chroma_map)) == AV_PIX_FMT_NONE ||
        !sws_isSupportedInput(upipe_sws_thumbs->input_pix_fmt)) {
        upipe_err(upipe, "incompatible flow def");
        uref_dump(flow_def, upipe->uprobe);
        return UBASE_ERR_EXTERNAL;
    }

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_input(upipe, flow_def, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_sws_thumbs_control(struct upipe *upipe,
                                    int command, va_list args)
{
    switch (command) {
        /* generic commands */
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_sws_thumbs_amend_ubuf_mgr(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_sws_thumbs_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_sws_thumbs_free_output_proxy(upipe, request);
        }

        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sws_thumbs_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_sws_thumbs_set_output(upipe, output);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_sws_thumbs_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_sws_thumbs_set_flow_def(upipe, flow);
        }

        /* specific commands */
        case UPIPE_SWS_THUMBS_GET_SIZE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SWS_THUMBS_SIGNATURE)
            int *hsize_p = va_arg(args, int*);
            int *vsize_p = va_arg(args, int*);
            int *cols_p = va_arg(args, int*);
            int *rows_p = va_arg(args, int*);
            return _upipe_sws_thumbs_get_size(upipe,
                                             hsize_p, vsize_p, cols_p, rows_p);
        }
        case UPIPE_SWS_THUMBS_SET_SIZE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SWS_THUMBS_SIGNATURE)
            int hsize = va_arg(args, int);
            int vsize = va_arg(args, int);
            int cols = va_arg(args, int);
            int rows = va_arg(args, int);
            return _upipe_sws_thumbs_set_size(upipe, hsize, vsize, cols, rows);
        }
        case UPIPE_SWS_THUMBS_FLUSH_NEXT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_SWS_THUMBS_SIGNATURE)
            upipe_sws_thumbs_flush(upipe, NULL);
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a swscale pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_sws_thumbs_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_sws_thumbs_alloc_flow(mgr, uprobe, signature,
                                                      args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_sws_thumbs *upipe_sws_thumbs =
            upipe_sws_thumbs_from_upipe(upipe);

    /* guess output format from output flow def */
    upipe_sws_thumbs->output_pix_fmt = upipe_av_pixfmt_from_flow_def(flow_def,
        NULL, upipe_sws_thumbs->output_chroma_map);
    if (upipe_sws_thumbs->output_pix_fmt == AV_PIX_FMT_NONE
            || !sws_isSupportedOutput(upipe_sws_thumbs->output_pix_fmt)) {
        uref_free(flow_def);
        upipe_sws_thumbs_free_flow(upipe);
        return NULL;
    }

    upipe_sws_thumbs_init_urefcount(upipe);
    upipe_sws_thumbs_init_ubuf_mgr(upipe);
    upipe_sws_thumbs_init_output(upipe);
    upipe_sws_thumbs_init_flow_def(upipe);
    upipe_sws_thumbs_init_input(upipe);

    upipe_sws_thumbs->convert_ctx = NULL;

    upipe_sws_thumbs->thumbsize = NULL;
    upipe_sws_thumbs->thumbnum = NULL;

    upipe_sws_thumbs->gallery = NULL;
    upipe_sws_thumbs->counter = 0;

    upipe_throw_ready(upipe);

    struct urational sar;
    sar.num = sar.den = 1;
    UBASE_FATAL(upipe, uref_pic_flow_set_sar(flow_def, sar))
    UBASE_FATAL(upipe, uref_pic_flow_set_align(flow_def, 16))
    upipe_sws_thumbs_store_flow_def_attr(upipe, flow_def);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sws_thumbs_free(struct upipe *upipe)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs =
        upipe_sws_thumbs_from_upipe(upipe);
    if (likely(upipe_sws_thumbs->convert_ctx)) {
        sws_freeContext(upipe_sws_thumbs->convert_ctx);
    }
    free(upipe_sws_thumbs->thumbsize);
    free(upipe_sws_thumbs->thumbnum);
    if (upipe_sws_thumbs->gallery) {
        upipe_sws_thumbs_flush(upipe, NULL);
    }

    upipe_throw_dead(upipe);
    upipe_sws_thumbs_clean_input(upipe);
    upipe_sws_thumbs_clean_output(upipe);
    upipe_sws_thumbs_clean_flow_def(upipe);
    upipe_sws_thumbs_clean_ubuf_mgr(upipe);
    upipe_sws_thumbs_clean_urefcount(upipe);
    upipe_sws_thumbs_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_sws_thumbs_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SWS_THUMBS_SIGNATURE,

    .upipe_alloc = upipe_sws_thumbs_alloc,
    .upipe_input = upipe_sws_thumbs_input,
    .upipe_control = upipe_sws_thumbs_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for swscale pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sws_thumbs_mgr_alloc(void)
{
    return &upipe_sws_thumbs_mgr;
}

