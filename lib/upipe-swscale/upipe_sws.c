/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short Upipe swscale (ffmpeg) module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/upipe.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-swscale/upipe_sws.h>
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

/** upipe_sws structure with swscale parameters */ 
struct upipe_sws {
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

    /** output image size */
    struct picsize *dstsize;

    /** input image swscale format */
    const struct upipe_av_pixfmt *srcfmt;
    /** output image swscale format */
    const struct upipe_av_pixfmt *dstfmt;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_sws, upipe);
UPIPE_HELPER_FLOW(upipe_sws, "pic.");
UPIPE_HELPER_OUTPUT(upipe_sws, output, output_flow, output_flow_sent)
UPIPE_HELPER_UBUF_MGR(upipe_sws, ubuf_mgr);

/** @internal @This configures swscale context
 *
 * @param upipe_sws upipe_sws structure
 */
static inline bool upipe_sws_set_context(struct upipe *upipe, 
                                         struct picsize *srcsize,
                                         struct picsize *dstsize) {
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);

    upipe_dbg_va(upipe, "%zux%zu => %zux%zu",
          srcsize->hsize, srcsize->vsize, dstsize->hsize, dstsize->vsize);

    upipe_sws->convert_ctx = sws_getCachedContext(upipe_sws->convert_ctx,
                srcsize->hsize, srcsize->vsize, *upipe_sws->srcfmt->pixfmt,
                dstsize->hsize, dstsize->vsize, *upipe_sws->dstfmt->pixfmt,
                SWS_BICUBIC, NULL, NULL, NULL);

    if (unlikely(!upipe_sws->convert_ctx)) {
        upipe_err(upipe, "could not get swscale context");
        return false;
    }

    return true;
}

/** @internal @This receives incoming pictures.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump pump that generated the buffer
 */
static void upipe_sws_input_pic(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    const struct upipe_av_plane *planes;
    struct ubuf *dstpic;
    struct picsize inputsize, *dstsize;
    size_t stride;
    int strides[4], dstrides[4];
    const uint8_t *slices[4];
    uint8_t *dslices[4];
    int ret, i;

    /* detect input format */
    if (unlikely(!upipe_sws->srcfmt)) {
        upipe_sws->srcfmt = upipe_av_pixfmt_from_ubuf(uref->ubuf);
        if (unlikely(!upipe_sws->srcfmt)) {
            upipe_warn(upipe, "unrecognized input format");
            uref_free(uref);
            return;
        }
    }

    /* set picture sizes */
    memset(&inputsize, 0, sizeof(struct picsize));
    uref_pic_size(uref, &inputsize.hsize, &inputsize.vsize, NULL);
    dstsize = upipe_sws->dstsize;
    if (!dstsize) {
        /* comes handy in case of format conversion with no rescaling */
        dstsize = &inputsize;
    }

    /* get sws context */
    if (unlikely(!upipe_sws_set_context(upipe, &inputsize, dstsize))) {
        uref_free(uref);
        return;
    }

    /* allocate dest ubuf */
    dstpic = ubuf_pic_alloc(upipe_sws->ubuf_mgr, dstsize->hsize, dstsize->vsize);
    if (unlikely(!dstpic)) {
        upipe_throw_aerror(upipe);
        uref_free(uref);
        return;
    }

    /* map input */
    memset(slices, 0, sizeof(slices));
    memset(strides, 0, sizeof(strides));
    planes = upipe_sws->srcfmt->planes;
    for (i=0; i < 4 && planes[i].chroma; i++) {
        uref_pic_plane_read(uref, planes[i].chroma, 0, 0, -1, -1, &slices[i]);
        uref_pic_plane_size(uref, planes[i].chroma, &stride, NULL, NULL, NULL);
        strides[i] = stride;
    }
    /* map output */
    memset(dslices, 0, sizeof(dslices));
    memset(dstrides, 0, sizeof(dstrides));
    planes = upipe_sws->dstfmt->planes;
    for (i=0; i < 4 && planes[i].chroma; i++) {
        ubuf_pic_plane_write(dstpic, planes[i].chroma, 0, 0, -1, -1, &dslices[i]);
        ubuf_pic_plane_size(dstpic, planes[i].chroma, &stride, NULL, NULL, NULL);
        dstrides[i] = stride;
    }

    /* fire ! */
    ret = sws_scale(upipe_sws->convert_ctx,
                    (const uint8_t *const*) slices, strides, 0, inputsize.vsize,
                    dslices, dstrides);

    /* unmap pictures */
    planes = upipe_sws->srcfmt->planes;
    for (i=0; i < 4 && planes[i].chroma; i++) {
        uref_pic_plane_unmap(uref, planes[i].chroma, 0, 0, -1, -1);
    }
    planes = upipe_sws->dstfmt->planes;
    for (i=0; i < 4 && planes[i].chroma; i++) {
        ubuf_pic_plane_unmap(dstpic, planes[i].chroma, 0, 0, -1, -1);
    }

    /* clean and output */
    ubuf_free(uref_detach_ubuf(uref));
    if (unlikely(ret <= 0)) {
        upipe_warn(upipe, "error during sws conversion");
        ubuf_free(dstpic);
        uref_free(uref);
        return;
    }
    uref_attach_ubuf(uref, dstpic);
    upipe_sws_output(upipe, uref, upump);
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump pump that generated the buffer
 */
static void upipe_sws_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);

    /* empty uref */
    if (unlikely(!uref->ubuf)) {
        upipe_sws_output(upipe, uref, upump);
        return;
    }

    /* check ubuf manager */
    if (unlikely(!upipe_sws->ubuf_mgr)) {
        upipe_throw_need_ubuf_mgr(upipe, upipe_sws->output_flow);
        if (unlikely(!upipe_sws->ubuf_mgr)) {
            upipe_warn(upipe, "ubuf_mgr not set !");
            uref_free(uref);
            return;
        }
    }

    /* check dst format */
    if (unlikely(!upipe_sws->dstfmt)) {
        upipe_warn(upipe, "unrecognized dst format");
        uref_free(uref);
        return;
    }

    upipe_sws_input_pic(upipe, uref, upump);
}

