/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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

/** upipe_sws_thumbs structure with swscale parameters */ 
struct upipe_sws_thumbs {
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow */
    struct uref *flow_def_input;
    /** attributes added by the pipe */
    struct uref *flow_def_attr;
    /** output flow */
    struct uref *output_flow;
    /** true if the flow definition has already been sent */
    bool output_flow_sent;
    /** output pipe */
    struct upipe *output;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

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
    /** flush before next uref */
    bool flush;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_sws_thumbs, upipe, UPIPE_SWS_THUMBS_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_sws_thumbs, urefcount, upipe_sws_thumbs_free)
UPIPE_HELPER_FLOW(upipe_sws_thumbs, "pic.");
UPIPE_HELPER_OUTPUT(upipe_sws_thumbs, output, output_flow, output_flow_sent)
UPIPE_HELPER_FLOW_DEF(upipe_sws_thumbs, flow_def_input, flow_def_attr)
UPIPE_HELPER_UBUF_MGR(upipe_sws_thumbs, ubuf_mgr);

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
 * @param upump pump that generated the buffer
 */
static inline void upipe_sws_thumbs_flush(struct upipe *upipe, struct upump *upump)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);
    struct uref *gallery = upipe_sws_thumbs->gallery;
    upipe_sws_thumbs->flush = false;
    if (likely(gallery)) {
        upipe_sws_thumbs->counter = 0;
        upipe_sws_thumbs->gallery = NULL;
        upipe_sws_thumbs_output(upipe, gallery, upump);
    }
}

/** @internal @This receives incoming pictures.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump pump that generated the buffer
 */
static void upipe_sws_thumbs_input_pic(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);
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
        return;
    }

    /* allocate dest ubuf */
    gallery = upipe_sws_thumbs->gallery;
    if (unlikely(!gallery)) {
        upipe_sws_thumbs->gallery = uref_dup(uref);
        gallery = upipe_sws_thumbs->gallery;
        if (unlikely(!gallery)) {
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        ubuf = ubuf_pic_alloc(upipe_sws_thumbs->ubuf_mgr,
                                           thumbsize->hsize * thumbnum->hsize,
                                           thumbsize->vsize * thumbnum->vsize);
        if (unlikely(!ubuf)) {
            uref_free(gallery);
            uref_free(uref);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }
        ubuf_pic_clear(ubuf, 0, 0, -1, -1);
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
        return;
    }

    /* output if gallery is complete */
    counter++;
    counter = counter % (thumbnum->hsize*thumbnum->vsize);
    upipe_sws_thumbs->counter = counter;
    if (unlikely(counter == 0)) {
        upipe_sws_thumbs_flush(upipe, upump);
    }
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump pump that generated the buffer
 */
static void upipe_sws_thumbs_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);

    /* check ubuf manager */
    if (unlikely(!upipe_sws_thumbs->ubuf_mgr)) {
        upipe_throw_need_ubuf_mgr(upipe, upipe_sws_thumbs->output_flow);
        if (unlikely(!upipe_sws_thumbs->ubuf_mgr)) {
            upipe_warn(upipe, "ubuf_mgr not set, dropping picture");
            uref_free(uref);
            return;
        }
    }

    if (unlikely(!upipe_sws_thumbs->flow_def_input)) {
        upipe_warn(upipe, "received picture before input flow, dropping");
        uref_free(uref);
        return;
    }

    /* check parameters */
    if (unlikely(!upipe_sws_thumbs->thumbsize ||
                 !upipe_sws_thumbs->thumbnum)) {
        upipe_warn(upipe, "thumbs size/num not set, dropping picture");
        uref_free(uref);
        return;
    }

    /* process flush_next order */
    if (unlikely(upipe_sws_thumbs->flush)) {
        upipe_sws_thumbs_flush(upipe, upump);
    }

    upipe_sws_thumbs_input_pic(upipe, uref, upump);
}

/** @internal @This sets output pictures size
 * @param upipe description structure of the pipe
 * @param hsize horizontal size
 * @param vsize vertical size
 * @return false in case of error
 */