/** @internal @This sets output pictures size
 * @param upipe description structure of the pipe
 * @param hsize horizontal size
 * @param vsize vertical size
 * @return false in case of error
 */
static bool _upipe_sws_set_size(struct upipe *upipe, int hsize, int vsize)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    upipe_sws->dstsize = realloc(upipe_sws->dstsize,
                                 sizeof(struct picsize));
    if (unlikely(!upipe_sws->dstsize)) {
        upipe_throw_aerror(upipe);
        return false;
    }
    upipe_sws->dstsize->hsize = hsize;
    upipe_sws->dstsize->vsize = vsize;

    upipe_dbg_va(upipe, "new output size: %dx%d", hsize, vsize);
    return true;
}

/** @internal @This retrieves output pictures size
 * @param upipe description structure of the pipe
 * @param hsize_p horizontal size
 * @param vsize_p vertical size
 * @return false in case of error
 */
static bool _upipe_sws_get_size(struct upipe *upipe, int *hsize_p, int *vsize_p)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    if (unlikely(!upipe_sws->dstsize)) {
        return false;
    }
    if (hsize_p) {
        *hsize_p = upipe_sws->dstsize->hsize;
    }
    if (vsize_p) {
        *vsize_p = upipe_sws->dstsize->vsize;
    }

    return true;
}

/** @internal @This configures output flow.
 *
 * @param upipe description structure of the pipe
 * @param flow uref structure describing the flow
 * @return false in case of failure
 */
static bool upipe_sws_set_flow_def(struct upipe *upipe, struct uref *flow)
{
    uint64_t hsize = 0, vsize = 0;
    if (!flow) {
        return false;
    }

    uref_pic_get_hsize(flow, &hsize);
    uref_pic_get_vsize(flow, &vsize);
    if (unlikely(!_upipe_sws_set_size(upipe, hsize, vsize))) {
        uref_free(flow);
        return false;
    }

    upipe_sws_store_flow_def(upipe, flow);
    return true;
}

/** @internal @This sets ubuf_mgr and finds corresponding pixfmt
 * @param upipe description structure of the pipe
 * @param ubuf_mgr ubuf manager
 * @return false in case of error
 */
static bool _upipe_sws_set_ubuf_mgr(struct upipe *upipe, struct ubuf_mgr *mgr)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    upipe_sws->dstfmt = upipe_av_pixfmt_from_ubuf_mgr(mgr);
    return upipe_sws_set_ubuf_mgr(upipe, mgr);
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_sws_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    switch (command) {
        /* generic commands */
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_sws_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return _upipe_sws_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sws_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_sws_set_output(upipe, output);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_sws_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_sws_set_flow_def(upipe, flow);
        }

        /* specific commands */
        case UPIPE_SWS_GET_SIZE: {
            int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_SWS_SIGNATURE);
            int *hsize_p = va_arg(args, int*);
            int *vsize_p = va_arg(args, int*);
            return _upipe_sws_get_size(upipe, hsize_p, vsize_p);
        }
        case UPIPE_SWS_SET_SIZE: {
            int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_SWS_SIGNATURE);
            int hsize = va_arg(args, int);
            int vsize = va_arg(args, int);
            return _upipe_sws_set_size(upipe, hsize, vsize);
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
static struct upipe *upipe_sws_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_sws_alloc_flow(mgr, uprobe, signature,
                                               args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    upipe_sws_init_ubuf_mgr(upipe);
    upipe_sws_init_output(upipe);

    upipe_sws->convert_ctx = NULL;
    upipe_sws->srcfmt = NULL;
    upipe_sws->dstfmt = NULL;
    upipe_sws->dstsize = NULL;
    upipe_sws_store_flow_def(upipe, flow_def);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sws_free(struct upipe *upipe)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    upipe_sws_clean_output(upipe);
    upipe_sws_clean_ubuf_mgr(upipe);

    if (upipe_sws->convert_ctx) {
        sws_freeContext(upipe_sws->convert_ctx);
    }
    free(upipe_sws->dstsize);

    upipe_throw_dead(upipe);
    upipe_sws_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_sws_mgr = {
    .signature = UPIPE_SWS_SIGNATURE,

    .upipe_alloc = upipe_sws_alloc,
    .upipe_input = upipe_sws_input,
    .upipe_control = upipe_sws_control,
    .upipe_free = upipe_sws_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for swscale pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sws_mgr_alloc(void)
{
    return &upipe_sws_mgr;
}