static bool _upipe_sws_thumbs_set_size(struct upipe *upipe,
                                       int hsize, int vsize, int cols, int rows)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);
    if (unlikely(hsize <= 0 || vsize <= 0 || cols <= 0 || rows <= 0)) {
        upipe_warn_va(upipe, "invalid size %dx%d %dx%d", hsize, vsize, cols, rows);
        return false;
    }
    upipe_sws_thumbs->thumbsize = realloc(upipe_sws_thumbs->thumbsize,
                                 sizeof(struct picsize));
    upipe_sws_thumbs->thumbnum = realloc(upipe_sws_thumbs->thumbnum,
                                 sizeof(struct picsize));
    if (unlikely(!upipe_sws_thumbs->thumbsize || !upipe_sws_thumbs->thumbnum)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
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
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }
    uref_pic_flow_set_hsize(flow, hsize * cols);
    uref_pic_flow_set_hsize_visible(flow, hsize * cols);
    uref_pic_flow_set_vsize(flow, vsize * rows);
    uref_pic_flow_set_vsize_visible(flow, vsize * rows);

    flow = upipe_sws_thumbs_store_flow_def_attr(upipe, flow);
    if (flow != NULL) {
        upipe_sws_thumbs_store_flow_def(upipe, flow);
    }

    upipe_dbg_va(upipe, "new output size: %dx%d (%dx%d * %dx%d)",
                 hsize*cols, vsize*rows, hsize, vsize, cols, rows);
    return true;
}

/** @internal @This retrieves output pictures size
 * @param upipe description structure of the pipe
 * @param hsize_p horizontal size
 * @param vsize_p vertical size
 * @return false in case of error
 */
static bool _upipe_sws_thumbs_get_size(struct upipe *upipe,
                           int *hsize_p, int *vsize_p, int *cols_p, int *rows_p)
{
    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);
    if (unlikely(!upipe_sws_thumbs->thumbsize || !upipe_sws_thumbs->thumbnum)) {
        return false;
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

    return true;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static bool upipe_sws_thumbs_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    if (flow_def == NULL)
        return false;

    if (unlikely(!uref_flow_match_def(flow_def, "pic.")))
        return false;

    struct upipe_sws_thumbs *upipe_sws_thumbs = upipe_sws_thumbs_from_upipe(upipe);
    if ((upipe_sws_thumbs->input_pix_fmt =
                upipe_av_pixfmt_from_flow_def(flow_def, NULL,
                            upipe_sws_thumbs->input_chroma_map)) == AV_PIX_FMT_NONE ||
        !sws_isSupportedInput(upipe_sws_thumbs->input_pix_fmt)) {
        upipe_err(upipe, "incompatible flow def");
        uref_dump(flow_def, upipe->uprobe);
        return false;
    }

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }

    flow_def = upipe_sws_thumbs_store_flow_def_input(upipe, flow_def);
    if (flow_def != NULL)
        upipe_sws_thumbs_store_flow_def(upipe, flow_def);
    return true;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_sws_thumbs_control(struct upipe *upipe,
                                     enum upipe_command command, va_list args)
{
    switch (command) {
        /* generic commands */
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_sws_thumbs_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_sws_thumbs_set_ubuf_mgr(upipe, ubuf_mgr);
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
            int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_SWS_THUMBS_SIGNATURE);
            int *hsize_p = va_arg(args, int*);
            int *vsize_p = va_arg(args, int*);
            int *cols_p = va_arg(args, int*);
            int *rows_p = va_arg(args, int*);
            return _upipe_sws_thumbs_get_size(upipe,
                                             hsize_p, vsize_p, cols_p, rows_p);
        }
        case UPIPE_SWS_THUMBS_SET_SIZE: {
            int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_SWS_THUMBS_SIGNATURE);
            int hsize = va_arg(args, int);
            int vsize = va_arg(args, int);
            int cols = va_arg(args, int);
            int rows = va_arg(args, int);
            return _upipe_sws_thumbs_set_size(upipe, hsize, vsize, cols, rows);
        }
        case UPIPE_SWS_THUMBS_FLUSH_NEXT: {
            int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_SWS_THUMBS_SIGNATURE);
            upipe_sws_thumbs_from_upipe(upipe)->flush = true;
            return true;
        }
        default:
            return false;
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

    upipe_sws_thumbs->convert_ctx = NULL;

    upipe_sws_thumbs->thumbsize = NULL;
    upipe_sws_thumbs->thumbnum = NULL;

    upipe_sws_thumbs->gallery = NULL;
    upipe_sws_thumbs->counter = 0;
    upipe_sws_thumbs->flush = false;


    struct urational sar;
    sar.num = sar.den = 1;
    uref_pic_flow_set_sar(flow_def, sar);
    upipe_sws_thumbs_store_flow_def_attr(upipe, flow_def);

    upipe_throw_ready(upipe);
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
    .upipe_control = upipe_sws_thumbs_control
};

/** @This returns the management structure for swscale pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sws_thumbs_mgr_alloc(void)
{
    return &upipe_sws_thumbs_mgr;
}

